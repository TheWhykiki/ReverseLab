# ReverseLab 1.0

ReverseLab is a tempo-synchronised stereo reverse effect for Cubase on macOS.

## Download and install

Download the current `.pkg` from [GitHub Releases](https://github.com/TheWhykiki/ReverseLab/releases/latest), open it, and follow the installer. It installs the universal VST3 into `/Library/Audio/Plug-Ins/VST3`. Restart Cubase or trigger a plug-in rescan afterwards.

Because the current package is ad-hoc signed rather than Apple-notarized, macOS may require **Control-click → Open** on first launch. The release also includes the raw VST3 as a ZIP and SHA-256 checksums.

## Build

```sh
git clone --recurse-submodules https://github.com/TheWhykiki/ReverseLab.git
cd ReverseLab
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ReverseLab ReverseLabTests
ctest --test-dir build -C Release --output-on-failure
```

The project builds a universal `arm64`/`x86_64` VST3. Install it only after tests pass:

```sh
./scripts/install-local.sh Release
```

The currently verified local installation uses the user-level VST3 directory, which Cubase scans without administrator privileges:

`~/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3`

## Controls

- Tempo-synchronised or free-time stereo reverse segments
- Linked or independent left/right segment lengths
- Reverse speed, crossfade, equal-power dry/wet, and output gain
- Freeze, retrigger, stable feedback, high/low-pass filtering
- Stereo offset and deterministic randomisation
- Six factory programs and full Cubase automation/state restoration

Changing segment length changes the reported plug-in latency. Cubase compensates other tracks, while ReverseLab delays its dry path internally to maintain alignment.

## Verified release

- JUCE 8.0.15, VST3 SDK 3.8
- Universal `arm64` and `x86_64`
- Minimum deployment target macOS 11.0 in both binary slices
- Ad-hoc signed; public release packages are not yet Apple-notarized
- Cubase 15 `vstscanner` exit code 0
- REAPER 7.79 native-arm64 host test: VST3 instantiated, 26 parameters exposed, project state saved, 4 s offline render completed at 44.1 kHz/24-bit stereo with no clipped samples
- Automated DSP and processor tests pass at 44.1/48/88.2/96/192 kHz, variable block sizes, continuous free timing, latency-aligned bypass, state restoration, reset invalidation, 0.25×/1×/4× speed, feedback stability, freeze, and deterministic randomisation
