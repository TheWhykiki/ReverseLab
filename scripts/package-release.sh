#!/bin/bash
set -euo pipefail

configuration="${1:-Release}"
project_root="$(cd "$(dirname "$0")/.." && pwd)"
# The version is defined once in CMakeLists.txt (project(... VERSION x.y.z)); pass a second
# argument only to override it deliberately.
cmake_version="$(sed -nE 's/^project\(ReverseLab VERSION ([0-9.]+).*/\1/p' "$project_root/CMakeLists.txt")"
if [[ -z "$cmake_version" ]]; then
    echo "Could not read the project version from CMakeLists.txt." >&2
    exit 1
fi
version="${2:-$cmake_version}"
plugin="$project_root/build/ReverseLab_artefacts/$configuration/VST3/ReverseLab.vst3"
dist="$project_root/dist"
stage="$(mktemp -d)"
application_identity="${REVERSELAB_APPLICATION_IDENTITY:-}"
installer_identity="${REVERSELAB_INSTALLER_IDENTITY:-}"
notary_profile="${REVERSELAB_NOTARY_PROFILE:-}"
trap 'rm -rf "$stage"' EXIT

if [[ ! -d "$plugin" ]]; then
    echo "Missing plug-in: $plugin" >&2
    echo "Build ReverseLab_VST3 in $configuration first." >&2
    exit 1
fi

bundle_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$plugin/Contents/Info.plist")"
if [[ "$bundle_version" != "$version" ]]; then
    echo "Version mismatch: requested $version, but the built VST3 is $bundle_version." >&2
    echo "Reconfigure and rebuild ReverseLab_VST3 before packaging." >&2
    exit 1
fi

rm -rf "$dist"
mkdir -p "$dist" "$stage/Library/Audio/Plug-Ins/VST3"
ditto "$plugin" "$stage/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3"

staged_plugin="$stage/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3"
package="$dist/ReverseLab-$version-macOS-universal.pkg"

if [[ -n "$application_identity" ]]; then
    codesign --force --deep --options runtime --timestamp \
        --sign "$application_identity" "$staged_plugin"
else
    echo "Warning: REVERSELAB_APPLICATION_IDENTITY is unset; applying a fresh ad-hoc signature." >&2
    codesign --force --deep --sign - "$staged_plugin"
fi
codesign --verify --deep --strict --verbose=2 "$staged_plugin"

pkgbuild_args=(
    --root "$stage"
    --identifier "audio.whykiki.reverselab.pkg"
    --version "$version"
    --install-location /
)
if [[ -n "$installer_identity" ]]; then
    pkgbuild_args+=(--sign "$installer_identity")
else
    echo "Warning: REVERSELAB_INSTALLER_IDENTITY is unset; the installer package will be unsigned." >&2
fi
pkgbuild "${pkgbuild_args[@]}" "$package"

if [[ -n "$notary_profile" ]]; then
    if [[ -z "$application_identity" || -z "$installer_identity" ]]; then
        echo "Notarization requires both signing identities." >&2
        exit 1
    fi
    xcrun notarytool submit "$package" --keychain-profile "$notary_profile" --wait
    xcrun stapler staple "$package"
    xcrun stapler validate "$package"
    # The package's notarization ticket also covers its nested VST3. Staple that ticket to the
    # standalone bundle before archiving it so the ZIP remains verifiable while offline.
    xcrun stapler staple "$staged_plugin"
    xcrun stapler validate "$staged_plugin"
    codesign --verify --deep --strict --verbose=2 "$staged_plugin"
fi

ditto -c -k --sequesterRsrc --keepParent \
    "$staged_plugin" "$dist/ReverseLab-$version-macOS-universal-VST3.zip"

(
    cd "$dist"
    shasum -a 256 ./*.pkg ./*.zip > SHA256SUMS.txt
)

echo "Release files written to $dist"
