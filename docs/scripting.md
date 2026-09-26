# Input scripts

`--script FILE` feeds inputs on a timeline. It works with or without a
window; with `--headless` it is the way to test apps from CI or an agent.
One command per line; `#` starts a comment.

| Command | Effect |
|---|---|
| `wait MS` | pause the script |
| `press BTN [MS]` | press and release a button (default 150 ms) |
| `hold BTN` / `release BTN` | hold or release a button |
| `touch X Y [MS]` | tap the screen at LCD coordinates (default 120 ms) |
| `drag X1 Y1 X2 Y2 [MS]` | swipe (default 300 ms) |
| `set NAME V [V V V]` | change a sensor or the room sound (table below) |
| `screenshot FILE [lcd\|device]` | PNG of the 480x320 LCD, or the whole front panel |
| `log TEXT` | print a marker |
| `quit` | end the run |

Buttons: `GREY YELLOW GREEN BLUE RED CENTER UP DOWN LEFT RIGHT HOME OK CANCEL PAGE`.

`set` names (the same names work as `--sensor NAME=V,V` and from the web page):

| Name | Values | Meaning |
|---|---|---|
| `temp` | °C | SHT40 temperature |
| `rh` | % | SHT40 relative humidity |
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
