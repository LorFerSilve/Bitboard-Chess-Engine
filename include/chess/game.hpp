#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace chess::game {
using Bitboard = std::uint64_t;
constexpr int MAX_MOVES = 256;

// a1=0, h1=7, a8=56, h8=63. Positive pieces are white.
enum Piece : std::int8_t { Empty=0, Pawn=1, Knight=2, Bishop=3, Rook=4, Queen=5, King=6 };
enum Flag : std::uint8_t { Normal, EnPassant, Castle, DoublePush };

struct Move {
    std::uint8_t from=0, to=0, promotion=0, flag=Normal;
    bool operator==(const Move&) const = default;
    explicit operator bool() const { return from!=to || promotion!=0 || flag!=Normal; }
    std::string uci() const;
};

struct MoveList {
    std::array<Move,MAX_MOVES> data{};
    int size=0;
    void push(Move m);
    Move operator[](int i) const { return data[i]; }
};

struct Undo {
    std::int8_t moved=0, captured=0;
    std::int8_t ep=-1;
    std::uint8_t castling=0;
    std::uint16_t halfmove=0, fullmove=1;
    std::uint64_t hash=0;
};

class Position {
public:
    std::array<std::int8_t,64> board{};
    std::array<std::array<Bitboard,6>,2> bitboards{};
    std::array<Bitboard,2> occupied{};
    bool whiteToMove=true;
    std::uint8_t castling=15; // K Q k q
    std::int8_t ep=-1;
    std::uint16_t halfmove=0, fullmove=1;
    std::uint64_t hash=0;

    static Position fromFen(std::string_view fen);
    static Position start();
    std::string fen() const;
    bool attacked(int square, bool byWhite) const;
    bool inCheck(bool white) const;
    void pseudoMoves(MoveList& out) const;
    void legalMoves(MoveList& out);
    bool playUci(std::string_view move);
    Undo make(Move move);
    void unmake(Move move, Undo undo);
    std::uint64_t perft(int depth);
    bool insufficientMaterial() const;

private:
    void put(int square, std::int8_t piece);
    std::int8_t remove(int square);
    void rehash();
};
} // namespace chess::game
