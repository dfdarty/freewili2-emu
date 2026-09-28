# Third-party notices

- **WiliBSP** — `third_party/wilibsp` (git submodule), MIT, © 2026 Dave Robins.
  Its own third-party components keep their licenses; see its THIRD-PARTY-NOTICES.md.
  Files in this repository that reproduce parts of WiliBSP:
  - `emu/src/dev_pic.c` — protocol constants (frame layout, rail positions)
    from `bsp/input/uartkbd_parse.c` and `bsp/input/picpwr_frame.c`.
  - `emu/include/pio/ws2812.pio.h`, `i2s_duplex.pio.h`, `pdm_capture.pio.h` —
    stand-ins for the headers pioasm generates from `bsp/leds/ws2812.pio`,
    `bsp/audio/i2s_duplex.pio` and `bsp/pdm/pdm_capture.pio`; the
    `*_program_init()` helpers follow the originals' `c-sdk` sections.
  - `cmake/fw2_emu_app.cmake` — mirrors the arguments and validation of
    `fw2_display_app()` / `fw2_psram_app()` in `bsp/CMakeLists.txt`.
- **CoreMark** — `third_party/coremark`, Apache License 2.0, © EEMBC. An
  unmodified copy of the benchmark's workload files (list, matrix, state,
  CRC) from https://github.com/eembc/coremark at 1f483d5. The emulator runs
  them for a moment at start-up to measure how fast the PC is, and slows app
  code to the RP2350's speed from that (`emu/src/cpu.c`, which has its own
  port header and driver in `emu/src/cpu/`). No CoreMark score is reported.
  CoreMark is a trademark of EEMBC.
- **Raspberry Pi Pico SDK** — BSD 3-Clause, © Raspberry Pi Ltd. The headers in
  `emu/sdk/include` are an independent host implementation of the Pico SDK's
  C API (function names, types and register-structure layouts) so WiliBSP
  compiles against it; no Pico SDK source is included.
- **SDL 2** — zlib license. Linked from the system (native) or built by
  Emscripten's SDL2 port (web).
- **Emscripten** — MIT / University of Illinois NCSA (web build toolchain; its
  runtime JavaScript is included in the web output).
- **nginx** — 2-clause BSD (serves the web build in the container).

FREE-WILi is a trademark of FREE-WILi LLC. This project is not affiliated with
FREE-WILi LLC.
