# ReverseLab acceptance

## Passed

- Universal Release binary contains native `arm64` and `x86_64` slices.
- Both slices declare macOS 11.0 as the minimum deployment target.
- Installed user-level VST3 passes strict ad-hoc signature verification.
- Automated DSP and processor integration suite passes.
- Unlinked L/R regression passes with identical input and strongly different left/right segment lengths.
- Cubase 15 `vstscanner` returns success and reports `ReverseLab 1.0.4`, VST3 SDK 3.8, category `Fx|Delay`.
- REAPER 7.79 native-arm64 instantiates the effect; the current build exposes 26 parameters.
- REAPER saves the plug-in state in a project and completes a 4.000-second, 44.1 kHz, stereo 24-bit offline render.
- `afclip` reports no clipped samples in the RC1 host render.
- REAPER stress test passes with 32 parallel instances and automated parameter variation; all 32 states are present in the saved project and the render contains no clipped samples.
- Link L/R now has an unambiguous editor state: Right Size is disabled and labelled `LINKED` while linked, then becomes independently editable when unlinked.

## Before final 1.0

- Listen to transient, vocal, drum, and sustained-source material in a real Cubase project.
- Exercise automation for Mix, Speed, Feedback, Freeze, Retrigger, and both timing modes.
- Test tempo ramps, cycle jumps, time-signature changes, and Cubase offline export.
- Run a real-time 32-instance CPU measurement at 48 kHz / 256 samples; the 32-instance offline-render stress test is already green.
- Record any acceptance issue before promoting RC1 to final 1.0.
