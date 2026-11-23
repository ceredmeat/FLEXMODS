/* SPDX-License-Identifier: GPL-2.0-or-later */
/* VISUALS for the Digitakt mk1 (core 4.1 + seq-events, OS 1.54): eye candy. Master pages (FUNC + LFO,
 * then LEFT / RIGHT) and two effects over every screen.
 *
 *   RDR  beat radar: a sweep goes round once a loop of the active track; every hit leaves a blip at its own
 *        track's place in its own loop (track 1 the inner ring, 8 the outer) that pings like a sonar contact,
 *        then fades out by the time the sweep comes round again. A polymeter's blips drift.
 *   PET  a little creature that lives on the music: it bounces on the beat, throws its arms up on loud hits,
 *        gets hungry and sad in silence, falls asleep after 20 s of it. Turn any knob to pet it.
 *   (Snake and Pong moved to the GAMES mod in 0.4.)
 *   SCR  the screen effects, saved:
 *        CRT  OFF, SFT (a refresh line rolls down the screen) or FUL (that, the picture switching off to
 *             a line and a dot when the pattern stops and back on when it plays, and now and then a glitch)
 *        INV  invert the whole screen for a moment: OFF, ACC (a hit at or above THR), BAR (beat 1 of each
 *             bar of the active track), KCK (each track 1 hit)
 *        THR  the accent's velocity, 64-127
 *
 * The effects draw on ev_draw, on top of every composed frame; the pages and games run on ev_tick; the
 * hits come from seq-events (ev_fire_vel, at interrupt level: a few stores) and the steps from ev_gen_trig. */
#include <stdint.h>
#include "../common/heapbuf.h"
typedef uint32_t (*u32_fn)(void);
typedef int32_t (*pat_fn)(uint32_t ui);
#define UI_STATE ((u32_fn)UI_STATE_AT)                 /* as KIT A/B uses them */
#define CUR_PAT  ((pat_fn)CUR_PAT_AT)
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"
#include "../common/mpui.h"

#define K_YES   12
#define K_NO    13
#define K_UP    14
#define K_DOWN  15
#define K_LEFT  16
#define K_RIGHT 17
#define F_DOWN    0x01
#define F_REPEAT  0x08
#define F_RELEASE 0x10
#define NOTE  60
#define PANEL 0x40

static volatile uint32_t ticks;
static uint32_t rnd_s = 0x13579bdu;
static uint32_t rnd(uint32_t n) { rnd_s ^= rnd_s << 13; rnd_s ^= rnd_s >> 17; rnd_s ^= rnd_s << 5; return n ? rnd_s % n : 0; }
static int32_t active(void) { int32_t t = (int32_t)fw_active_track; return t < 0 ? 0 : t > 15 ? 15 : t; }
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

/* ---- what the sequencer does (interrupt level: stores only) ------------------------------------------- */
static volatile uint32_t last_step_tick, fires, flash_req, kick_req, bar_req, accent_req;
/* 0.4: the tempo and bar from track 1's steps, timed in audio blocks */
static volatile uint32_t blks;                 /* audio blocks, 1500 a second */
static volatile uint32_t t1_at, t1_steps, t1_start, t1_hist[16];      /* track 1's last step (blocks) and steps since PLAY */
static volatile uint32_t step_blk_q8 = 48000;  /* blocks a track 1 step, x 256 (120 BPM to start) */
static volatile int32_t beat_req;              /* a beat of the active track: 1, or 2 on beat 1 */
static volatile int32_t step_req[16];
static volatile int32_t name_req;              /* 0.5: a track to name, big */
static int32_t art_mode, rain_mode, names, invt, boot_on = 1, tag;     /* 0.5: the ART page */
static int32_t boot_t = -1;                    /* the start-up splash's frame; -1 not yet */
#define BOOT_LEN 150
static volatile uint32_t life_req;             /* tracks that hit: the Game of Life plants something */
static int32_t life_shown;                     /* the Life art is on screen (as of the last tick) */
static int32_t thr = 110;                      /* SCR THR */

#define BLIPS 48
struct blip { uint8_t track, ang, big, live; uint32_t at; };    /* at: the sweep's whole angle when born */
static struct blip blips[BLIPS];
static volatile uint32_t blip_next;
static volatile uint32_t sweep_abs;            /* the sweep's angle, counting whole turns (x 256) */

void vs_step(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    (void)first; (void)trig;
    if (track < 0 || track > 15 || step < 0) return;
    step_req[track] = step + 1;
    last_step_tick = ticks;
    if (track == active() && (step & 15) == 0) bar_req = 1;
    if (track == active() && (step & 3) == 0) beat_req = (step & 15) == 0 ? 2 : 1;
    if (track == 0) {                              /* 0.4: the tempo and the bar, from track 1 */
        uint32_t now = blks, d = now - t1_at;
        if (d >= 8) {
            if (d > (step_blk_q8 >> 7)) { t1_steps = 0; t1_start = now; }   /* the first step after a stop */
            else {
                t1_steps++;
                /* over the last bar (16 steps): swing and the small timing wobbles cancel out */
                uint32_t n = t1_steps < 16 ? t1_steps : 16, then = t1_steps < 16 ? t1_start : t1_hist[t1_steps & 15];
                int32_t target = (int32_t)(((now - then) << 8) / n);
                step_blk_q8 += (target - (int32_t)step_blk_q8) >> 2;
            }
            t1_hist[t1_steps & 15] = now;
            t1_at = now;
        }
    }
}

void vs_fire(int32_t track, int32_t step, int32_t *vel)
{
    int32_t v = vel ? *vel : 100;
    struct sq_track info;
    if (track < 0 || track > 15 || step < 0) return;
    fires++;
    if (v >= thr) accent_req = 1;
    if (names == 2 || (names == 1 && v >= thr)) name_req = track + 1;   /* 0.5 */
    if (track < 8) life_req |= 1u << track;
    if (track == 0) kick_req = 1;
    if (track < 8) {                           /* a blip at the track's place in its own loop */
        int32_t len = sq_track_info(track, &info, sizeof info) == 0 && info.len > 0 ? info.len : 16;
        struct blip *b = &blips[blip_next++ % BLIPS];
        b->track = (uint8_t)track;
        b->ang = (uint8_t)(step * 256 / len);
        b->big = v >= 100;
        b->at = sweep_abs;
        b->live = 1;
    }
}

/* ---- sine, a byte a turn ---------------------------------------------------------------------------- */
static const int8_t qsin[65] = { 0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60,
    63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113,
    115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127 };
static int32_t isin(int32_t a)
{
    a &= 255;
    if (a < 64) return qsin[a];
    if (a < 128) return qsin[128 - a];
    if (a < 192) return -qsin[a - 128];
    return -qsin[256 - a];
}
static int32_t icos(int32_t a) { return isin(a + 64); }

/* ---- the screen effects' and the screensaver's settings, saved ---------------------------------------- */
enum { CRT_OFF, CRT_SOFT, CRT_FULL };
enum { INV_OFF, INV_ACC, INV_BAR, INV_KICK };
enum { LNK_OFF, LNK_FILL, LNK_DJF, LNK_BOTH };
enum { SV_OFF, SV_STAR, SV_LOGO, SV_PIPE, SV_RAIN, SV_LAVA, SV_FERRO, SV_LIFE, SV_LISS, SV_STORM, SAVERS };
static int32_t crt = CRT_OFF, inv = INV_OFF;
static int32_t vhs, snow, border, scan, lnk;                 /* 0.4: the new effects (0 off) */
static int32_t saver = SV_OFF, sv_time = 1, sv_play;
/* 0.4: what the bouncing logo says: a word from the list, or your own (up to 8 characters) */
#define WORDS 9
static const char *const words[WORDS] = { "FLEXMODS", "DIGITAKT", "JAM ON", "LOOP", "BEATS", "HELLO", "ZZZ", "PLAY ME", "CUSTOM" };
static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?-.";
#define NCHARS ((int32_t)sizeof(chars) - 1)
static int32_t word_i, my_pos;
static uint8_t my_word[8] = { 6, 12, 1, 25, 0, 0, 0, 0 };      /* indexes into chars: "FLEX" */
static uint8_t sum4(const uint8_t *b) { uint8_t c = 0x2d; for (int32_t i = 27; i < 33; i++) c = (uint8_t)(c * 7 + b[i]); return c; }
static uint8_t sum3(const uint8_t *b) { uint8_t c = 0x71; for (int32_t i = 17; i < 26; i++) c = (uint8_t)(c * 7 + b[i]); return c; }         /* 0.4: the screensaver: which, how long, while playing */
static const uint16_t sv_ticks[5] = { 1800, 3600, 7200, 18000, 36000 };   /* 30 s .. 10 min at 60 a second */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
HEAP_BUF(block, 512)          /* borrowed from the heap while loading or saving */
static int32_t loaded, save_wait;
#define MAGIC 0x56495331u                      /* "VIS1" */
static uint8_t sum2(void) { uint8_t c = 0x3c; for (int32_t i = 8; i < 16; i++) c = (uint8_t)(c * 7 + block[i]); return c; }
static void load(void)
{
    if (!block_ready()) return;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0) return;
    if (*(uint32_t *)block != MAGIC || (uint8_t)(block[4] + block[5] + block[6] + 0x5a) != block[7]) return;
    if (block[4] <= CRT_FULL) crt = block[4];
    if (block[5] <= INV_KICK) inv = block[5];
    if (block[6] >= 64 && block[6] <= 127) thr = block[6];
    if (block[16] != sum2()) return;                        /* 0.3's saves end here: the new ones stay off */
    vhs = block[8] & 1; snow = block[9] & 1; border = block[10] & 1; scan = ui_clamp(block[11], 0, 3);
    if (block[12] <= LNK_BOTH) lnk = block[12];
    if (block[13] < SAVERS) saver = block[13];
    if (block[14] < 5) sv_time = block[14];
    sv_play = block[15] & 1;
    if (block[26] != sum3(block)) return;                   /* the logo's word: from 0.4's later saves */
    if (block[17] < WORDS) word_i = block[17];
    for (int32_t i = 0; i < 8; i++) if (block[18 + i] < NCHARS) my_word[i] = block[18 + i];
    if (block[33] != sum4(block)) return;                   /* 0.5's ART settings */
    art_mode = ui_clamp(block[27] & 7, 0, 4); rain_mode = ui_clamp(block[28], 0, 2); names = ui_clamp(block[29], 0, 2);
    invt = block[30] & 1; boot_on = block[31] & 1; tag = block[32] & 1;
}
static void save(void)
{
    if (!block_ready()) return;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    *(uint32_t *)block = MAGIC;
    block[4] = (uint8_t)crt; block[5] = (uint8_t)inv; block[6] = (uint8_t)thr;
    block[7] = (uint8_t)(block[4] + block[5] + block[6] + 0x5a);
    block[8] = (uint8_t)vhs; block[9] = (uint8_t)snow; block[10] = (uint8_t)border; block[11] = (uint8_t)scan;
    block[12] = (uint8_t)lnk; block[13] = (uint8_t)saver; block[14] = (uint8_t)sv_time; block[15] = (uint8_t)sv_play;
    block[16] = sum2();
    block[17] = (uint8_t)word_i;
    for (int32_t i = 0; i < 8; i++) block[18 + i] = my_word[i];
    block[26] = sum3(block);
    block[27] = (uint8_t)art_mode; block[28] = (uint8_t)rain_mode; block[29] = (uint8_t)names;
    block[30] = (uint8_t)invt; block[31] = (uint8_t)boot_on; block[32] = (uint8_t)tag;
    block[33] = sum4(block);
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

/* ---- the radar ----------------------------------------------------------------------------------------- */
static int32_t step_ticks_q4 = 8 * 16;         /* ticks a step of the active track, x 16 */
static uint32_t at_step_tick;
static int32_t at_step = 0, at_len = 16;
#define CX 64
#define CY 26

static void polar(void *bmp, int32_t r, int32_t a, int32_t size)
{
    int32_t x = CX + (r * isin(a)) / 127, y = CY + (r * icos(a)) / 127;
    fw_fillrect(bmp, x, y, x + size - 1, y + size - 1, 1);
}

static void rdr_draw(void *view, void *bmp)
{
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    for (int32_t a = 0; a < 256; a += 8) polar(bmp, 25, a, 1);            /* the rim */
    for (int32_t a = 0; a < 256; a += 64) { polar(bmp, 12, a, 1); polar(bmp, 19, a, 1); }   /* the cross */
    uint32_t sw = sweep_abs;
    int32_t a = (int32_t)(sw & 255);
    for (int32_t r = 2; r <= 24; r++) polar(bmp, r, a, 1);              /* the sweep */
    for (int32_t r = 4; r <= 24; r += 3) polar(bmp, r, a - 6, 1);       /* its afterglow */
    for (int32_t i = 0; i < BLIPS; i++) {
        struct blip *b = &blips[i];
        if (!b->live) continue;
        uint32_t age = sw - b->at;
        if (age > 256) { b->live = 0; continue; }
        int32_t r = 5 + b->track * 19 / 7;                            /* track 1 at 5, track 8 at 24 */
        /* a sonar contact: a bright ping as the sweep paints it, a ring spreading out from it, then it
         * glows, dims and breaks up into a flicker until it's gone */
        int32_t x = CX + (r * isin(b->ang)) / 127, y = CY + (r * icos(b->ang)) / 127, big = b->big;
        if (age < 16) {
            int32_t rr = 1 + (int32_t)age / 6;         /* the ring, growing */
            fw_framerect(bmp, x - rr, y - rr, x + rr + 1, y + rr + 1, 1);
            fw_fillrect(bmp, x - big, y - big, x + 1 + big, y + 1 + big, 1);
        } else if (age < 90) polar(bmp, r, b->ang, big ? 3 : 2);
        else if (age < 160) polar(bmp, r, b->ang, big ? 2 : 1);
        else if (age < 210) { if (((ticks >> 1) + i) & 1) polar(bmp, r, b->ang, 1); }      /* flickering */
        else if ((((ticks >> 1) + i * 3) & 3) == 0) polar(bmp, r, b->ang, 1);                /* nearly gone */
    }
    char s[8];
    s[0] = active() < 8 ? 'T' : 'M'; s[1] = (char)('1' + (active() & 7)); s[2] = 0;
    ui_text(bmp, 2, 46, s);
    ui_num(s, at_len); ui_text(bmp, 2, 2, s);
}

/* ---- the pet ------------------------------------------------------------------------------------------- */
static int32_t mood = 60, food = 60, quiet_ticks, bounce, arms, petted, energy;   /* 0-100 */
static uint32_t fires_seen;

static void pet_tick(void)
{
    uint32_t f = fires;
    int32_t n = (int32_t)(f - fires_seen);
    fires_seen = f;
    energy = (energy * 15 + n * 400) / 16;      /* how busy the music is */
    if (n) { quiet_ticks = 0; if (food < 100 && (ticks & 3) == 0) food++; }
    else quiet_ticks++;
    if ((ticks & 63) == 0) {
        if (quiet_ticks > 60) { if (food > 0) food--; if (mood > 0) mood--; }
        else if (mood < 100) mood += 2;
        if (mood > 100) mood = 100;
    }
    if (bounce > 0) bounce--;
    if (arms > 0) arms--;
    if (petted > 0) petted--;
}

/* 0.5: the pet's garden. Plants that grow as the session goes on (a sprout at power-on, grown after an hour or
 * so), swaying when the pet dances, flowering when it's happy; and how long the Digitakt has been on. */
static void pet_scene(void *bmp)
{
    static const int8_t px[6] = { 3, 11, 20, 29, 68, 73 }, kind[6] = { 0, 1, 2, 1, 0, 1 }, rate[6] = { 5, 7, 4, 6, 8, 5 }, top[6] = { 8, 26, 25, 22, 9, 14 };
    uint32_t secs = blks / 1500, mins = secs / 60;
    int32_t sway_amp = energy > 100 ? 2 : 1;
    for (int32_t i = 0; i < 6; i++) {
        int32_t h = 2 + (int32_t)(mins * (uint32_t)rate[i] / 6);
        if (h > top[i]) h = top[i];
        int32_t sway = isin((int32_t)(ticks * 2 / 3) + i * 40) * sway_amp / 127, x0 = px[i];
        if (kind[i] == 0) {                       /* a grass tuft: three blades */
            for (int32_t b = -1; b <= 1; b++) ui_line(bmp, x0 + b, 11, x0 + b * 2 + sway, 11 + h - (b ? 2 : 0));
            continue;
        }
        int32_t tx = x0 + sway, ty = 11 + h;
        ui_line(bmp, x0, 11, tx, ty);           /* the stem, leaning with the sway */
        for (int32_t y = 15; y < ty - 1; y += 4) {   /* leaves, side to side, shifting slowly as it grows */
            int32_t sx = x0 + (tx - x0) * (y - 11) / (h ? h : 1), side = ((y / 4 + i + (int32_t)(mins / 7)) & 1) ? 1 : -1;
            fw_fillrect(bmp, sx + side, y, sx + side, y, 1); fw_fillrect(bmp, sx + side * 2, y + 1, sx + side * 2, y + 1, 1);
        }
        if (kind[i] == 2 && h > 14) {             /* grown up: a tree with a round crown */
            int32_t r = h / 5;
            for (int32_t a = 0; a < 256; a += 16) { int32_t cx = tx + r * isin(a + 64) / 127, cy = ty + r * isin(a) / 127; fw_fillrect(bmp, cx, cy, cx, cy, 1); }
            for (int32_t a = 8; a < 256; a += 32) { int32_t cx = tx + r * isin(a + 64) / 254, cy = ty + r * isin(a) / 254; fw_fillrect(bmp, cx, cy, cx, cy, 1); }
        } else if (mood > 70 && h > 6) {          /* happy: a flower on top */
            fw_fillrect(bmp, tx - 1, ty, tx + 1, ty, 1); fw_fillrect(bmp, tx, ty - 1, tx, ty + 1, 1); fw_fillrect(bmp, tx, ty, tx, ty, 0);
        }
    }
    char t[20], *p = put(t, "");
    uint32_t hh = secs / 3600, mm = (secs / 60) % 60, ss = secs % 60;
    if (hh) { p = ui_num(p, (int32_t)hh); *p++ = ':'; }
    *p++ = (char)('0' + mm / 10); *p++ = (char)('0' + mm % 10); *p++ = ':'; *p++ = (char)('0' + ss / 10); *p++ = (char)('0' + ss % 10); *p = 0;
    ui_text(bmp, 2, 46, t);                       /* time on */
}
static void pet_draw(void *view, void *bmp)
{
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    pet_scene(bmp);
    int32_t asleep = quiet_ticks > 60 * 20, sad = food < 15 && !petted;
    int32_t bx = 40, by = 14 + (bounce > 3 ? 3 : bounce > 0 ? 1 : 0);   /* its bottom left */
    /* the body: a round blob 22 wide, 18 high */
    static const uint8_t rows[18] = { 7, 4, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 4, 7 };   /* inset a row */
    for (int32_t r = 0; r < 18; r++)
        fw_fillrect(bmp, bx + rows[r], by + r, bx + 21 - rows[r], by + r, 1);
    fw_fillrect(bmp, bx + 3, by - 2, bx + 6, by - 1, 1);                 /* the feet */
    fw_fillrect(bmp, bx + 15, by - 2, bx + 18, by - 1, 1);
    /* the face, cut out of the body */
    int32_t ey = by + 10;
    if (asleep) {
        fw_fillrect(bmp, bx + 5, ey, bx + 8, ey, 0); fw_fillrect(bmp, bx + 13, ey, bx + 16, ey, 0);
        int32_t z = (int32_t)(ticks / 20) % 3;                           /* Zzz, floating up */
        ui_text(bmp, bx + 24 + z * 3, by + 16 + z * 4, "Z");
        if (z > 0) ui_text(bmp, bx + 22, by + 14, "z");
    } else if (petted || arms) {                                       /* happy: ^ ^ */
        fw_fillrect(bmp, bx + 5, ey, bx + 5, ey, 0); fw_fillrect(bmp, bx + 6, ey + 1, bx + 6, ey + 1, 0);
        fw_fillrect(bmp, bx + 7, ey, bx + 7, ey, 0);
        fw_fillrect(bmp, bx + 14, ey, bx + 14, ey, 0); fw_fillrect(bmp, bx + 15, ey + 1, bx + 15, ey + 1, 0);
        fw_fillrect(bmp, bx + 16, ey, bx + 16, ey, 0);
    } else if ((ticks % 180) < 6) {                                    /* a blink */
        fw_fillrect(bmp, bx + 5, ey, bx + 7, ey, 0); fw_fillrect(bmp, bx + 14, ey, bx + 16, ey, 0);
    } else {
        fw_fillrect(bmp, bx + 5, ey - 1, bx + 7, ey + 2, 0); fw_fillrect(bmp, bx + 14, ey - 1, bx + 16, ey + 2, 0);
        fw_fillrect(bmp, bx + 6, ey + 1, bx + 6, ey + 1, 1); fw_fillrect(bmp, bx + 15, ey + 1, bx + 15, ey + 1, 1);
    }
    int32_t my = by + 5;                                               /* the mouth */
    if (asleep) fw_fillrect(bmp, bx + 10, my, bx + 11, my, 0);
    else if (sad) { fw_fillrect(bmp, bx + 8, my, bx + 13, my, 0); fw_fillrect(bmp, bx + 7, my - 1, bx + 7, my - 1, 0);
                    fw_fillrect(bmp, bx + 14, my - 1, bx + 14, my - 1, 0); }
    else { fw_fillrect(bmp, bx + 8, my, bx + 13, my, 0); fw_fillrect(bmp, bx + 7, my + 1, bx + 7, my + 1, 0);
           fw_fillrect(bmp, bx + 14, my + 1, bx + 14, my + 1, 0);
           if (energy > 300) fw_fillrect(bmp, bx + 9, my - 2, bx + 12, my - 1, 0); }   /* singing */
    /* the arms: up on a loud hit, out while it dances, down at rest */
    int32_t up = arms > 0, out = !asleep && energy > 100;
    for (int32_t i = 0; i < 4; i++) {
        int32_t dy = up ? i + 1 : out ? ((ticks / 8) & 1 ? i / 2 : -i / 2) : -i;
        fw_fillrect(bmp, bx - 1 - i, by + 8 + dy, bx - 1 - i, by + 8 + dy, 1);
        fw_fillrect(bmp, bx + 22 + i, by + 8 + dy, bx + 22 + i, by + 8 + dy, 1);
    }
    if (petted) {                                                      /* a heart */
        static const uint8_t heart[5] = { 0x04, 0x0e, 0x1f, 0x1b, 0x00 };
        for (int32_t r = 0; r < 5; r++)
            for (int32_t c = 0; c < 5; c++)
                if (heart[r] >> c & 1) fw_fillrect(bmp, bx + 26 + c, by + 22 - r + petted / 20, bx + 26 + c, by + 22 - r + petted / 20, 1);
    }
    fw_fillrect(bmp, 0, 11, 127, 11, 0);
    for (int32_t x = 0; x < 128; x += 2) ui_dot(bmp, x, 10);           /* the floor */
    /* the meters */
    ui_text(bmp, 80, 44, "BIT");
    ui_text(bmp, 80, 34, "MOOD");
    fw_framerect(bmp, 80, 28, 122, 31, 1); fw_fillrect(bmp, 81, 29, 81 + mood * 40 / 100, 30, 1);
    ui_text(bmp, 80, 20, "FOOD");
    fw_framerect(bmp, 80, 14, 122, 17, 1); fw_fillrect(bmp, 81, 15, 81 + food * 40 / 100, 16, 1);
    ui_text(bmp, 2, 2, asleep ? "SLEEPING" : sad ? "HUNGRY: PLAY SOME MUSIC" : energy > 300 ? "DANCING" : "");
}

/* the pet: any knob pets it */
static int32_t pet_enc(void *view, void *event)
{
    (void)event;
    petted = 90;
    if (mood < 100) mood++;
    fw_invalidate(view);
    return 1;
}
static void pet_clear(void *view) { mood = food = 60; fw_invalidate(view); }

/* ---- SCR: the screen effects' page: eight knobs, the last one turned explained -------------------------- */
static const char *const crt_n[3] = { "OFF", "SFT", "FUL" };
static const char *const inv_n[4] = { "OFF", "ACC", "BAR", "KCK" };
static const char *const lnk_n[4] = { "OFF", "FIL", "DJF", "BTH" };
static const char *const scr_name[8] = { "CRT", "INV", "THR", "VHS", "SNW", "BRD", "SCN", "LNK" };
static const char *const scr_help[8] = {
    "CRT: ROLL LINE / +POWER+GLITCH", "FLASH: LOUD / BEAT 1 / TRACK 1", "FLASH: HOW LOUD IS LOUD",
    "VHS: WORN TAPE TRACKING NOISE", "SNOW: TV STATIC WHEN STOPPED", "BORDER: A FRAME ON THE BEATS",
    "SCANLINES: 1 LIGHT ... 3 HEAVY", "LINK: GLITCH ON FILL / DJ FLT" };
static int32_t scr_turned[8], scr_hl = -1, scr_hl_ticks;
static int32_t *scr_val(int32_t k)
{
    static int32_t *const v[8] = { &crt, &inv, &thr, &vhs, &snow, &border, &scan, &lnk };
    return v[k];
}
static const int8_t scr_max[8] = { 2, 3, 127, 1, 1, 1, 3, 3 };
static void scr_draw(void *view, void *bmp)
{
    char s[8];
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    ui_text(bmp, 2, 46, "SCREEN EFFECTS");
    ui_text(bmp, 2, 36, scr_hl >= 0 ? scr_help[scr_hl] : "TURN A KNOB");
    for (int32_t k = 0; k < 8; k++) {
        int32_t v = *scr_val(k);
        static const char *const scn_n[4] = { "OFF", "1", "2", "3" };
        const char *t = k == 0 ? crt_n[v] : k == 1 ? inv_n[v] : k == 7 ? lnk_n[v] : k == 6 ? scn_n[v] : k == 2 ? 0 : v ? "ON" : "OFF";
        if (!t) { ui_num(s, v); t = s; }
        ui_knob(bmp, k, scr_name[k], t, k == 2 ? v - 64 : v, k == 2 ? 63 : scr_max[k], k == 2 ? 0 : 2, scr_hl == k && scr_hl_ticks > 0);
    }
}
static int32_t scr_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > 8) return 0;
    int32_t k = knob - 1, per = k == 2 ? 4 : 16;
    scr_hl = k; scr_hl_ticks = 40;
    if ((scr_turned[k] > 0) != (turn > 0)) scr_turned[k] = 0;
    scr_turned[k] += turn;
    while (scr_turned[k] >= per || scr_turned[k] <= -per) {
        int32_t d = scr_turned[k] > 0 ? 1 : -1;
        scr_turned[k] -= d * per;
        *scr_val(k) = ui_clamp(*scr_val(k) + d, k == 2 ? 64 : 0, scr_max[k]);
    }
    save_wait = 60;
    fw_invalidate(view);
    return 1;
}
static void scr_clear(void *view)
{
    crt = CRT_OFF; inv = INV_OFF; thr = 110; vhs = snow = border = scan = 0; lnk = LNK_OFF;
    save_wait = 60; fw_invalidate(view);
}

/* ==== 0.4 ==================================================================================================== */

/* ---- the master audio (ev_render_master, interrupt level): the tempo's clock, the scope, the spectrum -------- */
static volatile int8_t scope_on, spec_on;      /* the SCP page wants them */
static volatile int32_t lvl_pk;
#define SCOPE_N 128
static int8_t sc_l[SCOPE_N], sc_r[SCOPE_N];
static volatile uint32_t sc_at;
static int32_t sp_lp[7];
static volatile int32_t sp_pk[8];              /* the bands' peaks, the UI task lets them fall */
static const uint16_t sp_k[7] = { 256, 636, 1671, 4028, 9140, 17836, 25945 };   /* 60 Hz .. 12 kHz one-poles */

void vs_audio(int32_t *pair)
{
    blks++;
    {                                             /* 0.5: the peak, every 4th frame */
        int32_t pk = lvl_pk;
        for (int32_t i = 0; i < 64; i += 8) { int32_t v = pair[i] >> 16; if (v < 0) v = -v; if (v > pk) pk = v; }
        lvl_pk = pk;
    }
    if (scope_on)
        for (int32_t i = 0; i < 32; i += 8) {        /* every 8th frame: 128 frames, 21 ms */
            uint32_t j = sc_at++ % SCOPE_N;
            sc_l[j] = (int8_t)(pair[2 * i] >> 24);
            sc_r[j] = (int8_t)(pair[2 * i + 1] >> 24);
        }
    if (spec_on) {
        int32_t pk[8];
        for (int32_t b = 0; b < 8; b++) pk[b] = 0;
        for (int32_t i = 0; i < 32; i++) {
            int32_t m = ((pair[2 * i] >> 1) + (pair[2 * i + 1] >> 1)) >> 16, prev = 0;
            for (int32_t b = 0; b < 7; b++) {
                sp_lp[b] += ((m - sp_lp[b]) * sp_k[b]) >> 15;
                int32_t band = sp_lp[b] - prev;
                prev = sp_lp[b];
                if (band < 0) band = -band;
                if (band > pk[b]) pk[b] = band;
            }
            int32_t top = m - prev;
            if (top < 0) top = -top;
            if (top > pk[7]) pk[7] = top;
        }
        for (int32_t b = 0; b < 8; b++) if (pk[b] > sp_pk[b]) sp_pk[b] = pk[b];
    }
}

/* ---- every key and knob: the screensaver's timer, its waking, and YES + NO for the FILL glitch ------------- */
static int32_t idle, sv_on, yes_held, no_held;
static int32_t wake(void)
{
    idle = 0;
    if (!sv_on) return 0;
    sv_on = 0;
    return 1;                                     /* the waking press does nothing else */
}
int32_t vs_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    int32_t down = (fl & F_DOWN) && !(fl & F_RELEASE);
    (void)brain;
    if (down && boot_t >= 0 && boot_t < BOOT_LEN) { boot_t = BOOT_LEN; return 1; }   /* 0.5: a key skips the splash */
    if (key == K_YES) yes_held = down;
    if (key == K_NO) no_held = down;
    return down ? wake() : 0;
}
int32_t vs_enc(void *brain, void *ev) { (void)brain; (void)ev; return wake(); }

/* ---- the screensavers, drawn in a box (the whole screen, or SAV's preview) ---------------------------------- */
static uint8_t star_x[32], star_y[32], star_z[32];
static int32_t logo_x = 20 << 4, logo_y = 20 << 4, logo_dx = 11, logo_dy = 7, logo_flash, logo_hits;
#define PIPE_N 96
static uint8_t pipe_x[PIPE_N], pipe_y[PIPE_N];
static int32_t pipe_n, pipe_dir, pipe_hold;
static uint8_t rain_y[16], rain_v[16];
static int32_t sv_ready;
static void sv_reset(void)
{
    for (int32_t i = 0; i < 32; i++) { star_x[i] = (uint8_t)rnd(256); star_y[i] = (uint8_t)rnd(256); star_z[i] = (uint8_t)(1 + rnd(255)); }
    for (int32_t i = 0; i < 16; i++) { rain_y[i] = (uint8_t)rnd(64); rain_v[i] = (uint8_t)(2 + rnd(4)); }
    pipe_n = 0;
    logo_x = 40 << 4; logo_y = 20 << 4;           /* the logo starts mid-screen (the preview pulls it back in) */
    sv_ready = 1;
}
/* the logo's word, trailing spaces dropped; an all-blank custom word shows as a dash */
static void logo_text(char *t)
{
    int32_t n = 0;
    if (word_i < WORDS - 1) { for (const char *p = words[word_i]; *p; p++) t[n++] = *p; }
    else { for (int32_t i = 0; i < 8; i++) t[n++] = chars[my_word[i]]; while (n > 0 && t[n - 1] == ' ') n--; }
    if (n == 0) t[n++] = '-';
    t[n] = 0;
}
static int32_t bpm10(void) { uint32_t q = step_blk_q8; return q ? (int32_t)(57600000u / q) : 1200; }
static int32_t playing_now(void);
/* x0..x0+w-1, y0..y0+h-1 (y up); one frame on */
static void art_frame(void *bmp, int32_t mode, int32_t x0, int32_t y0, int32_t w, int32_t h);
static void sv_frame(void *bmp, int32_t mode, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    if (mode >= SV_LAVA) { art_frame(bmp, mode, x0, y0, w, h); return; }
    int32_t cx = x0 + w / 2, cy = y0 + h / 2;
    if (!sv_ready) sv_reset();
    if (mode == SV_STAR) {                        /* flying faster at higher tempos */
        int32_t sp = playing_now() ? 2 + bpm10() / 400 : 2;
        for (int32_t i = 0; i < 32; i++) {
            int32_t z = star_z[i] - sp;
            if (z < 8) { z = 255; star_x[i] = (uint8_t)rnd(256); star_y[i] = (uint8_t)rnd(256); }
            star_z[i] = (uint8_t)z;
            int32_t px = cx + ((star_x[i] - 128) * w / 4) / z, py = cy + ((star_y[i] - 128) * h / 4) / z;
            if (px > x0 && px < x0 + w - 2 && py > y0 && py < y0 + h - 2)
                fw_fillrect(bmp, px, py, px + (z < 70), py + (z < 70), 1);
        }
    } else if (mode == SV_LOGO) {                 /* a word, bouncing; a corner hit is a little party */
        char txt[9];
        logo_text(txt);
        int32_t n = 0;
        while (txt[n]) n++;
        int32_t bw = n * 4 + 5, bh = 9, hit = 0;
        if (logo_x > (w - bw) << 4) logo_x = (w - bw) << 4;      /* moved from the full screen to the preview */
        if (logo_y > (h - bh) << 4) logo_y = (h - bh) << 4;
        logo_x += logo_dx; logo_y += logo_dy;
        if (logo_x < 0 || logo_x > (w - bw) << 4) { logo_dx = -logo_dx; logo_x += 2 * logo_dx; hit++; }
        if (logo_y < 0 || logo_y > (h - bh) << 4) { logo_dy = -logo_dy; logo_y += 2 * logo_dy; hit++; }
        if (hit == 2) { logo_flash = 40; logo_hits++; }
        int32_t lx = x0 + (logo_x >> 4), ly = y0 + (logo_y >> 4);
        fw_framerect(bmp, lx, ly, lx + bw, ly + bh, 1);
        ui_text(bmp, lx + 3, ly + 2, txt);
        if (logo_flash > 0) {
            logo_flash--;
            fw_fillrect(bmp, lx, ly, lx + bw, ly + bh, -1);
            for (int32_t i = 0; i < 12; i++) { int32_t px = x0 + (int32_t)rnd(w), py = y0 + (int32_t)rnd(h); fw_fillrect(bmp, px, py, px, py, 1); }
        }
    } else if (mode == SV_PIPE) {                 /* a pipe that wanders and turns, then starts over */
        if (pipe_n == 0) { pipe_x[0] = (uint8_t)(w / 2); pipe_y[0] = (uint8_t)(h / 2); pipe_n = 1; pipe_dir = 0; }
        if ((ticks & 1) == 0) {
            if (pipe_n >= PIPE_N) { if (++pipe_hold > 60) { pipe_hold = 0; pipe_n = 0; } }
            else {
                if (rnd(5) == 0) pipe_dir = (pipe_dir + (rnd(2) ? 1 : 3)) & 3;
                int32_t x = pipe_x[pipe_n - 1] + (pipe_dir == 0 ? 3 : pipe_dir == 2 ? -3 : 0);
                int32_t y = pipe_y[pipe_n - 1] + (pipe_dir == 1 ? 3 : pipe_dir == 3 ? -3 : 0);
                if (x < 1 || x > w - 3 || y < 1 || y > h - 3) pipe_dir = (pipe_dir + 2) & 3;
                else { pipe_x[pipe_n] = (uint8_t)x; pipe_y[pipe_n] = (uint8_t)y; pipe_n++; }
            }
        }
        for (int32_t i = 1; i < pipe_n; i++) {
            int32_t ax = x0 + pipe_x[i - 1], ay = y0 + pipe_y[i - 1], bx = x0 + pipe_x[i], by = y0 + pipe_y[i];
            fw_fillrect(bmp, ax < bx ? ax : bx, ay < by ? ay : by, ax < bx ? bx + 1 : ax + 1, ay < by ? by + 1 : ay + 1, 1);
        }
    } else if (mode == SV_RAIN) {                 /* falling characters */
        static const char glyphs[] = "0123456789ABCDEF";
        int32_t cols = w / 8;
        for (int32_t c = 0; c < cols && c < 16; c++) {
            if ((ticks & 1) == 0) { int32_t y = (int8_t)rain_y[c] - rain_v[c] / 2; if (y < -24) { y = h; rain_v[c] = (uint8_t)(2 + rnd(4)); } rain_y[c] = (uint8_t)(y & 255); }
            int32_t head = (int8_t)rain_y[c];
            for (int32_t k = 0; k < 4; k++) {
                int32_t y = head + k * 6;
                if (y < 0 || y > h - 6) continue;
                char g[2] = { glyphs[(c * 7 + k * 3 + (int32_t)(ticks >> 3)) & 15], 0 };
                ui_text(bmp, x0 + c * 8 + 1, y0 + y, g);
            }
        }
    }
}

/* ---- SAV: the screensaver's page, with a preview above the knobs -------------------------------------------- */
static const char *const sv_name[SAVERS] = { "OFF", "STR", "LGO", "PIP", "RAN", "LAV", "FER", "LIF", "LIS", "STM" };
static const char *const sv_long[SAVERS] = { "NO SCREENSAVER", "STARFIELD", "BOUNCING LOGO", "PIPES", "FALLING RAIN", "LAVA LAMP", "FERROFLUID", "GAME OF LIFE", "LISSAJOUS", "STORM" };
static const char *const tm_name[5] = { "30S", "1M", "2M", "5M", "10M" };
static int32_t sav_turned[6], sav_hl = -1, sav_hl_ticks;
static void sav_draw(void *view, void *bmp)
{
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    ui_text(bmp, 2, 46, sv_long[saver]);
    if (saver != SV_OFF && !sv_on) {               /* not while the real one runs: they'd share the logo */
        fw_framerect(bmp, 60, 27, 127, 52, 1);
        sv_frame(bmp, saver, 61, 28, 66, 24);
    }
    ui_knob(bmp, 0, "MOD", sv_name[saver], saver, SAVERS - 1, 2, sav_hl == 0 && sav_hl_ticks > 0);
    ui_knob(bmp, 1, "TIM", tm_name[sv_time], sv_time, 4, 2, sav_hl == 1 && sav_hl_ticks > 0);
    ui_knob(bmp, 2, "PLY", sv_play ? "ON" : "OFF", sv_play, 1, 2, sav_hl == 2 && sav_hl_ticks > 0);
    static const char *const wshort[WORDS] = { "FLX", "DGT", "JAM", "LOP", "BTS", "HI", "ZZZ", "PLY", "OWN" };
    ui_knob(bmp, 3, "TXT", wshort[word_i], word_i, WORDS - 1, 2, sav_hl == 3 && sav_hl_ticks > 0);
    if (word_i == WORDS - 1) {                    /* your own word: POS picks the letter, CHR changes it */
        char c[2] = { chars[my_word[my_pos]], 0 }, p[2] = { (char)('1' + my_pos), 0 };
        ui_knob(bmp, 4, "POS", p, my_pos, 7, 2, sav_hl == 4 && sav_hl_ticks > 0);
        ui_knob(bmp, 5, "CHR", c[0] == ' ' ? "SPC" : c, my_word[my_pos], NCHARS - 1, 2, sav_hl == 5 && sav_hl_ticks > 0);
        for (int32_t i = 0; i < 8; i++) {
            char g[2] = { chars[my_word[i]], 0 };
            ui_text(bmp, 2 + i * 6, 36, g);
            if (i == my_pos) fw_fillrect(bmp, 1 + i * 6, 34, 5 + i * 6, 34, 1);
        }
    }
}
static int32_t sav_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > (word_i == WORDS - 1 ? 6 : 4)) return 0;
    int32_t k = knob - 1;
    sav_hl = k; sav_hl_ticks = 40;
    if ((sav_turned[k] > 0) != (turn > 0)) sav_turned[k] = 0;
    sav_turned[k] += turn;
    while (sav_turned[k] >= 16 || sav_turned[k] <= -16) {
        int32_t d = sav_turned[k] > 0 ? 1 : -1;
        sav_turned[k] -= d * 16;
        if (k == 0) { saver = ui_clamp(saver + d, 0, SAVERS - 1); sv_ready = 0; }
        else if (k == 1) sv_time = ui_clamp(sv_time + d, 0, 4);
        else if (k == 2) sv_play = ui_clamp(sv_play + d, 0, 1);
        else if (k == 3) { word_i = ui_clamp(word_i + d, 0, WORDS - 1); logo_x = logo_y = 0; }
        else if (k == 4) my_pos = ui_clamp(my_pos + d, 0, 7);
        else my_word[my_pos] = (uint8_t)((my_word[my_pos] + d + NCHARS) % NCHARS);   /* wraps around */
    }
    save_wait = 60;
    fw_invalidate(view);
    return 1;
}
static void sav_clear(void *view) { saver = SV_OFF; sv_time = 1; sv_play = 0; word_i = 0; save_wait = 60; fw_invalidate(view); }

#include "art.h"                                /* 0.5: the art, the overlays, the ART page */

/* ---- CLK: the tempo and the bar, big ------------------------------------------------------------------------- */
static const uint8_t seg7[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
static void big_digit(void *bmp, int32_t x, int32_t y, int32_t d)   /* 12 x 21, y the bottom */
{
    uint8_t m = d < 0 ? 0x40 : seg7[d % 10];
    if (m & 0x01) fw_fillrect(bmp, x + 2, y + 18, x + 9, y + 20, 1);    /* a: the top */
    if (m & 0x02) fw_fillrect(bmp, x + 9, y + 11, x + 11, y + 18, 1);   /* b */
    if (m & 0x04) fw_fillrect(bmp, x + 9, y + 2, x + 11, y + 9, 1);     /* c */
    if (m & 0x08) fw_fillrect(bmp, x + 2, y, x + 9, y + 2, 1);          /* d: the bottom */
    if (m & 0x10) fw_fillrect(bmp, x, y + 2, x + 2, y + 9, 1);          /* e */
    if (m & 0x20) fw_fillrect(bmp, x, y + 11, x + 2, y + 18, 1);        /* f */
    if (m & 0x40) fw_fillrect(bmp, x + 2, y + 9, x + 9, y + 11, 1);     /* g: the middle */
}
/* the tap meter: tap YES in time, timed in audio blocks (1500 a second); NO clears it. It only measures:
 * the Digitakt's own tempo doesn't change (FUNC + TEMPO is the built-in tap) */
static uint32_t tap_t[8];
static int32_t tap_n;
static int32_t tap10(void)
{
    int32_t n = tap_n < 8 ? tap_n : 8;
    if (n < 2) return 0;
    uint32_t span = tap_t[(tap_n - 1) & 7] - tap_t[(tap_n - n) & 7];
    return span ? (int32_t)(900000u * (uint32_t)(n - 1) / span) : 0;    /* BPM x 10 */
}
static int32_t clk_key(void *view, void *event)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    if (key != K_YES && key != K_NO) return mp_key(view, event);
    if ((fl & F_DOWN) && !(fl & (F_RELEASE | F_REPEAT))) {
        uint32_t now = blks;
        if (key == K_NO) tap_n = 0;
        else {
            if (tap_n > 0 && now - tap_t[(tap_n - 1) & 7] > 3000) tap_n = 0;   /* a 2 s pause starts over */
            tap_t[tap_n & 7] = now;
            tap_n++;
        }
        fw_invalidate(view);
    }
    return 1;
}
static void clk_draw(void *view, void *bmp)
{
    char s[24], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    static int32_t shown = 1200;
    int32_t b = bpm10(), play = playing_now(), x = 4;
    if (b - shown >= 2 || shown - b >= 2) shown = b;             /* steady: only a real change moves it */
    b = shown;
    if (b > 3000) b = 3000;
    if (b >= 1000) { big_digit(bmp, x, 26, b / 1000); }
    x += 15;
    big_digit(bmp, x, 26, (b / 100) % 10); x += 15;
    big_digit(bmp, x, 26, (b / 10) % 10); x += 15;
    fw_fillrect(bmp, x, 26, x + 2, 28, 1); x += 6;               /* the point */
    big_digit(bmp, x, 26, b % 10);
    ui_text(bmp, 92, 44, "BPM");
    ui_text(bmp, 92, 35, play ? "PLAY" : "STOP");
    int32_t pt = CUR_PAT(UI_STATE()), at = active();             /* the pattern, as KIT A/B reads it */
    p = put(s, "PTN ");
    if (pt >= 0 && pt < 128) { *p++ = (char)('A' + pt / 16); *p++ = (char)('0' + (pt % 16 + 1) / 10); *p++ = (char)('0' + (pt % 16 + 1) % 10); *p = 0; }
    else put(p, "-");
    ui_text(bmp, 92, 26, s);
    p = put(s, at < 8 ? "AUD " : "MIDI "); *p++ = (char)('1' + (at & 7)); *p = 0;
    ui_text(bmp, 92, 17, s);
    uint32_t n = t1_steps;
    p = put(s, "BAR "); p = ui_num(p, (int32_t)(n / 16 + 1));
    p = put(p, "  BEAT "); p = ui_num(p, (int32_t)((n % 16) / 4 + 1)); *p++ = '.'; ui_num(p, (int32_t)(n % 4 + 1));
    ui_text(bmp, 4, 12, s);
    int32_t tb = tap10();                                         /* the tap meter's line */
    if (tap_n == 0) ui_text(bmp, 4, 19, "YES: TAP");
    else {
        p = put(s, "TAP ");
        if (tb) { p = ui_num(p, tb / 10); *p++ = '.'; p = ui_num(p, tb % 10); } else p = put(p, "...");
        p = put(p, "  X"); ui_num(p, tap_n);
        ui_text(bmp, 4, 19, s);
    }
    for (int32_t i = 0; i < 4; i++) {                             /* the beats of the bar */
        int32_t on = play && (int32_t)((n % 16) / 4) == i, xx = 4 + i * 30;
        if (on) fw_fillrect(bmp, xx, 2, xx + 24, 8, 1); else fw_framerect(bmp, xx, 2, xx + 24, 8, 1);
    }
}

/* ---- SCP: the scope, XY and the spectrum ---------------------------------------------------------------------- */
enum { SC_SCOPE, SC_XY, SC_SPEC };
static int32_t sc_mode, sc_turned, sp_show[8], sp_hold[8];
static void scp_draw(void *view, void *bmp)
{
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    static const char *const mname[3] = { "SCOPE", "XY", "SPECTRUM" };
    ui_text(bmp, 2, 46, mname[sc_mode]);
    ui_text(bmp, 92, 46, "A: MODE");
    uint32_t at = sc_at;
    if (sc_mode == SC_SCOPE) {
        int32_t prev = 22;
        for (int32_t i = 0; i < SCOPE_N; i++) {
            uint32_t j = (at + i) % SCOPE_N;
            int32_t y = 22 + ((sc_l[j] + sc_r[j]) * 20) / 256;
            if (y < 1) y = 1;
            if (y > 43) y = 43;
            fw_fillrect(bmp, i, prev < y ? prev : y, i, prev < y ? y : prev, 1);
            prev = y;
        }
    } else if (sc_mode == SC_XY) {
        for (int32_t i = 0; i < SCOPE_N; i++) {
            uint32_t j = (at + i) % SCOPE_N;
            int32_t x = 64 + ((sc_l[j] - sc_r[j]) * 20) / 128, y = 22 + ((sc_l[j] + sc_r[j]) * 20) / 256;
            if (x > 0 && x < 127 && y > 0 && y < 44) fw_fillrect(bmp, x, y, x, y, 1);
        }
        fw_fillrect(bmp, 64, 21, 64, 23, -1); fw_fillrect(bmp, 63, 22, 65, 22, -1);
    } else {
        static const char *const bn[8] = { "60", "150", "400", "1K", "2K5", "6K", "12K", "HI" };
        for (int32_t b = 0; b < 8; b++) {
            int32_t x = 2 + b * 16, h = sp_show[b];
            if (h > 0) fw_fillrect(bmp, x, 8, x + 12, 8 + h, 1);
            fw_fillrect(bmp, x, 8 + sp_hold[b], x + 12, 8 + sp_hold[b], 1);
            ui_text(bmp, x, 1, bn[b]);
        }
    }
}
static int32_t scp_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob != 1) return 0;
    if ((sc_turned > 0) != (turn > 0)) sc_turned = 0;
    sc_turned += turn;
    while (sc_turned >= 16 || sc_turned <= -16) { int32_t d = sc_turned > 0 ? 1 : -1; sc_turned -= d * 16; sc_mode = ui_clamp(sc_mode + d, 0, 2); }
    fw_invalidate(view);
    return 1;
}
/* the spectrum's bars: a level in dB-ish steps, falling slowly; the peaks held a moment */
static void spec_tick(void)
{
    for (int32_t b = 0; b < 8; b++) {
        int32_t v = sp_pk[b], h = 0;
        sp_pk[b] = 0;
        while (v > 250 && h < 34) { v = v * 7 / 8; h++; }         /* about 1.2 dB a pixel, 40 dB in all */
        if (h > sp_show[b]) sp_show[b] = h; else if (sp_show[b] > 0) sp_show[b]--;
        if (sp_show[b] >= sp_hold[b]) sp_hold[b] = sp_show[b]; else if ((ticks & 3) == 0 && sp_hold[b] > 0) sp_hold[b]--;
    }
}

struct core_mpage vs_rdr = { 99, "RDR", "Radar", rdr_draw, mp_key, 0, 0, 0, 0 };
struct core_mpage vs_pet = { 99, "PET", "Pet", pet_draw, mp_key, pet_enc, 0, 0, pet_clear };
struct core_mpage vs_scr = { 99, "SCR", "Screen FX", scr_draw, mp_key, scr_enc, 0, 0, scr_clear };
struct core_mpage vs_sav = { 99, "SAV", "Screensaver", sav_draw, mp_key, sav_enc, 0, 0, sav_clear };
struct core_mpage vs_clk = { 99, "CLK", "Clock", clk_draw, clk_key, 0, 0, 0, 0 };
struct core_mpage vs_scp = { 99, "SCP", "Scope", scp_draw, mp_key, scp_enc, 0, 0, 0 };
struct core_mpage vs_art = { 99, "ART", "Art and overlays", art_draw, mp_key, art_enc, 0, 0, art_clear };

static int32_t playing_now(void) { return ticks - last_step_tick < 30; }

/* ---- the effects over every frame -------------------------------------------------------------------- */
static int32_t flash, was_playing, power_anim, power_dir, glitch, glitch_y;   /* power_dir 1 on, -1 off */
#define POWER_FRAMES 14

/* 0.4: the DJ filter (djfilter 0.6+, if it's in the build): how much it cuts at each end, 0-127 */
extern volatile int8_t djfilter_kv[] __attribute__((weak));
/* FILL 0.3+ (if it's in the build): [0] playing, [1] its amount; without it, YES + NO held */
extern volatile int32_t fill_kv[] __attribute__((weak));
static int32_t fill_active(void) { return fill_kv ? fill_kv[0] != 0 : yes_held && no_held; }
static void dj_cut(int32_t *lo, int32_t *hi)
{
    *lo = *hi = 0;
    if (!djfilter_kv) return;
    int32_t f = djfilter_kv[0], type = djfilter_kv[2], mix = djfilter_kv[7];
    if (type <= 1) { if (f < 64) *hi = (64 - f) * 2; else *lo = (f - 64) * 2; }
    else if (type <= 3) *hi = 127 - f;
    else if (type <= 5) *lo = f;
    else if (type == 6) { *lo = f / 2; *hi = (127 - f) / 2; }
    *lo = *lo * mix / 127; *hi = *hi * mix / 127;
}
static int32_t border_w, stopped_ticks;

void vs_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (sv_on) {                                         /* 0.4: the screensaver, over everything */
        fw_fillrect(bmp, 0, 0, 127, 63, 0);
        sv_frame(bmp, saver, 0, 0, 128, 64);
        overlays(bmp);
        return;
    }
    if (scan) {                                          /* scanlines: every 4th, 3rd or 2nd line off */
        int32_t gap = scan == 1 ? 8 : scan == 2 ? 5 : 3;  /* 1 light enough to jam with, 3 heavy */
        int32_t roll = (int32_t)(ticks / 6) % gap;      /* drifting down a line at a time: each line of the text is
                                                           off only now and then, so the eye still reads it */
        for (int32_t y = roll; y < 64; y += gap) fw_fillrect(bmp, 0, 63 - y, 127, 63 - y, 0);
    }
    if (lnk >= LNK_DJF) {                                /* filter vision */
        int32_t lo, hi;
        dj_cut(&lo, &hi);
        if (hi > 8) { int32_t n = 9 - hi * 7 / 127; for (int32_t y = 0; y < 64; y += n) fw_fillrect(bmp, 0, y, 127, y, 0); }
        if (lo > 8) { int32_t n = 9 - lo * 7 / 127; for (int32_t x = 0; x < 128; x += n) fw_fillrect(bmp, x, 0, x, 63, 0); }
    }
    if (vhs) {                                           /* a worn tape: a band of tracking noise drifting down */
        int32_t by = 63 - (int32_t)((ticks / 3) % 90);
        for (int32_t i = 0; i < 26; i++) {
            int32_t y = by + (int32_t)rnd(6), x = (int32_t)rnd(128);
            if (y >= 0 && y < 64) fw_fillrect(bmp, x, y, x + (int32_t)rnd(4), y, -1);
        }
        if ((ticks % 240) < 3) { int32_t y = (int32_t)((ticks / 240 * 37) % 60); fw_fillrect(bmp, 0, y, 127, y, -1); }
    }
    if (snow && stopped_ticks > 0) {                     /* static when stopped, thickening for 2 s */
        int32_t n = stopped_ticks > 120 ? 160 : stopped_ticks * 4 / 3;
        for (int32_t i = 0; i < n; i++) { int32_t x = (int32_t)rnd(128), y = (int32_t)rnd(64); fw_fillrect(bmp, x, y, x, y, -1); }
    }
    if ((lnk == LNK_FILL || lnk == LNK_BOTH) && fill_active()) {   /* glitch while FILL plays, as wild as its amount */
        int32_t amt = fill_kv ? fill_kv[1] : 50, n = 1 + amt * 7 / 100, wide = 6 + amt * 50 / 100;
        for (int32_t i = 0; i < n; i++) {
            int32_t y = (int32_t)rnd(62), x = (int32_t)rnd(100);
            fw_fillrect(bmp, x, y, x + 4 + (int32_t)rnd((uint32_t)wide), y + (int32_t)rnd(amt > 60 ? 3 : 2), -1);
        }
    }
    if (border_w > 0) {                                  /* the beat border */
        int32_t w = border_w > 4 ? 3 : border_w > 2 ? 2 : 1;
        fw_fillrect(bmp, 0, 0, 127, w - 1, -1); fw_fillrect(bmp, 0, 64 - w, 127, 63, -1);
        fw_fillrect(bmp, 0, w, w - 1, 63 - w, -1); fw_fillrect(bmp, 128 - w, w, 127, 63 - w, -1);
    }
    if (power_anim > 0) {                                /* the old set switching off or on */
        int32_t f = power_dir < 0 ? POWER_FRAMES - power_anim : power_anim;   /* 0 full ... 14 gone */
        if (f < 8) {                                     /* the picture squeezes to a line */
            int32_t h = 32 - f * 4;
            if (h < 1) h = 1;
            fw_fillrect(bmp, 0, 0, 127, 31 - h, 0);
            fw_fillrect(bmp, 0, 32 + h, 127, 63, 0);
            if (h <= 4) fw_fillrect(bmp, 0, 31, 127, 32, 1);
        } else {                                         /* the line shrinks to a dot */
            int32_t w = 64 - (f - 8) * 11;
            fw_fillrect(bmp, 0, 0, 127, 63, 0);
            if (w > 0) fw_fillrect(bmp, 64 - w, 31, 63 + w, 32, 1);
            else fw_fillrect(bmp, 63, 31, 64, 32, 1);
        }
    }
    if (crt != CRT_OFF) {                                /* the refresh line, rolling down */
        int32_t y = 63 - (int32_t)((ticks / 2) % 80);
        if (y >= 0) fw_fillrect(bmp, 0, y, 127, y, -1);
    }
    if (glitch > 0) {
        fw_fillrect(bmp, 0, glitch_y, 127, glitch_y + 1, -1);
        for (int32_t i = 0; i < 6; i++) { int32_t x = (int32_t)rnd(128), y = (int32_t)rnd(64); fw_fillrect(bmp, x, y, x + 2, y, -1); }
    }
    if (flash > 0)
        fw_fillrect(bmp, 0, 0, 127, 63, -1);
    overlays(bmp);                                      /* 0.5 */
}

/* ---- the tick: loading, the pets, the games, the radar's sweep, the effects' timing ------------------ */
void vs_tick(void *ctrl)
{
    static uint32_t draws, drawn_at;
    static int32_t redraw_next;
    uint32_t now = ++ticks;
    int32_t redraw = redraw_next;
    redraw_next = 0;
    if (!loaded && now > 40) { loaded = 1; load(); }
    if (save_wait > 0 && --save_wait == 0) save();
    if (scr_hl_ticks > 0 && --scr_hl_ticks == 0) redraw = 1;

    /* the active track's step, and how long one lasts */
    int32_t at = active(), sr = step_req[at], step_now = 0;
    if (sr) {
        step_req[at] = 0;
        uint32_t d = now - at_step_tick;
        if (d >= 2 && d <= 120) step_ticks_q4 += ((int32_t)(d << 4) - step_ticks_q4) >> 2;
        at_step_tick = now;
        at_step = sr - 1;
        step_now = 1;
        struct sq_track info;
        at_len = sq_track_info(at, &info, sizeof info) == 0 && info.len > 0 ? info.len : 16;
    }
    for (int32_t t = 0; t < 16; t++) if (t != at) step_req[t] = 0;
    int32_t playing = now - last_step_tick < 30;
    if (playing) {                                       /* the sweep: where in its loop, between steps */
        int32_t frac = (int32_t)((now - at_step_tick) << 8) / (step_ticks_q4 > 16 ? step_ticks_q4 >> 4 : 1);
        if (frac > 255) frac = 255;
        int32_t L = at_len > 0 ? at_len : 16;          /* one loop of the track makes one turn */
        uint32_t a = (uint32_t)((at_step * 256 + frac) / L) & 255;
        uint32_t cur = sweep_abs & 255, d = (a - cur) & 255;
        if (d < 128) sweep_abs += d;                     /* only forward: a whole turn counted on a wrap */
    } else
        sweep_abs += 1;                                  /* stopped: a slow idle sweep */

    pet_tick();
    if (sr && energy > 0) bounce = 6;

    /* the flashes */
    if (accent_req) { accent_req = 0; if (inv == INV_ACC) flash = 3; arms = 30; if (rain_mode == 2 || storm_shown) bolt_strike(); }
    if (kick_req) { kick_req = 0; if (inv == INV_KICK) flash = 3; }
    if (bar_req) { bar_req = 0; if (inv == INV_BAR) flash = 3; }
    if (flash > 0) { flash--; redraw = 1; }

    /* CRT: power off at a stop, on at a start; a glitch now and then */
    if (crt == CRT_FULL && playing != was_playing) {
        power_dir = playing ? 1 : -1;
        power_anim = POWER_FRAMES;
    }
    was_playing = playing;
    if (power_anim > 0) { power_anim--; redraw = 1; if (!power_anim) redraw_next = 1; }
    if (crt == CRT_FULL && glitch == 0 && rnd(600) == 0) { glitch = 3; glitch_y = (int32_t)rnd(60); }
    if (glitch > 0) { glitch--; redraw = 1; if (!glitch) redraw_next = 1; }
    if (crt != CRT_OFF && (now & 1) == 0) redraw = 1;   /* the rolling line */

    /* 0.4: the screensaver */
    if (++idle > sv_ticks[sv_time] && saver != SV_OFF && !sv_on && (sv_play || !playing)) { sv_on = 1; sv_ready = 0; }
    if (sv_on && (now & 1) == 0) redraw = 1;
    static int32_t sv_was;
    if (sv_was && !sv_on) redraw = 1;                    /* woken: the normal screen back */
    sv_was = sv_on;
    /* 0.4: the effects' timing */
    stopped_ticks = playing ? 0 : stopped_ticks + 1;
    if (beat_req) { if (border) border_w = beat_req == 2 ? 7 : 4; beat_req = 0; }   /* only with BRD on */
    if (border_w > 0) { border_w--; redraw = 1; }
    if (vhs && (now & 1) == 0) redraw = 1;
    if (scan && now % 6 == 0) redraw = 1;             /* the scanlines roll */
    if (snow && stopped_ticks > 0 && (now & 1) == 0) redraw = 1;
    if ((lnk == LNK_FILL || lnk == LNK_BOTH) && fill_active()) redraw = 1;
    if (lnk >= LNK_DJF && (now & 3) == 0) { int32_t lo, hi; dj_cut(&lo, &hi); if (lo > 8 || hi > 8 || (now & 63) == 0) redraw = 1; }
    if (sav_hl_ticks > 0 && --sav_hl_ticks == 0) redraw = 1;
    spec_tick();
    /* 0.5: the art and the overlays */
    lvl_tick();
    lava_t++;
    if (loaded && !boot_done) { boot_done = 1; boot_t = boot_on ? 0 : BOOT_LEN; }
    if (boot_t >= 0 && boot_t < BOOT_LEN) { boot_t++; redraw = 1; if (boot_t == BOOT_LEN) redraw_next = 1; }
    if (rain_mode || storm_shown) { rain_tick(); redraw = 1; }
    if ((storm_shown || rain_mode == 2) && rnd(70) == 0) bolt_strike();   /* its own weather while stopped (bolt_strike rests 2-6 s between) */
    if (name_req) { name_show = name_req; name_req = 0; name_t = 10; }
    if (name_t > 0) { name_t--; redraw = 1; if (!name_t) redraw_next = 1; }
    if (art_hl_ticks > 0 && --art_hl_ticks == 0) redraw = 1;
    { uint32_t lr = life_req; life_req = 0;        /* the Game of Life runs only while it's on screen: in the
                                                      background it took time the +Drive's first-boot job needs */
      if (life_shown) { for (int32_t t = 0; t < 8; t++) if ((lr >> t) & 1 && rnd(3) == 0) life_plant(t);
                        if (playing ? step_now : (now & 7) == 0) life_step(); } }
    saver_leds();

    /* the pages keep moving while shown */
    if (core_mview && core_mview_draws != draws) { draws = core_mview_draws; drawn_at = now; }
    struct core_mpage *pg = core_mview && now - drawn_at < 10 ? core_mpage_shown(core_mview) : 0;
    if (pg) {
        if (pg == &vs_rdr || pg == &vs_art || (pg == &vs_pet && (now & 3) == 0)
            || pg == &vs_scp || (pg == &vs_sav && saver != SV_OFF) || (pg == &vs_clk && (now & 3) == 0))
            fw_invalidate(core_mview);
        else if ((pg == &vs_scr && scr_hl_ticks == 1) || (pg == &vs_sav && sav_hl_ticks == 1))
            fw_invalidate(core_mview);
    }
    if ((rain_mode == 2 || storm_shown) && sky() <= 4) redraw = 1;
    storm_shown = (sv_on && saver == SV_STORM) || (pg == &vs_art && art_mode == 4) || (pg == &vs_sav && saver == SV_STORM);
    life_shown = (sv_on && saver == SV_LIFE) || (pg == &vs_art && art_mode == 2) || (pg == &vs_sav && saver == SV_LIFE);
    scope_on = (pg == &vs_scp && sc_mode != SC_SPEC) || (pg == &vs_art && art_mode == 3) || (sv_on && saver == SV_LISS)
               || (pg == &vs_sav && saver == SV_LISS);         /* the audio hook works only for what's shown */
    spec_on = pg == &vs_scp && sc_mode == SC_SPEC;
    if (redraw) *((uint8_t *)ctrl + 0x20) = 1;
}
