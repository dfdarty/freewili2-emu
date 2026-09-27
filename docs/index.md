# freewili2-emu

**Run [FREE-WILi 2](https://freewili.com) apps before the hardware is in hand.**

freewili2-emu is an emulator for the FREE-WILi 2's DISPLAY processor — the
RP2350B where custom UF2 apps run. Apps written with
[WiliBSP](https://github.com/freewili/wilibsp), FREE-WILi's board support
package, compile **unmodified** against it and run:

- in a **desktop window** on Linux,
- **headless**, for scripted tests, CI and AI agents,
- in a **browser**, as WebAssembly.

[Try it in your browser](https://dfdarty.github.io/freewili2-emu/emulator/){ .md-button .md-button--primary }
[Install it](getting-started.md){ .md-button }

The browser demo runs WiliBSP's example apps. To run and test **your own**
app, build it with the emulator — see [Your first app](first-app.md).

![hello_display running in the emulator](hello_display.png)

!!! note "Unofficial"
    This is a community project, not affiliated with FREE-WILi LLC. If
    something behaves differently in the emulator than on a real board,
    [report it here](contributing.md#reporting-bugs), not to FREE-WILi.

## What you can do with it

- **Write apps now.** Use the same `CMakeLists.txt`, the same
  `fw2_display_app()` call and the same WiliBSP drivers you'd use on the
  device. The folder that builds in the emulator also builds a UF2 for the
  board.
- **Test them automatically.** Script button presses, touches and sensor
  changes, take screenshots, and check the app's log — headless, in CI.
- **Use WiliBSP's own tooling.** `fw.py press`, `touch` and `screenshot`
  work against the emulator over RTT, so WiliBSP's `AGENTS.md` workflow and
  its Claude Code skills run without a board.
- **Check that it will fit.** [`fw2emu hwcheck`](debugging.md#real-hardware-check-toolsfw2emu-hwcheck)
  builds your app with the real Pico SDK and Arm GCC and reports SRAM,
  PSRAM and worst-case stack against the chip's limits.

## What's modelled

The display, touch screen, all 14 buttons, the 16 RGB LEDs, the four
on-board sensors (temperature/humidity, light, IMU, magnetometer), the audio
codec with speaker and headphone jack, the four PDM microphones, the power
zones and charger status from the board-manager chip, 8 MB of PSRAM, and
the link to the MAIN processor with its SD card, header GPIO and Vout.
Radios are not modelled yet — see [App compatibility](compatibility.md) for exactly which WiliBSP apps
run, and [Accuracy](accuracy.md) for how closely each part follows the real
hardware.

## How it works

WiliBSP's drivers talk to the RP2350 through the Raspberry Pi Pico SDK. The
emulator replaces the Pico SDK with a host implementation of the same C API
and attaches **device models** that answer on the same buses, registers and
wire protocols as the real chips. `board_init()`, the power-zone handshake,
the LCD driver, the chord keyboard, "hold HOME 5 s to exit" — all run as
shipped. Your app's `main()` is renamed at compile time so the emulator can
start first, and every SDK wait or spin calls back into the emulator, so apps
keep their own `for (;;)` loop.

The emulator runs your app's **source code**, rebuilt for your PC. It cannot
run a compiled `.uf2` downloaded from somewhere else.
