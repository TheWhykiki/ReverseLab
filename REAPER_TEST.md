# ReverseLab REAPER Host Test

## Environment

- Host: REAPER 7.79, native arm64 process
- Plug-in: ReverseLab 1.0.0 VST3
- Test chain: REAPER JS Tone Generator -> ReverseLab -> master output
- Render: four seconds, 44.1 kHz, stereo, signed 24-bit PCM

## Results

- REAPER plug-in cache identified `ReverseLab (Whykiki Audio)` with the expected VST3 class ID.
- `TrackFX_AddByName` instantiated the plug-in successfully at FX index 1.
- REAPER exposed 24 plug-in parameters, including independent continuous left/right free-time controls.
- A separate audible speech demo uses 100% wet processing with Link L/R off and strongly different left/right segment lengths. The automated stereo regression verifies that this configuration produces energetic, non-identical channel output rather than mono collapse.
- The plug-in state was serialized into `ReverseLab-Reaper-Test.rpp`.
- REAPER completed its offline render action successfully.
- The output is a valid 4.000-second WAVE file containing 176,400 stereo frames.
- `afclip` reported no clipped samples.
- Latest RC1 retest render SHA-256: `a953da80e074953b2e54b8baf356ffde5f9eb0b3195a9d085fc8bfe6d4092bbc`.

## Stress test

- 32 parallel tracks, each containing a tone generator and one ReverseLab VST3 instance.
- All 32 instances were created and exposed 24 parameters each.
- Timing mode, free time, speed, crossfade, feedback, random amount, and stereo offset were varied across instances.
- The saved project contains 32 ReverseLab VST3 state blocks.
- The two-second, 44.1 kHz, stereo 24-bit offline render completed with no clipped samples.
- Stress render SHA-256: `5839704fee74f0070e5e5b3933b99e7ce47a5df5885431423d3e7f2ece3c789b`.

## External interference observed

The machine contains several unrelated commercial VST3 plug-ins whose activation scanners do not respond during REAPER's initial global scan. Those plug-ins were skipped/cancelled. ReverseLab itself scanned without an activation window, timeout, or crash.

REAPER also prints a warning that the installed VLC libraries are Intel-only while REAPER is running natively on Apple Silicon. This is unrelated to ReverseLab and did not affect VST3 instantiation, state saving, or the offline render.

## Artifacts

- `TestArtifacts/REAPER/ReverseLab-Reaper-Test.rpp`
- `TestArtifacts/REAPER/reverselab-reaper-render.wav`
- `TestArtifacts/REAPER/test-result.txt`
- `TestArtifacts/REAPER/render-complete.txt`
- `TestArtifacts/REAPER/ReverseLab-Stress-Test.rpp`
- `TestArtifacts/REAPER/reverselab-stress-render.wav`
- `TestArtifacts/REAPER/stress-result.txt`
- `TestArtifacts/REAPER/stress-complete.txt`
- `TestArtifacts/REAPER/ReverseLab-Audible-Demo.rpp`
- `TestArtifacts/REAPER/reverselab-demo-source.aiff`

final result: passed
