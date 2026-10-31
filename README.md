# Bitboard Chess Engine

A C++20 UCI chess engine with a separately reusable `Game` / `Engine` / `View` design. **Version 0.3** adds reproducible head-to-head strength testing and an optional Syzygy tablebase backend, while preserving single-core search and keeping NNUE out of scope.

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

## Strength testing: compare each version with its predecessor

The repo includes `tools/strength_test.py` (Python standard library only), which delegates UCI matches, Elo estimates, PGN output and logistic-Elo SPRT statistics to the external [Fastchess](https://github.com/Disservin/fastchess) executable. **Install Fastchess separately.** A former Git revision can be built automatically in an isolated temporary worktree, or you may supply an already-built baseline executable.

```sh
# Build the candidate in Release mode first.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Compare v0.3 to the v0.2 commit using matched clocks and hash sizes.
python tools/strength_test.py --candidate build/bitboard-chess-engine \
  --baseline-ref 6d38ac0c7eb1544ea17aff59d943b31ce5e9acc5 \
  --fastchess fastchess --mode sprt --elo0 0 --elo1 5 \
  --tc 10+0.1 --hash 16 --concurrency 1
```

Windows (Visual Studio generator): use `--candidate build/Release/bitboard-chess-engine.exe` and `--fastchess PATH_TO_FASTCHESS_EXE`. You can use `--baseline OLD_BINARY` instead of `--baseline-ref`, or omit both to test against `HEAD^` on a Git checkout. For a fixed-length smoke match, use `--mode fixed --rounds 100`. The test driver pairs games with color reversal (`-repeat`), uses a fixed random seed, and records `results/<UTC timestamp>/games.pgn`, `fastchess.log` and `metadata.json` with checksums of executables and the opening book. The bundled 16-opening `benchmarks/openings/smoke.epd` is a **test fixture**, not an adequate rating suite: use a larger neutral opening book with `--book` for credible Elo testing. `--concurrency` runs multiple *games*, not multithreaded search within an engine.

SPRT defaults to `elo0=0`, `elo1=5`, `alpha=beta=0.05`, `model=logistic`; its outcome may remain inconclusive. The `--rounds` argument is a maximum, not a requirement to play every game. **No measured Elo improvement is claimed** without actual completed matches. For a fair comparison, keep CPU, compilation settings, clocks, hash and optional tablebase configuration consistent.

## Optional Syzygy tablebases

Fathom is a separately licensed [MIT-licensed](https://github.com/jdart1/Fathom) endgame probing library, fetched at a **pinned commit** with CMake when requested. The default build does not require Fathom or tablebase files.

```sh
cmake -S . -B build-syzygy -DCMAKE_BUILD_TYPE=Release -DCHESS_SYZYGY=ON
cmake --build build-syzygy --config Release
```

The first configure downloads Fathom's source unless you provide a local source copy using `-DFETCHCONTENT_SOURCE_DIR_FATHOM=/path/to/Fathom`. **No tablebase data is bundled or automatically downloaded**: obtain compatible `.rtbw` (WDL) and `.rtbz` (DTZ) files separately from a trusted source. After loading them on your disk, send this UCI command before searching:

```text
uci
setoption name SyzygyPath value /your/tablebase/directory
isready
position fen 7k/8/5KQ1/8/8/8/8/8 w - - 0 1
go depth 8
```

The engine reports `info string Syzygy maximum pieces: N` when configuring (zero means no recognized files). The exact DTZ root probe selects a validated legal move when data is available. Internal WDL search probes currently require a zero 50-move halfmove clock, and positions with castling rights cannot be probed by Fathom. Missing endgame tables cause a normal search fallback, not fabricated perfect play. WDL values of +20000/-20000 in engine search are tablebase scores, **not** mate distances. Availability is limited to the positions covered by installed tablebases, typically up to seven pieces when all appropriate files are present.

The local Syzygy adapter was tested with a Fathom API stub, not with actual Syzygy data; a network-enabled GitHub Actions build verifies that the real upstream C source compiles. Real WDL/DTZ accuracy requires further tests using the official data files.

## Current limitations

This remains an experimental engine, not an Elo-tested tournament engine. The SEE implementation is a pseudo-legal exchange approximation and does not fully account for pinned recaptures; null-move and pruning heuristics need longer tactical/regression suites. Repetition and 50-move rules are treated as drawable for search rather than modeling all GUI draw-claim choices. No NNUE, Lazy SMP, built-in opening book or graphical interface is implemented. The Fastchess harness and optional Syzygy backend exist, but real match results and tablebase datasets require separate setup. `Engine::search()` is not reentrant; call `prepareSearch()` before launching a new asynchronous request, and never execute two searches on the same instance simultaneously.