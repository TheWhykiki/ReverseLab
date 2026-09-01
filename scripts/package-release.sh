#!/bin/bash
set -euo pipefail

configuration="${1:-Release}"
version="${2:-1.0.0}"
project_root="$(cd "$(dirname "$0")/.." && pwd)"
plugin="$project_root/build/ReverseLab_artefacts/$configuration/VST3/ReverseLab.vst3"
dist="$project_root/dist"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

if [[ ! -d "$plugin" ]]; then
    echo "Missing plug-in: $plugin" >&2
    echo "Build ReverseLab_VST3 in $configuration first." >&2
    exit 1
fi

rm -rf "$dist"
mkdir -p "$dist" "$stage/Library/Audio/Plug-Ins/VST3"
ditto "$plugin" "$stage/Library/Audio/Plug-Ins/VST3/ReverseLab.vst3"

pkgbuild \
    --root "$stage" \
    --identifier "audio.whykiki.reverselab.pkg" \
    --version "$version" \
    --install-location / \
    "$dist/ReverseLab-$version-macOS-universal.pkg"

ditto -c -k --sequesterRsrc --keepParent \
    "$plugin" "$dist/ReverseLab-$version-macOS-universal-VST3.zip"

(
    cd "$dist"
    shasum -a 256 ./*.pkg ./*.zip > SHA256SUMS.txt
)

echo "Release files written to $dist"
