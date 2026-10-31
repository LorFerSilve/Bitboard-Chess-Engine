#include "chess/game.hpp"
#include "chess/engine.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) throw std::runtime_error("Expected EPD book path");
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("Cannot open opening suite");
    chess::engine::Engine engine(16);
    chess::engine::Limits limits;
    limits.depth = 3;
    std::string epd;
    int tested = 0;
    while (std::getline(input, epd)) {
        if (epd.empty() || epd[0] == '#') continue;
        auto position = chess::game::Position::fromFen(epd + " 0 1");
        chess::game::MoveList legal;
        position.legalMoves(legal);
        if (!legal.size) throw std::runtime_error("Opening line is terminal: " + epd);
        engine.prepareSearch();
        auto result = engine.search(position, limits);
        if (result.info.depth != limits.depth)
            throw std::runtime_error("Search did not finish in opening: " + epd);
        bool found = false;
        for (int i=0; i<legal.size; ++i)
            if (result.best == legal[i]) {found=true; break;}
        if (!found) throw std::runtime_error("Search returned illegal move: " + epd);
        ++tested;
    }
    if (tested < 80) throw std::runtime_error("Opening suite unexpectedly small");
    std::cout << "Validated " << tested << " depth-3 searches on distinct openings\n";
}
