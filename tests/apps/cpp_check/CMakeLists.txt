# cpp_check: emulator self-test that a WiliBSP app written in C++ builds and
# runs: its main() links, and the standard library works.
add_executable(cpp_check main.cpp)
target_link_libraries(cpp_check freewili2_bsp)
fw2_display_app(cpp_check POWER_ZONES DISPLAY VERSION 001 DESCRIPTION "Self-test: a C++ app")
