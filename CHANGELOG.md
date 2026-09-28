# Changelog

What changed in each release of freewili2-emu. The GitHub Action follows
releases: `uses: dfdarty/freewili2-emu@v1` picks up every 1.x release, and a
change that would break a v1 workflow gets a new major version.

## [Unreleased]

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

[Unreleased]: https://github.com/dfdarty/freewili2-emu/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/dfdarty/freewili2-emu/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/dfdarty/freewili2-emu/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dfdarty/freewili2-emu/releases/tag/v1.0.0
