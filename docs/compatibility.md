# App compatibility

WiliBSP ships 17 example apps. They are the best measure of how much of the
board the emulator covers: each one below either runs unmodified, or is
blocked by a part that isn't modelled yet.

**12 of 17 run.** Each running app has a headless test in `tests/scripts/`
that CI runs on every change, and CI also checks it against the real chip's
limits with [`fw2emu hwcheck`](debugging.md#real-hardware-check-toolsfw2emu-hwcheck).

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
| `retrochat` | ❌ | needs `pico/multicore.h` (a second core) and `pico/unique_id.h` |
| `hello_ir` | ❌ | needs the infrared transmitter and receiver |
| `hello_cc1101` | ❌ | needs the CC1101 sub-GHz radio |
| `hello_usbdrive` | ❌ | needs USB host and a USB drive |
| `hello_dvi` | ❌ | needs HSTX DVI output (stubbed) |

¹ The emulator runs it; building it for the board with WiliBSP on Linux or
macOS currently fails WiliBSP's own layout check, which `hwcheck` reports.
That is an upstream build-script issue, not an emulator one.

² Runs here. For the board, `hwcheck` reports a stack of about 10.9 KB,
more than the RP2350's two 4 KB scratch banks. Every generated OneWili text
command keeps its buffers on the stack. That is an upstream client issue;
see [the MAIN link page](main-link.md#checking-it).

## Planned order

1. **Second core** — `retrochat`.
2. **Infrared** — `hello_ir`.
3. **CC1101**, then **USB host** and **DVI**.
4. **More of the MAIN CPU** over OneWili (CAN, analog inputs, the bus
   bridges): the link is in place, and each one is a new set of commands in
   `emu/src/dev_main.c`.

## Your own apps

An app that only uses modelled parts builds and runs. If it uses a driver
for a part that isn't modelled, the build fails — a missing header or an
undefined function names what's missing — rather than running and
misbehaving silently.
