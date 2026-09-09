# Preset QA – ReverseLab 1.1.0

Local Release verification on 2026-09-04:

- ReverseLabDSP and ReverseLabPresets: all passed.
- 64 complete factory recipes; unique names and recipes even when output and seed are excluded.
- All 6 legacy names, indices and values preserved.
- 192 rendered scenarios (64 presets × 3 input/tempo combinations), 48 kHz, 18 seconds each; all finite, audible, peak ≤ 1.001.
- Observed peak range: 0.064735–0.577926. 64 different combined audio fingerprints.
- User save/load, overwrite, rename, delete, import/export and favourites passed.
- Invalid/foreign/future-version files, duplicate names including umlauts, missing values, invalid numbers, write failures and stale-instance saves rejected without changing the sound/library.
- A one-step random-seed change marks the preset dirty.
- A stale Rename dialog cannot rename a different preset after a host-state change.
- DAW recall restores all parameter values, user name and unsaved edits even without the external preset file.
- A real desktop test window exercises Save As, Save, dirty-navigation Cancel/Discard, browser and search. Queue barriers wait for asynchronous callbacks without fixed-delay assumptions.
- Minimum/default/maximum editor sizes and browser/save-dialog snapshots visually inspected.
- Universal ARM64/x86_64 VST3, embedded version 1.1.0, strict ad-hoc signature verification passed.
- CI configurations updated, including Xvfb for the Linux UI tests; remote CI has not been run for this local change.

Audio checks establish technical behaviour under the defined fixtures; they do not replace musical listening in an arrangement or the broader DSP acceptance review.
The separate acceptance-review task owns integration of its DSP fixes and final release. This preset change does not publish or replace the installed plugin.
ReverseLab presets store controls, including Seed/Freeze, rather than captured audio buffers; frozen material is captured again from incoming audio.

Run `ReverseLabPresetTests <absolute-output-directory>` to retain the audio CSV and PNG previews, or set `WHYKIKI_PRESET_TEST_OUTPUT_DIR` for CTest.
The interactive part requires a desktop display. Tests use a temporary library and do not change real user presets.

## Follow-up: preset file and dialog safety

- Managed exports (including directory symlinks) are rejected without changing stored bytes. Ordinary exports, including UTF-8 file paths, remain supported.
- Load, Save and Rename reject mismatched file identities; Import can recover the file under a fresh identity.
- Pending Discard and Save As actions preserve a sound changed by the DAW while the dialog was open. Export uses the same sound guard.
- Error alerts use an explicit asynchronous callback, avoiding nested modal loops when JUCE permits modal dispatch.
- Regression UI checks wait for the requested alert across queue turns.
- The complete local preset suites pass for both products; DSP and host checks remain green. No DSP, parameter or build configuration changed in this follow-up.
