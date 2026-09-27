/* rtt.c — SEGGER RTT replacement.
 *
 * Channel 0 (DIAG) goes to stdout. With --rtt, channel 0 is also served on
 * TCP 127.0.0.1:9090 and channel 1 (agentio) on 127.0.0.1:9091 — the same
 * ports `fw rtt` opens through openocd — so WiliBSP's fw.py can talk to an
 * emulated device exactly as it talks to real hardware. */
#include "SEGGER_RTT.h"
#include "emu/emu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#define RTT_TCP 1
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define NCH 2
#define IN_CAP  4096
#define OUT_CAP (8u << 20)

typedef struct {
    int      lsock, csock;
    uint16_t port;
    uint8_t  in[IN_CAP];
    unsigned in_len;
    uint8_t *out;
    size_t   out_len;
} chan_t;

static chan_t s_ch[NCH];
static bool   s_tcp;
/* Channel 1 exists only once the app has called agentio_init(), which
 * configures its RTT buffers. Until then a client on :9091 gets nothing
 * back, so it is told so and disconnected instead of left waiting. */
static bool   s_agentio_ready;
#ifdef RTT_TCP
/* :9091 clients that arrive while the app is still starting wait in the
 * listen queue and are served as soon as the app calls agentio_init(). If
 * it hasn't within AGENTIO_GRACE_US of the first client, clients are turned
 * away (gracefully: EOF, then their request is drained). */
#define AGENTIO_GRACE_US 5000000u
static uint64_t s_grace_end;             /* set when the first client shows up */
static int    s_reject = -1;             /* a :9091 client being turned away */
static uint64_t s_reject_until;
#endif
static char   s_line[512];
static size_t s_line_len;

#ifdef RTT_TCP
static int listen_on(uint16_t port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 1) < 0) {
        int e = errno;
        close(s);
        if (e == EADDRINUSE)
            emu_fatal("rtt: port %u on 127.0.0.1 is already in use -- probably another emulator started with "
                      "--rtt (or OpenOCD / fw rtt). Stop it, or run this one without --rtt", port);
        emu_fatal("rtt: cannot listen on 127.0.0.1:%u (%s)", port, strerror(e));
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    return s;
}
#endif

void emu_rtt_init(bool tcp) {
    s_tcp = tcp;
    for (int i = 0; i < NCH; i++) {
        s_ch[i].lsock = s_ch[i].csock = -1;
        s_ch[i].port = (uint16_t)(9090 + i);
        s_ch[i].out = (uint8_t *)malloc(OUT_CAP);
    }
#ifdef RTT_TCP
    if (tcp) {
        for (int i = 0; i < NCH; i++) s_ch[i].lsock = listen_on(s_ch[i].port);
        emu_log("rtt: DIAG on 127.0.0.1:9090, agentio on 127.0.0.1:9091");
    }
#else
    if (tcp) emu_log("rtt: TCP is not available in this build");
#endif
}

static void queue_out(unsigned ch, const void *p, size_t n) {
    chan_t *c = &s_ch[ch];
    if (c->csock < 0) return;                  /* nobody listening: RTT drops it */
    if (c->out_len + n > OUT_CAP) n = OUT_CAP - c->out_len;
    memcpy(c->out + c->out_len, p, n);
    c->out_len += n;
}

void emu_rtt_task(void) {
#ifdef RTT_TCP
    if (!s_tcp) return;
    if (s_reject >= 0) {
        char junk[256];
        ssize_t r = recv(s_reject, junk, sizeof junk, 0);
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || emu_time_us() > s_reject_until) {
            close(s_reject);
            s_reject = -1;
        }
    }
    for (int i = 0; i < NCH; i++) {
        chan_t *c = &s_ch[i];
        if (c->lsock >= 0 && c->csock < 0) {
            if (i == 1 && !s_agentio_ready) {
                struct pollfd pf = { .fd = c->lsock, .events = POLLIN };
                if (poll(&pf, 1, 0) <= 0) continue;                       /* nobody waiting */
                uint64_t now = emu_time_us();
                if (!s_grace_end) s_grace_end = now + AGENTIO_GRACE_US;
                if (now < s_grace_end) continue;                          /* give the app time */
            }
            int s = accept(c->lsock, NULL, NULL);
            if (s >= 0 && i == 1 && !s_agentio_ready) {
                static bool said;
                if (!said || emu_verbose)
                    emu_log("rtt: agentio client connected, but the app hasn't called agentio_init() -- "
                            "closing it (see docs/wilibsp-tools.md)");
                said = true;
                /* Close gracefully: EOF first, then drain what the client
                 * already sent, so it reads "connection closed" rather than
                 * a reset. */
                if (s_reject >= 0) close(s_reject);
                shutdown(s, SHUT_WR);
                fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
                s_reject = s;
                s_reject_until = emu_time_us() + 1000000u;
                s = -1;
            }
            if (s >= 0) {
                fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
                int one = 1;
                setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                c->csock = s;
                c->out_len = 0;
                if (emu_verbose) emu_log("rtt: client on :%u", c->port);
            }
        }
        if (c->csock < 0) continue;
        if (c->in_len < IN_CAP) {
            ssize_t r = recv(c->csock, c->in + c->in_len, IN_CAP - c->in_len, 0);
            if (r > 0) c->in_len += (unsigned)r;
            else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { close(c->csock); c->csock = -1; continue; }
        }
        while (c->out_len) {
            ssize_t w = send(c->csock, c->out, c->out_len, MSG_NOSIGNAL);
            if (w <= 0) break;
            memmove(c->out, c->out + w, c->out_len - (size_t)w);
            c->out_len -= (size_t)w;
        }
    }
#endif
}

static void stdout_diag(const char *p, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        char ch = p[i];
        if (ch == '\n' || s_line_len == sizeof s_line - 1) {
            s_line[s_line_len] = 0;
            fprintf(stdout, "[diag] %s\n", s_line);
            emu_script_line(s_line);
            fflush(stdout);
            s_line_len = 0;
            if (ch == '\n') continue;
        }
        if (ch != '\r') s_line[s_line_len++] = ch;
    }
}

void SEGGER_RTT_Init(void) {}
int SEGGER_RTT_ConfigUpBuffer(unsigned i, const char *n, void *b, unsigned s, unsigned f) {
    (void)n; (void)b; (void)s; (void)f;
    if (i == 1) s_agentio_ready = true;
    return 0;
}
int SEGGER_RTT_ConfigDownBuffer(unsigned i, const char *n, void *b, unsigned s, unsigned f) {
    (void)n; (void)b; (void)s; (void)f;
    if (i == 1) s_agentio_ready = true;
    return 0;
}
int SEGGER_RTT_SetFlagsUpBuffer(unsigned i, unsigned f) { (void)i; (void)f; return 0; }

unsigned SEGGER_RTT_Write(unsigned i, const void *p, unsigned n) {
    if (i == 0) stdout_diag((const char *)p, n);
    if (i < NCH) queue_out(i, p, n);
    return n;
}
unsigned SEGGER_RTT_WriteNoLock(unsigned i, const void *p, unsigned n) { return SEGGER_RTT_Write(i, p, n); }
unsigned SEGGER_RTT_WriteString(unsigned i, const char *s) { return SEGGER_RTT_Write(i, s, (unsigned)strlen(s)); }
unsigned SEGGER_RTT_PutChar(unsigned i, char c) { return SEGGER_RTT_Write(i, &c, 1); }

unsigned SEGGER_RTT_Read(unsigned i, void *p, unsigned cap) {
    if (i >= NCH) return 0;
    emu_poll();
    chan_t *c = &s_ch[i];
    unsigned n = c->in_len < cap ? c->in_len : cap;
    memcpy(p, c->in, n);
    memmove(c->in, c->in + n, c->in_len - n);
    c->in_len -= n;
    return n;
}
unsigned SEGGER_RTT_ReadNoLock(unsigned i, void *p, unsigned cap) { return SEGGER_RTT_Read(i, p, cap); }
unsigned SEGGER_RTT_HasData(unsigned i) { return i < NCH ? s_ch[i].in_len : 0; }
unsigned SEGGER_RTT_HasDataUp(unsigned i) { return i < NCH ? (unsigned)s_ch[i].out_len : 0; }
unsigned SEGGER_RTT_GetBytesInBuffer(unsigned i) { return SEGGER_RTT_HasDataUp(i); }
unsigned SEGGER_RTT_GetAvailWriteSpace(unsigned i) {
    if (i >= NCH) return 0;
    emu_rtt_task();
    size_t used = s_ch[i].out_len;
    size_t free_ = OUT_CAP - used;
    return free_ > 65536 ? 65536u : (unsigned)free_;
}
int SEGGER_RTT_HasKey(void) { return s_ch[0].in_len > 0; }
int SEGGER_RTT_GetKey(void) {
    uint8_t c;
    return SEGGER_RTT_Read(0, &c, 1) ? c : -1;
}
int SEGGER_RTT_vprintf(unsigned i, const char *fmt, va_list *ap) {
    char buf[512];
    int n = vsnprintf(buf, sizeof buf, fmt, *ap);
    if (n < 0) return n;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    SEGGER_RTT_Write(i, buf, (unsigned)n);
    return n;
}
int SEGGER_RTT_printf(unsigned i, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = SEGGER_RTT_vprintf(i, fmt, &ap);
    va_end(ap);
    return n;
}
