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
void     emu_poll(void);             /* cheap; call from any wait/spin path */
void     emu_sleep_us(uint64_t us);  /* sleep while servicing the emulator  */
void     emu_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void     emu_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void     emu_app_exit(const char *why) __attribute__((noreturn));
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
    void *ctx;
} emu_uart_device_t;
void emu_uart_attach(unsigned uart, emu_uart_device_t *dev);
void emu_uart_to_mcu(unsigned uart, const uint8_t *b, size_t n); /* device -> MCU RX */

/* ----------------------------------------------------------------- dma */
void emu_dma_uart_rx_deliver(unsigned uart, const uint8_t *b, size_t n, size_t *taken);
void emu_irq_raise(unsigned irq);

/* ----------------------------------------------------------------- pio */
typedef struct emu_pio_sm_device {
    const char *name;
    void (*put)(struct emu_pio_sm_device *d, uint32_t word);
    void *ctx;
} emu_pio_sm_device_t;
void emu_pio_bind(unsigned pio_index, unsigned sm, emu_pio_sm_device_t *dev);

/* --------------------------------------------------------- device models */
/* LCD (ST7796-class, 480x320) */
#define EMU_LCD_W 480
#define EMU_LCD_H 320
void            emu_lcd_init(void);
const uint32_t *emu_lcd_pixels(void);       /* ARGB8888, what the glass shows */
bool            emu_lcd_changed(void);      /* since last call              */

/* Touch (FT6336) */
void emu_touch_init(void);
void emu_touch_set(int x, int y, bool down); /* screen coordinates          */

/* RGB LEDs (WS2812 chain on the display CPU) */
#define EMU_NUM_LEDS 16
void emu_leds_init(void);
void emu_leds_get(uint32_t out_rgb[EMU_NUM_LEDS]);

/* IO expander (PCAL6524) */
void emu_ioexp_init(void);

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
