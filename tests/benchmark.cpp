#include "chess/game.hpp"
#include "chess/engine.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

int main() {
    using Clock=std::chrono::steady_clock;
    auto p=chess::game::Position::start();
    auto start=Clock::now();
    auto count=p.perft(5);
    double seconds=std::chrono::duration<double>(Clock::now()-start).count();
    std::cout<<"perft(5): "<<count<<" positions in "<<std::fixed<<std::setprecision(3)
             <<seconds<<" s ("<<static_cast<std::uint64_t>(count/seconds)<<" pos/s)\n";
    chess::engine::Engine engine(32);
    chess::engine::Limits limits;limits.depth=7;
    engine.prepareSearch();
    start=Clock::now();
    auto result=engine.search(p,limits);
    seconds=std::chrono::duration<double>(Clock::now()-start).count();
    std::cout<<"search(7): "<<result.info.nodes<<" nodes in "<<seconds
             <<" s ("<<static_cast<std::uint64_t>(result.info.nodes/seconds)
             <<" nodes/s); best "<<(result.best?result.best.uci():"0000")<<'\n';
    return count==4865609 && result.best && result.info.depth==7?0:1;
}
