/* dev_touch.c — FT6336U capacitive touch controller model (I2C1 @ 0x38).
 *
 * Register-level: the BSP reads TD_STATUS (0x02) and the P1/P2 point latches
 * in one burst, exactly as on hardware. Screen coordinates from the mouse are
 * converted back to the chip's native orientation, the inverse of
 * ft6336_map_point() (screen X = chip Y, screen Y = 319 - chip X).
 * Powered by zone 2 (DISPLAY) together with the panel. */
#include "emu/emu.h"

#include <string.h>

#define FT_ADDR 0x38
#define FT_CHIP_ID 0x64

/* Contact state as the chip reports it, plus a small queue of pending
 * transitions. Each press or lift must be seen by the app (at least
 * SEEN_READS reads and a finger-like minimum time) before the next one is
 * applied. Real touches can't be shorter than the controller's report
 * period, and this keeps quick clicks — and quick double-taps — intact when
 * the host stalls the emulator (e.g. a busy browser tab). */
#define SEEN_READS   2u
#define MIN_DOWN_US  60000u
#define MIN_UP_US    20000u
#define STAGE_CAP_US 1000000u
#define QLEN 16

typedef struct { bool down; int x, y; } tev_t;

static struct {
    uint8_t  ptr;
    uint8_t  regs[256];
    bool     down;
    int      x, y;
    unsigned reads;          /* reads of the current state */
    uint64_t since_us;       /* when the current state began */
    tev_t    q[QLEN];
    int      qh, qn;
} T;

static void advance(void) {
    while (T.qn) {
        uint64_t held = emu_time_us() - T.since_us;
        uint64_t min_us = T.down ? MIN_DOWN_US : MIN_UP_US;
        if (!((T.reads >= SEEN_READS && held >= min_us) || held >= STAGE_CAP_US)) return;
        tev_t e = T.q[T.qh];
        T.qh = (T.qh + 1) % QLEN;
        T.qn--;
        if (emu_verbose && T.down && !e.down)
            emu_log("touch: lift after %u reads, %llu ms", T.reads, (unsigned long long)(held / 1000));
        T.down = e.down; T.x = e.x; T.y = e.y;
        T.reads = 0;
        T.since_us = emu_time_us();
    }
}

static void push(bool down, int x, int y) {
    if (T.qn == QLEN) return;
    T.q[(T.qh + T.qn) % QLEN] = (tev_t){ down, x, y };
    T.qn++;
}

static void refresh_regs(void) {
    memset(&T.regs[0x02], 0, 11);
    if (!T.down) { T.regs[0x03] = 0xFF; T.regs[0x05] = 0xFF; return; }
    int chip_x = (EMU_LCD_H - 1) - T.y;
    int chip_y = T.x;
    T.regs[0x02] = 1;                                   /* one point */
    T.regs[0x03] = (uint8_t)(0x80 | ((chip_x >> 8) & 0x0F)); /* event: contact */
    T.regs[0x04] = (uint8_t)chip_x;
    T.regs[0x05] = (uint8_t)((chip_y >> 8) & 0x0F);     /* touch id 0 */
    T.regs[0x06] = (uint8_t)chip_y;
    T.regs[0x07] = 0x40;                                /* weight */
    T.regs[0x08] = 0x10;                                /* area */
}

static bool present(emu_i2c_device_t *d) { (void)d; return emu_rail_on(2); }

static bool ft_write(emu_i2c_device_t *d, const uint8_t *b, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (n == 0) return true;
    T.ptr = b[0];
    for (size_t i = 1; i < n; i++) T.regs[T.ptr++] = b[i];
    return true;
}

static bool ft_read(emu_i2c_device_t *d, uint8_t *dst, size_t n, bool nostop) {
    (void)d; (void)nostop;
    refresh_regs();
    T.reads++;
    advance();           /* after the read: this report showed the current state */
    for (size_t i = 0; i < n; i++) dst[i] = T.regs[(uint8_t)(T.ptr + i)];
    return true;
}

static emu_i2c_device_t s_dev = { .name = "ft6336", .addr = FT_ADDR, .write = ft_write,
                                  .read = ft_read, .present = present };

void emu_touch_init(void) {
    memset(&T, 0, sizeof T);
    T.since_us = emu_time_us();
    T.regs[0xA3] = FT_CHIP_ID;
    T.regs[0xA6] = 0x10;    /* firmware version */
    T.regs[0xA8] = 0x11;    /* vendor id (FocalTech) */
    emu_i2c_attach(1, &s_dev);
}

void emu_touch_set(int x, int y, bool down) {
    if (x < 0) x = 0; else if (x >= EMU_LCD_W) x = EMU_LCD_W - 1;
    if (y < 0) y = 0; else if (y >= EMU_LCD_H) y = EMU_LCD_H - 1;
    if (down) {
        tev_t *last = T.qn ? &T.q[(T.qh + T.qn - 1) % QLEN] : NULL;
        if (last && last->down) { last->x = x; last->y = y; return; }     /* drag while queued */
        if (!last && T.down) { T.x = x; T.y = y; return; }                /* plain drag */
        push(true, x, y);
    } else {
        tev_t *last = T.qn ? &T.q[(T.qh + T.qn - 1) % QLEN] : NULL;
        bool will_be_down = last ? last->down : T.down;
        if (!will_be_down) return;
        push(false, x, y);
    }
    advance();
}

void emu_touch_task(void) { advance(); }

/* Where the finger is from the user's point of view (latest input). */
bool emu_touch_get(int *x, int *y) {
    const tev_t *last = T.qn ? &T.q[(T.qh + T.qn - 1) % QLEN] : NULL;
    if (x) *x = last ? last->x : T.x;
    if (y) *y = last ? last->y : T.y;
    return last ? last->down : T.down;
}
