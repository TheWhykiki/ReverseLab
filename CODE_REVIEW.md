# ReverseLab Critical Code Review

## Resolved findings

1. **Real-time host notification** — latency changes previously called `setLatencySamples()` directly from `processBlock()`. The audio thread now writes only atomic pending state; a 30 Hz message-thread timer applies the host notification.
2. **Bypass timing** — internal bypass previously crossfaded to the undelayed input while the plug-in continued reporting latency. It now crossfades to the latency-aligned dry path, avoiding track-timing jumps.
3. **Mono contamination** — the hidden right engine channel previously received the already processed mono output. It now receives the original mono input.
4. **Filter endpoints** — nominal off positions still applied 20 Hz high-pass and 20 kHz low-pass filters. DSP and UI now bypass those stages and display `Off`.
5. **Transport discontinuities** — playback start and timeline jumps now reset the reverse heads and restore the deterministic seed.
6. **Waveform playheads** — UI position was incorrectly normalized against a hard-coded 48,000 samples. The engine now exposes normalized per-segment phase.
7. **Speed quality** — linear interpolation was replaced with four-point cubic interpolation. Playback above 1× also applies a speed-dependent anti-alias low-pass stage.
8. **Retrigger behavior** — the automation parameter still remains host-visible, while the editor now returns it to off after a momentary trigger pulse.
9. **Missing public control** — Random Seed is now exposed in the editor and remains restorable/automatable.
10. **Minimum-size layout** — Freeze and Retrigger were clipped in the old 720 px layout. The new responsive grouping keeps every persistent control visible at 720×460.
11. **Deployment target drift** — the Xcode generator had retained the SDK default (macOS 14.5) even though the project contract requires macOS 11. The cache value is now forced during configuration; both universal slices report `minos 11.0`.
12. **Real-time reset cost** — transport resets previously cleared the entire multi-second ring buffer in `processBlock()`. Generation tags now invalidate old samples in constant time without allocation, locks, or large buffer clears.
13. **Stereo latency alignment** — independent L/R segment lengths previously left the shorter wet channel earlier than the longer channel. A preallocated wet alignment delay now aligns both wet channels and the dry path to the active maximum latency.
14. **Latency/segment mismatch** — the host-visible latency could change before the reverse engine accepted a new length. Latency now follows the engine's active segment lengths after a safe segment-boundary transition.
15. **Scope data race** — editor phase reads now use atomically published normalized phase values instead of reading mutable DSP head state.
16. **Continuous free timing** — free mode now exposes independent continuous 20–4000 ms L/R parameters instead of reusing the finite sync subdivision list.
17. **Musical retrigger** — retrigger is edge-triggered in the processor, scheduled to the next 1/32-note grid when host PPQ is available, and reset from the message thread without depending on an open editor.
18. **Automation smoothing** — speed, crossfade, feedback, filter cutoffs, stereo offset, and random amount now use sample-level smoothing in addition to mix, output, and bypass.
19. **Host time-signature handling** — `1 Bar` and `2 Bars` previously assumed 4/4. They now derive their quarter-note length from the host numerator and denominator, with regression coverage for 3/4 and 5/4 transitions.
20. **Linked-channel affordance** — with Link L/R enabled, the DSP correctly used the left timing value for both channels, but the right timing control still looked editable. The right sync/free control is now disabled and dimmed, and its caption explicitly reads `RIGHT SIZE · LINKED`; unlinking restores independent operation.
21. **Stable random latency** — Random now varies capture position without changing segment duration, so host latency no longer oscillates at every randomized segment boundary.
22. **Host/DSP latency handshake** — the message thread applies and acknowledges host latency before the audio thread switches its internal taps; old and new taps use a 10 ms equal-power transition.
23. **State-restore race** — state loading now requests an atomic DSP reset that is consumed inside `processBlock()` instead of mutating engine and delay state from the caller thread.
24. **Tail reporting** — tail duration now follows segment time and feedback decay, is bounded for Freeze, and remains valid when queried before `prepareToPlay()`.
25. **VST3 host bypass** — `getBypassParameter()` now returns ReverseLab's saved bypass parameter. This keeps VST3 hosts in `processBlock()` and uses the latency-aligned dry path instead of JUCE's default zero-latency `processBlockBypassed()` fallback.
26. **Real-time program changes** — `setCurrentProgram()` now publishes only an atomic request when called outside the message thread. The processor timer applies the parameter batch and host notifications safely, and the editor follows host-originated preset changes without feedback.
27. **Meter-aware tail reporting** — the published tail now uses the latest host time signature instead of assuming 4/4, matching the DSP's Bar/2-Bar segment length in asymmetric meters.

28. **Speed automation scrubbed the read head** — the read position was derived as `phase * speed`, so a smoothed speed change late in a segment moved the read position by `phase * Δspeed` per sample (a 1×→2× ramp 20 000 samples into a segment jumped ~17 samples per sample). The engine now integrates the read offset per sample (`readOffset += speed`); speed automation changes read velocity only. A click-detector regression on engine and processor level covers this.
29. **Read head could wrap into the write head** — at speeds above 1× a segment reads `speed × length` of history, which exceeded the ring at long segments (2 Bars at slow tempos ×4). The read offset is now clamped to the ring capacity and holds at the oldest valid sample instead of wrapping around into freshly written material.
30. **Hard wet-alignment tap switch at segment boundaries** — the wet alignment offset depends on the engine's active segment length, which changes at a segment boundary before the host has acknowledged the new latency; shortening a segment switched the tap by `old − new` samples without a crossfade. Every wet tap offset change is now crossfaded per channel over the same 10 ms equal-power transition used for the dry path. A multi-tone regression (440/631/977 Hz, 300 ms → 190 ms) covers it.
31. **Block-size-dependent tempo smoothing** — `smoothedBpm` used a fixed per-block coefficient, so tempo tracking was 32× faster at 2048-sample blocks than at 64. The smoothing is now expressed in time (~65 ms).
32. **Startup latency assumed 120 BPM 4/4** — `prepareToPlay()` now reads the host tempo and meter from the play head when available, so the latency reported at preparation matches the first processed segment.
33. **`processBlock()` before `prepareToPlay()`** — would have divided by a zero delay capacity. Audio now passes through unchanged until the processor is prepared.
34. **Stringly-typed editor styling** — the LookAndFeel chose ring width and accent colour by matching control names and button captions. Styling now keys off component properties (`rl.sizeControl`, `rl.violet`); Size and Free-Time controls also gained the double-click-to-default behaviour the other knobs already had.
35. **Parameter identifiers without version hints** — all parameters now use `juce::ParameterID { id, 1 }`. VST3 identifiers are derived from the string alone and are unchanged; an AU build would now get stable identifiers too.
36. **Build portability and CI** — the universal/deployment-target cache overrides are Apple-only, so the DSP/processor suite configures and runs on Linux; `.github/workflows/ci.yml` builds the universal VST3 and runs the tests on macOS and additionally runs the suite on Linux. `package-release.sh` reads the version from `CMakeLists.txt`.

## Verification

- Release build succeeds for `arm64` and `x86_64`.
- DSP suite covers finite maximum feedback, reverse ordering, freeze, deterministic randomisation, normalized phase, reset invalidation, all target sample rates, and 0.25×/1×/4× speed extremes.
- Processor integration tests cover continuous free timing, reported latency, latency-aligned bypass, state/UI-size restoration, mono processing, and variable block sizes.
- A dedicated stereo regression feeds identical material to both inputs and verifies that unlinked 50 ms/180 ms segment times produce non-identical, energetic L/R output rather than collapsing to mono.
- Visual and accessibility inspection passed at 720×460 and 900×610.
- Link, Freeze, and momentary Retrigger were exercised in the native preview editor.
- REAPER reloads the freshly installed VST3 and identifies it as `ReverseLab (Whykiki Audio)`; the dedicated unlinked-stereo regression remains green after the linked-control UI fix.
- The installed user-level VST3 passes strict code-signature verification and both binary slices declare macOS 11.0 as their minimum OS.
- Cubase 15 `vstscanner` exits with code 0 and identifies ReverseLab 1.0.0 as a native VST3 `Fx|Delay`.
- REAPER 7.79 (native arm64) discovers and instantiates ReverseLab as VST3, exposes 26 parameters in the current build, saves/restores the plug-in in an `.rpp`, and completes a four-second offline render at 44.1 kHz/24-bit stereo without clipped samples.
- A separate REAPER stress project creates 32 parallel ReverseLab instances, varies timing, speed, crossfade, feedback, randomisation, and stereo offset, saves all 32 VST3 states, and completes a two-second offline render without clipped samples.

- The full suite (22 test groups) passes on a Linux x86_64 build of the shared code; the two click-detector regressions and the wet-tap regression were confirmed to fail against the previous engine/processor before the fixes were applied.

## Remaining limits

- The anti-alias stage is a pragmatic real-time one-pole reconstruction filter, not a long-window polyphase resampler. It is appropriate for version 1 but extreme 4× material can still contain more aliasing than an offline-quality resampler.
- Latency notification is asynchronous by design. The DSP keeps using the acknowledged latency until the message thread has updated the host, then crossfades its internal delay taps.
- Freeze audio is intentionally not serialized, matching the product contract.
- A silent preview cannot validate the visual density of real program material; this is covered only when audio flows in Cubase.
- Cubase still needs a short listening/automation/offline-render acceptance pass inside a real user project; its scanner and REAPER's independent native instantiation/offline-render path are green.
- The 32 s ring plus dry and wet alignment delays cost roughly 36 MB per instance at 48 kHz (three stereo float buffers); 32 instances exceed 1 GB. Reducing the maximum segment to 16 s would halve this without affecting any tempo-sync value at 40 BPM or above.
- Feedback is taken from the engine output before the high-/low-pass stage, so the filters shape only the output and not the repeats. "Frozen Texture" combines Freeze with a feedback amount that has no effect while frozen.
- `ReverseLabTests` links both the shared-code target and the JUCE modules directly, which compiles every JUCE module twice and produces a `JUCE_STANDALONE_APPLICATION` redefinition warning; linking the tests against `ReverseLab` alone would halve build time.
- The repository has no LICENSE file; JUCE 8 usage terms (AGPLv3 or a JUCE plan) and the terms for ReverseLab's own code should be stated explicitly.
