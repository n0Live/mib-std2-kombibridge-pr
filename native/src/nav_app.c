/*
 * Navigation app detection: which app draws Android Auto's cluster map, so that libgal can give
 * the phone that app's content insets (VAR_DIR/app-insets, shim/src/cluster_sink.h). Included by
 * dm_hook.c after px_probe.c, whose QNX Screen helpers it uses.
 *
 * On while VAR_DIR/app-insets exists and this library plays the phone's video (decode mode).
 * About NAV_FIRST_MS after the stream starts and then every NAV_PERIOD_MS, the cluster display is
 * read into a pixmap kept for the stream (screen_read_display, the way the firmware's screenshot
 * tool does it), a NAV_GX x NAV_GY grid of the AA window (dec_win) is sampled and classified
 * (nav_app_classify.h). An app seen NAV_CONFIRM times in a row becomes the current one and is
 * written to NAV_OUT_FILE ("google", "waze", "yandex" or "2gis" and a newline); the splash and
 * pictures that match no app change nothing. The file is kept across streams and reconnections
 * (RAM: gone after a reboot), so libgal can send the last app's insets at once at the next Start,
 * before the app is seen again.
 * Which display is the cluster: "disp <ID>" in VAR_DIR/px-probe, else the first display that is
 * not 800x480 (car 2026-11-22: ID 6, 1024x384).
 * Crash guard: a RAM marker exists while Screen is being called; found at start, detection is off
 * until the next reboot. Any Screen error switches it off for the rest of the stream.
 */
#include "nav_app_classify.h"

#define NAV_ON_FILE   VAR_DIR "/app-insets"
#define NAV_OUT_FILE  "/dev/shmem/aa_nav_app"
#define NAV_RUN_FILE  "/dev/shmem/aatokombi-nav.running"
#define NAV_FIRST_MS  1000u
#define NAV_PERIOD_MS 2000u
#define NAV_GAP_MS    3000u       /* the decoder silent this long: the stream is over */
#define NAV_CONFIRM   2u
#define NAV_GX        32u
#define NAV_GY        12u

static struct { void *ctx, *disp, *pix, *buf; uint8_t *ptr; int w, h, stride; } nav_s;
static int nav_cur = NAVAPP_NONE, nav_cand = NAVAPP_NONE;
static unsigned nav_cand_n;

static void nav_close(void)
{
    if (nav_s.pix) S.pix_destroy(nav_s.pix);
    if (nav_s.ctx) S.ctx_destroy(nav_s.ctx);
    nav_s.ctx = nav_s.disp = nav_s.pix = nav_s.buf = NULL;
    nav_s.ptr = NULL;
}

/* context, cluster display and pixmap for this stream, with a first copy in it; 1 = ok */
static int nav_open(void)
{
    struct px_cfg c;
    void *list[32];
    int iv[16], n, k, dw, dh, sel = -1;
    px_read_cfg(&c);
    if (S.ctx_create(&nav_s.ctx, c.ctx) != 0 || !nav_s.ctx) { LOGV("nav: screen_create_context failed, errno", px_errno()); nav_s.ctx = NULL; return 0; }
    for (k = 0; k < 16; k++) iv[k] = -1;
    if (S.ctx_iv(nav_s.ctx, c.count, iv) != 0 || iv[0] < 1 || iv[0] > PX_MAX_DISPLAYS) { LOG("nav: no display count"); return 0; }
    n = iv[0];
    for (k = 0; k < 32; k++) list[k] = NULL;
    if (S.ctx_pv(nav_s.ctx, c.displays, list) != 0) { LOG("nav: no display list"); return 0; }
    for (k = 0; k < n && sel < 0; k++) {
        if (!px_size(list[k], &dw, &dh)) continue;
        if (c.disp >= 0 ? px_disp_iv(list[k], SCR_PROP_ID) == c.disp : (dw != PX_MAIN_W || dh != PX_MAIN_H)) sel = k;
    }
    if (sel < 0 && c.disp < 0 && px_size(list[0], &dw, &dh)) sel = 0;
    if (sel < 0 || !px_size(list[sel], &nav_s.w, &nav_s.h)) { LOG("nav: no cluster display"); return 0; }
    nav_s.disp = list[sel];
    if (!px_grab(nav_s.ctx, nav_s.disp, &c, nav_s.w, nav_s.h, &nav_s.pix, &nav_s.ptr, &nav_s.stride, &nav_s.buf) || !nav_s.buf) return 0;
    { int v[3]; v[0] = px_disp_iv(nav_s.disp, SCR_PROP_ID); v[1] = nav_s.w; v[2] = nav_s.h; px_logn("nav: cluster display (id w h)", v, 3); }
    return 1;
}

/* one copy of the cluster display, sampled into g (NAV_GX * NAV_GY, 0x00RRGGBB); 1 = ok */
static int nav_sample(unsigned *g)
{
    unsigned x0 = dec_win[0], y0 = dec_win[1], ww = dec_win[2], wh = dec_win[3], i, j, k = 0;
    if (!nav_s.pix) { if (!nav_open()) return 0; }
    else if (S.read_disp(nav_s.disp, nav_s.buf, 0, NULL, 0) != 0) { LOGV("nav: screen_read_display failed, errno", px_errno()); return 0; }
    if (!ww || !wh || x0 >= (unsigned)nav_s.w || y0 >= (unsigned)nav_s.h) { x0 = 0; y0 = 0; ww = (unsigned)nav_s.w; wh = (unsigned)nav_s.h; }
    if (x0 + ww > (unsigned)nav_s.w) ww = (unsigned)nav_s.w - x0;
    if (y0 + wh > (unsigned)nav_s.h) wh = (unsigned)nav_s.h - y0;
    for (j = 0; j < NAV_GY; j++) {
        unsigned y = y0 + dec_udiv(wh * (2u * j + 1u), 2u * NAV_GY);
        for (i = 0; i < NAV_GX; i++) {
            unsigned x = x0 + dec_udiv(ww * (2u * i + 1u), 2u * NAV_GX);
            const uint8_t *q = nav_s.ptr + y * (unsigned)nav_s.stride + x * 4u;     /* B, G, R, A */
            g[k++] = ((unsigned)q[2] << 16) | ((unsigned)q[1] << 8) | (unsigned)q[0];
        }
    }
    return 1;
}

static void nav_publish(int app, const unsigned *c)
{
    char b[40], *o;
    int fd, v[5];
    nav_cur = app;
    o = put(b, navapp_names[app]); *o++ = '\n';
    fd = open(NAV_OUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) { write(fd, b, (size_t)(o - b)); close(fd); }
    v[0] = (int)c[NAVAPP_NONE]; v[1] = (int)c[NAVAPP_GOOGLE]; v[2] = (int)c[NAVAPP_WAZE]; v[3] = (int)c[NAVAPP_YANDEX]; v[4] = (int)c[NAVAPP_2GIS];
    o = put(b, "nav: app "); o = put(o, navapp_names[app]); *o = 0;
    px_logn(b, v, 0);
    px_logn("nav:   samples black google waze yandex 2gis", v, 5);
}

static void nav_decide(int app, const unsigned *c)
{
    if (app < NAVAPP_GOOGLE) { nav_cand = NAVAPP_NONE; nav_cand_n = 0; return; }   /* splash, unknown */
    if (app == nav_cand) nav_cand_n++; else { nav_cand = app; nav_cand_n = 1; }
    if (nav_cand_n >= NAV_CONFIRM && app != nav_cur) nav_publish(app, c);
}

static void *nav_thread(void *arg)
{
    unsigned t0 = 0, next = 0, fcheck = 0, gen = 0;
    int on = 0, failed = 0;
    (void)arg;
    if (exists(NAV_RUN_FILE)) { LOG("nav: last detection did not end, off until the next reboot"); return NULL; }
    for (;;) {
        unsigned now, g[NAV_GX * NAV_GY], c[NAVAPP_COUNT];
        int playing, ok;
        usleep(250000);
        now = now_ms();
        if (!fcheck || now - fcheck >= 5000u) { fcheck = now | 1u; on = exists(NAV_ON_FILE); }
        playing = on && kv && kv->rd_alive_ms && (int)(now - kv->rd_alive_ms) < (int)NAV_GAP_MS && !switched_off();
        if (!playing) { if (nav_s.ctx) nav_close(); t0 = 0; continue; }
        if (!t0 || kv->generation != gen) {          /* a new stream: a new display session */
            gen = kv->generation;
            if (nav_s.ctx) nav_close();
            t0 = now | 1u; next = now + NAV_FIRST_MS; failed = 0; nav_cand = NAVAPP_NONE; nav_cand_n = 0;
            continue;
        }
        if (failed || (int)(now - next) < 0) continue;
        next = now + NAV_PERIOD_MS;
        if (!px_resolve()) { failed = 1; continue; }
        { int fd = open(NAV_RUN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (fd >= 0) close(fd); }
        ok = nav_sample(g);
        unlink(NAV_RUN_FILE);
        if (!ok) { nav_close(); failed = 1; LOG("nav: detection off for this stream"); continue; }
        nav_decide(navapp_classify(g, NAV_GX * NAV_GY, c), c);
    }
    return NULL;
}

static void nav_start(void)
{
    int t;
    if (pthread_create(&t, NULL, nav_thread, NULL) != 0) LOG("nav: thread not started");
}
