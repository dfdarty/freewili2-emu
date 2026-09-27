# Command line

## The emulator

Every app builds into its own program (`build/bin/<app>`). They all take the
same options:

| Option | Effect |
|---|---|
| `--headless` | no window: for tests, CI and agents |
| `--script FILE` | run an [input script](scripting.md) |
| `--run-ms N` | stop after N milliseconds |
| `--shot-on-exit PNG` | save the LCD as a PNG when the app exits |
| `--rtt` | serve RTT on 127.0.0.1:9090 (DIAG text) and :9091 (agentio), like WiliBSP's `fw rtt` — see [WiliBSP's agent tools](wilibsp-tools.md) |
| `--rails HEX` | power zones already on at launch (default `0x8183`) |
| `--scale N` | window scale factor |
| `--audio-out WAV` | record everything the codec plays |
| `--mic-wav WAV` | sound reaching the microphones, looped |
| `--mute` | don't play audio through the PC |
| `--sensor NAME=V[,V...]` | set a sensor or the room sound at start-up, e.g. `temp=24`, `tilt=30,0`, `tone=1000,8000`, `mics=1,1,0,1` (repeatable; names in [Sensors and sound](sensors-and-sound.md)) |
| `-v`, `-vv` | log model activity: power commands, touches, IO expander, audio; `-vv` also logs every sleep |

In the browser, pass the same options in the URL: `?app=hello_audio&args=-v`.

## `tools/fw2emu`

A helper for the things people do most. Its own options go **before** the
app folder; everything after the folder is passed to the emulator.

### `fw2emu run` — build one app and run it

```sh
tools/fw2emu run [--sanitize] [--m32] [--debug] [--gdb] [--build-dir DIR] APP_DIR [emulator options]
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

### `fw2emu web` — build one app for the browser and serve it

```sh
tools/fw2emu web [--port 8080] [--no-serve] [--build-dir DIR] APP_DIR [emulator options]
```

Needs `emcc` (Emscripten 4.0.15) on `PATH`. Serves on `127.0.0.1` only and
passes the emulator options as `?args=`.

### `fw2emu hwcheck` — check the app against the real chip

```sh
tools/fw2emu hwcheck [APP_DIR ...] [-v] [--json] [-o FILE] [--exclude APP]
                     [--build-dir DIR] [--sdk PATH] [--toolchain DIR] [--fetch-toolchain]
```

Builds with the Pico SDK and Arm GCC and reports SRAM image, RAM, PSRAM and
worst-case stack; exits 1 on an overflow or a failed build. With no folders,
it checks every app the emulator builds. Full description:
[Debugging and hardware checks](debugging.md#real-hardware-check-toolsfw2emu-hwcheck).

## CMake options

For building with CMake directly (`cmake -S . -B build -G Ninja -D...`):

| Option | Default | Effect |
|---|---|---|
| `FW2_EMU_UPSTREAM_APPS` | the WiliBSP apps that run | which WiliBSP example apps to build |
| `FW2_EMU_LOCAL_APPS` | `ON` | build every folder in `apps/` |
| `FW2_EMU_EXTRA_APPS` | empty | extra app folders, absolute paths, `;`-separated |
| `FW2_EMU_SANITIZE` | `OFF` | AddressSanitizer + UBSan (native only) |
| `FW2_EMU_32BIT` | `OFF` | `-m32` build (native only) |
| `FW2_EMU_SDL` | `ON` (`OFF` with `FW2_EMU_32BIT`) | window and host audio via SDL2; `OFF` builds a headless-only emulator |
| `FW2_AGENTIO` | `ON` natively, `OFF` on the web | WiliBSP's agentio harness (needed for `fw.py screenshot`) |
| `FW2_EMU_WEB_ENV` | `web` | Emscripten environment; `web,node` runs builds headless under Node |

## The test runner

```sh
tests/smoke.sh
BUILD_DIR=build-asan CMAKE_ARGS="-DFW2_EMU_SANITIZE=ON" tests/smoke.sh
```

Builds, then runs every app that has a script in `tests/scripts/<app>.txt`
headless. Screenshots, logs and audio land in `out/`. If
`tests/scripts/<app>.expect` exists, each line is a regular expression the
app's log must match.
