/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIDI Arp for the Digitakt mk1 (OS 1.54): an arpeggiator on each MIDI track, in time with the
 * sequencer, for the trigs in the pattern and for notes you hold.
 *
 * SETTINGS (in MOD SETTINGS): ARP TRACK picks which MIDI track (M1-M8) the next three rows show:
 *   ARP MODE  OFF, UP, DOWN, UPDN, RND
 *   ARP RATE  1/16, 1/8, 1/4 (every 1, 2 or 4 steps of the track)
 *   ARP OCT   1-3 octaves
 *
 * In the pattern: a note trig starts the arp with its notes (NOTE and NOT2-NOT4: its chord). The trig
 * plays the first note; then a note plays every RATE steps, walking the chord, until the track's next
 * trig. A new note trig starts again with its chord; a lock trig (FUNC + trig) just stops the arp.
 * Live: notes held on the track (the trig keys in chromatic mode, or MIDI in on its channel) are
 * arpeggiated while the sequencer plays, instead of sounding at once; let go and it stops. With the
 * sequencer stopped the keys play as normal.
 *
 * How it works: seq-events. ev_gen_trig adds a note trig on the arp's steps (the firmware then plays
 * it as any trig of the track: its length, its channel, its timing); ev_gen_notes gives that trig
 * its one note; ev_fire_vel gives a live note the velocity it was played with. core's ev_live_note
 * hands us the held notes. The sequencer's handlers run at interrupt level: they only read and count.
 * The held notes are written by the UI or MIDI task into the spare one of two sets and then swapped
 * in with one store, so the sequencer never reads a half-written set. */
#include <stdint.h>
#include "../common/heapbuf.h"
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

#define MT        8                             /* MIDI tracks: sequencer tracks 8-15 */
enum { M_OFF, M_UP, M_DOWN, M_UPDN, M_RND, MODES };
static uint8_t cfg_mode[MT], cfg_rate[MT], cfg_oct[MT];    /* rate 0-2: 1, 2, 4 steps; oct 0-2 */

/* ---- one track's arp ---------------------------------------------------------------------------------- */
#define HELD_MAX 8
struct set { uint8_t n, note[HELD_MAX]; };
struct arp {
    struct set chord;                           /* the last note trig's notes, low to high */
    struct set held[2];                         /* the held notes: [live] is the one in use */
    volatile uint32_t live;
    uint8_t taken[16];                          /* bit per note: its press was ours, so its release is */
    uint8_t vel;                                /* the last held note's velocity */
    uint8_t on;                                 /* the pattern's arp is running */
    uint8_t count;                              /* steps since it started */
    uint8_t gen, from_live, add;                /* this step: (first read's answer, kept for the second) */
    uint32_t pos, rnd;
};
static struct arp arp[MT];
static volatile int32_t steps_seen;
static int32_t last_seen, idle_ticks = 100;
static volatile int32_t stopped = 1;

static int32_t pick(struct arp *a, const struct set *s, int32_t m)
{
    int32_t n = s->n, len = n * (cfg_oct[m] + 1), i, note;
    if (!n)
        return -1;
    switch (cfg_mode[m]) {
    case M_DOWN: i = len - 1 - (int32_t)(a->pos % (uint32_t)len); break;
    case M_UPDN:
        if (len < 2) { i = 0; break; }
        i = (int32_t)(a->pos % (uint32_t)(2 * len - 2));
        if (i >= len) i = 2 * len - 2 - i;
        break;
    case M_RND:
        a->rnd = a->rnd * 1664525u + 1013904223u;
        i = (int32_t)((a->rnd >> 16) % (uint32_t)len);
        break;
    default: i = (int32_t)(a->pos % (uint32_t)len); break;
    }
    a->pos++;
    note = s->note[i % n] + 12 * (i / n);
    while (note > 127)
        note -= 12;
    return note;
}

/* ---- the sequencer (interrupt level) ------------------------------------------------------------------ */
void ma_gen_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    int32_t m = track - SQ_AUDIO;
    struct arp *a;
    (void)step;
    if (track == 0 && first)
        steps_seen++;
    if (m < 0 || m >= MT || cfg_mode[m] == M_OFF)
        return;
    a = &arp[m];
    if (first) {
        int32_t rate = 1 << cfg_rate[m];
        int32_t stored = (*trig & SQ_TRIG) != 0;
        a->gen = a->from_live = a->add = 0;
        if (a->held[a->live & 1].n) {                   /* held notes: they are the arp */
            if (a->count % rate == 0) {
                a->gen = a->from_live = 1;
                a->add = (uint8_t)!stored;
            }
            a->count++;
            a->on = 0;
        } else if (stored) {                            /* a note trig: start with its chord */
            a->on = 1;
            a->count = 1;
            a->pos = 0;
            a->gen = 1;
        } else if (*trig & SQ_LOCK) {                   /* a lock trig: stop */
            a->on = 0;
        } else if (a->on) {
            if (a->count % rate == 0)
                a->gen = a->add = 1;
            a->count++;
        }
    }
    if (a->add)
        *trig |= SQ_TRIG;
}

void ma_gen_notes(int32_t track, int32_t step, int32_t generated, int8_t notes[SQ_NOTES])
{
    int32_t m = track - SQ_AUDIO, i, j, note;
    struct arp *a;
    (void)step;
    if (m < 0 || m >= MT || cfg_mode[m] == M_OFF)
        return;
    a = &arp[m];
    if (!a->gen)
        return;
    if (a->from_live) {
        note = pick(a, &a->held[a->live & 1], m);
    } else {
        if (!generated) {                               /* the note trig: take its chord, low to high */
            struct set *c = &a->chord;
            c->n = 0;
            if (notes[0] < 0)
                return;
            for (i = 0; i < SQ_NOTES; i++) {
                int32_t v = i ? notes[0] + notes[i] : notes[0];
                if (i && !notes[i])
                    continue;
                if (v < 0 || v > 127)
                    continue;
                for (j = c->n; j > 0 && c->note[j - 1] > v; j--)
                    c->note[j] = c->note[j - 1];
                c->note[j] = (uint8_t)v;
                c->n++;
            }
        }
        note = pick(a, &a->chord, m);
    }
    if (note < 0)
        return;
    notes[0] = (int8_t)note;
    for (i = 1; i < SQ_NOTES; i++)
        notes[i] = 0;
}

void ma_vel(int32_t track, int32_t step, int32_t *vel)
{
    int32_t m = track - SQ_AUDIO;
    (void)step;
    if (m < 0 || m >= MT || cfg_mode[m] == M_OFF)
        return;
    if (arp[m].gen && arp[m].from_live && arp[m].vel)
        *vel = arp[m].vel;
}

/* ---- held notes (UI or MIDI task) --------------------------------------------------------------------- */
int32_t ma_live(int32_t track, int32_t note, int32_t vel, int32_t on, int32_t source)
{
    int32_t m = track - SQ_AUDIO, i, j;
    struct arp *a;
    struct set *cur, *nxt;
    (void)source;
    if (m < 0 || m >= MT || note < 0 || note > 127)
        return 0;
    a = &arp[m];
    if (on) {
        if (cfg_mode[m] == M_OFF || stopped)
            return 0;                                   /* plays as normal */
        a->taken[note >> 3] |= (uint8_t)(1u << (note & 7));
    } else {
        if (!(a->taken[note >> 3] & (1u << (note & 7))))
            return 0;                                   /* its press was not ours */
        a->taken[note >> 3] &= (uint8_t)~(1u << (note & 7));
    }
    cur = &a->held[a->live & 1];
    nxt = &a->held[(a->live + 1) & 1];
    nxt->n = 0;
    for (i = 0; i < cur->n; i++)                        /* the set without the note */
        if (cur->note[i] != note)
            nxt->note[nxt->n++] = cur->note[i];
    if (on && nxt->n < HELD_MAX) {                      /* and with it, in its place */
        for (j = nxt->n; j > 0 && nxt->note[j - 1] > note; j--)
            nxt->note[j] = nxt->note[j - 1];
        nxt->note[j] = (uint8_t)note;
        nxt->n++;
        a->vel = (uint8_t)(vel > 127 ? 127 : vel < 1 ? 1 : vel);
        if (!cur->n) {                                  /* the first key: start from the top */
            a->pos = 0;
            a->count = 0;
        }
    }
    a->live++;
    return 1;
}

/* ---- SETTINGS rows ------------------------------------------------------------------------------------ */
#define XOFF      -4
#define YOFF      0
#define TEXT_ADV  4
#define BOX_DOWN  1
#define BOX_UP    6
#define TOP_LIMIT 52
enum { ROW_TRACK, ROW_MODE, ROW_RATE, ROW_OCT, ROWS };
static int32_t sel;                             /* the MIDI track the rows show */
static int32_t save_wait;

static void redraw(void **payload)
{
    fw_invalidate((char *)*payload + 0x38);
}

static void step_row(int32_t row, int32_t delta)
{
    int32_t d = delta > 0 ? 1 : -1, v;
    if (!delta)
        return;
    switch (row) {
    case ROW_TRACK: sel = (sel + d + MT) % MT; return;
    case ROW_MODE:  v = cfg_mode[sel] + d; cfg_mode[sel] = (uint8_t)(v < 0 ? 0 : v >= MODES ? MODES - 1 : v); break;
    case ROW_RATE:  v = cfg_rate[sel] + d; cfg_rate[sel] = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v); break;
    default:        v = cfg_oct[sel] + d;  cfg_oct[sel] = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v); break;
    }
    save_wait = 60;
}

static void draw_row(int32_t row, void *bmp, int32_t x, int32_t y)
{
    static const char *const modes[MODES] = {"OFF", "UP", "DOWN", "UPDN", "RND"};
    static const char *const rates[3] = {"1/16", "1/8", "1/4"};
    static const char *const tracks[MT] = {"M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8"};
    static const char *const octs[3] = {"1", "2", "3"};
    const char *text;
    int32_t n = 0, tx;
    switch (row) {
    case ROW_TRACK: text = tracks[sel]; break;
    case ROW_MODE:  text = modes[cfg_mode[sel] % MODES]; break;
    case ROW_RATE:  text = rates[cfg_rate[sel] % 3]; break;
    default:        text = octs[cfg_oct[sel] % 3]; break;
    }
    while (text[n]) n++;
    if (y + YOFF + BOX_UP > TOP_LIMIT)
        return;
    tx = x + XOFF - TEXT_ADV * n;
    fw_fillrect(bmp, tx - 2, y + YOFF - BOX_DOWN, x + XOFF, y + YOFF + BOX_UP, 0);
    fw_textf(bmp, fw_font5, tx, y + YOFF, 16, "%s", text);
}

#define ROW_FNS(name, row)                                                                        \
    void ma_##name##_select(void **payload, void *item) { (void)item; step_row(row, 1); redraw(payload); } \
    void ma_##name##_change(void **payload, void *item, int32_t delta) { (void)item; step_row(row, delta); redraw(payload); } \
    void ma_##name##_draw(void *payload, void *item, void *bmp, int32_t x, int32_t y) { (void)payload; (void)item; draw_row(row, bmp, x, y); }
ROW_FNS(track, ROW_TRACK) ROW_FNS(mode, ROW_MODE) ROW_FNS(rate, ROW_RATE) ROW_FNS(oct, ROW_OCT)

extern const uint32_t ma_row_track[], ma_row_mode[], ma_row_rate[], ma_row_oct[];
extern void core_additem(void *menu, const void *row);
extern volatile int32_t modset_closed;          /* MOD SETTINGS: weak, 0 without that mod */

void ma_settings(void *menu)
{
    if (modset_closed)
        return;
    core_additem(menu, ma_row_track);
    core_additem(menu, ma_row_mode);
    core_additem(menu, ma_row_rate);
    core_additem(menu, ma_row_oct);
}

/* ---- saving, and the stopped sequencer ---------------------------------------------------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define MAGIC 0x4d415250u                       /* "MARP" */
struct save { uint32_t magic; uint8_t mode[MT], rate[MT], oct[MT]; uint32_t sum; };
HEAP_BUF(block, 512)          /* borrowed from the heap while loading or saving */
static int32_t loaded, ticks;

static uint32_t sum_of(const struct save *s)
{
    uint32_t x = s->magic;
    int32_t i;
    for (i = 0; i < MT; i++)
        x = x * 31 + s->mode[i] + (s->rate[i] << 8) + (s->oct[i] << 16);
    return x;
}

static void load(void)
{
    if (!block_ready()) return;
    struct save *s = (struct save *)block;
    int32_t i;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0 || s->magic != MAGIC || s->sum != sum_of(s))
        return;
    for (i = 0; i < MT; i++) {
        cfg_mode[i] = (uint8_t)(s->mode[i] < MODES ? s->mode[i] : 0);
        cfg_rate[i] = (uint8_t)(s->rate[i] < 3 ? s->rate[i] : 0);
        cfg_oct[i] = (uint8_t)(s->oct[i] < 3 ? s->oct[i] : 0);
    }
}

static void save(void)
{
    if (!block_ready()) return;
    struct save *s = (struct save *)block;
    int32_t i;
    for (i = 0; i < 512; i++)
        block[i] = 0;
    s->magic = MAGIC;
    for (i = 0; i < MT; i++) {
        s->mode[i] = cfg_mode[i];
        s->rate[i] = cfg_rate[i];
        s->oct[i] = cfg_oct[i];
    }
    s->sum = sum_of(s);
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

void ma_tick(void *ctrl)
{
    int32_t seen = steps_seen, i;
    (void)ctrl;
    if (!loaded && ++ticks > 30) {
        loaded = 1;
        load();
    }
    if (save_wait > 0 && --save_wait == 0)
        save();
    if (seen != last_seen) {
        last_seen = seen;
        idle_ticks = 0;
        stopped = 0;
    } else if (idle_ticks < 1000 && ++idle_ticks == 20) {   /* no step for 2/3 s: stopped */
        stopped = 1;
        for (i = 0; i < MT; i++)                /* so nothing left over plays at the next start */
            arp[i].on = 0;
    }
}
