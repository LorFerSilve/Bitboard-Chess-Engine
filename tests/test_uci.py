"""UCI integration and deterministic immediate-stop regression test."""
import subprocess
import sys

engine = sys.argv[1]

for i in range(15):
    result = subprocess.run(
        [engine],
        input="uci\nisready\nposition startpos\ngo infinite\nstop\nquit\n",
        text=True,
        capture_output=True,
        timeout=3,
        check=True,
    )
    assert "option name SyzygyPath" in result.stdout
    assert "option name Move Overhead" in result.stdout
    assert "uciok" in result.stdout
    assert "readyok" in result.stdout
    assert "bestmove " in result.stdout, result.stdout

# Verify a completed depth search and terminal result separately.
with subprocess.Popen([engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                      stderr=subprocess.PIPE, text=True, bufsize=1) as proc:
    proc.stdin.write("position startpos\ngo depth 5\n")
    proc.stdin.flush()
    output = []
    # Depth 5 is short and terminates even on a slow CI runner.
    for _ in range(30):
        line = proc.stdout.readline().strip()
        output.append(line)
        if line.startswith("bestmove "):
            break
    assert any(line.startswith("info depth 5 ") for line in output), output
    assert any(line.startswith("bestmove ") for line in output), output
    proc.stdin.write("quit\n")
    proc.stdin.flush()
    assert proc.wait(timeout=3) == 0

for fen in [
    "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1",  # checkmate
    "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",  # stalemate
]:
    r = subprocess.run([engine], input="position fen " + fen + "\ngo depth 2\nquit\n",
                       text=True, capture_output=True, timeout=3, check=True)
    assert "bestmove 0000" in r.stdout, r.stdout

# Every experimental search/evaluation heuristic must be exposed via UCI and
# configurable independently without silently ignoring unsupported flags.
heuristics = (
    "Clustered TT", "Null Verification", "History LMR", "Adaptive Aspiration",
    "SEE Prefilter", "King Pressure", "Pawn Support", "Weak Pawns",
    "Pawn Forks", "Knight Forks", "QSearch SEE Pruning",
)
commands = ["uci", "setoption name All Heuristics value false"]
for name in heuristics:
    commands.extend([f"setoption name {name} value true",
                     f"setoption name {name} value false"])
commands.extend(["setoption name All Heuristics value true", "isready", "quit"])
r = subprocess.run([engine], input="\n".join(commands) + "\n",
                   text=True, capture_output=True, timeout=8, check=True)
assert "info string error" not in r.stdout, r.stdout
assert "info string heuristic All Heuristics=false" in r.stdout
for name in heuristics:
    assert f"option name {name} type check default true" in r.stdout, name
    assert f"info string heuristic {name}=true" in r.stdout, name
    assert f"info string heuristic {name}=false" in r.stdout, name

print("UCI integration tests passed")

# No tablebase data or external dependency is required for ordinary builds.
r = subprocess.run([engine], input="setoption name SyzygyPath value /unused/path\nquit\n",
                   text=True, capture_output=True, timeout=3, check=True)
# If built with Syzygy enabled, Fathom may just report zero available files.
assert "Syzygy" in r.stdout, r.stdout

# Clock management must keep an allocated move below a near-expired clock.
# A zero-overhead option also remains accepted by the UCI adapter.
r = subprocess.run(
    [engine],
    input="uci\nsetoption name Move Overhead value 0\nposition startpos\ngo wtime 100 btime 100 winc 0 binc 0 movestogo 30\n",
    text=True, capture_output=True, timeout=5, check=True,
)
assert "bestmove " in r.stdout, r.stdout
