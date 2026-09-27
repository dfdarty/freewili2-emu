/* skin.c — software-rendered FREE-WILi 2 front panel.
 *
 * Used both for the window and for full-device screenshots, so headless runs
 * produce exactly what a person would see. Layout is schematic: LCD in the
 * middle, D-pad on the left, HOME/OK/CANCEL/PAGE on the right, the five
 * colour context keys under the screen and the WS2812 chain on top. */
#include "emu/emu.h"
#include "display/font5x7.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool emu_app_has_exited(const char **why);
bool emu_touch_get(int *x, int *y);
const char *emu_zone_name(unsigned zone);
extern const unsigned char fw2app_uf2_info[];

#define LCD_X 210
#define LCD_Y 76
#define W EMU_SKIN_W
#define H EMU_SKIN_H

typedef struct { int btn, x, y, w, h; uint32_t color; const char *label; bool round; } panel_key_t;

static const panel_key_t KEYS[] = {
    { EMU_BTN_UP,     95, 172, 40, 40, 0x3a3f48, "",       false },
    { EMU_BTN_DOWN,   95, 252, 40, 40, 0x3a3f48, "",       false },
    { EMU_BTN_LEFT,   55, 212, 40, 40, 0x3a3f48, "",       false },
    { EMU_BTN_RIGHT, 135, 212, 40, 40, 0x3a3f48, "",       false },
    { EMU_BTN_CENTER, 95, 212, 40, 40, 0x50565f, "",       true  },
    { EMU_BTN_HOME,  765, 162, 44, 44, 0x3a3f48, "HOME",   true  },
    { EMU_BTN_OK,    815, 212, 44, 44, 0x3a3f48, "OK",     true  },
    { EMU_BTN_CANCEL,765, 262, 44, 44, 0x3a3f48, "CANCEL", true  },
    { EMU_BTN_PAGE,  715, 212, 44, 44, 0x3a3f48, "PAGE",   true  },
    { EMU_BTN_GREY,  255, 420, 70, 30, 0x8a8f98, "1",      false },
    { EMU_BTN_YELLOW,335, 420, 70, 30, 0xe5c34b, "2",      false },
    { EMU_BTN_GREEN, 415, 420, 70, 30, 0x4caf6a, "3",      false },
    { EMU_BTN_BLUE,  495, 420, 70, 30, 0x4a86e8, "4",      false },
    { EMU_BTN_RED,   575, 420, 70, 30, 0xe05252, "5",      false },
};
#define NKEYS ((int)(sizeof KEYS / sizeof KEYS[0]))

static uint32_t *px;

static void put(int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < W && y < H) px[y * W + x] = 0xFF000000u | c; }

static void rect(int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) put(i, j, c);
}

static void rrect(int x, int y, int w, int h, int r, uint32_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
            int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
            if (dx * dx + dy * dy <= r * r) put(x + i, y + j, c);
        }
}

static void circle(int cx, int cy, int r, uint32_t c) {
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) put(cx + i, cy + j, c);
}

static uint32_t mix(uint32_t a, uint32_t b, int t /* 0..256 */) {
    uint32_t r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
    uint32_t bl = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return (r << 16) | (g << 8) | bl;
}

static int text(int x, int y, int s, uint32_t c, const char *str) {
    for (; *str; str++, x += 6 * s) {
        char ch = *str;
        if (ch >= 'a' && ch <= 'z' && !(ch >= FONT5X7_FIRST && ch <= FONT5X7_LAST)) ch = (char)(ch - 32);
        const uint8_t *cols = (ch >= FONT5X7_FIRST && ch <= FONT5X7_LAST) ? font5x7[ch - FONT5X7_FIRST] : font5x7[0];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if ((cols[col] >> row) & 1) rect(x + col * s, y + row * s, s, s, c);
    }
    return x;
}

static int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }

static void draw_key(const panel_key_t *k, bool down) {
    uint32_t base = k->color;
    uint32_t fill = down ? mix(base, 0xffffff, 110) : base;
    int ox = down ? 1 : 0;
    if (k->round) {
        circle(k->x + k->w / 2, k->y + k->h / 2 + 2, k->w / 2, 0x121418);
        circle(k->x + k->w / 2 + ox, k->y + k->h / 2 + ox, k->w / 2, fill);
        if (down) circle(k->x + k->w / 2 + ox, k->y + k->h / 2 + ox, k->w / 2 - 4, mix(fill, 0xffffff, 40));
    } else {
        rrect(k->x, k->y + 2, k->w, k->h, 6, 0x121418);
        rrect(k->x + ox, k->y + ox, k->w, k->h, 6, fill);
    }
    uint32_t tc = (k->btn >= EMU_BTN_GREY && k->btn <= EMU_BTN_RED) ? 0x15171b : 0xd8dce3;
    if (k->label[0]) {
        int s = 1, tw = text_w(k->label, s);
        if (k->btn >= EMU_BTN_HOME) text(k->x + (k->w - tw) / 2 + ox, k->y + k->h + 6, s, 0x9aa1ac, k->label);
        else text(k->x + (k->w - tw) / 2 + ox, k->y + (k->h - 7) / 2 + ox, s, tc, k->label);
    }
}

/* Static parts of the panel, drawn once. */
static uint32_t *s_base;

static void draw_static(void) {
    rect(0, 0, W, H, 0x0f1115);
    rrect(18, 18, W - 36, H - 36, 26, 0x2b3038);
    rrect(20, 20, W - 40, H - 40, 24, 0x22262d);
    text(44, 36, 2, 0xe8ebf0, "FREE-WILi 2");
    text(196, 43, 1, 0x7d8490, "EMULATOR");
    const char *name = (const char *)fw2app_uf2_info + 16;   /* fw2app_uf2_info_t.name */
    char app[64];
    snprintf(app, sizeof app, "APP: %.31s", name);
    text(W - 44 - text_w(app, 1), 40, 1, 0x9aa1ac, app);
    for (int i = 0; i < EMU_NUM_LEDS; i++) circle(290 + i * 21, 58, 7, 0x15171b);
    rrect(LCD_X - 10, LCD_Y - 10, EMU_LCD_W + 20, EMU_LCD_H + 20, 8, 0x0a0b0d);
    for (int i = 0; i < NKEYS; i++) draw_key(&KEYS[i], false);
#ifdef __EMSCRIPTEN__
    text(44, 484, 1, 0x5d636d, "KEYS: ARROWS+ENTER DPAD  H O C P  1-5 CONTEXT  F2 DOWNLOAD PNG");
#else
    text(44, 484, 1, 0x5d636d, "KEYS: ARROWS+ENTER DPAD  H O C P  1-5 CONTEXT  F2 SCREENSHOT");
#endif
}

static void draw_lcd(void) {
    const uint32_t *lcd = emu_lcd_pixels();
    for (int y = 0; y < EMU_LCD_H; y++) memcpy(&px[(LCD_Y + y) * W + LCD_X], &lcd[y * EMU_LCD_W], EMU_LCD_W * 4);
    int tx, ty;
    if (emu_touch_get(&tx, &ty))   /* crosshair where the finger is */
        for (int i = -12; i <= 12; i++) {
            if (tx + i >= 0 && tx + i < EMU_LCD_W) put(LCD_X + tx + i, LCD_Y + ty, 0x00e5ff);
            if (ty + i >= 0 && ty + i < EMU_LCD_H) put(LCD_X + tx, LCD_Y + ty + i, 0x00e5ff);
        }
    const char *why = NULL;
    if (emu_app_has_exited(&why)) {
        rect(LCD_X, LCD_Y + EMU_LCD_H / 2 - 24, EMU_LCD_W, 48, 0x000000);
        text(LCD_X + 12, LCD_Y + EMU_LCD_H / 2 - 16, 2, 0xffcc66, "APP EXITED");
        char msg[96];
        snprintf(msg, sizeof msg, "%.78s", why ? why : "");
        text(LCD_X + 12, LCD_Y + EMU_LCD_H / 2 + 6, 1, 0xe8ebf0, msg);
    }
}

/* User GPIO header (MAIN CPU, via OneWili): one square per pin, the VIO rail
 * that powers the level shifters, the programmable Vout and SD activity. */
#define HDR_X 40
#define HDR_Y 318
typedef struct {
    emu_header_pin_t pin[EMU_HEADER_PINS];
    int vio_cv, vout_cv;          /* centivolts */
    bool sd;
} header_view_t;

static void header_view(header_view_t *h) {
    memset(h, 0, sizeof *h);
    emu_main_header(h->pin);
    h->vio_cv = (int)(emu_main_vio() * 100.0f + 0.5f);
    h->vout_cv = (int)(emu_main_vout() * 100.0f + 0.5f);
    h->sd = emu_main_sd_active();
    for (int i = 0; i < EMU_HEADER_PINS; i++) if (h->pin[i].pwm) h->pin[i].level = false;  /* no flicker */
}

static void draw_header(const header_view_t *h) {
    char s[40];
    rrect(HDR_X, HDR_Y, 152, 132, 8, 0x1a1d22);
    text(HDR_X + 8, HDR_Y + 8, 1, 0x9aa1ac, "GPIO HEADER");
    bool vio = h->vio_cv >= 100;
    snprintf(s, sizeof s, "VIO %d.%02dV", h->vio_cv / 100, h->vio_cv % 100);
    text(HDR_X + 8, HDR_Y + 20, 1, vio ? 0x4cc38a : 0xd08a3a, vio ? s : "VIO OFF: PINS DEAD");
    for (int i = 0; i < EMU_HEADER_PINS; i++) {
        const emu_header_pin_t *p = &h->pin[i];
        int row = i < 7 ? 0 : 1, col = i < 7 ? i : i - 7;
        int x = HDR_X + 8 + col * 20, y = HDR_Y + 36 + row * 34;
        uint32_t edge = 0x5d636d, fill = 0x22262d;
        if (p->pwm) { edge = 0xb07cff; fill = vio ? 0x6a4a9a : 0x2e2640; }
        else if (p->output && p->level) { edge = vio ? 0x4cc38a : 0x8a6d2b; fill = vio ? 0x4cc38a : 0x3a3020; }
        else if (p->output) { edge = 0x4cc38a; fill = 0x15171b; }
        else if (p->ext) { edge = 0x4a86e8; fill = p->level ? 0x4a86e8 : 0x15171b; }
        rect(x, y, 14, 14, edge);
        rect(x + 2, y + 2, 10, 10, fill);
        snprintf(s, sizeof s, "%u", (unsigned)p->gpio);
        text(x + 7 - text_w(s, 1) / 2, y + 17, 1, 0x7d8490, s);
    }
    if (h->vout_cv) {
        snprintf(s, sizeof s, "VOUT %d.%02dV", h->vout_cv / 100, h->vout_cv % 100);
        text(HDR_X + 8, HDR_Y + 112, 1, 0xe5c34b, s);
    }
    text(HDR_X + 124, HDR_Y + 112, 1, h->sd ? 0x4cc38a : 0x3a3f48, "SD");
}

static void draw_dynamic(const uint32_t leds[EMU_NUM_LEDS], uint16_t held, uint32_t rails, int audio) {
    for (int i = 0; i < EMU_NUM_LEDS; i++) {
        int cx = 290 + i * 21, cy = 58;
        uint32_t c = leds[i];
        if (c) { circle(cx, cy, 7, mix(c, 0xffffff, 30)); circle(cx, cy, 4, mix(c, 0xffffff, 120)); }
        else circle(cx, cy, 5, 0x30353d);
    }
    for (int i = 0; i < NKEYS; i++)
        if ((held >> KEYS[i].btn) & 1) draw_key(&KEYS[i], true);
    char line[200];
    int n = snprintf(line, sizeof line, "ZONES ON:");
    for (unsigned z = 1; z <= 17 && n < (int)sizeof line - 16; z++)
        if (rails & (1u << (z - 1))) n += snprintf(line + n, sizeof line - (size_t)n, " %s", emu_zone_name(z));
    text(44, 470, 1, 0x7d8490, line);
    header_view_t hv;
    header_view(&hv);
    draw_header(&hv);
    static const char *const route[4] = { "", "AUDIO: SPEAKER", "AUDIO: 3.5MM JACK", "AUDIO: SPEAKER+JACK" };
    if (audio & 3) text(W - 44 - text_w(route[audio & 3], 1), 470, 1, 0x4cc38a, route[audio & 3]);
}

void emu_skin_render(uint32_t *out) {
    if (!s_base) {
        s_base = (uint32_t *)malloc((size_t)W * H * 4);
        px = s_base;
        draw_static();
    }
    memcpy(out, s_base, (size_t)W * H * 4);
    px = out;
    uint32_t leds[EMU_NUM_LEDS];
    emu_leds_get(leds);
    draw_dynamic(leds, emu_pic_buttons(), emu_pic_rails(), emu_audio_status(NULL));
    draw_lcd();
}

/* Incremental frame for the window: returns 0 if nothing visible changed,
 * 1 if only the LCD area changed (out updated there), 2 for a full redraw. */
int emu_skin_frame(uint32_t *out) {
    static bool first = true;
    static uint32_t p_leds[EMU_NUM_LEDS];
    static uint16_t p_held;
    static uint32_t p_rails;
    static int p_audio;
    static int p_tx = -1, p_ty = -1;
    static bool p_touch, p_exit;
    static header_view_t p_hdr;
    header_view_t hdr;
    header_view(&hdr);
    uint32_t leds[EMU_NUM_LEDS];
    emu_leds_get(leds);
    uint16_t held = emu_pic_buttons();
    uint32_t rails = emu_pic_rails();
    int tx, ty;
    bool touch = emu_touch_get(&tx, &ty);
    bool ex = emu_app_has_exited(NULL);
    int audio = emu_audio_status(NULL);
    bool panel = first || memcmp(leds, p_leds, sizeof leds) || held != p_held || rails != p_rails || audio != p_audio ||
                 memcmp(&hdr, &p_hdr, sizeof hdr);
    p_hdr = hdr;
    bool lcd = emu_lcd_changed() || touch != p_touch || (touch && (tx != p_tx || ty != p_ty)) || ex != p_exit;
    memcpy(p_leds, leds, sizeof leds);
    p_held = held; p_rails = rails; p_audio = audio; p_touch = touch; p_tx = tx; p_ty = ty; p_exit = ex;
    first = false;
    if (panel) { emu_skin_render(out); return 2; }
    if (lcd) { px = out; draw_lcd(); return 1; }
    return 0;
}

void emu_skin_lcd_rect(int *x, int *y, int *w, int *h) { *x = LCD_X; *y = LCD_Y; *w = EMU_LCD_W; *h = EMU_LCD_H; }

int emu_skin_hit_button(int x, int y) {
    for (int i = 0; i < NKEYS; i++) {
        const panel_key_t *k = &KEYS[i];
        if (x >= k->x - 4 && x < k->x + k->w + 4 && y >= k->y - 4 && y < k->y + k->h + 4) return k->btn;
    }
    return -1;
}

bool emu_skin_to_lcd(int x, int y, int *lx, int *ly) {
    if (x < LCD_X || y < LCD_Y || x >= LCD_X + EMU_LCD_W || y >= LCD_Y + EMU_LCD_H) return false;
    *lx = x - LCD_X;
    *ly = y - LCD_Y;
    return true;
}
