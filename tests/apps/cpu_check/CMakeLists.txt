# cpu_check — times app code against the PC's own clock, so the smoke test can
# check that emu/src/cpu.c slows it to the chip's speed: on core 0, in a timer
# callback, on core 1, and on both cores at once. Emulator-only self-test; run
# by tests/smoke.sh.
add_executable(cpu_check main.c)
target_link_libraries(cpu_check freewili2_bsp)
fw2_display_app(cpu_check
    POWER_ZONES DISPLAY
    VERSION 001
    DESCRIPTION "Emulator self-test of app code at the chip's speed")
