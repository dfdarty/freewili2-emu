#!/usr/bin/env bash
# Smoke-check a built image: serves the page, the app list and the apps with
# the right content types.   docker/smoke-image.sh IMAGE [PORT]
set -euo pipefail
image=$1
port=${2:-18080}
cid=$(docker run -d --rm -p "127.0.0.1:$port:80" "$image")
trap 'docker stop "$cid" >/dev/null' EXIT
url="http://127.0.0.1:$port"
for _ in $(seq 50); do curl -fsS -o /dev/null "$url/" 2>/dev/null && break; sleep 0.2; done

fail() { echo "FAIL: $*"; exit 1; }
curl -fsS "$url/" | grep -q '<canvas id="canvas"' || fail "index.html is not the emulator page"
apps=$(curl -fsS "$url/apps.json")
count=$(printf '%s' "$apps" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))') || fail "apps.json is not JSON"
[ "$count" -ge 1 ] || fail "apps.json lists no apps"
first=$(printf '%s' "$apps" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["name"])')
ctype() { curl -fsS -o /dev/null -w '%{content_type}' "$url/$1"; }
[ "$(ctype "$first.wasm")" = "application/wasm" ] || fail "$first.wasm is served as '$(ctype "$first.wasm")', not application/wasm"
case "$(ctype "$first.js")" in application/javascript*|text/javascript*) ;; *) fail "$first.js has type '$(ctype "$first.js")'";; esac
for a in $(printf '%s' "$apps" | python3 -c 'import json,sys; print(" ".join(a["name"] for a in json.load(sys.stdin)))'); do
    curl -fsS -o /dev/null "$url/$a.wasm" || fail "$a.wasm missing"
done
echo "ok: $count apps; index.html, apps.json, $first.js and application/wasm served"
