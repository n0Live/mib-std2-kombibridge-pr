/*
 * Pixel probe (diagnostic, opt-in): what the cluster display shows, as pixels, for working out how
 * to tell the navigation apps apart by colour (notes: nav app detection). Included by dm_hook.c
 * after dec_player.c.
 *
 * Runs only while VAR_DIR/px-probe exists AND this library is playing the phone's video (decode
 * mode). The phone's picture is decoded by mm-renderer into a window of DisplayManager's, so no
 * process of ours ever holds its pixels; the probe asks QNX Screen for a copy of the composed
 * cluster display instead (screen_read_display into a pixmap), about 1.5 s after the video starts
 * and then every PX_PERIOD_MS while it plays. A copy is KEPT only when it is the first of the
 * stream or the picture changed (switching apps, day/night): at least half of a 16x9 grid of
 * pixels moved by more than PX_CHANGE in R+G+B, and the last kept copy is PX_KEEP_MIN_MS old (a
 * moving map changes too; this keeps the log to a few lines a minute while driving). A kept copy: the grid in the log (hex AARRGGBB
 * words as read) and the whole picture as /dev/shmem/aa_px_<n>.raw (ring of PX_RAW_FILES): a text
 * header line
 * "KBPX1 <w> <h> <stride> <format> <probe seq> <ms>\n", then h rows of w*4 bytes.
 *
 * Screen calls and numbers are those of the firmware's own /usr/bin/screenshot (disassembled,
 * 0480 dump: "Screen-service-tsd-writeback--QNX6.5.0-SP1"): a DISPLAY_MANAGER context (8),
 * DISPLAY_COUNT 59 / DISPLAYS 60, a pixmap with USAGE (48) READ|NATIVE = 10, FORMAT (14) RGBA8888
 * = 8, BUFFER_SIZE (5) = the display's SIZE (40), RENDER_BUFFERS 37 -> POINTER 34 / STRIDE 44,
 * screen_read_display(display, buffer, 0, NULL, 0); the pixels are bytes B, G, R, A.
 * Which display is the cluster is NOT known: screenshot picks one by its ID (property 87) or TYPE
 * (47). So the first copy of each stream saves EVERY display (aa_px_d<id>.raw) and the log lists
 * index, ID, type and size of each; the copies compared later come from "disp <ID>" in the px-probe
 * file, else from the first display that is not 800x480 (the unit's own screen), else the first.
 * The px-probe file can still override the numbers: "ctx 8 count 59 displays 60 disp 2 fmt 8
 * usage 10" (any subset, any order). Screen's answers are checked before they are used, and every
 * property is read into a buffer far larger than it needs.
 * Crash guard: a RAM marker exists while Screen is being called; found at start, the probe is off
 * until the next reboot. The decode crash guard (dm-busy / dm-strikes) covers the rest.
 */
#define PX_FILE           VAR_DIR "/px-probe"
#define PX_RUN_FILE       "/dev/shmem/aatokombi-px.running"
#define PX_RAW_DIR        "/dev/shmem/aa_px_"
#define PX_RAW_FILES      6u
#define PX_FIRST_MS       1500u
#define PX_PERIOD_MS      5000u
#define PX_CHANGE         48u
#define PX_KEEP_MIN_MS    30000u
#define PX_GAP_MS         3000u   /* the decoder silent this long: the stream is over */
#define PX_GRID_X         16u
#define PX_GRID_Y         9u
#define PX_MAX_DISPLAYS   8

/* the numbers of /usr/bin/screenshot; the px-probe file can override the first ones */
#define SCR_DISPLAY_MANAGER_CONTEXT (1 << 3)
#define SCR_PROP_DISPLAY_COUNT      59
#define SCR_PROP_DISPLAYS           60
#define SCR_FORMAT_RGBA8888         8
#define SCR_USAGE_READ_NATIVE       ((1 << 1) | (1 << 3))
#define SCR_PROP_TYPE               47
#define SCR_PROP_ID                 87
#define PX_MAIN_W                   800   /* the unit's own screen: not the cluster */
#define PX_MAIN_H                   480
#define SCR_PROP_BUFFER_SIZE        5
#define SCR_PROP_FORMAT             14
#define SCR_PROP_POINTER            34
#define SCR_PROP_RENDER_BUFFERS     37
#define SCR_PROP_SIZE               40
#define SCR_PROP_STRIDE             44
#define SCR_PROP_USAGE              48

typedef int (*scr_ctx_create_t)(void **, int);
typedef int (*scr_destroy_t)(void *);
typedef int (*scr_get_iv_t)(void *, int, int *);
typedef int (*scr_get_pv_t)(void *, int, void **);
typedef int (*scr_set_iv_t)(void *, int, const int *);
typedef int (*scr_pix_create_t)(void **, void *);
typedef int (*scr_read_disp_t)(void *, void *, int, const int *, int);
typedef int *(*errno_ptr_t)(void);

static struct {
    scr_ctx_create_t ctx_create; scr_destroy_t ctx_destroy, pix_destroy, pix_buffer;
    scr_get_iv_t ctx_iv, disp_iv, buf_iv; scr_get_pv_t ctx_pv, pix_pv, buf_pv;
    scr_set_iv_t pix_set_iv; scr_pix_create_t pix_create; scr_read_disp_t read_disp;
} S;
static errno_ptr_t px_errno_ptr;
static int px_resolved;                       /* 0 not tried, 1 ok, -1 missing */
static unsigned px_seq;
static int px_listed;                         /* displays logged once per stream */
static unsigned px_last[16 * 9];              /* grid of the last kept copy (PX_GRID_X * PX_GRID_Y) */
static int px_have_last;                      /* 0: next copy is kept whatever it shows */
static unsigned px_kept_ms;                   /* when the last copy was kept */

struct px_cfg { int ctx, count, displays, disp, fmt, usage; };

static unsigned px_errno(void) { return px_errno_ptr ? (unsigned)*px_errno_ptr() : 0xffffffffu; }

static int px_resolve(void)
{
    void *h, *hc;
    if (px_resolved) return px_resolved > 0;
    px_resolved = -1;
    hc = dlopen("libc.so.3", RTLD_LAZY);
    if (hc) px_errno_ptr = (errno_ptr_t)dlsym(hc, "__get_errno_ptr");
    h = dlopen("libscreen.so.1", RTLD_LAZY);
    if (!h) { LOG("px: libscreen.so.1 not loaded"); return 0; }
    S.ctx_create = (scr_ctx_create_t)dlsym(h, "screen_create_context");
    S.ctx_destroy = (scr_destroy_t)dlsym(h, "screen_destroy_context");
    S.ctx_iv = (scr_get_iv_t)dlsym(h, "screen_get_context_property_iv");
    S.ctx_pv = (scr_get_pv_t)dlsym(h, "screen_get_context_property_pv");
    S.disp_iv = (scr_get_iv_t)dlsym(h, "screen_get_display_property_iv");
    S.pix_create = (scr_pix_create_t)dlsym(h, "screen_create_pixmap");
    S.pix_destroy = (scr_destroy_t)dlsym(h, "screen_destroy_pixmap");
    S.pix_set_iv = (scr_set_iv_t)dlsym(h, "screen_set_pixmap_property_iv");
    S.pix_buffer = (scr_destroy_t)dlsym(h, "screen_create_pixmap_buffer");
    S.pix_pv = (scr_get_pv_t)dlsym(h, "screen_get_pixmap_property_pv");
    S.buf_pv = (scr_get_pv_t)dlsym(h, "screen_get_buffer_property_pv");
    S.buf_iv = (scr_get_iv_t)dlsym(h, "screen_get_buffer_property_iv");
    S.read_disp = (scr_read_disp_t)dlsym(h, "screen_read_display");
    if (!S.ctx_create || !S.ctx_destroy || !S.ctx_iv || !S.ctx_pv || !S.disp_iv || !S.pix_create
        || !S.pix_destroy || !S.pix_set_iv || !S.pix_buffer || !S.pix_pv || !S.buf_pv || !S.buf_iv) {
        LOG("px: Screen functions missing");
        return 0;
    }
    if (!S.read_disp) { LOG("px: screen_read_display not in libscreen.so.1"); return 0; }
    px_resolved = 1;
    return 1;
}

static void px_read_cfg(struct px_cfg *c)
{
    char b[160], *p, *w;
    ssize_t n;
    int fd = open(PX_FILE, O_RDONLY);
    c->ctx = SCR_DISPLAY_MANAGER_CONTEXT; c->count = SCR_PROP_DISPLAY_COUNT; c->displays = SCR_PROP_DISPLAYS;
    c->disp = -1; c->fmt = SCR_FORMAT_RGBA8888; c->usage = SCR_USAGE_READ_NATIVE;
    if (fd < 0) return;
    n = read(fd, b, sizeof(b) - 1);
    close(fd);
    if (n <= 0) return;
    b[n] = 0;
    for (p = b; *p; ) {
        unsigned v = 0; int *dst = NULL;
        while (*p && (*p < 'a' || *p > 'z')) p++;
        w = p;
        while (*p >= 'a' && *p <= 'z') p++;
        if (p - w == 3 && w[0] == 'c' && w[1] == 't' && w[2] == 'x') dst = &c->ctx;
        else if (p - w == 5 && w[0] == 'c' && w[1] == 'o') dst = &c->count;
        else if (p - w == 8 && w[0] == 'd' && w[1] == 'i' && w[4] == 'l') dst = &c->displays;
        else if (p - w == 4 && w[0] == 'd' && w[1] == 'i') dst = &c->disp;
        else if (p - w == 3 && w[0] == 'f' && w[1] == 'm') dst = &c->fmt;
        else if (p - w == 5 && w[0] == 'u' && w[1] == 's') dst = &c->usage;
        while (*p == ' ' || *p == '\t') p++;
        if (*p < '0' || *p > '9') continue;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned)(*p - '0'); p++; }
        if (dst) *dst = (int)v;
    }
}

static char *put_hex(char *o, unsigned v, int digits)
{
    static const char hx[] = "0123456789abcdef";
    while (digits--) *o++ = hx[(v >> (digits * 4)) & 15u];
    return o;
}

/* "px: <a> <v1> <v2> ..." in one log line */
static void px_logn(const char *a, const int *v, int n)
{
    char b[200], *o = put(b, a);
    int i;
    for (i = 0; i < n; i++) { *o++ = ' '; if (v[i] < 0) { *o++ = '-'; o = put_u(o, (unsigned)-v[i]); } else o = put_u(o, (unsigned)v[i]); }
    *o = 0;
    LOG(b);
}

static void px_dump_raw(const char *name, const uint8_t *px, int w, int h, int stride, int fmt, unsigned seq)
{
    char hdr[96], *o;
    int fd, y;
    fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { LOG("px: raw file not opened"); return; }
    o = put(hdr, "KBPX1 "); o = put_u(o, (unsigned)w); *o++ = ' '; o = put_u(o, (unsigned)h); *o++ = ' ';
    o = put_u(o, (unsigned)stride); *o++ = ' '; o = put_u(o, (unsigned)fmt); *o++ = ' '; o = put_u(o, seq);
    *o++ = ' '; o = put_u(o, now_ms()); *o++ = '\n';
    write(fd, hdr, (size_t)(o - hdr));
    for (y = 0; y < h; y++) write(fd, px + y * stride, (size_t)(w * 4));
    close(fd);
}

/* a copy of one display in a new pixmap (*pix, the caller destroys it); 1 = ok */
static int px_grab(void *ctx, void *disp, const struct px_cfg *c, int w, int h, void **pix, uint8_t **ptr, int *stride)
{
    void *bufs[8], *pv[8];
    int iv[16], k;
    *pix = NULL; *ptr = NULL; *stride = 0;
    if (S.pix_create(pix, ctx) != 0 || !*pix) { LOGV("px: screen_create_pixmap failed, errno", px_errno()); *pix = NULL; return 0; }
    for (k = 0; k < 16; k++) iv[k] = 0;
    iv[0] = c->usage;
    if (S.pix_set_iv(*pix, SCR_PROP_USAGE, iv) != 0) { LOGV("px: pixmap usage refused, errno", px_errno()); return 0; }
    iv[0] = c->fmt;
    if (S.pix_set_iv(*pix, SCR_PROP_FORMAT, iv) != 0) { LOGV("px: pixmap format refused, errno", px_errno()); return 0; }
    iv[0] = w; iv[1] = h;
    if (S.pix_set_iv(*pix, SCR_PROP_BUFFER_SIZE, iv) != 0) { LOGV("px: pixmap size refused, errno", px_errno()); return 0; }
    if (S.pix_buffer(*pix) != 0) { LOGV("px: screen_create_pixmap_buffer failed, errno", px_errno()); return 0; }
    for (k = 0; k < 8; k++) bufs[k] = NULL;
    if (S.pix_pv(*pix, SCR_PROP_RENDER_BUFFERS, bufs) != 0 || !bufs[0]) { LOGV("px: pixmap buffer not got, errno", px_errno()); return 0; }
    pv[0] = NULL;
    if (S.buf_pv(bufs[0], SCR_PROP_POINTER, pv) == 0) *ptr = (uint8_t *)pv[0];
    for (k = 0; k < 16; k++) iv[k] = 0;
    if (S.buf_iv(bufs[0], SCR_PROP_STRIDE, iv) == 0) *stride = iv[0];
    if (!*ptr || *stride < w * 4 || *stride > 16384) {
        int v[2]; v[0] = *stride; v[1] = *ptr ? 1 : 0; px_logn("px: buffer not usable (stride pointer)", v, 2); return 0;
    }
    if (S.read_disp(disp, bufs[0], 0, NULL, 0) != 0) { LOGV("px: screen_read_display failed, errno", px_errno()); return 0; }
    return 1;
}

/* SIZE of a display into w/h, 1 = plausible */
static int px_size(void *disp, int *w, int *h)
{
    int iv[16];
    iv[0] = iv[1] = -1;
    if (!disp || S.disp_iv(disp, SCR_PROP_SIZE, iv) != 0) return 0;
    *w = iv[0]; *h = iv[1];
    return *w >= 64 && *w <= 2048 && *h >= 64 && *h <= 2048;
}

static int px_disp_iv(void *disp, int prop)
{
    int iv[16];
    iv[0] = -1;
    if (!disp || S.disp_iv(disp, prop, iv) != 0) return -1;
    return iv[0];
}

/* one probe; returns 0 when Screen refused something (the caller then stops for this stream) */
static int px_probe_once(void)
{
    struct px_cfg c;
    void *ctx = NULL, *pix = NULL, *disp = NULL, *list[32];
    int iv[16], n, k, w = 0, h = 0, stride = 0, ok = 0, dsel = -1, first = !px_have_last;
    uint8_t *ptr = NULL;
    unsigned gx, gy;

    px_read_cfg(&c);
    { int fd = open(PX_RUN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (fd >= 0) close(fd); }
    if (S.ctx_create(&ctx, c.ctx) != 0 || !ctx) { LOGV("px: screen_create_context failed, errno", px_errno()); ctx = NULL; goto out; }
    for (k = 0; k < 16; k++) iv[k] = -1;
    if (S.ctx_iv(ctx, c.count, iv) != 0) { LOGV("px: display count not read, errno", px_errno()); goto out; }
    n = iv[0];
    if (n < 1 || n > PX_MAX_DISPLAYS) { LOGV("px: implausible display count", (unsigned)n); goto out; }
    for (k = 0; k < 32; k++) list[k] = NULL;
    if (S.ctx_pv(ctx, c.displays, list) != 0) { LOGV("px: display list not read, errno", px_errno()); goto out; }
    for (k = 0; k < n; k++) {
        int dw = -1, dh = -1, id = px_disp_iv(list[k], SCR_PROP_ID), ty = px_disp_iv(list[k], SCR_PROP_TYPE);
        int good = px_size(list[k], &dw, &dh);
        if (!px_listed) {
            int v[5]; v[0] = k; v[1] = id; v[2] = ty; v[3] = dw; v[4] = dh;
            px_logn("px: display (index id type w h)", v, 5);
        }
        if (!good) continue;
        if (c.disp >= 0 ? id == c.disp : (dsel < 0 && (dw != PX_MAIN_W || dh != PX_MAIN_H))) dsel = k;
        if (first) {                               /* the first copy of a stream: every display */
            char name[48], *o;
            void *p2; uint8_t *q2; int s2;
            if (px_grab(ctx, list[k], &c, dw, dh, &p2, &q2, &s2)) {
                o = put(name, PX_RAW_DIR); *o++ = 'd'; o = put_u(o, id < 0 ? 99u : (unsigned)id); o = put(o, ".raw"); *o = 0;
                px_dump_raw(name, q2, dw, dh, s2, c.fmt, px_seq + 1u);
                { int v[3]; v[0] = id; v[1] = dw; v[2] = dh; px_logn("px: saved display (id w h)", v, 3); }
            }
            if (p2) S.pix_destroy(p2);
        }
    }
    px_listed = 1;
    if (dsel < 0 && c.disp < 0 && px_size(list[0], &w, &h)) dsel = 0;
    if (dsel < 0) { LOGV("px: no display to compare, disp", (unsigned)c.disp); goto out; }
    disp = list[dsel];
    if (!px_size(disp, &w, &h)) goto out;
    if (!px_grab(ctx, disp, &c, w, h, &pix, &ptr, &stride)) goto out;
    {
        unsigned grid[PX_GRID_X * PX_GRID_Y], moved = 0, i = 0;
        for (gy = 0; gy < PX_GRID_Y; gy++) {
            unsigned y = dec_udiv((unsigned)h * (2u * gy + 1u), 2u * PX_GRID_Y);
            for (gx = 0; gx < PX_GRID_X; gx++, i++) {
                unsigned x = dec_udiv((unsigned)w * (2u * gx + 1u), 2u * PX_GRID_X);
                const uint8_t *q = ptr + y * (unsigned)stride + x * 4u;
                grid[i] = (unsigned)q[0] | ((unsigned)q[1] << 8) | ((unsigned)q[2] << 16) | ((unsigned)q[3] << 24);
                if (px_have_last) {
                    unsigned d = 0, k2;
                    for (k2 = 0; k2 < 24; k2 += 8) {
                        unsigned u = (grid[i] >> k2) & 255u, v = (px_last[i] >> k2) & 255u;
                        d += u > v ? u - v : v - u;
                    }
                    if (d > PX_CHANGE) moved++;
                }
            }
        }
        ok = 1;
        if (px_have_last && (moved * 2u < PX_GRID_X * PX_GRID_Y || now_ms() - px_kept_ms < PX_KEEP_MIN_MS))
            goto out;                                /* same picture, or too soon: not kept */
        px_kept_ms = now_ms();
        px_seq++;
        { int v[6]; v[0] = (int)px_seq; v[1] = px_disp_iv(disp, SCR_PROP_ID); v[2] = w; v[3] = h; v[4] = stride; v[5] = (int)moved;
          px_logn("px: kept copy (seq display-id w h stride moved)", v, 6); }
        for (gy = 0, i = 0; gy < PX_GRID_Y; gy++) {
            char b[220], *o = put(b, "px: row ");
            o = put_u(o, dec_udiv((unsigned)h * (2u * gy + 1u), 2u * PX_GRID_Y)); *o++ = ':';
            for (gx = 0; gx < PX_GRID_X; gx++, i++) { *o++ = ' '; o = put_hex(o, grid[i], 8); }
            *o = 0;
            LOG(b);
        }
        for (i = 0; i < PX_GRID_X * PX_GRID_Y; i++) px_last[i] = grid[i];
        px_have_last = 1;
        {
            char name[48], *o = put(name, PX_RAW_DIR);
            o = put_u(o, px_seq - (px_seq / PX_RAW_FILES) * PX_RAW_FILES); o = put(o, ".raw"); *o = 0;
            px_dump_raw(name, ptr, w, h, stride, c.fmt, px_seq);
        }
    }
out:
    if (pix) S.pix_destroy(pix);
    if (ctx) S.ctx_destroy(ctx);
    unlink(PX_RUN_FILE);
    return ok;
}

static void *px_thread(void *arg)
{
    unsigned t0 = 0, next = 0, fcheck = 0, gen = 0;
    int on = 0, failed = 0;
    (void)arg;
    if (exists(PX_RUN_FILE)) { LOG("px: last probe did not end, probe off until the next reboot"); return NULL; }
    for (;;) {
        unsigned now;
        int playing;
        usleep(250000);
        now = now_ms();
        if (!fcheck || now - fcheck >= 5000u) { fcheck = now | 1u; on = exists(PX_FILE); }
        /* Signed: the decoder stamps rd_alive_ms with now_ms() | 1 after `now` was taken here, so
         * it can be a little AHEAD of `now` (unsigned, the difference wrapped and
         * every few seconds the probe took the running stream for a new one). */
        playing = on && kv && kv->rd_alive_ms && (int)(now - kv->rd_alive_ms) < (int)PX_GAP_MS && !switched_off();
        if (!playing) { t0 = 0; continue; }
        if (!t0 || kv->generation != gen) {          /* a new stream (the phone's Start): start over */
            gen = kv->generation;
            t0 = now | 1u; next = now + PX_FIRST_MS; failed = 0; px_listed = 0; px_have_last = 0;
            continue;
        }
        if (failed || (int)(now - next) < 0) continue;
        if (!px_resolve()) { failed = 1; continue; }
        if (!px_probe_once()) failed = 1;            /* one try per stream: no log flood */
        next = now + PX_PERIOD_MS;
    }
    return NULL;
}

static void px_start(void)
{
    int t;
    if (pthread_create(&t, NULL, px_thread, NULL) != 0) LOG("px: thread not started");
}
