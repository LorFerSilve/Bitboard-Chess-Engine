#include "chess/game.hpp"
#include "chess/engine.hpp"
#include <stdexcept>
#define CHECK(cond) do { if (!(cond)) throw std::runtime_error("Test failed: " #cond); } while(false)
#include <iostream>
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
    std::cout<<"All game/search tests passed\n";
}
