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
    auto tactical=game::Position::fromFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    CHECK(tactical.perft(4)==43238);
    auto castleAndPromotion=game::Position::fromFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    CHECK(castleAndPromotion.perft(3)==9467);
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
    engine::Engine e(1);
    engine::Limits l;l.depth=2;
    auto result=e.search(game::Position::start(),l);
    CHECK(static_cast<bool>(result.best));
    std::cout<<"All game/search tests passed\n";
}
