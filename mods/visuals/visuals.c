/* SPDX-License-Identifier: GPL-2.0-or-later */
/* VISUALS for the Digitakt mk1 (core 4.1 + seq-events, OS 1.54): eye candy. Five master pages (FUNC + LFO,
 * then LEFT / RIGHT) and two effects over every screen.
 *
 *   RDR  beat radar: a sweep goes round once a loop of the active track; every hit leaves a blip at its
 *        own track's place in its own loop (track 1 the inner ring, 8 the outer), big for loud hits, fading
 *        as the sweep comes round again. A polymeter's blips drift.
 *   PET  a little creature that lives on the music: it bounces on the beat, throws its arms up on loud hits,
 *        gets hungry and sad in silence, falls asleep after 20 s of it. Turn any knob to pet it.
 *   SNK  Snake: the arrows steer; it moves on the active track's steps while the pattern plays (in time),
 *        on its own when stopped. YES starts, NO pauses.
 *   PNG  Pong: knob B picks 1P (against the Digitakt) or 2P (between games). Player 1 (left) moves with
 *        knob A or UP / DOWN; in 2P player 2 (right) with knob H. YES starts, NO pauses.
 *        In both games knob D picks a track (OFF, 1-8) whose sound plays on each catch or hit.
 *        While a game runs the arrows are its; paused or over, LEFT / RIGHT step the pages again.
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
static volatile int32_t step_req[16];           /* a step began on track t (the UI task takes it) */
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
}

void vs_fire(int32_t track, int32_t step, int32_t *vel)
{
    int32_t v = vel ? *vel : 100;
    struct sq_track info;
    if (track < 0 || track > 15 || step < 0) return;
    fires++;
    if (v >= thr) accent_req = 1;
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

/* ---- the screen effects' settings, saved ----------------------------------------------------------- */
enum { CRT_OFF, CRT_SOFT, CRT_FULL };
enum { INV_OFF, INV_ACC, INV_BAR, INV_KICK };
static int32_t crt = CRT_OFF, inv = INV_OFF;
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
static uint8_t block[512] __attribute__((aligned(16)));
static int32_t loaded, save_wait;
#define MAGIC 0x56495331u                      /* "VIS1" */
static void load(void)
{
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0) return;
    if (*(uint32_t *)block != MAGIC || (uint8_t)(block[4] + block[5] + block[6] + 0x5a) != block[7]) return;
    if (block[4] <= CRT_FULL) crt = block[4];
    if (block[5] <= INV_KICK) inv = block[5];
    if (block[6] >= 64 && block[6] <= 127) thr = block[6];
}
static void save(void)
{
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    *(uint32_t *)block = MAGIC;
    block[4] = (uint8_t)crt; block[5] = (uint8_t)inv; block[6] = (uint8_t)thr;
    block[7] = (uint8_t)(block[4] + block[5] + block[6] + 0x5a);
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
        int32_t size = age < 96 ? (b->big ? 3 : 2) : age < 200 ? (b->big ? 2 : 1) : 1;   /* fading: shrinking */
        if (size) polar(bmp, r, b->ang, size);
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

static void pet_draw(void *view, void *bmp)
{
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
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

/* ---- the games: shared ------------------------------------------------------------------------------ */
enum { G_IDLE, G_RUN, G_PAUSE, G_OVER };
static int32_t snd;                            /* the track a catch plays: 0 off, 1-8 */
static void blip_sound(int32_t vel)
{
    if (snd > 0) { fw_note_off(snd - 1, NOTE, PANEL); fw_note_on(snd - 1, NOTE, vel, PANEL, 0, -1, -1); }
}

/* ---- Snake: 4-pixel cells, 32 x 11 -------------------------------------------------------------------- */
#define SCOLS 32
#define SROWS 11
#define SMAX 64
static uint8_t sx[SMAX], sy[SMAX];
static int32_t s_head, s_len, s_dir, s_next_dir, s_fx, s_fy, s_state = G_IDLE, s_score, s_hi, s_wait;
static const int8_t ddx[4] = { 1, 0, -1, 0 }, ddy[4] = { 0, 1, 0, -1 };   /* right, up, left, down */

static int32_t s_on(int32_t x, int32_t y)
{
    for (int32_t i = 0; i < s_len; i++) {
        int32_t k = (s_head - i + SMAX) % SMAX;
        if (sx[k] == x && sy[k] == y) return 1;
    }
    return 0;
}
static void s_food(void)
{
    do { s_fx = (int32_t)rnd(SCOLS); s_fy = (int32_t)rnd(SROWS); } while (s_on(s_fx, s_fy));
}
static void s_start(void)
{
    s_len = 4; s_head = 3; s_dir = s_next_dir = 0; s_score = 0;
    for (int32_t i = 0; i < 4; i++) { sx[i] = (uint8_t)(10 + i); sy[i] = 5; }
    s_food();
    s_state = G_RUN;
}
static void s_move(void)
{
    if ((s_next_dir ^ s_dir) != 2) s_dir = s_next_dir;          /* never straight back */
    int32_t x = sx[s_head] + ddx[s_dir], y = sy[s_head] + ddy[s_dir];
    if (x < 0 || x >= SCOLS || y < 0 || y >= SROWS || s_on(x, y)) {
        s_state = G_OVER;
        if (s_score > s_hi) s_hi = s_score;
        return;
    }
    s_head = (s_head + 1) % SMAX;
    sx[s_head] = (uint8_t)x; sy[s_head] = (uint8_t)y;
    if (x == s_fx && y == s_fy) {
        if (s_len < SMAX - 1) s_len++;
        s_score++;
        blip_sound(110);
        s_food();
    }
}
static void snk_tick(int32_t step_now)
{
    if (s_state != G_RUN) return;
    int32_t playing = ticks - last_step_tick < 60;
    if (playing ? step_now : ++s_wait >= 8) { s_wait = 0; s_move(); }
}
static void cell(void *bmp, int32_t x, int32_t y, int32_t full)
{
    int32_t px = x * 4, py = y * 4;
    if (full) fw_fillrect(bmp, px, py, px + 2, py + 2, 1);
    else fw_framerect(bmp, px, py, px + 2, py + 2, 1);
}
static void game_status(void *bmp, const char *name, int32_t score, int32_t hi, int32_t state)
{
    char s[24], *p = put(s, name);
    p = put(p, " "); p = ui_num(p, score);
    p = put(p, " HI "); ui_num(p, hi);
    ui_text(bmp, 2, 47, s);
    p = put(s, "SND "); if (snd) ui_num(p, snd); else put(p, "OFF");
    ui_text(bmp, 98, 47, s);
    if (state != G_RUN) {
        const char *m = state == G_OVER ? "GAME OVER  YES: AGAIN" : state == G_PAUSE ? "PAUSED  YES: GO ON" : "YES: START";
        int32_t n = 0; while (m[n]) n++;
        int32_t w = n * 4 + 6, x = 64 - w / 2;
        fw_fillrect(bmp, x, 18, x + w, 28, 0); fw_framerect(bmp, x, 18, x + w, 28, 1);
        ui_text(bmp, x + 4, 21, m);
    }
}
static void snk_draw(void *view, void *bmp)
{
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    fw_framerect(bmp, 0, 0, 127, 45, 1);
    if (s_len) {
        for (int32_t i = 0; i < s_len; i++) {
            int32_t k = (s_head - i + SMAX) % SMAX;
            cell(bmp, sx[k], sy[k], 1);
        }
        if ((ticks / 10) & 1 || s_state != G_RUN) cell(bmp, s_fx, s_fy, 0);   /* the food blinks */
    }
    game_status(bmp, "SNAKE", s_score, s_hi, s_state);
}

/* ---- Pong ----------------------------------------------------------------------------------------------- */
#define PH 10                                   /* paddle height */
static int32_t p_you = 18 << 8, p_cpu = 18 << 8, p_bx, p_by, p_vx, p_vy, p_s1, p_s2, p_state = G_IDLE;
static int32_t view_is_pong;
static int32_t up_held, down_held, p_two;      /* p_two: 2 players (knob B) */
static void p_serve(int32_t dir)
{
    p_bx = 64 << 8; p_by = 22 << 8;
    p_vx = dir * 230; p_vy = (int32_t)rnd(300) - 150;
}
static void png_tick(void)
{
    if (p_state != G_RUN) return;
    if (up_held) p_you += 2 << 8;
    if (down_held) p_you -= 2 << 8;
    p_you = ui_clamp(p_you, 1 << 8, (44 - PH) << 8);
    if (!p_two) {                                                    /* 1P: the Digitakt, a little slow */
        int32_t target = p_by - (PH / 2 << 8), d = target - p_cpu;
        p_cpu += d > 180 ? 180 : d < -180 ? -180 : d;
    }
    p_cpu = ui_clamp(p_cpu, 1 << 8, (44 - PH) << 8);
    p_bx += p_vx; p_by += p_vy;
    if (p_by < (1 << 8)) { p_by = 1 << 8; p_vy = -p_vy; }
    if (p_by > (42 << 8)) { p_by = 42 << 8; p_vy = -p_vy; }
    int32_t by = p_by >> 8;
    if (p_vx < 0 && p_bx <= (5 << 8) && p_bx > (2 << 8) && by + 1 >= p_you >> 8 && by <= (p_you >> 8) + PH) {
        p_vx = -p_vx * 17 / 16; p_vy += (by - (p_you >> 8) - PH / 2) * 40;
        blip_sound(110);
    }
    if (p_vx > 0 && p_bx >= (122 << 8) && p_bx < (125 << 8) && by + 1 >= p_cpu >> 8 && by <= (p_cpu >> 8) + PH) {
        p_vx = -p_vx * 17 / 16; p_vy += (by - (p_cpu >> 8) - PH / 2) * 40;
        blip_sound(80);
    }
    if (p_vx > 600) p_vx = 600;
    if (p_vx < -600) p_vx = -600;
    if (p_bx < 0) { p_s2++; p_serve(1); }
    if (p_bx > (127 << 8)) { p_s1++; p_serve(-1); }
    if (p_s1 >= 9 || p_s2 >= 9) p_state = G_OVER;
}
static void png_draw(void *view, void *bmp)
{
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    fw_fillrect(bmp, 0, 45, 127, 45, 1); fw_fillrect(bmp, 0, 0, 127, 0, 1);
    for (int32_t y = 2; y < 44; y += 4) fw_fillrect(bmp, 63, y, 63, y + 1, 1);   /* the net */
    fw_fillrect(bmp, 3, p_you >> 8, 4, (p_you >> 8) + PH, 1);
    fw_fillrect(bmp, 123, p_cpu >> 8, 124, (p_cpu >> 8) + PH, 1);
    if (p_state == G_RUN) fw_fillrect(bmp, p_bx >> 8, p_by >> 8, (p_bx >> 8) + 1, (p_by >> 8) + 1, 1);
    char s[4];
    ui_num(s, p_s1); ui_text(bmp, 52, 38, s);
    ui_num(s, p_s2); ui_text(bmp, 70, 38, s);
    if (p_state == G_OVER) ui_text(bmp, 2, 47, p_two ? (p_s1 > p_s2 ? "P1 WINS" : "P2 WINS") : p_s1 > p_s2 ? "YOU WIN" : "DIGITAKT WINS");
    else ui_text(bmp, 2, 47, p_two ? "PONG 2P" : "PONG 1P");
    if (p_state != G_RUN) {                                          /* who moves what */
        fw_fillrect(bmp, 1, 1, 126, 7, 0);
        ui_text(bmp, 2, 2, p_two ? "P1: A  P2: H  B: 1P/2P" : "YOU: A OR UP/DOWN  B: 1P/2P");
    }
    char t[12], *p = put(t, "SND "); if (snd) ui_num(p, snd); else put(p, "OFF");
    ui_text(bmp, 98, 47, t);
    if (p_state != G_RUN) {
        const char *m = p_state == G_OVER ? "YES: AGAIN" : p_state == G_PAUSE ? "PAUSED  YES: GO ON" : "YES: START";
        int32_t n = 0; while (m[n]) n++;
        int32_t w = n * 4 + 6, x = 64 - w / 2;
        fw_fillrect(bmp, x, 18, x + w, 28, 0); fw_framerect(bmp, x, 18, x + w, 28, 1);
        ui_text(bmp, x + 4, 21, m);
    }
}

/* ---- the game pages' keys and knobs ----------------------------------------------------------------- */
static int32_t game_key(void *view, void *event, int32_t is_snake)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    int32_t down = (fl & F_DOWN) && !(fl & F_RELEASE), press = down && !(fl & F_REPEAT);
    int32_t *st = is_snake ? &s_state : &p_state;
    if (!is_snake && key == K_UP) up_held = down && *st == G_RUN;
    if (!is_snake && key == K_DOWN) down_held = down && *st == G_RUN;
    if (key == K_YES) {
        if (press) {
            if (*st == G_PAUSE) *st = G_RUN;
            else if (*st != G_RUN) {
                if (is_snake) s_start();
                else { p_s1 = p_s2 = 0; p_serve(-1); p_state = G_RUN; }
            }
            fw_invalidate(view);
        }
        return 1;
    }
    if (key == K_NO && *st == G_RUN) {
        if (press) { *st = G_PAUSE; up_held = down_held = 0; fw_invalidate(view); }
        return 1;
    }
    if (*st != G_RUN)
        return mp_key(view, event);                  /* not playing: LEFT / RIGHT step the pages */
    if (key >= K_UP && key <= K_RIGHT) {
        if (press && is_snake) {
            static const int8_t dir_of[4] = { 1, 3, 2, 0 };   /* UP, DOWN, LEFT, RIGHT */
            s_next_dir = dir_of[key - K_UP];
        }
        return 1;
    }
    return 0;
}
static int32_t snk_key(void *view, void *event) { return game_key(view, event, 1); }
static int32_t game_enc(void *view, void *event);
static int32_t snk_enc(void *view, void *event) { view_is_pong = 0; return game_enc(view, event); }
static int32_t png_enc(void *view, void *event) { view_is_pong = 1; return game_enc(view, event); }
static int32_t png_key(void *view, void *event) { return game_key(view, event, 0); }
static int32_t game_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    static int32_t acc;
    if (knob == 2 && view_is_pong && p_state != G_RUN) {   /* B: 1P / 2P (between games) */
        acc += turn;
        while (acc >= 16 || acc <= -16) { int32_t d = acc > 0 ? 1 : -1; acc -= d * 16; p_two = d > 0; }
        fw_invalidate(view);
        return 1;
    }
    if (knob == 8 && view_is_pong && p_two && p_state == G_RUN) {   /* H: player 2's paddle */
        p_cpu = ui_clamp(p_cpu + turn * 64, 1 << 8, (44 - PH) << 8);
        return 1;
    }
    if (knob == 4) {                                    /* D: the sound */
        acc += turn;
        while (acc >= 16 || acc <= -16) { int32_t d = acc > 0 ? 1 : -1; acc -= d * 16; snd = ui_clamp(snd + d, 0, 8); }
        fw_invalidate(view);
        return 1;
    }
    if (knob == 1 && p_state == G_RUN) {               /* A: your paddle */
        p_you = ui_clamp(p_you + turn * 64, 1 << 8, (44 - PH) << 8);
        return 1;
    }
    return 0;
}
static void snk_clear(void *view) { s_hi = 0; s_state = G_IDLE; s_len = 0; fw_invalidate(view); }
static void png_clear(void *view) { p_s1 = p_s2 = 0; p_state = G_IDLE; fw_invalidate(view); }

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

/* ---- SCR: the screen effects' page ------------------------------------------------------------------- */
static const char *const crt_n[3] = { "OFF", "SFT", "FUL" };
static const char *const inv_n[4] = { "OFF", "ACC", "BAR", "KCK" };
static int32_t scr_turned[3], scr_hl = -1, scr_hl_ticks;
static void scr_draw(void *view, void *bmp)
{
    char s[8];
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    ui_text(bmp, 2, 46, "SCREEN EFFECTS");
    ui_text(bmp, 2, 36, crt == CRT_OFF ? "CRT: OFF" : crt == CRT_SOFT ? "CRT: ROLLING LINE" : "CRT: LINE+POWER+GLITCH");
    ui_text(bmp, 2, 28, inv == INV_OFF ? "FLASH: OFF" : inv == INV_ACC ? "FLASH: LOUD HITS" : inv == INV_BAR ? "FLASH: BEAT 1" : "FLASH: TRACK 1");
    ui_knob(bmp, 0, "CRT", crt_n[crt], crt, 2, 2, scr_hl == 0 && scr_hl_ticks > 0);
    ui_knob(bmp, 1, "INV", inv_n[inv], inv, 3, 2, scr_hl == 1 && scr_hl_ticks > 0);
    ui_num(s, thr);
    ui_knob(bmp, 2, "THR", s, thr - 64, 63, 0, scr_hl == 2 && scr_hl_ticks > 0);
}
static int32_t scr_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > 3) return 0;
    int32_t k = knob - 1, per = k == 2 ? 4 : 16;
    scr_hl = k; scr_hl_ticks = 40;
    if ((scr_turned[k] > 0) != (turn > 0)) scr_turned[k] = 0;
    scr_turned[k] += turn;
    while (scr_turned[k] >= per || scr_turned[k] <= -per) {
        int32_t d = scr_turned[k] > 0 ? 1 : -1;
        scr_turned[k] -= d * per;
        if (k == 0) crt = ui_clamp(crt + d, 0, 2);
        else if (k == 1) inv = ui_clamp(inv + d, 0, 3);
        else thr = ui_clamp(thr + d, 64, 127);
    }
    save_wait = 60;
    fw_invalidate(view);
    return 1;
}
static void scr_clear(void *view) { crt = CRT_OFF; inv = INV_OFF; thr = 110; save_wait = 60; fw_invalidate(view); }

struct core_mpage vs_rdr = { 99, "RDR", "Radar", rdr_draw, mp_key, 0, 0, 0, 0 };
struct core_mpage vs_pet = { 99, "PET", "Pet", pet_draw, mp_key, pet_enc, 0, 0, pet_clear };
struct core_mpage vs_snk = { 99, "SNK", "Snake", snk_draw, snk_key, snk_enc, 0, 0, snk_clear };
struct core_mpage vs_png = { 99, "PNG", "Pong", png_draw, png_key, png_enc, 0, 0, png_clear };
struct core_mpage vs_scr = { 99, "SCR", "Screen FX", scr_draw, mp_key, scr_enc, 0, 0, scr_clear };

/* ---- the effects over every frame -------------------------------------------------------------------- */
static int32_t flash, was_playing, power_anim, power_dir, glitch, glitch_y;   /* power_dir 1 on, -1 off */
#define POWER_FRAMES 14

void vs_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
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
        uint32_t a = (uint32_t)((at_step * 256 + frac) / (at_len > 0 ? at_len : 16)) & 255;
        uint32_t cur = sweep_abs & 255, d = (a - cur) & 255;
        if (d < 128) sweep_abs += d;                     /* only forward: a whole turn counted on a wrap */
    } else
        sweep_abs += 1;                                  /* stopped: a slow idle sweep */

    pet_tick();
    if (sr && energy > 0) bounce = 6;
    snk_tick(step_now);
    png_tick();

    /* the flashes */
    if (accent_req) { accent_req = 0; if (inv == INV_ACC) flash = 3; arms = 30; }
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

    /* the pages keep moving while shown */
    if (core_mview && core_mview_draws != draws) { draws = core_mview_draws; drawn_at = now; }
    if (core_mview && now - drawn_at < 10) {
        struct core_mpage *pg = core_mpage_shown(core_mview);
        if (pg == &vs_rdr || pg == &vs_snk || pg == &vs_png || (pg == &vs_pet && (now & 3) == 0))
            fw_invalidate(core_mview);
        else if (pg == &vs_scr && scr_hl_ticks == 1)
            fw_invalidate(core_mview);
    }
    if (redraw) *((uint8_t *)ctrl + 0x20) = 1;
}
