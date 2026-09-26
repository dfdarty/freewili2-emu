/* hardware/dma.h — host shim.
 *
 * Transfers complete immediately (no bus timing is modelled):
 *   memory -> SPI DR    bytes go to the SPI device, then DMA_IRQ_0 fires
 *   UART DR -> memory   an armed channel receives UART bytes into its ring
 *   memory -> memory    plain copy
 * Register fields are pointer-sized on the host so drivers that do
 * `hw->write_addr - (uintptr_t)buf` keep working on 64-bit machines. */
#ifndef FW2EMU_HARDWARE_DMA_H
#define FW2EMU_HARDWARE_DMA_H

#include "pico.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_DMA_CHANNELS 16

enum dma_channel_transfer_size { DMA_SIZE_8 = 0, DMA_SIZE_16 = 1, DMA_SIZE_32 = 2 };

/* DREQ numbers (RP2350) */
#define DREQ_PIO0_TX0 0
#define DREQ_PIO0_RX0 8
#define DREQ_PIO1_TX0 16
#define DREQ_PIO1_RX0 24
#define DREQ_PIO2_TX0 32
#define DREQ_PIO2_RX0 40
#define DREQ_SPI0_TX 48
#define DREQ_SPI0_RX 49
#define DREQ_SPI1_TX 50
#define DREQ_SPI1_RX 51
#define DREQ_UART0_TX 52
#define DREQ_UART0_RX 53
#define DREQ_UART1_TX 54
#define DREQ_UART1_RX 55
#define DREQ_PWM_WRAP0 56
#define DREQ_I2C0_TX 68
#define DREQ_I2C0_RX 69
#define DREQ_I2C1_TX 70
#define DREQ_I2C1_RX 71
#define DREQ_ADC 72
#define DREQ_HSTX 52
#define DREQ_FORCE 63

#define DMA_CH0_TRANS_COUNT_MODE_LSB 28u
#define DMA_CH0_TRANS_COUNT_MODE_BITS 0xf0000000u
#define DMA_CH0_TRANS_COUNT_MODE_VALUE_NORMAL 0x0u
#define DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF 0x1u
#define DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS 0xfu

typedef struct {
    uint8_t  size;
    bool     read_inc, write_inc;
    uint8_t  dreq;
    bool     ring_write;
    uint8_t  ring_bits;
    uint8_t  chain_to;
    bool     enable;
    bool     irq_quiet;
    bool     bswap;
    bool     high_priority;
} dma_channel_config;

typedef struct {
    volatile uintptr_t read_addr;
    volatile uintptr_t write_addr;
    volatile uint32_t  transfer_count;
    volatile uint32_t  ctrl_trig;
    volatile uint32_t  al1_ctrl;
    volatile uintptr_t al1_read_addr;
    volatile uintptr_t al1_write_addr;
    volatile uint32_t  al1_transfer_count_trig;
    volatile uint32_t  al2_ctrl;
    volatile uint32_t  al2_transfer_count;
    volatile uintptr_t al2_read_addr;
    volatile uintptr_t al2_write_addr_trig;
    volatile uint32_t  al3_ctrl;
    volatile uintptr_t al3_write_addr;
    volatile uint32_t  al3_transfer_count;
    volatile uintptr_t al3_read_addr_trig;
} dma_channel_hw_t;

dma_channel_hw_t *dma_channel_hw_addr(uint channel);

dma_channel_config dma_channel_get_default_config(uint channel);
dma_channel_config dma_get_channel_config(uint channel);
static inline void channel_config_set_read_increment(dma_channel_config *c, bool inc) { c->read_inc = inc; }
static inline void channel_config_set_write_increment(dma_channel_config *c, bool inc) { c->write_inc = inc; }
static inline void channel_config_set_dreq(dma_channel_config *c, uint dreq) { c->dreq = (uint8_t)dreq; }
static inline void channel_config_set_chain_to(dma_channel_config *c, uint ch) { c->chain_to = (uint8_t)ch; }
static inline void channel_config_set_transfer_data_size(dma_channel_config *c, enum dma_channel_transfer_size s) { c->size = (uint8_t)s; }
static inline void channel_config_set_ring(dma_channel_config *c, bool write, uint size_bits) { c->ring_write = write; c->ring_bits = (uint8_t)size_bits; }
static inline void channel_config_set_bswap(dma_channel_config *c, bool b) { c->bswap = b; }
static inline void channel_config_set_irq_quiet(dma_channel_config *c, bool q) { c->irq_quiet = q; }
static inline void channel_config_set_enable(dma_channel_config *c, bool e) { c->enable = e; }
static inline void channel_config_set_high_priority(dma_channel_config *c, bool h) { c->high_priority = h; }
static inline void channel_config_set_sniff_enable(dma_channel_config *c, bool s) { (void)c; (void)s; }

int  dma_claim_unused_channel(bool required);
void dma_channel_claim(uint channel);
void dma_channel_unclaim(uint channel);
bool dma_channel_is_claimed(uint channel);

void dma_channel_configure(uint channel, const dma_channel_config *config,
                           volatile void *write_addr, const volatile void *read_addr,
                           uint transfer_count, bool trigger);
void dma_channel_set_config(uint channel, const dma_channel_config *config, bool trigger);
void dma_channel_set_read_addr(uint channel, const volatile void *read_addr, bool trigger);
void dma_channel_set_write_addr(uint channel, volatile void *write_addr, bool trigger);
void dma_channel_set_trans_count(uint channel, uint32_t count, bool trigger);
void dma_channel_transfer_from_buffer_now(uint channel, const volatile void *read_addr, uint32_t count);
void dma_channel_transfer_to_buffer_now(uint channel, volatile void *write_addr, uint32_t count);
void dma_channel_start(uint channel);
void dma_channel_abort(uint channel);
bool dma_channel_is_busy(uint channel);
void dma_channel_wait_for_finish_blocking(uint channel);

void dma_channel_set_irq0_enabled(uint channel, bool enabled);
void dma_channel_set_irq1_enabled(uint channel, bool enabled);
bool dma_channel_get_irq0_status(uint channel);
bool dma_channel_get_irq1_status(uint channel);
void dma_channel_acknowledge_irq0(uint channel);
void dma_channel_acknowledge_irq1(uint channel);

#ifdef __cplusplus
}
#endif

#endif
