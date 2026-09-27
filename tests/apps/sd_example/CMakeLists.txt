# sd_example — the SD-card example from docs/main-link.md, as a real app:
# write a file in pieces, read it back, list the folder. Also builds for the
# board (tools/fw2emu hwcheck tests/apps/sd_example).
add_executable(sd_example main.c)
target_link_libraries(sd_example freewili2_bsp onewili_fwgui)   # the OneWili client
fw2_display_app(sd_example
    POWER_ZONES DISPLAY SDCARD                                  # SDCARD powers the card
    VERSION 001
    DESCRIPTION "Write, read back and list a file on the SD card")
