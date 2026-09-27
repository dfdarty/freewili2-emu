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

The binary is `build/bin/my_app`, and the web build lists it in the app
picker. App folders outside the repository work too:
`tools/fw2emu run path/to/my_app`.

Walkthrough: [Your first app](https://dfdarty.github.io/freewili2-emu/first-app/).
Which drivers are modelled: [App compatibility](https://dfdarty.github.io/freewili2-emu/compatibility/).
