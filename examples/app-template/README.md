# my_app

A [FREE-WILi 2](https://freewili.com) app built on WiliBSP, with a test that
runs in the [FREE-WILi 2 emulator](https://dfdarty.github.io/freewili2-emu/)
on every push.

| File | |
|---|---|
| `CMakeLists.txt`, `main.c` | the app, exactly as WiliBSP expects it |
| `test.txt` | its test: an [input script](https://dfdarty.github.io/freewili2-emu/scripting/) whose `expect` lines must pass |
| `.github/workflows/fw2emu.yml` | runs the test and the real-chip check on every push |

## Make it yours

1. Rename the app: replace `my_app` in `CMakeLists.txt` (all three places),
   `main.c` and `test.txt`, and change the `DESCRIPTION`.
2. Pick the `POWER_ZONES` your app needs (DISPLAY, RGB_LEDS, SENSORS, AUDIO, …).
3. Write the app in `main.c`, and what it should do in `test.txt`.

## Run it on your PC

Once, get the emulator (Linux, WSL2 on Windows, or a Codespace):

```sh
git clone --recurse-submodules https://github.com/dfdarty/freewili2-emu ~/freewili2-emu
```

Then, from this folder:

```sh
~/freewili2-emu/tools/fw2emu run .              # in a window
~/freewili2-emu/tools/fw2emu test .             # run test.txt: PASS or FAIL
~/freewili2-emu/tools/fw2emu run . --record new-test.txt   # click around; get a script back
~/freewili2-emu/tools/fw2emu hwcheck --fetch-toolchain .   # fits the chip? also builds the UF2
```

Keys: arrows and Enter for the D-pad, H O C P for HOME/OK/CANCEL/PAGE, 1–5 for
the colour buttons. See the [emulator docs](https://dfdarty.github.io/freewili2-emu/)
for everything else.

## On the board

`fw2emu hwcheck` also builds the real UF2:
`~/freewili2-emu/build-hw/apps/<this folder's name>/my_app.uf2`.
Inside a WiliBSP checkout, put this folder in `apps/` and use WiliBSP's own
`fw` tool to build and flash it.

The emulator is unofficial and not affiliated with FREE-WILi LLC; report
emulator problems [to the emulator](https://github.com/dfdarty/freewili2-emu/issues).
