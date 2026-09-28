/* cpu_check — app code takes the chip's time, not the PC's.
 *
 * emu/src/cpu.c times each core's app code between SDK calls and makes the
 * core sleep off the difference between the PC's speed and the chip's, K
 * times as long in all (emu_cpu_factor()). This app runs a stretch of pure
 * computation -- no SDK call inside -- and compares the board time it took
 * (time_us_64) with the PC time it took (the PC's own clock, which an
 * emulator-only test may read):
 *
 *   main   on core 0                             board time ~ K x PC time
 *   timer  inside an alarm callback, paid after it returns    (the same)
 *   core1  on core 1                                          (the same)
 *   both   on both cores at once: they overlap, as on the chip,
 *          so the pair takes ~K x one core's PC time, not twice that
 *
 * Logs "cc: <case> ratio N.NN" (board time / (K x PC time)) and
 * "cc: PASS" or "cc: FAIL <case>". With --cpu host, or a PC not faster than
 * the chip, there is nothing to check: "cc: SKIP". */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "emu/emu.h"

#include <time.h>

static uint64_t pc_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static volatile uint32_t s_sink;
static uint32_t s_n = 1000;                 /* work() iterations: ~3 ms of the PC's time */

static void work(uint32_t n) {
    uint32_t x = 1;
    for (uint32_t i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        x ^= x >> 13;
    }
    s_sink = x;
}

/* One timed stretch: board microseconds, and the PC's nanoseconds. */
typedef struct { uint64_t board_us, pc_ns; } span_t;

static span_t timed_work(void) {
    span_t s;
    uint64_t b0 = time_us_64();             /* an SDK call: any earlier debt is paid first */
    uint64_t p0 = pc_ns();
    work(s_n);
    s.pc_ns = pc_ns() - p0;
    s.board_us = time_us_64() - b0;         /* ... and this one pays for work() */
    return s;
}

static bool check(const char *what, double board_us, double pc_ns, double lo, double hi) {
    double r = board_us * 1000.0 / (emu_cpu_factor() * pc_ns);
    DIAG("cc: %s ratio %.2f (board %.1f ms, PC %.2f ms)\n", what, r, board_us / 1000.0, pc_ns / 1e6);
    if (r < lo || r > hi) { DIAG("cc: FAIL %s\n", what); return false; }
    return true;
}

/* ------------------------------------------------------------------ timer */
static volatile bool     s_fired;
static volatile uint64_t s_cb_pc_ns;

static int64_t on_alarm(alarm_id_t id, void *user) {
    (void)id; (void)user;
    uint64_t p0 = pc_ns();
    work(s_n);
    s_cb_pc_ns = pc_ns() - p0;
    s_fired = true;
    return 0;
}

/* ------------------------------------------------------------------ core 1 */
static volatile uint64_t s_c1_board_us, s_c1_pc_ns;

static void core1_main(void) {
    for (;;) {
        uint32_t cmd = multicore_fifo_pop_blocking();
        if (cmd == 1) {                     /* time a stretch on core 1 */
            span_t s = timed_work();
            s_c1_board_us = s.board_us;
            s_c1_pc_ns = s.pc_ns;
        } else {                            /* just compute, alongside core 0 */
            uint64_t p0 = pc_ns();
            work(s_n);
            s_c1_pc_ns = pc_ns() - p0;
        }
        multicore_fifo_push_blocking(cmd);
    }
}

int main(void) {
    board_init();
    fw2_app_recovery_init();
    double k = emu_cpu_factor();
    DIAG("cc: factor %.1f\n", k);
    if (k < 2.0) {
        DIAG("cc: SKIP (app code isn't being slowed: --cpu host, or a PC no faster than the chip)\n");
        for (;;) sleep_ms(100);
    }

    /* Size the stretch to ~3 ms of the PC's time. */
    for (;;) {
        uint64_t p0 = pc_ns();
        work(s_n);
        if (pc_ns() - p0 >= 3000000u || s_n >= (1u << 28)) break;
        s_n *= 2;
        sleep_ms(1);                        /* an SDK call between tries, to pay as we go */
    }
    bool ok = true;

    span_t m = timed_work();
    ok &= check("main", (double)m.board_us, (double)m.pc_ns, 0.85, 1.20);

    s_fired = false;
    uint64_t b0 = time_us_64();
    add_alarm_in_us(100, on_alarm, NULL, true);
    while (!s_fired) tight_loop_contents();
    uint64_t b1 = time_us_64();             /* the callback's debt is paid on the way in */
    /* Up to ~2 ms of that is the alarm's own delay and the 1 kHz service rate. */
    ok &= check("timer", (double)(b1 - b0), (double)s_cb_pc_ns, 0.85, 1.20 + 2000.0 * 1000.0 / (k * (double)s_cb_pc_ns));

    multicore_launch_core1(core1_main);
    multicore_fifo_push_blocking(1);
    multicore_fifo_pop_blocking();
    ok &= check("core1", (double)s_c1_board_us, (double)s_c1_pc_ns, 0.85, 1.20);

    b0 = time_us_64();
    multicore_fifo_push_blocking(2);
    uint64_t p0 = pc_ns();
    work(s_n);
    uint64_t own = pc_ns() - p0;
    multicore_fifo_pop_blocking();
    b1 = time_us_64();
    /* Each core's stretch takes K x its PC time; together they overlap. The
     * PC runs them one after the other, which adds one core's PC time. */
    double one = (double)(own > s_c1_pc_ns ? own : s_c1_pc_ns);
    ok &= check("both", (double)(b1 - b0), one, 0.85, 1.35);

    DIAG(ok ? "cc: PASS\n" : "cc: FAIL\n");
    for (;;) sleep_ms(100);
}
