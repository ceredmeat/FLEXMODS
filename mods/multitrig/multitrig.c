/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MULTITRIG 0.5 for the Digitakt mk1, OS 1.54: edit many trigs' locks at once, in GRID RECORDING.
 *
 *   - Hold a trig, tap PAGE: every trig on that trig's page is selected too.
 *   - Hold a trig, tap TRK:  every trig on the track is selected too.
 * Turn a knob: the held trig changes as on stock, then every selected trig with it:
 *   REL (SETTINGS > MULTI EDIT): by the same amount, each keeping its own value (transpose a page);
 *   ABS: every selected trig takes the held trig's new value.
 * Let go of the trig(s) and the selection clears.
 *
 * How (0.1-0.4 crashed the unit: they hid TRK and PAGE from the grid screen only, and other parts of
 * the Digitakt still acted on them, and they marked steps held that no finger was on):
 *   - TRK and PAGE are taken at the very first stop, core's ev_key, while a trig is held: nothing in
 *     the Digitakt sees them. The selection is kept here, not in the Digitakt's held-steps mask;
 *   - the knob-on-held-trigs routine builds a little function for each held step; its address
 *     (0x40031386, in 0x400315dc's instruction) points at mt_invoke, which, after the stock edits
 *     the held trig, edits the selected ones with the same routine (REL) or writes the held trig's
 *     new value with the page's own lock routine (ABS). */
#include <stdint.h>
#include "digitakt-mk1/core3.h"

static volatile uint8_t set_abs;                      /* SETTINGS > MULTI EDIT: 0 REL, 1 ABS */

typedef uint32_t (*fn0)(void);
typedef uint32_t (*fn1)(uint32_t);
typedef uint32_t (*fn2)(uint32_t, uint32_t);
#define APP()          (((fn0)0x40138882)())
#define PROJECT(a)     (((fn1)0x40014d86)(a))
#define ACTIVE(p)      (((fn1)0x4001d24e)(p))
#define PATTERNS(a)    (((fn1)0x40015786)(a))
#define TRACK_PAT(s,t) (((fn2)0x40012a02)((s), (t)))
#define HELD()         ((const uint8_t *)((fn0)0x40138928)())
#define TRACK_LEN(t)   (((fn1)0x40024700)(t))
#define STEP_BITS   ((int32_t (*)(uint32_t trk, int32_t step, int32_t mask))0x40023b32)
typedef void (*invoke_fn)(void **fn, int32_t step);
#define STOCK_INVOKE    ((invoke_fn)0x40031386)

#define KEY_TRK 2
#define KEY_PAGE 18
#define FL_PRESSED 0x01
#define FL_FUNC 0x02
#define FL_REPEAT 0x08
#define FL_RELEASE 0x10

static uint32_t sel_hi, sel_lo, seq, done;
static int32_t active, swallow_trk, swallow_page;

static int held_bit(const uint8_t *h, int32_t s)
{
    uint32_t hi = *(const uint32_t *)(h + 344), lo = *(const uint32_t *)(h + 348);
    return s < 32 ? (lo >> s) & 1 : (hi >> (s - 32)) & 1;
}

static void select_steps(int32_t from, int32_t to)
{
    uint32_t app = APP(), trk = TRACK_PAT(PATTERNS(app), ACTIVE(PROJECT(app)));
    int32_t len, s;
    if (!trk) return;
    len = (int32_t)TRACK_LEN(trk);
    if (len < 1 || len > 64) return;
    if (to > len) to = len;
    for (s = from; s < to; s++)
        if (STEP_BITS(trk, s, 1) & 0xff) {
            if (s < 32) sel_lo |= 1u << s; else sel_hi |= 1u << (s - 32);
        }
    active = 1;
}

int32_t mt_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    int32_t down = (fl & FL_PRESSED) && !(fl & FL_RELEASE);
    const uint8_t *h = HELD();
    (void)brain;
    if (key == KEY_TRK && swallow_trk) { if (!down) swallow_trk = 0; return 1; }
    if (key == KEY_PAGE && swallow_page) { if (!down) swallow_page = 0; return 1; }
    if ((key == KEY_TRK || key == KEY_PAGE) && down && !(fl & (FL_FUNC | FL_REPEAT)) && h && h[352]) {
        int32_t first = -1;
        for (int32_t s = 0; s < 64; s++) if (held_bit(h, s)) { first = s; break; }
        if (first < 0) return 0;
        if (key == KEY_TRK) { select_steps(0, 64); swallow_trk = 1; }
        else { int32_t p = first / 16; select_steps(p * 16, p * 16 + 16); swallow_page = 1; }
        return 1;
    }
    return 0;
}

void mt_sel_tick(void)
{
    const uint8_t *h = HELD();
    if (active && (!h || (!*(const uint32_t *)(h + 344) && !*(const uint32_t *)(h + 348)))) {
        active = 0;
        sel_hi = sel_lo = 0;
    }
}

int32_t mt_enc(void *brain, void *ev) { (void)brain; (void)ev; seq++; return 0; }

struct turn { void *view; int32_t id; int32_t delta; };
typedef int32_t (*lock_read_fn)(void *view, int32_t step, int32_t id);          /* -1: none */
typedef int32_t (*lock_write_fn)(void *view, int32_t step, int32_t id, int32_t value);
#define VT(view, off)   ((*(void ***)(view))[(off) / 4])

void mt_invoke(void **fn, int32_t step)
{
    struct turn *t = (struct turn *)*fn;
    STOCK_INVOKE(fn, step);
    if (!active || !t || !t->delta || done == seq)
        return;
    done = seq;                                         /* once a turn: the first held trig's */
    const uint8_t *h = HELD();
    int32_t target = ((lock_read_fn)VT(t->view, 112))(t->view, step, t->id);
    for (int32_t s = 0; s < 64; s++) {
        uint32_t bit = s < 32 ? (sel_lo >> s) & 1 : (sel_hi >> (s - 32)) & 1;
        if (!bit || s == step || (h && held_bit(h, s)))
            continue;                                   /* held ones: the stock's own loop */
        if (!set_abs)
            STOCK_INVOKE(fn, s);
        else if (target >= 0)
            ((lock_write_fn)VT(t->view, 116))(t->view, s, t->id, target);
    }
}

/* ---- saving (as the other mods: one eMMC block, two seconds after a change) ---------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define SAVE_MAGIC 0x4d545247u                          /* "MTRG" */
struct save { uint32_t magic; uint8_t version, abs, pad[2]; uint32_t sum; };
static uint8_t block[512] __attribute__((aligned(16)));
static int32_t save_wait, loaded, ticks;

static uint32_t checksum(const uint8_t *p, int32_t n)
{
    uint32_t s = 0x5eed1ed5u;
    for (int32_t i = 0; i < n; i++)
        s = (s << 5 | s >> 27) ^ p[i];
    return s;
}
static void load(void)
{
    struct save *s = (struct save *)block;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0)
        return;
    if (s->magic != SAVE_MAGIC || s->version != 1 ||
        s->sum != checksum(block, (int32_t)((uint8_t *)&s->sum - block)))
        return;
    set_abs = s->abs & 1;
}
static void save(void)
{
    struct save *s = (struct save *)block;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    s->magic = SAVE_MAGIC;
    s->version = 1;
    s->abs = set_abs;
    s->sum = checksum(block, (int32_t)((uint8_t *)&s->sum - block));
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}
void mt_tick(void *ctrl)
{
    (void)ctrl;
    mt_sel_tick();               /* let go of every trig: the selection clears */
    if (!loaded && ++ticks > 30) {
        loaded = 1;
        load();
    }
    if (save_wait > 0 && --save_wait == 0)
        save();
}

/* ---- SETTINGS row: MULTI EDIT (REL / ABS) ------------------------------------------------------------ */
#define XOFF      -4
#define YOFF      0
#define TEXT_ADV  4
#define BOX_DOWN  1
#define BOX_UP    6
#define TOP_LIMIT 52

static void redraw(void **payload) { fw_invalidate((char *)*payload + 0x38); }

static void draw_text(void *bmp, int32_t x, int32_t y, const char *text)
{
    int32_t n = 0, tx;
    while (text[n]) n++;
    if (y + YOFF + BOX_UP > TOP_LIMIT)
        return;
    tx = x + XOFF - TEXT_ADV * n;
    fw_fillrect(bmp, tx - 2, y + YOFF - BOX_DOWN, x + XOFF, y + YOFF + BOX_UP, 0);
    fw_textf(bmp, fw_font5, tx, y + YOFF, 16, "%s", text);
}

void mt_mode_select(void **payload, void *item) { (void)item; set_abs ^= 1; save_wait = 60; redraw(payload); }
void mt_mode_change(void **payload, void *item, int32_t delta)
{
    (void)item;
    if (delta) { set_abs = delta > 0; save_wait = 60; redraw(payload); }
}
void mt_mode_draw(void *payload, void *item, void *bmp, int32_t x, int32_t y)
{
    (void)payload; (void)item;
    draw_text(bmp, x, y, set_abs ? "ABS" : "REL");
}
extern const uint32_t mt_row_mode[];
extern void core_additem(void *menu, const void *row);
extern volatile int32_t modset_closed;                  /* MOD SETTINGS: weak, 0 without that mod */

void mt_settings(void *menu)
{
    if (modset_closed)
        return;
    core_additem(menu, mt_row_mode);
}
