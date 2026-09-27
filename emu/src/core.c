/* core.c — emulator entry point, clock, poll/yield loop and window.
 *
 * The app's own main() is renamed to fw2_emu_app_main() at compile time and
 * runs on this thread unchanged. It never returns control on its own, so
 * every SDK wait/spin primitive calls emu_poll(), which services the device
 * models, the scripted inputs and (about 60 times a second) the window. In
 * the browser build emu_poll() also yields to the event loop (ASYNCIFY).
 */
#include "emu/emu.h"
#include "common/uf2_info.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FW2EMU_HEADLESS_ONLY
#include <SDL.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* The page queues script-syntax commands (sensor sliders, buttons); the
 * emulator pulls them each frame, so JS never calls into a suspended program. */
EM_JS(char *, js_take_command, (void), {
    const q = Module.fw2Commands;
    if (!q || !q.length) return 0;
    return stringToNewUTF8(q.shift());
});
#else
#include <unistd.h>
#endif

#ifdef FW2_EMU_UBSAN_SUPPRESSIONS
/* FW2_EMU_SANITIZE builds: stop at the first UBSan report, with a stack, but
 * let tests/ubsan.supp name known upstream (WiliBSP) findings we cannot patch.
 * Any of these can still be overridden with ASAN_OPTIONS / UBSAN_OPTIONS. */
const char *__ubsan_default_options(void);
const char *__ubsan_default_options(void) {
    return "halt_on_error=1:print_stacktrace=1:suppressions=" FW2_EMU_UBSAN_SUPPRESSIONS;
}
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "abort_on_error=0:detect_leaks=1"; }
#endif

int fw2_emu_app_main(void);
bool emu_touch_get(int *x, int *y);
void emu_touch_task(void);
extern const unsigned char fw2app_uf2_info[];

int emu_verbose = 0;

static bool          s_headless;
static bool          s_rtt_tcp;
static bool          s_in_poll;
static bool          s_app_exited;
static const char   *s_exit_reason;
static uint64_t      s_start_ns;
static uint64_t      s_last_service_us;
static uint64_t      s_last_frame_us;
static uint64_t      s_run_limit_us;
static const char   *s_exit_shot;
static int           s_scale = 1;
static bool          s_mute;

#ifndef FW2EMU_HEADLESS_ONLY
static SDL_Window   *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture  *s_tex;
#endif
static uint32_t     *s_skin;

/* ------------------------------------------------------------------ time */
static uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t emu_time_us(void) { return (mono_ns() - s_start_ns) / 1000u; }

static void os_sleep_us(uint64_t us) {
#ifdef __EMSCRIPTEN__
    if (us >= 1000) emscripten_sleep((unsigned)(us / 1000));
#else
    usleep((useconds_t)us);
#endif
}

/* ------------------------------------------------------------------- log */
void emu_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[emu] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void emu_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[emu] FATAL: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

void panic(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    emu_log("app panic: %s", buf);
    emu_app_exit("panic");
}

void panic_unsupported(void) { panic("unsupported operation"); }

/* ---------------------------------------------------------------- window */
#ifndef FW2EMU_HEADLESS_ONLY
static int s_mouse_btn = -1;      /* skin button held by the mouse */
static bool s_mouse_touch;        /* mouse is touching the LCD     */
static uint16_t s_key_btns;       /* buttons held on the keyboard  */
static uint16_t s_mouse_btns;

/* Quick taps are held until the app has seen them — see dev_touch.c. */
static void touch_down(int lx, int ly) {
    s_mouse_touch = true;
    emu_touch_set(lx, ly, true);
}
static void touch_up(void) {
    if (!s_mouse_touch) return;
    s_mouse_touch = false;
    int x, y;
    emu_touch_get(&x, &y);
    emu_touch_set(x, y, false);
}

static int key_to_btn(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return EMU_BTN_UP;
    case SDLK_DOWN: return EMU_BTN_DOWN;
    case SDLK_LEFT: return EMU_BTN_LEFT;
    case SDLK_RIGHT: return EMU_BTN_RIGHT;
    case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: return EMU_BTN_CENTER;
    case SDLK_h: return EMU_BTN_HOME;
    case SDLK_o: return EMU_BTN_OK;
    case SDLK_c: case SDLK_BACKSPACE: return EMU_BTN_CANCEL;
    case SDLK_p: return EMU_BTN_PAGE;
    case SDLK_1: return EMU_BTN_GREY;
    case SDLK_2: return EMU_BTN_YELLOW;
    case SDLK_3: return EMU_BTN_GREEN;
    case SDLK_4: return EMU_BTN_BLUE;
    case SDLK_5: return EMU_BTN_RED;
    default: return -1;
    }
}

static void push_buttons(void) {
    emu_pic_set_buttons((uint16_t)(s_key_btns | s_mouse_btns));
}

static void mouse_at(int wx, int wy, bool down) {
    int sx = wx / s_scale, sy = wy / s_scale, lx, ly;
    if (!down) {
        touch_up();
        if (s_mouse_btn >= 0) { s_mouse_btns &= (uint16_t)~(1u << s_mouse_btn); push_buttons(); }
        s_mouse_btn = -1;
        return;
    }
    if (s_mouse_btn >= 0) return;                 /* holding a button */
    if (emu_skin_to_lcd(sx, sy, &lx, &ly)) {
        touch_down(lx, ly);
        return;
    }
    if (s_mouse_touch) return;                    /* dragged off the glass */
    int b = emu_skin_hit_button(sx, sy);
    if (b >= 0) {
        s_mouse_btn = b;
        s_mouse_btns |= (uint16_t)(1u << b);
        push_buttons();
    }
}

static void window_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            exit(0);
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F2) {
                char name[64];
                snprintf(name, sizeof name, "fw2emu-%llu.png", (unsigned long long)(emu_time_us() / 1000));
                if (emu_screenshot(name, true) == 0) emu_log("screenshot -> %s", name);
                break;
            }
            int b = key_to_btn(e.key.keysym.sym);
            if (b < 0 || e.key.repeat) break;
            if (e.type == SDL_KEYDOWN) s_key_btns |= (uint16_t)(1u << b);
            else s_key_btns &= (uint16_t)~(1u << b);
            push_buttons();
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_at(e.button.x, e.button.y, true);
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_at(e.button.x, e.button.y, false);
            break;
        case SDL_MOUSEMOTION:
            if (s_mouse_touch) {
                int lx, ly;
                if (emu_skin_to_lcd(e.motion.x / s_scale, e.motion.y / s_scale, &lx, &ly))
                    emu_touch_set(lx, ly, true);
            }
            break;
        default:
            break;
        }
    }
}

static void window_present(void) {
    int kind = emu_skin_frame(s_skin);
    if (kind == 0) return;                          /* nothing changed: skip the upload */
    if (kind == 1) {
        SDL_Rect r;
        emu_skin_lcd_rect(&r.x, &r.y, &r.w, &r.h);
        SDL_UpdateTexture(s_tex, &r, s_skin + r.y * EMU_SKIN_W + r.x, EMU_SKIN_W * 4);
    } else {
        SDL_UpdateTexture(s_tex, NULL, s_skin, EMU_SKIN_W * 4);
    }
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
}

static void window_open(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) emu_fatal("SDL_Init: %s", SDL_GetError());
    s_win = SDL_CreateWindow("FREE-WILi 2 emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             EMU_SKIN_W * s_scale, EMU_SKIN_H * s_scale, 0);
    if (!s_win) emu_fatal("SDL_CreateWindow: %s", SDL_GetError());
    s_ren = SDL_CreateRenderer(s_win, -1, 0);
    if (!s_ren) emu_fatal("SDL_CreateRenderer: %s", SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                              EMU_SKIN_W, EMU_SKIN_H);
    if (!s_tex) emu_fatal("SDL_CreateTexture: %s", SDL_GetError());
}
#else
/* Built with FW2_EMU_SDL=OFF (e.g. the -m32 build): no window, no host audio. */
static void window_events(void) {}
static void window_present(void) {}
static void window_open(void) {
    emu_fatal("this emulator was built without SDL (FW2_EMU_SDL=OFF); run it with --headless");
}
#endif

/* ------------------------------------------------------------ poll/yield */
void emu_poll(void) {
    if (s_in_poll) return;
    uint64_t now = emu_time_us();
    if (now - s_last_service_us < 1000u) return;       /* service at most 1 kHz */
    s_in_poll = true;
    s_last_service_us = now;

    emu_touch_task();
    emu_audio_task();
    emu_pdm_task();
    emu_pic_task();
    emu_main_task();
    emu_timers_task();
    emu_rtt_task();
    emu_script_task();

    if (s_run_limit_us && now >= s_run_limit_us) {
        s_in_poll = false;
        emu_app_exit("run time limit reached");
    }

    if (!s_headless && now - s_last_frame_us >= 16667u) {
        s_last_frame_us = now;
        window_events();
#ifdef __EMSCRIPTEN__
        for (char *cmd; (cmd = js_take_command()) != NULL; free(cmd))
            if (!emu_script_exec_line(cmd)) emu_log("web: bad command '%s'", cmd);
#endif
        window_present();
#ifdef __EMSCRIPTEN__
        emscripten_sleep(0);                           /* hand the browser a turn */
#endif
    }
    s_in_poll = false;
}

void emu_sleep_us(uint64_t us) {
    uint64_t end = emu_time_us() + us;
    if (emu_verbose > 1) emu_log("sleep %llu us", (unsigned long long)us);
    for (;;) {
        emu_poll();
        uint64_t now = emu_time_us();
        if (now >= end) return;
        uint64_t left = end - now;
#ifdef __EMSCRIPTEN__
        /* Every browser timer turn costs >=4 ms, so yield once per chunk (up
         * to the next frame) instead of in 1 ms slices. */
        if (left >= 1000u) {
            uint64_t ms = left / 1000u;
            emscripten_sleep((unsigned)(ms > 16 ? 16 : ms));
            continue;
        }
#else
        if (left > 1000u) os_sleep_us(1000u);
        else if (left > 100u) os_sleep_us(left - 50u);
#endif
    }
}

void tight_loop_contents(void) {
    emu_poll();
#ifndef __EMSCRIPTEN__
    os_sleep_us(20);                                   /* keep idle spins off 100% CPU */
#endif
}

/* ------------------------------------------------------------- app exit */
void emu_app_exit(const char *why) {
    s_app_exited = true;
    s_exit_reason = why;
    emu_log("app exited: %s", why);
    if (s_exit_shot) emu_screenshot(s_exit_shot, false);
    if (s_headless) {
        fflush(stdout);
        exit(0);
    }
    /* On hardware the loader takes over; here the window stays up so the
     * last frame can be inspected. */
    for (;;) {
        window_events();
        window_present();
#ifdef __EMSCRIPTEN__
        emscripten_sleep(50);
#elif defined(FW2EMU_HEADLESS_ONLY)
        os_sleep_us(30000);
#else
        SDL_Delay(30);
#endif
    }
}

bool emu_app_has_exited(const char **why) {
    if (why) *why = s_exit_reason;
    return s_app_exited;
}

/* ------------------------------------------------------------------ main */
static void usage(void) {
    fprintf(stderr,
        "usage: <app> [options]\n"
        "  --headless          no window (for tests / CI / agents)\n"
        "  --script FILE       run an input script (see docs/scripting.md)\n"
        "  --run-ms N          stop after N milliseconds\n"
        "  --shot-on-exit PNG  save the LCD when the app exits\n"
        "  --rtt               serve RTT on 127.0.0.1:9090 (DIAG) and :9091 (agentio)\n"
        "  --rails HEX         power zones already on at launch (default 0x8183)\n"
        "  --scale N           window scale factor (default 1)\n"
        "  --audio-out WAV     record everything the codec plays\n"
        "  --mic-wav WAV       sound reaching the microphones (looped)\n"
        "  --mute              don't play audio through the PC\n"
        "  --sdcard DIR        folder that stands in for the SD card (default ./sdcard; 'none' = no card)\n"
        "  --sensor NAME=V     set a sensor or sound: temp=24 lux=320 tilt=30,0 tone=1000,8000 mics=1,1,0,1\n"
        "                      gyro=0,0,0 mag=22,5,-40 tilt=PITCH,ROLL noise=1\n"
        "                      header inputs gpio12=1 (-1 releases), vrefext=3.3 (volts on Trig_IN/VREF)\n"
        "  -v                  verbose model logging\n");
}

int main(int argc, char **argv) {
    s_start_ns = mono_ns();
    uint32_t rails = 0x8183u;   /* sensors, display, USB hub, status LED, debug probe */
    const char *script = NULL;
    const char *sdcard = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--headless")) s_headless = true;
        else if (!strcmp(a, "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(a, "--run-ms") && i + 1 < argc) s_run_limit_us = strtoull(argv[++i], NULL, 10) * 1000u;
        else if (!strcmp(a, "--shot-on-exit") && i + 1 < argc) s_exit_shot = argv[++i];
        else if (!strcmp(a, "--rtt")) s_rtt_tcp = true;
        else if (!strcmp(a, "--rails") && i + 1 < argc) rails = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(a, "--scale") && i + 1 < argc) s_scale = atoi(argv[++i]) > 0 ? atoi(argv[i]) : 1;
        else if (!strcmp(a, "--sensor") && i + 1 < argc) {
            if (!emu_sensor_set_str(argv[++i])) { fprintf(stderr, "bad --sensor %s\n", argv[i]); return 1; }
        }
        else if (!strcmp(a, "--audio-out") && i + 1 < argc) emu_audio_set_wav_out(argv[++i]);
        else if (!strcmp(a, "--mic-wav") && i + 1 < argc) emu_audio_set_mic_wav(argv[++i]);
        else if (!strcmp(a, "--mute")) s_mute = true;
        else if (!strcmp(a, "--sdcard") && i + 1 < argc) sdcard = argv[++i];
        else if (!strcmp(a, "-v")) emu_verbose = 1;
        else if (!strcmp(a, "-vv")) emu_verbose = 2;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else { usage(); return 1; }
    }

    const fw2app_uf2_info_t *info = (const fw2app_uf2_info_t *)(const void *)fw2app_uf2_info;
    emu_log("FREE-WILi 2 emulator — app \"%.*s\" v%03u: %.*s",
            (int)sizeof info->name, info->name, (unsigned)info->app_version,
            (int)sizeof info->description, info->description);
    emu_log("app requests power zones 0x%05x; zones on at launch 0x%05x",
            (unsigned)fw2app_power_zones, (unsigned)rails);

    if (!emu_psram_map())
        emu_log("PSRAM not mapped at 0x11000000 on this host (agentio capture disabled)");

    emu_lcd_init();
    emu_touch_init();
    emu_leds_init();
    emu_ioexp_init();
    emu_sensors_init();
    emu_audio_init();
    emu_pdm_init();
    emu_pic_init(rails);
    emu_main_init(sdcard);
    emu_rtt_init(s_rtt_tcp);

    atexit(emu_audio_finish);
    emu_audio_enable_host(!s_headless && !s_mute);
    s_skin = (uint32_t *)calloc((size_t)EMU_SKIN_W * EMU_SKIN_H, sizeof(uint32_t));
    if (!s_headless) window_open();
    if (script) emu_script_load(script);

    int rc = fw2_emu_app_main();
    char why[48];
    snprintf(why, sizeof why, "main() returned %d", rc);
    emu_app_exit(why);
}
