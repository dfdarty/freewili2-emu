/* hardware/i2c.h — host shim. Transfers go to emulated devices by address
 * (FT6336 touch, PCAL6524 IO expander, ...). Absent addresses NAK. */
#ifndef FW2EMU_HARDWARE_I2C_H
#define FW2EMU_HARDWARE_I2C_H

#include "pico.h"
#include "pico/time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct i2c_inst {
    unsigned index;
    uint32_t baud;
    bool     enabled;
} i2c_inst_t;

extern i2c_inst_t emu_i2c_inst[2];
#define i2c0 (&emu_i2c_inst[0])
#define i2c1 (&emu_i2c_inst[1])

uint i2c_init(i2c_inst_t *i2c, uint baudrate);
void i2c_deinit(i2c_inst_t *i2c);
uint i2c_set_baudrate(i2c_inst_t *i2c, uint baudrate);
static inline uint i2c_get_index(i2c_inst_t *i2c) { return i2c->index; }
static inline void i2c_set_slave_mode(i2c_inst_t *i2c, bool slave, uint8_t addr) { (void)i2c; (void)slave; (void)addr; }

int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop);
int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop);
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, uint timeout_us);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, uint timeout_us);
static inline int i2c_write_blocking_until(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, absolute_time_t until) {
    (void)until; return i2c_write_blocking(i2c, addr, src, len, nostop);
}
static inline int i2c_read_blocking_until(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, absolute_time_t until) {
    (void)until; return i2c_read_blocking(i2c, addr, dst, len, nostop);
}
static inline int i2c_write_timeout_per_char_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, uint t) {
    (void)t; return i2c_write_blocking(i2c, addr, src, len, nostop);
}
static inline int i2c_read_timeout_per_char_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, uint t) {
    (void)t; return i2c_read_blocking(i2c, addr, dst, len, nostop);
}

#ifdef __cplusplus
}
#endif

#endif
