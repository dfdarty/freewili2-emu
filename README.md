# freewili2-emu

Run [FREE-WILi 2](https://freewili.com) apps before the hardware is in hand.

WiliBSP apps and drivers compile **unmodified** against an emulated FREE-WILi 2
display CPU. The result runs in three places:

- **a desktop window** on Linux
- **headless**, for scripted tests and AI agents
- **in a browser**, as WebAssembly, from a single static container

![hello_display running in the emulator](docs/hello_display.png)

## How it works

WiliBSP's drivers talk to the RP2350 through the Pico SDK. This project swaps
the Pico SDK for a host implementation (`emu/sdk/include`, `emu/src/sdk_periph.c`)
and attaches **device models** that answer on the same buses, registers and
wire protocols the real parts use:

| Real part | Model | What is emulated |
|---|---|---|
| ST7796 LCD on SPI1 | `dev_lcd.c` | Command/data decode (CASET, RASET, RAMWR, SLPOUT, DISPON…), RGB565, backlight GPIO, power zone 2 |
| FT6336U touch on I2C1 | `dev_touch.c` | Register file, point latches, chip-to-screen orientation. Taps are held until the app has read them |
| WS2812 × 16 on pio1 | `dev_leds.c` | GRB words from the state machine, latch timing, power zone 10 |
| PCAL6524 IO expander | `dev_ioexp.c` | Output ports and direction: VREF select, antenna switch, mic/IR/USB power |
| SHT40, OPT4001, BMI323, BMM350 on I2C1 | `dev_sensors.c` | Command/register protocols the Bosch and TI drivers expect; values set from the command line, a script or the web page (temperature, humidity, lux, tilt, heading); power zone 1 |
| NAU88C10 codec on I2C1 + I2S on pio0 | `dev_audio.c` | Register file with reset defaults; DAC mute/volume, speaker vs. jack routing and gain; ADC hears the room. The I2S program model runs at `clk_sys / clkdiv / 128` (16 009 Hz at 250 MHz) through DREQ-paced DMA, and plays through the PC or into a WAV |
| 4 × PDM MEMS mics on pio1 | `dev_pdm.c` | Shared 1.024 MHz clock, two data lines × two clock phases, bit-packed exactly like the `pdm_capture` program; one sigma-delta modulator per mic; free-running ring DMA. Silent unless MIC_PWR (IO expander P1.7) is on |
| Board-manager PIC on UART1 | `dev_pic.c` | The 62500-baud link, byte for byte. 23-byte status frames (14 buttons, charger, rails); break → `0xC9` → 11-byte power command; ~1 s rail walk |
| SEGGER RTT | `rtt.c` | DIAG to stdout; optional TCP on :9090 / :9091, the same ports `fw rtt` uses |
| PSRAM | `sdk_periph.c` | 8 MB mapped at the real address, `0x11000000` |

So `board_init()`, `fw2_app_recovery_init()`, `picpwr`, `uartkbd`, `st7796`,
`ft6336` and `ws2812` all run as shipped. That includes the power-zone
handshake and **HOME held 5 s → exit**.

The app's `main()` is renamed at compile time, and every SDK wait or spin calls
back into the emulator. Apps keep their own `for (;;)` loop.

### WiliBSP's own agent tooling works against it

Run an app with `--rtt`. WiliBSP's `tools/fw.py` then drives it exactly as it
drives hardware through openocd:

```sh
build/bin/hello_agentio --rtt &
cd third_party/wilibsp
python3 tools/fw.py press ok
python3 tools/fw.py touch 240 100
python3 tools/fw.py screenshot -o shot.png   # reads the app's own agentio framebuffer shadow
```

This means WiliBSP's `AGENTS.md` workflow and its Claude Code skills can build,
press, and screenshot-verify apps with no device attached.

## Build

### Native (Linux)

```sh
git clone --recurse-submodules https://github.com/dfdarty/freewili2-emu
cd freewili2-emu
sudo apt install cmake ninja-build libsdl2-dev python3
cmake -S . -B build -G Ninja && cmake --build build
build/bin/hello_display            # window
tests/smoke.sh                     # every app headless, screenshots in out/
```

### Browser

```sh
docker build -t freewili2-emu .
docker run --rm -p 127.0.0.1:8080:80 freewili2-emu      # open http://127.0.0.1:8080
```

The Dockerfile fetches WiliBSP and SDL at pinned versions if they are missing,
so it also builds straight from the Git URL. To build without Docker you need
Emscripten: `emcmake cmake -S . -B build-web && cmake --build build-web`, then
serve `build-web/bin/`.

## Controls

| Input | Button |
|---|---|
| Arrows, Enter | D-pad |
| `H` `O` `C` `P` | HOME, OK, CANCEL, PAGE |
| `1`–`5` | Grey, yellow, green, blue, red |
| Mouse on the screen | Touch (click or drag) |
| Mouse on the panel | Press a key |
| `F2` | Save a full-device screenshot |

## Command line

```
--headless          no window
--script FILE       scripted input (see docs/scripting.md)
--run-ms N          stop after N ms
--shot-on-exit PNG  save the LCD when the app exits
--rtt               serve RTT on 127.0.0.1:9090 (DIAG) and :9091 (agentio)
--rails HEX         power zones already on at launch (default 0x8183)
--scale N           window scale
--sensor NAME=V     set a sensor or the room sound: temp=24 lux=320 tilt=30,0
                    tone=1000,8000 mics=1,1,0,1 (see docs/scripting.md)
--audio-out WAV     record everything the codec plays
--mic-wav WAV       sound reaching the microphones (looped)
--mute              don't play audio through the PC
-v, -vv             log model activity (power commands, touches, IO expander, audio)
```

In the browser, pass the same flags as `?app=hello_display&args=-v`.

## Your own apps

Put an app in `apps/<name>/` with the same `CMakeLists.txt` you would use with
WiliBSP (`fw2_display_app(...)`). It builds for the emulator as-is. See
[apps/README.md](apps/README.md).

## What is not emulated (yet)

These features are absent from the emulator entirely:
- **DVI:** stubbed.
- **Radios:** CC1101, LoRa, NFC, IR.
- **USB host.**
- **The MAIN CPU / OneWili link.**

These parts are modelled only approximately:
- **Timing:** there is no bus timing. SPI and memory DMA complete instantly;
  PIO streams (I2S, PDM) are paced by their DREQs at the real sample rates.
- **Acoustics:** every mic hears the same scene (a WAV, a test tone, speaker
  bleed, a noise floor) scaled by a per-mic gain. There are no propagation
  delays between capsules, so beamforming can be exercised but not measured.
- **Sensors:** values are what you set, plus optional noise; there is no motion model.

Treat the emulator as the place to get logic, UI and data flow right. Then
verify drivers, timing and anything radio-related on real hardware.

## Layout

```
emu/sdk/include/    Pico SDK host shim (pico/*, hardware/*)
emu/include/        emulator-internal headers, PIO program models, RTT header
emu/src/            core loop, SDK peripherals, device models, skin, scripting
cmake/              host fw2_display_app() — same arguments and checks as WiliBSP
third_party/wilibsp WiliBSP, pinned submodule (MIT)
web/                browser page
tests/              headless scripts + smoke test
```

## License

MIT (see LICENSE). WiliBSP is MIT, © 2026 Dave Robins. Bundled and fetched
third-party components keep their own licenses (see THIRD-PARTY-NOTICES.md).
