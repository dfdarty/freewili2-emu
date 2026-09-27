/* emu/emu.h — internal interfaces between the emulator core and its device
 * models. Nothing in here is visible to app code; apps only see the Pico
 * SDK shim and the unmodified WiliBSP headers.
 */
#ifndef FW2EMU_EMU_H
#define FW2EMU_EMU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- core */
uint64_t emu_time_us(void);          /* monotonic, 0 at emulator start      */
uint64_t emu_time_ns(void);          /* the same clock in nanoseconds       */
void     emu_poll(void);             /* cheap; call from any wait/spin path */
void     emu_sleep_us(uint64_t us);  /* sleep while servicing the emulator  */
void     emu_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void     emu_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void     emu_app_exit(const char *why) __attribute__((noreturn));      /* the app ended itself */
void     emu_run_end(const char *why, int status) __attribute__((noreturn)); /* the run is over */
void     emu_script_line(const char *line);   /* every DIAG / log line, for script `expect` */
void     emu_make_parents(const char *path);  /* mkdir -p for an output file's folder */
extern int emu_verbose;

/* ---------------------------------------------------------------- gpio */
#define EMU_NUM_GPIO 48
typedef void (*emu_gpio_out_cb)(unsigned pin, bool level, void *ctx);
bool emu_gpio_out_level(unsigned pin);         /* level the MCU drives   */
void emu_gpio_watch(unsigned pin, emu_gpio_out_cb cb, void *ctx);
void emu_gpio_drive_input(unsigned pin, int level); /* -1 = release       */

/* ----------------------------------------------------------------- spi */
typedef struct emu_spi_device {
    const char *name;
    unsigned    cs_pin;                        /* active-low chip select */
    void (*write)(struct emu_spi_device *d, const uint8_t *b, size_t n);
    void (*read)(struct emu_spi_device *d, uint8_t tx, uint8_t *dst, size_t n);
    void *ctx;
} emu_spi_device_t;
void emu_spi_attach(unsigned bus, emu_spi_device_t *dev);
void emu_spi_bus_write(unsigned bus, const uint8_t *b, size_t n);
void emu_spi_bus_read(unsigned bus, uint8_t tx, uint8_t *dst, size_t n);

/* ----------------------------------------------------------------- i2c */
typedef struct emu_i2c_device {
    const char *name;
    uint8_t     addr;
    bool (*write)(struct emu_i2c_device *d, const uint8_t *b, size_t n, bool nostop);
    bool (*read)(struct emu_i2c_device *d, uint8_t *dst, size_t n, bool nostop);
    bool (*present)(struct emu_i2c_device *d);  /* NULL = always acks     */
    void *ctx;
} emu_i2c_device_t;
void emu_i2c_attach(unsigned bus, emu_i2c_device_t *dev);
int  emu_i2c_bus_write(unsigned bus, uint8_t addr, const uint8_t *b, size_t n, bool nostop);
int  emu_i2c_bus_read(unsigned bus, uint8_t addr, uint8_t *dst, size_t n, bool nostop);

/* ---------------------------------------------------------------- uart */
typedef struct emu_uart_device {
    const char *name;
    void (*rx_from_mcu)(struct emu_uart_device *d, const uint8_t *b, size_t n);
    void (*break_changed)(struct emu_uart_device *d, bool on);
    /* Optional: called whenever the MCU checks for received data, so a model
     * can pace its transmit at the wire rate (NULL = deliver immediately). */
    void (*poll)(struct emu_uart_device *d);
    void *ctx;
} emu_uart_device_t;
void   emu_uart_attach(unsigned uart, emu_uart_device_t *dev);
void   emu_uart_to_mcu(unsigned uart, const uint8_t *b, size_t n); /* device -> MCU RX */
size_t emu_uart_rx_level(unsigned uart);           /* bytes waiting in the MCU's RX FIFO */

/* --------------------------------------------------------------- cores */
/* Core 1 runs as a coroutine on the host thread (multicore.c). */
unsigned int emu_get_core_num(void);              /* 0 or 1 */
void emu_core_tick(void);                          /* emu_poll: switch after a 1 ms slice */
bool emu_core_idle(uint64_t wake_us);              /* wait loops: false = no other core, sleep as usual */
bool emu_core_irqs_off(unsigned core);             /* that core's PRIMASK */
bool emu_in_service(void);                         /* inside emu_poll's device servicing */
bool emu_set_board_id(const char *hex16);          /* --board-id */
void emu_irq_raise_core(unsigned core, unsigned irq); /* a per-core source (SIO FIFO, doorbell) */
void emu_irq_deliver_pending(void);                /* run IRQs held for the current core */

/* ---------------------------------------------------------- bus timing */
/* SPI and I2C transfers occupy their bus for as long as they would on the
 * wire at the rate the app configured; blocking calls wait for it and DMA to
 * SPI completes when the last byte would have been shifted out. */
extern bool emu_bus_timing;                        /* false with --instant-bus */
void emu_dma_timed_task(void);                     /* completes timed DMA; from emu_poll */
typedef struct {
    uint32_t spi_hz[2], i2c_hz[2];                 /* current bus clocks (0 = off)     */
    uint64_t spi_busy_ns[2], i2c_busy_ns[2];       /* cumulative time on the wire      */
    uint64_t lcd_px_bytes;                         /* cumulative RAMWR pixel bytes     */
} emu_perf_t;
void emu_perf_counters(emu_perf_t *out);
const char *emu_perf_line(void);                   /* last 1 s summary; "" until the first */

/* ---------------------------------------------------------------- misc */
void  emu_timers_task(void);                       /* SDK alarms / repeating timers */
float emu_adc_input_volts(unsigned input);         /* board voltage at ADC input n (GPIO 40+n) */

/* ----------------------------------------------------------------- dma */
void   emu_dma_uart_rx_deliver(unsigned uart, const uint8_t *b, size_t n, size_t *taken);
size_t emu_dma_dreq_pull(unsigned dreq, uint32_t *out, size_t max);   /* TX: memory -> device */
size_t emu_dma_dreq_push(unsigned dreq, const uint32_t *in, size_t n); /* RX: device -> memory */
bool   emu_dma_dreq_active(unsigned dreq);
void   emu_irq_raise(unsigned irq);

/* ----------------------------------------------------------------- pio */
struct pio_hw;
typedef struct emu_pio_sm_device {
    const char *name;
    void     (*put)(struct emu_pio_sm_device *d, uint32_t word);   /* CPU write to TX FIFO */
    unsigned (*tx_level)(struct emu_pio_sm_device *d);             /* words queued         */
    void     (*reset)(struct emu_pio_sm_device *d, bool clear_fifos);
    void *ctx;
} emu_pio_sm_device_t;
typedef void (*emu_pio_model_bind_fn)(struct pio_hw *pio, unsigned sm);
void emu_pio_bind(unsigned pio_index, unsigned sm, emu_pio_sm_device_t *dev);
void emu_pio_model_register(const char *program_name, emu_pio_model_bind_fn bind);
emu_pio_sm_device_t *emu_pio_device(unsigned pio_index, unsigned sm);
bool emu_pio_sm_enabled(unsigned pio_index, unsigned sm);

/* --------------------------------------------------------- device models */
/* LCD (ST7796-class, 480x320) */
#define EMU_LCD_W 480
#define EMU_LCD_H 320
void            emu_lcd_init(void);
const uint32_t *emu_lcd_pixels(void);       /* ARGB8888, what the glass shows */
bool            emu_lcd_changed(void);      /* since last call              */
uint64_t        emu_lcd_pixel_bytes(void);  /* pixel data received, cumulative */

/* Touch (FT6336) */
void emu_touch_init(void);
void emu_touch_set(int x, int y, bool down); /* screen coordinates          */

/* RGB LEDs (WS2812 chain on the display CPU) */
#define EMU_NUM_LEDS 16
void emu_leds_init(void);
void emu_leds_get(uint32_t out_rgb[EMU_NUM_LEDS]);

/* IO expander (PCAL6524) */
void    emu_ioexp_init(void);
uint8_t emu_ioexp_port(unsigned port);
bool    emu_ioexp_output_high(unsigned port, unsigned bit);

/* Board-manager coprocessor (PIC): buttons, charger, power zones */
enum {
    EMU_BTN_GREY = 0, EMU_BTN_YELLOW, EMU_BTN_GREEN, EMU_BTN_BLUE, EMU_BTN_RED,
    EMU_BTN_CENTER, EMU_BTN_UP, EMU_BTN_DOWN, EMU_BTN_LEFT, EMU_BTN_RIGHT,
    EMU_BTN_HOME, EMU_BTN_OK, EMU_BTN_CANCEL, EMU_BTN_PAGE, EMU_BTN_COUNT
};
void     emu_pic_init(uint32_t initial_rails);
void     emu_pic_task(void);
void     emu_pic_set_buttons(uint16_t mask);   /* bit = EMU_BTN_*         */
uint16_t emu_pic_buttons(void);
uint32_t emu_pic_rails(void);                  /* bit (zone-1) = powered  */
bool     emu_rail_on(unsigned zone);
const char *emu_btn_name(unsigned btn);
int      emu_btn_from_name(const char *name);

/* On-board I2C sensors (SHT40, OPT4001, BMI323, BMM350) */
void emu_sensors_init(void);
bool emu_sensor_set(const char *name, int n, const float *v);
bool emu_sensor_set_str(const char *spec);          /* "lux=300" "accel=0,0,1" */
void emu_sensor_describe(char *out, size_t cap);

/* Audio: NAU88C10 codec, I2S program model, host output, mic sources */
void  emu_audio_init(void);
void  emu_audio_task(void);
void  emu_audio_finish(void);                       /* close --audio-out WAV */
void  emu_audio_enable_host(bool on);               /* play through the PC's speakers */
void  emu_audio_set_wav_out(const char *path);
void  emu_audio_set_mic_wav(const char *path);      /* external sound reaching the mics */
void  emu_audio_set_mic_level(float gain);
float emu_audio_scene_at(double t_seconds, float speaker_bleed, uint32_t *rng);
float emu_audio_speaker_at(double t_seconds);       /* what the speaker played at t (int16 units) */
void  emu_pdm_init(void);
void  emu_pdm_task(void);
bool  emu_audio_set(const char *name, int n, const float *v); /* "tone" hz amp | "miclevel" g */
bool  emu_pdm_set(const char *name, int n, const float *v);   /* "mics" a,b,c,d | "mic.A" g */
void  emu_pdm_describe(char *out, size_t cap);
int   emu_audio_status(float *level);               /* bit0 speaker, bit1 jack */

/* MAIN CPU (the other RP2350): OneWili console, SDFS server, header GPIO,
 * VIO / programmable Vout. Reached over UART0 (the FwGUI display link). */
#define EMU_HEADER_PINS 13
typedef struct {
    uint8_t gpio;             /* MAIN-CPU GPIO number                      */
    bool    output;           /* driven by MAIN (s/l/t/p)                  */
    bool    pwm;              /* PWM output                                */
    bool    level;            /* pad level as MAIN reads it               */
    bool    ext;              /* driven from outside (script/--sensor)     */
} emu_header_pin_t;
void  emu_main_init(const char *sdcard_dir);        /* NULL = default ./sdcard, "none" = no card */
void  emu_main_task(void);
bool  emu_main_set(const char *name, int n, const float *v); /* "gpioN" 0|1|-1, "vrefext" volts */
int   emu_main_header(emu_header_pin_t out[EMU_HEADER_PINS]);
float emu_main_vio(void);                           /* header VIO rail, volts */
float emu_main_vout(void);                          /* programmable Vout, volts */
bool  emu_main_sd_active(void);                     /* SD request in the last ~150 ms */

/* SEGGER RTT back end */
void emu_rtt_init(bool tcp);
void emu_rtt_task(void);

/* PSRAM (fixed mapping at 0x11000000 on hosts that support it) */
bool emu_psram_map(void);

/* Headless / scripting */
void emu_script_load(const char *path);
void emu_script_task(void);
bool emu_script_active(void);
bool emu_script_exec_line(const char *line);        /* immediate command, e.g. from the web page */
int  emu_screenshot(const char *path, bool full_device);

/* Front panel renderer (software; used by the window and by screenshots) */
#define EMU_SKIN_W 900
#define EMU_SKIN_H 520
void emu_skin_render(uint32_t *out /* EMU_SKIN_W*EMU_SKIN_H */);
int  emu_skin_frame(uint32_t *out);            /* 0 unchanged, 1 LCD only, 2 full */
void emu_skin_lcd_rect(int *x, int *y, int *w, int *h);
int  emu_skin_hit_button(int x, int y);        /* -1 if none            */
bool emu_skin_to_lcd(int x, int y, int *lx, int *ly);

/* App metadata (from the generated FW2AINFO record) */
extern const uint32_t fw2app_power_zones;

#ifdef __cplusplus
}
#endif

#endif
