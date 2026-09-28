/* multicore.c — the RP2350's second core, and the SDK's inter-core pieces:
 * the SIO FIFOs, lockout, doorbells, per-core interrupt masking, the unique
 * board id.
 *
 * Both cores run on the one host thread as coroutines (ucontext natively,
 * Emscripten fibers in the browser), so the device models never see two
 * callers at once and runs stay repeatable. A core gives up the host thread
 *   - whenever it waits: emu_sleep_us(), tight_loop_contents() and every
 *     blocking SDK call built on them (FIFO, spin lock, mutex, semaphore,
 *     queue) call emu_core_idle();
 *   - after running for SLICE_US, at its next SDK call (emu_poll(), which
 *     time reads, DMA and UART status calls all make): emu_core_tick().
 * When both cores wait, the host sleeps until the earlier of their wake
 * times, so an idle two-core app doesn't spin the PC's CPU.
 *
 * What this can't show: true simultaneity. A data race that needs both cores
 * inside the same few instructions won't happen here; one that needs a core
 * to be preempted at an SDK call will. And a loop that spins on a plain
 * variable with no SDK call never yields. */
#include "emu/emu.h"
#include "pico.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#include <unistd.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#define FW2_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FW2_ASAN 1
#endif
#endif
#ifdef FW2_ASAN
#include <sanitizer/asan_interface.h>
#include <sanitizer/common_interface_defs.h>
#endif

#define SLICE_US 1000u
#ifdef FW2_ASAN
#define CORE1_STACK (4u << 20)             /* instrumented frames are much larger */
#else
#define CORE1_STACK (1u << 20)             /* host code, not the 2 KB of the chip */
#endif
#define ASYNCIFY_STACK (128u << 10)

typedef struct {
    bool     running;                      /* core 0 always; core 1 once launched */
    bool     waiting;                      /* parked in emu_core_idle()           */
    uint64_t wake_us;                      /* when its wait ends (0 = unknown)     */
    uint64_t slice_start_us;
    bool     irq_off;                      /* PRIMASK                              */
    uint32_t fifo[4];                      /* FIFO this core RECEIVES from         */
    unsigned fifo_n, fifo_r;
    uint32_t fifo_st;                      /* sticky WOF / ROE                     */
    uint8_t  bells;                        /* doorbells set for this core          */
#ifdef __EMSCRIPTEN__
    emscripten_fiber_t fiber;
    void    *astack;
#else
    ucontext_t ctx;
#endif
    void    *stack;
    size_t   stack_size;
    const void *asan_bottom;
    size_t   asan_size;
    void    *asan_fake;
} core_t;

static core_t   s_core[2] = { [0] = { .running = true } };
static unsigned s_cur;
static void   (*s_core1_entry)(void);
static void    *s_dead_stack;              /* core 1's stack after a reset, freed from core 0 */
static bool     s_victim[2], s_locked_out[2];
static uint8_t  s_bell_claimed[2];

uint emu_get_core_num(void) { return s_cur; }
bool emu_core_irqs_off(unsigned core) { return s_core[core & 1].irq_off; }

static bool runnable(unsigned c) { return s_core[c].running && !s_locked_out[c]; }

static void host_sleep_us(uint64_t us) {
#ifdef __EMSCRIPTEN__
    if (us >= 1000u) emscripten_sleep((unsigned)(us / 1000u));
#else
    if (us) usleep((useconds_t)us);
#endif
}

/* ------------------------------------------------------------- switching */
static void switch_to(unsigned to, bool dying) {
    unsigned from = s_cur;
    s_cur = to;
    s_core[to].slice_start_us = emu_time_us();
#ifdef __EMSCRIPTEN__
    (void)dying;
    emscripten_fiber_swap(&s_core[from].fiber, &s_core[to].fiber);
#else
#ifdef FW2_ASAN
    __sanitizer_start_switch_fiber(dying ? NULL : &s_core[from].asan_fake,
                                   s_core[to].asan_bottom, s_core[to].asan_size);
#else
    (void)dying;
#endif
    swapcontext(&s_core[from].ctx, &s_core[to].ctx);
#ifdef FW2_ASAN
    __sanitizer_finish_switch_fiber(s_core[from].asan_fake, NULL, NULL);
#endif
#endif
    /* running as `from` again */
    emu_irq_deliver_pending();
}

void emu_core_tick(void) {
    unsigned o = s_cur ^ 1u;
    if (!s_core[1].running || !runnable(o) || emu_in_service()) return;
    if (emu_time_us() - s_core[s_cur].slice_start_us < SLICE_US) return;
    s_core[s_cur].waiting = false;
    switch_to(o, false);
}

bool emu_core_idle(uint64_t wake_us) {
    unsigned o = s_cur ^ 1u;
    if (!s_core[1].running || !runnable(o) || emu_in_service()) return false;
    core_t *me = &s_core[s_cur], *other = &s_core[o];
    me->waiting = true;
    me->wake_us = wake_us;
    if (other->waiting) {                  /* both idle: let the PC rest */
        uint64_t now = emu_time_us(), until = now + 20u;
        if (me->wake_us && other->wake_us) {
            until = me->wake_us < other->wake_us ? me->wake_us : other->wake_us;
            if (until > now + 1000u) until = now + 1000u;
        }
        if (until > now) host_sleep_us(until - now);
    }
    switch_to(o, false);
    me->waiting = false;
    return true;
}

/* ------------------------------------------------------------------ core 1 */
static void core1_body(void) {
#if defined(FW2_ASAN) && !defined(__EMSCRIPTEN__)
    __sanitizer_finish_switch_fiber(NULL, &s_core[0].asan_bottom, &s_core[0].asan_size);
#endif
    emu_irq_deliver_pending();
    s_core1_entry();
    /* The SDK's core-1 wrapper returns to the bootrom, which waits for the
     * next launch. Nothing more runs on core 1. */
    emu_log("core 1: its entry function returned; core 1 is idle until the next launch");
    s_core[1].running = false;
    s_core[1].waiting = false;
    switch_to(0, true);
    emu_fatal("core 1 resumed after it stopped");
}

#ifdef __EMSCRIPTEN__
static void core1_fiber_entry(void *arg) { (void)arg; core1_body(); }
#endif

static void free_dead_stack(void) {
    if (!s_dead_stack) return;
#ifdef FW2_ASAN
    ASAN_UNPOISON_MEMORY_REGION(s_dead_stack, CORE1_STACK);
#endif
    free(s_dead_stack);
    s_dead_stack = NULL;
}

void multicore_launch_core1(void (*entry)(void)) {
    if (s_cur != 0) emu_fatal("multicore_launch_core1 called from core 1");
    if (s_core[1].running) {
        emu_log("multicore_launch_core1: core 1 is already running. On the board this call "
                "waits forever for core 1's bootrom; reset it first with multicore_reset_core1()");
        for (;;) tight_loop_contents();
    }
    if (s_core[1].stack) { s_dead_stack = s_core[1].stack; s_core[1].stack = NULL; }
    free_dead_stack();
    core_t *c = &s_core[1];
    memset(c->fifo, 0, sizeof c->fifo);
    c->fifo_n = c->fifo_r = 0;
    c->fifo_st = 0;
    c->irq_off = false;
    c->waiting = false;
    c->stack_size = CORE1_STACK;
    c->stack = malloc(c->stack_size);
    if (!c->stack) emu_fatal("no memory for core 1's stack");
    c->asan_bottom = c->stack;
    c->asan_size = c->stack_size;
    s_core1_entry = entry;
#ifdef __EMSCRIPTEN__
    if (!s_core[0].astack) {
        s_core[0].astack = malloc(ASYNCIFY_STACK);
        emscripten_fiber_init_from_current_context(&s_core[0].fiber, s_core[0].astack, ASYNCIFY_STACK);
    }
    if (!c->astack) c->astack = malloc(ASYNCIFY_STACK);
    emscripten_fiber_init(&c->fiber, core1_fiber_entry, NULL, c->stack, c->stack_size, c->astack, ASYNCIFY_STACK);
#else
    getcontext(&c->ctx);
    c->ctx.uc_stack.ss_sp = c->stack;
    c->ctx.uc_stack.ss_size = c->stack_size;
    c->ctx.uc_link = NULL;
    makecontext(&c->ctx, core1_body, 0);
#endif
    c->running = true;
    emu_log("core 1: launched");
}

void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes) {
    (void)stack_bottom; (void)stack_size_bytes;   /* host code gets a host-sized stack */
    multicore_launch_core1(entry);
}
void multicore_launch_core1_raw(void (*entry)(void), uint32_t *sp, uint32_t vector_table) {
    (void)sp; (void)vector_table;
    multicore_launch_core1(entry);
}

void multicore_reset_core1(void) {
    if (s_cur != 0) emu_fatal("multicore_reset_core1 called from core 1");
    if (s_core[1].running) emu_log("core 1: reset");
    s_core[1].running = false;
    s_core[1].waiting = false;
    s_locked_out[1] = false;
    s_victim[1] = false;
    s_dead_stack = s_core[1].stack;        /* parked mid-call; never resumed */
    s_core[1].stack = NULL;
    free_dead_stack();
    /* The reset also empties core 1's side of the FIFOs. */
    s_core[1].fifo_n = s_core[1].fifo_r = 0;
}

/* ------------------------------------------------------------------- FIFOs */
/* s_core[c].fifo holds the words core c will read: the other core writes
 * them. 4 entries each way on the RP2350 (8 on the RP2040). */
bool multicore_fifo_rvalid(void) { emu_poll(); return s_core[s_cur].fifo_n != 0; }
bool multicore_fifo_wready(void) { emu_poll(); return s_core[s_cur ^ 1u].fifo_n < 4u; }

uint32_t multicore_fifo_get_status(void) {
    emu_poll();
    uint32_t st = s_core[s_cur].fifo_st;
    if (s_core[s_cur].fifo_n) st |= SIO_FIFO_ST_VLD_BITS;
    if (s_core[s_cur ^ 1u].fifo_n < 4u) st |= SIO_FIFO_ST_RDY_BITS;
    return st;
}

static void fifo_put(uint32_t v) {
    unsigned o = s_cur ^ 1u;
    core_t *rx = &s_core[o];
    if (rx->fifo_n >= 4u) { s_core[s_cur].fifo_st |= SIO_FIFO_ST_WOF_BITS; return; }
    rx->fifo[(rx->fifo_r + rx->fifo_n) % 4u] = v;
    rx->fifo_n++;
    emu_irq_raise_core(o, SIO_IRQ_FIFO);
}

static uint32_t fifo_get(void) {
    core_t *me = &s_core[s_cur];
    if (!me->fifo_n) { me->fifo_st |= SIO_FIFO_ST_ROE_BITS; return 0; }
    uint32_t v = me->fifo[me->fifo_r];
    me->fifo_r = (me->fifo_r + 1u) % 4u;
    me->fifo_n--;
    return v;
}

void multicore_fifo_push_blocking(uint32_t data) {
    while (s_core[s_cur ^ 1u].fifo_n >= 4u) tight_loop_contents();
    fifo_put(data);
}
bool multicore_fifo_push_timeout_us(uint32_t data, uint64_t timeout_us) {
    uint64_t end = emu_time_us() + timeout_us;
    while (s_core[s_cur ^ 1u].fifo_n >= 4u) {
        if (emu_time_us() >= end) return false;
        tight_loop_contents();
    }
    fifo_put(data);
    return true;
}
uint32_t multicore_fifo_pop_blocking(void) {
    while (!s_core[s_cur].fifo_n) tight_loop_contents();
    return fifo_get();
}
bool multicore_fifo_pop_timeout_us(uint64_t timeout_us, uint32_t *out) {
    uint64_t end = emu_time_us() + timeout_us;
    while (!s_core[s_cur].fifo_n) {
        if (emu_time_us() >= end) return false;
        tight_loop_contents();
    }
    *out = fifo_get();
    return true;
}
void multicore_fifo_drain(void) { s_core[s_cur].fifo_n = s_core[s_cur].fifo_r = 0; }
void multicore_fifo_clear_irq(void) { s_core[s_cur].fifo_st = 0; }

/* ----------------------------------------------------------------- lockout */
/* On the board the victim core sits in an interrupt handler until the
 * lockout ends. Here it simply isn't given the host thread. */
void multicore_lockout_victim_init(void) { s_victim[s_cur] = true; }
void multicore_lockout_victim_deinit(void) { s_victim[s_cur] = false; }
bool multicore_lockout_victim_is_initialized(uint core_num) { return core_num < 2 && s_victim[core_num]; }

static bool lockout_start(uint64_t timeout_us, bool forever) {
    unsigned o = s_cur ^ 1u;
    if (!s_victim[o] || !s_core[o].running) {
        if (forever) {
            emu_log("multicore_lockout_start_blocking: core %u never called multicore_lockout_victim_init(), "
                    "so on the board this waits forever", o);
            for (;;) tight_loop_contents();
        }
        emu_sleep_us(timeout_us);
        return false;
    }
    s_locked_out[o] = true;
    return true;
}
void multicore_lockout_start_blocking(void) { lockout_start(0, true); }
bool multicore_lockout_start_timeout_us(uint64_t timeout_us) { return lockout_start(timeout_us, false); }
void multicore_lockout_end_blocking(void) { s_locked_out[s_cur ^ 1u] = false; }
bool multicore_lockout_end_timeout_us(uint64_t timeout_us) { (void)timeout_us; multicore_lockout_end_blocking(); return true; }

/* --------------------------------------------------------------- doorbells */
void multicore_doorbell_claim(uint n, uint core_mask) {
    for (unsigned c = 0; c < 2; c++)
        if (core_mask & (1u << c)) {
            if (s_bell_claimed[c] & (1u << n)) emu_fatal("doorbell %u already claimed", n);
            s_bell_claimed[c] |= (uint8_t)(1u << n);
        }
}
int multicore_doorbell_claim_unused(uint core_mask, bool required) {
    for (uint n = 0; n < NUM_DOORBELLS; n++) {
        bool free_ = true;
        for (unsigned c = 0; c < 2; c++)
            if ((core_mask & (1u << c)) && (s_bell_claimed[c] & (1u << n))) free_ = false;
        if (free_) { multicore_doorbell_claim(n, core_mask); return (int)n; }
    }
    if (required) emu_fatal("no unused doorbell");
    return -1;
}
void multicore_doorbell_unclaim(uint n, uint core_mask) {
    for (unsigned c = 0; c < 2; c++) if (core_mask & (1u << c)) s_bell_claimed[c] &= (uint8_t)~(1u << n);
}
void multicore_doorbell_set_other_core(uint n) {
    unsigned o = s_cur ^ 1u;
    s_core[o].bells |= (uint8_t)(1u << n);
    emu_irq_raise_core(o, SIO_IRQ_BELL);
}
void multicore_doorbell_clear_other_core(uint n) { s_core[s_cur ^ 1u].bells &= (uint8_t)~(1u << n); }
void multicore_doorbell_set_current_core(uint n) {
    s_core[s_cur].bells |= (uint8_t)(1u << n);
    emu_irq_raise_core(s_cur, SIO_IRQ_BELL);
}
void multicore_doorbell_clear_current_core(uint n) { s_core[s_cur].bells &= (uint8_t)~(1u << n); }
bool multicore_doorbell_is_set_current_core(uint n) { emu_poll(); return (s_core[s_cur].bells >> n) & 1u; }
bool multicore_doorbell_is_set_other_core(uint n) { emu_poll(); return (s_core[s_cur ^ 1u].bells >> n) & 1u; }

/* -------------------------------------------------------- interrupt mask */
uint32_t save_and_disable_interrupts(void) {
    uint32_t was = s_core[s_cur].irq_off ? 1u : 0u;
    s_core[s_cur].irq_off = true;
    return was;
}
void restore_interrupts(uint32_t status) {
    s_core[s_cur].irq_off = (status & 1u) != 0;
    if (!s_core[s_cur].irq_off) emu_irq_deliver_pending();
}

/* ---------------------------------------------------------------- board id */
static uint8_t s_board_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES] = { 0xE6, 0x61, 0x64, 0x08, 0x43, 0x2A, 0x7B, 0x15 };

bool emu_set_board_id(const char *hex) {
    uint8_t id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
    if (strlen(hex) != 2u * sizeof id) return false;
    for (size_t i = 0; i < sizeof id; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return false;
        id[i] = (uint8_t)v;
    }
    memcpy(s_board_id, id, sizeof id);
    return true;
}
void pico_get_unique_board_id(pico_unique_board_id_t *id_out) { memcpy(id_out->id, s_board_id, sizeof s_board_id); }
void pico_get_unique_board_id_string(char *id_out, uint len) {
    if (!len) return;
    size_t n = 0;
    for (size_t i = 0; i < sizeof s_board_id && n + 2 < len; i++, n += 2)
        snprintf(id_out + n, 3, "%02X", s_board_id[i]);
    id_out[n] = '\0';
}

/* ======================================================= pico/rand.h */
#include "pico/rand.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
static void host_random(void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(emscripten_random() * 256.0);
}
#else
#include <sys/random.h>
static void host_random(void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    while (n) {
        ssize_t got = getrandom(p, n, 0);
        if (got <= 0) { for (; n; n--) *p++ = (uint8_t)rand(); break; }
        p += got;
        n -= (size_t)got;
    }
}
#endif
void get_rand_128(rng_128_t *r) { host_random(r, sizeof *r); }
uint64_t get_rand_64(void) { uint64_t v; host_random(&v, sizeof v); return v; }
uint32_t get_rand_32(void) { uint32_t v; host_random(&v, sizeof v); return v; }
