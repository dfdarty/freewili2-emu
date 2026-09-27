#!/usr/bin/env bash
# Build natively and run every app headless with its script: tests/scripts/<app>.txt
# for WiliBSP's apps, and apps/<app>/test.txt for your own (so an app joins the
# run without touching tests/). The run fails if the app exits non-zero, e.g.
# when a script `expect` times out. Screenshots land in out/.
# An .expect file next to the script (tests/scripts/<app>.expect or
# apps/<app>/test.expect) lists regexes the log must
# match (one per line; a line starting with ! is a regex it must NOT match, and
# "@sd PATH REGEX" checks a file the app left on its SD card).
# Each app gets a fresh SD card folder, out/sdcard-<app>. Exit status is
# non-zero if any app fails to start or crashes.
#
#   BUILD_DIR=build-asan CMAKE_ARGS="-DFW2_EMU_SANITIZE=ON" tests/smoke.sh
set -euo pipefail
cd "$(dirname "$0")/.."
build=${BUILD_DIR:-build}
# shellcheck disable=SC2086
cmake -S . -B "$build" -G Ninja ${CMAKE_ARGS:-} >/dev/null
cmake --build "$build"
mkdir -p out
fail=0
for s in tests/scripts/*.txt apps/*/test.txt; do
    [ -f "$s" ] || continue
    if [[ "$s" == apps/* ]]; then
        app=$(basename "$(dirname "$s")")
        exp="${s%.txt}.expect"
    else
        app=$(basename "$s" .txt)
        exp="tests/scripts/$app.expect"
    fi
    [ -x "$build/bin/$app" ] || { echo "skip $app (not built)"; continue; }
    rm -rf "out/sdcard-$app"
    if timeout 60 "$build/bin/$app" --headless --script "$s" --audio-out "out/$app.wav" \
            --sdcard "out/sdcard-$app" > "out/$app.log" 2>&1; then
        if [ -f "$exp" ]; then
            while IFS= read -r re; do
                [ -z "$re" ] || [[ "$re" == \#* ]] && continue
                if [[ "$re" == @sd\ * ]]; then
                    read -r _ f fre <<< "$re"
                    grep -Eq -- "$fre" "out/sdcard-$app/$f" 2>/dev/null || { echo "FAIL $app (SD file $f lacks /$fre/)"; fail=1; continue 2; }
                elif [[ "$re" == !* ]]; then
                    ! grep -Eq -- "${re:1}" "out/$app.log" || { echo "FAIL $app (log has /${re:1}/)"; fail=1; continue 2; }
                else
                    grep -Eq -- "$re" "out/$app.log" || { echo "FAIL $app (log lacks /$re/)"; fail=1; continue 2; }
                fi
            done < "$exp"
        fi
        echo "ok   $app"
    else
        echo "FAIL $app (see out/$app.log)"; fail=1
        # say why: a failed expect or screenshot, or a fatal script / option error
        grep -m1 -E -- "FATAL|FAIL script|FAIL --shot" "out/$app.log" | sed 's/^/     /' || true
    fi
done
exit $fail
