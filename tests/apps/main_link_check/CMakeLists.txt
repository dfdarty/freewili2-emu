# main_link_check — exercises the emulated MAIN CPU through the unmodified
# OneWili client: every SDFS operation and its error paths, header GPIO
# (set/toggle/read/PWM/streamed reports/external inputs), programmable Vout,
# unmodelled commands and EPOWERZONE. Run by tests/smoke.sh; it also builds
# for hardware (tools/fw2emu hwcheck tests/apps/main_link_check).
add_executable(main_link_check main.c)
target_link_libraries(main_link_check freewili2_bsp onewili_fwgui hardware_adc)
fw2_display_app(main_link_check
    POWER_ZONES DISPLAY SENSORS SDCARD ANALOG
    VERSION 001
    DESCRIPTION "Emulator self-test of the OneWili MAIN-CPU link")
