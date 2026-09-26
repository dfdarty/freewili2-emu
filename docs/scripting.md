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
| `screenshot FILE [lcd\|device]` | PNG of the 480x320 LCD, or the whole front panel |
| `log TEXT` | print a marker |
| `quit` | end the run |

Buttons: `GREY YELLOW GREEN BLUE RED CENTER UP DOWN LEFT RIGHT HOME OK CANCEL PAGE`.

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
