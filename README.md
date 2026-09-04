# ReverseLab 1.1.0 (development)

ReverseLab is a tempo-synchronised stereo reverse effect for Cubase on macOS.

Copyright © 2026 Whykiki Audio. ReverseLab is free software licensed under the GNU Affero General Public License v3.0; see [LICENSE](LICENSE). JUCE remains available under its own dual-licensing terms.

## Download and install

Download the current `.pkg` from [GitHub Releases](https://github.com/TheWhykiki/ReverseLab/releases/latest), open it, and follow the installer. It installs the universal VST3 into `/Library/Audio/Plug-Ins/VST3`. Restart Cubase or trigger a plug-in rescan afterwards.

The VST3 inside the current release is ad-hoc signed, but the `.pkg` itself is unsigned and the release is not Apple-notarized. Gatekeeper will therefore block the installer on first launch. Open it once, then go to **System Settings → Privacy & Security** and choose **Open Anyway**. Alternatively unzip the raw VST3 from the release, copy it to `~/Library/Audio/Plug-Ins/VST3`, and remove its quarantine flag with `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3`. The release also includes SHA-256 checksums.

## Build

```sh
git clone --recurse-submodules https://github.com/TheWhykiki/ReverseLab.git
cd ReverseLab
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ReverseLab_VST3 ReverseLabTests ReverseLabPresetTests
ctest --test-dir build -C Release --output-on-failure
```

The project builds a universal `arm64`/`x86_64` VST3. Install it only after tests pass:

```sh
./scripts/install-local.sh Release
```

`./scripts/package-release.sh Release` creates the `.pkg`, VST3 ZIP, and checksums and refuses to package a stale bundle whose embedded version differs from `CMakeLists.txt`. For a Developer-ID-signed and notarized package, set `REVERSELAB_APPLICATION_IDENTITY`, `REVERSELAB_INSTALLER_IDENTITY`, and `REVERSELAB_NOTARY_PROFILE` to the corresponding signing identities and `notarytool` keychain profile before running it.

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

## Continuous integration

Every push and pull request builds and strictly verifies the universal VST3 and its ZIP roundtrip on macOS, runs the DSP/processor suite on macOS and Linux, and exercises the Makefiles path under ASan/UBSan. See `.github/workflows/ci.yml`.

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
