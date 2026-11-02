# Bitboard Chess Engine

A C++20 **single-threaded** chess engine with a reusable `Game` / `Engine` / `View` architecture and a UCI command-line adapter. **v0.6 development candidate** builds on v0.5 with a clustered transposition table, safer null-move verification, adaptive aspiration windows, and tactical/positional evaluation improvements. NNUE and Lazy SMP remain deferred.

This is an **experimental engine**. In a 200-game paired match, **v0.5 beat v0.4 by +92.46 Elo ±34.97** at 3+0.03 (see [benchmark report](benchmarks/results/v0.5-v0.4.md)). The first fully enabled v0.6 prototype **regressed by -45.42 Elo ±39.24** against v0.5. Subsequent [individual screening](benchmarks/results/v0.6-isolated-screen.md) and [combination tests](benchmarks/results/v0.6-combinations.md) have not yet established a positive release gain. The [five-way targeted selection](benchmarks/results/v0.6-targeted-selection.md) shortlisted TT + aspiration (+27.85 ±38.85 Elo over 200 games), but the [400-game confirmation](benchmarks/results/v0.6-tt-aspiration-confirmation.md) finished at **−2.61 ±25.58 Elo**. No v0.6 configuration is a validated strength improvement. A separately switchable **QSearch SEE Pruning** experiment is now awaiting head-to-head evidence. **v0.5 remains the strongest validated release**.

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

Supported UCI commands: `uci`, `isready`, `ucinewgame`, `position startpos moves ...`, `position fen <six fields> moves ...`, `go depth N`, `go movetime N`, `go wtime ... btime ...`, `go nodes N`, `go infinite`, `go perft N`, `stop`, `setoption name Hash value N`, `setoption name Move Overhead value N`, `setoption name SyzygyPath value PATH`, `quit`.

## Architecture

- **Game** (`include/chess/game.hpp`, `src/game/`): 12 bitboards, square lookup, FEN, move generation, reversible make/unmake, incremental Zobrist hashing, and Perft. `attacksFrom()` exposes pseudo-attacks to evaluation and future interfaces.
- **Engine** (`include/chess/engine.hpp`, `src/engine/`): single-core iterative deepening, PVS/alpha-beta, aspiration windows, quiescence, null-move pruning, LMR, legal-recature-aware static exchange evaluation for ordering, TT, history/killer ordering, tapered classical evaluation, and optional Syzygy probing.
- **View** (`src/view/uci.cpp`): asynchronous UCI adapter; a future GUI can use Game and Engine directly.

### v0.6 candidate changes

- Four-entry clustered transposition table to limit destructive collisions, preserving exact-depth and age-aware replacement.
- Incremental aspiration-window widening instead of immediate full-window retries; history-aware late-move reductions.
- Null-move verification at higher depths and repetition-history separation across artificial null moves.
- Fixed duplicate node counting when negamax transitions into quiescence.
- Cheap recapture-availability screening before expensive legal SEE for unfavorable-looking captures.
- Tapered evaluation now scores king-zone attacking pressure, pawn-chain support, selected weak pawns, and simple current/one-ply pawn and knight fork threats for both sides. These are *heuristic threats*, not full tactical proof of a forced fork.
- Threefold-repetition regression test and search tests exercising clustered TT use.
- Experimental `QSearch SEE Pruning`: avoid searching previously identified SEE-negative captures at quiescence nodes unless the capture gives check; never prune check evasions or promotions. This is a new opt-in testable feature, not an established strength gain.

The v0.6 experimental switches are independently controllable via UCI (see [HEURISTICS.md](benchmarks/HEURISTICS.md)). The first combined prototype lost against v0.5, and the screened combinations have not established superiority. Do not merge all-on defaults into a release without new evidence. Historical v0.5 baseline: `9189218c76bee69c08a7e29dad12650ada2d1b13`.

### v0.5 changes

- Quiescence generates and sorts only captures/promotions unless in check, and delays expensive stalemate verification until needed. Stand-pat beta cutoffs still verify that at least one legal move exists.
- Quiet history updates now reward cutoff moves and penalize previously tried quiet failures; shallow non-PV futility pruning skips selected nonchecking non-pawn quiets only after a searched legal move.
- A configurable UCI `Move Overhead` option (default 20 ms) reserves time from the clock. Iterative deepening stops starting new iterations once 75% of the hard budget is consumed.
- All pruning/evaluation changes remain **strength hypotheses**, not proven Elo improvements. Verify with paired matches against v0.4 before calling v0.5 stronger.

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
- The v0.5 vs v0.4 200-game comparison is recorded in the repository, but cross-time-control validation and systematic per-heuristic tuning are still needed. No v0.6 configuration has yet shown a statistically convincing Elo advantage against v0.5.
- Move generation still relies on pseudo-legal moves filtered by make/unmake; specialized check/pin/evasion generation could reduce work. Quiescence still checks stalemate by testing legal moves.
- SEE uses legal least-valued recaptures, which is more accurate on pins but can be more expensive; extensive tactical performance regression and tuning remain important.
- Time management is currently basic. Draw claims are simplified for search rather than modeled as player choices at GUI level.
- Engine instances must not be searched concurrently. For UCI asynchronous searching, reset the stop flag before launching each new search, and stop/join before changing options.
