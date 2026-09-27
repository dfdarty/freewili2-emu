# Accuracy and known differences

The emulator is the place to get an app's logic, UI and data flow right. The
board is where timing, radios and analog behaviour get verified. This page
says what is modelled closely, what is approximate, and what is missing, so
you know which results to trust.

## How the models work

WiliBSP's drivers run unchanged. They reach the hardware through the Pico
SDK's C API, which the emulator implements on the host, and the emulator
attaches a model to each bus, register block or wire protocol:

| Real part | Model | What is emulated |
|---|---|---|
| ST7796 LCD on SPI1 | `dev_lcd.c` | command/data decode (CASET, RASET, RAMWR, SLPOUT, DISPON…), RGB565, backlight GPIO; DISPLAY power zone |
| FT6336U touch on I2C1 | `dev_touch.c` | register file, point latches, chip-to-screen orientation; taps are held until the app has read them |
| WS2812 × 16 on pio1 | `dev_leds.c` | GRB words from the PIO state machine, latch timing; RGB_LEDS power zone |
| PCAL6524 IO expander | `dev_ioexp.c` | output ports and pin directions: VREF select, antenna switch, mic/IR/USB power |
| MAIN CPU on UART0 (FwGUI link) | `dev_main.c` | the 8 Mbaud link byte for byte: OneWili console commands and replies, the SDFS SD-card protocol over a host folder, header GPIO, programmable Vout, `EPOWERZONE` ([details](main-link.md)) |
| Display ADC (VIO / Vout monitors) | `sdk_periph.c`, `dev_main.c` | 12-bit conversions of the header rails through the 2:1 dividers, at levels measured on hardware |
| Board-manager PIC on UART1 | `dev_pic.c` | the 62 500-baud link byte for byte: 23-byte status frames (14 buttons, charger, rails); break → `0xC9` → 11-byte power command; ~1 s rail walk |
| SHT40, OPT4001, BMI323, BMM350 on I2C1 | `dev_sensors.c` | each part's command/register protocol, CRCs, ranges and encodings; SENSORS power zone |
| NAU88C10 codec + I2S on pio0 | `dev_audio.c` | register file with reset defaults; DAC mute/volume, speaker vs. jack routing and gain; ADC input. I2S runs at `clk_sys / clkdiv / 128` through DREQ-paced DMA |
| 4 × PDM microphones on pio1 | `dev_pdm.c` | shared 1.024 MHz clock, 2 data lines × 2 clock phases, bit-packed like WiliBSP's `pdm_capture` program; one sigma-delta modulator per mic; free-running ring DMA; off unless MIC_PWR is on |
| SEGGER RTT | `rtt.c` | `DIAG()` to stdout; TCP on :9090 / :9091 for WiliBSP's tools |
| PSRAM | `sdk_periph.c` | 8 MB at the real address, `0x11000000` |

## Close to the hardware

- **Power zones.** Parts are dead until the app's `POWER_ZONES` are switched
  on through the real handshake. A missing zone fails here as it does on the
  board.
- **Button and touch timing.** Presses and releases arrive as real status
  frames and touch registers, with the board-manager's own framing.
- **Audio and microphone sample rates** follow the app's clock and PIO
  divider settings.
- **Sensor ranges.** Readings saturate at the range the driver configured.
- **Memory map.** PSRAM is at its real address, so pointer arithmetic and
  `fw2_psram_app()` images behave as on the chip.
- **The MAIN link.** The OneWili client runs unmodified against a MAIN CPU
  that speaks its wire protocol, with 8 Mbaud pacing, flow control and
  MAIN's 2 KB receive ring. A too-large SD write fails here as it does on
  the board ([why](main-link.md#link-timing)).

## Approximate

- **Timing.** SPI and I2C transfers and memory-to-memory DMA complete
  instantly, so an app that is too slow on the RP2350 can look fine here.
  PIO audio streams (I2S, PDM) and the two UART links (the board manager at
  62 500 baud, the MAIN CPU at 8 Mbaud) are paced at their real rates.
- **CPU speed.** Your PC runs the app much faster than a 250 MHz Cortex-M33,
  and `sleep_ms()` waits in real time. Heavy computation, drawing and
  decoding will be slower on the board.
- **Acoustics.** All microphones hear the same sound with no delay between
  capsules; per-mic gains are the only spatial effect.
- **Sensors** return what you set, to the part's resolution, plus optional
  noise. There is no motion model or temperature drift.
- **LED output** shows the colour data the app sends; the real LEDs'
  brightness and first-frame latch quirk are not modelled.
- **The MAIN CPU's** response times and SD-card busy periods are estimates.
  The MAIN firmware isn't public, so a few replies are inferred;
  [the MAIN link page](main-link.md#wire-protocol) marks which. The SD card
  is a folder on your PC, so FAT's limits don't apply.

## Not modelled yet

- Most of what the MAIN processor does beyond the SD card, header GPIO and
  Vout: CAN, analog inputs, the UART/I2C/SPI bridges, the FPGA and logic
  analyzer. Those OneWili commands return a failure instead of hanging.
- The radios: the CC1101 sub-GHz radio, LoRa, Wi-Fi/Bluetooth, NFC/RFID and
  infrared.
- The second core (`pico/multicore.h`), USB host, and DVI output (stubbed).

See [App compatibility](compatibility.md) for how this maps onto WiliBSP's
example apps.

## What the emulator can't tell you, and what can

| Question | Use |
|---|---|
| Does it fit in SRAM / PSRAM? Is the stack big enough? | [`fw2emu hwcheck`](debugging.md#real-hardware-check-toolsfw2emu-hwcheck) — builds with the real toolchain |
| Does it corrupt memory? | the [sanitizer build](debugging.md#sanitizers-fw2_emu_sanitize) |
| Does it assume 64-bit pointers? | the [32-bit build](debugging.md#32-bit-build-fw2_emu_32bit) |
| Is it fast enough? Does the radio work? Does it survive a real battery? | the board |

## The emulator runs source, not UF2 files

Apps are recompiled for your PC. A `.uf2` downloaded from somewhere else
can't run here; that would need a full RP2350 instruction-set emulator. It
also means compiler differences (x86 or WebAssembly vs. Arm) can hide or
expose bugs — `hwcheck` builds the same source with the real Arm compiler.
