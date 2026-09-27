/* hardware/spi.h — host shim. Bytes written to SPI go to whichever emulated
 * device has its chip select asserted (e.g. the ST7796 LCD model). The
 * register block exists so `&spi_get_hw(spi)->dr` can serve as a DMA target. */
#ifndef FW2EMU_HARDWARE_SPI_H
#define FW2EMU_HARDWARE_SPI_H

#include "pico.h"
#include "hardware/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile uint32_t cr0;
    volatile uint32_t cr1;
    volatile uint32_t dr;
    volatile uint32_t sr;
    volatile uint32_t cpsr;
    volatile uint32_t imsc;
    volatile uint32_t ris;
    volatile uint32_t mis;
    volatile uint32_t icr;
    volatile uint32_t dmacr;
} spi_hw_t;

typedef struct spi_inst {
    spi_hw_t hw;
    unsigned index;
    uint32_t baud;
} spi_inst_t;

extern spi_inst_t emu_spi_inst[2];
#define spi0 (&emu_spi_inst[0])
#define spi1 (&emu_spi_inst[1])

#define SPI_SSPSR_BSY_BITS 0x00000010u
#define SPI_SSPSR_RFF_BITS 0x00000008u
#define SPI_SSPSR_RNE_BITS 0x00000004u
#define SPI_SSPSR_TNF_BITS 0x00000002u
#define SPI_SSPSR_TFE_BITS 0x00000001u

typedef enum { SPI_CPHA_0 = 0, SPI_CPHA_1 = 1 } spi_cpha_t;
typedef enum { SPI_CPOL_0 = 0, SPI_CPOL_1 = 1 } spi_cpol_t;
typedef enum { SPI_LSB_FIRST = 0, SPI_MSB_FIRST = 1 } spi_order_t;

static inline spi_hw_t *spi_get_hw(spi_inst_t *spi) { return &spi->hw; }
static inline const spi_hw_t *spi_get_const_hw(const spi_inst_t *spi) { return &spi->hw; }
static inline uint spi_get_index(const spi_inst_t *spi) { return spi->index; }

uint spi_init(spi_inst_t *spi, uint baudrate);
void spi_deinit(spi_inst_t *spi);
uint spi_set_baudrate(spi_inst_t *spi, uint baudrate);
uint spi_get_baudrate(const spi_inst_t *spi);
static inline void spi_set_format(spi_inst_t *spi, uint data_bits, spi_cpol_t cpol, spi_cpha_t cpha, spi_order_t order) {
    (void)spi; (void)data_bits; (void)cpol; (void)cpha; (void)order;
}
static inline void spi_set_slave(spi_inst_t *spi, bool slave) { (void)spi; (void)slave; }
static inline bool spi_is_writable(const spi_inst_t *spi) { (void)spi; return true; }
static inline bool spi_is_readable(const spi_inst_t *spi) { (void)spi; return false; }
bool spi_is_busy(const spi_inst_t *spi);         /* true while the bus is still shifting */

int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len);
int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx_data, uint8_t *dst, size_t len);
int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *src, uint8_t *dst, size_t len);
int spi_write16_blocking(spi_inst_t *spi, const uint16_t *src, size_t len);

uint spi_get_dreq(spi_inst_t *spi, bool is_tx);

#ifdef __cplusplus
}
#endif

#endif
