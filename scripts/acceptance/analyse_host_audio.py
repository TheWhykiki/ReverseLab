#!/usr/bin/env python3
"""Generate scoped audio evidence for Cubase or an independent stress run."""
import argparse
import json
import math
from pathlib import Path
from audio_fixture import analyse, compare, read_audio

SUB_BASE = "01-SubLab808-Cubase.wav"
SUB_TAIL = "03-SubLab808-Cubase-tail2s.wav"
SUB_RELOAD = "04-SubLab808-Cubase-after-reload-tail20s.wav"
REVERSE_BASE = "02-ReverseLab-Cubase.wav"
REVERSE_RELOAD = "05-ReverseLab-Cubase-after-reload.wav"
COMPARISONS = (
    ("sublab808", "export_consistency", SUB_BASE, SUB_TAIL, 16),
    ("sublab808", "recall", SUB_BASE, SUB_RELOAD, 16),
    ("sublab808", "recall", SUB_TAIL, SUB_RELOAD, 18),
    ("reverselab", "recall", REVERSE_BASE, REVERSE_RELOAD, None),
)
REQUIRED_RECALL_FILES = {
    "sublab808": (SUB_BASE, SUB_RELOAD),
    "reverselab": (REVERSE_BASE, REVERSE_RELOAD),
}


def recall_tolerance(value):
    tolerance = float(value)
    if not math.isfinite(tolerance) or tolerance < 0:
        raise argparse.ArgumentTypeError("Recall tolerance must be finite and non-negative")
    return tolerance


def check_comparisons(root, tolerance, required_plugins):
    """Gate every available pair and report recall coverage for each plugin separately."""
    comparisons = []
    for plugin, purpose, before, after, seconds in COMPARISONS:
        if not (root / before).is_file() or not (root / after).is_file():
            continue
        comparison = (fixture_onset_comparison(root / before, root / after, seconds)
                      if seconds is not None else compare(root / before, root / after))
        comparison.update(plugin=plugin, purpose=purpose, required_seconds=seconds,
                          max_allowed_absolute_difference=tolerance)
        if not comparison["compatible"]:
            reason = ("Sample rates/channels must match; both files must cover the comparison interval"
                      if seconds is not None else "Sample rates, channels and full file frame counts must match")
        elif not comparison.get("samples_finite", False):
            reason = "Nonfinite samples invalidate the comparison"
        elif comparison["compared_frames"] == 0:
            reason = "Empty audio cannot establish a comparison"
        elif comparison["max_absolute_difference"] > tolerance:
            reason = "Maximum absolute sample difference exceeds the configured tolerance"
        else:
            reason = None
        comparison.update(passed=reason is None, reason=reason)
        comparisons.append(comparison)

    plugins = {}
    for plugin, expected in REQUIRED_RECALL_FILES.items():
        recalls = [c for c in comparisons if c["plugin"] == plugin and c["purpose"] == "recall"]
        missing = [name for name in expected if not (root / name).is_file()]
        required = plugin in required_plugins
        if (required and missing) or any(not c["passed"] for c in recalls):
            status = "failed"
        else:
            status = "passed" if recalls else "not_checked"
        plugins[plugin] = {"required": required, "status": status,
                          "passed": None if status == "not_checked" else status == "passed",
                          "comparisons_checked": len(recalls),
                          "required_files": list(expected) if required else [],
                          "missing_required_files": missing if required else []}
    statuses = [p["status"] for p in plugins.values()]
    status = "failed" if "failed" in statuses else "passed" if "passed" in statuses else "not_checked"
    return comparisons, {"status": status, "passed": None if status == "not_checked" else status == "passed",
                         "required_plugins": required_plugins, "plugins": plugins}


def fixture_onset_comparison(first, second, seconds):
    result = compare(first, second, seconds=seconds)
    if not result["compatible"] or not result.get("samples_finite", False):
        return result
    a, av = read_audio(first)
    _, bv = read_audio(second)
    outside_max, outside_square, outside_count = 0.0, 0.0, 0
    for i in range(round(seconds * a["sample_rate"]) * a["channels"]):
        t = (i // a["channels"]) / a["sample_rate"]
        onset_index = math.floor((t - .5) / .5)
        elapsed = t - (.5 + onset_index * .5)
        onset_window = 0 <= onset_index < 28 and 0 <= elapsed < .02
        if not onset_window:
            difference = av[i] - bv[i]
            outside_max = max(outside_max, abs(difference))
            outside_square += difference * difference
            outside_count += 1
    result["outside_first_20ms_of_fixture_note_onsets"] = {
        "samples": outside_count, "max_absolute_difference": outside_max,
        "difference_rms": math.sqrt(outside_square / max(1, outside_count)),
        "scope": "Fixture note-on slots at 0.5 + n * 0.5 seconds, n=0..27; verifies where differences occur, not their cause."}
    return result


def check_tail(path, expected_seconds=36.0, silence_start=30.0):
    """Verify this fixture's full capture and each tail sample, never its filename alone."""
    metadata, samples = read_audio(path)
    rate, channels = metadata["sample_rate"], metadata["channels"]
    expected_frames = round(expected_seconds * rate)
    start_frame = round(silence_start * rate)
    if expected_frames <= start_frame or start_frame < 0:
        raise ValueError("Tail interval must be nonempty and within the expected render")
    end_sample, start_sample = expected_frames * channels, start_frame * channels
    tail = samples[start_sample:end_sample]
    prefix = samples[:start_sample]
    finite = all(math.isfinite(value) for value in samples)
    result = {
        "path": metadata["path"], "sha256": metadata["sha256"],
        "expected_frames": expected_frames, "actual_frames": metadata["frames"],
        "silence_start_frame": start_frame,
        "duration_matches": metadata["frames"] == expected_frames,
        "all_samples_finite": finite,
        "input_region_has_signal": any(math.isfinite(value) and abs(value) > 1e-5 for value in prefix),
        "tail_samples_checked": len(tail),
        "tail_region_complete": len(tail) == end_sample - start_sample,
        "tail_exactly_zero": bool(tail) and all(value == 0.0 for value in tail),
        "final_frame_exactly_zero": len(samples) >= channels and all(value == 0.0 for value in samples[-channels:]),
    }
    result["passed"] = all(result[key] for key in (
        "duration_matches", "all_samples_finite", "input_region_has_signal", "tail_region_complete",
        "tail_exactly_zero", "final_frame_exactly_zero"))
    return result


def format_db(value):
    return "-inf" if value is None else f"{value:.2f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output-directory", type=Path, help="Write reports separately from the original evidence")
    parser.add_argument("--require-recall", choices=("sublab808", "reverselab", "both"),
                        help="Fail unless the selected plugins have their complete baseline/reload evidence pairs")
    parser.add_argument("--recall-tolerance", type=recall_tolerance, default=0.0,
                        help="Maximum absolute sample difference for every same-host comparison (default: 0, exact)")
    args = parser.parse_args()
    root = args.directory.resolve()
    if not root.is_dir():
        parser.error("The audio directory does not exist")
    files = sorted(root.glob("*.wav"))
    if not files:
        parser.error("No WAV evidence files found")
    records = [analyse(p) for p in files]
    result = {"directory": str(root), "renders": records,
              "limits": ["No cross-host waveform equality is asserted: settings and sample rates differ.",
                         "Signal metrics are not subjective listening or user-interface acceptance.",
                         "Without --require-recall, missing pairs do not fail; consult each plugin's recall status.",
                         "Recall checks cover the named fixture comparisons only, not all presets or DAW state.",
                         "An energetic file endpoint requires tail/export-boundary review, not an automatic plugin-bug finding."]}
    required_plugins = (list(REQUIRED_RECALL_FILES) if args.require_recall == "both"
                        else [args.require_recall] if args.require_recall else [])
    result["recall_tolerance"] = args.recall_tolerance
    result["same_host_comparisons"], result["recall_check"] = check_comparisons(
        root, args.recall_tolerance, required_plugins)
    if (root / SUB_RELOAD).is_file():
        result["tail_check"] = check_tail(root / SUB_RELOAD)
        if result["tail_check"]["passed"]:
            result["tail_resolution"] = (
                "PASS: measured 36-second render with preceding signal; every sample in seconds 30–36 "
                "and the final frame is exactly zero. All file samples are finite.")
        else:
            result["tail_resolution"] = (
                "FAIL: the expected complete 36-second render with a silent 30–36-second tail "
                "is not established. See the measured tail_check conditions; filename is not evidence.")
    summary = root / "stress-summary.txt"
    if summary.exists():
        result["host_summary"] = summary.read_text()
    result["signal_checks_passed"] = all(r["basic_signal_ok"] for r in records)
    result["comparison_checks_passed"] = (all(c["passed"] for c in result["same_host_comparisons"])
                                          if result["same_host_comparisons"] else None)
    result["passed"] = (result["signal_checks_passed"] and result.get("tail_check", {}).get("passed", True)
                        and result["comparison_checks_passed"] is not False
                        and result["recall_check"]["passed"] is not False)
    destination = args.output_directory.resolve() if args.output_directory else root
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "audio-analysis.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    lines = ["# Host audio evidence", "", f"Folder: `{root.name}`", "",
             f"Overall checks: **{'PASS' if result['passed'] else 'FAIL'}**. "
             f"Signal checks: {'PASS' if result['signal_checks_passed'] else 'FAIL'}. "
             f"Recall: **{result['recall_check']['status'].upper()}**.", "",
             f"Maximum allowed absolute sample difference: {args.recall_tolerance}. "
             "Every available same-host comparison gates the overall result; note-on samples are included.", ""]
    for plugin, check in result["recall_check"]["plugins"].items():
        lines.append(f"- {plugin}: {check['status'].upper()}; required={check['required']}; "
                     f"recall comparisons checked={check['comparisons_checked']}.")
        if check["missing_required_files"]:
            lines.append("  Missing required files: " + ", ".join(f"`{name}`" for name in check["missing_required_files"]) + ".")
    lines += ["",
             "| File | Rate | Encoding | Seconds | Peak dBFS | RMS dBFS | Nonfinite | Full-scale | Last100ms RMS |",
             "|---|---:|---|---:|---:|---:|---:|---:|---:|"]
    for r in records:
        encoding = ("float" if r["encoding"] == 3 else "PCM") + str(r["bits"])
        lines.append(f"| {Path(r['path']).name} | {r['sample_rate']} | {encoding} | {r['seconds']:.6f} | {format_db(r['peak_dbfs'])} | {format_db(r['rms_dbfs'])} | {r['nonfinite_samples']} | {r['full_scale_samples']} | {r['tail_last_100ms_rms']:.8f} |")
    if "host_summary" in result:
        lines += ["", "## Host run summary", "", "```text", result["host_summary"].rstrip(), "```"]
    for r in records:
        if max(map(abs, r["final_frame"]), default=0) > 1e-3:
            lines += ["", f"Boundary observation: `{Path(r['path']).name}` ends with frame {r['final_frame']}; "
                      "it does not end in silence. Check the requested render range/tail settings before assessing tail preservation."]
    for c in result["same_host_comparisons"]:
        scope = f"first {c['required_seconds']} seconds" if c["required_seconds"] is not None else "entire files, equal frame counts required"
        lines += ["", f"**{'PASS' if c['passed'] else 'FAIL'}** ({c['purpose']}): "
                  f"`{Path(c['first']).name}` → `{Path(c['second']).name}` ({scope}): "
                  f"compatible={c['compatible']}; "
                  f"sample-exact={c.get('sample_exact')}; max delta={c.get('max_absolute_difference')}; "
                  f"difference RMS={c.get('difference_rms')}; relative RMS error={c.get('relative_difference_rms')}."]
        if c["reason"]:
            lines.append(c["reason"] + ".")
        outside = c.get("outside_first_20ms_of_fixture_note_onsets")
        if outside:
            lines += ["", f"Outside the first 20 ms of fixture note-on slots: maximum delta {outside['max_absolute_difference']}; "
                      f"difference RMS {outside['difference_rms']}. This diagnostic localizes differences without establishing "
                      "their cause or excluding any samples from the pass/fail check."]
    if "tail_resolution" in result:
        lines += ["", "## Tail result", "", result["tail_resolution"]]
    lines += ["", "## Limits", ""] + [f"- {v}" for v in result["limits"]]
    (destination / "audio-analysis.md").write_text("\n".join(lines) + "\n")
    print(json.dumps({"json": str(destination / "audio-analysis.json"), "markdown": str(destination / "audio-analysis.md"),
                      "passed": result["passed"], "signal_checks_passed": result["signal_checks_passed"],
                      "comparison_checks_passed": result["comparison_checks_passed"],
                      "recall_tolerance": result["recall_tolerance"], "recall_check": result["recall_check"],
                      "renders": records}, indent=2, allow_nan=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
