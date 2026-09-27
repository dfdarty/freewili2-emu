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
| ++f2++ | Screenshot of the whole panel: saved as `fw2emu-<ms>.png` in the current folder, or downloaded in the browser |

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

The browser version adds a guide beside the device and a row of panels under it (one to four columns, depending on the window width):

- **App picker** and **Restart** at the top. `?app=NAME` in the URL picks an
  app; `?args=...` passes [command-line flags](cli.md), for example
  `?app=hello_audio&args=-v`.
- **About this app:** for each WiliBSP example, what it tests, what you're
  seeing, and things to try. The ▶ buttons do them for you (tilt the device,
  play a tone into one microphone, type with the chord keyboard, …) by moving
  the same sliders and pressing the same keys you would. It also says which
  log line confirms the result. Panels the app uses are outlined, tagged
  "used by this app" and listed first in the panels under the device. Your own apps get a
  short general guide.
- **Sensors:** temperature, humidity, light, pitch, roll and compass heading,
  plus sensor noise. **Rocket launch** plays the built-in flight, and **Play a
  CSV…** plays your own [sensor log](sensors-and-sound.md#playing-a-sensor-log).
- **Sound in the room:** a test tone and each microphone's pickup, in their
  physical order (D, B, A, C from left to right).
- **GPIO header inputs:** drive the MAIN processor's header pins from
  outside, and choose what is wired to Trig_IN/VREF.
- **RTT diagnostics:** the app's `DIAG()` output.

Each panel carries a one-line hint about what it's for; **hide hints** in the
guide turns them off (the page remembers the choice).

Click the device before typing, so it has keyboard focus.

## Headless

Without a window, drive the app with an [input script](scripting.md), or over
RTT with [WiliBSP's `fw.py`](wilibsp-tools.md).
