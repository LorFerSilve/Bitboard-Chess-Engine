# Bitboard Chess Engine

A C++20 **single-threaded** chess engine with a reusable `Game` / `Engine` / `View` architecture and a UCI command-line adapter. **v0.4** improves classical evaluation, tactical capture ordering, and search validation. NNUE and Lazy SMP are deliberately deferred.

This is an **experimental engine**. The improvements below are implementation and correctness changes; **no Elo improvement has yet been established**. The intended process is to run statistically meaningful paired matches using the supplied strength-testing runner before calling any search change a strength improvement.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Linux, run `build/bitboard-chess-engine`. On Windows with a multi-config Visual Studio generator, run `build/Release/bitboard-chess-engine.exe`. Requirements: C++20 compiler, CMake 3.20+, optional Python 3 for integration tests.

```text
uci
isready
position startpos moves e2e4 e7e5
go depth 8
```

Supported UCI commands: `uci`, `isready`, `ucinewgame`, `position startpos moves ...`, `position fen <six fields> moves ...`, `go depth N`, `go movetime N`, `go wtime ... btime ...`, `go nodes N`, `go infinite`, `go perft N`, `stop`, `setoption name Hash value N`, `setoption name SyzygyPath value PATH`, `quit`.

## Architecture

- **Game** (`include/chess/game.hpp`, `src/game/`): 12 bitboards, square lookup, FEN, move generation, reversible make/unmake, incremental Zobrist hashing, and Perft. `attacksFrom()` exposes pseudo-attacks to evaluation and future interfaces.
- **Engine** (`include/chess/engine.hpp`, `src/engine/`): single-core iterative deepening, PVS/alpha-beta, aspiration windows, quiescence, null-move pruning, LMR, legal-recature-aware static exchange evaluation for ordering, TT, history/killer ordering, tapered classical evaluation, and optional Syzygy probing.
- **View** (`src/view/uci.cpp`): asynchronous UCI adapter; a future GUI can use Game and Engine directly.

### v0.4 changes

- Classical evaluation adds piece mobility, pawn support, defended knight outposts, and pawn shielding around kings. Passed-pawn masks are precomputed.
- Capture ordering uses legal recaptures in the exchange sequence, excluding pinned pieces and unsafe king recaptures; quiescence delta pruning preserves checking captures.
- Late Move Reductions adapt to depth and move index; power-of-two transposition table sizes allow masking instead of division for hash lookup.
- A 95-position extended opening suite is checked into `benchmarks/openings/extended.epd`; an additional CTest target verifies completed depth-3 searches always return legal moves on these positions.
- The strength runner now defaults to the extended opening suite; the original 16-position `smoke.epd` remains available for quick checks.

## Strength testing (Elo / SPRT)

Install [Fastchess](https://github.com/Disservin/fastchess) separately. The Python orchestrator uses only the standard library; do not confuse `--concurrency` (simultaneous games) with multithreaded **engine** searching.

Run from a real Git checkout so the script can build a previous Git revision in an isolated worktree:

```sh
python tools/strength_test.py \
  --candidate build/bitboard-chess-engine \
  --baseline-ref 3bca5914e329298066399e174cebcc7557898dac \
  --mode sprt --elo0 0 --elo1 5 --tc 10+0.1 --concurrency 1
```

On Windows, adjust `--candidate` to the `.exe` path. Alternatively use `--baseline PATH` to compare two already-built executables. Outcomes include PGN, `fastchess.log`, and `metadata.json` with binary and book checksums. The paired-opening arrangement is useful for regression detection; 95 related openings are *not* sufficient alone for a high-confidence universal Elo claim. Prefer a diverse, unbiased rated opening suite for serious tests.

## Syzygy tablebases (optional)

The optional [Fathom](https://github.com/jdart1/Fathom) backend is pinned in CMake. To build it, CMake needs network access during configuration; this is not required for the default engine build:

```sh
cmake -S . -B build-syzygy -DCMAKE_BUILD_TYPE=Release -DCHESS_SYZYGY=ON
cmake --build build-syzygy --config Release --parallel
```

Download Syzygy `.rtbw` and `.rtbz` data files separately and configure UCI:

```text
setoption name SyzygyPath value /path/to/syzygy
```

Without tablebase files, the engine searches normally. Exact supported endgames depend on installed files and chess-rule/50-move compatibility. A stub adapter is included in tests; this repository does **not** ship Syzygy data. Real database integration has not yet been established by a local end-to-end tablebase test.

## Performance builds

Release enables compiler optimizations and optionally LTO. For local CPU-specific builds:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DCHESS_NATIVE=ON -DCHESS_PEXT=ON -DCHESS_LTO=ON
cmake --build build-native --config Release --parallel
ctest --test-dir build-native -C Release --output-on-failure
```

`CHESS_NATIVE=OFF` produces portable builds by default. BMI2/PEXT attack lookups only compile when BMI2 is available. Benchmark `CHESS_PEXT=ON` versus `OFF` on target hardware. GCC profile-guided optimization is available through `-DCHESS_PGO=GENERATE` followed by representative search training and `-DCHESS_PGO=USE` in the same build directory; check compiler warnings when not every compilation unit receives training coverage. `build/chess_bench` runs a small deterministic throughput benchmark, **not** a chess-strength test.

## Open limitations / next steps

- No NNUE, multithreaded search, or graphical interface. Those are independent future phases.
- Fathom requires real tablebases to prove exact endgame behavior and remains an optional build.
- Large-scale engine-vs-engine Elo/SPRT tests have not been run on this release. Search and evaluation weights have not been tuned through statistical tests.
- Move generation still relies on pseudo-legal moves filtered by make/unmake; specialized check/pin/evasion generation could reduce work. Quiescence still checks stalemate by testing legal moves.
- SEE uses legal least-valued recaptures, which is more accurate on pins but can be more expensive; extensive tactical performance regression and tuning remain important.
- Time management is currently basic. Draw claims are simplified for search rather than modeled as player choices at GUI level.
- Engine instances must not be searched concurrently. For UCI asynchronous searching, reset the stop flag before launching each new search, and stop/join before changing options.
