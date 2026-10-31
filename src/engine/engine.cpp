#include "chess/engine.hpp"
#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <limits>

namespace chess::engine {
namespace {
constexpr int values[]={0,100,325,335,500,950,0};
int kind(std::int8_t p){return std::abs(static_cast<int>(p));}
int kingDistanceToCenter(int square) {
    int f=square&7,r=square>>3;
    return std::abs(2*f-7)+std::abs(2*r-7);
}
}
Engine::Engine(std::size_t hashMb){resizeHash(hashMb);}
void Engine::resizeHash(std::size_t mb) {
    mb=std::clamp<std::size_t>(mb,1,1024);
    // Power-of-two table avoids costly integer division in every probe.
    const auto requested=std::max<std::size_t>(1,mb*1024*1024/sizeof(TTEntry));
    tt.assign(std::bit_floor(requested),{});
}
unsigned Engine::setSyzygyPath(std::string_view path) {
    clearHash();
    return tablebase.setPath(path);
}
void Engine::clearHash(){std::fill(tt.begin(),tt.end(),TTEntry{});}
void Engine::prepareSearch(){stopped.store(false,std::memory_order_relaxed);}
void Engine::stop(){stopped.store(true,std::memory_order_relaxed);}
bool Engine::expired() {
    if(stopped.load(std::memory_order_relaxed))return true;
    if(limits.nodes&&nodes>=limits.nodes)return true;
    if(limits.timeMs>0 && (nodes&1023)==0 &&
       std::chrono::steady_clock::now()-started>=std::chrono::milliseconds(limits.timeMs))return true;
    return false;
}
bool Engine::draw(const game::Position& pos) const {
    if(pos.halfmove>=100||pos.insufficientMaterial())return true;
    int seen=0;
    // Only reversible moves can create a repetition of the current position.
    int start=std::max<int>(0,static_cast<int>(history.size())-1-pos.halfmove);
    for(int i=static_cast<int>(history.size())-2;i>=start;--i)
        if(history[i]==pos.hash && ++seen>=2)return true;
    return false;
}
int Engine::evaluate(const game::Position& pos) const {
    // Tapered classical evaluation. Features and weights are deliberately
    // conservative until calibrated with paired games against the last release.
    constexpr int mgValue[]={0,100,325,335,500,950,0};
    constexpr int egValue[]={0,115,315,335,525,940,0};
    constexpr game::Bitboard fileA=0x0101010101010101ULL;
    static const auto masks=[] {
        struct Masks {
            std::array<game::Bitboard,8> files{}, neighbours{};
            std::array<std::array<game::Bitboard,64>,2> passed{};
        } result;
        for(int f=0;f<8;++f) {
            result.files[f]=fileA<<f;
            result.neighbours[f]=(f>0?fileA<<(f-1):0) |
                                 (f<7?fileA<<(f+1):0);
        }
        for(int sq=0;sq<64;++sq) {
            const int f=sq&7,r=sq>>3;
            for(int rr=r+1;rr<8;++rr)
                result.passed[0][sq]|=(result.files[f]|result.neighbours[f]) & (0xFFULL<<(rr*8));
            for(int rr=0;rr<r;++rr)
                result.passed[1][sq]|=(result.files[f]|result.neighbours[f]) & (0xFFULL<<(rr*8));
        }
        return result;
    }();
    const auto whitePawns=pos.bitboards[0][game::Pawn-1];
    const auto blackPawns=pos.bitboards[1][game::Pawn-1];
    const auto fileH=fileA<<7;
    const std::array<game::Bitboard,2> pawnAttacks{
        ((whitePawns&~fileA)<<7)|((whitePawns&~fileH)<<9),
        ((blackPawns&~fileA)>>9)|((blackPawns&~fileH)>>7)
    };
    // Aggregate piece attacks by type. This makes king pressure and tactical
    // vulnerability cheap to evaluate without generating legal moves.
    std::array<std::array<game::Bitboard,6>,2> attackMaps{};
    static const auto knightTargets=[] {
        std::array<game::Bitboard,64> result{};
        constexpr int df[]={1,2,2,1,-1,-2,-2,-1};
        constexpr int dr[]={2,1,-1,-2,-2,-1,1,2};
        for(int sq=0;sq<64;++sq)for(int i=0;i<8;++i){
            const int f=(sq&7)+df[i],r=(sq>>3)+dr[i];
            if(f>=0&&f<8&&r>=0&&r<8)result[sq]|=1ULL<<(r*8+f);
        }
        return result;
    }();
    attackMaps[0][game::Pawn-1]=pawnAttacks[0];
    attackMaps[1][game::Pawn-1]=pawnAttacks[1];
    int mg=0,eg=0,phase=0;
    for(int sq=0;sq<64;++sq) {
        const int piece=pos.board[sq];
        if(!piece)continue;
        const bool white=piece>0;
        const int side=white?1:-1;
        const int color=white?0:1;
        const int type=kind(static_cast<std::int8_t>(piece));
        const int rank=white?(sq>>3):(7-(sq>>3));
        const int file=sq&7;
        const int center=14-kingDistanceToCenter(sq);
        int middle=0,end=0;
        const game::Bitboard ownPawns=white?whitePawns:blackPawns;
        const game::Bitboard enemyPawns=white?blackPawns:whitePawns;
        const auto pieceAttacks=type==game::Pawn?pawnAttacks[color]:
                                (type==game::King?game::Bitboard{0}:pos.attacksFrom(sq));
        if(type>=game::Knight&&type<=game::Queen)attackMaps[color][type-1]|=pieceAttacks;
        switch(type) {
            case game::Pawn: {
                middle=rank*8+center/2;
                end=rank*12+center/3;
                if(!(ownPawns&masks.neighbours[file])){middle-=13;end-=10;}
                if(std::popcount(ownPawns&masks.files[file])>1){middle-=11;end-=15;}
                // Supported pawn chains are more robust than lone pawns.
                if(pawnAttacks[color]&(1ULL<<sq)){middle+=8;end+=9;}
                const int front=sq+(white?8:-8);
                if(front>=0&&front<64 && (pawnAttacks[1-color]&(1ULL<<front)) &&
                   !(pawnAttacks[color]&(1ULL<<sq)) &&
                   !(ownPawns&masks.neighbours[file])) {
                    middle-=10;end-=7;
                }
                // A one-push pawn fork is an immediate tactical opportunity.
                if(front>=0&&front<64 && pos.board[front]==0) {
                    const int targetRank=rank+2;
                    if(targetRank<=7) {
                        int valuables=0;
                        for(int df : {-1,1}) {
                            const int targetFile=file+df;
                            if(targetFile<0||targetFile>7)continue;
                            const int target=front+(white?8:-8)+df;
                            if(target<0||target>=64)continue;
                            const int enemy=kind(pos.board[target]);
                            if(pos.board[target] && (pos.board[target]>0)!=white &&
                               enemy>=game::Knight)++valuables;
                        }
                        if(valuables>=2){middle+=22;end+=14;}
                    }
                }
                if(!(enemyPawns&masks.passed[color][sq])) {
                    middle+=rank*rank*2;
                    end+=rank*rank*4;
                }
                // Connected pawns hold space and are harder to attack.
                const int backward=sq+(white?-8:8);
                if(backward>=0 && backward<64 &&
                   (ownPawns & masks.neighbours[file] &
                    (0xFFULL<<((backward>>3)*8)))) {
                    middle+=4;end+=5;
                }
                break;
            }
            case game::Knight: {
                phase+=1;
                const int mobility=std::popcount(pieceAttacks&~pos.occupied[color]);
                middle=center*5+3*(mobility-4);
                end=center*3+2*(mobility-4);
                // Static fork awareness: both colors are evaluated symmetrically,
                // so an opponent's looming knight fork becomes a defensive penalty.
                const auto enemyValuables=pos.bitboards[1-color][game::Rook-1] |
                                          pos.bitboards[1-color][game::Queen-1] |
                                          pos.bitboards[1-color][game::King-1];
                const auto otherTargets=pos.bitboards[1-color][game::Knight-1] |
                                         pos.bitboards[1-color][game::Bishop-1];
                const auto victims=enemyValuables|otherTargets;
                if(std::popcount(pieceAttacks&victims)>=2 &&
                   (pieceAttacks&enemyValuables)){middle+=23;end+=18;}
                int reachableForks=0;
                game::Bitboard destinations=pieceAttacks&~pos.occupied[color]&~pawnAttacks[1-color];
                while(destinations) {
                    const int target=std::countr_zero(destinations);
                    destinations&=destinations-1;
                    const auto threats=knightTargets[target]&victims;
                    if(std::popcount(threats)>=2 && (threats&enemyValuables))++reachableForks;
                }
                middle+=std::min(22,reachableForks*5);
                end+=std::min(12,reachableForks*3);
                // Pawns defending a central knight outpost.
                if(rank>=3 && rank<=5) {
                    const int supportRank=(sq>>3)+(white?-1:1);
                    if(supportRank>=0 && supportRank<8) {
                        game::Bitboard defenders=ownPawns&masks.neighbours[file]&
                                                (0xFFULL<<(supportRank*8));
                        if(defenders && !(enemyPawns&masks.passed[color][sq])) {
                            middle+=16;end+=10;
                        }
                    }
                }
                break;
            }
            case game::Bishop: {
                phase+=1;
                const int mobility=std::popcount(pieceAttacks&~pos.occupied[color]);
                middle=center*3+2*(mobility-7);
                end=center*3+3*(mobility-7);
                break;
            }
            case game::Rook: {
                phase+=2;
                if(!(ownPawns&masks.files[file])) {
                    middle+=(enemyPawns&masks.files[file])?7:15;
                    end+=12;
                }
                if(rank==6){middle+=15;end+=18;}
                break;
            }
            case game::Queen:middle=center;end=center*2;phase+=4;break;
            case game::King: {
                middle=-center*4;
                end=center*5;
                // King pawn shield: penalize open files near the king while
                // major pieces remain. Fade this term out in the endgame.
                int protection=0;
                const int kingRank=sq>>3;
                for(int df=-1;df<=1;++df) {
                    const int f=file+df;
                    if(f<0||f>7)continue;
                    bool shield=false;
                    for(int step=1;step<=2;++step) {
                        const int r=kingRank+(white?step:-step);
                        if(r<0||r>7)break;
                        if(ownPawns&(1ULL<<(r*8+f))) {shield=true;break;}
                    }
                    if(shield)++protection;
                }
                middle+=(protection-3)*12;
                break;
            }
            default:break;
        }
        mg+=side*(mgValue[type]+middle);
        eg+=side*(egValue[type]+end);
    }
    for(int color=0;color<2;++color) {
        // Count attacked squares immediately surrounding our king, weighting
        // heavy-piece pressure and multiple attacking piece types. Unlike a
        // pure pawn-shield term this detects open-file and piece-based attacks.
        const int kingSq=std::countr_zero(pos.bitboards[color][game::King-1]);
        const int kingFile=kingSq&7,kingRank=kingSq>>3;
        game::Bitboard zone=0;
        for(int df=-1;df<=1;++df)for(int dr=-1;dr<=1;++dr) {
            const int f=kingFile+df,r=kingRank+dr;
            if(f>=0&&f<8&&r>=0&&r<8)zone|=1ULL<<(r*8+f);
        }
        const auto& enemy=attackMaps[1-color];
        const int pawnHits=std::popcount(zone&enemy[game::Pawn-1]);
        const int knightHits=std::popcount(zone&enemy[game::Knight-1]);
        const int bishopHits=std::popcount(zone&enemy[game::Bishop-1]);
        const int rookHits=std::popcount(zone&enemy[game::Rook-1]);
        const int queenHits=std::popcount(zone&enemy[game::Queen-1]);
        const int attackers=(knightHits>0)+(bishopHits>0)+(rookHits>0)+(queenHits>0);
        const int pressure=pawnHits+3*knightHits+2*bishopHits+3*rookHits+4*queenHits;
        const int kingPenalty=std::min(125,pressure*(1+attackers)/2);
        mg+=(color==0?-1:1)*kingPenalty;
        if(std::popcount(pos.bitboards[color][game::Bishop-1])>=2) {
            const int side=color==0?1:-1;
            mg+=side*28;eg+=side*35;
        }
    }
    phase=std::min(24,phase);
    const int score=(mg*phase+eg*(24-phase))/24;
    return pos.whiteToMove?score:-score;
}
namespace {
// Least-valuable LEGAL recaptures. Unlike an occupancy-only swap-list SEE,
// this does not count pinned pieces or a king walking onto an attacked square.
// This is called only when ordering potentially losing captures, not for every
// candidate in the search tree.
int recaptureValue(game::Position& pos,int target,int victim,int ply) {
    if(ply>=24)return 0;
    const int color=pos.whiteToMove?0:1;
    const auto occ=pos.occupied[0]|pos.occupied[1];
    const auto available=pos.attackersTo(target,pos.whiteToMove,occ);
    for(int type=game::Pawn;type<=game::King;++type) {
        auto candidates=available&pos.bitboards[color][type-1];
        while(candidates) {
            const int from=std::countr_zero(candidates);
            candidates&=candidates-1;
            const bool promotes=type==game::Pawn && ((target>>3)==0 || (target>>3)==7);
            game::Move move{static_cast<std::uint8_t>(from),
                            static_cast<std::uint8_t>(target),
                            static_cast<std::uint8_t>(promotes?game::Queen:0),game::Normal};
            const bool mover=pos.whiteToMove;
            auto undo=pos.make(move);
            const bool legal=!pos.inCheck(mover);
            if(legal) {
                const int promotionBonus=promotes?values[game::Queen]-values[game::Pawn]:0;
                const int newVictim=values[promotes?game::Queen:type];
                const int gain=victim+promotionBonus-recaptureValue(pos,target,newVictim,ply+1);
                pos.unmake(move,undo);
                return std::max(0,gain); // Either capture or decline the exchange.
            }
            pos.unmake(move,undo);
        }
    }
    return 0;
}
}
int Engine::see(const game::Position& pos,game::Move move) const {
    const int captured=move.flag==game::EnPassant?game::Pawn:kind(pos.board[move.to]);
    const int promotionBonus=move.promotion?values[move.promotion]-values[game::Pawn]:0;
    game::Position copy=pos;
    const bool mover=copy.whiteToMove;
    auto undo=copy.make(move);
    if(copy.inCheck(mover))return -30000;
    const int victim=values[move.promotion?move.promotion:kind(undo.moved)];
    return values[captured]+promotionBonus-recaptureValue(copy,move.to,victim,0);
}
int Engine::moveScore(const game::Position& pos, game::Move m, game::Move hashMove,int ply) const {
    if(m==hashMove && static_cast<bool>(hashMove))return 1000000;
    int victim=kind(pos.board[m.to]);
    if(m.flag==game::EnPassant)victim=game::Pawn;
    if(m.promotion)return 110000+values[m.promotion]+values[victim]*16;
    if(victim) {
        int estimate=values[victim]*16-values[kind(pos.board[m.from])];
        // Losing exchanges are searched after quiet killer/history moves.
        if(values[victim]<values[kind(pos.board[m.from])] && see(pos,m)<0)
            return -15000+estimate;
        return 100000+estimate;
    }
    if(m==killers[ply][0])return 80000;
    if(m==killers[ply][1])return 79000;
    return moveHistory[pos.whiteToMove?0:1][m.from][m.to];
}
int Engine::quiescence(game::Position& pos,int alpha,int beta,int ply) {
    if(expired())return 0;
    ++nodes;
    bool check=pos.inCheck(pos.whiteToMove);
    // Checkmate wins over a possible draw by clock/repetition.
    if(draw(pos) && !(check && !pos.hasLegalMove()))return 0;
    if(ply>=MaxPly-1)return check && !pos.hasLegalMove()?-Mate+ply:evaluate(pos);
    int stand=-Mate;
    if(!check) {
        stand=evaluate(pos);
        // Stand-pat is not legal in a stalemate, so validate before a cutoff.
        if(stand>=beta)return pos.hasLegalMove()?beta:0;
        alpha=std::max(alpha,stand);
    }
    game::MoveList moves;
    // Generate only captures/promotions; checks still require every evasion.
    pos.pseudoMoves(moves,!check);
    std::array<int,game::MAX_MOVES> scores{};
    for(int i=0;i<moves.size;++i)scores[i]=moveScore(pos,moves[i],{},ply);
    int legal=0;
    for(int i=0;i<moves.size;++i) {
        int best=i;
        for(int j=i+1;j<moves.size;++j)if(scores[j]>scores[best])best=j;
        std::swap(moves.data[i],moves.data[best]);
        std::swap(scores[i],scores[best]);
        auto m=moves[i];
        const int captured=m.flag==game::EnPassant?game::Pawn:kind(pos.board[m.to]);
        if(!check && !captured && !m.promotion)continue;
        bool mover=pos.whiteToMove;
        auto undo=pos.make(m);
        if(pos.inCheck(mover)){pos.unmake(m,undo);continue;}
        // Checking captures can be tactical even when their immediate material
        // value is small; never apply delta pruning to those moves.
        const bool givesCheck=pos.inCheck(pos.whiteToMove);
        if(!check && !givesCheck && !m.promotion &&
           stand+values[captured]+250<alpha) {
            pos.unmake(m,undo);
            continue;
        }
        ++legal;
        history.push_back(pos.hash);
        int score=-quiescence(pos,-beta,-alpha,ply+1);
        history.pop_back();pos.unmake(m,undo);
        if(expired())return 0;
        if(score>=beta)return beta;
        alpha=std::max(alpha,score);
    }
    if(check && !legal)return -Mate+ply;
    // Captures exist in almost every tactical leaf. Only check for a quiet
    // stalemate when *none* is legal; this avoids an expensive full legal
    // move scan at most quiescence nodes.
    if(!check && !legal && !pos.hasLegalMove())return 0;
    return alpha;
}
int Engine::negamax(game::Position& pos,int depth,int alpha,int beta,int ply,bool allowNull) {
    if(expired())return 0;
    ++nodes;
    bool inCheck=pos.inCheck(pos.whiteToMove);
    if(draw(pos) && !(inCheck && !pos.hasLegalMove()))return 0;
    if(ply>=MaxPly-1)return inCheck && !pos.hasLegalMove()?-Mate+ply:evaluate(pos);
    if(auto tb=tablebase.probeWdl(pos)) return *tb;
    if(depth<=0)return quiescence(pos,alpha,beta,ply);
    if(inCheck)++depth; // check extension
    const int originalAlpha=alpha;
    const bool pvNode=beta-alpha>1;
    // Only prune late, quiet, nonchecking moves at the shallowest non-PV
    // frontier, and only after a legal move has already been fully searched.
    const bool futile=depth==1 && !pvNode && !inCheck &&
                      alpha<Mate-256 && evaluate(pos)+180<=alpha;
    auto& entry=tt[pos.hash&(tt.size()-1)];
    game::Move hashMove{};
    // At high 50-move counts the position's halfmove clock matters but is not hashed.
    const bool ttSafe=pos.halfmove<80;
    if(ttSafe && entry.key==pos.hash) {
        hashMove=entry.move;
        if(entry.depth>=depth && ply>0) {
            int cached=entry.score;
            if(cached>Mate-512)cached-=ply;
            else if(cached<-Mate+512)cached+=ply;
            if(entry.bound==0||(entry.bound==1&&cached>=beta)||(entry.bound==2&&cached<=alpha))return cached;
        }
    }
    // Null-move pruning: skip only with a non-pawn piece to avoid basic zugzwang.
    // Skip PV nodes/check and high halfmove counts; verification via normal search on fail.
    if(allowNull && !pvNode && !inCheck && depth>=4 && ply>0 && pos.halfmove<90 &&
       (pos.bitboards[pos.whiteToMove?0:1][game::Knight-1] ||
        pos.bitboards[pos.whiteToMove?0:1][game::Bishop-1] ||
        pos.bitboards[pos.whiteToMove?0:1][game::Rook-1] ||
        pos.bitboards[pos.whiteToMove?0:1][game::Queen-1]) && evaluate(pos)>=beta) {
        auto undo=pos.makeNull();
        history.push_back(pos.hash);
        int value=-negamax(pos,depth-4,-beta,-beta+1,ply+1,false);
        history.pop_back();pos.unmakeNull(undo);
        if(expired())return 0;
        if(value>=beta)return beta;
    }
    game::MoveList moves;pos.pseudoMoves(moves);
    std::array<int,game::MAX_MOVES> scores{};
    for(int i=0;i<moves.size;++i)scores[i]=moveScore(pos,moves[i],hashMove,ply);
    int best=-Mate-100,legal=0;
    game::Move bestMove{};
    game::MoveList triedQuiets;
    for(int i=0;i<moves.size;++i) {
        int chosen=i;
        for(int j=i+1;j<moves.size;++j)if(scores[j]>scores[chosen])chosen=j;
        std::swap(moves.data[i],moves.data[chosen]);
        std::swap(scores[i],scores[chosen]);
        auto m=moves[i];
        bool mover=pos.whiteToMove;
        auto undo=pos.make(m);
        if(pos.inCheck(mover)){pos.unmake(m,undo);continue;}
        ++legal;
        history.push_back(pos.hash);
        bool quiet=undo.captured==0 && m.promotion==0;
        const bool givesCheck=pos.inCheck(pos.whiteToMove);
        if(futile && legal>1 && best>-Mate+512 && quiet &&
           kind(undo.moved)!=game::Pawn && !givesCheck) {
            history.pop_back();pos.unmake(m,undo);
            continue;
        }
        int scoreValue;
        if(legal==1) scoreValue=-negamax(pos,depth-1,-beta,-alpha,ply+1);
        else {
            int reduction=0;
            if(depth>=3 && legal>=4 && quiet && !inCheck && !givesCheck) {
                reduction=1;
                if(depth>=7 && legal>=8)++reduction;
                if(depth>=11 && legal>=14)++reduction;
                // Preserve the principal variation, and never reduce below 1 ply.
                if(pvNode && reduction>1)--reduction;
                reduction=std::min(reduction,depth-2);
            }
            scoreValue=-negamax(pos,depth-1-reduction,-alpha-1,-alpha,ply+1);
            if(scoreValue>alpha && reduction)scoreValue=-negamax(pos,depth-1,-alpha-1,-alpha,ply+1);
            if(scoreValue>alpha && scoreValue<beta)scoreValue=-negamax(pos,depth-1,-beta,-alpha,ply+1);
        }
        history.pop_back();pos.unmake(m,undo);
        if(expired())return 0;
        if(scoreValue>best){best=scoreValue;bestMove=m;}
        if(scoreValue>alpha)alpha=scoreValue;
        if(alpha>=beta) {
            if(quiet) {
                if(!(m==killers[ply][0])){killers[ply][1]=killers[ply][0];killers[ply][0]=m;}
                const int delta=std::min(2048,depth*depth*16);
                auto& bonus=moveHistory[mover?0:1][m.from][m.to];
                bonus=std::clamp(bonus+delta,-70000,70000);
                // Quiet moves tried earlier and found inferior should not
                // repeatedly outrank a move that actually causes a cutoff.
                for(int j=0;j<triedQuiets.size;++j) {
                    auto previous=triedQuiets[j];
                    auto& h=moveHistory[mover?0:1][previous.from][previous.to];
                    h=std::clamp(h-delta/2,-70000,70000);
                }
            }
            break;
        }
        if(quiet)triedQuiets.push(m);
    }
    if(!legal)return inCheck?-Mate+ply:0;
    if(ply==0)rootBestMove=bestMove;
    // Prefer replacing old entries and shallower entries; exact scores carry the most value.
    if(ttSafe && (entry.key==pos.hash || entry.age!=ttAge ||
                  depth>=entry.depth || (best>originalAlpha && best<beta))) {
        entry.key=pos.hash;entry.move=bestMove;entry.depth=static_cast<std::int16_t>(depth);
        int stored=best;
        if(stored>Mate-512)stored+=ply;
        else if(stored<-Mate+512)stored-=ply;
        entry.score=static_cast<std::int16_t>(stored);
        entry.bound=best<=originalAlpha?2:(best>=beta?1:0);
        entry.age=ttAge;
    }
    return best;
}
std::vector<game::Move> Engine::principalVariation(game::Position pos,int length) {
    std::vector<game::Move> pv;
    for(int i=0;i<length;++i) {
        const auto& e=tt[pos.hash&(tt.size()-1)];
        if(e.key!=pos.hash||!static_cast<bool>(e.move))break;
        game::MoveList moves;pos.legalMoves(moves);
        auto m=e.move;
        bool found=false;
        for(int j=0;j<moves.size;++j)if(moves[j]==m){found=true;break;}
        if(!found)break;
        pv.push_back(m);
        pos.make(m);
    }
    return pv;
}
Result Engine::search(game::Position pos,Limits requested,
                      const std::vector<std::uint64_t>& priorHashes,
                      const InfoCallback& onInfo) {
    limits=requested;limits.depth=std::clamp(limits.depth,1,MaxPly-2);
    nodes=0;started=std::chrono::steady_clock::now();
    ++ttAge;
    history=priorHashes;
    if(history.empty()||history.back()!=pos.hash)history.push_back(pos.hash);
    std::fill(&killers[0][0],&killers[0][0]+MaxPly*2,game::Move{});
    game::MoveList initial;pos.legalMoves(initial);
    Result result;
    if(initial.size==0)return result;
    result.best=initial[0];
    // Root DTZ probe respects the halfmove clock; ordinary search is the fallback.
    if(auto move=tablebase.probeRoot(pos)) {
        result.best=move->move;
        result.info.pv={move->move};
        result.info.score=move->score;
        return result;
    }
    if(initial.size==1){result.info.pv.push_back(result.best);return result;}
    for(int depth=1;depth<=limits.depth;++depth) {
        if(expired())break;
        rootBestMove={};
        // Narrow aspiration windows improve move ordering via faster cutoffs.
        int alpha=-Mate-100,beta=Mate+100;
        if(depth>=4 && result.info.depth>=3) {
            alpha=std::max(-Mate-100,result.info.score-35);
            beta=std::min(Mate+100,result.info.score+35);
        }
        int score=negamax(pos,depth,alpha,beta,0);
        if(expired())break;
        if(score<=alpha || score>=beta) {
            // Full-window retry: do not publish an unconfirmed aspiration result.
            score=negamax(pos,depth,-Mate-100,Mate+100,0);
        }
        if(expired())break; // Preserve previous fully completed depth.
        auto pv=principalVariation(pos,depth);
        if(rootBestMove) {
            // Root move is authoritative; TT entries may be suppressed near a
            // 50-move draw, or a replacement collision can remove the root PV.
            result.best=rootBestMove;
            if(pv.empty() || !(pv.front()==rootBestMove)) {
                pv={rootBestMove};
                auto next=pos;
                next.make(rootBestMove);
                auto tail=principalVariation(next,depth-1);
                pv.insert(pv.end(),tail.begin(),tail.end());
            }
        }
        result.info={depth,score,nodes,static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()),std::move(pv)};
        if(onInfo)onInfo(result.info);
        if(std::abs(score)>=Mate-128)break;
        // Avoid beginning a likely unfinished iteration near the hard clock
        // limit. A completed iteration is always preferable to a timeout.
        if(limits.timeMs>0 && depth>=2 &&
           result.info.elapsedMs*4>=static_cast<std::uint64_t>(limits.timeMs)*3)break;
    }
    return result;
}
} // namespace chess::engine
