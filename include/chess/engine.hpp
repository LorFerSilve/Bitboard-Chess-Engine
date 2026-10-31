#pragma once

#include "chess/game.hpp"
#include "chess/tablebase.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace chess::engine {
struct Limits {
    int depth=64;
    int timeMs=0; // 0 means no clock limit
    std::uint64_t nodes=0; // 0 means unlimited
};
struct SearchInfo {
    int depth=0;
    int score=0; // centipawns from side-to-move perspective; mate encoded near +/-30000
    std::uint64_t nodes=0;
    std::uint64_t elapsedMs=0;
    std::vector<game::Move> pv;
};
struct Result {
    game::Move best{};
    SearchInfo info{};
};
using InfoCallback=std::function<void(const SearchInfo&)>;

class Engine {
public:
    explicit Engine(std::size_t hashMb=32);
    void resizeHash(std::size_t mb);
    void clearHash();
    unsigned setSyzygyPath(std::string_view path);
    unsigned syzygyPieces() const { return tablebase.maxPieces(); }
    // Must be called by the owner before launching a new asynchronous search.
    // search() itself does NOT reset the flag: stop requests must never be lost.
    void prepareSearch();
    void stop();
    Result search(game::Position position, Limits limits={},
                  const std::vector<std::uint64_t>& priorHashes={},
                  const InfoCallback& info={});
private:
    static constexpr int MaxPly=128;
    static constexpr int Mate=30000;
    struct TTEntry {
        std::uint64_t key=0;
        game::Move move{};
        std::int16_t score=0;
        std::int16_t depth=-1;
        std::uint8_t bound=0; // 0 exact, 1 lower, 2 upper
        std::uint8_t age=0;
    };
    // Four-entry clusters reduce destructive hash collisions.
    static constexpr std::size_t TTClusterSize=4;
    std::vector<TTEntry> tt;
    Tablebase tablebase;
    std::uint8_t ttAge=0;
    game::Move rootBestMove{};
    std::atomic<bool> stopped{false};
    std::chrono::steady_clock::time_point started;
    Limits limits;
    std::uint64_t nodes=0;
    std::vector<std::uint64_t> history;
    game::Move killers[MaxPly][2]{};
    int moveHistory[2][64][64]{};

    bool expired();
    bool draw(const game::Position& pos) const;
    int evaluate(const game::Position& pos) const;
    int see(const game::Position& pos, game::Move move) const;
    int moveScore(const game::Position& pos, game::Move m, game::Move hashMove, int ply) const;
    int quiescence(game::Position& pos, int alpha, int beta, int ply);
    int negamax(game::Position& pos, int depth, int alpha, int beta, int ply, bool allowNull=true);
    std::vector<game::Move> principalVariation(game::Position pos, int length);
};
} // namespace chess::engine
