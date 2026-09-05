# ReverseLab 1.1.0 (development)

ReverseLab is a tempo-synchronised stereo reverse VST3 effect for macOS and Windows.

Copyright © 2026 Whykiki Audio. ReverseLab is free software licensed under the GNU Affero General Public License v3.0; see [LICENSE](LICENSE). JUCE remains available under its own dual-licensing terms.

Any distributed binary must remain traceable to its exact corresponding source.
Tag the release commit, keep the source tag publicly available, and record that tag
and commit alongside the binary checksums. A local candidate directory or an
untagged branch head is not, by itself, the corresponding-source record for a
public release.

## Download and install (current macOS release)

The 1.1.0 source includes acceptance fixes that may not yet be in the latest downloadable release. In particular, earlier builds could sustain excessive feedback after input stopped when crossfade was enabled. Check the downloaded version; the earlier smoke-test results below do not certify this corrected source or every feedback setting.

Download the current `.pkg` from [GitHub Releases](https://github.com/TheWhykiki/ReverseLab/releases/latest), open it, and follow the installer. It installs the universal VST3 into `/Library/Audio/Plug-Ins/VST3`. Restart Cubase or trigger a plug-in rescan afterwards.

The currently published installer described here is macOS-only. The source now
contains separate Windows x64 and ARM64EC MSI/updater paths; CI builds and fully
extracts both as explicitly unsigned test candidates. A signed public Windows
installer and documented Windows DAW acceptance still require the real
distribution certificate and physical x64/Arm systems. The gated publication
contract is documented in [WINDOWS_RELEASE.md](WINDOWS_RELEASE.md).

The VST3 inside the current release is ad-hoc signed, but the `.pkg` itself is unsigned and the release is not Apple-notarized. Gatekeeper will therefore block the installer on first launch. Open it once, then go to **System Settings → Privacy & Security** and choose **Open Anyway**. Alternatively unzip the raw VST3 from the release, copy it to `~/Library/Audio/Plug-Ins/VST3`, and remove its quarantine flag with `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3`. The release also includes SHA-256 checksums.

## Build

```sh
git clone --recurse-submodules https://github.com/TheWhykiki/ReverseLab.git
cd ReverseLab
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On macOS the project builds one universal `arm64`/`x86_64` VST3 with a macOS
11.0 deployment target in both slices. Install it only after tests pass:

```sh
./scripts/install-local.sh Release
```

The local installer requires Python 3.9 or newer. It verifies the built bundle without re-signing or changing it, stages a fresh copy and verifies the installed bytes and signature. An existing installation is moved to a uniquely named backup; its exact path is printed on success. Failed installation attempts restore the previous bundle where possible and retain diagnostic staging/backup paths instead of deleting recovery data. It never merges new files into an old bundle. A pre-existing installer lock is reported for manual inspection, not removed automatically. Close plug-in hosts before replacing a loaded bundle.

Windows uses separate PE binaries rather than one universal binary. Configure
and test each architecture in its own build directory:

```powershell
# Windows x86_64 (Visual Studio 2022)
cmake -S . -B build-windows-x86_64 -G "Visual Studio 17 2022" -A x64
cmake --build build-windows-x86_64 --config Release --parallel 2
ctest --test-dir build-windows-x86_64 -C Release --output-on-failure

# Windows on Arm (Visual Studio 2026 with ARM64EC tools)
cmake -S . -B build-windows-arm64ec -G "Visual Studio 18 2026" -A ARM64EC
cmake --build build-windows-arm64ec --config Release --parallel 2
ctest --test-dir build-windows-arm64ec -C Release --output-on-failure
```

The default build compiles the complete Windows updater in a side-effect-free
self-test mode. A distributable build additionally requires
`-DREVERSELAB_WINDOWS_UPDATER_SIGNER_SHA256=<64-hex-certificate-fingerprint>`;
only then is `ReverseLabUpdater.exe` embedded and the **Updates...** button
enabled. Build the reviewed MSI with `scripts/build-windows-installer.ps1` as
documented in [WINDOWS_INSTALLER.md](WINDOWS_INSTALLER.md). Updater verification,
release naming and recovery behavior are specified in
[WINDOWS_UPDATER.md](WINDOWS_UPDATER.md).

Those unsplit `ctest` commands include the interactive dialog-lifecycle suite
and therefore assume a usable desktop session. Hosted Windows CI uses the
non-dialog plus `WHYKIKI_PRESET_TEST_SAVE_RESTORE_ONLY=1` split described below.

The supported Windows-on-Arm format is ARM64EC. JUCE 8.0.15 places these
binaries in `Contents/arm64ec-win`; x86_64 uses `Contents/x86_64-win`. Do not
substitute `-A ARM64`: that produces the distinct `arm64-win` VST3 layout and
does not satisfy ReverseLab's Cubase/Windows-on-Arm compatibility target.
[Microsoft documents the ARM64EC toolchain and ABI](https://learn.microsoft.com/en-us/windows/arm/arm64ec-build).
The [Visual Studio 2026 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)
requires CMake 4.2 or newer; the native CI runner supplies a current CMake
release and the ARM64EC C++ components.

ARM64EC builds are accepted only on native Windows on Arm, where the generated
`moduleinfo.json` helper and VST3 host test can both execute; an x64-to-ARM64EC
cross-build is rejected at configure time. The CI job uses GitHub's native
Windows Arm64 hosted runner. A repo-local, JUCE-8.0.15-pinned CMake override
forwards the outer `-A x64` or `-A ARM64EC` setting to JUCE's nested manifest
helper build, keeping the helper ABI identical to the plug-in ABI.

`./scripts/package-release.sh Release` takes a source snapshot (including initialized JUCE and local source edits), builds it from scratch, runs every CTest suite, and loads both the extracted ZIP and installer payload as real VST3s. It does not trust an existing same-version build. A complete candidate is published atomically in a new `dist/ReverseLab-VERSION-COMMIT-SOURCEHASH-BINARYHASH/` directory; existing candidates are never overwritten. The directory includes source/release manifests, test reports and SHA-256 checksums. See [the release pipeline contract](docs/RELEASE_PIPELINE.md).

For a Developer-ID-signed and notarized package, set `REVERSELAB_APPLICATION_IDENTITY`, `REVERSELAB_INSTALLER_IDENTITY`, and `REVERSELAB_NOTARY_PROFILE` to the corresponding signing identities and `notarytool` keychain profile before running it. Without these identities the candidate is ad-hoc signed, its installer is unsigned, and no notarization is claimed.

Direct macOS CMake builds likewise default to inside-out ad-hoc signing. Set
`-DREVERSELAB_CODESIGN_IDENTITY="Developer ID Application: …"` only when that
identity is actually available; the embedded updater is signed before the VST3
root and `--deep` is used only for recursive strict verification.

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

Every main-branch push and pull request builds and strictly verifies the
universal VST3 and its ZIP roundtrip in isolated jobs on both a native
`macos-15` Arm64 runner and a native `macos-15-intel` x86_64 runner. Each job
verifies both slices and the macOS 11.0 deployment target, then runs the real
host/state/audio render on its native slice. The workflow also runs
DSP/processor, parameter-text, preset and validator tests, and exercises the
Makefiles path under ASan/UBSan. Separate Windows jobs build x86_64 and ARM64EC
bundles in isolated directories, verify their VST3 layout, generated manifest
and final PE architecture, and run the real VST3 host/state/audio test both
before and after a ZIP roundtrip. They also validate and administratively extract
architecture-specific WiX MSI candidates, then load the extracted payload. CI
artifacts are visibly marked unsigned. The ARM64EC job runs on a native
`windows-11-vs2026-arm` host; it is not an x64 cross-build. Linux tests run
under Xvfb for the real preset UI. See `.github/workflows/ci.yml` and
[GitHub's hosted-runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

Windows Hosted Actions do not provide a trustworthy interactive DAW desktop
acceptance environment. CI therefore excludes the full dialog-opening preset
suite there and runs its deterministic headless persistence subset instead.
The automated JUCE host test proves bundle discovery, instantiation, parameter
state recall and finite non-dry stereo rendering; the MSI roundtrip additionally
proves package layout and byte identity. Neither claims Cubase/REAPER scanner,
editor-window, production-signing or privileged installation acceptance on Windows.

The real VST3 host test requires canonical setter readbacks, a fresh instance whose defaults differ from the saved fixture, and non-silent effect output on both channels. Negative controls reject ignored parameter writes, passthrough, gain/delayed dry audio and dead channels. The separate state/audio roundtrip still compares all 19 parameters and every sample without fitting or alignment.

Preset tests include 39 editor-lifecycle/host-callback cases plus six macOS-only native Import/Export panel cases. The former include two real Save-As-then-Load flows: normal success must load the requested next preset, while an intervening restore must keep its newer state. Set `WHYKIKI_PRESET_TEST_NATIVE_ONLY=1` on `ReverseLabPresetTests` for focused native execution; `WHYKIKI_PRESET_TEST_NATIVE_CASE=import-detach` selects one valid case. CTest runs all six in fresh, serial processes with independent 60-second deadlines. The test-only Cocoa bridge requires an active desktop, rejects a fallback, and verifies panel/delegate/modal teardown, unchanged fixture files/state and a usable reopened editor. Real Cubase/REAPER window behavior remains a separate acceptance gate.

`WHYKIKI_PRESET_TEST_SAVE_RESTORE_ONLY=1` runs dirty-state guards and 14 deterministic persistence/control groups without opening an editor: real file-commit interleavings, same-ID/ABA restores, coherent capture, host-notification restores and bounded contention before writing. `WHYKIKI_PRESET_TEST_DIRTY_ONLY=1` runs just the dirty-state guards. Do not combine either focused mode with another. Their scheduling hooks exist only in the preset-test target, never in the VST3.

`ReverseLabListenerLock` runs the genuine held-JUCE-listener-lock regression separately with a 20-second CTest timeout; the normal DSP binary also includes it. DSP, parameter-text and bundle tests have 300-second limits, the non-native full preset test 600 seconds and each native panel case 60 seconds. A timeout fails the test.

## Control-thread contract

Program application and state restoration commit all parameter values, program identity, user-preset metadata and restored dimensions under a short control gate. The gate calls no JUCE parameter listeners, host callbacks or editor methods. State capture returns a complete committed state; even the first parameter callback observes the complete NEW state. A nested or independent state restore commits before returning, without waiting for another thread's notification dispatcher.

Notifications run separately and never write an old captured value back to a parameter. If a newer state supersedes a notification, the dispatcher replays current values. Each drain sends at most 64 notifications; outstanding work resumes on later timer ticks instead of being discarded. A tick which also applies a pending program can run two drains. Actual ranged parameters are authoritative for DSP, persistence and dirty status; APVTS raw/UI caches may lag until their notifications complete.

Program requests made off the message thread retain their deferred, coalesced next-service-tick behavior. Reentrant program requests also retain next-tick behavior, while redundant same-program notification echoes are ignored. `processBlock` makes one sequence-validated read of the committed parameter packet and never takes the control gate or waits for publication. This transaction guarantee does not make separate host-automation writes into a single atomic operation.

The additional overlapping JUCE-control-thread tests do not change VST3's UI-thread state-access contract and do not replace actual Cubase/REAPER acceptance. Arbitrary cross-thread waits inside external JUCE listeners can still create their own lock cycles.

## Earlier macOS release validation (1.0.4)

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

## Native Updates

Die Schaltfläche **Updates...** verwendet je Plattform einen separaten nativen
Updater; Details zum universellen macOS-Pfad stehen in [UPDATER.md](UPDATER.md).
Windows verwendet architekturspezifische, signaturgepinnte Updater/MSI-Pakete;
siehe [WINDOWS_UPDATER.md](WINDOWS_UPDATER.md) und
[WINDOWS_INSTALLER.md](WINDOWS_INSTALLER.md); siehe für den manuellen
Tag-/Signing-Ablauf zusätzlich [WINDOWS_RELEASE.md](WINDOWS_RELEASE.md). Ohne echten Zertifikatspin bleibt
der Windows-Button deaktiviert. CI-Ausgaben sind ausdrücklich keine
Distributionsartefakte; reale signierte Cubase-/REAPER-Abnahme bleibt offen.
