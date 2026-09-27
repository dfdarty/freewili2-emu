# bus_timing_check — times WiliBSP's LCD and I2C calls, so the smoke test can
# check that SPI and I2C transfers take as long as they would on the wire.
# Emulator-only self-test; run by tests/smoke.sh.
add_executable(bus_timing_check main.c)
target_link_libraries(bus_timing_check freewili2_bsp)
fw2_display_app(bus_timing_check
    POWER_ZONES DISPLAY SENSORS
    VERSION 001
    DESCRIPTION "Emulator self-test of SPI and I2C timing")
