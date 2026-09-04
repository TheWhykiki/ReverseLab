# Host audio acceptance reports

Run `analyse_host_audio.py` against a directory of WAV exports. It writes
`audio-analysis.json` and `audio-analysis.md`, prints a JSON summary, and exits
with `0` when the evaluated checks pass or `1` when they fail. Invalid CLI
arguments or a nonexistent input directory produce a usage error (exit `2`).
An existing directory without WAV evidence, malformed/unsupported/unreadable
WAVs, and errors reading a present host summary produce a current **FAIL**
report and exit `1`. Readable WAVs are still analyzed; input errors cannot be
silently omitted from the overall result.

Each report includes a unique `run_id` and `analysed_at_utc` timestamp. The
JSON report, Markdown report, and CLI summary share that identity. A later
failed evidence run replaces an earlier passing report in the selected output
directory. For an audit trail, choose a new `--output-directory` for each run.
Always check the process exit code: invalid CLI arguments and output write
failures cannot guarantee a fresh report. If a write fails partway through,
report files can belong to different runs; check their `run_id` values.

From the repository root:

```sh
# Inspect available evidence, including all recognized comparison pairs.
python3 -B scripts/acceptance/analyse_host_audio.py /path/to/Cubase

# Require baseline/reload evidence for both plugins and keep original reports.
python3 -B scripts/acceptance/analyse_host_audio.py /path/to/Cubase \
  --require-recall both --output-directory /path/to/new-reports
```

`--require-recall` accepts `sublab808`, `reverselab`, or `both`. The required
filenames and comparison intervals are:

| Plugin | Before reload | After reload | Compared audio |
|---|---|---|---|
| SubLab808 | `01-SubLab808-Cubase.wav` | `04-SubLab808-Cubase-after-reload-tail20s.wav` | First 16 seconds of both files |
| ReverseLab | `02-ReverseLab-Cubase.wav` | `05-ReverseLab-Cubase-after-reload.wav` | Entire files; equal frame counts required |

The optional `03-SubLab808-Cubase-tail2s.wav` adds two comparisons when the
corresponding files exist: the first 16 seconds against `01`, and the first
18 seconds against `04`. The `01`/`03` pair checks export consistency and does
not establish recall. The `03`/`04` pair can supply an available recall check,
but `--require-recall sublab808` still requires the canonical `01`/`04` pair.
The optional `03` file is never required. A failing optional comparison also
fails the overall result, even if the canonical pair passes.

Every comparison requires matching sample rates and channel counts, a nonempty
comparison interval, finite samples, and enough frames in both files to cover
the entire interval. ReverseLab compares all frames without truncating to a
shared prefix. SubLab808 permits different total lengths for the configured
tail exports, and separately checks that `04` is exactly 36 seconds, contains
preceding signal, and has only zero samples from seconds 30–36. Those tail
conditions remain mandatory whenever that file is present.

The default maximum absolute sample difference is **zero**: every decoded
sample in each comparison interval must match. Samples are neither aligned,
normalized, nor omitted around note onsets. The existing diagnostic for samples
outside the first 20 ms of note-on windows only localizes differences.

If a test protocol explicitly permits differences, set `--recall-tolerance`
to a finite, non-negative number in normalized linear sample units, where
`1.0` represents full scale. The measured maximum difference must be less than
or equal to this value. This option applies to **all** available same-host
comparisons, including export consistency. It cannot waive missing files,
incompatible formats/durations, nonfinite samples, signal checks, or tail
checks. Reports record the configured tolerance and actual difference;
`sample_exact` remains false when a nonzero difference passes within tolerance.
Choose a tolerance from the test protocol, not to conceal an unexplained failure.

Without `--require-recall`, standalone signal and tail analysis remains valid.
Missing pairs do not fail the run, but their recall status is `not_checked`,
with `passed: null`. Existing recognized pairs always affect the overall
result, including when no recall flag was given. Unrecognized filenames are
analyzed for signal quality only.

The JSON report contains the detailed measurements. Its CLI summary and Markdown
also expose the run identity, scope and outcome:

- `analysis_completed`: false when evidence is missing from an empty directory,
  cannot be read, or fails to decode. This is separate from the quality of audio
  that could be measured. A measurable waveform mismatch completes analysis
  but fails acceptance.
- `input_errors`: each read/decoding failure with its stage, absolute paths,
  error type and message. An unreadable comparison has `compatible: null`
  because compatibility could not be measured; it still fails the comparison.
- `signal_checks_passed`: all WAVs contain finite, non-silent audio without
  full-scale samples, using the existing signal criteria.
- `comparison_checks_passed`: all available comparisons passed; `null` when
  none were available. `same_host_comparisons` gives each pair's purpose,
  interval, tolerance, measured differences, pass/fail result, and failure reason.
- `recall_check`: aggregate `passed`, `failed`, or `not_checked` status, plus
  coverage and missing required files for **each plugin**. An aggregate pass
  only covers the listed comparisons, not a plugin marked `not_checked`.
- `passed`: analysis completed, signal and applicable tail checks passed, no
  available comparison failed, and no required recall evidence is missing or
  failing. A passing comparison alongside an unreadable extra WAV does not
  make the overall run pass.

This is scoped waveform evidence for named exports. Record the source build,
host, preset, export settings, and reload steps separately. The script cannot
prove from filenames that a project was actually reopened, certify every
preset, or replace DAW interaction and listening checks. Cross-host waveform
equality is not asserted.

Use the [DAW acceptance protocol](DAW_ACCEPTANCE.md) for the practical preset,
save/reload, MIDI, Freeze, and listening checks. It includes separate coverage
for each plugin and a record template; it is a procedure, not a completed test.

Run the dependency-free regression suite from this directory:

```sh
python3 -B -m unittest -v test_analyse_host_audio
```
