#!/usr/bin/env python3
"""Generate scoped audio evidence for Cubase or an independent stress run."""
import argparse
import json
import math
from pathlib import Path
from audio_fixture import analyse, compare, read_audio


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
                         "An energetic file endpoint requires tail/export-boundary review, not an automatic plugin-bug finding."]}
    result["same_host_comparisons"] = []
    if any(p.name.endswith("Cubase.wav") or "Cubase-" in p.name for p in files):
        for before, after, seconds in [
            ("01-SubLab808-Cubase.wav", "03-SubLab808-Cubase-tail2s.wav", 16),
            ("01-SubLab808-Cubase.wav", "04-SubLab808-Cubase-after-reload-tail20s.wav", 16),
            ("03-SubLab808-Cubase-tail2s.wav", "04-SubLab808-Cubase-after-reload-tail20s.wav", 18),
        ]:
            if (root / before).is_file() and (root / after).is_file():
                result["same_host_comparisons"].append(fixture_onset_comparison(root / before, root / after, seconds))
        if (root / "02-ReverseLab-Cubase.wav").is_file() and (root / "05-ReverseLab-Cubase-after-reload.wav").is_file():
            result["same_host_comparisons"].append(compare(root / "02-ReverseLab-Cubase.wav", root / "05-ReverseLab-Cubase-after-reload.wav"))
        if (root / "04-SubLab808-Cubase-after-reload-tail20s.wav").is_file():
            result["tail_check"] = check_tail(root / "04-SubLab808-Cubase-after-reload-tail20s.wav")
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
    result["passed"] = result["signal_checks_passed"] and result.get("tail_check", {}).get("passed", True)
    destination = args.output_directory.resolve() if args.output_directory else root
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "audio-analysis.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    lines = ["# Host audio evidence", "", f"Folder: `{root.name}`", "",
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
        lines += ["", f"Same-host comparison `{Path(c['first']).name}` → `{Path(c['second']).name}` over first {c.get('compared_seconds')} seconds: "
                  f"sample-exact={c.get('sample_exact')}; max delta={c.get('max_absolute_difference')}; "
                  f"difference RMS={c.get('difference_rms')}; relative RMS error={c.get('relative_difference_rms')}."]
        outside = c.get("outside_first_20ms_of_fixture_note_onsets")
        if outside:
            lines += ["", f"Outside the first 20 ms of fixture note-on slots: maximum delta {outside['max_absolute_difference']}; "
                      f"difference RMS {outside['difference_rms']}. This localizes any differences; Click PRNG state is a potential cause, "
                      "and must not be represented as sample-exact recall."]
    if "tail_resolution" in result:
        lines += ["", "## Tail result", "", result["tail_resolution"]]
    lines += ["", "## Limits", ""] + [f"- {v}" for v in result["limits"]]
    (destination / "audio-analysis.md").write_text("\n".join(lines) + "\n")
    print(json.dumps({"json": str(destination / "audio-analysis.json"), "markdown": str(destination / "audio-analysis.md"), "passed": result["passed"], "renders": records}, indent=2, allow_nan=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
