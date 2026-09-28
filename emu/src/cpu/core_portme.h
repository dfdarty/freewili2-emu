/* core_portme.h — CoreMark's port header for the emulator's speed
 * calibration (emu/src/cpu.c).
 *
 * CoreMark's own files in third_party/coremark are unmodified; this header is
 * what every CoreMark port supplies. It builds the benchmark's workload (list,
 * matrix and state machine) with the same compiler and flags as the app, so
 * the measured speed is the speed app code runs at in this build: a
 * sanitizer, 32-bit or WebAssembly build measures itself. cpu.c has the driver
 * (CoreMark's core_main.c is not used), and no CoreMark score is reported. */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>
#include <stdint.h>

#define HAS_FLOAT 1
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 0
#define HAS_PRINTF 0

#define COMPILER_VERSION "host"
#define COMPILER_FLAGS "as the app"
#define MEM_LOCATION "static"

typedef signed short   ee_s16;
typedef unsigned short ee_u16;
typedef signed int     ee_s32;
typedef double         ee_f32;
typedef unsigned char  ee_u8;
typedef unsigned int   ee_u32;
typedef uintptr_t      ee_ptr_int;         /* holds a pointer on 32- and 64-bit hosts */
typedef size_t         ee_size_t;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK 0
#define USE_SOCKET 0
#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0

typedef struct CORE_PORTABLE_S {
    ee_u8 portable_id;
} core_portable;

extern ee_u32 default_num_contexts;

int ee_printf(const char *fmt, ...);

#endif
