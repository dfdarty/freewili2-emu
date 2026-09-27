# Controls

The FREE-WILi 2 has 14 buttons and a touch screen. In the emulator window
(and on the browser page) you can use the keyboard or click the drawn panel.

## Keyboard

| Key | Button |
|---|---|
| ++arrow-up++ ++arrow-down++ ++arrow-left++ ++arrow-right++ | D-pad |
| ++enter++, ++space++ | D-pad centre |
| ++h++ | HOME |
| ++o++ | OK |
| ++c++, ++backspace++ | CANCEL |
| ++p++ | PAGE |
| ++1++ ++2++ ++3++ ++4++ ++5++ | Grey, yellow, green, blue, red (the keys under the screen) |
| ++f2++ | Save a screenshot of the whole panel (`fw2emu-<ms>.png` in the current folder) |

Keys are held for as long as you hold them, so long-press gestures work:
**hold ++h++ for 5 seconds** to exit an app (WiliBSP's recovery rule), and
hold ++p++ for 5 seconds for an app's About screen.

## Mouse

- **On the screen:** a touch. Click to tap, drag to swipe.
- **On a drawn button:** presses it.

Every press, release and tap goes through the same path as on hardware:
buttons through the board-manager chip's serial link, touches through the
touch controller's registers. A quick click is held until the app has read
it, so it is never lost between two polls.

## Browser page

The browser version adds panels beside the device:

- **App picker** and **Restart** at the top. `?app=NAME` in the URL picks an
  app; `?args=...` passes [command-line flags](cli.md), for example
  `?app=hello_audio&args=-v`.
- **Sensors:** temperature, humidity, light, pitch, roll and compass heading,
  plus sensor noise.
- **Sound in the room:** a test tone and each microphone's pickup, in their
  physical order (D, B, A, C from left to right).
- **RTT diagnostics:** the app's `DIAG()` output.

Click the device before typing, so it has keyboard focus.

## Headless

Without a window, drive the app with an [input script](scripting.md), or over
RTT with [WiliBSP's `fw.py`](wilibsp-tools.md).
