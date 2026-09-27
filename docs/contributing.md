# Reporting bugs and contributing

## Reporting bugs

This is an **unofficial** project, not affiliated with FREE-WILi LLC.

- If an app behaves differently in the emulator than on a real FREE-WILi 2,
  or the emulator itself misbehaves, please
  [open an issue here](https://github.com/dfdarty/freewili2-emu/issues).
- Only report a problem to FREE-WILi or WiliBSP once you have reproduced it
  on real hardware.

A good report has the command you ran, the output with `-v`, and — if you can —
a small [input script](scripting.md) that shows the problem, since that
reproduces it exactly. The issue form asks for these.

What changed in each release is in the
[changelog](https://github.com/dfdarty/freewili2-emu/blob/main/CHANGELOG.md).

## How the code is organised

```
emu/sdk/include/    host implementation of the Pico SDK's C API (pico/*, hardware/*)
emu/include/        emulator-internal headers, PIO program stand-ins, RTT header
emu/src/            core loop, SDK peripherals, device models (dev_*.c), skin, scripting
cmake/              host fw2_display_app() — same arguments and checks as WiliBSP's
hwcheck/            CMake project for real-hardware builds (used by fw2emu hwcheck)
tools/              fw2emu helper, stack analysis
third_party/wilibsp WiliBSP, pinned submodule (never modified here)
web/                browser page
tests/              headless scripts, log expectations, smoke test;
                    tests/apps/ holds self-test apps (e.g. main_link_check)
docs/               this site
```

## Ground rules

- **WiliBSP compiles unmodified.** Never patch `third_party/wilibsp`. If
  WiliBSP has a bug, it gets reported upstream; the emulator models the
  hardware, not a fixed version of the driver.
- **Model the hardware, not the driver.** A device model answers on the bus
  or wire protocol the real part uses — registers, commands, frames — so any
  correct driver works against it, not just WiliBSP's.
- **Fail like the hardware.** If a part is unpowered or misconfigured, the
  model should behave as the real part would (no ACK, no data, DC output), not
  helpfully work anyway.
- **Note it in the changelog.** Add a line under *Unreleased* in
  `CHANGELOG.md` for anything a user would notice.
- **Keep the tests green.** `tests/smoke.sh` must pass natively; CI also
  runs the sanitizer, 32-bit, hwcheck and web builds.

## Adding a device model

1. Read the WiliBSP driver and the part's datasheet, and note which bus,
   address, power zone and registers the driver uses.
2. Add `emu/src/dev_<part>.c`. Attach it with `emu_i2c_attach()`,
   `emu_spi_attach()`, `emu_uart_attach()` or, for a PIO program,
   `emu_pio_model_register()` plus a stand-in header in `emu/include/pio/`
   (see `dev_pdm.c` and `pdm_capture.pio.h`). Gate it on its power zone with
   `emu_rail_on()`.
3. Add the model and the WiliBSP driver sources to `CMakeLists.txt`, and
   initialise the model in `emu/src/core.c`.
4. If people need to control it, add `set` names (see `emu_sensor_set()`),
   and a panel on `web/index.html` if it helps.
5. Add the WiliBSP example app that uses the part to `FW2_EMU_UPSTREAM_APPS`,
   with `tests/scripts/<app>.txt` and a `.expect` file that checks something
   meaningful in its log.
6. Update [App compatibility](compatibility.md) and [Accuracy](accuracy.md).

## Writing docs

This site is built with [MkDocs Material](https://squidfunk.github.io/mkdocs-material/)
from the `docs/` folder:

```sh
pip install -r requirements-docs.txt
mkdocs serve                # live preview on http://127.0.0.1:8000
mkdocs build --strict       # as the Pages workflow does: fails on broken links
```

Every page has an edit link at the top.

## License

MIT. Contributions are accepted under the same license. WiliBSP is MIT,
© 2026 Dave Robins; see `THIRD-PARTY-NOTICES.md` for everything else.
