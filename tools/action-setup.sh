#!/usr/bin/env bash
# The GitHub Action's "Get the emulator" step. GitHub hands a composite action
# its repository without submodules, so this fetches the same commit again
# with WiliBSP (and WiliBSP's nested OneWili) and reports where it is.
#
# In:  ACTION_PATH (the action's files), ACTION_REPO / ACTION_REF (may be
#      empty inside composite actions; then they come from ACTION_PATH,
#      which is .../_actions/OWNER/REPO/REF).
# Out: root=DIR on $GITHUB_OUTPUT.
set -euo pipefail

if [ -f "$ACTION_PATH/third_party/wilibsp/bsp/fw2.h" ] &&
   [ -f "$ACTION_PATH/third_party/wilibsp/libs/onewili/wilibsp/CMakeLists.txt" ]; then
    root="$ACTION_PATH"        # `uses: ./` in a checkout with submodules
else
    repo=${ACTION_REPO:-}
    ref=${ACTION_REF:-}
    if [ -z "$repo" ] || [ -z "$ref" ]; then
        ref=$(basename "$ACTION_PATH")
        repo="$(basename "$(dirname "$(dirname "$ACTION_PATH")")")/$(basename "$(dirname "$ACTION_PATH")")"
    fi
    root="$RUNNER_TEMP/freewili2-emu"
    rm -rf "$root"
    echo "fetching $repo@$ref with its submodules"
    git init -q "$root"
    git -C "$root" remote add origin "https://github.com/$repo"
    git -C "$root" fetch -q --depth 1 origin "$ref"
    git -C "$root" checkout -q FETCH_HEAD
    git -C "$root" submodule update -q --init --recursive --depth 1
fi
echo "emulator: $root ($(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo 'no git'))"
echo "root=$root" >> "$GITHUB_OUTPUT"
