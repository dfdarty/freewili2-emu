/* cpu.c — app code at the RP2350's speed.
 *
 * The emulator's clock is the PC's: buses, DMA, timers and the second core
 * all run in real time. App code, though, runs at the PC's speed, tens of
 * times faster than a Cortex-M33, so without this an app that would drop
 * frames on the board looks smooth here. This file makes each core's app
 * code take as long as it would on the chip:
 *
 *   measure  The emulator's own sources that app code calls into (the SDK,
 *            RTT, core.c) are built with -finstrument-functions. Their entry
 *            and exit hooks, below, see each core cross between app code and
 *            emulator code, so the time a core spends in app code between
 *            SDK calls is known. Interrupt handlers, timer callbacks, main()
 *            and core 1's entry are app code the emulator calls; those calls
 *            are bracketed with emu_cpu_app_begin/end.
 *   scale    That host time is multiplied by K = the PC's speed over the
 *            chip's, measured at start-up by running CoreMark's workload
 *            (third_party/coremark, unmodified) in this very build, against
 *            the Cortex-M33's ~4 CoreMark/MHz at the app's clk_sys.
 *   charge   The difference (K-1 times the host time) is owed. A core pays it
 *            at its next SDK call by sleeping, as emu_sleep_us() does: the
 *            buses, DMA, timers and the other core carry on meanwhile, as they
 *            would while the chip computed. Interrupt handlers and timer
 *            callbacks add to it and pay after they return.
 *
 * It is an estimate: CoreMark's mix of integer and pointer work stands for
 * all app code. What it leaves out is logged or noted in hwcheck: code and
 * data in PSRAM behind the 16 KB XIP cache (fw2_psram_app), and
 * double-precision maths, which the M33 does in software. `--cpu host` turns
 * the slowing off (the load estimate still shows); `--cpu-factor F` sets K.
 */
#include "emu/emu.h"
#include "coremark.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* The hooks run on every call into the emulator and touch only this file's
 * own state: no instrumentation of their own, and no sanitizer checks. */
#define NOINSTR __attribute__((no_instrument_function, no_sanitize("address", "undefined")))

/* The chip. Arm rates the Cortex-M33 at 4.09 CoreMark/MHz with its own
 * compiler; GCC gets a little less. WiliBSP runs it at 250 MHz; the SDK's
 * default is 150 MHz. */
#define CHIP_COREMARK_PER_MHZ 4.0
/* Debt is slept off once it reaches PAY_NS. A sleep can overrun (a browser
 * timer turn is >= 4 ms), and up to CREDIT_NS of overrun counts towards the
 * next debt, so the average rate stays right; more than that is a stall of
 * the PC's, not app code's time, and isn't given back as a burst. */
#ifdef __EMSCRIPTEN__
#define PAY_NS    4.0e6
#define CREDIT_NS 25.0e6
#else
#define PAY_NS    1.0e6
#define CREDIT_NS 3.0e6
#endif

typedef struct {
    int      depth;                        /* emulator frames on this core's stack */
    int      cb;                           /* app callbacks (IRQs, timers) the emulator is running */
    uint64_t app_since;                    /* when this core went back to app code (0: not yet) */
    double   debt_ns;                      /* chip time owed */
    double   busy_ns;                      /* chip time app code has used, all told */
    double   win_busy_ns;                  /* ... at the start of this perf window */
    double   peak;                         /* the busiest perf window, 0..1 */
    bool     paying;
    bool     seen;                         /* ran app code at all */
} cpu_core_t;

static cpu_core_t s_cpu[2];
static unsigned   s_core;                  /* which core the host thread is running */
static __thread int t_main;                /* the emulator's thread (not SDL's audio thread) */
static bool       s_on;                    /* hooks live */
static bool       s_throttle = true;       /* --cpu chip */
static double     s_fixed_k;               /* --cpu-factor */
static double     s_host_cm;               /* CoreMark iterations/s of this build on this PC */
static double     s_k;                     /* chip ns per host ns of app code */
static uint32_t   s_sys_hz;
static uint64_t   s_start_ns;
static bool       s_warned_slow;
static bool       s_announce;              /* say the factor once the app has set its clock */

__attribute__((weak)) const int fw2_emu_psram_app = 0;   /* 1 from fw2_psram_app() */

static NOINSTR uint64_t clk(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------ calibration */
volatile ee_s32 seed1_volatile = 0, seed2_volatile = 0, seed3_volatile = 0x66,
                seed4_volatile = 1, seed5_volatile = 0;
ee_u32 default_num_contexts = 1;
ee_s32 get_seed_32(int i);                 /* core_util.c; declared in core_main.c, which isn't built */
int ee_printf(const char *fmt, ...) { (void)fmt; return 0; }   /* only check_data_types() prints */

static ee_u8 s_blk[TOTAL_DATA_SIZE] __attribute__((aligned(8)));

/* CoreMark's performance-run set-up (core_main.c, which isn't built): seeds
 * 0, 0, 0x66 read from volatiles, 2000 bytes split between the list, matrix
 * and state-machine workloads. */
static void cm_setup(core_results *r) {
    memset(r, 0, sizeof *r);
    r->seed1 = (ee_s16)get_seed_32(1);
    r->seed2 = (ee_s16)get_seed_32(2);
    r->seed3 = (ee_s16)get_seed_32(3);
    r->execs = ALL_ALGORITHMS_MASK;
    r->size = TOTAL_DATA_SIZE / NUM_ALGORITHMS;
    r->memblock[0] = s_blk;
    for (int i = 0; i < NUM_ALGORITHMS; i++) r->memblock[i + 1] = s_blk + r->size * (ee_u32)i;
    r->list = core_list_init(r->size, r->memblock[1], r->seed1);
    core_init_matrix(r->size, r->memblock[2], (ee_s32)r->seed1 | (((ee_s32)r->seed2) << 16), &r->mat);
    core_init_state(r->size, r->seed1, r->memblock[3]);
}

/* CoreMark's iterate(). */
static void cm_iterate(core_results *r, ee_u32 n) {
    r->crc = r->crclist = r->crcmatrix = r->crcstate = 0;
    for (ee_u32 i = 0; i < n; i++) {
        ee_u16 crc = core_bench_list(r, 1);
        r->crc = crcu16(crc, r->crc);
        crc = core_bench_list(r, -1);
        r->crc = crcu16(crc, r->crc);
        if (i == 0) r->crclist = r->crc;
    }
}

/* Seconds for n iterations, or <0 if the results are wrong. */
static double cm_run(ee_u32 n, bool check) {
    core_results r;
    cm_setup(&r);
    uint64_t t0 = clk();
    cm_iterate(&r, n);
    uint64_t t1 = clk();
    if (check) {
        ee_u16 seedcrc = 0;
        seedcrc = crc16(r.seed1, seedcrc);
        seedcrc = crc16(r.seed2, seedcrc);
        seedcrc = crc16(r.seed3, seedcrc);
        seedcrc = crc16((ee_s16)r.size, seedcrc);
        /* CoreMark's known answers for the 2K performance run. */
        if (seedcrc != 0xe9f5 || r.crclist != 0xe714 || r.crcmatrix != 0x1fd7 || r.crcstate != 0x8e3a
            || check_data_types() != 0)
            return -1.0;
    }
    return (double)(t1 - t0) / 1e9;
}

/* Iterations per second, best of several short runs after a warm-up long
 * enough for a browser's WebAssembly tier-up and a CPU's clock ramp. */
static double calibrate(void) {
    if (cm_run(1, true) < 0) return 0.0;
    ee_u32 n = 10;
    uint64_t warm_until = clk() + 40000000ull;
    double s;
    while ((s = cm_run(n, false)) < 0.004 || clk() < warm_until)
        if (s < 0.004 && n < (1u << 24)) n *= 2;
    double best = 1e9;
    for (int i = 0; i < 5; i++) {
        s = cm_run(n, false);
        if (s > 0 && s < best) best = s;
    }
    return (double)n / best;
}

/* ---------------------------------------------------------------- scaling */
static void set_k(void) {
    double chip_cm = CHIP_COREMARK_PER_MHZ * (double)s_sys_hz / 1e6;
    s_k = s_fixed_k > 0 ? s_fixed_k : (s_host_cm > 0 && chip_cm > 0 ? s_host_cm / chip_cm : 0.0);
}

static void announce(void) {
    s_announce = false;
    if (s_k <= 0) return;
    unsigned mhz = (unsigned)((s_sys_hz + 500000u) / 1000000u);
    if (s_k < 1.0) {
        if (!s_warned_slow)
            emu_log("cpu: this PC runs app code at %.2fx the speed of the RP2350 at %u MHz, so it can't be "
                    "slowed to match: app code is slower here than on the board", s_k, mhz);
        s_warned_slow = true;
    } else if (s_throttle) {
        emu_log("cpu: app code runs at the RP2350's speed at %u MHz (this PC is %.0fx as fast; "
                "--cpu host runs it at full speed)", mhz, s_k);
    } else {
        emu_log("cpu: app code runs at this PC's full speed, %.0fx the RP2350's at %u MHz (--cpu host)",
                s_k, mhz);
    }
}

/* Apps set clk_sys first thing (WiliBSP: 250 MHz), so the factor is said
 * then; an app that keeps the SDK's 150 MHz hears it after a second. */
void emu_cpu_clock_hz(uint32_t hz) {
    if (hz == s_sys_hz || !hz) return;
    s_sys_hz = hz;
    set_k();
    if (s_on) announce();
}

bool emu_cpu_set_mode(const char *mode) {
    if (!strcmp(mode, "chip")) s_throttle = true;
    else if (!strcmp(mode, "host")) s_throttle = false;
    else return false;
    return true;
}

bool emu_cpu_set_factor(const char *f) {
    char *end;
    double v = strtod(f, &end);
    if (end == f || *end || !(v > 0)) return false;
    s_fixed_k = v;
    return true;
}

void emu_cpu_init(uint32_t sys_hz) {
    t_main = 1;
    s_sys_hz = sys_hz;
    if (s_fixed_k <= 0) {
        s_host_cm = calibrate();
        if (s_host_cm <= 0)
            emu_log("cpu: the speed calibration (CoreMark's workload) gave wrong answers in this build; "
                    "app code runs at the PC's full speed");
    }
    set_k();
    s_announce = true;
    if (fw2_emu_psram_app && s_throttle && s_k > 1.0)
        emu_log("cpu: on the board this app's code and data are in PSRAM, behind a 16 KB cache; the "
                "emulator doesn't model cache misses, so the board can be slower than this");
    s_start_ns = clk();
    s_cpu[0].depth = 1;                    /* we're in the emulator's main() */
    s_on = true;
}

/* ------------------------------------------------------------- accounting */
static NOINSTR void charge(cpu_core_t *c, uint64_t host_ns) {
    double chip = (double)host_ns * s_k;
    c->busy_ns += chip;
    c->seen = true;
    if (s_throttle && s_k > 1.0) c->debt_ns += chip - (double)host_ns;
}

static NOINSTR void pay(cpu_core_t *c) {
    c->paying = true;
    uint64_t t0 = clk();
    emu_sleep_us((uint64_t)(c->debt_ns / 1000.0));
    c->debt_ns -= (double)(clk() - t0);
    if (c->debt_ns < -CREDIT_NS) c->debt_ns = -CREDIT_NS;
    c->paying = false;
}

#ifdef __EMSCRIPTEN__
/* Clang hands each hook its caller's return address, and WebAssembly has no
 * way to read one: Emscripten's emscripten_return_address() builds a
 * JavaScript stack trace to find it, milliseconds a call. The hooks ignore
 * it, and nothing else in the emulator asks for a return address, so this
 * build's answer is none. */
void *emscripten_return_address(int level) { (void)level; return NULL; }
#endif

NOINSTR void __cyg_profile_func_enter(void *fn, void *site) {
    (void)fn; (void)site;
    if (!s_on || !t_main) return;
    cpu_core_t *c = &s_cpu[s_core];
    if (c->depth++ != 0) return;
    if (c->app_since) charge(c, clk() - c->app_since);
    if (!c->cb && !c->paying && c->debt_ns >= PAY_NS) pay(c);
}

NOINSTR void __cyg_profile_func_exit(void *fn, void *site) {
    (void)fn; (void)site;
    if (!s_on || !t_main) return;
    cpu_core_t *c = &s_cpu[s_core];
    if (c->depth > 0 && --c->depth == 0) c->app_since = clk();
}

int emu_cpu_app_begin(bool callback) {
    cpu_core_t *c = &s_cpu[s_core];
    int saved = c->depth;
    c->depth = 0;
    if (callback) c->cb++;
    c->app_since = clk();
    return saved;
}

void emu_cpu_app_end(int saved, bool callback) {
    cpu_core_t *c = &s_cpu[s_core];
    if (s_on && c->depth == 0 && c->app_since) charge(c, clk() - c->app_since);
    c->depth = saved;
    if (callback) c->cb--;
}

void emu_cpu_switched(unsigned core) { s_core = core & 1u; }

double emu_cpu_factor(void) { return s_throttle && s_k > 1.0 ? s_k : 1.0; }

void emu_cpu_launch(unsigned core) {
    cpu_core_t *c = &s_cpu[core & 1u];
    double busy = c->busy_ns, peak = c->peak;
    memset(c, 0, sizeof *c);
    c->busy_ns = c->win_busy_ns = busy;    /* keep the totals across a relaunch */
    c->peak = peak;
}

/* ---------------------------------------------------------------- reports */
int emu_cpu_perf(char *out, size_t cap, double span_ns) {
    if (s_announce && clk() - s_start_ns >= 2000000000ull) announce();   /* the app kept the SDK's clock */
    if (s_k <= 0 || span_ns <= 0) return 0;
    int n = 0;
    for (unsigned i = 0; i < 2; i++) {
        cpu_core_t *c = &s_cpu[i];
        double load = (c->busy_ns - c->win_busy_ns) / span_ns;
        c->win_busy_ns = c->busy_ns;
        if (load > c->peak) c->peak = load;
        if (!c->seen) continue;
        if (load > 1.0)                    /* --cpu host: more than the chip could do */
            n += snprintf(out + n, cap - (size_t)n, "%sCPU%u >100%%", n ? "  " : "", i);
        else
            n += snprintf(out + n, cap - (size_t)n, "%sCPU%u %.0f%%", n ? "  " : "", i, load * 100.0);
        if ((size_t)n >= cap) return (int)cap - 1;
    }
    return n;
}

void emu_cpu_report(void) {
    static bool done;
    if (done || !s_on || s_k <= 0) return;
    done = true;
    double wall = (double)(clk() - s_start_ns);
    if (wall < 1e9) return;
    unsigned mhz = (unsigned)((s_sys_hz + 500000u) / 1000000u);
    for (unsigned i = 0; i < 2; i++) {
        cpu_core_t *c = &s_cpu[i];
        if (!c->seen) continue;
        double avg = c->busy_ns / wall;
        if (c->peak > 1.0)
            emu_log("cpu: core %u ran more app code than the RP2350 at %u MHz could (--cpu host): it would "
                    "have been busy all the time", i, mhz);
        else
            emu_log("cpu: core %u busy %.0f%% of the run, %.0f%% in its busiest second (the RP2350 at %u MHz, "
                    "estimated)", i, avg * 100.0, c->peak * 100.0, mhz);
    }
}
