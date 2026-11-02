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

    def test_individual_heuristic_ablation(self):
        for feature in st.HEURISTICS:
            for mode in ("isolated", "leave-one-out"):
                args = st.parse_args(["--candidate", str(self.exe),
                                      "--ablation", feature, "--ablation-mode", mode,
                                      "--mode", "fixed", "--rounds", "4"])
                candidate, baseline = st.ablation_options(feature, mode)
                self.assertEqual(args.candidate_option, candidate)
                self.assertEqual(args.baseline_option, baseline)
                self.assertEqual(args.ablation, feature)
                cmd = st.fastchess_command(args, self.exe, self.exe, Path("sample.pgn"))
                self.assertEqual(cmd.count("-engine"), 2)
                first, second = cmd.index("-engine"), cmd.index("-engine", cmd.index("-engine")+1)
                self.assertEqual(cmd[first+1], cmd[second+1])
                self.assertEqual(cmd[first+3:second],
                                 [f"option.{option}" for option in candidate])
                self.assertEqual(cmd[second+3:cmd.index("-each")],
                                 [f"option.{option}" for option in baseline])
                st.verify_uci_options(self.exe, candidate + baseline)

    def test_manual_options_are_per_engine(self):
        args = st.parse_args(["--candidate", str(self.exe), "--baseline", str(self.exe),
                              "--candidate-option", "King Pressure=false",
                              "--baseline-option", "Knight Forks=false",
                              "--mode", "fixed", "--rounds", "2"])
        cmd = st.fastchess_command(args, self.exe, self.exe, Path("sample.pgn"))
        self.assertIn("option.King Pressure=false", cmd)
        self.assertIn("option.Knight Forks=false", cmd)
        self.assertNotEqual(cmd.index("option.King Pressure=false"),
                            cmd.index("option.Knight Forks=false"))

    def test_ablation_does_not_accept_confounded_settings(self):
        with self.assertRaises(SystemExit):
            st.parse_args(["--candidate", str(self.exe),
                           "--baseline", str(self.exe), "--ablation", "King Pressure"])
        with self.assertRaises(SystemExit):
            st.parse_args(["--candidate", str(self.exe), "--ablation", "King Pressure",
                           "--candidate-option", "Knight Forks=false"])
        with self.assertRaises(SystemExit):
            st.parse_args(["--candidate", str(self.exe), "--candidate-option", "invalid"])
        with self.assertRaises(ValueError):
            st.verify_uci_options(self.exe, ["Not a Real UCI Option=false"])

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
