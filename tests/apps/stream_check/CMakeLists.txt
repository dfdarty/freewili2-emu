# stream_check — exercises the emulated MAIN's peer-stream router
# (emu/src/dev_stream.c) through OneWili's unmodified stream client: HELLO and
# CREDIT, loopback, drops for MAIN / absent peers / ESP32 Mode, datagrams to
# and from scripted peers, the credit window, and link expiry. Run by
# tests/smoke.sh with --peer esp32=script --peer cm0=script.
add_executable(stream_check main.c)
target_link_libraries(stream_check freewili2_bsp onewili_fwgui)
fw2_display_app(stream_check
    POWER_ZONES DISPLAY
    VERSION 001
    DESCRIPTION "Emulator self-test of OneWili peer streams")
