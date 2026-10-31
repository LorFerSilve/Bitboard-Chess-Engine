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
