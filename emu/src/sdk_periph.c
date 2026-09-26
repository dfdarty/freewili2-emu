/* sdk_periph.c — host implementations of the Pico SDK peripheral APIs:
 * GPIO, SPI, I2C, UART, DMA, IRQ, PIO, clocks, PSRAM, watchdog.
 * Each one forwards to the attached device models. */
#include "emu/emu.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/psram.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"

#include <string.h>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <sys/mman.h>
#endif

/* ================================================================ GPIO */
typedef struct {
    gpio_function_t fn;
    bool out_dir, out_level, pull_up, pull_down;
    int  ext;                 /* level driven by a model on an input, -1 = none */
    uint outover;
} pin_t;

static pin_t s_pin[EMU_NUM_GPIO];

#define MAX_WATCH 32
static struct { unsigned pin; emu_gpio_out_cb cb; void *ctx; } s_watch[MAX_WATCH];
static int s_nwatch;

static bool valid_pin(uint g) { return g < EMU_NUM_GPIO; }

__attribute__((constructor)) static void gpio_boot(void) {
    for (int i = 0; i < EMU_NUM_GPIO; i++) { s_pin[i].fn = GPIO_FUNC_NULL; s_pin[i].ext = -1; }
}

void emu_gpio_watch(unsigned pin, emu_gpio_out_cb cb, void *ctx) {
    if (s_nwatch < MAX_WATCH) s_watch[s_nwatch++] = (typeof(s_watch[0])){ pin, cb, ctx };
}

bool emu_gpio_out_level(unsigned pin) { return valid_pin(pin) && s_pin[pin].out_level; }

void emu_gpio_drive_input(unsigned pin, int level) { if (valid_pin(pin)) s_pin[pin].ext = level; }

static void set_out(uint g, bool v) {
    if (!valid_pin(g)) return;
    bool old = s_pin[g].out_level;
    s_pin[g].out_level = v;
    if (old != v)
        for (int i = 0; i < s_nwatch; i++)
            if (s_watch[i].pin == g) s_watch[i].cb(g, v, s_watch[i].ctx);
}

void gpio_init(uint g) {
    if (!valid_pin(g)) return;
    s_pin[g].fn = GPIO_FUNC_SIO;
    s_pin[g].out_dir = false;
    set_out(g, false);
}
void gpio_deinit(uint g) { if (valid_pin(g)) s_pin[g].fn = GPIO_FUNC_NULL; }
void gpio_init_mask(uint64_t m) { for (uint i = 0; i < EMU_NUM_GPIO; i++) if (m & (1ull << i)) gpio_init(i); }
void gpio_set_function(uint g, gpio_function_t fn) { if (valid_pin(g)) s_pin[g].fn = fn; }
gpio_function_t gpio_get_function(uint g) { return valid_pin(g) ? s_pin[g].fn : GPIO_FUNC_NULL; }
void gpio_set_dir(uint g, bool out) { if (valid_pin(g)) s_pin[g].out_dir = out; }
bool gpio_is_dir_out(uint g) { return valid_pin(g) && s_pin[g].out_dir; }
void gpio_put(uint g, bool v) { set_out(g, v); }
bool gpio_get_out_level(uint g) { return emu_gpio_out_level(g); }
bool gpio_get(uint g) {
    if (!valid_pin(g)) return false;
    const pin_t *p = &s_pin[g];
    if (p->out_dir && p->fn == GPIO_FUNC_SIO) return p->out_level;
    if (p->ext >= 0) return p->ext != 0;
    return p->pull_up;
}
uint64_t gpio_get_all64(void) {
    uint64_t v = 0;
    for (uint i = 0; i < EMU_NUM_GPIO; i++) if (gpio_get(i)) v |= 1ull << i;
    return v;
}
void gpio_set_mask64(uint64_t m) { for (uint i = 0; i < EMU_NUM_GPIO; i++) if (m & (1ull << i)) set_out(i, true); }
void gpio_clr_mask64(uint64_t m) { for (uint i = 0; i < EMU_NUM_GPIO; i++) if (m & (1ull << i)) set_out(i, false); }
void gpio_put_masked64(uint64_t m, uint64_t v) {
    for (uint i = 0; i < EMU_NUM_GPIO; i++) if (m & (1ull << i)) set_out(i, (v >> i) & 1u);
}
void gpio_set_dir_masked64(uint64_t m, uint64_t v) {
    for (uint i = 0; i < EMU_NUM_GPIO; i++) if (m & (1ull << i)) s_pin[i].out_dir = (v >> i) & 1u;
}
void gpio_set_pulls(uint g, bool up, bool down) { if (valid_pin(g)) { s_pin[g].pull_up = up; s_pin[g].pull_down = down; } }
bool gpio_is_pulled_up(uint g) { return valid_pin(g) && s_pin[g].pull_up; }
bool gpio_is_pulled_down(uint g) { return valid_pin(g) && s_pin[g].pull_down; }
void gpio_set_outover(uint g, uint v) { if (valid_pin(g)) s_pin[g].outover = v; }
void gpio_set_inover(uint g, uint v) { (void)g; (void)v; }
void gpio_set_oeover(uint g, uint v) { (void)g; (void)v; }

/* ================================================================= SPI */
spi_inst_t emu_spi_inst[2] = {
    { .hw = { .sr = SPI_SSPSR_TFE_BITS | SPI_SSPSR_TNF_BITS }, .index = 0 },
    { .hw = { .sr = SPI_SSPSR_TFE_BITS | SPI_SSPSR_TNF_BITS }, .index = 1 },
};

#define MAX_SPI_DEV 4
static emu_spi_device_t *s_spi_dev[2][MAX_SPI_DEV];

void emu_spi_attach(unsigned bus, emu_spi_device_t *d) {
    for (int i = 0; i < MAX_SPI_DEV; i++) if (!s_spi_dev[bus][i]) { s_spi_dev[bus][i] = d; return; }
}

static emu_spi_device_t *spi_selected(unsigned bus) {
    for (int i = 0; i < MAX_SPI_DEV; i++) {
        emu_spi_device_t *d = s_spi_dev[bus][i];
        if (d && !emu_gpio_out_level(d->cs_pin)) return d;
    }
    return NULL;
}

void emu_spi_bus_write(unsigned bus, const uint8_t *b, size_t n) {
    emu_spi_device_t *d = spi_selected(bus);
    if (d && d->write) d->write(d, b, n);
}

void emu_spi_bus_read(unsigned bus, uint8_t tx, uint8_t *dst, size_t n) {
    emu_spi_device_t *d = spi_selected(bus);
    if (d && d->read) d->read(d, tx, dst, n);
    else memset(dst, 0xFF, n);
}

uint spi_init(spi_inst_t *s, uint baud) { s->baud = baud; return baud; }
void spi_deinit(spi_inst_t *s) { s->baud = 0; }
uint spi_set_baudrate(spi_inst_t *s, uint baud) { s->baud = baud; return baud; }
uint spi_get_baudrate(const spi_inst_t *s) { return s->baud; }
int spi_write_blocking(spi_inst_t *s, const uint8_t *src, size_t len) {
    emu_spi_bus_write(s->index, src, len);
    return (int)len;
}
int spi_read_blocking(spi_inst_t *s, uint8_t tx, uint8_t *dst, size_t len) {
    emu_spi_bus_read(s->index, tx, dst, len);
    return (int)len;
}
int spi_write_read_blocking(spi_inst_t *s, const uint8_t *src, uint8_t *dst, size_t len) {
    /* Full duplex: devices that care model it in read(); writes first. */
    emu_spi_bus_write(s->index, src, len);
    emu_spi_bus_read(s->index, 0, dst, len);
    return (int)len;
}
int spi_write16_blocking(spi_inst_t *s, const uint16_t *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t b[2] = { (uint8_t)(src[i] >> 8), (uint8_t)src[i] };
        emu_spi_bus_write(s->index, b, 2);
    }
    return (int)len;
}
uint spi_get_dreq(spi_inst_t *s, bool tx) { return s->index ? (tx ? DREQ_SPI1_TX : DREQ_SPI1_RX) : (tx ? DREQ_SPI0_TX : DREQ_SPI0_RX); }

/* ================================================================= I2C */
i2c_inst_t emu_i2c_inst[2] = { { .index = 0 }, { .index = 1 } };

#define MAX_I2C_DEV 12
static emu_i2c_device_t *s_i2c_dev[2][MAX_I2C_DEV];

void emu_i2c_attach(unsigned bus, emu_i2c_device_t *d) {
    for (int i = 0; i < MAX_I2C_DEV; i++) if (!s_i2c_dev[bus][i]) { s_i2c_dev[bus][i] = d; return; }
}

static emu_i2c_device_t *i2c_find(unsigned bus, uint8_t addr) {
    for (int i = 0; i < MAX_I2C_DEV; i++) {
        emu_i2c_device_t *d = s_i2c_dev[bus][i];
        if (d && d->addr == addr && (!d->present || d->present(d))) return d;
    }
    return NULL;
}

int emu_i2c_bus_write(unsigned bus, uint8_t addr, const uint8_t *b, size_t n, bool nostop) {
    emu_i2c_device_t *d = i2c_find(bus, addr);
    if (!d || !emu_i2c_inst[bus].enabled) return PICO_ERROR_GENERIC;
    return d->write && d->write(d, b, n, nostop) ? (int)n : PICO_ERROR_GENERIC;
}

int emu_i2c_bus_read(unsigned bus, uint8_t addr, uint8_t *dst, size_t n, bool nostop) {
    emu_i2c_device_t *d = i2c_find(bus, addr);
    if (!d || !emu_i2c_inst[bus].enabled) return PICO_ERROR_GENERIC;
    return d->read && d->read(d, dst, n, nostop) ? (int)n : PICO_ERROR_GENERIC;
}

uint i2c_init(i2c_inst_t *i, uint baud) { i->baud = baud; i->enabled = true; return baud; }
void i2c_deinit(i2c_inst_t *i) { i->enabled = false; }
uint i2c_set_baudrate(i2c_inst_t *i, uint baud) { i->baud = baud; return baud; }
int i2c_write_blocking(i2c_inst_t *i, uint8_t a, const uint8_t *s, size_t n, bool ns) { return emu_i2c_bus_write(i->index, a, s, n, ns); }
int i2c_read_blocking(i2c_inst_t *i, uint8_t a, uint8_t *d, size_t n, bool ns) { return emu_i2c_bus_read(i->index, a, d, n, ns); }
int i2c_write_timeout_us(i2c_inst_t *i, uint8_t a, const uint8_t *s, size_t n, bool ns, uint t) { (void)t; return i2c_write_blocking(i, a, s, n, ns); }
int i2c_read_timeout_us(i2c_inst_t *i, uint8_t a, uint8_t *d, size_t n, bool ns, uint t) { (void)t; return i2c_read_blocking(i, a, d, n, ns); }

/* ================================================================ UART */
uart_inst_t emu_uart_inst[2] = { { .index = 0 }, { .index = 1 } };
static emu_uart_device_t *s_uart_dev[2];

void emu_uart_attach(unsigned u, emu_uart_device_t *d) { s_uart_dev[u] = d; }

void emu_uart_to_mcu(unsigned u, const uint8_t *b, size_t n) {
    size_t taken = 0;
    emu_dma_uart_rx_deliver(u, b, n, &taken);
    uart_inst_t *ui = &emu_uart_inst[u];
    for (size_t i = taken; i < n; i++) {
        unsigned next = (ui->head + 1) % EMU_UART_FIFO;
        if (next == ui->tail) break;               /* overrun: drop */
        ui->fifo[ui->head] = b[i];
        ui->head = next;
    }
}

uint uart_init(uart_inst_t *u, uint baud) { u->baud = baud; u->enabled = true; return baud; }
void uart_deinit(uart_inst_t *u) { u->enabled = false; }
uint uart_set_baudrate(uart_inst_t *u, uint baud) { u->baud = baud; return baud; }
bool uart_is_readable(uart_inst_t *u) { emu_poll(); return u->head != u->tail; }
bool uart_is_readable_within_us(uart_inst_t *u, uint32_t us) {
    uint64_t end = emu_time_us() + us;
    while (!uart_is_readable(u)) { if (emu_time_us() >= end) return false; emu_sleep_us(10); }
    return true;
}
char uart_getc(uart_inst_t *u) {
    while (!uart_is_readable(u)) emu_sleep_us(20);
    char c = (char)u->fifo[u->tail];
    u->tail = (u->tail + 1) % EMU_UART_FIFO;
    return c;
}
void uart_read_blocking(uart_inst_t *u, uint8_t *dst, size_t n) { for (size_t i = 0; i < n; i++) dst[i] = (uint8_t)uart_getc(u); }
void uart_write_blocking(uart_inst_t *u, const uint8_t *src, size_t n) {
    emu_uart_device_t *d = s_uart_dev[u->index];
    if (d && d->rx_from_mcu) d->rx_from_mcu(d, src, n);
    /* wire time at the configured baud (10 bits per byte) */
    if (u->baud) emu_sleep_us((uint64_t)n * 10u * 1000000u / u->baud);
}
void uart_set_break(uart_inst_t *u, bool en) {
    emu_uart_device_t *d = s_uart_dev[u->index];
    if (d && d->break_changed) d->break_changed(d, en);
}
uint uart_get_dreq(uart_inst_t *u, bool tx) { return u->index ? (tx ? DREQ_UART1_TX : DREQ_UART1_RX) : (tx ? DREQ_UART0_TX : DREQ_UART0_RX); }

/* ================================================================= IRQ */
#define MAX_HANDLERS 8
static irq_handler_t s_irq[NUM_IRQS][MAX_HANDLERS];
static bool s_irq_en[NUM_IRQS];

void irq_set_exclusive_handler(uint n, irq_handler_t h) { memset(s_irq[n], 0, sizeof s_irq[n]); s_irq[n][0] = h; }
void irq_add_shared_handler(uint n, irq_handler_t h, uint8_t order) {
    (void)order;
    for (int i = 0; i < MAX_HANDLERS; i++) if (!s_irq[n][i]) { s_irq[n][i] = h; return; }
    emu_fatal("too many shared handlers on IRQ %u", n);
}
void irq_remove_handler(uint n, irq_handler_t h) {
    for (int i = 0; i < MAX_HANDLERS; i++) if (s_irq[n][i] == h) s_irq[n][i] = NULL;
}
void irq_set_enabled(uint n, bool en) { s_irq_en[n] = en; }
bool irq_is_enabled(uint n) { return s_irq_en[n]; }

void emu_irq_raise(unsigned n) {
    static int depth;
    if (n >= NUM_IRQS || !s_irq_en[n] || depth > 4) return;
    depth++;
    for (int i = 0; i < MAX_HANDLERS; i++) if (s_irq[n][i]) s_irq[n][i]();
    depth--;
}

/* ================================================================= DMA */
typedef struct {
    dma_channel_hw_t   hw;
    dma_channel_config cfg;
    bool claimed, irq0_en, irq1_en, irq0_st, irq1_st, busy;
    bool rx_armed, rx_endless;
    uint32_t rx_left;
} dma_ch_t;

static dma_ch_t s_dma[NUM_DMA_CHANNELS];

dma_channel_hw_t *dma_channel_hw_addr(uint ch) { return &s_dma[ch].hw; }

dma_channel_config dma_channel_get_default_config(uint ch) {
    dma_channel_config c = { .size = DMA_SIZE_32, .read_inc = true, .write_inc = false,
                             .dreq = DREQ_FORCE, .chain_to = (uint8_t)ch, .enable = true };
    return c;
}
dma_channel_config dma_get_channel_config(uint ch) { return s_dma[ch].cfg; }

int dma_claim_unused_channel(bool required) {
    for (int i = 0; i < NUM_DMA_CHANNELS; i++)
        if (!s_dma[i].claimed) { s_dma[i].claimed = true; return i; }
    if (required) panic("No DMA channels are available");
    return -1;
}
void dma_channel_claim(uint ch) { s_dma[ch].claimed = true; }
void dma_channel_unclaim(uint ch) { s_dma[ch].claimed = false; }
bool dma_channel_is_claimed(uint ch) { return s_dma[ch].claimed; }

static unsigned elem(const dma_ch_t *c) { return 1u << c->cfg.size; }

static void dma_complete(uint ch) {
    dma_ch_t *c = &s_dma[ch];
    c->busy = false;
    c->hw.transfer_count = 0;
    if (!c->cfg.irq_quiet) {
        if (c->irq0_en) { c->irq0_st = true; emu_irq_raise(DMA_IRQ_0); }
        if (c->irq1_en) { c->irq1_st = true; emu_irq_raise(DMA_IRQ_1); }
    }
}

static int spi_of(uintptr_t a) {
    for (int i = 0; i < 2; i++) if (a == (uintptr_t)&emu_spi_inst[i].hw.dr) return i;
    return -1;
}
static int uart_of(uintptr_t a) {
    for (int i = 0; i < 2; i++) if (a == (uintptr_t)&emu_uart_inst[i].hw.dr) return i;
    return -1;
}

static void dma_start(uint ch) {
    dma_ch_t *c = &s_dma[ch];
    if (!c->cfg.enable) return;
    uint32_t n = c->hw.transfer_count;
    int spi = spi_of(c->hw.write_addr);
    if (spi >= 0) {
        uint8_t chunk[512];
        uintptr_t rd = c->hw.read_addr;
        while (n) {
            uint32_t k = n < sizeof chunk ? n : (uint32_t)sizeof chunk;
            for (uint32_t i = 0; i < k; i++) {
                chunk[i] = *(const volatile uint8_t *)rd;       /* 8-bit SPI frames */
                if (c->cfg.read_inc) rd += elem(c);
            }
            emu_spi_bus_write((unsigned)spi, chunk, k);
            n -= k;
        }
        c->hw.read_addr = rd;
        dma_complete(ch);
        return;
    }
    if (uart_of(c->hw.read_addr) >= 0) {       /* UART RX -> ring */
        c->rx_armed = true;
        c->rx_endless = false;
        c->rx_left = n;
        c->busy = true;
        return;
    }
    /* memory -> memory */
    uintptr_t rd = c->hw.read_addr, wr = c->hw.write_addr;
    unsigned e = elem(c);
    for (uint32_t i = 0; i < n; i++) {
        memcpy((void *)wr, (const void *)rd, e);
        if (c->cfg.read_inc) rd += e;
        if (c->cfg.write_inc) wr += e;
    }
    c->hw.read_addr = rd;
    c->hw.write_addr = wr;
    dma_complete(ch);
}

void dma_channel_configure(uint ch, const dma_channel_config *cfg, volatile void *wr,
                           const volatile void *rd, uint count, bool trigger) {
    dma_ch_t *c = &s_dma[ch];
    c->cfg = *cfg;
    c->hw.write_addr = (uintptr_t)wr;
    c->hw.read_addr = (uintptr_t)rd;
    c->hw.transfer_count = count;
    c->hw.al1_transfer_count_trig = 0;
    c->rx_armed = false;
    if (trigger) dma_start(ch);
}
void dma_channel_set_config(uint ch, const dma_channel_config *cfg, bool trigger) { s_dma[ch].cfg = *cfg; if (trigger) dma_start(ch); }
void dma_channel_set_read_addr(uint ch, const volatile void *a, bool t) { s_dma[ch].hw.read_addr = (uintptr_t)a; if (t) dma_start(ch); }
void dma_channel_set_write_addr(uint ch, volatile void *a, bool t) { s_dma[ch].hw.write_addr = (uintptr_t)a; if (t) dma_start(ch); }
void dma_channel_set_trans_count(uint ch, uint32_t n, bool t) { s_dma[ch].hw.transfer_count = n; if (t) dma_start(ch); }
void dma_channel_transfer_from_buffer_now(uint ch, const volatile void *rd, uint32_t n) { s_dma[ch].hw.read_addr = (uintptr_t)rd; s_dma[ch].hw.transfer_count = n; dma_start(ch); }
void dma_channel_transfer_to_buffer_now(uint ch, volatile void *wr, uint32_t n) { s_dma[ch].hw.write_addr = (uintptr_t)wr; s_dma[ch].hw.transfer_count = n; dma_start(ch); }
void dma_channel_start(uint ch) { dma_start(ch); }
void dma_channel_abort(uint ch) { s_dma[ch].busy = false; s_dma[ch].rx_armed = false; }
bool dma_channel_is_busy(uint ch) { emu_poll(); return s_dma[ch].busy; }
void dma_channel_wait_for_finish_blocking(uint ch) { while (s_dma[ch].busy) tight_loop_contents(); }
void dma_channel_set_irq0_enabled(uint ch, bool en) { s_dma[ch].irq0_en = en; }
void dma_channel_set_irq1_enabled(uint ch, bool en) { s_dma[ch].irq1_en = en; }
bool dma_channel_get_irq0_status(uint ch) { return s_dma[ch].irq0_st; }
bool dma_channel_get_irq1_status(uint ch) { return s_dma[ch].irq1_st; }
void dma_channel_acknowledge_irq0(uint ch) { s_dma[ch].irq0_st = false; }
void dma_channel_acknowledge_irq1(uint ch) { s_dma[ch].irq1_st = false; }

/* A driver may start a UART RX channel by writing the TRANS_COUNT trigger
 * alias directly (endless mode). Those writes are plain stores into the
 * register block, so detect them when bytes arrive. */
void emu_dma_uart_rx_deliver(unsigned u, const uint8_t *b, size_t n, size_t *taken) {
    *taken = 0;
    uintptr_t dr = (uintptr_t)&emu_uart_inst[u].hw.dr;
    for (uint ch = 0; ch < NUM_DMA_CHANNELS; ch++) {
        dma_ch_t *c = &s_dma[ch];
        if (!c->claimed || c->hw.read_addr != dr) continue;
        if (!c->rx_armed && c->hw.al1_transfer_count_trig) {
            uint32_t v = c->hw.al1_transfer_count_trig;
            c->hw.al1_transfer_count_trig = 0;
            c->rx_armed = true;
            c->rx_endless = (v >> DMA_CH0_TRANS_COUNT_MODE_LSB) == DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS;
            c->rx_left = v & 0x0fffffffu;
            c->busy = true;
        }
        if (!c->rx_armed) continue;
        size_t i = 0;
        for (; i < n; i++) {
            if (!c->rx_endless && c->rx_left == 0) break;
            uintptr_t wa = c->hw.write_addr;
            *(volatile uint8_t *)wa = b[i];
            if (c->cfg.write_inc) {
                if (c->cfg.ring_write && c->cfg.ring_bits) {
                    uintptr_t mask = ((uintptr_t)1 << c->cfg.ring_bits) - 1;
                    wa = (wa & ~mask) | ((wa + 1) & mask);
                } else {
                    wa += 1;
                }
                c->hw.write_addr = wa;
            }
            if (!c->rx_endless) c->rx_left--;
        }
        *taken = i;
        if (!c->rx_endless && c->rx_left == 0) { c->rx_armed = false; dma_complete(ch); }
        return;
    }
}

/* ================================================================= PIO */
pio_hw_t emu_pio_inst[3] = { { .index = 0 }, { .index = 1 }, { .index = 2 } };
static emu_pio_sm_device_t *s_pio_dev[3][4];
static bool s_pio_en[3][4];

void emu_pio_bind(unsigned p, unsigned sm, emu_pio_sm_device_t *d) { s_pio_dev[p][sm] = d; }

bool pio_can_add_program(PIO pio, const pio_program_t *prog) { return pio->instr_used + prog->length <= 32; }
int pio_add_program(PIO pio, const pio_program_t *prog) {
    if (!pio_can_add_program(pio, prog)) return PICO_ERROR_INSUFFICIENT_RESOURCES;
    int off = (int)pio->instr_used;
    pio->instr_used += prog->length;
    return off;
}
int pio_add_program_at_offset(PIO pio, const pio_program_t *prog, uint off) { (void)prog; return (int)off; }
void pio_remove_program(PIO pio, const pio_program_t *prog, uint off) {
    if (off + prog->length == pio->instr_used) pio->instr_used = off;
}
int pio_claim_unused_sm(PIO pio, bool required) {
    for (int i = 0; i < 4; i++)
        if (!(pio->sm_claimed & (1u << i))) { pio->sm_claimed |= (uint8_t)(1u << i); return i; }
    if (required) panic("No PIO state machines are available");
    return -1;
}
void pio_sm_claim(PIO pio, uint sm) { pio->sm_claimed |= (uint8_t)(1u << sm); }
void pio_sm_unclaim(PIO pio, uint sm) { pio->sm_claimed &= (uint8_t)~(1u << sm); s_pio_dev[pio->index][sm] = NULL; }
bool pio_sm_is_claimed(PIO pio, uint sm) { return pio->sm_claimed & (1u << sm); }
void pio_sm_set_enabled(PIO pio, uint sm, bool en) { s_pio_en[pio->index][sm] = en; }
void pio_sm_put(PIO pio, uint sm, uint32_t data) {
    emu_pio_sm_device_t *d = s_pio_dev[pio->index][sm];
    if (d && s_pio_en[pio->index][sm] && d->put) d->put(d, data);
}

/* ============================================================== clocks */
static uint32_t s_clk[CLK_COUNT] = {
    [clk_ref] = 12000000u, [clk_sys] = 150000000u, [clk_peri] = 150000000u,
    [clk_usb] = 48000000u, [clk_adc] = 48000000u, [clk_hstx] = 150000000u,
};
uint32_t clock_get_hz(clock_handle_t c) { return c < CLK_COUNT ? s_clk[c] : 0; }
bool clock_configure(clock_handle_t c, uint32_t src, uint32_t aux, uint32_t sf, uint32_t f) {
    (void)src; (void)aux; (void)sf;
    if (c < CLK_COUNT) s_clk[c] = f;
    return true;
}
bool clock_configure_undivided(clock_handle_t c, uint32_t src, uint32_t aux, uint32_t sf) { return clock_configure(c, src, aux, sf, sf); }
bool set_sys_clock_khz(uint32_t khz, bool required) { (void)required; s_clk[clk_sys] = khz * 1000u; return true; }

/* =============================================================== PSRAM */
#define EMU_PSRAM_BASE 0x11000000u
static bool s_psram_ok;

bool emu_psram_map(void) {
#if defined(__EMSCRIPTEN__)
    /* Linked with GLOBAL_BASE above the PSRAM window, so linear memory at
     * 0x11000000 is ours and untouched by the program's data or heap. */
    extern unsigned char __global_base;
    s_psram_ok = (uintptr_t)&__global_base >= EMU_PSRAM_BASE + (uintptr_t)PICO_PSRAM_SIZE_BYTES;
    if (s_psram_ok) memset((void *)(uintptr_t)EMU_PSRAM_BASE, 0, (size_t)PICO_PSRAM_SIZE_BYTES);
#elif !defined(_WIN32)
    void *want = (void *)(uintptr_t)EMU_PSRAM_BASE;
    void *p = mmap(want, (size_t)PICO_PSRAM_SIZE_BYTES, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    s_psram_ok = (p == want);
    if (p != MAP_FAILED && p != want) munmap(p, (size_t)PICO_PSRAM_SIZE_BYTES);
#endif
    return s_psram_ok;
}
bool psram_is_available(void) { return s_psram_ok; }
size_t psram_get_size(void) { return s_psram_ok ? (size_t)PICO_PSRAM_SIZE_BYTES : 0; }

/* ============================================================ watchdog */
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc; (void)sp; (void)delay_ms;
    emu_app_exit("watchdog reboot (on hardware: back to the FREE-WILi 2 loader)");
}
