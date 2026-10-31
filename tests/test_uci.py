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
