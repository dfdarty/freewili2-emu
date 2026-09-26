#ifndef FW2EMU_HARDWARE_RESETS_H
#define FW2EMU_HARDWARE_RESETS_H
#include "pico.h"
#define RESETS_RESET_PIO0_BITS (1u << 11)
#define RESETS_RESET_PIO1_BITS (1u << 12)
#define RESETS_RESET_PIO2_BITS (1u << 13)
#define RESETS_RESET_SPI0_BITS (1u << 18)
#define RESETS_RESET_SPI1_BITS (1u << 19)
#define RESETS_RESET_I2C0_BITS (1u << 4)
#define RESETS_RESET_I2C1_BITS (1u << 5)
#define RESETS_RESET_UART0_BITS (1u << 26)
#define RESETS_RESET_UART1_BITS (1u << 27)
#define RESETS_RESET_HSTX_BITS (1u << 3)
#define RESETS_RESET_DMA_BITS (1u << 2)
#define RESETS_RESET_USBCTRL_BITS (1u << 28)
static inline void reset_block(uint32_t bits) { (void)bits; }
static inline void unreset_block(uint32_t bits) { (void)bits; }
static inline void unreset_block_wait(uint32_t bits) { (void)bits; }
static inline void reset_unreset_block_wait(uint32_t bits) { (void)bits; }
static inline void reset_block_mask(uint32_t bits) { (void)bits; }
static inline void unreset_block_mask_wait_blocking(uint32_t bits) { (void)bits; }
#endif
