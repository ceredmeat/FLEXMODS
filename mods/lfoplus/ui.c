/* SPDX-License-Identifier: MIT
 * DEJA VU 0.3: the LFO (3/3) page. LFO steps LFO (1/3), LFO (2/3), LFO (3/3), back to 1/3.
 *
 * As DISTORT's third FLTR page does: the LFO view keeps its pages as a list of page ids
 * (+124 begin, +128 end, +132 capacity, +144 the one shown; the stock's are 2 and 3). Once a
 * UI tick we add an id 0 to it (a page the stock draws as an empty title bar, its names from
 * 0x4197df88/8c), so the stock pages through it and counts it in the title; while it is the
 * shown one, ev_draw draws the knobs and ev_enc takes A-H.
 *
 *   A TRG1  LFO 1 restarts on: OWN (its track's trigs) or T1-T8 (that track's)
 *   B FOL1  LFO 1's AEN/ENV source: T1-T8
 *   C TRG2  D FOL2: the same for LFO 2
 *   E ATK   F REL: ENV's attack and release
 *   G VEL   AEN scaled by the source trig's velocity: OFF/ON
 *   H GAIN  ENV's gain (x1 .. x16)
 *
 * Per track. In LFO+ 0.6 (Cere): saved with the RND loop's settings two seconds after a change,
 * and back at power-on (dejavu.c).
 */
#include <stdint.h>
#include "digitakt-mk1/core3.h"

#define LFO_VT        0x401af858u       /* the LFO view's vtable (OS 1.54, measured in digiemu) */
#define KIND0_SHORT   (*(const char *volatile *)0x4197df88)
#define KIND0_LONG    (*(const char *volatile *)0x4197df8c)
#define DT_POPUP_ADDR 0x40093ab0u
#define DT_VIEW_POPUP 108
#define DN_FW  ((const unsigned char *)0x40200ed0)      /* the stock 5-px font: widths, */
#define DN_FO  ((const unsigned short *)0x40200fd0)     /* offsets, */
#define DN_FB  ((const unsigned char *)0x402011d0)      /* columns */

#define K_FUNC   1
#define K_LFO    23                     /* the LFO key (measured in digiemu) */
#define K_PUSH_A 40
#define PER_STEP 16                     /* encoder counts a detent */

struct dv3_cfg {
    uint8_t trg[2], fol[2], atk, rel, vel, gain;
};
extern volatile struct dv3_cfg dv3_cfg[8];
extern volatile uint8_t dv3_any_trg;

static void *ui_ctrl;
static void *ui_view;                   /* the LFO view, while it exists */
static int ui_func;
static uint8_t ui_push[8];
static int ui_acc[9];
static int ui_init;
volatile int32_t dv3_up;                /* the page is shown (tests read it) */
volatile int32_t dv3_lastkey;           /* the last key id pressed (tests) */

static int okp(uint32_t a) { return a - 0x40000000u < 0x08000000u && !(a & 1u); }

extern volatile int32_t dv3_cfg_loaded;   /* dejavu.c: the saved settings are in */
extern volatile int32_t dv_save_wait;     /* dejavu.c: ticks to the next save */

static void defaults(void)
{
    int t;
    if (dv3_cfg_loaded)
        return;
    for (t = 0; t < 8; t++) {
        dv3_cfg[t].trg[0] = dv3_cfg[t].trg[1] = 0;
        dv3_cfg[t].fol[0] = dv3_cfg[t].fol[1] = 0;
        dv3_cfg[t].atk = 8;
        dv3_cfg[t].rel = 60;
        dv3_cfg[t].vel = 0;
        dv3_cfg[t].gain = 32;
    }
    dv3_any_trg = 0;
}

static void *find_view(void *ctrl)
{
    uint32_t c = (uint32_t)ctrl, n, v;
    int k;
    if (!okp(c))
        return 0;
    n = *(uint32_t *)(c + 28);
    for (k = 0; k < 32 && okp(n) && n != c + 20; k++) {
        v = *(uint32_t *)(n + 8);
        if (okp(v) && *(uint32_t *)v == LFO_VT)
            return (void *)v;
        n = *(uint32_t *)n;
    }
    return 0;
}

/* the view's page list, with an id 0 added once */
static void add_page(void *view)
{
    uint32_t v = (uint32_t)view, beg = *(uint32_t *)(v + 124), end = *(uint32_t *)(v + 128);
    uint32_t n, i, nb;
    if (!okp(beg) || end < beg || end - beg > 64)
        return;
    n = (end - beg) >> 2;
    if (n < 1 || n > 8)
        return;
    for (i = 0; i < n; i++)
        if (((uint32_t *)beg)[i] == 0)
            return;                     /* there already (ours, or another mod's) */
    nb = fw_op_new(4 * n + 4);
    if (!nb)
        return;
    for (i = 0; i < n; i++)
        ((uint32_t *)nb)[i] = ((uint32_t *)beg)[i];
    ((uint32_t *)nb)[n] = 0;
    *(uint32_t *)(v + 124) = nb;
    *(uint32_t *)(v + 128) = nb + 4 * n + 4;
    *(uint32_t *)(v + 132) = nb + 4 * n + 4;
}

static int is_up(void *view)
{
    uint32_t v = (uint32_t)view, beg, end, idx;
    if (!view)
        return 0;
    beg = *(uint32_t *)(v + 124);
    end = *(uint32_t *)(v + 128);
    idx = *(uint32_t *)(v + 144);
    if (!okp(beg) || end < beg || end - beg > 64 || idx >= (end - beg) >> 2)
        return 0;
    return ((uint32_t *)beg)[idx] == 0;
}

void dv3_tick(void *ctrl)
{
    int up;
    if (!ui_init) {
        defaults();
        ui_init = 1;
    }
    ui_ctrl = ctrl;
    ui_view = find_view(ctrl);
    if (ui_view)
        add_page(ui_view);
    up = is_up(ui_view);
    if (up) {
        KIND0_SHORT = "LFO";
        KIND0_LONG = "LFO";
    }
    if (up != dv3_up && ui_view)
        fw_invalidate(ui_view);
    dv3_up = up;
}

/* ---- drawing: direct framebuffer, 1 bpp column-major, as PUMP X ---- */
static unsigned char *fb;

static void fill(int x0, int y0, int x1, int y1, int c)       /* y down from the top */
{
    int x, y, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 > 127) x1 = 127;
    if (y0 < 0) y0 = 0;
    if (y1 > 63) y1 = 63;
    for (x = x0; x <= x1; x++)
        for (y = y0; y <= y1; y++) {
            unsigned char *b = fb + (x << 3) + (7 - (y >> 3));
            if (c) *b |= (unsigned char)(1u << (y & 7));
            else *b &= (unsigned char)~(1u << (y & 7));
        }
}

static void frame(int x0, int y0, int x1, int y1)
{
    fill(x0, y0, x1, y0, 1); fill(x0, y1, x1, y1, 1);
    fill(x0, y0, x0, y1, 1); fill(x1, y0, x1, y1, 1);
}

static int strw(const char *s)
{
    int w = 0;
    for (; *s; s++)
        w += DN_FW[(unsigned char)*s] + 1;
    return w - 1;
}

static void text(int x, int y, const char *s, int c)          /* y: the glyphs' top row */
{
    for (; *s; s++) {
        int ch = (unsigned char)*s, w = DN_FW[ch], off = DN_FO[ch], k, r;
        for (k = 0; k < w; k++) {
            int bits = DN_FB[off + k];
            for (r = 0; r < 8; r++)
                if (bits & (1 << r)) {
                    int px = x + k, py = y + r;
                    if ((unsigned)px < 128u && (unsigned)py < 64u) {
                        unsigned char *b = fb + (px << 3) + (7 - (py >> 3));
                        if (c) *b |= (unsigned char)(1u << (py & 7));
                        else *b &= (unsigned char)~(1u << (py & 7));
                    }
                }
        }
        x += w + 1;
    }
}

static void ctext(int cx, int y, const char *s, int c) { text(cx - strw(s) / 2, y, s, c); }

static void itoa3(char *b, int v)
{
    int n = 0;
    if (v >= 100) b[n++] = (char)('0' + v / 100);
    if (v >= 10) b[n++] = (char)('0' + (v / 10) % 10);
    b[n++] = (char)('0' + v % 10);
    b[n] = 0;
}

static void val_str(int k, char *b)
{
    volatile struct dv3_cfg *c = &dv3_cfg[fw_active_track & 7u];
    switch (k) {
    case 0: case 2: {
        int s = c->trg[k >> 1];
        if (!s) { b[0] = 'O'; b[1] = 'W'; b[2] = 'N'; b[3] = 0; }
        else { b[0] = 'T'; b[1] = (char)('0' + s); b[2] = 0; }
        break;
    }
    case 1: case 3:
        b[0] = 'T'; b[1] = (char)('1' + (c->fol[k >> 1] & 7)); b[2] = 0;
        break;
    case 4: itoa3(b, c->atk); break;
    case 5: itoa3(b, c->rel); break;
    case 6:
        if (c->vel) { b[0] = 'O'; b[1] = 'N'; b[2] = 0; }
        else { b[0] = 'O'; b[1] = 'F'; b[2] = 'F'; b[3] = 0; }
        break;
    default: itoa3(b, c->gain); break;
    }
}

static const char *const labels[8] = { "TRG1", "FOL1", "TRG2", "FOL2", "ATK", "REL", "VEL", "GAIN" };

void dv3_draw(void *bmp, void *ctrl)
{
    char *b = (char *)bmp, s[8];
    int k;
    (void)ctrl;
    if (!dv3_up || !ui_view || !is_up(ui_view))
        return;
    if (*(int32_t *)(b + 4) != 128 || *(int32_t *)(b + 8) != 64 || !okp(*(uint32_t *)(b + 16)))
        return;
    fb = *(unsigned char **)(b + 16);
    fill(0, 11, 127, 63, 0);                                /* under the stock title bar */
    for (k = 0; k < 8; k++) {
        int x0 = (k & 3) * 32, y0 = k < 4 ? 13 : 38;
        frame(x0 + 2, y0, x0 + 29, y0 + 13);
        val_str(k, s);
        ctext(x0 + 16, y0 + 3, s, 1);
        ctext(x0 + 16, y0 + 16, labels[k], 1);
    }
}

/* ---- knobs ---- */
static void pop(const char *name, const char *val)
{
    uint32_t o;
    if (!ui_view)
        return;
    o = *(uint32_t *)((char *)ui_view + DT_VIEW_POPUP);
    if (okp(o))
        ((void (*)(void *, const char *, ...))DT_POPUP_ADDR)((void *)o, "%s=%s", name, val);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static const char *const names[8] = { "LFO1 Trig Source", "LFO1 Follow Source", "LFO2 Trig Source",
                                      "LFO2 Follow Source", "Follow Attack", "Follow Release",
                                      "Velocity", "Follow Gain" };

int32_t dv3_enc(void *brain, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12), delta = *(int32_t *)((char *)ev + 16), d, k, t;
    volatile struct dv3_cfg *c;
    char s[8];
    (void)brain;
    if (!dv3_up || !ui_view || !is_up(ui_view) || id < 1 || id > 8)
        return 0;
    if ((delta > 0 && ui_acc[id] < 0) || (delta < 0 && ui_acc[id] > 0))
        ui_acc[id] = 0;
    ui_acc[id] += delta;
    d = ui_acc[id] / PER_STEP;
    ui_acc[id] -= d * PER_STEP;
    if (!d)
        return 1;
    k = id - 1;
    t = fw_active_track & 7;
    c = &dv3_cfg[t];
    if (ui_func)
        d = d > 0 ? 127 : -127;                             /* FUNC + turn: to an end */
    switch (k) {
    case 0: case 2:                                         /* lists: one item a detent */
        if (!ui_func) d = d > 0 ? 1 : -1;
        c->trg[k >> 1] = (uint8_t)clampi(c->trg[k >> 1] + d, 0, 8);
        break;
    case 1: case 3:
        if (!ui_func) d = d > 0 ? 1 : -1;
        c->fol[k >> 1] = (uint8_t)clampi(c->fol[k >> 1] + d, 0, 7);
        break;
    case 6: c->vel = (uint8_t)(d > 0); break;
    default: {
        volatile uint8_t *p = k == 4 ? &c->atk : k == 5 ? &c->rel : &c->gain;
        int step = ui_push[k] || ui_func ? 1 : 4;           /* pressed + turn: fine */
        *p = (uint8_t)clampi(*p + d * step, 0, 127);
        break;
    }
    }
    {
        int u, any = 0;
        for (u = 0; u < 8; u++)
            any |= dv3_cfg[u].trg[0] | dv3_cfg[u].trg[1];
        dv3_any_trg = (uint8_t)(any != 0);
    }
    dv_save_wait = 60;                  /* saved two seconds after the last change */
    val_str(k, s);
    pop(names[k], s);
    fw_invalidate(ui_view);
    return 1;
}

/* every key, never taken: FUNC and the knob presses */
int32_t dv3_key(void *brain, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12), down = (*(int32_t *)((char *)ev + 16) & 1) != 0;
    (void)brain;
    if (down)
        dv3_lastkey = id;
    if (down && id == K_LFO) {          /* the stock names the page as the key switches to it */
        KIND0_SHORT = "LFO";
        KIND0_LONG = "LFO";
    }
    if (id == K_FUNC)
        ui_func = down;
    else if (id >= K_PUSH_A && id < K_PUSH_A + 8)
        ui_push[id - K_PUSH_A] = (uint8_t)down;
    return 0;
}
