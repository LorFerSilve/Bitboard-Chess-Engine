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
    tt.assign(std::max<std::size_t>(1,mb*1024*1024/sizeof(TTEntry)),{});
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
    // Tapered, handcrafted evaluation. Not a neural-network evaluation.
    constexpr int mgValue[]={0,100,325,335,500,950,0};
    constexpr int egValue[]={0,115,315,335,525,940,0};
    int mg=0,eg=0,phase=0;
    const game::Bitboard files[8]={
        0x0101010101010101ULL,0x0202020202020202ULL,
        0x0404040404040404ULL,0x0808080808080808ULL,
        0x1010101010101010ULL,0x2020202020202020ULL,
        0x4040404040404040ULL,0x8080808080808080ULL};
    const game::Bitboard whitePawns=pos.bitboards[0][game::Pawn-1];
    const game::Bitboard blackPawns=pos.bitboards[1][game::Pawn-1];
    for(int sq=0;sq<64;++sq) {
        int piece=pos.board[sq];
        if(!piece)continue;
        bool white=piece>0;
        int side=white?1:-1;
        int type=kind(static_cast<std::int8_t>(piece));
        int rank=white?(sq>>3):(7-(sq>>3));
        int file=sq&7;
        int center=14-kingDistanceToCenter(sq);
        int middle=0,end=0;
        switch(type) {
            case game::Pawn: {
                middle=rank*8+center/2;
                end=rank*12+center/3;
                game::Bitboard own=white?whitePawns:blackPawns;
                game::Bitboard enemy=white?blackPawns:whitePawns;
                game::Bitboard neighbours= (file?files[file-1]:0) | (file<7?files[file+1]:0);
                if(!(own&neighbours)){middle-=13;end-=10;}
                if(std::popcount(own&files[file])>1){middle-=11;end-=15;}
                game::Bitboard front=0;
                if(white)for(int r=(sq>>3)+1;r<8;++r)front|=0xFFULL<<(r*8);
                else for(int r=0;r<(sq>>3);++r)front|=0xFFULL<<(r*8);
                if(!(enemy&front&(neighbours|files[file]))) {
                    middle+=rank*rank*2;
                    end+=rank*rank*4;
                }
                break;
            }
            case game::Knight: middle=center*5;end=center*3;phase+=1;break;
            case game::Bishop: middle=center*3;end=center*3;phase+=1;break;
            case game::Rook: {
                phase+=2;
                game::Bitboard own=white?whitePawns:blackPawns;
                game::Bitboard enemy=white?blackPawns:whitePawns;
                if(!(own&files[file])){middle+=enemy&files[file]?7:15;end+=12;}
                if(rank==6){middle+=15;end+=18;}
                break;
            }
            case game::Queen: middle=center;end=center*2;phase+=4;break;
            case game::King: middle=-center*4;end=center*5;break;
            default:break;
        }
        mg+=side*(mgValue[type]+middle);
        eg+=side*(egValue[type]+end);
    }
    for(int color=0;color<2;++color) {
        if(std::popcount(pos.bitboards[color][game::Bishop-1])>=2) {
            int side=color==0?1:-1;
            mg+=side*28;eg+=side*35;
        }
    }
    phase=std::min(24,phase);
    int score=(mg*phase+eg*(24-phase))/24;
    return pos.whiteToMove?score:-score;
}
int Engine::see(const game::Position& pos,game::Move m) const {
    // Static exchange evaluation on one destination square. It tracks occupancy
    // and x-ray attackers without mutating the actual search position.
    const int captured=m.flag==game::EnPassant?game::Pawn:kind(pos.board[m.to]);
    int gains[32]{};
    gains[0]=values[captured]+(m.promotion?values[m.promotion]-values[game::Pawn]:0);
    game::Bitboard occ=pos.occupied[0]|pos.occupied[1];
    occ&=~(1ULL<<m.from);
    if(m.flag==game::EnPassant)occ&=~(1ULL<<(m.to+(pos.whiteToMove?-8:8)));
    occ|=1ULL<<m.to;
    int victim=m.promotion?values[m.promotion]:values[kind(pos.board[m.from])];
    bool white=!pos.whiteToMove;
    int depth=0;
    while(depth<30) {
        auto attackers=pos.attackersTo(m.to,white,occ);
        int from=-1,attackerType=0;
        for(int type=game::Pawn;type<=game::King;++type) {
            auto pieces=attackers&pos.bitboards[white?0:1][type-1];
            if(pieces){from=std::countr_zero(pieces);attackerType=type;break;}
        }
        if(from<0)break;
        const int previous=gains[depth];
        gains[++depth]=victim-previous;
        victim=values[attackerType];
        occ&=~(1ULL<<from);
        white=!white;
        if(attackerType==game::King)break;
    }
    while(depth>0){
        gains[depth-1]=-std::max(-gains[depth-1],gains[depth]);
        --depth;
    }
    return gains[0];
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
        // A quiet stalemate must not be scored as a material advantage.
        if(!pos.hasLegalMove())return 0;
        stand=evaluate(pos);
        if(stand>=beta)return beta;
        alpha=std::max(alpha,stand);
    }
    game::MoveList moves;pos.pseudoMoves(moves);
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
        // Delta pruning of clearly futile captures; never prune promotions/check evasions.
        if(!check && !m.promotion && stand+values[captured]+250<alpha)continue;
        bool mover=pos.whiteToMove;
        auto undo=pos.make(m);
        if(pos.inCheck(mover)){pos.unmake(m,undo);continue;}
        ++legal;
        history.push_back(pos.hash);
        int score=-quiescence(pos,-beta,-alpha,ply+1);
        history.pop_back();pos.unmake(m,undo);
        if(expired())return 0;
        if(score>=beta)return beta;
        alpha=std::max(alpha,score);
    }
    if(check && !legal)return -Mate+ply;
    return alpha;
}
int Engine::negamax(game::Position& pos,int depth,int alpha,int beta,int ply,bool allowNull) {
    if(expired())return 0;
    ++nodes;
    bool inCheck=pos.inCheck(pos.whiteToMove);
    if(draw(pos) && !(inCheck && !pos.hasLegalMove()))return 0;
    if(ply>=MaxPly-1)return inCheck && !pos.hasLegalMove()?-Mate+ply:evaluate(pos);
    if(depth<=0)return quiescence(pos,alpha,beta,ply);
    if(inCheck)++depth; // check extension
    const int originalAlpha=alpha;
    const bool pvNode=beta-alpha>1;
    auto& entry=tt[pos.hash%tt.size()];
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
        int scoreValue;
        if(legal==1) scoreValue=-negamax(pos,depth-1,-beta,-alpha,ply+1);
        else {
            int reduction=(depth>=3 && legal>=4 && quiet && !inCheck && !pos.inCheck(pos.whiteToMove))?1:0;
            scoreValue=-negamax(pos,depth-1-reduction,-alpha-1,-alpha,ply+1);
            if(scoreValue>alpha && reduction)scoreValue=-negamax(pos,depth-1,-alpha-1,-alpha,ply+1);
            if(scoreValue>alpha && scoreValue<beta)scoreValue=-negamax(pos,depth-1,-beta,-alpha,ply+1);
        }
        history.pop_back();pos.unmake(m,undo);
        if(expired())return 0;
        if(scoreValue>best){best=scoreValue;bestMove=m;}
        if(scoreValue>alpha)alpha=scoreValue;
        if(alpha>=beta) {
            if(quiet){
                if(!(m==killers[ply][0])){killers[ply][1]=killers[ply][0];killers[ply][0]=m;}
                auto& bonus=moveHistory[mover?0:1][m.from][m.to];
                bonus=std::min(70000,bonus+depth*depth);
            }
            break;
        }
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
        const auto& e=tt[pos.hash%tt.size()];
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
    }
    return result;
}
} // namespace chess::engine
