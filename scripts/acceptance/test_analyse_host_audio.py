"""Regression tests for evidence generation (no DAW or external packages required)."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

import analyse_host_audio as report


class AcceptanceEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="reverselab-evidence-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cubase = self.root / "Cubase"
        self.cubase.mkdir()
        self.tail_file = self.cubase / "04-SubLab808-Cubase-after-reload-tail20s.wav"

    def write_float_wave(self, seconds=36, overrides=None):
        rate, channels = 100, 2
        samples = [0.125 if frame < 20 else 0.0
                   for frame in range(round(seconds * rate)) for _ in range(channels)]
        for index, value in (overrides or {}).items():
            samples[index] = value
        data = struct.pack("<" + "f" * len(samples), *samples)
        fmt = struct.pack("<HHIIHH", 3, channels, rate, rate * channels * 4, channels * 4, 32)
        body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
        self.tail_file.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)

    def run_report(self):
        with patch.object(sys, "argv", ["analyse_host_audio.py", str(self.cubase)]), \
             patch.object(report, "__file__", str(self.root / "analyse_host_audio.py")), \
             contextlib.redirect_stdout(io.StringIO()):
            report.main()
        return json.loads((self.cubase / "audio-analysis.json").read_text())

    def test_misnamed_short_file_cannot_claim_full_tail(self):
        self.write_float_wave(seconds=1)
        result = self.run_report()
        self.assertNotIn("seconds 30–36 and the final frame are exactly zero",
                         result.get("tail_resolution", ""))
        self.assertFalse(result["tail_check"]["passed"])
        self.assertFalse(result["tail_check"]["duration_matches"])
        self.assertFalse(result["passed"])

    def test_actual_complete_tail_passes(self):
        self.write_float_wave()
        result = self.run_report()
        self.assertTrue(result["passed"])
        self.assertTrue(result["tail_check"]["passed"])
        self.assertEqual(result["tail_check"]["tail_samples_checked"], 1200)

    def test_nonzero_tail_sample_is_not_hidden_by_zero_last_frame(self):
        self.write_float_wave(overrides={6101: 0.001})
        result = self.run_report()
        self.assertTrue(result["tail_check"]["final_frame_exactly_zero"])
        self.assertFalse(result["tail_check"]["tail_exactly_zero"])
        self.assertFalse(result["passed"])

    def test_nan_or_inf_cannot_be_sanitised_into_tail_success(self):
        for invalid in (float("nan"), float("inf"), -float("inf")):
            for index in (1, 6101, 7199):
                with self.subTest(value=invalid, index=index):
                    self.write_float_wave(overrides={index: invalid})
                    result = self.run_report()
                    self.assertFalse(result["tail_check"]["all_samples_finite"])
                    self.assertFalse(result["passed"])

    def test_zero_or_short_data_is_not_success(self):
        for duration in (0, 30, 35.99, 36.01):
            with self.subTest(duration=duration):
                self.write_float_wave(seconds=duration)
                self.assertFalse(self.run_report()["passed"])

    def test_silent_entire_file_does_not_prove_decay(self):
        self.write_float_wave(overrides={i: 0 for i in range(40)})
        result = self.run_report()
        self.assertFalse(result["passed"])
        self.assertFalse(result["tail_check"]["input_region_has_signal"])
        self.assertIn("-inf", (self.cubase / "audio-analysis.md").read_text())

    def test_nonfinite_waveform_comparison_is_explicitly_invalid(self):
        self.write_float_wave(overrides={1: float("nan")})
        result = report.compare(self.tail_file, self.tail_file)
        self.assertFalse(result["samples_finite"])
        self.assertFalse(result["sample_exact"])
        json.dumps(result, allow_nan=False)

    def test_truncated_wave_rejected(self):
        self.write_float_wave()
        data = self.tail_file.read_bytes()
        self.tail_file.write_bytes(data[:-1])
        with self.assertRaises(ValueError):
            self.run_report()


if __name__ == "__main__":
    unittest.main()
