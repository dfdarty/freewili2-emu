# Third-party notices

- **WiliBSP** — `third_party/wilibsp` (git submodule), MIT, © 2026 Dave Robins.
  Its own third-party components keep their licenses; see its THIRD-PARTY-NOTICES.md.
  `emu/src/dev_pic.c` reproduces protocol constants (frame layout, rail
  positions) from WiliBSP's `bsp/input/uartkbd_parse.c` and `bsp/input/picpwr_frame.c`.
- **SDL 2** — zlib license. Linked from the system (native) or built by
  Emscripten's SDL2 port (web).
- **Emscripten** — MIT / University of Illinois NCSA (web build toolchain; its
  runtime JavaScript is included in the web output).
- **nginx** — 2-clause BSD (serves the web build in the container).

FREE-WILi is a trademark of FREE-WILi LLC. This project is not affiliated with
FREE-WILi LLC.
