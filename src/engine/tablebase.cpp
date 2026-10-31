#include "chess/tablebase.hpp"
#include <bit>
#include <cstdint>
#include <stdexcept>

#ifdef CHESS_WITH_SYZYGY
#include <tbprobe.h>
#endif

namespace chess::engine {
namespace {
#ifdef CHESS_WITH_SYZYGY
struct TablePosition {
    std::uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
    unsigned halfmove, castling, ep;
    bool whiteTurn;
};
TablePosition convert(const game::Position& p) {
    const auto both = [&](int piece) {
        return p.bitboards[0][piece-1] | p.bitboards[1][piece-1];
    };
    return {p.occupied[0], p.occupied[1], both(game::King), both(game::Queen),
            both(game::Rook), both(game::Bishop), both(game::Knight), both(game::Pawn),
            p.halfmove, p.castling, p.ep < 0 ? 0U : static_cast<unsigned>(p.ep),
            p.whiteToMove};
}
unsigned rootProbe(const TablePosition& p) {
    return tb_probe_root(p.white,p.black,p.kings,p.queens,p.rooks,p.bishops,
                         p.knights,p.pawns,p.halfmove,p.castling,p.ep,p.whiteTurn,nullptr);
}
#endif
}
Tablebase::~Tablebase() {
#ifdef CHESS_WITH_SYZYGY
    tb_free();
#endif
}
unsigned Tablebase::setPath(std::string_view path) {
#ifdef CHESS_WITH_SYZYGY
    tb_free();
    largest_ = 0;
    if (path.empty() || path == "<empty>") return 0;
    std::string owned(path);
    if (!tb_init(owned.c_str())) throw std::runtime_error("Syzygy initialization failed");
    largest_ = TB_LARGEST;
    return largest_;
#else
    if (!path.empty() && path != "<empty>")
        throw std::runtime_error("Syzygy support not compiled (configure with CHESS_SYZYGY=ON)");
    return 0;
#endif
}
std::optional<int> Tablebase::probeWdl(const game::Position& pos) const {
#ifdef CHESS_WITH_SYZYGY
    if (!largest_ || pos.castling || pos.halfmove ||
        std::popcount(pos.occupied[0] | pos.occupied[1]) > largest_) return std::nullopt;
    const auto p = convert(pos);
    const unsigned result = tb_probe_wdl(p.white,p.black,p.kings,p.queens,p.rooks,
        p.bishops,p.knights,p.pawns,p.halfmove,p.castling,p.ep,p.whiteTurn);
    if (result == TB_RESULT_FAILED) return std::nullopt;
    if (result == TB_WIN) return 20000;
    if (result == TB_LOSS) return -20000;
    return 0; // cursed wins, blessed losses and draws obey 50-move rules
#else
    (void)pos;
    return std::nullopt;
#endif
}
std::optional<Tablebase::RootResult> Tablebase::probeRoot(game::Position pos) const {
#ifdef CHESS_WITH_SYZYGY
    if (!largest_ || pos.castling ||
        std::popcount(pos.occupied[0] | pos.occupied[1]) > largest_) return std::nullopt;
    const unsigned result = rootProbe(convert(pos));
    if (result == TB_RESULT_FAILED) return std::nullopt;
    int promotion = 0;
    switch (TB_GET_PROMOTES(result)) {
        case TB_PROMOTES_QUEEN: promotion = game::Queen; break;
        case TB_PROMOTES_ROOK: promotion = game::Rook; break;
        case TB_PROMOTES_BISHOP: promotion = game::Bishop; break;
        case TB_PROMOTES_KNIGHT: promotion = game::Knight; break;
        default: break;
    }
    game::MoveList legal;
    pos.legalMoves(legal);
    for (int i=0;i<legal.size;++i) {
        const auto m=legal[i];
        if (m.from == TB_GET_FROM(result) && m.to == TB_GET_TO(result) &&
            m.promotion == promotion) {
            int score=0;
            if (TB_GET_WDL(result)==TB_WIN) score=20000;
            if (TB_GET_WDL(result)==TB_LOSS) score=-20000;
            return RootResult{m,score,TB_GET_DTZ(result)};
        }
    }
#else
    (void)pos;
#endif
    return std::nullopt;
}
}
