# script_check — reports how long buttons and touches are held and what the
# SHT40 reads, so tests/scripts/script_check.txt can check that every script
# command is parsed as written (inline # comments included). Emulator-only
# self-test; run by tests/smoke.sh.
add_executable(script_check main.c)
target_link_libraries(script_check freewili2_bsp)
fw2_display_app(script_check
    POWER_ZONES DISPLAY SENSORS
    VERSION 001
    DESCRIPTION "Emulator self-test of input-script parsing")
