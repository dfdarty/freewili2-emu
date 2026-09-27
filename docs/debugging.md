# Debugging and checking apps

The emulator runs your app as a normal host program, so the usual host tools
work on it. Some classes of bug only show up on the real RP2350, though:
running out of memory, overflowing the 4 KB stack, and code that assumes
64-bit pointers. This page covers both.

| Tool | Catches |
|---|---|
| gdb on the native build | logic bugs: step through app and BSP code |
| `FW2_EMU_SANITIZE` build | out-of-bounds accesses, use-after-free, leaks, undefined behaviour |
| `FW2_EMU_32BIT` build | code that assumes 64-bit pointers or `long` |
| `tools/fw2emu hwcheck` | apps that do not fit the real chip: image, RAM, PSRAM, stack |

## gdb

Native builds are `RelWithDebInfo` by default. For full local variables,
configure a `Debug` build in its own directory:

```sh
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
gdb --args build-debug/bin/hello_display --headless --script tests/scripts/hello_display.txt
```

The emulator starts first and then calls your app. Your app's `main()` is
renamed to `fw2_emu_app_main` at compile time, so break on that name, or on
a file and line:

```
(gdb) break fw2_emu_app_main
(gdb) break main.c:42
(gdb) run
```

`break main` stops in the emulator's own `main`, not in your app.

Other things to know:

- Drop `--headless` to debug with the window open. The app and the window
  share one thread, so the window freezes while you are stopped at a
  breakpoint.
- Every SDK wait (`sleep_ms`, `tight_loop_contents`, ...) calls back into the
  emulator. When you step over one, the device models run and time moves on.
- Ctrl-C in gdb interrupts the app wherever it is, usually inside
  `emu_poll()`. Use `finish` or `up` to get back to your code.

## Sanitizers: `FW2_EMU_SANITIZE`

```sh
BUILD_DIR=build-asan CMAKE_ARGS="-DFW2_EMU_SANITIZE=ON" tests/smoke.sh
# or by hand:
cmake -S . -B build-asan -G Ninja -DFW2_EMU_SANITIZE=ON
cmake --build build-asan
build-asan/bin/hello_display --headless --script tests/scripts/hello_display.txt
```

This builds the app, WiliBSP and the emulator with AddressSanitizer and
UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`). The first
report stops the program with a stack trace. Reports cover out-of-bounds
reads and writes on the stack, globals and the heap; use-after-free; leaks;
signed overflow; misaligned access; bad shifts; and similar errors.

On the RP2350 these bugs usually corrupt memory without a sound and fail
later somewhere else. The sanitizer build pins them to the line that causes
them.

`tests/ubsan.supp` lists known findings in upstream WiliBSP code, which this
project does not patch (they are reported upstream instead). It is loaded
automatically. To see those findings too, run with
`UBSAN_OPTIONS=suppressions=/dev/null`. You can override any other sanitizer
setting with `ASAN_OPTIONS` or `UBSAN_OPTIONS` as usual.

This option is for native builds only. The emulated PSRAM at `0x11000000`
does not overlap ASan's shadow memory on x86-64 or i386.

## 32-bit build: `FW2_EMU_32BIT`

```sh
sudo apt install gcc-multilib
BUILD_DIR=build-m32 CMAKE_ARGS="-DFW2_EMU_32BIT=ON" tests/smoke.sh
```

This option compiles everything with `-m32`, so pointers, `long` and
`size_t` are 32 bits wide, as on the RP2350. It finds bugs such as a pointer
stored in a `uint32_t` or a `long` used where the code needs exactly 32 bits.
Floating point uses SSE (`-msse2 -mfpmath=sse`), which rounds to IEEE single
and double precision like the Cortex-M33's FPU. It does not use x87's 80-bit
intermediates.

A 32-bit SDL2 (`libsdl2-dev:i386`) often cannot be installed next to a 64-bit
desktop's packages. For that reason the 32-bit build defaults to
`FW2_EMU_SDL=OFF`: a headless-only emulator with no window and no host
audio. `--script`, `--rtt`, screenshots and `--audio-out` still work. If you
do have a 32-bit SDL2 that CMake can find, pass `-DFW2_EMU_SDL=ON`.

You can combine it with `FW2_EMU_SANITIZE=ON`, which needs the 32-bit ASan
runtime (`lib32asan8`, installed with `gcc-multilib`).

The browser build is already 32-bit (wasm32).

## Real-hardware check: `tools/fw2emu hwcheck`

The emulator cannot tell you whether an app fits the real chip. Host code is
x86 or wasm rather than Thumb-2, and a host thread has megabytes of stack.
`hwcheck` builds the app for the FREE-WILi 2 the same way WiliBSP does, with
the Pico SDK and Arm GCC, and reads the exact numbers from the linked image.

```sh
tools/fw2emu hwcheck                       # every app the emulator builds
tools/fw2emu hwcheck apps/my_app           # one app (any folder with a fw2_display_app() CMakeLists.txt)
tools/fw2emu hwcheck -v apps/my_app        # also list IRQ handlers and every unknown
tools/fw2emu hwcheck --json > hwcheck.json # machine-readable report
tools/fw2emu hwcheck -o hwcheck.json       # table on stdout, JSON to a file
tools/fw2emu hwcheck --fetch-toolchain     # use (and if needed download) Arm GNU Toolchain 14.2.Rel1
```

Options: `--build-dir DIR` (default `build-hw/`), `--sdk PATH`,
`--toolchain DIR`, `--fetch-toolchain`, and `--exclude APP` (repeatable).

**Requirements:**

- Arm GCC. WiliBSP builds with the Arm GNU Toolchain 14.2.Rel1, and so does
  CI. The tool looks for a toolchain in this order:
  1. `--toolchain`;
  2. `PICO_TOOLCHAIN_PATH`;
  3. `~/.pico-sdk/toolchain/14_2_Rel1`;
  4. `~/.cache/fw2emu/arm-gnu-toolchain-14.2.rel1-<arch>-arm-none-eabi`;
  5. `arm-none-eabi-gcc` on `PATH`.

  `--fetch-toolchain` downloads 14.2.Rel1 from developer.arm.com into that
  cache folder if none of the first four exist (Linux x86_64 and aarch64; the
  SHA-256 is checked).

  As a local fallback, the distribution's GCC also works:
  `sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`.
  Ubuntu 24.04 ships GCC 13.2. Its sizes and frames are close to 14.2's but
  not identical, and `hwcheck` prints a note when it uses anything other
  than 14.2.
- The Pico SDK 2.3.0. The tool looks for it in this order: `PICO_SDK_PATH`,
  then `~/.pico-sdk/sdk/2.3.0` (where WiliBSP's `fw` tool expects it). If it
  finds neither, it clones the SDK into `~/.cache/fw2emu/pico-sdk-2.3.0` on
  first use. picotool is built once and cached in `~/.cache/fw2emu/picotool`.
- WiliBSP's nested `onewili` submodule:
  `git -C third_party/wilibsp submodule update --init --depth 1 libs/onewili`.

The build runs the same post-link checks as WiliBSP (`make_app_uf2.py`,
`check_app_uf2.py`, and an app's own `POST_BUILD` checks such as
`hello_psram_exec`'s `verify_layout.py`), so it also produces the real
`.uf2` in `build-hw/apps/<app>/`. When a build step or check fails,
`hwcheck` prints only its error lines (compiler, linker or script messages)
under the app, plus the path of the full log. If the ELF was still linked,
the sizes are reported as well.

### What it reports

| Column | Meaning | Limit |
|---|---|---|
| image (loaded) | the UF2 payload, measured as how far it extends from the start of its window. `SRAM` for `fw2_display_app()`, `PSRAM` for `fw2_psram_app()` | SRAM: 448 KB (the installer only accepts SRAM payload inside `0x20000000..0x20070000`). PSRAM: 8 MB |
| SRAM (static) | everything placed in SRAM at run time: code and data copied there, `.bss`, and the `.heap` reserve (the highest SRAM address the linker uses) | 512 KB for SRAM apps (malloc can use the rest up to `0x20080000`). For PSRAM apps, up to the stack top (448 KB) |
| PSRAM (all) | loaded PSRAM image plus `__psram` / `__uninitialized_psram` data (NOLOAD) | 8 MB (`0x11000000..0x11800000`) |
| stack | worst case for core 0 / available below the stack top | 4 KB (SCRATCH_Y) for SRAM apps; the gap above static SRAM for PSRAM apps |
| unknowns | places where the stack figure is only a lower bound (see below) | |

The command exits with status 1 if any of the following is true:

- an SRAM image goes past 448 KB, or a PSRAM image past 8 MB;
- static SRAM goes past its limit;
- PSRAM goes past 8 MB;
- an image mixes SRAM and PSRAM payload, or has payload in flash or scratch
  RAM (the installer rejects both);
- the stack does not fit (see below);
- a build step or post-link check fails.

`--exclude APP` skips an app, by folder or target name.

**PSRAM apps (`fw2_psram_app()`).** WiliBSP links these with its own
scripts (`bsp/app/psram_link/`):

- The vector table, the app's code and read-only data load into and run
  from PSRAM.
- An SRAM bootstrap (`.sram_bootstrap`: the BSP and the SDK code that
  touches clocks and QMI) and `.data` load into PSRAM and are copied to SRAM
  by `psram_startup.S` / `psram_bootstrap.c` before `main()`.

`hwcheck` reports the PSRAM image, what is copied to SRAM, and the SRAM in
use at run time (bootstrap + `.data` + `.bss` + heap reserve). The stack
figure starts at `fw2_psram_bootstrap()`, which calls `main()` on the same
stack.

**Stack layout on the RP2350.** The limits come from the linked ELF's
`__StackTop` (the initial stack pointer) and the scratch sections. There is
no stack guard by default.

- **SRAM apps**: core 0's stack starts at the top of SCRATCH_Y
  (`0x20082000`) and grows down. Past the 4 KB of SCRATCH_Y it runs into
  SCRATCH_X, where core 1's stack lives. Past SCRATCH_X it runs into the top
  of RAM, where the heap ends. `hwcheck` handles this as follows:
  - up to 4 KB: OK;
  - more than 4 KB: a warning, or an error if the app calls
    `multicore_launch_core1()`;
  - more than 8 KB: an error.
- **PSRAM apps**: WiliBSP sets `__StackTop` to `0x20070000`, below the SRAM
  the DISPLAY loader reserves. The stack grows down towards the end of
  static SRAM, and it is an error if it does not fit in that gap. The heap
  grows up into the same gap. If the app uses `malloc`, `hwcheck` warns:
  the SDK's `_sbrk` only stops at `__StackLimit` (`0x20080000`), which lies
  above the stack top, so nothing stops the heap from running into the
  stack.

A core 1 entry point is analysed against SCRATCH_X.

**How the stack figure is computed.** Every object is compiled with
`-fcallgraph-info=su`, which writes a `.ci` file with each function's frame
size and its calls (the plain `.su` files sit next to them).
`tools/stackcheck.py` builds the call graph from the objects the linker
actually used, taken from the link map. It fills the gaps from the
disassembly: calls GCC added late (libgcc helpers), and the frames and calls
of assembly and prebuilt newlib functions. Those appear as "estimated from
disassembly".

- **main**: the deepest call chain from `main()` (from
  `fw2_psram_bootstrap()` for PSRAM apps). Linker veneers, the long-branch
  stubs between SRAM and PSRAM code, are followed.
- **IRQ handlers** come from three sources:
  - functions passed to `irq_set_exclusive_handler()` or
    `irq_add_shared_handler()`;
  - C functions in the vector table;
  - callbacks that SDK dispatchers run in interrupt context (alarms,
    repeating timers, GPIO IRQ callbacks).

  Handlers are found as function addresses loaded by the code that registers
  them. This is a heuristic: a handler whose address travels through a
  variable is not found. Shared handlers add the 8-byte chain trampoline.
- **Exception entry** is 104 bytes, the Cortex-M33 frame with FP context,
  plus 4 bytes of alignment padding. If the app has no FP instructions it is
  32 + 4 bytes.
- **Combined** = main + exception entry + the worst handler. Interrupts stack
  on top of whatever they interrupt, on the same stack. All SDK IRQs default
  to one priority, so handlers do not nest. If the app calls
  `irq_set_priority()`, hwcheck says so, because nesting then adds more.

**Unknowns** are reported, never silently ignored:

- **indirect calls**: calls through function pointers, including boot ROM
  calls and stdio drivers;
- **recursion**: for example the float formatting in the SDK's `printf`;
- **dynamic frames**: `alloca` or VLAs;
- **functions with no stack information**.

Each of these means the true worst case can be higher than the figure shown.
`-v` lists them all.

To analyse one ELF from an existing hwcheck build directory, run
`tools/stackcheck.py build-hw/apps/<app>/<app>.elf [--json]`.
