# Release candidate contract

Run `./scripts/package-release.sh Release` from a macOS checkout with initialized JUCE, CMake, Python 3 and the Xcode command-line tools. `REVERSELAB_BUILD_JOBS` defaults to 2. The full rebuild is intentional.

1. Validate configuration, version and paired signing identities before packaging. An explicit version must match CMake.
2. Snapshot tracked file bytes and new Source/Tests/scripts/Presets/Resources/Assets inputs, including the actual initialized submodule contents. Record commits, dirty status, modes and SHA-256 hashes. Reject a changing snapshot or source symlinks.
3. Require `generate-presets.py --check` to pass against the snapshot before compiling: authored JSON recipes must match the compiled factory-bank header. Build the snapshot in a temporary directory under ignored `dist`. Build every target and require all registered tests to pass; keep JUnit and CTest logs.
4. Check version, executable permissions, both arm64/x86_64 slices, macOS 11.0 deployment targets and strict signatures. Sign the staged candidate using the provided application identity, or explicitly ad hoc.
5. Build the installer. If a notary profile is provided, require an Accepted response and validate stapled tickets. No external notarization is attempted without the supplied identities/profile.
6. Extract the ZIP and installer payload independently, verify each binary hash/signature, and actually instantiate/render/restore each VST3 through the host test.
7. Publish the completed artifact directory with an atomic same-filesystem rename under a publication lock. Never erase or replace earlier candidates. Write source/release manifests and checksums covering the artifacts and test reports.

Pipeline unit tests inject stale factory recipes, build, test, signing, packaging, corruption, host-loading and publication failures. The recipe check runs the actual generator on staged fixtures; macOS tools are faked. These are safety checks, not evidence of actual Apple notarization. A real local candidate run is a separate check.

Failures inside the staged run retain a distinct `dist/failed-run-*` diagnostic folder before temporary cleanup. `failure.json` records the failed step, external tool/exit code when available, and the snapshot commit/hash only if the snapshot completed. Available JUnit, CTest, CMake diagnostic logs and the source manifest are retained with checksums. Captured failed-command output is retained when available; this is not a complete compiler transcript or an automatic macOS crash-report collection. Environment variables and command arguments are not serialized. No failed ZIP/PKG is published as a release, and previous candidates are unchanged. A diagnostic-writing failure reports a warning without hiding the original error. These diagnostic folders are local evidence, not distribution artifacts; inspect before sharing and remove only explicitly selected old runs when no longer needed.

The VST3 host test uses legal parameter values obtained through the plugin's own formatter/parser. Hosts can cache unsnapped normalized requests; treating those cached requests as the saved value would produce a false state-recall failure. The test still compares all 19 saved parameters and every rendered sample after reload.

Local ad-hoc success is not public-distribution clearance. Developer-ID signing, actual notarization, installation on another Mac and subjective listening remain independent release gates. Creating a local candidate does not publish a GitHub Release or install anything.
