/* dev_lcd.c — ST7796-class LCD controller model (480x320, SPI1).
 *
 * Decodes the byte stream the unmodified WiliBSP st7796 driver sends:
 * DC low = command, DC high = parameters / pixel data, framed by CS (GPIO 9).
 * Supported: SWRESET, SLPIN/SLPOUT, DISPON/DISPOFF, INVON/INVOFF, CASET,
 * RASET, RAMWR, RAMWRC, MADCTL, COLMOD, TEON. Pixels are RGB565, high byte
 * first on the wire.
 *
 * Colour: the real panel is a BGR, inversion-type IPS glass; with the
 * driver's INVON + MADCTL(BGR) the net result is that the RGB565 values
 * apps write appear as written (0xF800 = red). The model shows them that way.
 * Power: the panel and its controller sit on power zone 2 (DISPLAY). With the
 * zone off the glass is dark and the controller loses its state.
 */
#include "emu/emu.h"
#include "platform/board.h"

#include <string.h>

enum {
    CMD_NONE = -1, SWRESET = 0x01, SLPIN = 0x10, SLPOUT = 0x11, INVOFF = 0x20, INVON = 0x21,
    DISPOFF = 0x28, DISPON = 0x29, CASET = 0x2A, RASET = 0x2B, RAMWR = 0x2C,
    TEON = 0x35, MADCTL = 0x36, COLMOD = 0x3A, RAMWRC = 0x3C,
};

static struct {
    int      cmd;
    uint8_t  params[8];
    int      nparam;
    bool     sleeping, display_on, inverted;
    uint8_t  madctl, colmod;
    uint16_t x0, x1, y0, y1, cx, cy;
    bool     have_hi;
    uint8_t  hi;
    bool     powered;
    uint16_t ram[EMU_LCD_W * EMU_LCD_H];   /* RGB565 */
    uint32_t out[EMU_LCD_W * EMU_LCD_H];   /* ARGB, as the glass shows */
    bool     dirty;
    bool     warned_madctl;
} L;

static void controller_reset(void) {
    L.cmd = CMD_NONE;
    L.sleeping = true;
    L.display_on = false;
    L.inverted = false;
    L.madctl = 0;
    L.colmod = 0x66;
    L.x0 = 0; L.x1 = EMU_LCD_W - 1;
    L.y0 = 0; L.y1 = EMU_LCD_H - 1;
    L.cx = 0; L.cy = 0;
    L.have_hi = false;
    L.dirty = true;
}

static void pixel(uint16_t v) {
    if (L.cx < EMU_LCD_W && L.cy < EMU_LCD_H) L.ram[L.cy * EMU_LCD_W + L.cx] = v;
    if (++L.cx > L.x1) { L.cx = L.x0; if (++L.cy > L.y1) L.cy = L.y0; }
    L.dirty = true;
}

static void command(uint8_t c) {
    L.cmd = c;
    L.nparam = 0;
    L.have_hi = false;
    switch (c) {
    case SWRESET: controller_reset(); break;
    case SLPIN: L.sleeping = true; L.dirty = true; break;
    case SLPOUT: L.sleeping = false; L.dirty = true; break;
    case DISPON: L.display_on = true; L.dirty = true; break;
    case DISPOFF: L.display_on = false; L.dirty = true; break;
    case INVON: L.inverted = true; break;
    case INVOFF: L.inverted = false; break;
    case RAMWR: L.cx = L.x0; L.cy = L.y0; break;
    case RAMWRC: break;
    default: break;
    }
}

static uint64_t s_px_bytes;
uint64_t emu_lcd_pixel_bytes(void) { return s_px_bytes; }

static void param(uint8_t b) {
    if (L.cmd == RAMWR || L.cmd == RAMWRC) {
        s_px_bytes++;
        if (!L.have_hi) { L.hi = b; L.have_hi = true; }
        else { pixel((uint16_t)((L.hi << 8) | b)); L.have_hi = false; }
        return;
    }
    if (L.nparam < (int)sizeof L.params) L.params[L.nparam] = b;
    L.nparam++;
    switch (L.cmd) {
    case CASET:
        if (L.nparam == 4) {
            L.x0 = (uint16_t)((L.params[0] << 8) | L.params[1]);
            L.x1 = (uint16_t)((L.params[2] << 8) | L.params[3]);
        }
        break;
    case RASET:
        if (L.nparam == 4) {
            L.y0 = (uint16_t)((L.params[0] << 8) | L.params[1]);
            L.y1 = (uint16_t)((L.params[2] << 8) | L.params[3]);
        }
        break;
    case MADCTL:
        L.madctl = b;
        if (b != 0x2C && !L.warned_madctl) {
            emu_log("lcd: MADCTL 0x%02x not modelled (only the BSP's 0x2C landscape mode is)", b);
            L.warned_madctl = true;
        }
        break;
    case COLMOD:
        L.colmod = b;
        if ((b & 0x07) != 0x05) emu_log("lcd: COLMOD 0x%02x — only 16 bpp (0x05) is modelled", b);
        break;
    default: break;
    }
}

static void lcd_write(emu_spi_device_t *d, const uint8_t *b, size_t n) {
    (void)d;
    if (!L.powered) return;
    bool data = emu_gpio_out_level(PIN_LCD_DC);
    for (size_t i = 0; i < n; i++) {
        if (data) param(b[i]);
        else command(b[i]);
    }
}

static void cs_changed(unsigned pin, bool level, void *ctx) {
    (void)pin; (void)ctx;
    if (level) L.have_hi = false;          /* CS high ends a pixel pair */
}

static void backlight_changed(unsigned pin, bool level, void *ctx) {
    (void)pin; (void)level; (void)ctx;
    L.dirty = true;
}

static emu_spi_device_t s_dev = { .name = "st7796", .cs_pin = PIN_LCD_CS, .write = lcd_write };

void emu_lcd_init(void) {
    memset(&L, 0, sizeof L);
    controller_reset();
    L.powered = true;
    emu_spi_attach(1, &s_dev);
    emu_gpio_watch(PIN_LCD_CS, cs_changed, NULL);
    emu_gpio_watch(PIN_LCD_BL, backlight_changed, NULL);
}

static void track_power(void) {
    bool on = emu_rail_on(2);
    if (on == L.powered) return;
    L.powered = on;
    if (on) {
        controller_reset();
        for (int i = 0; i < EMU_LCD_W * EMU_LCD_H; i++) L.ram[i] = (uint16_t)(i * 2654435761u >> 16);
    }
    L.dirty = true;
    if (emu_verbose) emu_log("lcd: power zone 2 %s", on ? "on" : "off");
}

bool emu_lcd_changed(void) {
    track_power();
    bool d = L.dirty;
    return d;
}

const uint32_t *emu_lcd_pixels(void) {
    track_power();
    if (!L.dirty) return L.out;
    L.dirty = false;
    bool lit = L.powered && L.display_on && !L.sleeping;
    bool backlight = emu_gpio_out_level(PIN_LCD_BL);
    for (int i = 0; i < EMU_LCD_W * EMU_LCD_H; i++) {
        if (!lit) { L.out[i] = 0xFF050607u; continue; }
        uint16_t v = L.ram[i];
        uint32_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        if (!backlight) { r = r * 12 / 255; g = g * 12 / 255; b = b * 12 / 255; }  /* barely visible */
        L.out[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return L.out;
}
