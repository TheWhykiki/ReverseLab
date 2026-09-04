# ReverseLab 1.1.0 (development)

ReverseLab is a tempo-synchronised stereo reverse effect for Cubase on macOS.

Copyright © 2026 Whykiki Audio. ReverseLab is free software licensed under the GNU Affero General Public License v3.0; see [LICENSE](LICENSE). JUCE remains available under its own dual-licensing terms.

## Download and install

The 1.1.0 source includes acceptance fixes that may not yet be in the latest downloadable release. In particular, earlier builds could sustain excessive feedback after input stopped when crossfade was enabled. Check the downloaded version; the earlier smoke-test results below do not certify this corrected source or every feedback setting.

Download the current `.pkg` from [GitHub Releases](https://github.com/TheWhykiki/ReverseLab/releases/latest), open it, and follow the installer. It installs the universal VST3 into `/Library/Audio/Plug-Ins/VST3`. Restart Cubase or trigger a plug-in rescan afterwards.

The VST3 inside the current release is ad-hoc signed, but the `.pkg` itself is unsigned and the release is not Apple-notarized. Gatekeeper will therefore block the installer on first launch. Open it once, then go to **System Settings → Privacy & Security** and choose **Open Anyway**. Alternatively unzip the raw VST3 from the release, copy it to `~/Library/Audio/Plug-Ins/VST3`, and remove its quarantine flag with `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3`. The release also includes SHA-256 checksums.

## Build

```sh
git clone --recurse-submodules https://github.com/TheWhykiki/ReverseLab.git
cd ReverseLab
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The project builds a universal `arm64`/`x86_64` VST3. Install it only after tests pass:

```sh
./scripts/install-local.sh Release
```

The local installer requires Python 3.9 or newer. It verifies the built bundle without re-signing or changing it, stages a fresh copy and verifies the installed bytes and signature. An existing installation is moved to a uniquely named backup; its exact path is printed on success. Failed installation attempts restore the previous bundle where possible and retain diagnostic staging/backup paths instead of deleting recovery data. It never merges new files into an old bundle. A pre-existing installer lock is reported for manual inspection, not removed automatically. Close plug-in hosts before replacing a loaded bundle.

`./scripts/package-release.sh Release` takes a source snapshot (including initialized JUCE and local source edits), builds it from scratch, runs every CTest suite, and loads both the extracted ZIP and installer payload as real VST3s. It does not trust an existing same-version build. A complete candidate is published atomically in a new `dist/ReverseLab-VERSION-COMMIT-SOURCEHASH-BINARYHASH/` directory; existing candidates are never overwritten. The directory includes source/release manifests, test reports and SHA-256 checksums. See [the release pipeline contract](docs/RELEASE_PIPELINE.md).

For a Developer-ID-signed and notarized package, set `REVERSELAB_APPLICATION_IDENTITY`, `REVERSELAB_INSTALLER_IDENTITY`, and `REVERSELAB_NOTARY_PROFILE` to the corresponding signing identities and `notarytool` keychain profile before running it. Without these identities the candidate is ad-hoc signed, its installer is unsigned, and no notarization is claimed.

The currently verified local installation uses the user-level VST3 directory, which Cubase scans without administrator privileges:

`~/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3`

## Controls

- Tempo-synchronised or free-time stereo reverse segments
- Linked or independent left/right segment lengths
- Reverse speed, crossfade, equal-power dry/wet, and output gain
- Freeze, retrigger, stable feedback, high/low-pass filtering
- Stereo offset and deterministic randomisation
- 64 factory programs with a searchable user preset library and full Cubase automation/state restoration

Changing segment length changes the reported plug-in latency. Cubase compensates other tracks, while ReverseLab delays its dry path internally to maintain alignment.
ReverseLab keeps at most 16 seconds of history; `2 Bars` is therefore capped only below 30 BPM in 4/4 (or equivalent very long meters).

Feedback uses a separate convex interpolation/crossfade path so its gain stays below unity; the audible cubic/equal-power path remains available. This prevents self-sustaining gain caused by feeding equal-power crossfades back repeatedly, but is not an output loudness limiter.

Once Freeze has captured its initial window, changing length or tempo does not resume recording. Unfreeze explicitly resumes capture. The host-reported tail conservatively includes reverse read history, speed, feedback and output alignment. Extreme 16-second/95%-feedback settings can report about 97 minutes; use an explicit export range for intentionally held Freeze textures or when a shorter musical tail is desired.

## Continuous integration

Every main-branch push and pull request builds and strictly verifies the universal VST3 and its ZIP roundtrip on macOS, runs DSP/processor, parameter-text, preset and validator tests, and exercises the Makefiles path under ASan/UBSan. The extracted VST3 is instantiated and rendered with samplewise finite checks and parameter/audio state recall. Linux tests run under Xvfb for the real preset UI. See `.github/workflows/ci.yml`.

The real VST3 host test requires canonical setter readbacks, a fresh instance whose defaults differ from the saved fixture, and non-silent effect output on both channels. Negative controls reject ignored parameter writes, passthrough, gain/delayed dry audio and dead channels. The separate state/audio roundtrip still compares all 19 parameters and every sample without fitting or alignment.

Preset tests include 39 editor-lifecycle/host-callback cases plus six macOS-only native Import/Export panel cases. The former include two real Save-As-then-Load flows: normal success must load the requested next preset, while an intervening restore must keep its newer state. Set `WHYKIKI_PRESET_TEST_NATIVE_ONLY=1` on `ReverseLabPresetTests` for the focused native suite; it requires an active desktop and rejects a non-native fallback. The test-only Cocoa bridge observes its own process, verifies panel/delegate/modal teardown, unchanged fixture files/state and a usable reopened editor. Real Cubase/REAPER window behavior remains a separate acceptance gate.

`WHYKIKI_PRESET_TEST_SAVE_RESTORE_ONLY=1` runs dirty-state guards and 14 deterministic persistence/control groups without opening an editor: real file-commit interleavings, same-ID/ABA restores, coherent capture, host-notification restores and bounded contention before writing. `WHYKIKI_PRESET_TEST_DIRTY_ONLY=1` runs just the dirty-state guards. Do not combine either focused mode with another. Their scheduling hooks exist only in the preset-test target, never in the VST3.

`ReverseLabListenerLock` runs the genuine held-JUCE-listener-lock regression separately with a 20-second CTest timeout; the normal DSP binary also includes it. DSP, parameter-text and bundle tests have 300-second limits and the full preset test 600 seconds. A timeout fails the test.

## Control-thread contract

Program application and state restoration commit all parameter values, program identity, user-preset metadata and restored dimensions under a short control gate. The gate calls no JUCE parameter listeners, host callbacks or editor methods. State capture returns a complete committed state; even the first parameter callback observes the complete NEW state. A nested or independent state restore commits before returning, without waiting for another thread's notification dispatcher.

Notifications run separately and never write an old captured value back to a parameter. If a newer state supersedes a notification, the dispatcher replays current values. Each drain sends at most 64 notifications; outstanding work resumes on later timer ticks instead of being discarded. A tick which also applies a pending program can run two drains. Actual ranged parameters are authoritative for DSP, persistence and dirty status; APVTS raw/UI caches may lag until their notifications complete.

Program requests made off the message thread retain their deferred, coalesced next-service-tick behavior. Reentrant program requests also retain next-tick behavior, while redundant same-program notification echoes are ignored. `processBlock` makes one sequence-validated read of the committed parameter packet and never takes the control gate or waits for publication. This transaction guarantee does not make separate host-automation writes into a single atomic operation.

The additional overlapping JUCE-control-thread tests do not change VST3's UI-thread state-access contract and do not replace actual Cubase/REAPER acceptance. Arbitrary cross-thread waits inside external JUCE listeners can still create their own lock cycles.

## Earlier release validation (1.0.4)

- JUCE 8.0.15, VST3 SDK 3.8
- Universal `arm64` and `x86_64`
- Minimum deployment target macOS 11.0 in both binary slices
- Embedded VST3 ad-hoc signed; public `.pkg` unsigned and not yet Apple-notarized
- Cubase 15 `vstscanner` exit code 0
- REAPER 7.79 native-arm64 host test: VST3 instantiated, 26 parameters exposed, project state saved, 4 s offline render completed at 44.1 kHz/24-bit stereo with no clipped samples
- Automated DSP and processor tests pass at 44.1/48/88.2/96/192 kHz, variable block sizes, continuous free timing, latency-aligned bypass, validated state/editor restoration, coherent cross-thread editor sizing, precise long-running fractional read positions, host-visible program changes, resource release/re-prepare, independent stereo scope output, reset invalidation, 0.25×/1×/4× speed, feedback stability, Freeze/Unfreeze recovery and fresh-state pre-roll, click-free filter bypass automation, hostile transport metadata, superseded latency requests, and deterministic randomisation


## Preset library (1.1.0)

64 factory presets with product-specific categories, search and favourites. The original 6 host programs
keep their names, indices and settings. Open the preset name to browse; use **Save** / **Save As** for your own
sounds. **More** provides rename, delete, import and export. `*` marks unsaved changes.

User presets are stored separately for ReverseLab, in the JUCE user application data directory under
`Whykiki Audio/ReverseLab/Presets`, using `.reverselabpreset` files. Factory sounds remain protected.
The DAW project also stores current unsaved edits and the user preset name, independently of the library files.

See [the full preset catalogue](Presets/CATALOG.md) and [the implementation and acceptance plan](PRESET_PLAN.md).
