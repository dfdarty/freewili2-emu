/* hardware/uart.h — host shim. UART1 is the display CPU's link to the
 * board-manager coprocessor (buttons, charger, power zones); the emulator
 * models that coprocessor at the byte level. */
#ifndef FW2EMU_HARDWARE_UART_H
#define FW2EMU_HARDWARE_UART_H

#include "pico.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile uint32_t dr;
    volatile uint32_t rsr;
    uint32_t _pad0[4];
    volatile uint32_t fr;
    uint32_t _pad1;
    volatile uint32_t ilpr;
    volatile uint32_t ibrd;
    volatile uint32_t fbrd;
    volatile uint32_t lcr_h;
    volatile uint32_t cr;
    volatile uint32_t ifls;
    volatile uint32_t imsc;
    volatile uint32_t ris;
    volatile uint32_t mis;
    volatile uint32_t icr;
    volatile uint32_t dmacr;
} uart_hw_t;

#define EMU_UART_FIFO 4096
typedef struct uart_inst {
    uart_hw_t hw;
    unsigned  index;
    uint32_t  baud;
    bool      enabled;
    /* RX FIFO used when no DMA channel is draining the UART */
    uint8_t   fifo[EMU_UART_FIFO];
    unsigned  head, tail;
} uart_inst_t;

extern uart_inst_t emu_uart_inst[2];
#define uart0 (&emu_uart_inst[0])
#define uart1 (&emu_uart_inst[1])

typedef enum { UART_PARITY_NONE, UART_PARITY_EVEN, UART_PARITY_ODD } uart_parity_t;

static inline uart_hw_t *uart_get_hw(uart_inst_t *uart) { return &uart->hw; }
static inline uint uart_get_index(uart_inst_t *uart) { return uart->index; }

uint uart_init(uart_inst_t *uart, uint baudrate);
void uart_deinit(uart_inst_t *uart);
uint uart_set_baudrate(uart_inst_t *uart, uint baudrate);
static inline void uart_set_format(uart_inst_t *u, uint data_bits, uint stop_bits, uart_parity_t parity) {
    (void)u; (void)data_bits; (void)stop_bits; (void)parity;
}
static inline void uart_set_hw_flow(uart_inst_t *u, bool cts, bool rts) { (void)u; (void)cts; (void)rts; }
static inline void uart_set_fifo_enabled(uart_inst_t *u, bool en) { (void)u; (void)en; }
static inline void uart_set_translate_crlf(uart_inst_t *u, bool en) { (void)u; (void)en; }
static inline void uart_set_irq_enables(uart_inst_t *u, bool rx, bool tx) { (void)u; (void)rx; (void)tx; }
static inline bool uart_is_enabled(uart_inst_t *u) { return u->enabled; }
static inline bool uart_is_writable(uart_inst_t *u) { (void)u; return true; }
static inline void uart_tx_wait_blocking(uart_inst_t *u) { (void)u; }
bool uart_is_readable(uart_inst_t *uart);
bool uart_is_readable_within_us(uart_inst_t *uart, uint32_t us);
void uart_set_break(uart_inst_t *uart, bool en);
void uart_write_blocking(uart_inst_t *uart, const uint8_t *src, size_t len);
void uart_read_blocking(uart_inst_t *uart, uint8_t *dst, size_t len);
char uart_getc(uart_inst_t *uart);
static inline void uart_putc_raw(uart_inst_t *uart, char c) { uart_write_blocking(uart, (const uint8_t *)&c, 1); }
static inline void uart_putc(uart_inst_t *uart, char c) { uart_putc_raw(uart, c); }
static inline void uart_puts(uart_inst_t *uart, const char *s) { while (*s) uart_putc(uart, *s++); }
uint uart_get_dreq(uart_inst_t *uart, bool is_tx);
static inline uint uart_get_dreq_num(uart_inst_t *uart, bool is_tx) { return uart_get_dreq(uart, is_tx); }

#ifdef __cplusplus
}
#endif

#endif
