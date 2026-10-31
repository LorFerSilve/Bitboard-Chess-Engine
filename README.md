# Bitboard Chess Engine

A C++20 bitboard chess engine with a modular `Game` / `Engine` / `View` architecture.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `build/bitboard-chess-engine` (on Windows: `build/Release/bitboard-chess-engine.exe` with a multi-config generator).

## UCI

Supports `uci`, `isready`, `ucinewgame`, `position startpos moves ...`, `position fen <six fields> moves ...`, `go depth N`, `go movetime N`, clock-based `go wtime ... btime ...`, `go nodes N`, `go infinite`, `go perft N`, `stop`, `setoption name Hash value N`, `quit`.

Example:

```text
uci
isready
position startpos moves e2e4 e7e5
go depth 4
```

UCI lets you use the engine with GUI clients such as Arena, Cute Chess or a future custom frontend.

## Architecture

- `include/chess/game.hpp`, `src/game/`: position, 12 bitboards, redundant square array, FEN, pseudo/legal moves, castling, en passant, promotions, incremental Zobrist hash, make/unmake, perft.
- `include/chess/engine.hpp`, `src/engine/`: alpha-beta negamax, iterative deepening, principal variation search (PVS), quiescence search, late-move reductions (LMR), move ordering (TT/captures/killers/history), transposition table with configurable fixed memory, repetition and material draws, simple handcrafted evaluation, stop/time controls.
- `src/view/`: asynchronous UCI adapter; GUI can call Game and Engine directly without depending on the UCI protocol.

## Status and limitations

This is an independently implemented **first playable baseline**, not yet a top-tier engine. Sliding pieces currently use direct ray scans rather than Magic Bitboards or PEXT. NNUE requires a correctly defined, trained and validated weight file and incremental feature updates; it is **not yet implemented**. Root search is currently single-threaded. There is no opening book, Syzygy tablebase, comprehensive FEN legality validation or SPRT/Elo benchmarking yet.

`Position::hash` includes the FEN en-passant square even when no legal capture exists; repetition detection may therefore be conservative in some edge cases. Threefold repetition and 50-move rules are treated as drawable during search; a GUI should adjudicate claims separately. Please do not call this tournament-ready before more adversarial position and perft suites are run.
