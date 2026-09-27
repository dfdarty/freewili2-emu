# Troubleshooting

## Building

**`third_party/wilibsp` is empty / "does not contain a CMakeLists.txt".**
The WiliBSP submodule, or its own nested `libs/onewili`, wasn't fetched:

```sh
git submodule update --init --recursive --depth 1
```

**"WiliBSP's nested libs/onewili submodule is missing".** The same fix: the
OneWili client (the MAIN-CPU link) is a submodule inside WiliBSP.

**CMake is too old.** The project needs CMake 3.20 or newer.

**`SDL2` not found.** Install `libsdl2-dev`, or configure with
`-DFW2_EMU_SDL=OFF` for a headless-only build.

**My app fails to build with a missing header or an undefined function.**
It uses a driver for a part the emulator doesn't model yet — see
[App compatibility](compatibility.md).

**"skipping a hardware-image build step on the host".** Your app's
`CMakeLists.txt` adds a `POST_BUILD` step that inspects the Arm ELF or UF2
(like `hello_psram_exec`'s layout check). It can't apply to a host program,
so the emulator build skips it; `fw2emu hwcheck` runs it against the real
image.

## Running

**The screen stays black.**

- The app may still be starting: `board_init()` and the power-zone
  handshake take about a second, and some apps wait longer. Run with `-v` to
  see the power commands.
- Check the app's `POWER_ZONES` includes `DISPLAY`, and that it turns the
  backlight on (`board_backlight_set(1)`).

**Colours are wrong (red shows as blue-ish, green as magenta…).** The LCD
driver takes RGB565 colours in wire byte order. Pass `0x00F8` for red, not
`0xF800`, or swap with a helper — see [Your first app](first-app.md). The
board shows the same wrong colours.

**A sensor reads zero or "not found".** Its power zone isn't on — sensors
need `SENSORS` in `POWER_ZONES`.

**The microphones read flat / pure DC.** The mic power switch (IO expander
port 1, bit 7) is off. WiliBSP's `pdm_capture_init()` turns it on.

**No sound in the browser.** Browsers block audio until you interact with the
page: click the device once.

**Keys don't work in the browser.** Click the device so it has keyboard focus.

**`--mic-wav` fails.** It needs an uncompressed PCM WAV, 8 or 16 bit.

**`fw.py` can't connect.** Start the app with `--rtt`, and run `fw.py` on the
same machine: the ports listen on 127.0.0.1 only. `fw rtt` doesn't work (it
starts OpenOCD); read the emulator's output instead.

## `fw2emu hwcheck`

**"arm-none-eabi-gcc not found".** Install `gcc-arm-none-eabi` and
`libnewlib-arm-none-eabi`, or use `--fetch-toolchain` for the exact Arm GNU
Toolchain 14.2.Rel1 that WiliBSP uses.

**The first run is slow.** It fetches the Pico SDK and builds picotool once
(about 2 minutes); later runs reuse `~/.cache/fw2emu`.

**Numbers differ slightly from someone else's.** Compiler versions change
code size. Use `--fetch-toolchain` for WiliBSP's exact compiler.

## Still stuck?

[Open an issue](https://github.com/dfdarty/freewili2-emu/issues) with the
command you ran and its output (add `-v`).
