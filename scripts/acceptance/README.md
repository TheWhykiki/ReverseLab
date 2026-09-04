# Host audio acceptance reports

Run `analyse_host_audio.py` against a directory of WAV exports. It writes
`audio-analysis.json` and `audio-analysis.md`, prints a JSON summary, and exits
with `0` when the evaluated checks pass or `1` when they fail. Invalid CLI
arguments or a directory without WAV evidence produce a usage error (exit `2`).
Malformed or unsupported WAV files raise an error and cannot produce a new
passing report; consumers must check the process exit code, not an older report.

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

JSON, the CLI summary, and Markdown expose the scope and outcome:

- `signal_checks_passed`: all WAVs contain finite, non-silent audio without
  full-scale samples, using the existing signal criteria.
- `comparison_checks_passed`: all available comparisons passed; `null` when
  none were available. `same_host_comparisons` gives each pair's purpose,
  interval, tolerance, measured differences, pass/fail result, and failure reason.
- `recall_check`: aggregate `passed`, `failed`, or `not_checked` status, plus
  coverage and missing required files for **each plugin**. An aggregate pass
  only covers the listed comparisons, not a plugin marked `not_checked`.
- `passed`: signal and applicable tail checks passed, no available comparison
  failed, and no required recall evidence is missing or failing.

This is scoped waveform evidence for named exports. Record the source build,
host, preset, export settings, and reload steps separately. The script cannot
prove from filenames that a project was actually reopened, certify every
preset, or replace DAW interaction and listening checks. Cross-host waveform
equality is not asserted.

Run the dependency-free regression suite from this directory:

```sh
python3 -B -m unittest -v test_analyse_host_audio
```
