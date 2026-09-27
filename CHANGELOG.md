# Changelog

What changed in each release of freewili2-emu. The GitHub Action follows
releases: `uses: dfdarty/freewili2-emu@v1` picks up every 1.x release, and a
change that would break a v1 workflow gets a new major version.

## [Unreleased]

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

[Unreleased]: https://github.com/dfdarty/freewili2-emu/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/dfdarty/freewili2-emu/releases/tag/v1.0.0
