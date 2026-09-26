#!/usr/bin/env bash
# Build natively and run every app headless with its script. Screenshots land
# in out/. A tests/scripts/<app>.expect file lists regexes the log must
# match (one per line). Exit status is non-zero if any app fails to start or crashes.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -G Ninja >/dev/null
cmake --build build
mkdir -p out
fail=0
for s in tests/scripts/*.txt; do
    app=$(basename "$s" .txt)
    [ -x "build/bin/$app" ] || { echo "skip $app (not built)"; continue; }
    if timeout 60 "build/bin/$app" --headless --script "$s" --audio-out "out/$app.wav" > "out/$app.log" 2>&1; then
        exp="tests/scripts/$app.expect"
        if [ -f "$exp" ]; then
            while IFS= read -r re; do
                [ -z "$re" ] || [[ "$re" == \#* ]] && continue
                grep -Eq -- "$re" "out/$app.log" || { echo "FAIL $app (log lacks /$re/)"; fail=1; continue 2; }
            done < "$exp"
        fi
        echo "ok   $app"
    else
        echo "FAIL $app (see out/$app.log)"; fail=1
    fi
done
exit $fail
