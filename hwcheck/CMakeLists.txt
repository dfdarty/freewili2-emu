# hwcheck/CMakeLists.txt — build apps for the real FREE-WILi 2 with the Pico SDK
# and Arm GCC, exactly as WiliBSP does, so tools/fw2emu hwcheck can report the
# real memory footprint and worst-case stack. Driven by tools/fw2emu; not part
# of the emulator build.
#
#   -DWILIBSP_DIR=...        WiliBSP checkout (default: ../third_party/wilibsp)
#   -DFW2_HW_APPS=a;b        app directories, each with a fw2_display_app() CMakeLists.txt
cmake_minimum_required(VERSION 3.13)

if(NOT WILIBSP_DIR)
    get_filename_component(WILIBSP_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/wilibsp" ABSOLUTE)
endif()

# Same board, options and SDK bring-up as WiliBSP's top-level CMakeLists.txt.
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${WILIBSP_DIR}/bsp/boards")
option(FW2_AGENTIO "Build the agentio remote-control/capture harness" ON)
set(PICO_PIO_USB_PATH "${WILIBSP_DIR}/bsp/third_party/Pico-PIO-USB")
include(${WILIBSP_DIR}/pico_sdk_import.cmake)
project(fw2_hwcheck C CXX ASM)
set(CMAKE_C_STANDARD 11)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
pico_sdk_init()

# Per-function stack usage plus the call graph (GCC writes a .ci file next to
# each object); tools/stackcheck.py combines them into a worst-case path.
# -fstack-usage also writes the plain per-function .su files for reference.
add_compile_options(-fcallgraph-info=su -fstack-usage)

add_subdirectory(${WILIBSP_DIR}/bsp wilibsp/bsp)
add_subdirectory(${WILIBSP_DIR}/libs/onewili/wilibsp wilibsp/onewili)   # the display-CPU OneWili package

foreach(app IN LISTS FW2_HW_APPS)
    get_filename_component(app "${app}" ABSOLUTE)
    get_filename_component(name "${app}" NAME)
    add_subdirectory("${app}" "apps/${name}")
endforeach()
