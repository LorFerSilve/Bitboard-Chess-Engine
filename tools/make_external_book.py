#!/usr/bin/env python3
"""Build a fixed external ECO-balanced EPD book (benchmark tooling only).

Requires python-chess 1.11.2, installed solely in the benchmark workflow.
Upstream: lichess-org/chess-openings, CC0; source revision pinned in workflow.
"""
import argparse
import csv
import hashlib
import io
import json
from collections import Counter, defaultdict
from pathlib import Path
import random
import re

import chess
import chess.pgn


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(source_dir, exclude_file, out, manifest, per_volume, seed):
    excluded = {line.strip() for line in exclude_file.read_text(encoding="utf-8").splitlines()
                if line.strip() and not line.startswith("#")}
    buckets = defaultdict(lambda: defaultdict(list))
    stats = Counter()
    inputs = {}
    for volume in "abcde":
        source = source_dir / (volume + ".tsv")
        inputs[source.name] = sha256(source)
        with source.open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                eco, pgn = row["eco"].strip(), row["pgn"].strip()
                if not re.fullmatch(volume.upper() + r"[0-9]{2}", eco):
                    raise ValueError(f"Invalid ECO {eco!r} in {source}")
                game = chess.pgn.read_game(io.StringIO(pgn))
                if game is None or game.errors:
                    stats["parse_rejected"] += 1
                    continue
                board = game.end().board()
                plies = board.ply()
                if not 8 <= plies <= 26 or board.is_game_over(claim_draw=True) or board.is_check():
                    stats["position_filtered"] += 1
                    continue
                if not board.is_valid():
                    stats["invalid"] += 1
                    continue
                epd = board.epd()  # four FEN fields; legal en passant squares only
                if len(epd.split()) != 4:
                    raise ValueError(f"Unexpected EPD: {epd}")
                if epd in excluded:
                    stats["overlap_excluded"] += 1
                    continue
                buckets[volume.upper()][eco].append((epd, plies))
                stats["eligible"] += 1

    rng = random.Random(seed)
    used = set(excluded)
    selected = {}
    for volume in "ABCDE":
        eco_codes = sorted(buckets[volume])
        rng.shuffle(eco_codes)
        for code in eco_codes:
            rng.shuffle(buckets[volume][code])
        chosen = []
        # Round robin over ECO codes limits overrepresentation of popular
        # or exhaustively catalogued subvariations.
        while len(chosen) < per_volume:
            progressed = False
            for code in eco_codes:
                rows = buckets[volume][code]
                while rows:
                    epd, plies = rows.pop()
                    if epd in used:
                        continue
                    used.add(epd)
                    chosen.append({"eco": code, "epd": epd, "plies": plies})
                    progressed = True
                    break
                if len(chosen) == per_volume:
                    break
            if not progressed:
                raise ValueError(f"Only {len(chosen)} distinct eligible openings in ECO {volume}; "
                                 f"need {per_volume}")
        selected[volume] = chosen

    # Interleave the ECO volumes; Fastchess still independently shuffles
    # each match with a recorded seed.
    ordered = [selected[v][i] for i in range(per_volume) for v in "ABCDE"]
    output_bytes = ("\n".join(row["epd"] for row in ordered) + "\n").encode("utf-8")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(output_bytes)
    details = {
        "source_repository": "https://github.com/lichess-org/chess-openings",
        "source_commit": "712a58be746c143f4ab935b68acc50f4bac813eb",
        "source_license": "CC0-1.0",
        "parser": f"python-chess {chess.__version__}",
        "source_file_sha256": inputs,
        "excluded_book_sha256": sha256(exclude_file),
        "generation_seed": seed,
        "sampling": "round-robin across ECO codes within each volume; unique EPD; 8..26 plies; no checks/terminal positions",
        "per_eco_volume": {volume: len(selected[volume]) for volume in "ABCDE"},
        "distinct_eco_codes": len(set(row["eco"] for row in ordered)),
        "total_positions": len(ordered),
        "duplicate_positions": len(ordered) - len(set(row["epd"] for row in ordered)),
        "old_book_overlap": len({row["epd"] for row in ordered} & excluded),
        "plies_min": min(row["plies"] for row in ordered),
        "plies_max": max(row["plies"] for row in ordered),
        "source_stats": dict(stats),
        "book_sha256": hashlib.sha256(output_bytes).hexdigest()
    }
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps(details, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(details, indent=2, sort_keys=True))
    assert len(ordered) == 5 * per_volume and details["duplicate_positions"] == 0
    assert details["old_book_overlap"] == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--exclude", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--per-volume", type=int, default=150)
    parser.add_argument("--seed", type=int, default=20261108)
    args = parser.parse_args()
    if args.per_volume < 1:
        parser.error("per-volume must be positive")
    generate(args.source_dir, args.exclude, args.output, args.manifest,
             args.per_volume, args.seed)


if __name__ == "__main__":
    main()
