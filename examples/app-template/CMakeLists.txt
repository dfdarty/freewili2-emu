# A FREE-WILi 2 app, as WiliBSP expects it: the same folder builds for the
# emulator and, with the Pico SDK and Arm GCC, into a UF2 for the board.
add_executable(my_app main.c)
target_link_libraries(my_app freewili2_bsp)
fw2_display_app(my_app
    POWER_ZONES DISPLAY RGB_LEDS
    VERSION 001
    DESCRIPTION "Counts OK presses and colours the LEDs")
