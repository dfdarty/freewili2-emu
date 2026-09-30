# Command line

## The emulator

Every app builds into its own program (`build/bin/<app>`). They all take the
same options:

| Option | Effect |
|---|---|
| `--headless` | no window: for tests, CI and agents (automatic when there is no display, see below) |
| `--script FILE` | run an [input script](scripting.md); its `quit` ends the process |
| `--run-ms N` | end the run after N milliseconds |
| `--shot-on-exit PNG` | save the LCD as a PNG when the run or the app ends |
| `--rtt` | serve RTT on 127.0.0.1:9090 (DIAG text) and :9091 (agentio), like WiliBSP's `fw rtt` — see [WiliBSP's agent tools](wilibsp-tools.md). If a port is taken (usually by another emulator with `--rtt`), the emulator stops with an error |
| `--rails HEX` | power zones already on at launch (default `0x8183`) |
| `--scale N` | window scale factor |
| `--audio-out WAV` | record everything the codec plays |
| `--mic-wav WAV` | sound reaching the microphones, looped |
| `--mute` | don't play audio through the PC |
| `--record FILE` | write what you do (keys, clicks, touches, F2 screenshots) as an [input script](scripting.md#recording-a-script) when the run ends, with the app's log lines as `# expect` hints |
| `--sensor-csv FILE` | play a time-stamped sensor log from the start of the run: a CSV, or `@launch` for the built-in rocket flight ([format](sensors-and-sound.md#playing-a-sensor-log)) |
| `--board-id HEX16` | the chip's 64-bit unique id, as `pico_get_unique_board_id()` returns it (default `E6616408432A7B15`); give two emulators different ids when they talk to each other |
| `--radio FILE` | the Wi-Fi networks and Bluetooth devices in range, for the stock scans an app asks MAIN for: a scene file, or `@town` for a built-in neighbourhood ([Wi-Fi and Bluetooth scans](main-link.md#wi-fi-and-bluetooth-scans)) |
| `--peer NAME=MODE` | another OneWili client for [peer streams](main-link.md#peer-streams-esp32-cm0-pc) (repeatable): `esp32=dualcpu` runs a stand-in for the ESP32 half of WiliBSP's `dualcpu`; `esp32=script`, `cm0=script` and `host=script` are driven from an [input script](scripting.md) |
| `--rtc WHEN` | the board clock at start-up, e.g. `"2027-05-16 09:30:00"`; it then runs with emulator time. Without it the clock starts at the PC's local time. Apps read and set it with `ow_hardware_get_time()` / `ow_hardware_set_time()` ([MAIN link](main-link.md#the-board-clock)) |
| `--perf` | log CPU and bus load and LCD throughput once a second (`perf: CPU0 45%  SPI1 62.5 MHz 41%  I2C1 400 kHz 2%  LCD 10.4 screens/s`); the same line is always on the device's bottom edge — see [bus timing](debugging.md#is-it-fast-enough-bus-timing) |
| `--cpu chip\|host` | `chip` (the default): app code runs at the RP2350's speed; `host`: at your PC's full speed — see [CPU speed](debugging.md#is-it-fast-enough-cpu-speed) |
| `--cpu-factor F` | your PC runs app code F times as fast as the chip; the default is measured at start-up |
| `--instant-bus` | SPI and I2C transfers take no time, as before bus timing existed; for comparing, not for testing |
| `--sdcard DIR` | folder that stands in for the SD card in the MAIN CPU's slot (default `./sdcard`, created with sample files on first use; `none` = no card) — see [The MAIN processor link](main-link.md) |
| `--sensor NAME=V[,V...]` | set a sensor or the room sound at start-up, e.g. `temp=24`, `tilt=30,0`, `tone=1000,8000`, `mics=1,1,0,1` (repeatable; names in [Sensors and sound](sensors-and-sound.md)); also header inputs, e.g. `gpio12=1`, and `vrefext=3.3` ([MAIN link](main-link.md#header-gpio-and-vio)) |
| `-v`, `-vv` | log model activity: power commands, touches, IO expander, audio, OneWili replies; `-vv` also logs every sleep and SD request |

In the browser, pass the same options in the URL: `?app=hello_audio&args=-v`.
Without `?args=`, `dualcpu` starts with `--peer esp32=dualcpu --radio @town`.
`--sensor` values given there also set the page's sliders.

### When the process ends

- A run with `--run-ms`, `--script` or `--shot-on-exit` ends the process
  when it is over, with or without a window: exit status 0, or 1 if a
  script [`expect`](scripting.md#pass-or-fail-expect) timed out. Errors in
  the options or the script exit with status 2.
- If the **app** ends itself (HOME held 5 s, `main()` returning, a panic)
  during an interactive run with a window, the window stays open on the
  last frame, and the log says so. Close it to quit. Headless, the process
  exits.
- **No display.** On Linux, when neither `DISPLAY` nor `WAYLAND_DISPLAY` is
  set, or SDL can only offer its offscreen or dummy driver, the emulator
  says `no display … running headless` and carries on without a window, and
  without host audio. Stop it with ++ctrl+c++, or give it `--run-ms` or a
  script.

## `tools/fw2emu`

A helper for the things people do most. Its own options go **before** the
app folder; everything after the folder is passed to the emulator.

### `fw2emu run` — build one app and run it

```sh
tools/fw2emu run [--sanitize] [--m32] [--debug] [--gdb] [--build-only] [--build-dir DIR] APP_DIR [emulator options]
```

`APP_DIR` is any folder with a `fw2_display_app()` `CMakeLists.txt`, inside
the repository or not. Only that app is configured, in its own build folder
(`build-run/` by default). With no display available it runs headless.

| Option | Effect |
|---|---|
| `--sanitize` | AddressSanitizer + UBSan build ([details](debugging.md#sanitizers-fw2_emu_sanitize)) |
| `--m32` | 32-bit build, headless ([details](debugging.md#32-bit-build-fw2_emu_32bit)) |
| `--debug` | `CMAKE_BUILD_TYPE=Debug` |
| `--gdb` | Debug build, started under gdb with a breakpoint in the app's `main` |
| `--build-only` | build, print the program's path and stop (`bin/current-app` also points at it) |
| `--build-dir DIR` | default `build-run/`; `-asan`, `-m32` and `-debug` are added to the name with the options above |

### `fw2emu test` — build one app, run its test and check it

```sh
tools/fw2emu test [--script FILE] [--out DIR] [--timeout S] [--build-dir DIR] [--sanitize] [--m32] APP_DIR [emulator options]
```

Builds the app headless (no SDL needed, in `build-test/`), runs it with its
[input script](scripting.md) — `APP_DIR/test.txt` unless `--script` says
otherwise — and checks the result: the script's `expect` lines, and
`SCRIPT.expect` next to the script if there is one (the same rules as
[the test runner](#the-test-runner)). It prints PASS or FAIL with the
reason, and leaves the log, the recorded audio and the SD card the app used
in `out/`. Exit status 0 passed, 1 failed, 2 a mistake in the script or the
options. This is what the [GitHub Action](ci.md) runs; inside GitHub
Actions a failure also becomes an annotation on the script line and a job
summary.

### `fw2emu new` — start a new app from the template

```sh
tools/fw2emu new [--repo] [--name NAME] DIR
```

Creates `DIR` with a working app (`CMakeLists.txt`, `main.c`) and its test
(`test.txt`), named after the folder: it counts OK presses and colours the
LEDs, and its test passes. Put it in `apps/` to have it built with
everything else. `--repo` adds a README, a `.gitignore` and a GitHub
workflow that tests every push, for an app in its own repository. The files
come from `examples/app-template/`.

### `fw2emu web` — build one app for the browser and serve it

```sh
tools/fw2emu web [--port 8080] [--no-serve] [--build-dir DIR] APP_DIR [emulator options]
```

Needs `emcc` (Emscripten 4.0.15) on `PATH`. Serves on `127.0.0.1` only and
passes the emulator options as `?args=`.

#### The app's page: `fw2emu-web.json`

A `fw2emu-web.json` in the app folder sets up the app's page in the browser
build: your apps in `apps/`, and those built with `fw2emu web`, Docker's
`EXTRA_APPS` or `FW2_EMU_EXTRA_APPS`.
Every member is optional:

```json
{
  "args": "--radio @town",
  "source": "https://github.com/you/your-app",
  "license": "MIT",
  "guide": {
    "what": "What the app is.",
    "see": "What you see when it starts.",
    "tryit": [
      { "label": "A camera appears", "send": ["radio ap b4:1e:52:00:11:22 6 3 -58 FLOCK-CAM-2291"],
        "text": "- the app raises its alert." },
      { "label": "Tap twice", "send": ["touch 240 40", "touch 240 40"], "gap_ms": 2500 },
      { "text": "A line with no button." }
    ],
    "check": "What to look for in the log.",
    "panels": ["log"]
  }
}
```

- `args`: the [options](#the-emulator) the page starts the app with when
  its URL has no `?args=`.
- `source`, `license`: a link next to the app's name (an `https://` address).
- `guide`: the **About this app** card. Each `tryit` entry is a line, with
  a ▶ button when it has `send`: [input-script](scripting.md) commands,
  sent `gap_ms` apart (default 300). `wait`, `expect` and `quit` aren't
  accepted from the page. `panels` highlights the panels the app uses:
  `controls`, `sensors`, `sound`, `header`, `log`. The page shows all of it
  as plain text.

The build warns about a malformed file and leaves it out.

### `fw2emu hwcheck` — check the app against the real chip

```sh
tools/fw2emu hwcheck [APP_DIR ...] [-v] [--json] [-o FILE] [--exclude APP] [--strict]
                     [--build-dir DIR] [--sdk PATH] [--toolchain DIR] [--fetch-toolchain]
                     [--uf2-dir DIR]
```

Builds with the Pico SDK and Arm GCC and reports SRAM image, RAM, PSRAM and
worst-case stack; exits 1 on an overflow or a failed build. With no folders,
it checks every app the emulator builds. Known upstream failures of
WiliBSP's own example apps show as `KNOWN (upstream)` and don't change the
exit status; `--strict` makes them count. Each app that passes shows its
UF2, the file you install on the board; `--uf2-dir DIR` also copies them
into DIR. Full description:
[Debugging and hardware checks](debugging.md#real-hardware-check-toolsfw2emu-hwcheck).

## CMake options

For building with CMake directly (`cmake -S . -B build -G Ninja -D...`):

| Option | Default | Effect |
|---|---|---|
| `FW2_EMU_UPSTREAM_APPS` | the 14 WiliBSP apps that run, plus `canblast` (it builds; CAN isn't modelled) | which WiliBSP example apps to build |
| `WILIBSP_DIR` | `third_party/wilibsp` | the WiliBSP checkout to build against |
| `FW2_EMU_LOCAL_APPS` | `ON` | build every folder in `apps/` |
| `FW2_EMU_TEST_APPS` | `ON` natively, `OFF` on the web | build the self-test apps in `tests/apps/` |
| `FW2_EMU_EXTRA_APPS` | empty | extra app folders, absolute paths, `;`-separated |
| `FW2_EMU_SANITIZE` | `OFF` | AddressSanitizer + UBSan (native only) |
| `FW2_EMU_32BIT` | `OFF` | `-m32` build (native only) |
| `FW2_EMU_SDL` | `ON` (`OFF` with `FW2_EMU_32BIT`) | window and host audio via SDL2; `OFF` builds a headless-only emulator |
| `FW2_AGENTIO` | `ON` natively, `OFF` on the web | WiliBSP's agentio harness (needed for `fw.py screenshot`) |
| `FW2_EMU_WEB_DEMO` | `OFF` | web page: show the note that the hosted demo runs WiliBSP's example apps (and SquachWatch and Orca-9) only (on for the GitHub Pages build) |
| `FW2_EMU_WEB_ENV` | `web` | Emscripten environment; `web,node` runs builds headless under Node, with the host's files visible (`--script`, `--sdcard`, screenshots) |

## The test runner

```sh
tests/smoke.sh
BUILD_DIR=build-asan CMAKE_ARGS="-DFW2_EMU_SANITIZE=ON" tests/smoke.sh
```

Builds, then runs every app that has a script — `tests/scripts/<app>.txt`,
or `apps/<app>/test.txt` for your own apps — headless, each with a fresh SD
card folder (`out/sdcard-<app>`). Screenshots, logs and audio land in
`out/`. An app fails if it exits non-zero (a script `expect` timed out) or
if its log doesn't meet `tests/scripts/<app>.expect` (or
`apps/<app>/test.expect`): each line there is a regular expression the log
must match; a line starting with `!` must not match, and `@sd PATH REGEX`
checks a file the app left on its SD card. Extra emulator options for one
app go in `tests/scripts/<app>.args` (or `apps/<app>/test.args`).
