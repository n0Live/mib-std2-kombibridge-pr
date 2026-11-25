/*
 * Which navigation app draws Android Auto's cluster map, from the colours of its picture.
 * Pure function, no libc: used by libkb_dm.so (native/src/nav_app.c) and the host test
 * tests/navapp.
 *
 * The apps keep their own palettes, and the cluster shows them unchanged (the
 * colours read back from the composed cluster display are the screenshots' colours to the unit):
 *   Google   night background (27,51,75) ~58 %, day (246,244,240)
 *   Waze     always night on the cluster: (41,45,58) ~55 %
 *   Yandex   (Yango is the same engine) night (44,50,68) (46,54,72) (54,65,84) (56,63,90),
 *            day (253,251,249); the blue manoeuvre plate (21,24,188)
 *   2GIS     only its hint plate: black around it, plate (40,42,44), ETA strip (19,19,19); the
 *            plate's size follows the insets
 * The stream starts with Android Auto's splash: black with a grey logo. That is "no app yet", not
 * 2GIS (no plate).
 *
 * Rules, on a grid of samples from the AA window (a sample "matches" a palette colour when every
 * channel is within NAVAPP_TOL; "black" = every channel <= NAVAPP_BLACK):
 *   1. 2GIS plate colours >= 5 %, black >= 10 %, both together >= 60 %     -> 2GIS
 *   2. black >= 60 %                                                         -> NONE (splash/empty)
 *   3. the app with the most matching samples, if >= 20 %                   -> that app
 *   4. else                                                                  -> UNKNOWN
 * Day Google and day Yandex are only ~7 apart per channel: NAVAPP_TOL must stay below 4.
 */
#ifndef NAV_APP_CLASSIFY_H
#define NAV_APP_CLASSIFY_H

enum { NAVAPP_NONE = 0, NAVAPP_UNKNOWN, NAVAPP_GOOGLE, NAVAPP_WAZE, NAVAPP_YANDEX, NAVAPP_2GIS, NAVAPP_COUNT };
#define NAVAPP_TOL   3u
#define NAVAPP_BLACK 8u

static const char *const navapp_names[NAVAPP_COUNT] = { "none", "unknown", "google", "waze", "yandex", "2gis" };

struct navapp_colour { unsigned char app, r, g, b; };
static const struct navapp_colour navapp_palette[] = {
    { NAVAPP_GOOGLE, 27, 51, 75 },   { NAVAPP_GOOGLE, 29, 50, 75 },   { NAVAPP_GOOGLE, 33, 61, 88 },
    { NAVAPP_GOOGLE, 246, 244, 240 }, { NAVAPP_GOOGLE, 247, 246, 242 },
    { NAVAPP_WAZE, 41, 45, 58 },
    { NAVAPP_YANDEX, 44, 50, 68 },   { NAVAPP_YANDEX, 46, 54, 72 },   { NAVAPP_YANDEX, 46, 52, 69 },
    { NAVAPP_YANDEX, 54, 65, 84 },   { NAVAPP_YANDEX, 56, 63, 90 },   { NAVAPP_YANDEX, 52, 63, 82 },
    { NAVAPP_YANDEX, 253, 251, 249 }, { NAVAPP_YANDEX, 254, 252, 251 }, { NAVAPP_YANDEX, 252, 250, 248 },
    { NAVAPP_2GIS, 40, 42, 44 },     { NAVAPP_2GIS, 19, 19, 19 },
};

static unsigned navapp_diff(unsigned a, unsigned b) { return a > b ? a - b : b - a; }

/*
 * rgb: n samples as 0x00RRGGBB. counts (optional, NAVAPP_COUNT entries): matches per app, with
 * the black samples in counts[NAVAPP_NONE]. Returns a NAVAPP_* value.
 */
static __attribute__((unused)) int navapp_classify(const unsigned *rgb, unsigned n, unsigned *counts)
{
    unsigned c[NAVAPP_COUNT], i, k, best = NAVAPP_UNKNOWN;
    for (k = 0; k < NAVAPP_COUNT; k++) c[k] = 0;
    for (i = 0; i < n; i++) {
        unsigned r = (rgb[i] >> 16) & 255u, g = (rgb[i] >> 8) & 255u, b = rgb[i] & 255u;
        if (r <= NAVAPP_BLACK && g <= NAVAPP_BLACK && b <= NAVAPP_BLACK) { c[NAVAPP_NONE]++; continue; }
        for (k = 0; k < sizeof(navapp_palette) / sizeof(navapp_palette[0]); k++) {
            const struct navapp_colour *p = &navapp_palette[k];
            if (navapp_diff(r, p->r) <= NAVAPP_TOL && navapp_diff(g, p->g) <= NAVAPP_TOL && navapp_diff(b, p->b) <= NAVAPP_TOL) {
                c[p->app]++;
                break;
            }
        }
    }
    if (counts) for (k = 0; k < NAVAPP_COUNT; k++) counts[k] = c[k];
    if (!n) return NAVAPP_NONE;
    if (c[NAVAPP_2GIS] * 100u >= 5u * n && c[NAVAPP_NONE] * 100u >= 10u * n
        && (c[NAVAPP_2GIS] + c[NAVAPP_NONE]) * 100u >= 60u * n) return NAVAPP_2GIS;
    if (c[NAVAPP_NONE] * 100u >= 60u * n) return NAVAPP_NONE;
    for (k = NAVAPP_GOOGLE; k <= NAVAPP_YANDEX; k++) if (best == NAVAPP_UNKNOWN || c[k] > c[best]) best = k;
    return c[best] * 100u >= 20u * n ? (int)best : NAVAPP_UNKNOWN;
}

#endif
