#!/bin/bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
configuration="${1:-Release}"
source_bundle="$project_dir/build/ReverseLab_artefacts/$configuration/VST3/ReverseLab.vst3"
destination_dir="$HOME/Library/Audio/Plug-Ins/VST3"

if [[ ! -d "$source_bundle" ]]; then
    echo "Build not found: $source_bundle" >&2
    exit 1
fi

codesign --force --deep --sign - "$source_bundle"
mkdir -p "$destination_dir"
ditto "$source_bundle" "$destination_dir/ReverseLab.vst3"
codesign --force --deep --sign - "$destination_dir/ReverseLab.vst3"
echo "Installed $destination_dir/ReverseLab.vst3"
