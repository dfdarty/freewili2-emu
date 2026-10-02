# Changelog

What changed in each release of freewili2-emu. The GitHub Action follows
releases: `uses: dfdarty/freewili2-emu@v2` picks up every 2.x release, and a
change that would break a v2 workflow gets a new major version. `@v1` stays
at 1.2.0.

## [Unreleased]

## [2.3.0] — 2026-10-02

### Changed
- **The browser page runs app code at full speed.** Its chip-speed estimate
  runs 2-3x pessimistic for drawing-heavy code (WebAssembly's speeds differ
  from native code's relative to CoreMark): SquachWatch drew 2-3 frames a
  second there against an estimated 8-10 on the board. `?args=--cpu chip`
  still gives the estimate. Native builds, `fw2emu test` and the GitHub
  Action keep the chip's speed.

## [2.2.1] — 2026-10-01

### Added
- The hosted demo includes Orca-9: Pod Commander, a 3D space shooter built
  on WiliBSP, with its guide.

### Fixed
- **A quick tap reached a slow app as two.** The touch model held a quick
  tap until the app had read the chip twice. An app polling once a frame at
  2-3 frames a second (as in the browser) saw it in two frames, and one that
  acts on every frame the screen is pressed took it for two taps:
  SquachWatch's menu opened and closed again. A tap is now held for one read.
  `script_check` checks it.
- **A failing script step is reported at its line in the file.** Blank lines
  and comments weren't counted, so `FAIL script line N`, `fw2emu test`'s
  `file:line: error` and the GitHub annotations pointed at the wrong line in
  any script with comments. CI now checks this.
- `-v` logs every dropped peer-stream datagram with its reason, as the docs
  said; before, only drops for an absent peer were logged.
- The web page's guides for `dualcpu` and `canblast`: they showed the generic
  text for your own apps.
- Docs: corrected throughout after a line-by-line review against the code
  (what isn't modelled, the build packages, option lists, task names, links).

## [2.2.0] — 2026-09-30

### Added
- **An app's page in the browser: `fw2emu-web.json`.** In the app folder,
  it sets the options the page starts the app with, the **About this app**
  guide (with ▶ buttons that send input-script commands) and a source link.
  It works with `fw2emu web`, Docker's `EXTRA_APPS` and
  `FW2_EMU_EXTRA_APPS`. See
  [the docs](https://dfdarty.github.io/freewili2-emu/cli/#the-apps-page-fw2emu-webjson).
- The hosted demo includes SquachWatch for the FREE-WILi 2, with its guide.

### Fixed
- The web page could not send `radio` commands (nor `stream` and `peer`):
  its command filter didn't list them.

## [2.1.0] — 2026-09-29

### Changed
- WiliBSP updated to be4bdd6 (OneWili b0eeccd, with peer streams). OneWili's
  display-CPU package now lives in `libs/onewili/wilibsp/`. The emulator
  builds its FwGUI transport in OneWili's host mode (no UART interrupt), with
  the board's clock for peer streams (`emu/src/onewili_fwgui_emu.c`).
- `fw2emu hwcheck` lists `canblast` and `dualcpu` with the known OneWili
  stack issue.

### Added
- **OneWili peer streams.** MAIN routes datagrams between the display app
  and other OneWili clients as `ow_stream_wire.h` specifies: HELLO and
  CREDIT, the credit window, keepalive and link expiry, drop counting, and
  Wireless > ESP32 Mode (`w\e`). `--peer esp32=dualcpu` stands in for the
  ESP32 half of WiliBSP's `dualcpu` (PING/PONG, telemetry with a Wi-Fi scan
  of the radio scene, LED, SCAN_NOW), so `dualcpu` runs; `--peer
  esp32|cm0|host=script` puts an input script at the other end (`stream`
  and `peer` commands). The web page starts `dualcpu` with its stand-in.
  New self-test `stream_check`. See
  [Peer streams](https://dfdarty.github.io/freewili2-emu/main-link/#peer-streams-esp32-cm0-pc).
- CI builds and smoke-tests the emulator on 64-bit Arm Linux too, and runs
  `hwcheck` there with the aarch64 Arm toolchain: a Raspberry Pi 5 works as
  the development machine.
- **The UF2 you install on the board, from hwcheck and the Action.**
  `fw2emu hwcheck` shows each passing app's UF2 and `--uf2-dir DIR` copies
  them out; with `hwcheck: true` the Action puts it in the `out` artifact
  and gives its path as the `uf2` output, for attaching to a release
  ([example](https://dfdarty.github.io/freewili2-emu/ci/#examples)).

## [2.0.0] — 2026-09-28

### Breaking
- App code runs at the RP2350's speed, in tests too (below). An app that is
  too slow for the board is now too slow in its test, and a script whose
  fixed `wait`s were tuned at PC speed may need longer ones — better, an
  `expect` on the output it waits for. To upgrade, change `@v1` to `@v2`;
  `args: --cpu host` keeps the old speed where a test needs it.

### Added
- App code runs at the RP2350's speed. Each core's app code (interrupt
  handlers and timer callbacks included) is timed between SDK calls and
  slowed to what the Cortex-M33 would take at the app's clock, from a
  CoreMark measurement of the PC at start-up; the buses, DMA and the other
  core carry on meanwhile. It is an estimate: PSRAM cache misses and
  double-precision maths aren't counted. `--cpu host` runs app code at the
  PC's full speed as before; `--cpu-factor F` sets the ratio.
  `tests/apps/cpu_check` checks it on core 0, core 1, in a timer callback
  and on both cores at once.
- The device's bottom edge and `--perf` show each core's load (`CPU0 45%`),
  and a run ends with each core's average and peak.

- `tests/smoke.sh` passes extra emulator flags to one app from
  `tests/scripts/<app>.args` (or `apps/<app>/test.args`).

### Changed
- Tests run at the chip's speed too, so an app too slow for the board is too
  slow in its test. A script tuned at PC speed may need longer `wait`s, or
  `--cpu host`. Sanitizer builds run app code at full speed unless given
  `--cpu chip`: their checks would skew the estimate.
- The smoke scripts for hello_charger, hello_keyboard, hello_mics,
  hello_psram_exec, hello_vref and toggleled wait for the app's output
  instead of a fixed time.

### Fixed
- A `fw2_psram_app()` app now starts as on the board: WiliBSP's PSRAM
  start-up runs `board_init_psram()` (250 MHz, PSRAM timing, peripherals)
  before `main()`. hello_psram_exec, which never calls `board_init()`, ran
  at the SDK's 150 MHz before.

## [1.2.0] — 2026-09-28

### Added
- Wi-Fi and Bluetooth scans: `ow_wireless_wifi_on_scan_for_access_points()`
  and `ow_wireless_bluetooth_le_on_scan_bt_devices()` report a scene of
  virtual networks and devices as `wifiscan` / `btscan` events. Scenes come
  from `--radio FILE`, the built-in `--radio @town`, or the script `radio`
  command.
- Apps written in C++ build and run: the emulator's build enables C++, and
  an app's `main()` links whichever language it is in. `tests/apps/cpp_check`
  checks it. (WiliBSP's headers have no `extern "C"` guards, on the board
  too, so a C++ app wraps its WiliBSP includes in `extern "C" { … }`.)
- `pico/rand.h`: `get_rand_32()`, `get_rand_64()` and `get_rand_128()`, from
  the PC's random source.
- Docker: `EXTRA_APPS` repositories are cloned with their submodules.

### Changed
- `fw2emu hwcheck` no longer warns that malloc may grow into the stack when
  the app defines its own `_sbrk` (a heap in PSRAM, say); it notes it
  instead.

## [1.1.0] — 2026-09-27

### Added
- The board clock: `ow_hardware_get_time()` and `ow_hardware_set_time()`
  work, starting at the PC's local time or at `--rtc "YYYY-MM-DD HH:MM:SS"`.
- Docker: the `EXTRA_APPS` build argument puts apps from other Git
  repositories on the web page, next to WiliBSP's examples.

## [1.0.0] — 2026-09-27

The first release.

### The emulator
- WiliBSP apps compile unmodified against a host implementation of the Pico
  SDK's C API, with each part modelled on its real bus or wire protocol:
  ST7796 LCD, FT6336U touch, 16 WS2812 LEDs, the PCAL6524 IO expander, the
  board-manager PIC link (14 buttons, charger, power zones), SHT40, OPT4001,
  BMI323 and BMM350 sensors, the NAU88C10 codec with I2S, four PDM
  microphones, SEGGER RTT, and 8 MB of PSRAM at its real address.
- The MAIN processor over the OneWili link at 8 Mbaud: SD card (a folder on
  your PC), header GPIO, VIO and programmable Vout.
- Both RP2350 cores: `pico/multicore.h`, the FIFOs, per-core interrupts and
  masking, spin locks, mutexes, semaphores, queues, lockout, doorbells and
  `pico/unique_id.h`.
- SPI and I2C at their real clock rates (the LCD bus: 62.5 MHz, 39.3 ms a
  full screen), with a live bus-load readout.
- Sensor-log playback from CSV, and a built-in model-rocket flight.
- 13 of WiliBSP's 17 example apps run, including `retrochat` on both cores.

### Running and testing
- A window, headless runs, and the browser (Emscripten), with a guide for
  each example app on the web page.
- Input scripts with `expect`, for pass/fail tests; `tools/fw2emu run`,
  `web`, `test` and `wait-rtt`.
- WiliBSP's `fw.py press / touch / type / screenshot` over RTT.
- `tools/fw2emu hwcheck`: the same app built with the real Pico SDK 2.3.0 and
  Arm GCC 14.2.Rel1, reporting image, RAM, PSRAM and worst-case stack
  against the chip's limits.
- Sanitizer and 32-bit builds.
- A GitHub Action for app repositories.

### Tools for app developers
- Record a session as an input script: `--record FILE`, or **● Record** in
  the browser. The app's log lines after each step come along as
  `# expect` hints.
- `tools/fw2emu new DIR [--repo]` and `examples/app-template`: a new app with
  its test (and, with `--repo`, a README and a workflow that tests every
  push).
- A weekly workflow that moves WiliBSP to its latest commit, runs every test
  and the hardware check, and opens a pull request with the results.
- Issue and pull-request templates, and this changelog.
- Windows (WSL2) and VS Code: a setup page, and `.vscode` tasks (run, test,
  record, hwcheck, new app), gdb debug configurations and IntelliSense
  (`compile_commands.json` in every build).
- `fw2emu run --build-only`; `bin/current-app` points at the last app built.
- `fw2emu test` prints failures as `file:line: error:` for editors.
- Ctrl+C ends a run cleanly: a recording or `--audio-out` file is still
  written.

[Unreleased]: https://github.com/dfdarty/freewili2-emu/compare/v2.3.0...HEAD
[2.3.0]: https://github.com/dfdarty/freewili2-emu/compare/v2.2.1...v2.3.0
[2.2.1]: https://github.com/dfdarty/freewili2-emu/compare/v2.2.0...v2.2.1
[2.2.0]: https://github.com/dfdarty/freewili2-emu/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/dfdarty/freewili2-emu/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/dfdarty/freewili2-emu/compare/v1.2.0...v2.0.0
[1.2.0]: https://github.com/dfdarty/freewili2-emu/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/dfdarty/freewili2-emu/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dfdarty/freewili2-emu/releases/tag/v1.0.0
