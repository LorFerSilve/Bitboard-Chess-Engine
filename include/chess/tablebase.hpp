#pragma once

#include "chess/game.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace chess::engine {
// Non-copyable because Fathom maintains process-global tablebase state.
// Configure only when the search is stopped.
class Tablebase {
public:
    Tablebase() = default;
    ~Tablebase();
    Tablebase(const Tablebase&) = delete;
    Tablebase& operator=(const Tablebase&) = delete;

    // Empty path disables probing. Returns number of pieces supported (0 if none).
    unsigned setPath(std::string_view path);
    unsigned maxPieces() const { return largest_; }

    // Exact WDL when available; otherwise nullopt. Scores are from side to move.
    std::optional<int> probeWdl(const game::Position& position) const;
    struct RootResult {
        game::Move move;
        int score = 0; // Exact WDL translated to centipawns (not distance-to-mate)
        unsigned dtz = 0;
    };
    // DTZ-informed best move at root, checked against the engine's legal move list.
    std::optional<RootResult> probeRoot(game::Position position) const;

private:
    unsigned largest_ = 0;
};
}
