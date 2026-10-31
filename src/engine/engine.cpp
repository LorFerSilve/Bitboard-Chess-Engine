#include "chess/engine.hpp"
#include <algorithm>
#include <bit>
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
    int score=0;
    for(int sq=0;sq<64;++sq) {
        int piece=pos.board[sq];
        if(!piece)continue;
        bool white=piece>0;
        int type=kind(static_cast<std::int8_t>(piece));
        int rank=white?(sq>>3):(7-(sq>>3));
        int center=14-kingDistanceToCenter(sq);
        int bonus=0;
        switch(type) {
            case game::Pawn:bonus=rank*9+center/2;break;
            case game::Knight:bonus=center*5;break;
            case game::Bishop:bonus=center*3;break;
            case game::Rook:bonus=rank*2;break;
            case game::Queen:bonus=center;break;
            case game::King:bonus=-center*3;break;
            default:break;
        }
        score+=(white?1:-1)*(values[type]+bonus);
    }
    return pos.whiteToMove?score:-score;
}
int Engine::moveScore(const game::Position& pos, game::Move m, game::Move hashMove,int ply) const {
    if(m==hashMove && static_cast<bool>(hashMove))return 1000000;
    int victim=kind(pos.board[m.to]);
    if(m.flag==game::EnPassant)victim=game::Pawn;
    if(victim)return 100000+values[victim]*16-values[kind(pos.board[m.from])];
    if(m.promotion)return 90000+values[m.promotion];
    if(m==killers[ply][0])return 80000;
    if(m==killers[ply][1])return 79000;
    return moveHistory[pos.whiteToMove?0:1][m.from][m.to];
}
int Engine::quiescence(game::Position& pos,int alpha,int beta,int ply) {
    if(expired())return 0;
    ++nodes;
    if(draw(pos))return 0;
    bool check=pos.inCheck(pos.whiteToMove);
    if(ply>=MaxPly-1)return evaluate(pos);
    if(!check) {
        int stand=evaluate(pos);
        if(stand>=beta)return beta;
        alpha=std::max(alpha,stand);
    }
    game::MoveList moves;pos.pseudoMoves(moves);
    // Stable, bounded in-place selection sorting; no heap allocations.
    for(int i=0;i<moves.size;++i) {
        int best=i,bs=moveScore(pos,moves[i],{},ply);
        for(int j=i+1;j<moves.size;++j) {
            int s=moveScore(pos,moves[j],{},ply);
            if(s>bs){bs=s;best=j;}
        }
        std::swap(moves.data[i],moves.data[best]);
    }
    int legal=0;
    for(int i=0;i<moves.size;++i) {
        auto m=moves[i];
        if(!check && pos.board[m.to]==0 && m.flag!=game::EnPassant && !m.promotion)continue;
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
int Engine::negamax(game::Position& pos,int depth,int alpha,int beta,int ply) {
    if(expired())return 0;
    ++nodes;
    if(draw(pos))return 0;
    if(ply>=MaxPly-1)return evaluate(pos);
    bool inCheck=pos.inCheck(pos.whiteToMove);
    if(depth<=0)return quiescence(pos,alpha,beta,ply);
    if(inCheck)++depth; // check extension
    const int originalAlpha=alpha;
    auto& entry=tt[pos.hash%tt.size()];
    game::Move hashMove{};
    if(entry.key==pos.hash) {
        hashMove=entry.move;
        if(entry.depth>=depth && ply>0) {
            int cached=entry.score;
            if(cached>Mate-512)cached-=ply;
            else if(cached<-Mate+512)cached+=ply;
            if(entry.bound==0||(entry.bound==1&&cached>=beta)||(entry.bound==2&&cached<=alpha))return cached;
        }
    }
    game::MoveList moves;pos.pseudoMoves(moves);
    int best=-Mate-100,legal=0;
    game::Move bestMove{};
    for(int i=0;i<moves.size;++i) {
        int chosen=i,score=moveScore(pos,moves[i],hashMove,ply);
        for(int j=i+1;j<moves.size;++j) {
            int s=moveScore(pos,moves[j],hashMove,ply);
            if(s>score){score=s;chosen=j;}
        }
        std::swap(moves.data[i],moves.data[chosen]);
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
    entry.key=pos.hash;entry.move=bestMove;entry.depth=static_cast<std::int16_t>(depth);
    int stored=best;
    if(stored>Mate-512)stored+=ply;
    else if(stored<-Mate+512)stored-=ply;
    entry.score=static_cast<std::int16_t>(stored);
    entry.bound=best<=originalAlpha?2:(best>=beta?1:0);
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
    stopped.store(false,std::memory_order_relaxed);
    limits=requested;limits.depth=std::clamp(limits.depth,1,MaxPly-2);
    nodes=0;started=std::chrono::steady_clock::now();
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
        int score=negamax(pos,depth,-Mate-100,Mate+100,0);
        if(expired())break; // Preserve previous fully completed depth.
        auto pv=principalVariation(pos,depth);
        if(!pv.empty())result.best=pv.front();
        else if(depth==1){ // Defensive fallback: root search must provide a move.
            const auto& e=tt[pos.hash%tt.size()];
            if(e.key==pos.hash&&static_cast<bool>(e.move))result.best=e.move;
        }
        result.info={depth,score,nodes,static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()),std::move(pv)};
        if(onInfo)onInfo(result.info);
        if(std::abs(score)>=Mate-128)break;
    }
    return result;
}
} // namespace chess::engine
