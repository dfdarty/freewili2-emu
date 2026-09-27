# Your first app

An emulator app **is** a WiliBSP app: the same folder, the same
`CMakeLists.txt` and the same `main.c` build for the emulator and, with
WiliBSP's own toolchain, into a UF2 for the board.

## 1. Make the folder

Put it in `apps/` inside the repository (every folder there is built
automatically), or anywhere else and use `tools/fw2emu run`.

```sh
mkdir -p apps/my_app
```

`apps/my_app/CMakeLists.txt`:

```cmake
add_executable(my_app main.c)
target_link_libraries(my_app freewili2_bsp)
fw2_display_app(my_app
    POWER_ZONES DISPLAY RGB_LEDS
    VERSION 001
    DESCRIPTION "Counts OK presses and colours the LEDs")
```

`fw2_display_app()` checks the same things WiliBSP's does: a three-digit
version, a description, and valid power-zone names. `POWER_ZONES` lists the
rails your app needs; WiliBSP's start-up code asks the board-manager chip to
switch them on, and the emulator models that handshake, so a part whose zone
you forget stays dark here too.

## 2. Write `main.c`

```c
#include "fw2.h"
#include "hardware/pio.h"
#include "platform/diag.h"
#include <stdio.h>

// The LCD driver takes RGB565 colours in wire (big-endian) byte order.
static inline uint16_t be16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

int main(void) {
    board_init();
    fw2_app_recovery_init();          // HOME held 5 s exits; also starts the button link
    st7796_init();
    fw2_app_about_use_lcd();
    st7796_fill_screen(be16(0x0000));
    board_backlight_set(1);

    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(64);

    unsigned count = 0;
    rgb_t colour = { .r = 0, .g = 0, .b = 255 };
    bool redraw = true;

    for (;;) {
        fw2_app_recovery_task();

        uartkbd_event_t ev;
        while (uartkbd_next_event(&ev)) {
            if (!ev.pressed) continue;
            if (ev.btn == UARTKBD_BTN_OK) { count++; redraw = true; }
            if (ev.btn == UARTKBD_BTN_GREEN) { colour = (rgb_t){ .r = 0, .g = 255, .b = 0 }; redraw = true; }
            if (ev.btn == UARTKBD_BTN_RED)   { colour = (rgb_t){ .r = 255, .g = 0, .b = 0 }; redraw = true; }
        }

        if (redraw) {
            char line[32];
            snprintf(line, sizeof line, "OK PRESSED %u TIMES", count);
            st7796_fill_rect(0, 140, 480, 40, be16(0x0000));
            st7796_draw_text(40, 150, 3, be16(0xFFE0), be16(0x0000), line);
            ws2812_fill(colour);
            ws2812_show();
            DIAG("count=%u\n", count);
            redraw = false;
        }
        tight_loop_contents();
    }
}
```

A few WiliBSP conventions this follows:

- Call `fw2_app_recovery_task()` every loop — it services the button link
  and makes **HOME held for 5 s** exit the app.
- Clear the screen before turning the backlight on.
- `DIAG()` goes to RTT, which the emulator prints to the terminal. It has no
  float formatting, as on the board.

## 3. Run it

```sh
tools/fw2emu run apps/my_app
```

Press ++enter++ (the D-pad centre), ++o++ for OK, ++3++ for green and ++5++
for red. See [Controls](controls.md) for every key.

![my_app after two OK presses and GREEN](first-app.png)

## 4. Test it automatically

`apps/my_app/test.txt`:

```text
wait 2500          # board_init and the power-zone handshake
press OK
press OK
press GREEN
wait 300
screenshot my_app.png device
quit
```

```sh
tools/fw2emu run apps/my_app --headless --script apps/my_app/test.txt
```

The log shows `count=1` and `count=2`, and `my_app.png` is the whole front
panel, LEDs included. [Input scripts](scripting.md) has every command,
including touches and sensor changes.

## 5. Check it fits the real chip

```sh
tools/fw2emu hwcheck apps/my_app
```

This builds the app with the real Pico SDK and Arm GCC and reports its SRAM
image, RAM, PSRAM and worst-case stack against the RP2350's limits — the
things the emulator itself can't tell you. It also leaves the real UF2 in
`build-hw/apps/my_app/`. See
[Debugging and hardware checks](debugging.md#real-hardware-check-toolsfw2emu-hwcheck).

## Starting from WiliBSP's template

WiliBSP's own starting point works the same way:

```sh
cp -r third_party/wilibsp/apps/template apps/my_app
sed -i 's/\btemplate\b/my_app/g' apps/my_app/CMakeLists.txt
```

Inside a WiliBSP checkout, `fw new-app my_app` does the same copy and
rename.
