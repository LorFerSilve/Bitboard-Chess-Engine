# External Lichess ECO benchmark — preregistered protocol

## Status and scope

A **new external opening pool** and **longer time control** are now the next validation stage for the v0.6 experimental branch. This file describes the plan **before match outcomes are known**. No Elo gain is claimed. The previously used 95-position `extended.epd` is excluded from the new pool. As the evaluation changes were already developed before this held-out experiment, the independent corpus is useful for confirmation, but this is not a blinded clinical-style experiment.

## Data provenance and selection

- Source: [Lichess chess-openings](https://github.com/lichess-org/chess-openings), **CC0-1.0**, commit `712a58be746c143f4ab935b68acc50f4bac813eb`.
- Input: five upstream `a.tsv` to `e.tsv` files (ECO volumes A–E). Install **`chess==1.11.2`** only in the benchmark workflow for PGN-to-EPD conversion; there is no new C++ engine dependency.
- Generator: `tools/make_external_book.py`; deterministic seed **20261108**. Reject terminal/in-check positions and lines outside 8–26 halfmoves. Remove duplicate EPDs and every position in the local `extended.epd` corpus.
- Selection: round-robin across ECO codes, exactly **150 distinct positions per ECO volume**, **750 total**. The generated EPD, manifest, content SHA-256 and source TSV hashes are all archived in the same workflow.
- The source represents **named ECO variations, not frequency-weighted games**. Equal ECO-volume weighting is deliberate for coverage rather than a claim about average practical-game performance.

## Precommitted comparison protocol

[Workflow](../../.github/workflows/v06-independent-eco.yml) runs **five comparisons** with independent match logs, all using the *same exact* generated pool and opening shuffle seed **20261109**, with **150 reversed-colour opening pairs = 300 games per comparison**, `10+0.1` seconds plus increment per move, hash **32 MiB**, two simultaneous games, portable Release+LTO.

| Comparison | Candidate options | Baseline |
| --- | --- | --- |
| control-v05 | `All Heuristics=false` | Historical v0.5, unchanged `9189218` |
| qsearch-v05 | All off + `QSearch SEE Pruning=true` | Historical v0.5 |
| tt-aspiration-v05 | All off + `Clustered TT=true` + `Adaptive Aspiration=true` | Historical v0.5 |
| null-v05 | All off + `Null Verification=true` | Historical v0.5 |
| qsearch-isolated | All off + `QSearch SEE Pruning=true` | Same v0.6 executable, all off |

This fixed-screen protocol avoids choosing settings after looking at results. Pairing reverses colours for every selected opening. The opening pool (750) exceeds the number of unique openings played per match (150); the runner samples them using the fixed match seed. Different jobs use a shared book/seed, but paired scores across *separate* Fastchess runs are not the same statistical unit as a single controlled ablation.

## Decision criteria and limitations

- **Do not claim positive Elo** unless a statistically meaningful interval excludes zero *and* the effect survives a new confirmation set. With five comparisons and 300 games each, false-positive/multiple-testing risks remain, so these results are for screening and ranking, **not automatic promotion**.
- If no candidate clears the above bar, keep the v0.5 release and retain all switches off by default.
- The higher clock and independent ECO data improve external validity, but the book contains named opening sequences (not realistic occurrence frequencies), one fixed source, and limited opening coverage per match. Different time controls, CPU architectures and a fresh master-game distribution would further strengthen release conclusions.
- Historical data from the old 95-position book must not be pooled indiscriminately with this screen. Elo estimates are meaningful only with their stated time control and sampling uncertainty.

GitHub Actions runs the external-book validation, existing CTests and an additional C++ opening search-legality test before any games. Every matrix job saves `games.pgn`, `fastchess.log`, `metadata.json`, and book provenance.
