/* dev_ioexp.c — PCAL6524 I/O expander model (I2C1 @ 0x23).
 *
 * Tracks the output ports the BSP writes: antenna switch, mic / IR / USB
 * host power, USB D+ pull-up and the GPIO header voltage reference. Other
 * models can query them (e.g. header GPIO is dead until a VREF is chosen,
 * as on hardware). Powered by zone 1 (SENSORS). */
#include "emu/emu.h"

#include <string.h>

static struct {
    uint8_t ptr;
    uint8_t out[3];
    uint8_t cfg[3];
} X;

static const char *vref_name(uint8_t p2) {
    switch (p2 & 0x78) {
    case 0x40: return "3.3V";
    case 0x20: return "5.0V";
    case 0x10: return "programmable Vout";
    case 0x08: return "external Trig_IN/VREF pin";
    default:   return "none";
    }
}

static bool present(emu_i2c_device_t *d) { (void)d; return emu_rail_on(1); }

static bool x_write(emu_i2c_device_t *d, const uint8_t *b, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (!n) return true;
    X.ptr = b[0];
    uint8_t before[3];
    memcpy(before, X.out, 3);
    for (size_t i = 1; i < n; i++, X.ptr++) {
        if (X.ptr >= 0x04 && X.ptr <= 0x06) X.out[X.ptr - 0x04] = b[i];
        else if (X.ptr >= 0x0C && X.ptr <= 0x0E) X.cfg[X.ptr - 0x0C] = b[i];
    }
    if (emu_verbose && memcmp(before, X.out, 3))
        emu_log("ioexp: P0=%02x P1=%02x P2=%02x (VREF %s)", X.out[0], X.out[1], X.out[2], vref_name(X.out[2]));
    return true;
}

static bool x_read(emu_i2c_device_t *d, uint8_t *dst, size_t n, bool nostop) {
    (void)d; (void)nostop;
    for (size_t i = 0; i < n; i++, X.ptr++) {
        if (X.ptr <= 0x02) dst[i] = X.out[X.ptr];         /* inputs read back outputs */
        else if (X.ptr >= 0x04 && X.ptr <= 0x06) dst[i] = X.out[X.ptr - 0x04];
        else if (X.ptr >= 0x0C && X.ptr <= 0x0E) dst[i] = X.cfg[X.ptr - 0x0C];
        else dst[i] = 0;
    }
    return true;
}

static emu_i2c_device_t s_dev = { .name = "pcal6524", .addr = 0x23, .write = x_write,
                                  .read = x_read, .present = present };

void emu_ioexp_init(void) {
    memset(&X, 0, sizeof X);
    memset(X.cfg, 0xFF, sizeof X.cfg);
    emu_i2c_attach(1, &s_dev);
}

uint8_t emu_ioexp_port(unsigned port) { return port < 3 ? X.out[port] : 0; }

/* True when the pin is configured as an output (CFG bit 0) driving high and
 * the expander itself is powered — i.e. the rail it switches is live. */
bool emu_ioexp_output_high(unsigned port, unsigned bit) {
    if (port >= 3 || bit >= 8 || !emu_rail_on(1)) return false;
    uint8_t m = (uint8_t)(1u << bit);
    return !(X.cfg[port] & m) && (X.out[port] & m);
}
