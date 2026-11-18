/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared drawing for the master pages (the DJ filter's look): text, dials, a line, the knob row.
 * y = 0 is the bottom row; a master page draws in 0-52 (the core draws the title bar above). */
#ifndef MPUI_H
#define MPUI_H
#include <stdint.h>
/* the dials: points around a 270-degree sweep, from bottom left (0) over the top to bottom right */
#define DIAL_N 25
static const int8_t dial6[25][2] = {{-4,-4},{-5,-3},{-6,-2},{-6,-1},{-6,0},{-6,1},{-6,2},{-5,3},{-4,4},{-3,5},{-2,6},{-1,6},{0,6},{1,6},{2,6},{3,5},{4,4},{5,3},{6,2},{6,1},{6,0},{6,-1},{6,-2},{5,-3},{4,-4}};
static const int8_t dial5[25][2] = {{-4,-4},{-4,-3},{-5,-2},{-5,-1},{-5,0},{-5,1},{-5,2},{-4,3},{-4,4},{-3,4},{-2,5},{-1,5},{0,5},{1,5},{2,5},{3,4},{4,4},{4,3},{5,2},{5,1},{5,0},{5,-1},{5,-2},{4,-3},{4,-4}};
static const int8_t dial3[25][2] = {{-2,-2},{-3,-2},{-3,-1},{-3,-1},{-4,0},{-3,1},{-3,1},{-3,2},{-2,2},{-2,3},{-1,3},{-1,3},{0,4},{1,3},{1,3},{2,3},{2,2},{3,2},{3,1},{3,1},{4,0},{3,-1},{3,-1},{3,-2},{2,-2}};

static int32_t ui_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t ui_len(const char *s) { int32_t n = 0; while (s[n]) n++; return n; }
static void ui_text(void *bmp, int32_t x, int32_t y, const char *s) { fw_textf(bmp, fw_font5, x, y, -1, "%s", s); }
static void ui_centred(void *bmp, int32_t x0, int32_t w, int32_t y, const char *s)
{
    int32_t x = x0 + (w - (4 * ui_len(s) - 1)) / 2;
    ui_text(bmp, x < x0 ? x0 : x, y, s);
}
static void ui_dot(void *bmp, int32_t x, int32_t y) { fw_fillrect(bmp, x, y, x, y, 1); }
static void ui_line(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        ui_dot(bmp, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static char *ui_num(char *b, int32_t v)
{
    char d[8]; int n = 0;
    if (v < 0) { *b++ = '-'; v = -v; }
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *b++ = d[--n];
    *b = 0;
    return b;
}
/* a dial: kind 0 fills from the left, 1 two-way from the top, 2 a switch (a notch a setting) */
static void ui_dial(void *bmp, int32_t cx, int32_t cy, int32_t v, int32_t max, int32_t kind)
{
    int32_t at = v * (DIAL_N - 1) / (max ? max : 1), mid = (DIAL_N - 1) / 2;
    for (int32_t i = 0; i < DIAL_N; i++) {
        int32_t on = kind == 1 ? ((i >= mid && i <= at) || (i <= mid && i >= at)) : kind == 2 ? i == at : i <= at;
        if (on) {
            ui_dot(bmp, cx + dial6[i][0], cy + dial6[i][1]);
            ui_dot(bmp, cx + dial5[i][0], cy + dial5[i][1]);
        } else if (kind == 2 ? (i * max) % (DIAL_N - 1) < max : (i & 1) == 0)
            ui_dot(bmp, cx + dial6[i][0], cy + dial6[i][1]);
    }
    if (kind == 1) ui_dot(bmp, cx, cy + 7);
    ui_line(bmp, cx, cy, cx + dial3[at][0], cy + dial3[at][1]);
    fw_fillrect(bmp, cx - 1, cy - 1, cx + 1, cy + 1, 1);
}
/* the knob row: name (y 20), dial (centre y 12), value (y 0) for knob k; lit if it is the one just turned */
static void ui_knob(void *bmp, int32_t k, const char *name, const char *val, int32_t v, int32_t max, int32_t kind,
                    int32_t lit)
{
    int32_t x0 = k * 16;
    ui_centred(bmp, x0, 15, 20, name);
    ui_dial(bmp, x0 + 7, 12, v, max, kind);
    ui_centred(bmp, x0, 15, 0, val);
    if (lit) fw_fillrect(bmp, x0, 0, x0 + 14, 25, -1);
}

/* ---- stepping through the master pages with LEFT / RIGHT -------------------------------------------------
 * The master view keeps its pages' kinds in a list (+124 its start, +128 its end) and the place of the one it
 * shows at +144 (core's master.s); a step writes that place and has the view drawn again. */
#define MP_K_LEFT  16
#define MP_K_RIGHT 17
static void mp_step(void *view, int32_t dir)
{
    uint8_t *v = (uint8_t *)view;
    int32_t n = (int32_t)((*(uint32_t *)(v + 128) - *(uint32_t *)(v + 124)) >> 2);
    int32_t cur = *(int32_t *)(v + 144);
    if (n < 2 || cur < 0) return;
    *(int32_t *)(v + 144) = (cur + dir + n) % n;
    fw_invalidate(view);
}
/* a page's key handler: LEFT / RIGHT (pressed, no FUNC) step the pages; the rest left to the stock */
static int32_t mp_key(void *view, void *event)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    if (key != MP_K_LEFT && key != MP_K_RIGHT) return 0;
    if ((fl & 1) && !(fl & 0x12)) mp_step(view, key == MP_K_RIGHT ? 1 : -1);
    return 1;
}
#endif
