#!/usr/bin/env python3
"""Reproducible paired engine-vs-engine SPRT/fixed-match runner using Fastchess.

The orchestrator uses only the Python standard library. Fastchess is an
explicit external tool, not a dependency of the chess engine itself.
"""
import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

# Match the UCI names in chess::engine::HeuristicNames.
HEURISTICS = (
    "Clustered TT", "Null Verification", "History LMR", "Adaptive Aspiration",
    "SEE Prefilter", "King Pressure", "Pawn Support", "Weak Pawns",
    "Pawn Forks", "Knight Forks",
)


def option_name(raw: str) -> str:
    name, separator, value = raw.rpartition("=")
    if not separator or not name.strip() or not value.strip():
        raise ValueError(f"Expected a UCI option formatted as NAME=VALUE, got: {raw!r}")
    return name


def ablation_options(name: str, mode: str) -> tuple[list[str], list[str]]:
    if name not in HEURISTICS:
        raise ValueError(f"Unknown heuristic: {name!r}")
    if mode == "isolated":
        # Difference: exactly this option; all others disabled in both engines.
        return ["All Heuristics=false", f"{name}=true"], ["All Heuristics=false"]
    if mode == "leave-one-out":
        # Difference: exactly this option; all others enabled in both engines.
        return [], [f"{name}=false"]
    raise ValueError(f"Unknown ablation mode: {mode!r}")


def verify_uci_options(exe: Path, options: list[str]) -> None:
    if not options:
        return
    result = subprocess.run([str(exe)], input="uci\nquit\n", text=True,
                            capture_output=True, timeout=10, check=True)
    for option in options:
        name = option_name(option)
        if f"option name {name} type " not in result.stdout:
            raise ValueError(f"Engine {exe} does not advertise UCI option {name!r}")




def check_binary(path: Path, label: str) -> Path:
    path = path.expanduser().resolve()
    if not path.is_file():
        raise ValueError(f"{label} executable does not exist: {path}")
    return path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def current_revision() -> str | None:
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                            text=True, capture_output=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def build_baseline(ref: str, worktree: Path, build_dir: Path) -> Path:
    if not (ROOT / ".git").exists():
        raise ValueError("--baseline-ref requires a Git checkout; pass --baseline instead")
    subprocess.run(["git", "worktree", "add", "--detach", str(worktree), ref],
                   cwd=ROOT, check=True)
    try:
        subprocess.run(["cmake", "-S", str(worktree), "-B", str(build_dir),
                        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
                        "-DCHESS_NATIVE=OFF", "-DCHESS_LTO=ON"], check=True)
        subprocess.run(["cmake", "--build", str(build_dir), "--config", "Release",
                        "--parallel", "2"], check=True)
        name = "bitboard-chess-engine.exe" if sys.platform == "win32" else "bitboard-chess-engine"
        exe = build_dir / ("Release" if sys.platform == "win32" else "") / name
        return check_binary(exe, "Baseline")
    except BaseException:
        subprocess.run(["git", "worktree", "remove", "--force", str(worktree)],
                       cwd=ROOT, check=False)
        raise


def fastchess_command(args, old: Path, new: Path, pgn: Path) -> list[str]:
    candidate_options = getattr(args, "candidate_option", [])
    baseline_options = getattr(args, "baseline_option", [])
    cmd = [args.fastchess,
           "-engine", f"cmd={new}", "name=Candidate",
           *(f"option.{value}" for value in candidate_options),
           "-engine", f"cmd={old}", "name=Baseline",
           *(f"option.{value}" for value in baseline_options),
           "-each", f"tc={args.tc}", f"option.Hash={args.hash}",
           "-openings", f"file={args.book.resolve()}", "format=epd", "order=random",
           "-srand", str(args.seed),
           "-repeat", "-rounds", str(args.rounds),
           "-concurrency", str(args.concurrency),
           "-pgnout", f"file={pgn}", "notation=san", "nodes=true"]
    if args.mode == "sprt":
        cmd += ["-sprt", f"elo0={args.elo0}", f"elo1={args.elo1}",
                f"alpha={args.alpha}", f"beta={args.beta}", "model=logistic"]
    return cmd


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--candidate", type=Path, required=True, help="New UCI engine executable")
    baseline = p.add_mutually_exclusive_group()
    baseline.add_argument("--baseline", type=Path, help="Previously built UCI executable")
    baseline.add_argument("--baseline-ref", help="Git commit/tag to build in an isolated worktree")
    p.add_argument("--candidate-option", action="append", default=[], metavar="NAME=VALUE",
                   help="UCI option applied only to candidate; can be repeated")
    p.add_argument("--baseline-option", action="append", default=[], metavar="NAME=VALUE",
                   help="UCI option applied only to baseline; can be repeated")
    p.add_argument("--ablation", choices=HEURISTICS, metavar="HEURISTIC",
                   help="Use the candidate binary on both sides, toggling exactly one heuristic")
    p.add_argument("--ablation-mode", choices=("isolated", "leave-one-out"),
                   default="isolated", help="All-off with one on, or all-on with one off")
    p.add_argument("--fastchess", default="fastchess", help="Fastchess executable in PATH or absolute path")
    p.add_argument("--book", type=Path, default=ROOT / "benchmarks/openings/extended.epd")
    p.add_argument("--mode", choices=("fixed", "sprt"), default="sprt")
    p.add_argument("--rounds", type=int, default=None, help="Pairs, i.e. 2 games per round")
    p.add_argument("--tc", default="10+0.1", help="Seconds+increment, same for both engines")
    p.add_argument("--hash", type=int, default=16, help="Hash MiB per engine process")
    p.add_argument("--concurrency", type=int, default=1, help="Concurrent games, NOT engine threads")
    p.add_argument("--seed", type=int, default=20261031)
    p.add_argument("--elo0", type=float, default=0)
    p.add_argument("--elo1", type=float, default=5)
    p.add_argument("--alpha", type=float, default=0.05)
    p.add_argument("--beta", type=float, default=0.05)
    p.add_argument("--out", type=Path, default=None)
    p.add_argument("--dry-run", action="store_true", help="Print command only; do not start matches")
    args = p.parse_args(argv)
    if args.ablation:
        if args.baseline or args.baseline_ref or args.candidate_option or args.baseline_option:
            p.error("--ablation uses the same binary on both sides; omit baseline and manual options")
        args.candidate_option, args.baseline_option = ablation_options(
            args.ablation, args.ablation_mode)
    for option in args.candidate_option + args.baseline_option:
        try:
            option_name(option)
        except ValueError as exc:
            p.error(str(exc))
    if args.rounds is None:
        args.rounds = 50000 if args.mode == "sprt" else 100
    if args.rounds <= 0 or args.concurrency <= 0 or args.hash <= 0:
        p.error("rounds, concurrency and hash must be positive")
    if not (0 < args.alpha < 1 and 0 < args.beta < 1 and args.elo0 < args.elo1):
        p.error("SPRT needs 0<alpha,beta<1 and elo0 < elo1")
    if not args.book.is_file():
        p.error(f"opening book not found: {args.book}")
    return args


def main(argv=None):
    args = parse_args(argv)
    candidate = check_binary(args.candidate, "Candidate")
    if not args.dry_run and not (shutil.which(args.fastchess) or Path(args.fastchess).is_file()):
        raise ValueError("Fastchess not found; install it or pass --fastchess PATH")
    with ExitStack() as stack:
        worktree = None
        if args.ablation:
            baseline = candidate
            baseline_ref = "same executable (per-heuristic ablation)"
        elif args.baseline:
            baseline = check_binary(args.baseline, "Baseline")
            baseline_ref = "external executable"
        else:
            if args.dry_run:
                raise ValueError("--dry-run requires --baseline; building a git ref is not a dry run")
            temp = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="chess-baseline-")))
            worktree = temp / "repo"
            baseline_ref = args.baseline_ref or "HEAD^"
            baseline = build_baseline(baseline_ref, worktree, temp / "build")
        try:
            verify_uci_options(candidate, args.candidate_option)
            verify_uci_options(baseline, args.baseline_option)
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            outdir = (args.out or ROOT / "results" / stamp).resolve()
            command = fastchess_command(args, baseline, candidate, outdir / "games.pgn")
            if args.dry_run:
                print(json.dumps(command, indent=2))
                return 0
            outdir.mkdir(parents=True, exist_ok=False)
            metadata = {
                "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "candidate": str(candidate), "candidate_sha256": sha256(candidate),
                "baseline": str(baseline), "baseline_sha256": sha256(baseline),
                "baseline_ref": baseline_ref, "current_revision": current_revision(),
                "candidate_options": args.candidate_option,
                "baseline_options": args.baseline_option,
                "ablation_heuristic": args.ablation,
                "ablation_mode": args.ablation_mode if args.ablation else None,
                "opening_book": str(args.book.resolve()),
                "opening_book_sha256": sha256(args.book),
                "mode": args.mode, "command": command, "rounds_limit": args.rounds,
                "max_games": args.rounds * 2, "seed": args.seed,
                "time_control": args.tc, "hash_mib": args.hash,
                "concurrency": args.concurrency,
                "sprt": {"elo0": args.elo0, "elo1": args.elo1,
                         "alpha": args.alpha, "beta": args.beta} if args.mode == "sprt" else None,
                "status": "running",
            }
            manifest = outdir / "metadata.json"
            manifest.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
            try:
                with (outdir / "fastchess.log").open("w", encoding="utf-8") as log:
                    proc = subprocess.Popen(command, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, text=True,
                                            encoding="utf-8", errors="replace")
                    for line in proc.stdout:
                        print(line, end="", flush=True)
                        log.write(line)
                    status = proc.wait()
                metadata["status"] = "completed" if status == 0 else "failed"
                metadata["exit_code"] = status
                return status
            except BaseException:
                metadata["status"] = "interrupted"
                raise
            finally:
                manifest.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
                print(f"Match artifacts: {outdir}")
        finally:
            if worktree is not None:
                subprocess.run(["git", "worktree", "remove", "--force", str(worktree)],
                               cwd=ROOT, check=False)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(2)
