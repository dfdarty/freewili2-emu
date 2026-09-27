# Input scripts

`--script FILE` feeds inputs on a timeline. It works with or without a
window; with `--headless` it is the way to test apps from CI or an agent.
One command per line. `#` starts a comment, at the start of a line or
after a command (`press OK   # confirm`); a `#` inside double quotes, as in
`expect "#3"`, is kept. A mistake in the script — an unknown command, a
word where a number should be, too many arguments — stops the run with
`FATAL: script line N: …` and exit status 2.

| Command | Effect |
|---|---|
| `wait MS` | pause the script |
| `press BTN [MS]` | press and release a button (default 150 ms) |
| `hold BTN` / `release BTN` | hold or release a button |
| `touch X Y [MS]` | tap the screen at LCD coordinates (default 120 ms) |
| `drag X1 Y1 X2 Y2 [MS]` | swipe (default 300 ms) |
| `set NAME V [V V V]` | change a sensor or the room sound (table below) |
| `play FILE [loop] [step]` / `play stop` | play a time-stamped sensor log (CSV, or `@launch`) from now; see [Playing a sensor log](sensors-and-sound.md#playing-a-sensor-log) |
| `screenshot FILE [lcd\|device]` | PNG of the 480x320 LCD, or the whole front panel |
| `log TEXT` | print a marker |
| `header` | log the MAIN CPU's GPIO header: each pin's level, VIO and Vout ([MAIN link](main-link.md#header-gpio-and-vio)) |
| `expect REGEX [MS]` | wait for a log line matching REGEX (default 2000 ms); if none comes, the run fails ([below](#pass-or-fail-expect)) |
| `quit` | end the run (exit status 0) |

Buttons: `GREY YELLOW GREEN BLUE RED CENTER UP DOWN LEFT RIGHT HOME OK CANCEL PAGE`.

A run with a script ends the process when the script says `quit` (or when
`--run-ms` runs out), with or without a window.

`set` names (the same names work as `--sensor NAME=V,V` and from the web page):

| Name | Values | Meaning |
|---|---|---|
| `temp` | °C | SHT40 temperature |
| `rh` (or `humidity`) | % | SHT40 relative humidity |
| `lux` | lux | OPT4001 light |
| `accel` | x y z (g) | BMI323 acceleration |
| `gyro` | x y z (°/s) | BMI323 rotation rate |
| `mag` | x y z (µT) | BMM350 field |
| `tilt` | pitch roll (°) | sets `accel` for a device tilted in 1 g |
| `noise` | 0 / 1 | sensor noise off / on |
| `tone` | Hz [level] | a sine in the room, level in 16-bit units (default 8000); `tone 0` stops it |
| `miclevel` | gain | scale for the `--mic-wav` source |
| `mics` | A B C D | per-mic pickup gain, 0–1 (physical order left to right is D B A C) |
| `mic.A` … `mic.D` | gain | one mic's pickup gain |
| `gpio8` … `gpio17`, `gpio25` … `gpio27` | 1 / 0 / -1 | drive a MAIN-CPU header pin high or low from outside; -1 stops driving it |
| `vrefext` | V | volts on the Trig_IN/VREF pin (used by `VREF_EXT_PIN`); -1 = nothing connected |

The codec ADC and all four PDM mics hear the same room: the `--mic-wav`
source, the tone, and some of whatever the speaker is playing.

Buttons go through the emulated board-manager coprocessor, the same wire
protocol as hardware, so `uartkbd_next_event()` sees normal press/release
edges. Touches go through the FT6336 register model.

Example:

```
wait 3000            # let board_init + the power-zone handshake finish
press OK
touch 240 160
screenshot out/after.png device
hold HOME
wait 5600            # WiliBSP recovery: HOME held 5 s exits the app
```

## Pass or fail: `expect`

```text
press OK
expect "count=1"                 # default timeout 2000 ms
expect "temp=2[0-9]{3} centi" 5000
```

`expect` pauses the script until a line of output matches the regular
expression, a POSIX extended regex (the dialect of `grep -E`). Put it in
double quotes if it contains spaces; `\"` is a quote inside it.

- **Which lines.** Every `DIAG()` line and every emulator log line, without
  the `[diag]` / `[emu]` prefix. That includes the script's own lines, so
  `header` followed by `expect "25=1\(out\)"` checks a pin.
- **From where.** `expect` looks at lines after the one the previous
  `expect` matched, including lines printed before the script got to it. So
  `press OK` followed by `expect "count=1"` works even if the app logged the
  count during the press.
- **On a timeout** the emulator logs `FAIL script line N: expect /REGEX/ …`
  and stops at once with exit status 1, which fails `tests/smoke.sh` and CI.
  A `--shot-on-exit` screenshot is still taken.

It works the same natively, in the sanitizer and 32-bit builds, and in
WebAssembly under Node.
