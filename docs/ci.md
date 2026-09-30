# Testing in CI (GitHub Action)

This repository is also a GitHub Action. Add it to your own app's
repository and every push builds the app for the emulator, runs its
[test script](scripting.md) headless and keeps the log and screenshots. A
failed `expect` fails the check and is marked on the script line.

## Start from the template

`tools/fw2emu new --repo ~/my_app` creates an app folder that is ready to be
its own repository: the app, its `test.txt`, a README and
`.github/workflows/fw2emu.yml`. Push it to a new GitHub repository and the
first run starts. The same files are in
[`examples/app-template`](https://github.com/dfdarty/freewili2-emu/tree/main/examples/app-template).

## Set it up by hand

Your repository needs the app folder (with its `fw2_display_app()`
`CMakeLists.txt`, as in [Your first app](first-app.md)) and a test script,
by default `test.txt` in that folder. Then add
`.github/workflows/fw2emu.yml`:

```yaml
name: FREE-WILi 2 emulator
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7
      - uses: dfdarty/freewili2-emu@v2
        with:
          app: .                 # the folder with CMakeLists.txt
```

Push it. The run shows ✅ or ❌ with a summary, and the **Artifacts** of the
run hold the log, the screenshots the script took and the recorded audio.

Setting up is quick: the action fetches the emulator and WiliBSP and builds
only your app (about ten seconds of building on a 2-core machine). The test
itself takes as long as the script. `hwcheck` adds a download of the Pico
SDK and Arm GCC the first time; later runs take it from the cache.

## Options

| Input | Default | |
|---|---|---|
| `app` | `.` | the app folder, relative to your repository |
| `script` | `APP/test.txt` | the test script; `SCRIPT.expect` next to it is checked too |
| `args` | | extra [emulator flags](cli.md), e.g. `--sensor-csv logs/flight.csv` |
| `sanitize` | `false` | build with AddressSanitizer and UBSan: memory bugs fail the run. App code then runs at the runner's full speed (the checks would skew the chip-speed estimate); add `args: --cpu chip` to slow it anyway |
| `hwcheck` | `false` | also build for the real chip and check image size, RAM, PSRAM and stack ([details](debugging.md#real-hardware-check-toolsfw2emu-hwcheck)); the SDK and toolchain are cached after the first run. If the check passes, the app's UF2 goes in `out` too |
| `out` | `out` | where the log, audio and SD card go (point screenshots there too: `screenshot out/…`) |
| `artifact-name` | `fw2emu-APPNAME` | name of the uploaded artifact |
| `upload` | `true` | upload `out` as an artifact, pass or fail |

The step's outputs: `result` is `pass` or `fail`, and with `hwcheck`, `uf2` is
the path of the app's UF2 in `out` (empty if the check failed).

App code runs at the RP2350's estimated speed, measured against the runner
at the start of each run, so a slower runner doesn't make the app look
slower than it is; load that changes during the run still can
([CPU speed](debugging.md#is-it-fast-enough-cpu-speed)).
The log ends with each core's load. `args: --cpu host` runs app code at the
runner's full speed instead.

## Examples

Several apps in one repository, each with its own test, with sanitizers:

```yaml
jobs:
  test:
    runs-on: ubuntu-24.04
    strategy:
      matrix:
        app: [apps/logger, apps/viewer]
    steps:
      - uses: actions/checkout@v7
      - uses: dfdarty/freewili2-emu@v2
        with:
          app: ${{ matrix.app }}
          sanitize: true
          hwcheck: true
```

A flight computer tested against a recorded flight:

```yaml
      - uses: dfdarty/freewili2-emu@v2
        with:
          app: .
          script: tests/flight.txt
          args: --sensor-csv tests/flight.csv
```

with `tests/flight.txt` checking what the app logs:

```text
wait 2000
expect "armed" 3000
expect "launch detected" 5000
expect "apogee at" 15000
screenshot out/after_flight.png
quit
```

Publishing a release with its UF2 attached, as WiliBSP asks of published
apps: push a tag such as `v1.0.0`, and this workflow tests the app, checks
it against the chip and attaches the UF2 to a GitHub release:

```yaml
name: Release
on:
  push:
    tags: ["v*"]
permissions:
  contents: write
jobs:
  release:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7
      - id: fw2
        uses: dfdarty/freewili2-emu@v2
        with:
          app: .
          hwcheck: true
      - run: gh release create "$GITHUB_REF_NAME" "${{ steps.fw2.outputs.uf2 }}" --generate-notes
        env:
          GH_TOKEN: ${{ github.token }}
```

## Run the same test on your PC

```sh
tools/fw2emu test path/to/app              # or --script FILE, --sanitize
```

It builds the app, runs the script and prints PASS or FAIL, the same way
the action does ([details](cli.md#fw2emu-test-build-one-app-run-its-test-and-check-it)).

## Which version

- `@v2` (recommended) follows the v2 releases: fixes and new models arrive
  without changes to your workflow. Anything that would break a v2 workflow
  gets a new major version.
- `@v2.0.0` (or a commit SHA) stays on exactly one version.
- `@v1` stays at 1.2.0, where app code ran at the PC's full speed. Moving to
  `@v2` runs it at the chip's speed; see the
  [2.0.0 notes](https://github.com/dfdarty/freewili2-emu/blob/main/CHANGELOG.md#200--2026-09-28).
- `@main` is the latest commit, for trying something before it is released.
