/*
 * Host test of native/src/nav_app_classify.h: synthetic sample grids built from each app's
 * palette (no car pictures here: they show real streets). Shares, shifts and the splash follow
 * what the composed cluster display shows.
 */
#include <stdio.h>
#include "../../native/src/nav_app_classify.h"

static int failed;
#define N 384u                                    /* the 32 x 12 grid of nav_app.c */
static unsigned grid[N];
static unsigned rnd = 12345u;
static unsigned next_rnd(void) { rnd = rnd * 1103515245u + 12345u; return rnd >> 8; }
static unsigned rgb(int r, int g, int b) { return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b; }

/* fill: list of {colour, percent}; the rest is "other" (mid grey-green, matches no palette) */
struct part { unsigned c; unsigned pct; };
static void fill(const struct part *p, unsigned np, int shift)
{
    unsigned i = 0, k;
    for (k = 0; k < np; k++) {
        unsigned m = p[k].pct * N / 100u, j;
        for (j = 0; j < m && i < N; j++, i++) {
            int r = (int)(p[k].c >> 16) + shift, g = (int)((p[k].c >> 8) & 255u) - shift, b = (int)(p[k].c & 255u) + shift;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
            grid[i] = rgb(r, g, b);
        }
    }
    for (; i < N; i++) grid[i] = rgb(100 + (int)(next_rnd() % 40u), 120, 90);
    for (i = N - 1; i > 0; i--) { unsigned j = next_rnd() % (i + 1u), t = grid[i]; grid[i] = grid[j]; grid[j] = t; }
}

static void expect(const char *what, int want)
{
    unsigned c[NAVAPP_COUNT];
    int got = navapp_classify(grid, N, c);
    if (got != want) {
        failed++;
        printf("  FAIL %s: got %s, want %s (black %u google %u waze %u yandex %u 2gis %u)\n", what, navapp_names[got],
               navapp_names[want], c[NAVAPP_NONE], c[NAVAPP_GOOGLE], c[NAVAPP_WAZE], c[NAVAPP_YANDEX], c[NAVAPP_2GIS]);
    } else printf("  ok   %-44s %s\n", what, navapp_names[got]);
}

int main(void)
{
    int s;
    for (s = -2; s <= 2; s += 2) {                /* the decoder's error on flat areas: +-2 */
        char name[64];
        { struct part p[] = { { rgb(27, 51, 75), 58 }, { rgb(9, 121, 110), 3 } }; fill(p, 2, s);
          snprintf(name, sizeof name, "Google night, shift %d", s); expect(name, NAVAPP_GOOGLE); }
        { struct part p[] = { { rgb(246, 244, 240), 50 } }; fill(p, 1, s);
          snprintf(name, sizeof name, "Google day, shift %d", s); expect(name, NAVAPP_GOOGLE); }
        { struct part p[] = { { rgb(41, 45, 58), 55 }, { rgb(48, 64, 79), 6 } }; fill(p, 2, s);
          snprintf(name, sizeof name, "Waze, shift %d", s); expect(name, NAVAPP_WAZE); }
        { struct part p[] = { { rgb(44, 50, 68), 15 }, { rgb(46, 54, 72), 7 }, { rgb(54, 65, 84), 4 }, { rgb(56, 63, 90), 3 },
                              { rgb(21, 24, 188), 4 } }; fill(p, 5, s);
          snprintf(name, sizeof name, "Yandex night, shift %d", s); expect(name, NAVAPP_YANDEX); }
        { struct part p[] = { { rgb(253, 251, 249), 30 } }; fill(p, 1, s);
          snprintf(name, sizeof name, "Yandex day, shift %d", s); expect(name, NAVAPP_YANDEX); }
        { struct part p[] = { { rgb(0, 0, 0), 72 }, { rgb(40, 42, 44), 19 }, { rgb(19, 19, 19), 8 } }; fill(p, 3, s > 0 ? s : 0);
          snprintf(name, sizeof name, "2GIS, shift %d", s > 0 ? s : 0); expect(name, NAVAPP_2GIS); }
    }
    { struct part p[] = { { rgb(0, 0, 0), 20 }, { rgb(40, 42, 44), 62 }, { rgb(19, 19, 19), 10 } }; fill(p, 3, 0);
      expect("2GIS with a large plate (small insets)", NAVAPP_2GIS); }
    { struct part p[] = { { rgb(2, 2, 2), 97 }, { rgb(52, 52, 52), 2 } }; fill(p, 2, 0);
      expect("Android Auto splash", NAVAPP_NONE); }
    { struct part p[] = { { rgb(0, 0, 0), 100 } }; fill(p, 1, 0);
      expect("black", NAVAPP_NONE); }
    { struct part p[] = { { rgb(44, 50, 68), 10 } }; fill(p, 1, 0);
      expect("too little of any palette", NAVAPP_UNKNOWN); }
    { struct part p[] = { { rgb(1, 1, 1), 0 } }; fill(p, 1, 0);
      expect("nothing known", NAVAPP_UNKNOWN); }
    { struct part p[] = { { rgb(41, 45, 58), 30 }, { rgb(44, 50, 68), 25 } }; fill(p, 2, 0);
      expect("Waze ahead of Yandex", NAVAPP_WAZE); }
    if (navapp_classify(grid, 0, 0) != NAVAPP_NONE) { failed++; puts("  FAIL no samples"); }
    puts(failed ? "TESTS FAILED" : "all tests passed");
    return failed ? 1 : 0;
}
