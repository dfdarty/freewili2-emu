# Your apps

Each folder here with a `CMakeLists.txt` is built automatically. Use exactly
what WiliBSP expects, so the same folder also builds a UF2 for hardware:

```cmake
add_executable(my_app main.c)
target_link_libraries(my_app freewili2_bsp)
fw2_display_app(my_app
    POWER_ZONES DISPLAY RGB_LEDS
    VERSION 001
    DESCRIPTION "What the app does")
```

Start from WiliBSP's template: `cp -r third_party/wilibsp/apps/template apps/my_app`,
rename `template` to `my_app` in its `CMakeLists.txt`, then rebuild. The binary
is `build/bin/my_app`, and the web build lists it in the app picker.

Only the BSP pieces with device models link today (display, touch, buttons,
RGB LEDs, power zones, RTT, PSRAM). Calls into unmodelled drivers fail at
link time instead of misbehaving silently.
