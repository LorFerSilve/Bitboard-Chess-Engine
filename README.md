# Bitboard Chess Engine

A C++20 UCI chess engine with a separately reusable `Game` / `Engine` / `View` design. **Version 0.2** focuses on correct, fast single-core searching. No NNUE or parallel search is used.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Linux run `build/bitboard-chess-engine`; on Windows with Visual Studio run `build/Release/bitboard-chess-engine.exe`. Use a C++20 compiler, CMake 3.20+, and optionally Python 3 for UCI integration tests.

### Single-core performance builds

Compiler-provided Release optimizations (`-O3` with GCC) and optional link-time optimization are enabled. To target only the local machine, including BMI2 `PEXT` attack tables when the CPU supports them:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DCHESS_NATIVE=ON -DCHESS_PEXT=ON -DCHESS_LTO=ON
cmake --build build-native --config Release --parallel
ctest --test-dir build-native -C Release --output-on-failure
```

`CHESS_NATIVE` is **OFF** by default for portable binaries. `CHESS_PEXT` selects the precomputed attack table implementation only when `__BMI2__` is supported at compile time; otherwise the portable precomputed-ray/bit-scan fallback is used. Compare both (`-DCHESS_PEXT=OFF` versus `ON`) on your hardware rather than assuming PEXT is faster. Tables occupy roughly 1 MB on compatible builds; the portable implementation needs substantially less memory.

Benchmark representative workloads with `build/chess_bench` (or `build/Release/chess_bench.exe`). Perft is a rules-generator benchmark, **not a measure of Elo or actual search strength**.

GCC profile-guided optimization (PGO) is optional; use the **same build directory** for all three steps:

```sh
cmake -S . -B build-pgo -DCMAKE_BUILD_TYPE=Release -DCHESS_PGO=GENERATE
cmake --build build-pgo --config Release --parallel
./build-pgo/chess_bench
cmake -S . -B build-pgo -DCMAKE_BUILD_TYPE=Release -DCHESS_PGO=USE
cmake --build build-pgo --config Release --parallel
```

These profiles cover the benchmark workload; for serious PGO gains also run searches on varied middlegame and endgame positions during training. Sanitizer builds are recommended when changing search logic.

## UCI

Supports `uci`, `isready`, `ucinewgame`, `position startpos moves ...`, `position fen <six fields> moves ...`, `go depth N`, `go movetime N`, clock-based `go wtime ... btime ...`, `go nodes N`, `go infinite`, `go perft N`, `stop`, `setoption name Hash value N`, `setoption name SyzygyPath value PATH`, `quit`.

```text
uci
isready
position startpos moves e2e4 e7e5
go depth 8
```

The engine can be loaded into compatible UCI GUI clients, including Arena or Cute Chess. A future graphical frontend should call the Game and Engine APIs without depending on the UCI adapter.

## Architecture

- `include/chess/game.hpp`, `src/game/`: 12 bitboards plus a square array, FEN, move generation, castling, en passant, promotions, reversible make/unmake (also null move), incremental Zobrist hashing, attack lookup/bit scans, legal move existence, perft.
- `include/chess/engine.hpp`, `src/engine/`: single-threaded iterative deepening, alpha-beta/PVS, aspiration windows, quiescence search, null-move pruning, LMR, static exchange evaluation for ordering, transposition table with age-aware replacement, killer/history ordering, pawn structure and tapered handcrafted evaluation, repetition detection and time controls.
- `src/view/`: asynchronous UCI adapter. Stop flags are reset **before** launching the worker, preventing a lost-stop race.
- `tests/`: deterministic game/search regressions, UCI stop-race integration, and a manual benchmarking tool.

## Current limitations

This remains an experimental engine, not an Elo-tested tournament engine. The SEE implementation is a pseudo-legal exchange approximation and does not fully account for pinned recaptures; null-move and pruning heuristics need longer tactical/regression suites. Repetition and 50-move rules are treated as drawable for search rather than modeling all GUI draw-claim choices. No NNUE, Lazy SMP, Syzygy tablebases, opening book, comprehensive SPRT/Elo testing or graphical interface has been added yet. `Engine::search()` is not reentrant; call `prepareSearch()` before launching a new asynchronous request, and never execute two searches on the same instance simultaneously.
