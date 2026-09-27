#!/usr/bin/env bash
# End-to-end check of --record: drive a real SDL window (on a virtual X
# display) with xdotool, let the emulator write the script, then replay it
# headless with every "# expect" hint switched on. Needs Xvfb and xdotool;
# CI's native job runs it.
#
#   BUILD_DIR=build tests/record.sh
set -euo pipefail
cd "$(dirname "$0")/.."
build=${BUILD_DIR:-build}
app="$build/bin/script_check"
[ -x "$app" ] || { echo "record: $app not built (run tests/smoke.sh first)"; exit 1; }
out=$(mktemp -d)
trap 'kill "${xvfb:-}" "${emu:-}" 2>/dev/null || true; rm -rf "$out"' EXIT

display=:$((90 + RANDOM % 9))
Xvfb "$display" -screen 0 1280x800x24 >/dev/null 2>&1 & xvfb=$!
export DISPLAY=$display
for _ in $(seq 50); do xdotool getmouselocation >/dev/null 2>&1 && break; sleep 0.1; done

"$app" --record "$out/rec.txt" --mute > "$out/live.log" 2>&1 & emu=$!
win=
for _ in $(seq 100); do win=$(xdotool search --name "FREE-WILi" 2>/dev/null | head -1) && [ -n "$win" ] && break; sleep 0.1; done
[ -n "$win" ] || { echo "record: no emulator window"; cat "$out/live.log"; exit 1; }
sleep 4                                               # past boot
xdotool keydown --window "$win" o; sleep 0.4; xdotool keyup --window "$win" o; sleep 1       # OK, 400 ms
xdotool mousemove --window "$win" 450 236 mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 1  # tap at 240,160
xdotool mousemove --window "$win" 310 176 mousedown 1                                           # swipe
for i in 1 2 3 4 5 6 7 8; do xdotool mousemove --window "$win" $((310 + i * 25)) $((176 + i * 12)); sleep 0.05; done
xdotool mouseup 1; sleep 1
xdotool keydown --window "$win" 5; sleep 0.2; xdotool keydown --window "$win" 3; sleep 0.3   # RED, then GREEN, overlapping
xdotool keyup --window "$win" 5; sleep 0.2; xdotool keyup --window "$win" 3; sleep 0.8
kill -INT "$emu"; wait "$emu" || true; emu=

echo "--- recorded:"; grep -v '^# [A-Z]\|^# app\|^# uncomment\|^# https' "$out/rec.txt"
fail=0
for want in '^press OK [0-9]+$' '^touch 240 160 [0-9]+$' '^drag 100 100 [0-9]+ [0-9]+ [0-9]+$' \
            '^hold RED$' '^hold GREEN$' '^release RED$' '^release GREEN$' '^# expect "script_check: ready"$'; do
    grep -Eq -- "$want" "$out/rec.txt" || { echo "FAIL record: script lacks /$want/"; fail=1; }
done
sed 's/^# expect/expect/' "$out/rec.txt" > "$out/replay.txt"
if "$app" --headless --script "$out/replay.txt" > "$out/replay.log" 2>&1; then
    echo "ok   the recording replays with every expect on"
else
    echo "FAIL record: replay failed"; grep -E "FAIL|FATAL" "$out/replay.log" | head -3; fail=1
fi
for want in 'check: btn 11 held (3[5-9][0-9]|4[0-4][0-9]) ms' 'check: touch held [0-9]+ ms from 240,160' 'check: btn 4 held' 'check: btn 2 held'; do
    grep -Eq -- "$want" "$out/replay.log" || { echo "FAIL record: replay lacks /$want/"; fail=1; }
done
[ $fail = 0 ] && echo "ok   record"
exit $fail
