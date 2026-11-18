/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TRIG MODES for the Digitakt mk1 (core 4.2 + seq-events, OS 1.54), after the Digitakt II's: the 16 trig
 * keys become something to play.
 *
 *   NO + PAGE (hold NO, press PAGE) steps through them; a box says which, and a small one stays in the
 *   top corner while a mode is on:
 *     OFF   the trig keys as stock
 *     TRKS  keys 1-8 play audio tracks 1-8, keys 9-16 MIDI tracks 1-8: finger drumming on every track
 *     VEL   the keys play the active track, key 1 the softest, key 16 the loudest
 *     RTRG  hold a key: the active track repeats in time. Keys 1-8: 1/4, 1/8, 1/8 triplets, 1/16, 1/16
 *           triplets, 1/32, 1/32 triplets, 1/64; keys 9-16 the same, each hit softer (a fade-out roll)
 *     RTRK  after the Model:Samples' retrig: keys 1-8 each repeat their own track (1-8) while held, as
 *           many at once as you like; keys 9-16 pick the rate for all (1/4 ... 1/64, as RTRG's 1-8),
 *           which stays until another is picked (1/16 to start)
 *   TRK + a trig still picks a track, FUNC + a trig is the stock's and YES / NO + a trig mutesnap's, in
 *   every mode.
 *
 * Notes go out as the trig keys' own do (fw_note_on, source the panel). They are played, not recorded.
 * RTRG's timing: the length of a step is measured while the pattern plays (the active track's steps,
 * counted in render blocks, ev_render_master); stopped, it keeps the last one (120 BPM to start). */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

enum { M_OFF, M_TRKS, M_VEL, M_RTRG, M_RTRK, MODES };
static const char *const mname[MODES] = { "OFF", "TRKS", "VEL", "RTRG", "RTRK" };
static const char *const mlong[MODES] = { "TRIG MODE OFF", "TRIG MODE TRACKS", "TRIG MODE VELOCITY", "TRIG MODE RETRIG",
                                          "TRIG MODE RETRIG TRACKS" };
static volatile int32_t mode;

#define K_FUNC  1
#define K_TRK   2
#define K_YES   12
#define K_NO    13
#define K_PAGE  18
#define K_TRIG1 24
#define F_DOWN    0x01
#define F_FUNC    0x02
#define F_REPEAT  0x08
#define F_RELEASE 0x10
#define NOTE      60                                /* the track's own pitch, as a trig key plays it */
#define PANEL     0x40

/* ---- the clock: render blocks, and how many make a step (x 256) -------------------------------------- */
static volatile uint32_t blocks;
static volatile int32_t step_q8 = 188 * 256;         /* 120 BPM: a 16th is 125 ms, 187.5 blocks */
static uint32_t last_seen = SQ_CLOCK_NONE;
static volatile uint32_t last_at;                    /* the block the active track's last step began */

void tm_render(int32_t *pair)
{
    (void)pair;
    uint32_t b = ++blocks;
    int32_t t = (int32_t)fw_active_track;
    uint32_t cur = sq_clock[t & 15].last;
    if (cur != last_seen) {                          /* a new step on the active track: how long it took */
        uint32_t d = b - last_at;
        if (last_seen != SQ_CLOCK_NONE && d >= 20 && d <= 2000)
            step_q8 += ((int32_t)(d << 8) - step_q8) >> 2;
        last_seen = cur;
        last_at = b;
    }
}

/* ---- rolls: one a track at most (RTRG: the active track's; RTRK: any of tracks 1-8) ------------------------ */
static const uint16_t rate_q8[8] = { 1024, 512, 341, 256, 171, 128, 85, 64 };   /* steps a hit, x 256 */
static const char *const rate_n[8] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
struct roll {
    volatile int8_t on;                              /* rolling */
    volatile int8_t rate;                            /* 0-7 */
    volatile int8_t fade;                            /* each hit softer */
    int8_t sounding;                                 /* a note of ours is on */
    int8_t first;                                    /* the press's own hit, right away */
    volatile int32_t vel;
    uint32_t next, start;                            /* blocks: the next hit, the press */
};
static struct roll roll[16];
static int32_t rtrk_rate = 3;                        /* RTRK's rate: 1/16 to start */

static int32_t active(void) { int32_t t = (int32_t)fw_active_track; return t < 0 ? 0 : t > 15 ? 15 : t; }
static int playing(void) { return blocks - last_at < (uint32_t)(step_q8 >> 8) * 2u; }
static int32_t on_grid(int32_t r) { int32_t q = rate_q8[r & 7]; return (q & 255) == 0 ? q >> 8 : 0; }   /* steps a hit */

static void roll_start(int32_t t, int32_t rate, int32_t fade)
{
    struct roll *r = &roll[t];
    r->rate = (int8_t)rate; r->fade = (int8_t)fade; r->vel = 110;
    r->next = r->start = blocks;
    r->first = 1;
    r->on = 1;
}
static void roll_stop(int32_t t)
{
    struct roll *r = &roll[t];
    r->on = 0;
    if (r->sounding) fw_note_off(t, NOTE, PANEL);
    r->sounding = 0;
}
static void roll_stop_all(void) { for (int32_t t = 0; t < 16; t++) roll_stop(t); }

/* ---- keys ------------------------------------------------------------------------------------------------- */
static int32_t no_down, yes_down, trk_down, swallow_page, shown, shown_rate;
static int32_t held_key = -1, held_track;           /* VEL / TRKS: the note sounding from a key */
static int32_t rtrg_key = -1;                        /* RTRG: the key held */

int32_t tm_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    int32_t down = (fl & F_DOWN) && !(fl & F_RELEASE), press = down && !(fl & F_REPEAT);
    (void)brain;
    if (key == K_NO) no_down = down;
    if (key == K_YES) yes_down = down;
    if (key == K_TRK) trk_down = down;
    if (key == K_PAGE && swallow_page) {
        if (!down) swallow_page = 0;
        return 1;
    }
    if (key == K_PAGE && press && no_down) {         /* NO + PAGE: the next mode */
        roll_stop_all();
        rtrg_key = -1;
        mode = (mode + 1) % MODES;
        shown = 120;
        shown_rate = 0;
        swallow_page = 1;
        return 1;
    }
    if (mode == M_OFF || key < K_TRIG1 || key > K_TRIG1 + 15)
        return 0;
    if (trk_down || yes_down || no_down || (fl & F_FUNC))
        return 0;                                    /* TRK / FUNC + trig: the stock's; YES / NO + trig: mutesnap's */
    if (fl & F_REPEAT)
        return 1;
    int32_t k = key - K_TRIG1;
    if (mode == M_RTRG) {
        int32_t t = active();
        if (down) {
            roll_stop_all();
            rtrg_key = k;
            roll_start(t, k & 7, k >= 8);
        } else if (k == rtrg_key) {
            roll_stop_all();
            rtrg_key = -1;
        }
        return 1;
    }
    if (mode == M_RTRK) {
        if (k >= 8) {                                /* keys 9-16: the rate, for every roll */
            if (down) {
                rtrk_rate = k - 8;
                for (int32_t t = 0; t < 8; t++) roll[t].rate = (int8_t)rtrk_rate;
                shown = 60;
                shown_rate = 1;
            }
        } else if (down)
            roll_start(k, rtrk_rate, 0);             /* keys 1-8: their own track rolls while held */
        else
            roll_stop(k);
        return 1;
    }
    if (down) {
        int32_t t = mode == M_TRKS ? k : active();
        int32_t vel = mode == M_VEL ? 8 + k * 119 / 15 : 100;
        if (held_key >= 0) fw_note_off(held_track, NOTE, PANEL);
        fw_note_on(t, NOTE, vel, PANEL, 0, -1, -1);
        held_key = k; held_track = t;
    } else if (k == held_key) {
        fw_note_off(held_track, NOTE, PANEL);
        held_key = -1;
    }
    return 1;
}

/* ---- the rolls' hits -------------------------------------------------------------------------------------- */
/* The rolls on the step grid (1/4, 1/8, 1/16) are the sequencer's own trigs while it plays (seq-events,
 * exact); the others (triplets, 1/32, 1/64), and every roll while stopped, are the UI task's. */
void tm_gen_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    int32_t every;
    (void)first;
    if (track < 0 || track > 15 || step < 0 || !roll[track].on || !(every = on_grid(roll[track].rate)))
        return;
    if (step % every == 0)
        *trig |= SQ_TRIG;
}

void tm_fire_vel(int32_t track, int32_t step, int32_t *vel)
{
    struct roll *r;
    (void)step;
    if (track < 0 || track > 15 || !(r = &roll[track])->on || !on_grid(r->rate))
        return;
    *vel = r->vel;
    if (r->fade)
        r->vel = r->vel * 85 / 100 > 6 ? r->vel * 85 / 100 : 6;           /* the fade-out roll */
}

/* The hits sit on a grid locked to the sequencer's steps: each next hit is worked out from the last step's
 * start, never from the hit before, so a late hit (the UI task busy) does not push the rest later. Each
 * hit plays, even a little late: none is skipped. */
static uint32_t grid_next(uint32_t now, uint32_t gap_q8, uint32_t start)     /* gap in blocks x 256 */
{
    uint32_t anchor = last_at;
    if (now - anchor > (uint32_t)(step_q8 >> 8) * 2u)  /* no step for two: stopped, run free */
        anchor = start;
    uint32_t from = (now - anchor) << 8, k = from / gap_q8 + 1;
    uint32_t at = anchor + ((k * gap_q8) >> 8);
    if ((at - now) << 8 < gap_q8 / 2)                 /* too close (a new step's grid): the one after */
        at = anchor + (((k + 1) * gap_q8) >> 8);
    return at;
}

void tm_tick(void *ctrl)
{
    uint32_t now = blocks;
    for (int32_t t = 0; t < 16; t++) {
        struct roll *r = &roll[t];
        if (!r->on || (int32_t)(now - r->next) < 0)
            continue;
        uint32_t gap = (uint32_t)(((step_q8 >> 4) * rate_q8[r->rate & 7]) >> 4);   /* x 256 */
        if (gap < 512) gap = 512;
        if (!r->first && on_grid(r->rate) && playing()) {
            /* the sequencer plays this one */
        } else {
            r->first = 0;
            if (r->sounding) fw_note_off(t, NOTE, PANEL);
            fw_note_on(t, NOTE, r->vel, PANEL, 0, -1, -1);
            r->sounding = 1;
            if (r->fade)
                r->vel = r->vel * 85 / 100 > 6 ? r->vel * 85 / 100 : 6;
        }
        r->next = grid_next(now, gap, r->start);
    }
    if (shown > 0) {
        shown--;
        *((uint8_t *)ctrl + 0x20) = 1;
    }
}

void tm_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (shown > 0) {                                 /* the mode just chosen (or RTRK's rate), in the middle */
        char buf[28];
        const char *s = mlong[mode];
        if (shown_rate) {
            const char *a = "RETRIG RATE ", *b = rate_n[rtrk_rate];
            char *p = buf;
            while (*a) *p++ = *a++;
            while (*b) *p++ = *b++;
            *p = 0;
            s = buf;
        }
        int32_t n = 0;
        while (s[n]) n++;
        int32_t w = 4 * n + 8, x = 64 - w / 2;
        fw_fillrect(bmp, x, 26, x + w, 38, 0);
        fw_framerect(bmp, x, 26, x + w, 38, 1);
        fw_textf(bmp, fw_font5, x + 5, 30, 28, "%s", s);
    }
    if (mode != M_OFF) {                             /* a chip in the top right while a mode is on */
        fw_fillrect(bmp, 108, 56, 127, 63, 0);
        fw_textf(bmp, fw_font5, 110, 57, 8, "%s", mname[mode]);
        fw_fillrect(bmp, 108, 56, 127, 63, -1);
    }
}
