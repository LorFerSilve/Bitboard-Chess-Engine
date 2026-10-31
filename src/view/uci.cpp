#include "chess/game.hpp"
#include "chess/engine.hpp"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <string>
#include <thread>
#include <vector>

namespace {
std::mutex outputMutex;
void print(const std::string& text){std::lock_guard lock(outputMutex);std::cout<<text<<std::endl;}
int parameter(const std::vector<std::string>& tokens,const std::string& name,int fallback=0) {
    auto it=std::find(tokens.begin(),tokens.end(),name);
    if(it==tokens.end()||++it==tokens.end())return fallback;
    try {return std::max(0,std::stoi(*it));} catch(...) {return fallback;}
}
}
int main() {
    using namespace chess;
    game::Position position=game::Position::start();
    std::vector<std::uint64_t> gameHistory{position.hash};
    engine::Engine engine(32);
    int moveOverheadMs=20;
    std::thread worker;
    auto stopSearch=[&] {engine.stop();if(worker.joinable())worker.join();};
    std::string input;
    while(std::getline(std::cin,input)) {
        std::istringstream line(input);
        std::vector<std::string> args;
        for(std::string t;line>>t;)args.push_back(t);
        if(args.empty())continue;
        const auto& command=args[0];
        try {
            if(command=="uci") {
                print("id name Bitboard Chess Engine");
                print("id author Bitboard-Chess-Engine contributors");
                print("option name Hash type spin default 32 min 1 max 1024");
                print("option name Move Overhead type spin default 20 min 0 max 1000");
                print("option name SyzygyPath type string default <empty>");
                print("uciok");
            } else if(command=="isready")print("readyok");
            else if(command=="ucinewgame") {
                stopSearch();engine.clearHash();position=game::Position::start();gameHistory={position.hash};
            } else if(command=="position") {
                stopSearch();
                if(args.size()<2)throw std::invalid_argument("Missing position");
                std::size_t i=2;
                game::Position candidate;
                if(args[1]=="startpos")candidate=game::Position::start();
                else if(args[1]=="fen"&&args.size()>=8) {
                    std::string fen;
                    for(i=2;i<8;++i){if(i>2)fen+=' ';fen+=args[i];}
                    candidate=game::Position::fromFen(fen);
                } else throw std::invalid_argument("Bad position command");
                std::vector<std::uint64_t> candidateHistory{candidate.hash};
                if(i<args.size()&&args[i]=="moves") {
                    for(++i;i<args.size();++i) {
                        if(!candidate.playUci(args[i]))throw std::invalid_argument("Illegal UCI move: "+args[i]);
                        candidateHistory.push_back(candidate.hash);
                    }
                }
                position=std::move(candidate);
                gameHistory=std::move(candidateHistory);
            } else if(command=="setoption") {
                stopSearch();
                if(input.find("name Hash")!=std::string::npos)engine.resizeHash(parameter(args,"value",32));
                else if(input.find("name Move Overhead")!=std::string::npos)
                    moveOverheadMs=std::clamp(parameter(args,"value",20),0,1000);
                else if(input.find("name SyzygyPath")!=std::string::npos) {
                    auto value=input.find(" value ");
                    std::string path=value==std::string::npos?"":input.substr(value+7);
                    unsigned pieces=engine.setSyzygyPath(path);
                    print("info string Syzygy maximum pieces: "+std::to_string(pieces));
                }
            } else if(command=="go") {
                stopSearch();
                if(args.size()>=3&&args[1]=="perft") {
                    int depth=std::stoi(args[2]);
                    if(depth<0||depth>7)throw std::invalid_argument("perft depth must be 0..7");
                    print("nodes "+std::to_string(position.perft(depth)));
                    continue;
                }
                engine::Limits limits;
                limits.depth=parameter(args,"depth",64);
                limits.nodes=static_cast<std::uint64_t>(parameter(args,"nodes",0));
                limits.timeMs=parameter(args,"movetime",0);
                if(limits.timeMs==0 && std::find(args.begin(),args.end(),"infinite")==args.end()) {
                    const int clock=parameter(args,position.whiteToMove?"wtime":"btime",0);
                    const int inc=parameter(args,position.whiteToMove?"winc":"binc",0);
                    const int moves=std::max(1,parameter(args,"movestogo",30));
                    if(clock>0) {
                        // Never intentionally consume the GUI's entire clock.
                        // Use 64-bit arithmetic to avoid overflow on long clocks.
                        const auto budget=std::max(1,clock-moveOverheadMs);
                        const auto target=static_cast<long long>(clock)/moves+
                                          3LL*inc/4;
                        limits.timeMs=static_cast<int>(std::clamp<long long>(
                            target,1,std::min<long long>(budget,std::max(1,clock/5))));
                    }
                }
                engine.prepareSearch(); // Reset before launching, never inside the worker.
                worker=std::thread([&,p=position,h=gameHistory,limits] {
                    auto result=engine.search(p,limits,h,[](const engine::SearchInfo& info) {
                        std::ostringstream text;
                        text<<"info depth "<<info.depth<<" score ";
                        if(std::abs(info.score)>=29744) {
                            int plies=30000-std::abs(info.score);
                            text<<"mate "<<(info.score>0?(plies+1)/2:-(plies+1)/2);
                        } else text<<"cp "<<info.score;
                        text<<" nodes "<<info.nodes<<" time "<<info.elapsedMs;
                        if(info.elapsedMs)text<<" nps "<<(info.nodes*1000/info.elapsedMs);
                        text<<" pv";
                        for(auto move:info.pv)text<<' '<<move.uci();
                        print(text.str());
                    });
                    print("bestmove "+(result.best?result.best.uci():"0000"));
                });
            } else if(command=="stop")stopSearch();
            else if(command=="quit") {stopSearch();break;}
            else if(command=="d")print(position.fen());
        } catch(const std::exception& ex) {
            print(std::string("info string error: ")+ex.what());
        }
    }
    stopSearch();
}
