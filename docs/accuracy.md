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
| MAIN CPU on UART0 (FwGUI link) | `dev_main.c` | the 8 Mbaud link byte for byte: OneWili console commands and replies, the SDFS SD-card protocol over a host folder, header GPIO, programmable Vout, the board clock, `EPOWERZONE` ([details](main-link.md)) |
| Display ADC (VIO / Vout monitors) | `sdk_periph.c`, `dev_main.c` | 12-bit conversions of the header rails through the 2:1 dividers, at levels measured on hardware |
| Board-manager PIC on UART1 | `dev_pic.c` | the 62 500-baud link byte for byte: 23-byte status frames (14 buttons, charger, rails); break → `0xC9` → 11-byte power command; ~1 s rail walk |
| SHT40, OPT4001, BMI323, BMM350 on I2C1 | `dev_sensors.c` | each part's command/register protocol, CRCs, ranges and encodings; SENSORS power zone |
| NAU88C10 codec + I2S on pio0 | `dev_audio.c` | register file with reset defaults; DAC mute/volume, speaker vs. jack routing and gain; ADC input. I2S runs at `clk_sys / clkdiv / 128` through DREQ-paced DMA |
| 4 × PDM microphones on pio1 | `dev_pdm.c` | shared 1.024 MHz clock, 2 data lines × 2 clock phases, bit-packed like WiliBSP's `pdm_capture` program; one sigma-delta modulator per mic; free-running ring DMA; off unless MIC_PWR is on |
| SEGGER RTT | `rtt.c` | `DIAG()` to stdout; TCP on :9090 / :9091 for WiliBSP's tools |
| PSRAM | `sdk_periph.c` | 8 MB at the real address, `0x11000000` |
| Core 1 and the SIO | `multicore.c`, `sdk_sync.c` | `pico/multicore.h` (launch, reset, 4-deep FIFOs, lockout, doorbells), per-core interrupt enables and masking, spin locks, mutexes, semaphores, queues ([details](#the-second-core)) |

## Close to the hardware

- **Power zones.** Parts are dead until the app's `POWER_ZONES` are switched
  on through the real handshake. A missing zone fails here as it does on the
  board.
- **Bus speed.** SPI runs at the rate the PL022's dividers produce (WiliBSP's
  LCD: 62.5 MHz, 39.3 ms a full screen), I2C at 9 clocks a byte, so an app
  that draws more than the bus can carry is slow here too.
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

- **Timing.** SPI and I2C transfers take their wire time at the clock the
  app configured, and DMA to SPI completes when its last byte would have
  been sent ([details](debugging.md#is-it-fast-enough-bus-timing)).
  Per-byte gaps and the CPU time spent feeding a blocking transfer are not
  counted, so the board can be slightly slower. Memory-to-memory DMA
  completes at once. PIO audio streams (I2S, PDM) and the two UART links
  (the board manager at 62 500 baud, the MAIN CPU at 8 Mbaud) are paced at
  their real rates.
- **CPU speed.** App code is slowed to the RP2350's speed at the app's
  clock, from a CoreMark measurement of your PC at start-up
  ([details](debugging.md#is-it-fast-enough-cpu-speed)). It is an estimate,
  good to within about a factor of two: PSRAM cache misses and
  double-precision maths aren't counted, so heavy code of either kind is
  slower on the board.
- **Acoustics.** All microphones hear the same sound with no delay between
  capsules; per-mic gains are the only spatial effect.
- **Sensors** return what you set or [play from a log](sensors-and-sound.md#playing-a-sensor-log),
  to the part's resolution, plus optional noise. There is no physics: a
  log is played as recorded, and nothing drifts on its own.
- **LED output** shows the colour data the app sends; the real LEDs'
  brightness and first-frame latch quirk are not modelled.
- **The MAIN CPU's** response times and SD-card busy periods are estimates.
  The MAIN firmware isn't public, so a few replies are inferred;
  [the MAIN link page](main-link.md#wire-protocol) marks which. The SD card
  is a folder on your PC, so FAT's limits don't apply.

## Not modelled yet

- Most of what the MAIN processor does beyond the SD card, header GPIO,
  Vout, the board clock and peer streams: CAN, analog inputs, the
  UART/I2C/SPI bridges, the FPGA and logic analyzer. Those OneWili commands
  return a failure instead of hanging.
- The radios, except stand-ins for the ESP32-C5: its stock Wi-Fi and
  Bluetooth scans report a [scene of virtual networks](main-link.md#wi-fi-and-bluetooth-scans),
  and a [stand-in ESP32](main-link.md#peer-streams-esp32-cm0-pc) answers
  combined apps. The CC1101 sub-GHz radio, LoRa, NFC/RFID and infrared
  aren't modelled.
- USB host and DVI output (stubbed).

## The second core

`multicore_launch_core1()` starts core 1 for real, and the SDK's inter-core
API behaves as on the chip: the 4-deep FIFOs in each direction, the FIFO and
doorbell interrupts, `multicore_lockout_*`, spin locks, `mutex_t`,
`semaphore_t`, `critical_section_t` and `pico/util/queue.h`. Interrupts are
per core: a handler runs on the core that enabled the line, and
`save_and_disable_interrupts()` holds them pending on that core until it
restores them. SDK alarms run on core 0, where the default alarm pool lives.

The two cores take turns on one host thread. A core hands over whenever it
waits — `sleep_ms()`, `tight_loop_contents()`, or any blocking FIFO, lock,
mutex, semaphore or queue call — and after running for 1 ms, at its next
SDK call (reading the time counts). When both wait, the PC sleeps. This has
two consequences:

- **A loop that spins on a plain variable with no SDK call in it never lets
  the other core run**, so it hangs here even though it works on the board:

    ```c
    while (!core1_ready) { }                         // hangs in the emulator
    while (!core1_ready) tight_loop_contents();      // works in both
    ```

    The second form is what the SDK's own examples do, and costs nothing on
    the board.
- **Races that need both cores in the same few instructions don't show up.**
  Races that need a core to be interrupted at an SDK call do. Protect shared
  data as you would on the board (a spin lock, mutex or queue), and test
  timing-sensitive sharing on hardware.

A core that waits for a non-recursive mutex it already holds hangs, as on
the board, and the log says so once. `pico_get_unique_board_id()` returns
`E6616408432A7B15` unless the run gives `--board-id`, so two emulators can
tell each other apart.

See [App compatibility](compatibility.md) for how this maps onto WiliBSP's
example apps.

## What the emulator can't tell you, and what can

| Question | Use |
|---|---|
| Does it fit in SRAM / PSRAM? Is the stack big enough? | [`fw2emu hwcheck`](debugging.md#real-hardware-check-toolsfw2emu-hwcheck) — builds with the real toolchain |
| Does it corrupt memory? | the [sanitizer build](debugging.md#sanitizers-fw2_emu_sanitize) |
| Does it assume 64-bit pointers? | the [32-bit build](debugging.md#32-bit-build-fw2_emu_32bit) |
| Can the SPI or I2C bus keep up? | the [bus readout](debugging.md#is-it-fast-enough-bus-timing) |
| Is the CPU fast enough? | the [CPU readout](debugging.md#is-it-fast-enough-cpu-speed) for an estimate; the board to be sure |
| Does the radio work? Does it survive a real battery? | the board |

## The emulator runs source, not UF2 files

Apps are recompiled for your PC. A `.uf2` downloaded from somewhere else
can't run here; that would need a full RP2350 instruction-set emulator. It
also means compiler differences (x86 or WebAssembly vs. Arm) can hide or
expose bugs — `hwcheck` builds the same source with the real Arm compiler.
