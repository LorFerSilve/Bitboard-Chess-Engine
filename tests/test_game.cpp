#include "chess/game.hpp"
#include "chess/engine.hpp"
#include <stdexcept>
#define CHECK(cond) do { if (!(cond)) throw std::runtime_error("Test failed: " #cond); } while(false)
#include <iostream>
#include <sstream>
#include <vector>

int main() {
    using namespace chess;
    auto p=game::Position::start();
    const auto start=p.fen();
    CHECK(start=="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(p.perft(1)==20);
    CHECK(p.perft(2)==400);
    CHECK(p.perft(3)==8902);
    CHECK(p.perft(4)==197281);
    CHECK(p.perft(5)==4865609);
    // Attack masks include defended friendly squares and all ray squares
    // through the first blocker, for use by the classical evaluation.
    CHECK((p.attacksFrom(1) & (1ULL<<16)) != 0); // b1 knight attacks a3
    CHECK((p.attacksFrom(0) & (1ULL<<8)) != 0);  // a1 rook defends a2
    CHECK((p.attacksFrom(12) & (1ULL<<19)) != 0); // e2 pawn attacks d3
    CHECK(p.attacksFrom(28)==0);                  // empty e4
    auto tactical=game::Position::fromFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    CHECK(tactical.perft(4)==43238);
    CHECK(tactical.perft(5)==674624);
    // The dedicated tactical generator must match the full pseudo generator
    // after filtering captures/promotions, including quiet promotions and EP.
    for(const auto* fen : {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"
    }) {
        auto sample=game::Position::fromFen(fen);
        game::MoveList all,captures;
        sample.pseudoMoves(all);
        sample.pseudoMoves(captures,true);
        int n=0;
        for(int i=0;i<all.size;++i) {
            auto m=all[i];
            if(m.promotion || m.flag==game::EnPassant || sample.board[m.to]) {
                CHECK(n<captures.size && captures[n]==m);
                ++n;
            }
        }
        CHECK(captures.size==n);
    }
    auto castleAndPromotion=game::Position::fromFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    CHECK(castleAndPromotion.perft(3)==9467);
    CHECK(castleAndPromotion.perft(4)==422333);
    CHECK(p.fen()==start);
    auto hash=p.hash;
    game::MoveList legal;p.legalMoves(legal);
    for(int i=0;i<legal.size;++i) {
        auto undo=p.make(legal[i]);p.unmake(legal[i],undo);
        CHECK(p.fen()==start&&p.hash==hash);
    }
    CHECK(p.playUci("e2e4"));
    CHECK(p.fen()=="rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    CHECK(!p.playUci("e2e4"));
    // An uncapturable EP field must not change the repetition hash.
    auto withoutEp=game::Position::fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    CHECK(withoutEp.hash==p.hash);
    // Cross-check incremental board state and Zobrist hashing after typical play.
    auto sample=game::Position::start();
    for (auto uci : {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4"}) {
        CHECK(sample.playUci(uci));
        CHECK(game::Position::fromFen(sample.fen()).hash == sample.hash);
    }
    auto ep=game::Position::fromFen("8/4k3/8/3pP3/8/8/8/4K3 w - d6 0 1");
    CHECK(ep.playUci("e5d6"));
    CHECK(ep.board[43]==game::Pawn && ep.board[35]==0);
    auto castle=game::Position::fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    CHECK(castle.playUci("e1g1"));
    CHECK(castle.board[6]==game::King&&castle.board[5]==game::Rook&&castle.board[7]==0);
    auto promote=game::Position::fromFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    CHECK(promote.playUci("a7a8q"));
    CHECK(promote.board[56]==game::Queen);
    bool invalid=false;
    try{game::Position::fromFen("invalid");}catch(const std::invalid_argument&){invalid=true;}
    CHECK(invalid);
    for(auto badFen : {
        "4k3/8/8/8/8/8/8/4K3 w K - 0 1", // no rook for right
        "4k3/8/8/8/8/8/8/4K3 w - d6 0 1", // no double-moved pawn
        "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1" // previous side is in check
    }) {
        bool rejected=false;
        try{(void)game::Position::fromFen(badFen);}catch(const std::invalid_argument&){rejected=true;}
        CHECK(rejected);
    }
    engine::Engine e(1);
    engine::Limits l;l.depth=2;
    auto result=e.search(game::Position::start(),l);
    CHECK(static_cast<bool>(result.best));
    // Pinned en-passant is not a legal capture and does not affect repetition.
    auto pinned=game::Position::fromFen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    auto pinnedNoEp=game::Position::fromFen("k3r3/8/8/3pP3/8/8/8/4K3 w - - 0 1");
    CHECK(pinned.hash==pinnedNoEp.hash);
    CHECK(!pinned.playUci("e5d6"));
    auto pinnedHash=pinned.hash;
    auto nullUndo=pinned.makeNull();
    CHECK(!pinned.whiteToMove);
    pinned.unmakeNull(nullUndo);
    CHECK(pinned.whiteToMove && pinned.hash==pinnedHash && pinned.fen()=="k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");

    // No legal moves in a stalemate; king can be in check without legal moves.
    auto stalemate=game::Position::fromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    auto mate=game::Position::fromFen("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(!stalemate.hasLegalMove() && !stalemate.inCheck(false));
    CHECK(!mate.hasLegalMove() && mate.inCheck(false));
    auto mateInOne=game::Position::fromFen("7k/8/5KQ1/8/8/8/8/8 w - - 99 1");
    e.prepareSearch();
    auto mated=e.search(mateInOne,l);
    CHECK(mated.best.uci()=="g6g7" && mated.info.score>29900);
    auto drawn=e.search(stalemate,l);
    CHECK(!drawn.best);
    auto lost=e.search(mate,l);
    CHECK(!lost.best);

    // Deterministic multi-ply make/unmake plus full hash consistency.
    auto randomPosition=game::Position::start();
    const auto originalFen=randomPosition.fen();
    const auto originalHash=randomPosition.hash;
    std::vector<std::pair<game::Move,game::Undo>> states;
    for(int ply=0;ply<70;++ply) {
        game::MoveList options;randomPosition.legalMoves(options);
        if(options.size==0)break;
        auto move=options[(ply*37+11)%options.size];
        auto undo=randomPosition.make(move);
        states.emplace_back(move,undo);
        CHECK(game::Position::fromFen(randomPosition.fen()).hash==randomPosition.hash);
    }
    while(!states.empty()) {
        auto [move,undo]=states.back();states.pop_back();
        randomPosition.unmake(move,undo);
        CHECK(game::Position::fromFen(randomPosition.fen()).hash==randomPosition.hash);
    }
    CHECK(randomPosition.fen()==originalFen && randomPosition.hash==originalHash);
    // Threefold repetition must be recognized using real game history.
    // In particular this remains correct after null-move pruning attempts.
    auto repeated=game::Position::start();
    std::vector<std::uint64_t> previous{repeated.hash};
    for(const auto* uci:{"g1f3","g8f6","f3g1","f6g8",
                         "g1f3","g8f6","f3g1","f6g8"}) {
        CHECK(repeated.playUci(uci));
        previous.push_back(repeated.hash);
    }
    CHECK(repeated.hash==game::Position::start().hash);
    engine::Limits repetitionLimits;repetitionLimits.depth=5;
    e.prepareSearch();
    auto repeatedSearch=e.search(repeated,repetitionLimits,previous);
    CHECK(repeatedSearch.info.depth==5);
    CHECK(repeatedSearch.info.score==0);
    CHECK(static_cast<bool>(repeatedSearch.best));
    // Fastchess 600-game confirmation reported PVs that continued beyond a
    // third occurrence of a position (both v0.5 and experimental v0.6).
    // Replay one real game from that log; validate *every* reported iteration,
    // not just the final bestmove. Reported moves must stay legal and stop
    // as soon as a threefold/50-move/insufficient-material draw is reached.
    auto pvPosition=game::Position::fromFen(
        "rnbqkb1r/1p2pppp/p2p1n2/8/3NP3/2N4P/PPP2PP1/R1BQKB1R b KQkq - 0 1");
    std::vector<std::uint64_t> pvHistory{pvPosition.hash};
    std::istringstream played(
        "e7e5 d4f3 b7b5 a2a4 b5b4 c3d5 f6e4 f1d3 e4c5 d5b4 e5e4 "
        "d3c4 e4f3 c4f7 e8f7 d1f3 f7e8 f3a8 c8b7 a8a7 b7g2 h1h2 "
        "b8c6 b4c6 g2c6 b2b4 c5a4 a7a6 d8d7 a6e2 d7e7 c2c4 e7e2 "
        "e1e2 d6d5 b4b5 a4c3 e2d3 c3b5 c4b5 c6b5 d3d4 b5c4 c1f4 "
        "h8g8 f4e5 f8e7 f2f4 g7g6 a1a7 h7h5 h2b2 e8f7 b2b7 g8e8 "
        "e5d6 f7e6 d6e7");
    for(std::string uci;played>>uci;) {
        CHECK(pvPosition.playUci(uci));
        pvHistory.push_back(pvPosition.hash);
    }
    engine::Engine pvEngine(2);
    engine::Limits pvLimits;pvLimits.depth=10;
    int observedPvs=0;
    auto validatePv=[&](const engine::SearchInfo& info) {
        if(info.pv.empty())return;
        ++observedPvs;
        auto continuation=pvPosition;
        auto hashes=pvHistory;
        for(const auto move:info.pv) {
            CHECK(continuation.halfmove<100);
            CHECK(!continuation.insufficientMaterial());
            const int start=std::max<int>(
                0,static_cast<int>(hashes.size())-1-continuation.halfmove);
            int repetitions=0;
            for(int i=static_cast<int>(hashes.size())-2;i>=start;--i) {
                if(hashes[i]==0)break;
                if(hashes[i]==continuation.hash)++repetitions;
            }
            CHECK(repetitions<2);
            game::MoveList choices;continuation.legalMoves(choices);
            bool legalPvMove=false;
            for(int i=0;i<choices.size;++i)
                if(choices[i]==move)legalPvMove=true;
            CHECK(legalPvMove);
            continuation.make(move);
            hashes.push_back(continuation.hash);
        }
    };
    pvEngine.prepareSearch();
    const auto pvResult=pvEngine.search(pvPosition,pvLimits,pvHistory,validatePv);
    CHECK(pvResult.info.depth==10 && observedPvs>0);
    validatePv(pvResult.info);
    // Exercise clustered TT lookups and replacement across search positions.
    e.clearHash();
    for(const auto* fen:{
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "rnbqkbnr/pp1ppppp/8/2p5/2P5/8/PP1PPPPP/RNBQKBNR w KQkq c6 0 2",
        "4k3/8/8/8/8/8/8/4K2Q w - - 0 1"
    }) {
        auto position=game::Position::fromFen(fen);
        game::MoveList legalChoices;position.legalMoves(legalChoices);
        e.prepareSearch();
        engine::Limits stressLimits;stressLimits.depth=4;
        const auto searched=e.search(position,stressLimits);
        CHECK(searched.info.depth==4);
        bool matches=false;
        for(int i=0;i<legalChoices.size;++i)
            if(legalChoices[i]==searched.best)matches=true;
        CHECK(matches);
    }
    // Switches must be independently writable and readable; no heuristic may
    // implicitly alter another. Search must still work with everything off.
    engine::Engine ablation(2);
    // No unvalidated v0.6 heuristic is enabled in a fresh engine.
    for(auto name:engine::HeuristicNames) {
        CHECK(!ablation.heuristicEnabled(name));
        CHECK(ablation.setHeuristic(name,false));
        CHECK(!ablation.heuristicEnabled(name));
        CHECK(ablation.setHeuristic(name,true));
        CHECK(ablation.heuristicEnabled(name));
    }
    CHECK(!ablation.setHeuristic("unknown heuristic",false));
    ablation.setAllHeuristics(false);
    for(auto name:engine::HeuristicNames)CHECK(!ablation.heuristicEnabled(name));
    engine::Limits ablationLimits;ablationLimits.depth=3;
    ablation.prepareSearch();
    auto disabledSearch=ablation.search(game::Position::start(),ablationLimits);
    CHECK(disabledSearch.info.depth==3 && static_cast<bool>(disabledSearch.best));
    CHECK(ablation.setHeuristic("King Pressure",true));
    CHECK(ablation.heuristicEnabled("King Pressure"));
    for(auto name:engine::HeuristicNames)
        if(name!="King Pressure")CHECK(!ablation.heuristicEnabled(name));
    ablation.prepareSearch();
    auto singleSearch=ablation.search(game::Position::start(),ablationLimits);
    CHECK(singleSearch.info.depth==3 && static_cast<bool>(singleSearch.best));
    // Check evasions and forcing checks must remain searchable with this
    // selective quiescence switch enabled.
    ablation.setAllHeuristics(false);
    CHECK(ablation.setHeuristic("QSearch SEE Pruning",true));
    for(const auto* fen : {
        "7k/8/5KQ1/8/8/8/8/8 w - - 99 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "8/4k3/8/3pP3/8/8/8/4K3 w - d6 0 1"
    }) {
        auto position=game::Position::fromFen(fen);
        game::MoveList legalMoves;position.legalMoves(legalMoves);
        ablation.prepareSearch();
        engine::Limits tacticalLimits;tacticalLimits.depth=4;
        auto test=ablation.search(position,tacticalLimits);
        CHECK(test.info.depth>=1 && test.info.depth<=4); // Exact-mate scores stop iterative deepening early.
        bool found=false;
        for(int i=0;i<legalMoves.size;++i)
            if(test.best==legalMoves[i])found=true;
        CHECK(found);
    }
    ablation.setAllHeuristics(true);
    for(auto name:engine::HeuristicNames)CHECK(ablation.heuristicEnabled(name));
    std::cout<<"All game/search tests passed\n";
}
