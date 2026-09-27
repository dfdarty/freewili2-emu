# The MAIN processor link

A FREE-WILi 2 has two RP2350s. WiliBSP apps run on the **display** CPU. The
**MAIN** CPU owns the SD card, the user GPIO header and the programmable
Vout. An app reaches the MAIN CPU through WiliBSP's OneWili client
(`libs/onewili`), over the FwGUI display link: UART0 at 8 Mbaud with
hardware flow control.

The MAIN firmware isn't public. The emulator provides a MAIN CPU that
answers the unmodified OneWili client **on the wire**, byte for byte
(`emu/src/dev_main.c`). The client library isn't changed or replaced, so
`ow_sd_*`, `ow_io_gpio_*` and the rest behave as they do on the board, up to
the limits below. `hello_sdcard`, `toggleled` and `hello_vref` run
unmodified.

## What's modelled

| Area | What works | OneWili calls |
|---|---|---|
| SD card | files and folders, backed by a folder on your PC | every `ow_sd_*` call: open/read/write/seek/close (two handles), whole-file get/put, stat, list, mkdir, remove, rename |
| Header GPIO | pins 8–17 and 25–27: drive, toggle, PWM, read back, stream | `ow_io_gpio_set_io_high/low/toggle`, `set_pwm`, `read_all`, `stream_io` (gpioReport binary events) |
| Header supply (VIO) | the rail chosen with `ioexp_vref()` on the display side, as the display's ADC monitor reads it | `ioexp_vref()`, `adc_read()` on inputs 5 (VIO) and 1 (Vout) |
| Programmable Vout | on/off and 1.0–5.5 V; the ANALOG power zone must be on | `ow_io_analog_out_set_v_prog_vout` |
| Power-zone refusals | `EPOWERZONE` for GPIO/analog commands, once the app has reported its zones | `ow_fwgui_send_power_zones` |
| Anything else | a well-formed failure reply: the call returns `OW_ERR_FAILED` at once instead of timing out after 5 s | the other ~480 generated commands |

The front panel shows the header: one square per pin (green = driven high,
outlined = driven low, blue = driven from outside, purple = PWM), the VIO
rail, Vout when it's on, and an **SD** light while the card is in use.

![toggleled driving header GPIO 25 high, with VIO at 3.3 V](main-link.png)

## The SD card folder

```sh
build/bin/hello_sdcard                      # card = ./sdcard
build/bin/hello_sdcard --sdcard ~/fw2card   # any folder
build/bin/hello_sdcard --sdcard none        # no card in the slot
```

- The folder is created the first time an app touches the card, with a
  `README.TXT` and a `samples/` folder (`hello.txt`, `readings.csv`). A
  folder that already exists is used as it is.
- SD paths map onto it: `/owlog/run.txt` is `sdcard/owlog/run.txt`. Names
  are matched without regard to case, as FAT does. Paths with `..` or with
  characters FAT forbids (`" * : < > ? | \`) are rejected
  (`SDFS_ERR_BAD_REQUEST`).
- The card only answers while the app's **SD Card** power zone (`SDCARD` in
  `POWER_ZONES`) is on. Otherwise every call fails with
  `SDFS_ERR_NO_CARD`, as it does for `--sdcard none`.
- Directory listings come back sorted by name. FAT returns directory order,
  which the host can't reproduce.
- In the browser the card is kept in memory: it starts with the sample
  files on every page load and disappears when you close the page.

`tests/smoke.sh` gives each app a fresh card in `out/sdcard-<app>`.

## Header GPIO and VIO

The header is level-shifted. The shifters are powered by **VIO**, which the
display CPU selects with `ioexp_vref()` (WiliBSP invariant 11). Without VIO,
MAIN still toggles its pin and `ow_io_gpio_read_all()` still reports the new
level, but the header pin doesn't move. The emulator reproduces this: the
panel shows "VIO OFF: PINS DEAD" and the pin squares go dim.

VIO and Vout values are the ones measured on a bench unit (WiliBSP's
`docs/superpowers/findings/2026-07-26-gpio-vref-e2e.md`):

| `ioexp_vref()` | VIO | ADC input 5 reads |
|---|---|---|
| `VREF_NONE` | 0 V | ~25 mV |
| `VREF_3V3` | 3.27 V | ~3290 mV |
| `VREF_5V0` | 4.83 V | ~4850 mV |
| `VREF_EXT_PIN` | whatever is on Trig_IN/VREF; 4.80 V with nothing connected, as measured | ~4825 mV |
| `VREF_PROG_VOUT` | the programmable Vout (0 V while it's off) | ~25 mV |

`read_all` returns the full 32-bit `gpio_get_all()` value. Bits that aren't
header pins, and header pins nobody drives, read as they did on that bench
unit (`0xE0FF21F3` with GPIO 25 low). Drive a pin from outside to test an
input:

```sh
build/bin/my_app --sensor gpio12=1          # at start-up; gpio12=-1 releases it
```

```
set gpio12 1        # in a script: 1 high, 0 low, -1 not driven
set vrefext 3.3     # volts on the Trig_IN/VREF pin (-1 = nothing connected)
header              # log every pin, VIO and Vout
```

The web page has the same controls under **GPIO header inputs**.

## Link timing

- **MAIN → display.** Bytes leave at 8 Mbaud and stop while the display's
  32-byte receive FIFO is full, which is the display's RTS. MAIN works
  through one request at a time and waits while its reply is still going
  out.
- **Display → MAIN.** MAIN receives into a 2048-byte DMA ring. The ring
  never pauses the sender, and when it's full the oldest bytes are lost.
- **Card busy periods.** The card goes busy for 6 ms after every 16 KB
  written, and MAIN stops reading the link while it waits.

Together these reproduce a real hardware failure. The OneWili client that
WiliBSP ships sends a whole `ow_sd_write()` as one burst. If the card goes
busy for more than about 2.5 ms during a burst, bytes arrive faster than
MAIN takes them, the ring overruns, and chunks are lost. `ow_sd_close()` then
fails with `SDFS_ERR_IO`, or the call times out if the final chunk was lost.
In the emulator the card goes busy after every 16 KB written, so a single
write that spans one of those points fails. On the board it depends on the
card. The upstream client in
github.com/freewili/onewili batches writes to fix this.
With the version WiliBSP ships, **write in pieces of 1 KB or less**, as
`tests/apps/main_link_check` does. The emulator logs
`main: receive ring overrun` when this happens.

## Checking it

- `-v` logs every OneWili response (`main: [i\g\t 00000001F681D880 3 Ok 1]`),
  GPIO changes, VREF and Vout. `-vv` also logs each SD request.
- `tests/apps/main_link_check` is a self-test built next to the example
  apps. It runs 55 checks through the unmodified client: every SD operation
  and its errors, GPIO, PWM, streamed reports, Vout, `EPOWERZONE` and an
  unmodelled command.
- `fw2emu hwcheck` builds the app for the board. Note that every OneWili
  text command (`ow_io_gpio_*`, `ow_io_analog_*`, …) keeps about 10 KB of
  buffers on the stack. That is more than both 4 KB scratch banks, so
  `hwcheck` reports `toggleled` and `hello_vref` as overflowing, as a
  [known upstream issue](compatibility.md#known-upstream-issues). The SD
  calls don't have this problem. This comes from the generated client, not
  from the emulator; your own app that sends text commands gets the same
  error.

## Limits

- **Not modelled:** CAN, analog inputs, the UART/I2C/SPI/MDIO bridges, the
  FPGA and logic analyzer, radios, NFC, WILEye, the MAIN-side GUI, and MAIN
  rebooting on its own. These commands return the failure reply described
  above. The emulator names each one in its log the first time an app uses
  it.
- `i\g\v` (`ow_io_gpio_set_io_voltage_source`) answers `Ok` but changes
  nothing. On the board, MAIN passes this command to the stock display
  firmware, which isn't running while a WiliBSP app is. Use `ioexp_vref()`
  instead.
- MAIN's processing times are approximate: about 0.15 ms per command,
  1.5 ms per SD metadata operation and 0.1 ms per 96-byte data chunk.
- The SD card has the host filesystem's limits, not FAT's. There's no
  4 GB file limit and no 8.3 names, and free space is the host disk's.

## Wire protocol

This section is for anyone extending the model. **Inferred** marks behaviour
that isn't documented anywhere public and was chosen to be consistent with
the client code.

| Direction | Frame |
|---|---|
| display → MAIN | `B0 1D`, length (u16 LE, **excluding** the event byte), event, payload, checksum (u16 LE sum of every preceding byte) |
| MAIN → display | `BE BA`, length (u16 LE), command, payload, checksum (u16 LE sum of sync + length + command + payload) |

| Event / command | Carries |
|---|---|
| event 24 `M_TERM_INPUT` | console text, `01` marker + count + up to 56 bytes |
| event 42 `SDFS_REQUEST` | one SDFS frame |
| event 48 `POWER_ZONES` | 24-bit zone mask |
| command `0x5D` | console output: responses and `[*…]` text events |
| command `0x5E` | binary WILI event frames |
| command `0x5F` | one SDFS frame |

**Console.** A command is `0x02` (back to the menu root, quiet mode), a menu
path and its arguments, then a newline, e.g. `\x02i\g\t 25\n`. The reply is
one line, `[<path> <16 hex digits: ns> <sequence> <body> <1|0>]`, where the
last digit is the success flag.

- The body of a successful set command is `Ok`, as in the upstream capture.
- `read_all` prints `%04X` of the whole bitfield, which gives eight digits
  when high pins are set, as in the WiliBSP bench log.
- Failure bodies are **inferred**: `Invalid pin N`,
  `Not supported by the FREE-WILi 2 emulator`. `EPOWERZONE <zones> <names>`
  is documented.
- The console output is sent in frames of 256 bytes or less (**inferred**;
  the client drops frames over 512).
- MAIN doesn't reply to an empty line, including the one `ow_open()` sends
  (**inferred**; a reply there would be taken as the next command's
  response).

**SDFS.** Frames have an 11-byte header (opcode, request id, flags, seq,
total, path length, payload length), with at most 128 bytes of path and 96
of payload, the values fw2main is built with. The replies and their
payloads are defined by what the client parses. These server choices are
**inferred**:

- A whole-file write sends no reply of its own; `STATUS_REQ` reports its
  result afterwards. An unknown request id reads `SDFS_PENDING`.
- A read of an empty file sends one empty chunk flagged LAST.
- Handle reads end with a chunk shorter than asked, flagged LAST.
- Of each handle-write batch, only the chunk flagged LAST is acknowledged.
  `HCLOSE` compares the byte count it's given with what was written.
- `HCLOSE` of a handle that isn't open answers `SDFS_ERR_BAD_REQUEST`. The
  client does this for both handles when it starts.
- FatFs results map to SDFS statuses as follows. A missing file or parent
  gives `NOT_FOUND`. An existing target, a folder that isn't empty, or
  writing to a read handle gives `IO`. A third open handle gives `BUSY`.
- `STAT` of a missing path answers `STAT_RESP` with `NOT_FOUND`, not `NACK`.
- `LIST` entries (`flags, size u32, name length, name`) never span two
  chunks. Names longer than 90 bytes are cut to fit.
- Streamed `gpioReport`: WILI header type 0, repeat count 1, 12-byte payload
  (u64 ns timestamp, u32 bitfield).
