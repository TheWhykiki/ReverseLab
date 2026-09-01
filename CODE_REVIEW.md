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
- REAPER 7.79 (native arm64) discovers and instantiates ReverseLab as VST3, exposes all 24 parameters, saves/restores the plug-in in an `.rpp`, and completes a four-second offline render at 44.1 kHz/24-bit stereo without clipped samples.
- A separate REAPER stress project creates 32 parallel ReverseLab instances, varies timing, speed, crossfade, feedback, randomisation, and stereo offset, saves all 32 VST3 states, and completes a two-second offline render without clipped samples.

## Remaining limits

- The anti-alias stage is a pragmatic real-time one-pole reconstruction filter, not a long-window polyphase resampler. It is appropriate for version 1 but extreme 4× material can still contain more aliasing than an offline-quality resampler.
- Latency notification is asynchronous by design; a host may observe it one message-loop tick after the DSP length request.
- Freeze audio is intentionally not serialized, matching the product contract.
- A silent preview cannot validate the visual density of real program material; this is covered only when audio flows in Cubase.
- Cubase still needs a short listening/automation/offline-render acceptance pass inside a real user project; its scanner and REAPER's independent native instantiation/offline-render path are green.
