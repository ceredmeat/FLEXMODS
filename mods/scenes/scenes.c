/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SCENES for the Digitakt mk1 (OS 1.54): two scenes, A and B, and a crossfader between them, after the
 * Octatrack's. A scene is a set of sound values on any of the eight audio tracks (filter, amp, SRC,
 * LFO knobs ...). With the fader at A the tracks play scene A's values, at B scene B's, in between a
 * mix; a value only one scene has fades from the track's own setting.
 *
 *   TRK + FUNC        opens the SCENES page (again: closes it)
 *   on the page:
 *     any knob          moves the crossfader
 *     YES               jumps the fader to the other end
 *     LEFT / RIGHT      record scene A / B: the page closes, REC A (or B) shows at the top, and every
 *                       knob you turn on any track goes into the scene instead of the sound. TRK + FUNC
 *                       ends it: the sounds go back to how they were and the page shows again.
 *     FUNC + LEFT/RIGHT clears scene A / B
 *     DOWN              0.3: the SCENE INFO page: every value in the scenes, a row each (the track, the
 *                       parameter, its value in A and in B, -- where a scene doesn't change it).
 *                       UP / DOWN choose a row; FUNC + LEFT / RIGHT take it out of scene A / B; FUNC + YES
 *                       out of both; NO back to the fader. The knobs still move the fader.
 *     NO                closes the page
 *     a page key        0.3: (TRIG SRC FLTR AMP LFO, GLOBAL, SAMPLE) closes it and goes there
 *
 * How it works:
 *   - recording: the sounds of the eight tracks (the UI kit, fw_kit) are copied when it starts, with
 *     the scene's own values put in so you edit from them. When it ends, every sound value that differs
 *     from the copy is the scene; the copy is put back, in the kit and in the engine (fw_set_param).
 *   - playing: once a render block (ev_render_voices, 1500 a second) each scene value is written into
 *     the engine's copy of the sound (0x800014f0 + 2 (53 t + 9 + slot), through fw_set_param, as a knob
 *     does). The track's own value is what the engine's copy holds when it is not what we last wrote:
 *     a trig, a lock or a knob put it there.
 *   - the scenes are saved (SAVE_BLOCK) and come back at power-on; the fader starts at A.
 *   - 0.3: and they go with the project, through PROJECT LINK (common/projsave.h): kept with it when
 *     you SAVE PROJECT, back when you load it, none in a new project. */
#include <stdint.h>
#include "../common/heapbuf.h"
#include "digitakt-mk1/core3.h"

#define P88(t, k)  (*(volatile int16_t *)(0x800014f0u + 2u * (53u * (uint32_t)(t) + 9u + (uint32_t)(k))))
#define SOUND(t)   (fw_kit + 0x20 + 0xa2 * (t))
#define KIT_WORD(t, k) (*(volatile int16_t *)(SOUND(t) + 20 + 2 * (k)))
#define MACHINE(t) (SOUND(t)[0x7e])
#define NONE       ((int16_t)-32768)
#define MAXE       80
#define SLOTS      53
#define XF_MAX     127

/* which sound slots a scene may hold: not 0 (the machine), the LFO destinations (4, 12), SAMP (20) */
static int32_t allowed(int32_t k) { return k >= 1 && k < SLOTS && k != 4 && k != 12 && k != 20; }

struct ent { uint8_t t, k; int16_t v[2]; };
static struct ent ents[MAXE];
static int16_t base[MAXE], last[MAXE];
static uint8_t tracked[MAXE];
static volatile int32_t nent, xf, busy, rec;           /* rec: 0, or 1 + the scene being recorded */
static int32_t full;

/* ---- playing: at interrupt level, once a render block ----------------------------------------------- */
void sc_render(int32_t *blocks)
{
    (void)blocks;
    if (busy || rec)
        return;
    for (int32_t i = 0; i < nent; i++) {
        const struct ent *e = &ents[i];
        int32_t cur = P88(e->t, e->k), a, b, out;
        if (!tracked[i] || cur != last[i]) {
            base[i] = (int16_t)cur;                     /* the track's own value, new */
            tracked[i] = 1;
        }
        a = e->v[0] == NONE ? base[i] : e->v[0];
        b = e->v[1] == NONE ? base[i] : e->v[1];
        out = a + (b - a) * xf / XF_MAX;
        if (out != cur)
            fw_set_param(out, e->t, e->k);
        last[i] = (int16_t)out;
    }
}

/* Every scene value back to the track's own (the UI task, busy set). */
static void unapply(void)
{
    for (int32_t i = 0; i < nent; i++) {
        if (tracked[i] && P88(ents[i].t, ents[i].k) == last[i] && base[i] != last[i])
            fw_set_param(base[i], ents[i].t, ents[i].k);
        tracked[i] = 0;
    }
}

static int32_t find(int32_t t, int32_t k)
{
    for (int32_t i = 0; i < nent; i++)
        if (ents[i].t == t && ents[i].k == k)
            return i;
    return -1;
}

static void compact(void)
{
    int32_t n = 0;
    for (int32_t i = 0; i < nent; i++)
        if (ents[i].v[0] != NONE || ents[i].v[1] != NONE)
            ents[n++] = ents[i];
    nent = n;
}

static int32_t count(int32_t s)
{
    int32_t n = 0;
    for (int32_t i = 0; i < nent; i++)
        n += ents[i].v[s] != NONE;
    return n;
}

/* ---- saving ------------------------------------------------------------------------------------------ */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define SAVE_MAGIC 0x53434e53u                          /* "SCNS" */
struct save { uint32_t magic; uint8_t version, n; uint8_t pad[2]; struct ent e[MAXE]; uint32_t sum; };
HEAP_BUF(block, 512)          /* borrowed from the heap while loading or saving */
static int32_t save_wait, loaded, ticks;

static uint32_t checksum(const uint8_t *p, int32_t n)
{
    uint32_t s = 0x5eed1ed5u;
    for (int32_t i = 0; i < n; i++)
        s = (s << 5 | s >> 27) ^ p[i];
    return s;
}

/* the scenes go with the project (common/projsave.h, through PROJECT LINK): each slot's at 0x62400 + slot */
#define PJ_BASE 0x62400
#define PJ_NB 1
#include "../common/projsave.h"
static void load(void)
{
    if (!block_ready()) return;
    struct save *s = (struct save *)block;
    int32_t n = 0;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0)
        return;
    if (s->magic != SAVE_MAGIC || s->version != 1 || s->n > MAXE ||
        s->sum != checksum(block, (int32_t)((uint8_t *)&s->sum - block)))
        return;
    busy = 1;
    for (int32_t i = 0; i < s->n; i++)
        if (s->e[i].t < 8 && allowed(s->e[i].k))
            ents[n++] = s->e[i];
    for (int32_t i = 0; i < n; i++)
        tracked[i] = 0;
    nent = n;
    busy = 0;
}

static void save(void)
{
    if (!block_ready()) return;
    struct save *s = (struct save *)block;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    s->magic = SAVE_MAGIC;
    s->version = 1;
    s->n = (uint8_t)nent;
    for (int32_t i = 0; i < nent; i++)
        s->e[i] = ents[i];
    s->sum = checksum(block, (int32_t)((uint8_t *)&s->sum - block));
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}
static void pj_defaults(void) { busy = 1; unapply(); nent = 0; busy = 0; }

/* ---- recording ----------------------------------------------------------------------------------------- */
static int16_t snap[8][SLOTS];
static uint8_t snap_machine[8];

static void rec_start(int32_t s)
{
    busy = 1;
    unapply();
    for (int32_t t = 0; t < 8; t++) {
        snap_machine[t] = MACHINE(t);
        for (int32_t k = 0; k < SLOTS; k++)
            snap[t][k] = KIT_WORD(t, k);
    }
    for (int32_t i = 0; i < nent; i++)                  /* edit from the scene's own values */
        if (ents[i].v[s] != NONE) {
            KIT_WORD(ents[i].t, ents[i].k) = ents[i].v[s];
            fw_set_param(ents[i].v[s], ents[i].t, ents[i].k);
        }
    rec = s + 1;
    busy = 0;
}

static void rec_end(void)
{
    int32_t s = rec - 1;
    busy = 1;
    rec = 0;
    full = 0;
    for (int32_t i = 0; i < nent; i++)
        ents[i].v[s] = NONE;
    for (int32_t t = 0; t < 8; t++) {
        if (MACHINE(t) != snap_machine[t])
            continue;                                   /* the machine changed: leave that track alone */
        for (int32_t k = 0; k < SLOTS; k++) {
            int16_t kv = KIT_WORD(t, k);
            if (kv == snap[t][k])
                continue;
            if (allowed(k)) {
                int32_t i = find(t, k);
                if (i < 0) {
                    compact();
                    if (nent >= MAXE) { full = 1; i = -1; }
                    else {
                        i = nent;
                        ents[i].t = (uint8_t)t; ents[i].k = (uint8_t)k;
                        ents[i].v[0] = ents[i].v[1] = NONE;
                        tracked[i] = 0;
                        nent = i + 1;
                    }
                }
                if (i >= 0)
                    ents[i].v[s] = kv;
            }
            KIT_WORD(t, k) = snap[t][k];                /* the sound as it was */
            fw_set_param(snap[t][k], t, k);
        }
    }
    compact();
    for (int32_t i = 0; i < nent; i++)
        tracked[i] = 0;
    busy = 0;
    save_wait = 60;
}

static void clear_scene(int32_t s)
{
    busy = 1;
    unapply();
    for (int32_t i = 0; i < nent; i++)
        ents[i].v[s] = NONE;
    compact();
    busy = 0;
    save_wait = 60;
}

/* ---- keys and knobs --------------------------------------------------------------------------------------- */
#define K_FUNC  1
#define K_TRK   2
#define K_YES   12
#define K_NO    13
#define K_UP    14
#define K_DOWN  15
#define K_LEFT  16
#define K_RIGHT 17
#define F_DOWN    0x01
#define F_FUNC    0x02
#define F_REPEAT  0x08
#define F_RELEASE 0x10

static int32_t ui_open, trk_down, swallow_func, swallow_no;
static int32_t info, sel;                               /* 0.3: the info page, and its chosen row */

/* 0.3: take one value out of a scene (s 0 A, 1 B; 2 both), as FUNC + LEFT / RIGHT clears a whole scene */
static void clear_one(int32_t i, int32_t s)
{
    if (i < 0 || i >= nent) return;
    busy = 1;
    unapply();
    if (s != 1) ents[i].v[0] = NONE;
    if (s != 0) ents[i].v[1] = NONE;
    compact();
    busy = 0;
    if (sel >= nent) sel = nent > 0 ? nent - 1 : 0;
    save_wait = 60;
}

int32_t sc_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    int32_t down = (fl & F_DOWN) && !(fl & F_RELEASE), press = down && !(fl & F_REPEAT);
    (void)brain;
    if (key == K_TRK)
        trk_down = down;
    if (key == K_FUNC && swallow_func) {                /* the FUNC that opened or closed us */
        if (!down) swallow_func = 0;
        return 1;
    }
    if (key == K_FUNC && press && trk_down) {
        swallow_func = 1;
        if (rec) {
            rec_end();
            ui_open = 1;
        } else
            ui_open = !ui_open;
        info = 0;
        return 1;
    }
    if (!ui_open)
        return 0;
    if ((key >= 19 && key <= 23) || key == 6 || key == 7) {   /* a page key (TRIG SRC FLTR AMP LFO, GLOBAL, SAMPLE): */
        if (press) { ui_open = 0; info = 0; }         /* close, and let the Digitakt go straight there */
        return 0;
    }
    if (key == K_NO && swallow_no) {
        if (!down) swallow_no = 0;
        return 1;
    }
    if (info) {                                         /* 0.3: the info page */
        if (!press && key != K_UP && key != K_DOWN) return 1;
        if ((key == K_UP || key == K_DOWN) && down) {
            if (key == K_UP && sel > 0) sel--;
            if (key == K_DOWN && sel < nent - 1) sel++;
            return 1;
        }
        if (key == K_NO) { info = 0; swallow_no = 1; return 1; }
        if (fl & F_FUNC) {
            if (key == K_LEFT) clear_one(sel, 0);
            else if (key == K_RIGHT) clear_one(sel, 1);
            else if (key == K_YES) clear_one(sel, 2);
        }
        return 1;
    }
    switch (key) {
    case K_DOWN:
        if (press) { info = 1; if (sel >= nent) sel = 0; }
        return 1;
    case K_NO:
        if (press) { ui_open = 0; swallow_no = 1; }
        return 1;
    case K_YES:
        if (press) xf = xf > XF_MAX / 2 ? 0 : XF_MAX;
        return 1;
    case K_LEFT: case K_RIGHT:
        if (press) {
            int32_t s = key == K_RIGHT;
            if (fl & F_FUNC)
                clear_scene(s);
            else {
                rec_start(s);
                ui_open = 0;
            }
        }
        return 1;
    }
    return 0;
}

int32_t sc_enc(void *brain, void *ev)
{
    int32_t enc = *(int32_t *)((uint8_t *)ev + 12), delta = *(int32_t *)((uint8_t *)ev + 16), v;
    (void)brain;
    if (!ui_open)
        return 0;
    if (enc >= 1 && enc <= 8 && delta) {
        v = xf + delta;                                 /* about 4 a click: a full sweep in ~32 clicks */
        xf = v < 0 ? 0 : v > XF_MAX ? XF_MAX : v;
    }
    return 1;                                           /* every knob is the fader while the page shows */
}

/* ---- drawing (y = 0 is the bottom row; 128 x 64) ------------------------------------------------------- */
static void text(void *bmp, int32_t x, int32_t y, const char *s) { fw_textf(bmp, fw_font5, x, y, 32, "%s", s); }

static void num(char *p, int32_t v)
{
    if (v >= 100) *p++ = (char)('0' + v / 100);
    if (v >= 10) *p++ = (char)('0' + v / 10 % 10);
    *p++ = (char)('0' + v % 10);
    *p = 0;
}

static void draw_page(void *bmp)
{
    char n[8];
    int32_t x0 = 14, x1 = 113, hx = x0 + (x1 - x0) * xf / XF_MAX;
    fw_fillrect(bmp, 0, 0, 127, 63, 0);
    text(bmp, 1, 57, "SCENES");
    num(n, xf);
    text(bmp, 104, 57, n);
    /* which tracks each scene changes: a block per track that has values in it */
    text(bmp, 1, 44, "A");
    text(bmp, 1, 36, "B");
    for (int32_t t = 0; t < 8; t++) {
        int32_t x = 18 + t * 12, ha = 0, hb = 0;
        char tn[3] = { 'T', (char)('1' + t), 0 };
        for (int32_t i = 0; i < nent; i++)
            if (ents[i].t == t) { ha += ents[i].v[0] != NONE; hb += ents[i].v[1] != NONE; }
        text(bmp, x, 51, tn);
        if (ha) fw_fillrect(bmp, x, 45, x + 7, 49, 1); else fw_framerect(bmp, x + 3, 47, x + 4, 47, 1);
        if (hb) fw_fillrect(bmp, x, 37, x + 7, 41, 1); else fw_framerect(bmp, x + 3, 39, x + 4, 39, 1);
    }
    /* the crossfader */
    text(bmp, 1, 22, "A");
    text(bmp, 118, 22, "B");
    fw_framerect(bmp, x0, 21, x1, 29, 1);
    fw_fillrect(bmp, x0, 24, hx, 26, 1);
    fw_fillrect(bmp, hx - 2, 19, hx + 2, 31, 1);
    fw_fillrect(bmp, hx - 1, 20, hx + 1, 30, 0);
    /* the counts and the keys */
    fw_fillrect(bmp, 0, 14, 127, 14, 1);
    if (full)
        text(bmp, 1, 8, "SCENES FULL (80)");
    else {
        char line[24], *p = line;
        const char *a = "A:";
        while (*a) *p++ = *a++;
        num(p, count(0)); while (*p) p++;
        a = "  B:"; while (*a) *p++ = *a++;
        num(p, count(1)); while (*p) p++;
        *p = 0;
        text(bmp, 1, 8, line);
    }
    text(bmp, 64, 8, "YES:A/B  V:INFO");
    text(bmp, 1, 1, "</>:REC  FN+</>:CLEAR");
}

/* 0.3: the info page: every value in the scenes, a row each: the track, the parameter, A's and B's */
static void slot_name(char *p, int32_t k)
{
    static const char *const grp[4] = { "LFO", "SRC", "FLT", "AMP" };
    static const char *const nm[4][8] = {
        { "SPD", "MUL", "FAD", "DST", "WAV", "SPH", "MOD", "DEP" },
        { "TUN", "PLY", "BR", "SMP", "STR", "LEN", "LOP", "LEV" },
        { "ATK", "DEC", "SUS", "REL", "FRQ", "RES", "TYP", "ENV" },
        { "ATK", "HLD", "DEC", "OVR", "DEL", "REV", "PAN", "VOL" } };
    int32_t g = k >= 1 && k <= 16 ? 0 : k >= 17 && k <= 24 ? 1 : k >= 29 && k <= 36 ? 2 : k >= 38 && k <= 45 ? 3 : -1;
    const char *a, *b;
    if (g < 0) { *p++ = 'S'; num(p, k); return; }
    a = grp[g]; b = nm[g][g == 0 ? (k - 1) & 7 : k - (g == 1 ? 17 : g == 2 ? 29 : 38)];
    while (*a) *p++ = *a++;
    if (g == 0) *p++ = k <= 8 ? '1' : '2';
    *p++ = ' ';
    while (*b) *p++ = *b++;
    *p = 0;
}
static void val_text(char *p, int16_t v)
{
    int32_t x = v >> 8;
    if (v == NONE) { p[0] = '-'; p[1] = '-'; p[2] = 0; return; }
    if (x < 0) { *p++ = '-'; x = -x; }
    num(p, x);
}
static void draw_info(void *bmp)
{
    char line[24], n[8];
    fw_fillrect(bmp, 0, 0, 127, 63, 0);
    text(bmp, 1, 57, "SCENE INFO");
    text(bmp, 84, 57, "A");
    text(bmp, 108, 57, "B");
    fw_fillrect(bmp, 0, 54, 127, 54, 1);
    if (!nent) text(bmp, 1, 40, "NOTHING IN THE SCENES");
    int32_t rows = 6, top = sel - rows / 2;
    if (top > nent - rows) top = nent - rows;
    if (top < 0) top = 0;
    for (int32_t r = 0; r < rows && top + r < nent; r++) {
        const struct ent *e = &ents[top + r];
        int32_t y = 46 - r * 7;
        line[0] = 'T'; line[1] = (char)('1' + e->t); line[2] = ' ';
        slot_name(line + 3, e->k);
        text(bmp, 1, y, line);
        val_text(n, e->v[0]); text(bmp, 80, y, n);
        val_text(n, e->v[1]); text(bmp, 104, y, n);
        if (top + r == sel) fw_fillrect(bmp, 0, y - 1, 127, y + 5, -1);
    }
    fw_fillrect(bmp, 0, 7, 127, 7, 1);
    text(bmp, 1, 1, "FN+</>:CLR A/B  FN+YES:ROW");
}

void sc_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (ui_open) {
        if (info) draw_info(bmp); else draw_page(bmp);
        return;
    }
    if (rec) {                                          /* recording: a box at the top */
        fw_fillrect(bmp, 46, 55, 80, 63, 0);
        fw_framerect(bmp, 46, 55, 80, 63, 1);
        fw_textf(bmp, fw_font5, 53, 57, 8, "%s", rec == 1 ? "REC A" : "REC B");
    }
}

void sc_tick(void *ctrl)
{
    if (!loaded && ++ticks > 30) {
        loaded = 1;
        load();
    }
    if (loaded && pj_tick())                            /* a project loaded or saved */
        sel = 0;
    if (save_wait > 0 && --save_wait == 0)
        save();
    if (ui_open || rec)
        *((uint8_t *)ctrl + 0x20) = 1;                  /* redraw */
}
