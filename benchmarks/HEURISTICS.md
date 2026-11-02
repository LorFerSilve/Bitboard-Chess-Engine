# v0.6: one-heuristic-at-a-time strength experiments

## Goal

Reject or keep individual search/evaluation heuristics based on measured playing strength rather than code complexity. The first **combined** v0.6 experiment scored **-45.42 ± 39.24 Elo** versus v0.5. The initial *group* ablations were also negative: search-only **-96.19 ± 37.42 Elo**, evaluation-only **-22.62 ± 34.19 Elo** (each at 200 games, 3+0.03). These are **not** measurements of individual heuristics.

Each switch below is independently configurable via a standard UCI `check` option, defaults to `true`, and clears the transposition table when modified:

| UCI heuristic name | What is switched |
| --- | --- |
| `Clustered TT` | 4-entry clusters vs original single-entry TT |
| `Null Verification` | Verification search after selected deep null-move beta cutoffs |
| `History LMR` | History-dependent adjustment to late-move reductions |
| `Adaptive Aspiration` | Incremental widening vs immediate full-window retry |
| `SEE Prefilter` | Cheap attacker check before expensive legal recapture evaluation |
| `King Pressure` | Newly added attack-pressure score around the enemy king |
| `Pawn Support` | New bonuses for defended pawn chains |
| `Weak Pawns` | New penalty for exposed, unsupported pawns |
| `Pawn Forks` | New static pawn-fork threat bonuses |
| `Knight Forks` | New static immediate/potential knight-fork bonuses |
| `QSearch SEE Pruning` | Reuse move-ordering SEE to skip unfavorable nonchecking quiescence captures; always preserve checks, evasions and promotions |

**Control:** `All Heuristics` toggles all ten settings simultaneously. All off is a *v0.6 control*, not bit-for-bit identical to historical v0.5: some search-correctness changes (e.g. node accounting and artificial-null repetition boundaries) are deliberately always active.

## One controlled experiment, one executable

Build v0.6 once and install Fastchess separately. Run from the checkout root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

To test *only* king pressure with all other v0.6 heuristics disabled in both engines:

```sh
python tools/strength_test.py \
  --candidate build/bitboard-chess-engine \
  --ablation "King Pressure" --ablation-mode isolated \
  --mode fixed --rounds 100 --tc 3+0.03 --concurrency 2
```

To measure the marginal value of removing knight-fork scoring from the *full* v0.6 candidate:

```sh
python tools/strength_test.py \
  --candidate build/bitboard-chess-engine \
  --ablation "Knight Forks" --ablation-mode leave-one-out \
  --mode fixed --rounds 100 --tc 3+0.03 --concurrency 2
```

For each `--ablation`, the script runs the **same binary on both sides**:

- **`isolated`** — Candidate: all off, specified heuristic on. Baseline: all off.
- **`leave-one-out`** — Candidate: all on. Baseline: all on except specified heuristic off.

Both comparisons change **exactly one** option. The tester checks that each UCI option is advertised before starting Fastchess; unknown options raise an error rather than producing a misleading no-op result.

For any other pair of configurations, `--candidate-option "NAME=VALUE"` and `--baseline-option "NAME=VALUE"` can be supplied repeatedly to the existing strength runner, with independent validation. A config differing in multiple options is **not** a controlled single-heuristic measurement.

## Automatable GitHub workflow

Use **Actions → v0.6 single-heuristic ablation → Run workflow** on the development branch after the workflow becomes dispatchable. Select the heuristic, isolated/leave-one-out mode, opening pairs, and time control. The workflow builds the engine, runs tests, compiles Fastchess, then stores `games.pgn`, `fastchess.log`, and `metadata.json` as an artifact.

A workflow-file push also starts one **4-pair smoke run** (1+0.01) to test wiring. That smoke run is **not an Elo benchmark**.

Each match metadata file records the candidate and baseline executable SHA-256 digests, exact option arrays, Git revision, opening-book checksum, seed, time control, hash, round count and result status.

## Decision policy

First run short correctness/performance smoke tests. Then compare each promising heuristic in isolated mode, and confirm any positive signal with leave-one-out tests. Use a diverse, independent opening suite and an appropriate SPRT or sufficiently large fixed sample to decide release inclusion. Repeating many experiments can create false positives: do not call a slight gain from 200 games proven strength. Do not merge the old combined candidate simply because its tests pass.
