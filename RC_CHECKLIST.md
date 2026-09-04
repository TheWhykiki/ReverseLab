# ReverseLab acceptance and release gates

## Current candidate requirements

This checklist is a procedure, not a blanket PASS for the current checkout.
Every new candidate must identify its source commit, dirty/source manifest,
VST3 binary SHA-256, host versions and test reports. A historical DAW result
does not certify a later binary, even when the displayed version is unchanged.

- All registered CTest suites pass on the exact source snapshot, including
  factory recipes, preset persistence/UI, audio-report and installer tests.
- The actual ZIP and PKG payloads pass architecture, minimum macOS, signature
  and VST3 host state/audio tests. Use the reports produced by
  [`scripts/package-release.sh`](scripts/package-release.sh), as described in
  [the release pipeline contract](docs/RELEASE_PIPELINE.md).
- CI for the final PR head passes; record sanitizer coverage separately from
  ordinary builds. Disabled LeakSanitizer is not a leak-free claim.
- Cubase and REAPER load the exact candidate bytes. Record project reload,
  waveform comparison, parameter recall and native dialog/editor interaction
  separately, using [the DAW acceptance protocol](scripts/acceptance/DAW_ACCEPTANCE.md).
- Listener/editor teardown and Import/Export cancellation leave no stale
  dialogs, unintended file changes or blocked replacement editors.
- Signal-analysis PASS is not subjective listening, a foreign-Mac installation
  test or Developer-ID notarization. These have their own release decisions.

The processor exposes 19 audio parameters. Host-generated controls can make
the total number shown by a DAW larger; record both counts without treating
them as the same parameter layout.

## Historical RC1 evidence (not current-candidate approval)

The following records belong to earlier 1.0.x/RC1 builds. They are retained
for context, not carried forward as proof for the 1.1.0 preset/lifecycle work.

- Universal Release binary contains native `arm64` and `x86_64` slices.
- Both slices declare macOS 11.0 as the minimum deployment target.
- Installed user-level VST3 passes strict ad-hoc signature verification.
- Automated DSP and processor integration suite passes.
- Unlinked L/R regression passes with identical input and strongly different left/right segment lengths.
- Cubase 15 `vstscanner` returns success and reports `ReverseLab 1.0.4`, VST3 SDK 3.8, category `Fx|Delay`.
- REAPER 7.79 native-arm64 instantiated the tested build and reported 26 host-exposed controls.
- REAPER saves the plug-in state in a project and completes a 4.000-second, 44.1 kHz, stereo 24-bit offline render.
- `afclip` reports no clipped samples in the RC1 host render.
- REAPER stress test passes with 32 parallel instances and automated parameter variation; all 32 states are present in the saved project and the render contains no clipped samples.
- Link L/R now has an unambiguous editor state: Right Size is disabled and labelled `LINKED` while linked, then becomes independently editable when unlinked.

## Independent listening and performance checks

- Listen to transient, vocal, drum, and sustained-source material in a real Cubase project.
- Exercise automation for Mix, Speed, Feedback, Freeze, Retrigger, and both timing modes.
- Test tempo ramps, cycle jumps, time-signature changes, and Cubase offline export.
- Run a real-time 32-instance CPU measurement at 48 kHz / 256 samples; the 32-instance offline-render stress test is already green.
- Record issues and unresolved checks before promoting the current candidate.
