/* hardware/address_mapped.h — host shim. The set/clear/xor aliases of RP2350
 * registers become plain read-modify-writes on the emulated register blocks. */
#ifndef FW2EMU_HARDWARE_ADDRESS_MAPPED_H
#define FW2EMU_HARDWARE_ADDRESS_MAPPED_H
#include "pico.h"
typedef volatile uint32_t io_rw_32;
typedef const volatile uint32_t io_ro_32;
typedef volatile uint32_t io_wo_32;
static inline void hw_set_bits(io_rw_32 *addr, uint32_t mask) { *addr |= mask; }
static inline void hw_clear_bits(io_rw_32 *addr, uint32_t mask) { *addr &= ~mask; }
static inline void hw_xor_bits(io_rw_32 *addr, uint32_t mask) { *addr ^= mask; }
static inline void hw_write_masked(io_rw_32 *addr, uint32_t values, uint32_t mask) {
    *addr = (*addr & ~mask) | (values & mask);
}
#endif
