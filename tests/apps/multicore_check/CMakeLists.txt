# multicore_check — exercises core 1 and the SDK's inter-core pieces (FIFOs,
# per-core interrupts and masking, spin locks, mutexes, semaphores, queues,
# lockout, relaunch). Emulator-only self-test; run by tests/smoke.sh.
add_executable(multicore_check main.c)
target_link_libraries(multicore_check freewili2_bsp pico_multicore pico_sync pico_util pico_unique_id)
fw2_display_app(multicore_check
    POWER_ZONES DISPLAY
    VERSION 001
    DESCRIPTION "Emulator self-test of the second core")
