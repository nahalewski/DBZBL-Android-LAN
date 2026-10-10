#!/bin/sh
set -eu
mac_project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$mac_project_root"
# Keep the pinned MoltenVK source compatible with this native Mac build.
mac_mvk_source="$mac_project_root/thirdparty/rexglue-sdk/thirdparty/moltenvk"
mac_mvk_patch="$mac_project_root/macos/moltenvk-drawable-lifetime.patch"
if [ ! -f "$mac_mvk_source/MoltenVK/MoltenVK/GPUObjects/MVKImage.mm" ]; then
    echo "MoltenVK source is missing; initialize the SDK submodules first." >&2
    exit 1
fi
if (cd "$mac_mvk_source" && patch --dry-run --forward -p1 -i "$mac_mvk_patch" >/dev/null 2>&1); then
    (cd "$mac_mvk_source" && patch --forward -p1 -i "$mac_mvk_patch")
elif ! (cd "$mac_mvk_source" && patch --dry-run --forward -R -p1 -i "$mac_mvk_patch" >/dev/null 2>&1); then
    echo "The MoltenVK lifetime patch does not match this SDK version." >&2
    exit 1
fi
cmake --preset mac-arm64-release
cmake --build --preset mac-arm64-release --target burstlimit_codegen --parallel "${BURSTLIMIT_BUILD_JOBS:-6}"
# Fresh clones need a configure after codegen emits the source list.
cmake --preset mac-arm64-release
cmake --build --preset mac-arm64-release --target burstlimit --parallel "${BURSTLIMIT_BUILD_JOBS:-6}"
