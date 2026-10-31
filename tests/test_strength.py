"""Strength tournament command invariants; does not require Fastchess installed."""
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import strength_test as st

ENGINE_EXE = Path(sys.argv[1]).resolve()
sys.argv = [sys.argv[0]]

class StrengthTests(unittest.TestCase):
    exe = ENGINE_EXE

    def test_sprt_command(self):
        self.assertIsNotNone(self.exe)
        args = st.parse_args(["--candidate", str(self.exe), "--baseline", str(self.exe),
                              "--mode", "sprt", "--rounds", "32", "--hash", "32"])
        cmd = st.fastchess_command(args, self.exe, self.exe, Path("sample.pgn"))
        self.assertEqual(cmd.count("-engine"), 2)
        self.assertIn("-sprt", cmd)
        self.assertIn("-repeat", cmd)
        self.assertEqual(cmd[cmd.index("-rounds") + 1], "32")
        self.assertEqual(cmd[cmd.index("-concurrency") + 1], "1")
        self.assertIn("option.Hash=32", cmd)
        self.assertEqual(cmd[cmd.index("-srand") + 1], "20261031")
        self.assertIn("model=logistic", cmd)

    def test_fixed_match(self):
        args = st.parse_args(["--candidate", str(self.exe), "--baseline", str(self.exe),
                              "--mode", "fixed", "--rounds", "12"])
        cmd = st.fastchess_command(args, self.exe, self.exe, Path("sample.pgn"))
        self.assertNotIn("-sprt", cmd)
        self.assertEqual(cmd[cmd.index("-rounds") + 1], "12")

    def test_invalid_sprt_rejected(self):
        with self.assertRaises(SystemExit):
            st.parse_args(["--candidate", str(self.exe), "--baseline", str(self.exe),
                           "--elo0", "10", "--elo1", "0"])

    def test_openings_all_unique_and_playable(self):
        fens = (st.ROOT / "benchmarks/openings/smoke.epd").read_text().splitlines()
        self.assertEqual(len(fens), 16)
        self.assertEqual(len(set(fens)), 16)
        for fen in fens:
            proc = subprocess.run([str(self.exe)], input=f"position fen {fen} 0 1\ngo depth 1\nquit\n",
                                  capture_output=True, text=True, timeout=5, check=True)
            self.assertNotIn("error:", proc.stdout)
            self.assertIn("bestmove ", proc.stdout)

    def test_extended_openings_all_unique_and_playable(self):
        fens = (st.ROOT / "benchmarks/openings/extended.epd").read_text().splitlines()
        self.assertGreaterEqual(len(fens), 80)
        self.assertEqual(len(set(fens)), len(fens))
        for fen in fens:
            proc = subprocess.run([str(self.exe)],
                                  input=f"position fen {fen} 0 1\nd\nquit\n",
                                  capture_output=True, text=True, timeout=5, check=True)
            self.assertNotIn("error:", proc.stdout, fen)
            self.assertIn(fen, proc.stdout, fen)

if __name__ == "__main__":
    unittest.main()
