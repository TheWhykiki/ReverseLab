"""Regression tests for evidence generation (no DAW or external packages required)."""
import contextlib
import io
import json
from pathlib import Path
import struct
import subprocess
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

    def write_float_wave(self, seconds=36, overrides=None, path=None, rate=100, channels=2):
        samples = [0.125 if frame < 20 else 0.0
                   for frame in range(round(seconds * rate)) for _ in range(channels)]
        for index, value in (overrides or {}).items():
            samples[index] = value
        data = struct.pack("<" + "f" * len(samples), *samples)
        fmt = struct.pack("<HHIIHH", 3, channels, rate, rate * channels * 4, channels * 4, 32)
        body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
        path = path or self.tail_file
        path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
        return path

    def run_report(self, *options):
        output = io.StringIO()
        with patch.object(sys, "argv", ["analyse_host_audio.py", str(self.cubase), *options]), \
             contextlib.redirect_stdout(output):
            self.exit_code = report.main()
        self.cli_result = json.loads(output.getvalue())
        return json.loads((self.cubase / "audio-analysis.json").read_text())

    def test_changed_reload_audio_fails_overall_and_exit_status(self):
        self.write_float_wave(seconds=1, path=self.cubase / "02-ReverseLab-Cubase.wav")
        self.write_float_wave(seconds=1, overrides={10: -0.125},
                              path=self.cubase / "05-ReverseLab-Cubase-after-reload.wav")
        result = self.run_report()
        self.assertTrue(result["signal_checks_passed"])
        self.assertFalse(result["passed"])
        self.assertEqual(self.exit_code, 1)

    def test_shortened_reload_audio_fails_overall_and_exit_status(self):
        self.write_float_wave(seconds=1, path=self.cubase / "02-ReverseLab-Cubase.wav")
        self.write_float_wave(seconds=0.5, path=self.cubase / "05-ReverseLab-Cubase-after-reload.wav")
        result = self.run_report()
        self.assertTrue(result["signal_checks_passed"])
        self.assertFalse(result["passed"])
        self.assertEqual(self.exit_code, 1)

    def test_exact_reverse_recall_passes_with_explicit_coverage(self):
        for name in (report.REVERSE_BASE, report.REVERSE_RELOAD):
            self.write_float_wave(seconds=1, path=self.cubase / name)
        result = self.run_report("--require-recall", "reverselab")
        self.assertTrue(result["passed"])
        self.assertEqual(self.exit_code, 0)
        self.assertEqual(result["recall_check"]["status"], "passed")
        self.assertEqual(result["recall_check"]["plugins"]["sublab808"]["status"], "not_checked")
        self.assertTrue(result["same_host_comparisons"][0]["sample_exact"])
        self.assertEqual(self.cli_result["recall_check"], result["recall_check"])
        self.assertIn("Recall: **PASSED**", (self.cubase / "audio-analysis.md").read_text())

    def test_missing_pair_is_not_checked_by_default_but_fails_when_required(self):
        self.write_float_wave(seconds=1, path=self.cubase / report.REVERSE_BASE)
        result = self.run_report()
        self.assertTrue(result["passed"])
        self.assertEqual(self.exit_code, 0)
        self.assertEqual(result["recall_check"]["status"], "not_checked")
        self.assertIsNone(result["recall_check"]["passed"])
        self.assertIsNone(result["comparison_checks_passed"])
        self.assertIn("Recall: **NOT_CHECKED**", (self.cubase / "audio-analysis.md").read_text())
        result = self.run_report("--require-recall", "reverselab")
        self.assertFalse(result["passed"])
        self.assertEqual(self.exit_code, 1)
        check = result["recall_check"]["plugins"]["reverselab"]
        self.assertEqual(check["status"], "failed")
        self.assertEqual(check["missing_required_files"], [report.REVERSE_RELOAD])
        self.assertIn("Missing required files", (self.cubase / "audio-analysis.md").read_text())

    def test_requiring_both_plugins_cannot_pass_with_only_reverse_evidence(self):
        for name in (report.REVERSE_BASE, report.REVERSE_RELOAD):
            self.write_float_wave(seconds=1, path=self.cubase / name)
        result = self.run_report("--require-recall", "both")
        self.assertTrue(result["comparison_checks_passed"])
        self.assertFalse(result["passed"])
        self.assertEqual(self.exit_code, 1)
        self.assertEqual(result["recall_check"]["status"], "failed")
        self.assertEqual(result["recall_check"]["plugins"]["sublab808"]["missing_required_files"],
                         [report.SUB_BASE, report.SUB_RELOAD])

    def test_matching_both_plugins_passes_without_optional_tail2s_export(self):
        for name, seconds in ((report.SUB_BASE, 16), (report.SUB_RELOAD, 36),
                              (report.REVERSE_BASE, 1), (report.REVERSE_RELOAD, 1)):
            self.write_float_wave(seconds=seconds, path=self.cubase / name)
        result = self.run_report("--require-recall", "both")
        self.assertTrue(result["passed"])
        self.assertEqual(self.exit_code, 0)
        self.assertTrue(all(p["passed"] for p in result["recall_check"]["plugins"].values()))

    def test_sub_recall_uses_shared_prefix_and_checks_tail_separately(self):
        self.write_float_wave(seconds=16, path=self.cubase / report.SUB_BASE)
        self.write_float_wave(seconds=18, path=self.cubase / report.SUB_TAIL)
        self.write_float_wave(seconds=36, overrides={4001: 0.125})
        result = self.run_report("--require-recall", "sublab808")
        self.assertTrue(result["passed"])
        self.assertEqual([c["compared_seconds"] for c in result["same_host_comparisons"]], [16, 16, 18])
        self.assertTrue(result["tail_check"]["passed"])

    def test_sub_prefix_must_be_complete(self):
        self.write_float_wave(seconds=15.99, path=self.cubase / report.SUB_BASE)
        self.write_float_wave()
        result = self.run_report("--require-recall", "sublab808")
        self.assertTrue(result["tail_check"]["passed"])
        self.assertFalse(result["same_host_comparisons"][0]["compatible"])
        self.assertFalse(result["recall_check"]["passed"])
        self.assertFalse(result["passed"])

    def test_optional_extended_sub_comparison_failure_also_gates_overall(self):
        self.write_float_wave(seconds=16, path=self.cubase / report.SUB_BASE)
        self.write_float_wave(seconds=18, path=self.cubase / report.SUB_TAIL)
        self.write_float_wave(overrides={3401: 0.125})  # Difference at 17 seconds, past the baseline.
        result = self.run_report()
        self.assertEqual([c["passed"] for c in result["same_host_comparisons"]], [True, True, False])
        self.assertFalse(result["recall_check"]["passed"])
        self.assertFalse(result["passed"])

    def test_sub_optional_tail_reference_cannot_replace_required_baseline(self):
        self.write_float_wave(seconds=18, path=self.cubase / report.SUB_TAIL)
        self.write_float_wave()
        self.assertTrue(self.run_report()["recall_check"]["passed"])
        result = self.run_report("--require-recall", "sublab808")
        self.assertFalse(result["passed"])
        self.assertEqual(result["recall_check"]["plugins"]["sublab808"]["missing_required_files"],
                         [report.SUB_BASE])

    def test_note_on_diagnostic_cannot_hide_mismatch(self):
        self.write_float_wave(seconds=16, path=self.cubase / report.SUB_BASE)
        self.write_float_wave(overrides={102: 0.125})  # 0.51 seconds: inside a note-on window.
        result = self.run_report()
        comparison = result["same_host_comparisons"][0]
        self.assertEqual(comparison["outside_first_20ms_of_fixture_note_onsets"]["max_absolute_difference"], 0)
        self.assertFalse(comparison["passed"])
        self.assertFalse(result["passed"])

    def test_export_comparison_also_gates_overall_without_claiming_recall(self):
        self.write_float_wave(seconds=16, path=self.cubase / report.SUB_BASE)
        self.write_float_wave(seconds=18, overrides={80: 0.125}, path=self.cubase / report.SUB_TAIL)
        result = self.run_report()
        self.assertTrue(result["signal_checks_passed"])
        self.assertFalse(result["passed"])
        self.assertEqual(result["recall_check"]["status"], "not_checked")
        self.assertEqual(result["same_host_comparisons"][0]["purpose"], "export_consistency")

    def test_mismatched_rate_or_channels_fail_even_with_large_tolerance(self):
        self.write_float_wave(seconds=1, path=self.cubase / report.REVERSE_BASE)
        for rate, channels in ((200, 2), (100, 1)):
            with self.subTest(rate=rate, channels=channels):
                self.write_float_wave(seconds=1, rate=rate, channels=channels,
                                      path=self.cubase / report.REVERSE_RELOAD)
                result = self.run_report("--recall-tolerance", "1")
                self.assertFalse(result["same_host_comparisons"][0]["compatible"])
                self.assertFalse(result["passed"])
                self.assertEqual(self.exit_code, 1)

    def test_nonfinite_reload_cannot_pass_with_tolerance(self):
        self.write_float_wave(seconds=1, path=self.cubase / report.REVERSE_BASE)
        for value in (float("nan"), float("inf"), -float("inf")):
            with self.subTest(value=value):
                self.write_float_wave(seconds=1, overrides={1: value}, path=self.cubase / report.REVERSE_RELOAD)
                result = self.run_report("--recall-tolerance", "1")
                self.assertFalse(result["recall_check"]["passed"])
                self.assertFalse(result["passed"])
                self.assertEqual(self.exit_code, 1)

    def test_tolerance_is_explicit_inclusive_and_keeps_sample_exact_false(self):
        delta = 1 / 1024  # Exactly representable in the WAV and the CLI argument.
        self.write_float_wave(seconds=1, path=self.cubase / report.REVERSE_BASE)
        self.write_float_wave(seconds=1, overrides={10: 0.125 + delta}, path=self.cubase / report.REVERSE_RELOAD)
        for tolerance, expected in ((0, False), (delta / 2, False), (delta, True), (delta * 2, True)):
            with self.subTest(tolerance=tolerance):
                result = self.run_report("--recall-tolerance", str(tolerance))
                self.assertEqual(result["passed"], expected)
                self.assertEqual(self.exit_code, 0 if expected else 1)
                self.assertEqual(result["recall_tolerance"], tolerance)
                comparison = result["same_host_comparisons"][0]
                self.assertFalse(comparison["sample_exact"])
                self.assertEqual(comparison["max_absolute_difference"], delta)

    def test_invalid_tolerances_are_usage_errors(self):
        self.write_float_wave(seconds=1, path=self.cubase / report.REVERSE_BASE)
        for value in ("-0.1", "nan", "inf", "-inf", "1e309", "invalid"):
            with self.subTest(value=value), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    self.run_report("--recall-tolerance=" + value)
                self.assertEqual(error.exception.code, 2)
                self.assertFalse((self.cubase / "audio-analysis.json").exists())

    def test_empty_pair_is_not_recall_evidence(self):
        for name in (report.REVERSE_BASE, report.REVERSE_RELOAD):
            self.write_float_wave(seconds=0, path=self.cubase / name)
        result = self.run_report()
        self.assertFalse(result["recall_check"]["passed"])
        self.assertFalse(result["passed"])

    def test_process_exit_and_separate_reports_preserve_original_evidence(self):
        before = self.write_float_wave(seconds=1, path=self.cubase / report.REVERSE_BASE)
        after = self.cubase / report.REVERSE_RELOAD
        output = self.root / "reports"
        for duration, overrides, expected_exit in ((1, None, 0), (1, {10: -0.125}, 1), (0.5, None, 1)):
            with self.subTest(duration=duration, overrides=overrides):
                self.write_float_wave(seconds=duration, overrides=overrides, path=after)
                originals = {path: path.read_bytes() for path in (before, after)}
                process = subprocess.run([sys.executable, "-B", report.__file__, str(self.cubase),
                                          "--output-directory", str(output)], capture_output=True, text=True)
                self.assertEqual(process.returncode, expected_exit, process.stderr)
                cli = json.loads(process.stdout)
                saved = json.loads((output / "audio-analysis.json").read_text())
                self.assertEqual(cli["passed"], expected_exit == 0)
                self.assertEqual(cli["recall_check"], saved["recall_check"])
                self.assertEqual(saved["passed"], expected_exit == 0)
                self.assertIn("Overall checks: **" + ("PASS" if expected_exit == 0 else "FAIL"),
                              (output / "audio-analysis.md").read_text())
                self.assertFalse((self.cubase / "audio-analysis.json").exists())
                for path, original in originals.items():
                    self.assertEqual(path.read_bytes(), original)

    def test_corrupt_export_replaces_previous_pass_report(self):
        for name in (report.REVERSE_BASE, report.REVERSE_RELOAD):
            self.write_float_wave(seconds=1, path=self.cubase / name)
        command = [sys.executable, "-B", report.__file__, str(self.cubase)]
        previous = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(previous.returncode, 0, previous.stderr)
        previous_id = json.loads(previous.stdout).get("run_id")
        after = self.cubase / report.REVERSE_RELOAD
        after.write_bytes(after.read_bytes()[:-1])
        current = subprocess.run(command, capture_output=True, text=True)
        saved = json.loads((self.cubase / "audio-analysis.json").read_text())
        self.assertFalse(saved["passed"])
        self.assertEqual(current.returncode, 1)
        cli = json.loads(current.stdout)
        self.assertFalse(cli["passed"])
        self.assertFalse(saved["analysis_completed"])
        self.assertEqual(cli["run_id"], saved["run_id"])
        self.assertNotEqual(saved["run_id"], previous_id)
        self.assertTrue(saved["analysed_at_utc"].endswith("+00:00"))
        self.assertIn(saved["run_id"], (self.cubase / "audio-analysis.md").read_text())
        self.assertFalse(saved["recall_check"]["passed"])
        self.assertIn("Overall checks: **FAIL**", (self.cubase / "audio-analysis.md").read_text())

    def test_empty_evidence_directory_replaces_previous_pass_report(self):
        path = self.write_float_wave(seconds=1, path=self.cubase / "standalone.wav")
        command = [sys.executable, "-B", report.__file__, str(self.cubase)]
        previous = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(previous.returncode, 0, previous.stderr)
        path.unlink()
        current = subprocess.run(command, capture_output=True, text=True)
        saved = json.loads((self.cubase / "audio-analysis.json").read_text())
        self.assertFalse(saved["passed"])
        self.assertEqual(current.returncode, 1)
        self.assertFalse(json.loads(current.stdout)["passed"])

    def test_unsupported_extra_wave_does_not_hide_valid_pair_or_input_error(self):
        for name in (report.REVERSE_BASE, report.REVERSE_RELOAD):
            self.write_float_wave(seconds=1, path=self.cubase / name)
        unsupported = self.write_float_wave(seconds=1, path=self.cubase / "extra.wav")
        data = bytearray(unsupported.read_bytes())
        struct.pack_into("<H", data, 20, 6)  # Unsupported A-law encoding.
        unsupported.write_bytes(data)
        result = self.run_report()
        self.assertEqual(len(result["renders"]), 2)
        self.assertTrue(result["recall_check"]["passed"])
        self.assertFalse(result["analysis_completed"])
        self.assertFalse(result["passed"])
        self.assertEqual(self.exit_code, 1)
        self.assertEqual(result["input_errors"][0]["paths"], [str(unsupported.resolve())])
        self.assertIn("Unsupported", result["input_errors"][0]["message"])
        self.assertEqual(unsupported.read_bytes(), data)

    def test_unreadable_required_wave_has_an_explicit_error(self):
        for name in (report.REVERSE_BASE, report.REVERSE_RELOAD):
            self.write_float_wave(seconds=1, path=self.cubase / name)
        original_analyse = report.analyse

        def deny_reload(path):
            if path.name == report.REVERSE_RELOAD:
                raise PermissionError("Access denied to reload export")
            return original_analyse(path)

        with patch.object(report, "analyse", side_effect=deny_reload):
            result = self.run_report("--require-recall", "reverselab")
        self.assertFalse(result["passed"])
        self.assertFalse(result["analysis_completed"])
        self.assertFalse(result["recall_check"]["passed"])
        self.assertEqual(result["input_errors"][0]["error_type"], "PermissionError")
        self.assertIsNone(result["same_host_comparisons"][0]["compatible"])
        self.assertIn("Input errors", (self.cubase / "audio-analysis.md").read_text())

    def test_file_read_failure_during_comparison_is_reported(self):
        for name in (report.REVERSE_BASE, report.REVERSE_RELOAD):
            self.write_float_wave(seconds=1, path=self.cubase / name)
        with patch.object(report, "compare", side_effect=FileNotFoundError("Export removed during analysis")):
            result = self.run_report()
        self.assertTrue(result["signal_checks_passed"])
        self.assertFalse(result["analysis_completed"])
        self.assertFalse(result["passed"])
        self.assertFalse(result["same_host_comparisons"][0]["passed"])
        self.assertEqual(result["input_errors"][0]["stage"], "comparison")

    def test_file_read_failure_during_tail_check_is_reported(self):
        self.write_float_wave()
        with patch.object(report, "check_tail", side_effect=OSError("Tail export became unavailable")):
            result = self.run_report()
        self.assertTrue(result["signal_checks_passed"])
        self.assertFalse(result["analysis_completed"])
        self.assertFalse(result["passed"])
        self.assertFalse(result["tail_check"]["passed"])
        self.assertEqual(result["input_errors"][0]["stage"], "tail")

    def test_unreadable_host_summary_cannot_leave_a_passing_report(self):
        self.write_float_wave(seconds=1, path=self.cubase / "standalone.wav")
        self.assertTrue(self.run_report()["passed"])
        (self.cubase / "stress-summary.txt").write_bytes(b"\xff\xfe")
        result = self.run_report()
        self.assertTrue(result["signal_checks_passed"])
        self.assertFalse(result["analysis_completed"])
        self.assertFalse(result["passed"])
        self.assertEqual(result["input_errors"][0]["stage"], "host_summary")

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
        result = self.run_report()
        self.assertFalse(result["passed"])
        self.assertFalse(result["analysis_completed"])
        self.assertFalse(result["tail_check"]["passed"])
        self.assertEqual(self.exit_code, 1)
        self.assertEqual(result["input_errors"][0]["paths"], [str(self.tail_file.resolve())])
        self.assertIn("Truncated", result["input_errors"][0]["message"])


if __name__ == "__main__":
    unittest.main()
