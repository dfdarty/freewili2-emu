# App compatibility

WiliBSP ships 19 example apps. They are the best measure of how much of the
board the emulator covers: each one below either runs unmodified, or is
blocked by a part that isn't modelled yet.

**14 of 19 run.** On every push, CI runs each of them headless with its
script in `tests/scripts/` (natively, with sanitizers and as a 32-bit build),
and builds all of them for the board with
[`fw2emu hwcheck`](debugging.md#real-hardware-check-toolsfw2emu-hwcheck)
using the Arm GNU Toolchain 14.2.Rel1. hwcheck fails CI on any error except
the known upstream issues listed [below](#known-upstream-issues), which it
reports without failing. The web build is compiled in CI but not run
there.

| App | Status | What it exercises / what's missing |
|---|---|---|
| `template` | ✅ runs | LCD, recovery (HOME 5 s) |
| `hello_display` | ✅ runs | LCD, touch, 16 RGB LEDs |
| `hello_agentio` | ✅ runs | agentio injection and capture, RTT, PSRAM |
| `hello_keyboard` | ✅ runs | all 14 buttons, two-press chord keyboard |
| `hello_charger` | ✅ runs | charger and USB-C status from the board-manager chip |
| `hello_sensors` | ✅ runs | SHT40, OPT4001, BMI323, BMM350 |
| `hello_audio` | ✅ runs | NAU88C10 codec, I2S, speaker/jack routing, mic input |
| `hello_mics` | ✅ runs | 4 PDM microphones, CIC decimation |
| `hello_psram_exec` | ✅ runs | an app executing from PSRAM (`fw2_psram_app()`), power-zone cycling ¹ |
| `hello_sdcard` | ✅ runs | SD card on the MAIN CPU over OneWili: mkdir, appends, stat, read, list ([MAIN link](main-link.md)) |
| `toggleled` | ✅ runs | header GPIO 25 toggled over OneWili, VIO select ² |
| `hello_vref` | ✅ runs | VIO select measured on the display ADC, GPIO read-back over OneWili ² |
| `retrochat` | ✅ runs | the [second core](accuracy.md#the-second-core) (its modem decoder), speaker → air → microphones, `pico/unique_id.h`; the acoustic self-test decodes its own "HI" |
| `dualcpu` | ✅ runs | the display half of a DISPLAY + ESP32 app over OneWili [peer streams](main-link.md#peer-streams-esp32-cm0-pc), against the emulator's stand-in for its ESP32 half (`--peer esp32=dualcpu`): PING/PONG, telemetry, LED, Wi-Fi scan ² |
| `canblast` | ❌ | needs MAIN's CAN FD controller (`h\s\p\*`, `i\c\*`) and its `canRxReport` events; building it for the board also hits a [known upstream issue](#known-upstream-issues) |
| `hello_ir` | ❌ | needs the infrared transmitter and receiver |
| `hello_cc1101` | ❌ | needs the CC1101 sub-GHz radio |
| `hello_usbdrive` | ❌ | needs USB host and a USB drive |
| `hello_dvi` | ❌ | fails to link: HSTX DVI output is stubbed, and the DVI on-screen-display functions (`dvi_osd_*`) aren't provided |

¹ ² Runs here; building it for the board hits a
[known upstream issue](#known-upstream-issues).

## Known upstream issues

These come from WiliBSP or its libraries, not from the app or the emulator.
`fw2emu hwcheck` knows them for WiliBSP's own example apps only: it reports
them as `KNOWN (upstream)` with the reason, and they don't change its exit
status (`--strict` makes them count). The same failure in your own app is an
ordinary error. The list is in `tools/fw2emu` (`KNOWN_UPSTREAM`); if one of
them stops happening, hwcheck warns so the list can be updated.

| App | Failure | Reason |
|---|---|---|
| `hello_psram_exec` | post-link layout check | WiliBSP's `bsp/app/psram_link` selects the SDK objects for the SRAM bootstrap as `*.c.obj`, the Windows object naming. With `*.c.o` (Linux, macOS) the clock and QMI code lands in PSRAM, and the app's own `verify_layout.py` rejects the image. |
| `toggleled`, `hello_vref`, `canblast`, `dualcpu` | stack (~10.9–11.8 KB) | Every generated OneWili text command keeps its buffers on the stack: 5 KB in the call plus 5 KB in `ow__call`. In `dualcpu` it's the fallback path of `ow_stream_drops` (asking MAIN with `h\a\c`, for links without pushed credits), which the analysis can't rule out. That is more than the RP2350's two 4 KB scratch banks together. The SD calls don't have this problem; see [the MAIN link page](main-link.md#checking-it). |

## Planned order

1. **The radios** — infrared (`hello_ir`), then the CC1101 (`hello_cc1101`).
   This is next.
2. **USB host** and **DVI**.
3. **More of the MAIN CPU** over OneWili (CAN, analog inputs, the bus
   bridges): the link is in place, and each one is a new set of commands in
   `emu/src/dev_main.c`.

## Your own apps

An app that only uses modelled parts builds and runs. If it uses a driver
for a part that isn't modelled, the build fails — a missing header or an
undefined function names what's missing — rather than running and
misbehaving silently.
