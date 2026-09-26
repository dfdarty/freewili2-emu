cmake_minimum_required(VERSION 3.16)

# freewili2-emu — run FREE-WILi 2 (WiliBSP) apps on a PC or in a browser.
#
# The unmodified WiliBSP drivers and apps are compiled against a host shim of
# the Pico SDK (emu/sdk/include). Hardware is replaced by device models
# (emu/src/dev_*.c) that speak the same register / wire protocols.

project(freewili2_emu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
    if(EMSCRIPTEN)
        set(CMAKE_BUILD_TYPE Release)
    else()
        set(CMAKE_BUILD_TYPE RelWithDebInfo)
    endif()
endif()

set(WILIBSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wilibsp" CACHE PATH "WiliBSP checkout")
set(FW2_TOOLS_DIR "${WILIBSP_DIR}/tools")
if(NOT EXISTS "${WILIBSP_DIR}/bsp/fw2.h")
    message(FATAL_ERROR "WiliBSP not found at ${WILIBSP_DIR} — run: git submodule update --init")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(EMSCRIPTEN)
    set(_agentio_default OFF)
else()
    set(_agentio_default ON)
endif()
option(FW2_AGENTIO "Build WiliBSP's agentio remote-control/capture harness" ${_agentio_default})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ------------------------------------------------------------------ fidelity
# Native-only build options that make host runs closer to (or stricter than)
# the real RP2350. See docs/debugging.md.
option(FW2_EMU_SANITIZE "Build app, BSP and emulator with AddressSanitizer + UBSan (native only)" OFF)
option(FW2_EMU_32BIT "Build a 32-bit (-m32) emulator so pointer-size bugs show up as on the RP2350 (native only)" OFF)
if(EMSCRIPTEN AND (FW2_EMU_SANITIZE OR FW2_EMU_32BIT))
    message(FATAL_ERROR "FW2_EMU_SANITIZE and FW2_EMU_32BIT are for native builds (the web build is already 32-bit)")
endif()
if(FW2_EMU_SANITIZE)
    # UBSan stays recoverable so tests/ubsan.supp can name known upstream
    # findings; everything else halts (halt_on_error=1, set in emu/src/core.c).
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(FW2_EMU_32BIT)
    # SSE math keeps float/double at IEEE single/double precision like the
    # Cortex-M33 FPU, instead of x87's 80-bit intermediates.
    add_compile_options(-m32 -msse2 -mfpmath=sse)
    add_link_options(-m32)
    # A 32-bit SDL2 (libsdl2-dev:i386) is often not installable next to the
    # 64-bit toolchain, so the -m32 build is headless-only unless asked.
    set(_sdl_default OFF)
else()
    set(_sdl_default ON)
endif()
option(FW2_EMU_SDL "Window and host audio via SDL2 (OFF: headless-only emulator, no SDL needed)" ${_sdl_default})

# ------------------------------------------------------------------ BSP + emu
set(_bsp "${WILIBSP_DIR}/bsp")
add_library(freewili2_bsp STATIC
    # --- unmodified WiliBSP sources ---
    ${_bsp}/common/uf2_info.c
    ${_bsp}/agentio/agentio.c
    ${_bsp}/agentio/agentio_rle.c
    ${_bsp}/agentio/agentio_shadow.c
    ${_bsp}/platform/board.c
    ${_bsp}/platform/ioexp.c
    ${_bsp}/platform/psram.c
    ${_bsp}/platform/spi_bus.c
    ${_bsp}/display/st7796.c
    ${_bsp}/display/font5x7.c
    ${_bsp}/input/ft6336.c
    ${_bsp}/input/ft6336_map.c
    ${_bsp}/leds/led_color.c
    ${_bsp}/leds/ws2812_driver.c
    ${_bsp}/input/uartkbd_parse.c
    ${_bsp}/input/uartkbd.c
    ${_bsp}/input/app_recovery.c
    ${_bsp}/input/app_about.c
    ${_bsp}/input/picpwr_frame.c
    ${_bsp}/input/picpwr.c
    ${_bsp}/keyboard/fw2kb.c
    ${_bsp}/keyboard/fw2kb_hidmap.c
    ${_bsp}/gfx/palette.c
    ${_bsp}/audio/tone_gen.c
    ${_bsp}/audio/vu_meter.c
    ${_bsp}/audio/codec_nau88c10.c
    ${_bsp}/audio/audio_i2s_duplex.c
    ${_bsp}/audio/audio_capture.c
    ${_bsp}/pdm/pdm_capture.c
    ${_bsp}/dsp/cic.c
    ${_bsp}/dsp/dcblock.c
    ${_bsp}/sensors/sht40.c
    ${_bsp}/sensors/opt4001.c
    ${_bsp}/sensors/bmi323.c
    ${_bsp}/sensors/bmm350.c
    ${_bsp}/sensors/bmm350_comp.c
    # --- emulator ---
    emu/src/core.c
    emu/src/sdk_periph.c
    emu/src/dev_lcd.c
    emu/src/dev_touch.c
    emu/src/dev_leds.c
    emu/src/dev_ioexp.c
    emu/src/dev_pic.c
    emu/src/dev_sensors.c
    emu/src/dev_audio.c
    emu/src/dev_pdm.c
    emu/src/rtt.c
    emu/src/script.c
    emu/src/skin.c
    emu/src/stub_dvi.c)

# Shim headers first so they win over any SDK-looking header.
target_include_directories(freewili2_bsp BEFORE PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/emu/sdk/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/emu/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/emu/include/pio"
    "${_bsp}")
target_compile_definitions(freewili2_bsp PUBLIC
    FW2_AGENTIO=$<BOOL:${FW2_AGENTIO}>
    FW2_EMULATOR=1
    PICO_BUILD=1
    _GNU_SOURCE)
target_compile_options(freewili2_bsp PRIVATE -Wall -Wno-unused-parameter -Wno-unused-function)
if(FW2_EMU_SANITIZE)
    set_source_files_properties(emu/src/core.c PROPERTIES COMPILE_DEFINITIONS
        "FW2_EMU_UBSAN_SUPPRESSIONS=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/ubsan.supp\"")
endif()

if(EMSCRIPTEN)
    target_compile_options(freewili2_bsp PUBLIC "-sUSE_SDL=2")
    target_link_options(freewili2_bsp PUBLIC "-sUSE_SDL=2")
else()
    if(FW2_EMU_SDL)
        find_package(SDL2 REQUIRED)
        target_link_libraries(freewili2_bsp PUBLIC SDL2::SDL2)
    else()
        target_compile_definitions(freewili2_bsp PRIVATE FW2EMU_HEADLESS_ONLY=1)
    endif()
    target_link_libraries(freewili2_bsp PUBLIC m)
    if(FW2_AGENTIO)
        # agentio keeps its screen shadow at the top of PSRAM and checks that the
        # linker placed nothing there (&__psram_end__ <= shadow). Nothing is placed
        # in emulated PSRAM, so pin the marker to the PSRAM base. Non-PIE linking
        # keeps that absolute symbol from being relocated to the load address.
        target_link_options(freewili2_bsp PUBLIC "-Wl,--defsym=__psram_end__=0x11000000" "-no-pie")
    endif()
endif()

include(cmake/fw2_emu_app.cmake)

# ------------------------------------------------------------------ apps
# WiliBSP's own apps, built straight from the submodule with their own
# CMakeLists.txt. Your apps go in apps/ (see apps/README.md).
set(FW2_EMU_UPSTREAM_APPS template hello_display hello_agentio hello_sensors hello_audio hello_mics CACHE STRING "WiliBSP apps to build")
foreach(_app IN LISTS FW2_EMU_UPSTREAM_APPS)
    add_subdirectory("${WILIBSP_DIR}/apps/${_app}" "${CMAKE_BINARY_DIR}/wilibsp/${_app}")
endforeach()

option(FW2_EMU_LOCAL_APPS "Build every app in apps/" ON)
if(FW2_EMU_LOCAL_APPS)
    file(GLOB _local_apps LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/apps/*")
    foreach(_dir IN LISTS _local_apps)
        if(IS_DIRECTORY "${_dir}" AND EXISTS "${_dir}/CMakeLists.txt")
            add_subdirectory("${_dir}")
        endif()
    endforeach()
endif()

# App folders anywhere on disk (tools/fw2emu run / web use this with the two
# lists above emptied, so only the requested app is configured).
set(FW2_EMU_EXTRA_APPS "" CACHE STRING "Extra app folders to build (absolute paths, ;-separated)")
foreach(_dir IN LISTS FW2_EMU_EXTRA_APPS)
    get_filename_component(_dir "${_dir}" ABSOLUTE)
    if(NOT EXISTS "${_dir}/CMakeLists.txt")
        message(FATAL_ERROR "FW2_EMU_EXTRA_APPS: ${_dir} has no CMakeLists.txt")
    endif()
    string(MD5 _hash "${_dir}")
    string(SUBSTRING "${_hash}" 0 8 _hash)
    get_filename_component(_name "${_dir}" NAME)
    add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/extra/${_name}-${_hash}")
endforeach()

# ------------------------------------------------------------------ web page
if(EMSCRIPTEN)
    get_property(_apps GLOBAL PROPERTY FW2_EMU_APPS)
    set(_json "[")
    set(_sep "")
    foreach(_entry IN LISTS _apps)
        string(REPLACE "|" ";" _f "${_entry}")
        list(GET _f 0 _name)
        list(GET _f 1 _ver)
        list(GET _f 2 _desc)
        string(REPLACE "\\" "\\\\" _desc "${_desc}")
        string(REPLACE "\"" "\\\"" _desc "${_desc}")
        string(APPEND _json "${_sep}\n  {\"name\": \"${_name}\", \"version\": \"${_ver}\", \"description\": \"${_desc}\"}")
        set(_sep ",")
    endforeach()
    string(APPEND _json "\n]\n")
    file(WRITE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/apps.json" "${_json}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
                   "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/index.html" COPYONLY)
endif()
