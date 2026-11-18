/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FREQSEQ for the Digitakt mk1 (OS 1.54): a generative sequencer on every track. Each of the 16 tracks
 * (8 audio, 8 MIDI) can run one of eight sequencers, each with its own settings:
 *
 *   POLY     after the Moog Subharmonicon: four rhythms (every 1-16 steps) play the track, and each
 *            time they fire a 4-step pitch sequence moves on
 *   CORRUPT  after the Labyrinth: each loop a few steps get a bit of their drift flipped (notes, then
 *            velocities, then filter / resonance / pan), so it mutates loop after loop
 *   TURING   after Music Thing's Turing Machine: a loop of bits makes notes (and rhythm), locked or
 *            slowly changing
 *   DRUNK    the playhead wanders: backwards, ping-pong, anywhere, or a staggering walk
 *   LIFE     Conway's Game of Life: every LIFE track is a row of one board, 16 steps wide; each loop
 *            the next generation
 *   EROSION  each loop some trigs fade a step, until the pattern is gone (and, if CYCL, back again)
 *   MARKOV   learns which note follows which from the track's notes and what you play on it, then
 *            plays notes picked from that
 *   GRIDS    after Mutable Instruments' Grids: a kick, snare or hat part read off a map of drum
 *            patterns at X / Y, as busy as DENS
 *
 * The FEEL page (DOWN) holds what all of them share: SCALE and ROOT (the melodic ones play in key),
 * HUMAN (a random velocity nudge) and GROOVE (off-beats softer).
 *
 * TRK + PAGE (no trig held) opens the page for the active track; change track as usual (TRK + trig).
 * LEFT / RIGHT pick the track's sequencer, the knobs set it, YES does its action (CORRUPT reset,
 * TURING new loop, DRUNK new path, LIFE new board, EROSION restore, MARKOV forget), DOWN / UP the FEEL
 * page, NO closes. The pattern itself never changes: seq-events changes what plays. Saved.
 *
 * GRIDS' drum maps (grids.h) are Emilie Gillet's, GPL v3, so FREQSEQ is GPL v3 or later. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"
#include "grids.h"

#define NT 16                                       /* tracks: 0-7 audio, 8-15 MIDI */
enum { M_OFF, M_POLY, M_CORR, M_TURN, M_DRNK, M_LIFE, M_EROS, M_MRKV, M_GRID,
       M_CANN, M_ROUL, M_SCAT, M_WOLF, M_MATH, M_MORS, M_BERN, MODES };
static const char *const mode_name[MODES] = { "OFF", "POLYRHYTHM", "CORRUPT", "TURING", "DRUNK", "LIFE",
                                              "EROSION", "MARKOV", "GRIDS", "CANON", "ROULETTE", "SCATTER",
                                              "WOLFRAM", "MATHS", "MORSE", "BERNOULLI" };

/* ---- the settings: per track and sequencer, a few values each, packed ------------------------------- */
enum { F_NUM, F_PCT, F_DIV, F_SGN, F_NAME };
struct prm { const char *name; int8_t min, max, def, step, fmt; const char *const *names; };
static const char *const what_n[3] = { "NTE", "N+V", "ALL" };
static const char *const tmode_n[3] = { "NTE", "TRG", "BTH" };
static const char *const dmode_n[4] = { "REV", "PNG", "RND", "DRK" };
static const char *const rule_n[4] = { "LIF", "HIG", "SED", "MAZ" };
static const char *const onoff_n[2] = { "OFF", "ON" };
static const char *const part_n[3] = { "KCK", "SNR", "HAT" };

static const struct prm p_poly[8] = {
    { "R1", 0, 16, 2, 1, F_DIV, 0 }, { "R2", 0, 16, 3, 1, F_DIV, 0 }, { "R3", 0, 16, 0, 1, F_DIV, 0 },
    { "R4", 0, 16, 0, 1, F_DIV, 0 }, { "S1", -12, 12, 0, 1, F_SGN, 0 }, { "S2", -12, 12, 7, 1, F_SGN, 0 },
    { "S3", -12, 12, 12, 1, F_SGN, 0 }, { "S4", -12, 12, 5, 1, F_SGN, 0 } };
static const struct prm p_corr[2] = {
    { "AMT", 0, 100, 20, 5, F_PCT, 0 }, { "WHT", 0, 2, 0, 1, F_NAME, what_n } };
static const struct prm p_turn[6] = {
    { "LCK", 0, 100, 80, 5, F_PCT, 0 }, { "LEN", 2, 16, 8, 1, F_NUM, 0 }, { "RNG", 1, 36, 12, 1, F_NUM, 0 },
    { "MOD", 0, 2, 0, 1, F_NAME, tmode_n }, { "DNS", 0, 100, 50, 5, F_PCT, 0 },
    { "VEL", 0, 100, 0, 5, F_PCT, 0 } };
static const struct prm p_drnk[2] = {
    { "MOD", 0, 3, 3, 1, F_NAME, dmode_n }, { "AMT", 0, 100, 100, 5, F_PCT, 0 } };
static const struct prm p_life[2] = {
    { "RUL", 0, 3, 0, 1, F_NAME, rule_n }, { "LPS", 1, 8, 1, 1, F_NUM, 0 } };
static const struct prm p_eros[3] = {
    { "AMT", 0, 100, 20, 5, F_PCT, 0 }, { "SFT", 1, 4, 2, 1, F_NUM, 0 }, { "CYC", 0, 1, 1, 1, F_NAME, onoff_n } };
static const struct prm p_mrkv[2] = {
    { "AMT", 0, 100, 60, 5, F_PCT, 0 }, { "LRN", 0, 1, 1, 1, F_NAME, onoff_n } };
static const struct prm p_grid[5] = {
    { "PRT", 0, 2, 0, 1, F_NAME, part_n }, { "X", 0, 127, 64, 2, F_NUM, 0 }, { "Y", 0, 127, 64, 2, F_NUM, 0 },
    { "DNS", 0, 127, 80, 2, F_NUM, 0 }, { "CHA", 0, 127, 10, 2, F_NUM, 0 } };

/* 0.2 */
static const char *const grid_n[5] = { "OFF", "2", "4", "8", "16" };
static const char *const wolf_n[8] = { "30", "45", "54", "60", "90", "105", "110", "150" };
static const uint8_t wolf_rule[8] = { 30, 45, 54, 60, 90, 105, 110, 150 };
static const char *const msrc_n[3] = { "PI", "FIB", "PRM" };
#define WORDS 10
static const char *const word_n[WORDS] = { "SOS", "DUB", "HIT", "BAR", "SUB", "LOW", "FUN", "MIX", "ART", "TEK" };
static const struct prm p_cann[3] = {
    { "SRC", 1, 16, 1, 1, F_NUM, 0 }, { "DLY", 1, 16, 4, 1, F_NUM, 0 }, { "TRN", -24, 24, 12, 1, F_SGN, 0 } };
static const struct prm p_roul[3] = {
    { "LO", 0, 127, 0, 1, F_NUM, 0 }, { "HI", 0, 127, 7, 1, F_NUM, 0 }, { "PRB", 0, 100, 100, 5, F_PCT, 0 } };
static const struct prm p_scat[3] = {
    { "AMT", 0, 127, 64, 2, F_NUM, 0 }, { "PRB", 0, 100, 50, 5, F_PCT, 0 }, { "GRD", 0, 4, 3, 1, F_NAME, grid_n } };
static const struct prm p_wolf[2] = {
    { "RUL", 0, 7, 0, 1, F_NAME, wolf_n }, { "LPS", 1, 8, 1, 1, F_NUM, 0 } };
static const struct prm p_math[4] = {
    { "SRC", 0, 2, 0, 1, F_NAME, msrc_n }, { "MOD", 0, 2, 0, 1, F_NAME, tmode_n }, { "RNG", 1, 24, 12, 1, F_NUM, 0 },
    { "OFS", 0, 63, 0, 1, F_NUM, 0 } };
static const struct prm p_mors[2] = {
    { "WRD", 0, WORDS - 1, 0, 1, F_NAME, word_n }, { "UNT", 1, 4, 1, 1, F_NUM, 0 } };
static const struct prm p_bern[2] = {
    { "TRK", 1, 16, 2, 1, F_NUM, 0 }, { "PRB", 0, 100, 50, 5, F_PCT, 0 } };

static const struct prm *const mode_prm[MODES] = { 0, p_poly, p_corr, p_turn, p_drnk, p_life, p_eros, p_mrkv, p_grid,
                                                   p_cann, p_roul, p_scat, p_wolf, p_math, p_mors, p_bern };
static const uint8_t mode_n[MODES] = { 0, 8, 2, 6, 2, 2, 3, 2, 5, 3, 3, 3, 2, 4, 2, 2 };
static const uint8_t mode_off[MODES] = { 0, 0, 8, 10, 16, 18, 20, 23, 25,      /* where each starts */
                                         30, 33, 36, 39, 41, 45, 47 };
#define PER_TRACK 49
#define PER_TRACK_V1 30

static const char *const scale_n[13] = { "OFF", "MAJ", "MIN", "DOR", "PHR", "LYD", "MIX",
                                         "LOC", "HMI", "MMI", "PN+", "PN-", "BLU" };
static const char *const root_n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
enum { G_SCALE, G_ROOT, G_HUMAN, G_GROOVE, GLOBALS };
static const struct prm p_feel[GLOBALS] = {
    { "SCL", 0, 12, 0, 1, F_NAME, scale_n }, { "KEY", 0, 11, 0, 1, F_NAME, root_n },
    { "HUM", 0, 32, 0, 2, F_NUM, 0 }, { "GRV", 0, 50, 0, 5, F_NUM, 0 } };

static volatile int8_t mode[NT];
static volatile int8_t prm[NT][PER_TRACK];
static volatile int8_t glob[GLOBALS];
#define P(t, m, i) (prm[t][mode_off[m] + (i)])

/* ---- each track's working state: only its own sequencer's, so it shares the room ----------------------- */
static struct st {
    uint32_t parity_seen;
    union {
        struct { int8_t note[SQ_STEPS], vel[SQ_STEPS], lock[3][SQ_STEPS]; } c;
        struct { uint8_t tab[2][SQ_STEPS]; uint8_t pos; } d;
        struct { uint8_t level[SQ_STEPS]; uint8_t silent; } e;
        struct { uint8_t count[12][12]; int8_t prev_in, prev_out; } m;
        struct { uint16_t reg; uint8_t hpos, hist[16]; uint8_t fire[SQ_STEPS]; } t;
        struct { uint8_t pert, loops; } g;
        struct { int8_t note[SQ_STEPS]; uint8_t vel[SQ_STEPS]; uint32_t hit[2]; } n;      /* CANON's memory */
        struct { uint16_t row; uint8_t loops, hpos; uint16_t hist[6]; } w;      /* WOLFRAM */
        struct { uint16_t pos; uint8_t fire[SQ_STEPS]; } x;                              /* MATHS */
        struct { uint8_t stream[128]; uint8_t len, pos, fire[SQ_STEPS]; } o;             /* MORSE */
        struct { uint32_t stored[2]; } b;                                                /* BERNOULLI */
    } u;
} st[NT];
static volatile uint16_t l_grid[NT];               /* LIFE: each LIFE track's row of the board */
static uint8_t l_loops;
static volatile uint32_t rearm;

static uint32_t dice = 0x6d2b79f5u, idice = 0x2545f491u;
static uint32_t roll(uint32_t *d)
{
    uint32_t x = *d;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *d = x;
}

/* ---- SCALE LOCK ------------------------------------------------------------------------------------------ */
static const uint16_t scale_mask[13] = {            /* bit n: semitone n above the root */
    0xfff, 0xab5, 0x5ad, 0x6ad, 0x5ab, 0xad5, 0x6b5, 0x56b, 0x9ad, 0xaad, 0x295, 0x4a9, 0x4e9 };
static int32_t lock_note(int32_t n)
{
    uint32_t mask = scale_mask[glob[G_SCALE]];
    int32_t root = glob[G_ROOT];
    if (!glob[G_SCALE])
        return n;
    for (int32_t d = 0; d <= 6; d++) {              /* the nearest, down first on a tie */
        int32_t lo = n - d, hi = n + d;
        if (lo >= 0 && (mask >> ((lo - root + 120) % 12)) & 1) return lo;
        if (hi <= 127 && (mask >> ((hi - root + 120) % 12)) & 1) return hi;
    }
    return n;
}
static int melodic(int32_t m) { return m == M_POLY || m == M_CORR || m == M_TURN || m == M_MRKV || m == M_CANN || m == M_MATH; }

/* ---- the sequencers' sums (interrupt level unless said) ------------------------------------------------- */
static int poly_fires(int32_t t, int32_t step)
{
    for (int r = 0; r < 4; r++) {
        int32_t n = P(t, M_POLY, r);
        if (n && step % n == 0)
            return 1;
    }
    return 0;
}

static void t_clock(int32_t t)
{
    struct st *s = &st[t];
    int32_t len = P(t, M_TURN, 1);
    uint32_t r = s->u.t.reg, bit = (r >> (len - 1)) & 1u, x = roll(&dice);
    if ((int32_t)(x % 200u) < 100 - P(t, M_TURN, 0))
        bit ^= 1u;                                  /* a coin flip at 0 %, never at 100 % */
    r = ((r << 1) | bit) & ((1u << len) - 1u);
    if (len < 8)                                    /* a short loop repeats to fill the 8 bits */
        for (int32_t k = len; k < 16; k += len)
            r |= (r & ((1u << len) - 1u)) << k;
    s->u.t.reg = (uint16_t)r;
    s->u.t.hist[s->u.t.hpos & 15] = (uint8_t)(r & 255u);
    s->u.t.hpos++;
}

static int32_t g_level(int32_t t, int32_t step)
{
    int32_t part = P(t, M_GRID, 0), x = P(t, M_GRID, 1) * 2, y = P(t, M_GRID, 2) * 2;
    int32_t i = x >> 6, j = y >> 6, fx = (x << 2) & 255, fy = (y << 2) & 255, off = part * 32 + (step & 31);
    if (i > 3) { i = 3; fx = 255; }
    if (j > 3) { j = 3; fy = 255; }
    int32_t a = g_node[g_map[i][j]][off], b = g_node[g_map[i + 1][j]][off];
    int32_t c = g_node[g_map[i][j + 1]][off], d = g_node[g_map[i + 1][j + 1]][off];
    int32_t ab = (a * (255 - fx) + b * fx) >> 8, cd = (c * (255 - fx) + d * fx) >> 8;
    int32_t lv = (ab * (255 - fy) + cd * fy) >> 8, pert = st[t].u.g.pert;
    return lv < 255 - pert ? lv + pert : 255;
}
static int g_hits(int32_t t, int32_t step) { return g_level(t, step) > 255 - P(t, M_GRID, 3) * 2; }

/* ---- 0.2's sums ------------------------------------------------------------------------------------------- */
static int bit_of(const volatile uint32_t *m, int32_t k) { return (m[k >> 5] >> (k & 31)) & 1u; }
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x85ebca6bu ^ b * 0xc2b2ae35u ^ c * 0x27d4eb2fu ^ 0x9e3779b9u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
/* BERNOULLI: does source s's trig at step stay on s (1) or go to its other track (0)? */
static int bern_stays(int32_t s, int32_t step)
{
    return (int32_t)(hash3((uint32_t)s, (uint32_t)step, sq_clock[s].parity) % 100u) < P(s, M_BERN, 1);
}
/* MATHS: the value (0-9) at position pos, and whether it plays as a trig */
static const uint8_t pi_digits[64] = { 3,1,4,1,5,9,2,6,5,3,5,8,9,7,9,3,2,3,8,4,6,2,6,4,3,3,8,3,2,7,9,5,
                                       0,2,8,8,4,1,9,7,1,6,9,3,9,9,3,7,5,1,0,5,8,2,0,9,7,4,9,4,4,5,9,2 };
static int is_prime(int32_t n) { if (n < 2) return 0; for (int32_t d = 2; d * d <= n; d++) if (n % d == 0) return 0; return 1; }
static int32_t fib10(int32_t n) { int32_t a = 0, b = 1; n %= 60; while (n--) { int32_t c = (a + b) % 10; a = b; b = c; } return a; }
static int32_t math_value(int32_t t, int32_t pos)
{
    int32_t p = pos + P(t, M_MATH, 3);
    switch (P(t, M_MATH, 0)) {
    case 0: return pi_digits[p & 63];
    case 1: return fib10(p);
    default: { int32_t k = 0, n = 1; while (k <= p % 64) { n++; if (is_prime(n)) k++; } return n % 10; }
    }
}
static int math_fires(int32_t t, int32_t pos)
{
    int32_t p = pos + P(t, M_MATH, 3);
    switch (P(t, M_MATH, 0)) {
    case 0: return pi_digits[p & 63] >= 5;
    case 1: return fib10(p) & 1;
    default: return is_prime(p % 64 + 1);
    }
}

/* ---- seq-events ----------------------------------------------------------------------------------------- */
int32_t ss_fire_step(int32_t track, int32_t step, int32_t len)
{
    if (track < 0 || track >= NT || mode[track] != M_DRNK || step < 0 || step >= SQ_STEPS)
        return step;
    int32_t m = st[track].u.d.tab[sq_clock[track].parity & 1u][step];
    return m < len ? m : step;
}

void ss_gen_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    if (track < 0 || track >= NT || step < 0 || step >= SQ_STEPS)
        return;
    struct st *s = &st[track];
    if (first)                                      /* CANON: a source's step is unplayed till it fires */
        for (int32_t c = 0; c < NT; c++)
            if (mode[c] == M_CANN && P(c, M_CANN, 0) - 1 == track)
                st[c].u.n.hit[step >> 5] &= ~(1u << (step & 31));
    switch (mode[track]) {
    case M_POLY:
        *trig = poly_fires(track, step) ? SQ_TRIG : 0;
        break;
    case M_CANN: {                                  /* what the source played DLY steps ago */
        int32_t src = P(track, M_CANN, 0) - 1;
        struct sq_track info;
        int32_t len = src >= 0 && sq_track_info(src, &info, sizeof info) == 0 ? info.len : SQ_STEPS;
        int32_t from = ((step - P(track, M_CANN, 1)) % len + len) % len;
        *trig = src != track && bit_of(s->u.n.hit, from) ? SQ_TRIG : 0;
        break;
    }
    case M_WOLF:
        *trig = (s->u.w.row >> (step & 15)) & 1u ? SQ_TRIG : 0;
        break;
    case M_MATH:
        if (P(track, M_MATH, 1)) {                  /* TRIG / BOTH: on each step */
            if (first) {
                s->u.x.fire[step] = (uint8_t)math_fires(track, s->u.x.pos);
                s->u.x.pos++;
            }
            *trig = s->u.x.fire[step] ? SQ_TRIG : 0;
        }
        break;
    case M_MORS:
        if (first && s->u.o.len) {
            uint32_t unit = (uint32_t)P(track, M_MORS, 1), at = s->u.o.pos;
            s->u.o.fire[step] = at % unit == 0 ? s->u.o.stream[(at / unit) % s->u.o.len] : 0;
            s->u.o.pos = (uint8_t)((at + 1) % (s->u.o.len * unit));
        }
        *trig = s->u.o.fire[step] ? SQ_TRIG : 0;
        break;
    case M_BERN:
        if ((*trig & SQ_TRIG) && !bern_stays(track, step))
            *trig &= ~SQ_TRIG;                      /* this one goes to the other track */
        break;
    case M_TURN:
        if (P(track, M_TURN, 3)) {                  /* TRIG / BOTH: it moves on each step */
            if (first) {
                t_clock(track);
                s->u.t.fire[step] = (s->u.t.reg & 255) * 100 >= (100 - P(track, M_TURN, 4)) * 256;
            }
            *trig = s->u.t.fire[step] ? SQ_TRIG : 0;
        }
        break;
    case M_LIFE:
        *trig = (l_grid[track] >> (step & 15)) & 1u ? SQ_TRIG : 0;
        break;
    case M_EROS:
        if (!s->u.e.level[step])
            *trig &= ~SQ_TRIG;
        break;
    case M_GRID:
        *trig = g_hits(track, step) ? SQ_TRIG : 0;
        break;
    }
    for (int32_t b = 0; b < NT; b++)                /* BERNOULLI: trigs sent here from another track */
        if (mode[b] == M_BERN && b != track && P(b, M_BERN, 0) - 1 == track &&
            bit_of(st[b].u.b.stored, step) && !bern_stays(b, step))
            *trig |= SQ_TRIG;
}

void ss_gen_notes(int32_t track, int32_t step, int32_t generated, int8_t notes[SQ_NOTES])
{
    if (track < 0 || track >= NT || notes[0] < 0)
        return;
    struct st *s = &st[track];
    if (mode[track] == M_POLY) {
        int32_t pos = 0;                            /* how many times the rhythms fired before */
        for (int32_t k = 0; k < step; k++)
            pos += poly_fires(track, k);
        int32_t n = notes[0] + P(track, M_POLY, 4 + (pos & 3));
        notes[0] = (int8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
    } else if (mode[track] == M_TURN) {
        if (!P(track, M_TURN, 3))
            t_clock(track);                         /* NOTE mode: it moves on with each trig */
        if (P(track, M_TURN, 3) != 1) {
            int32_t n = notes[0] + (s->u.t.reg & 255) * P(track, M_TURN, 2) / 255;
            notes[0] = (int8_t)(n > 127 ? 127 : n);
        }
    } else if (mode[track] == M_CANN) {
        int32_t src = P(track, M_CANN, 0) - 1;
        struct sq_track info;
        int32_t len = src >= 0 && sq_track_info(src, &info, sizeof info) == 0 ? info.len : SQ_STEPS;
        int32_t from = ((step - P(track, M_CANN, 1)) % len + len) % len, n = s->u.n.note[from] + P(track, M_CANN, 2);
        notes[0] = (int8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
    } else if (mode[track] == M_MATH) {
        if (!P(track, M_MATH, 1))
            s->u.x.pos++;                           /* NOTE mode: on with each trig */
        if (P(track, M_MATH, 1) != 1) {
            int32_t v = math_value(track, s->u.x.pos), n = notes[0] + v * P(track, M_MATH, 2) / 9;
            notes[0] = (int8_t)(n > 127 ? 127 : n);
        }
    } else if (mode[track] == M_MRKV && P(track, M_MRKV, 1) && !generated) {
        int32_t pc = notes[0] % 12;                 /* it learns the track's own notes */
        if (s->u.m.prev_in >= 0) {
            uint8_t *row = s->u.m.count[s->u.m.prev_in];
            if (row[pc] == 255)
                for (int i = 0; i < 12; i++) row[i] >>= 1;
            row[pc]++;
        }
        s->u.m.prev_in = (int8_t)pc;
    }
}

static void canon_record(int32_t track, int32_t step, int32_t note)
{
    for (int32_t c = 0; c < NT; c++)
        if (mode[c] == M_CANN && c != track && P(c, M_CANN, 0) - 1 == track) {
            st[c].u.n.note[step] = (int8_t)note;
            st[c].u.n.hit[step >> 5] |= 1u << (step & 31);
        }
}

void ss_fire_notes(int32_t track, int32_t step, int8_t notes[SQ_NOTES])
{
    if (track < 0 || track >= NT || notes[0] < 0 || step < 0 || step >= SQ_STEPS)
        return;
    if (!melodic(mode[track])) {
        canon_record(track, step, notes[0]);
        return;
    }
    struct st *s = &st[track];
    int32_t n = notes[0];
    if (mode[track] == M_CORR)
        n += s->u.c.note[step];
    else if (mode[track] == M_MRKV) {
        uint32_t x = roll(&dice);
        int32_t from = s->u.m.prev_out >= 0 ? s->u.m.prev_out : n % 12, total = 0;
        for (int32_t d = 0; d < 12 && !total; d++) {   /* nothing learnt after it: the nearest that has */
            int32_t f = (from + (d & 1 ? (d + 1) / 2 : 12 - d / 2)) % 12;
            int32_t sum = 0;
            for (int i = 0; i < 12; i++) sum += s->u.m.count[f][i];
            if (sum) { from = f; total = sum; }
        }
        if (total && (int32_t)(x % 100u) < P(track, M_MRKV, 0)) {
            int32_t pick = (int32_t)((x >> 8) % (uint32_t)total), pc = 0;
            while (pick >= s->u.m.count[from][pc]) pick -= s->u.m.count[from][pc++];
            n += (pc - n % 12 + 18) % 12 - 6;       /* the nearest note of that class */
        }
        s->u.m.prev_out = (int8_t)(((n % 12) + 12) % 12);
    }
    notes[0] = (int8_t)lock_note(n < 0 ? 0 : n > 127 ? 127 : n);
    canon_record(track, step, notes[0]);
}

void ss_fire_vel(int32_t track, int32_t step, int32_t *vel)
{
    if (track < 0 || track >= NT || step < 0 || step >= SQ_STEPS)
        return;
    for (int32_t c = 0; c < NT; c++)                /* CANON: the source's velocity too */
        if (mode[c] == M_CANN && c != track && P(c, M_CANN, 0) - 1 == track)
            st[c].u.n.vel[step] = (uint8_t)(*vel < 0 ? 0 : *vel > 127 ? 127 : *vel);
    if (mode[track] == M_OFF)
        return;
    struct st *s = &st[track];
    switch (mode[track]) {
    case M_CANN: {
        int32_t src = P(track, M_CANN, 0) - 1;
        struct sq_track info;
        int32_t len = src >= 0 && sq_track_info(src, &info, sizeof info) == 0 ? info.len : SQ_STEPS;
        int32_t from = ((step - P(track, M_CANN, 1)) % len + len) % len;
        if (s->u.n.vel[from]) *vel = s->u.n.vel[from];
        break;
    }
    case M_MORS:
        if (s->u.o.fire[step] == 2) *vel += 30;     /* dashes are accents */
        break;
    case M_CORR:
        if (P(track, M_CORR, 1) >= 1)
            *vel += s->u.c.vel[step] * 8;
        break;
    case M_TURN:
        if (P(track, M_TURN, 5))                    /* VEL: some from the loop's top bits */
            *vel = (*vel * (100 - P(track, M_TURN, 5)) + (int32_t)((s->u.t.reg >> 8) & 127u) *
                    P(track, M_TURN, 5)) / 100;
        break;
    case M_EROS:
        if (s->u.e.level[step] < P(track, M_EROS, 1))
            *vel = *vel * s->u.e.level[step] / P(track, M_EROS, 1);
        break;
    case M_GRID:
        if (g_level(track, step) <= 192)
            *vel = *vel * 70 / 100;                 /* the strong hits are its accents */
        break;
    }
    if (glob[G_GROOVE] && (step & 1))
        *vel -= glob[G_GROOVE];
    if (glob[G_HUMAN]) {
        uint32_t x = roll(&idice);
        *vel += (int32_t)(x % (uint32_t)(2 * glob[G_HUMAN] + 1)) - glob[G_HUMAN];
    }
}

/* CORRUPT's ALL: filter FREQ, RESO and PAN drift from the sound's own value (the kit's) */
static const uint8_t lock_slot[3] = { 26, 27, 44 };
/* 0.5: a lock of exactly 0 reads as no lock on the mk1 (found with REVERSE), so 0 goes out as 0x0001: the same
 * value, a fine part of 1 */
static int16_t nz(int32_t v) { return (int16_t)(v ? v : 1); }
#define KIT_WORD(t, k) (*(volatile int16_t *)(fw_kit + 0x20 + 0xa2 * (t) + 20 + 2 * (k)))

int32_t ss_fire_locks(int32_t track, int32_t step, struct sq_lock *out, int32_t room)
{
    int32_t n = 0;
    if (track >= 0 && track < SQ_AUDIO && room > 0 && (mode[track] == M_ROUL || mode[track] == M_SCAT)) {
        uint32_t x = roll(&idice);
        int32_t m = mode[track];
        if ((int32_t)(x % 100u) >= P(track, m, m == M_ROUL ? 2 : 1))
            return 0;
        if (m == M_ROUL) {                          /* ROULETTE: a sample from LO to HI */
            int32_t lo = P(track, M_ROUL, 0), hi = P(track, M_ROUL, 1);
            if (hi < lo) { int32_t t2 = lo; lo = hi; hi = t2; }
            out[0].slot = 20;                       /* SAMP */
            out[0].value = nz((lo + (int32_t)((x >> 8) % (uint32_t)(hi - lo + 1))) << 8);
        } else {                                    /* SCATTER: STRT moved, maybe onto a slice grid */
            int32_t base = fw_kit ? KIT_WORD(track, 21) >> 8 : 0, amt = P(track, M_SCAT, 0);
            int32_t v = base + (int32_t)((x >> 8) % (uint32_t)(amt + 1));
            int32_t g = P(track, M_SCAT, 2);
            if (g) { int32_t slice = 128 >> g; v = v / slice * slice; }
            out[0].slot = 21;                       /* STRT */
            out[0].value = nz((v > 127 ? v % 128 : v) << 8);
        }
        return 1;
    }
    if (track < 0 || track >= SQ_AUDIO || mode[track] != M_CORR || P(track, M_CORR, 1) < 2 ||
        step < 0 || step >= SQ_STEPS || !fw_kit)
        return 0;
    for (int k = 0; k < 3 && n < room; k++) {
        int32_t d = st[track].u.c.lock[k][step];
        if (!d)
            continue;
        int32_t v = KIT_WORD(track, lock_slot[k]) + d * (4 << 8);   /* +-32 of 127 at most */
        out[n].slot = lock_slot[k];
        out[n].value = nz(v < 0 ? 0 : v > 0x7f00 ? 0x7f00 : v);
        n++;
    }
    return n;
}

int32_t ss_live_note(int32_t track, int32_t note, int32_t vel, int32_t on, int32_t source)
{
    (void)vel; (void)source;
    if (track >= 0 && track < NT && mode[track] == M_MRKV && on && P(track, M_MRKV, 1) && note >= 0 && note < 128) {
        struct st *s = &st[track];                  /* MARKOV learns what you play too */
        int32_t pc = note % 12;
        if (s->u.m.prev_in >= 0 && s->u.m.count[s->u.m.prev_in][pc] < 255)
            s->u.m.count[s->u.m.prev_in][pc]++;
        s->u.m.prev_in = (int8_t)pc;
    }
    return 0;
}

/* ---- the UI task: each track's work once a loop ---------------------------------------------------------- */
static int32_t track_len(int32_t t)
{
    struct sq_track info;
    return sq_track_info(t, &info, sizeof info) == 0 ? info.len : SQ_STEPS;
}
static int32_t sext4(int32_t v) { v &= 15; return v >= 8 ? v - 16 : v; }
/* bit k of a 64-step mask, in 32-bit halves (no 64-bit shift helper on the device) */
static int bit64(uint64_t v, int32_t k) { return k < 32 ? ((uint32_t)v >> k) & 1u : ((uint32_t)(v >> 32) >> (k - 32)) & 1u; }

static void corrupt_loop(int32_t t)
{
    struct st *s = &st[t];
    int32_t amt = P(t, M_CORR, 0), what = P(t, M_CORR, 1), len = track_len(t);
    for (int32_t k = 0; k < len; k++) {
        if ((int32_t)(roll(&dice) % 100u) >= amt)
            continue;
        uint32_t r = roll(&dice);
        int32_t which = what == 0 ? 0 : what == 1 ? (int32_t)(r & 1) : (int32_t)(r % 5u);
        int32_t bit = 1 << ((r >> 8) & 3);
        if (which == 0)
            s->u.c.note[k] = (int8_t)sext4(s->u.c.note[k] ^ bit);
        else if (which == 1)
            s->u.c.vel[k] = (int8_t)sext4(s->u.c.vel[k] ^ bit);
        else if (t < SQ_AUDIO)
            s->u.c.lock[which - 2][k] = (int8_t)sext4(s->u.c.lock[which - 2][k] ^ bit);
    }
}

/* DRUNK: the path of the loop with parity q */
static void drunk_fill(int32_t t, uint32_t q)
{
    struct st *s = &st[t];
    int32_t m = P(t, M_DRNK, 0), amt = P(t, M_DRNK, 1), len = track_len(t), pos = s->u.d.pos;
    uint8_t *tab = s->u.d.tab[q & 1u];
    for (int32_t k = 0; k < SQ_STEPS; k++) {
        int32_t v = k;
        if (k < len) {
            if (m == 0) v = len - 1 - k;
            else if (m == 1) v = (q & 1u) ? len - 1 - k : k;
            else if (m == 2) v = (int32_t)(roll(&dice) % (uint32_t)len);
            else {
                uint32_t r = roll(&dice) % 20u;     /* forward, back, stay, or skip one */
                pos += r < 10 ? 1 : r < 15 ? -1 : r < 18 ? 0 : 2;
                pos = (pos % len + len) % len;
                v = pos;
            }
            if ((int32_t)(roll(&dice) % 100u) >= amt)
                v = k;                              /* this step plays as it is */
        }
        tab[k] = (uint8_t)v;
    }
    s->u.d.pos = (uint8_t)pos;
}

static void eros_restore(int32_t t)
{
    for (int32_t k = 0; k < SQ_STEPS; k++)
        st[t].u.e.level[k] = (uint8_t)P(t, M_EROS, 1);
    st[t].u.e.silent = 0;
}
static void eros_loop(int32_t t)
{
    struct st *s = &st[t];
    uint64_t trigs = sq_stored(t);
    int32_t len = track_len(t), left = 0;
    for (int32_t k = 0; k < len; k++) {
        if (!bit64(trigs, k) || !s->u.e.level[k])
            continue;
        if ((int32_t)(roll(&dice) % 100u) < P(t, M_EROS, 0))
            s->u.e.level[k]--;
        left += s->u.e.level[k] != 0;
    }
    if (!left && P(t, M_EROS, 2) && ++s->u.e.silent > 1)
        eros_restore(t);                            /* CYCL: a loop of silence, then all back */
}

/* LIFE: one generation of the board made of every LIFE track (in track order) */
static void life_seed(int32_t t) { l_grid[t] = (uint16_t)(sq_stored(t) & 0xffffu); }
static void life_step(int32_t rule)
{
    static const uint16_t born[4] = { 1 << 3, (1 << 3) | (1 << 6), 1 << 2, 1 << 3 };
    static const uint16_t keep[4] = { (1 << 2) | (1 << 3), (1 << 2) | (1 << 3), 0, 0x3e };
    int32_t rows[NT], n = 0, changed = 0, alive = 0;
    uint16_t next[NT];
    for (int32_t t = 0; t < NT; t++)
        if (mode[t] == M_LIFE) rows[n++] = t;
    for (int32_t r = 0; r < n; r++) {
        uint16_t up = l_grid[rows[(r + n - 1) % n]], me = l_grid[rows[r]], dn = l_grid[rows[(r + 1) % n]];
        uint16_t out = 0;
        for (int32_t c = 0; c < 16; c++) {
            int32_t cl = (c + 15) & 15, cr = (c + 1) & 15, cnt = 0;
            cnt += ((up >> cl) & 1) + ((up >> c) & 1) + ((up >> cr) & 1);
            cnt += ((me >> cl) & 1) + ((me >> cr) & 1);
            cnt += ((dn >> cl) & 1) + ((dn >> c) & 1) + ((dn >> cr) & 1);
            int32_t on = (me >> c) & 1;
            if (on ? (keep[rule] >> cnt) & 1 : (born[rule] >> cnt) & 1)
                out |= (uint16_t)(1u << c);
        }
        next[r] = out;
        changed |= out != me;
        alive |= out != 0;
    }
    for (int32_t r = 0; r < n; r++)
        l_grid[rows[r]] = next[r];
    if (n && !alive)
        for (int32_t r = 0; r < n; r++) life_seed(rows[r]);          /* all dead: from the pattern again */
    else if (n && !changed) {
        int32_t r = (int32_t)(roll(&dice) % (uint32_t)n);
        l_grid[rows[r]] ^= (uint16_t)(1u << (roll(&dice) & 15));     /* stuck: a cell flips */
    }
}

/* MORSE: the word as a stream of units: 1 a dot, 2 a dash (then two silent units), gaps between */
static const char *const morse_az[26] = { ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.." };
static void morse_build(int32_t t)
{
    struct st *s = &st[t];
    const char *w = word_n[P(t, M_MORS, 0)];
    int32_t n = 0;
    for (; *w && n < 120; w++) {
        const char *c = morse_az[*w - 'A'];
        for (; *c && n < 120; c++) {
            s->u.o.stream[n++] = *c == '.' ? 1 : 2;
            if (*c == '-') { s->u.o.stream[n++] = 0; s->u.o.stream[n++] = 0; }
            s->u.o.stream[n++] = 0;                 /* the gap inside a letter */
        }
        s->u.o.stream[n++] = 0; s->u.o.stream[n++] = 0;   /* between letters: 3 in all */
    }
    for (int32_t k = 0; k < 4 && n < 128; k++) s->u.o.stream[n++] = 0;   /* the word's end: 7 */
    s->u.o.len = (uint8_t)n;
    s->u.o.pos = 0;
}

/* WOLFRAM: the next row by the rule, wrapping round */
static void wolf_seed(int32_t t)
{
    uint16_t r = (uint16_t)(sq_stored(t) & 0xffffu);
    st[t].u.w.row = r ? r : 0x0100;                 /* the pattern, or one cell in the middle */
}
static void wolf_step(int32_t t)
{
    struct st *s = &st[t];
    uint32_t rule = wolf_rule[P(t, M_WOLF, 0)], r = s->u.w.row, out = 0;
    for (int32_t c = 0; c < 16; c++) {
        uint32_t l = (r >> ((c + 15) & 15)) & 1u, m = (r >> c) & 1u, rr = (r >> ((c + 1) & 15)) & 1u;
        if ((rule >> (l << 2 | m << 1 | rr)) & 1u) out |= 1u << c;
    }
    s->u.w.hist[s->u.w.hpos % 6] = (uint16_t)r;
    s->u.w.hpos++;
    s->u.w.row = (uint16_t)(out ? out : 0x0100);
}

/* a track's sequencer starts (or changes): its state from clean */
static void start_mode(int32_t t, int32_t m)
{
    struct st *s = &st[t];
    mode[t] = M_OFF;                                /* the interrupt keeps away while we set it up */
    uint8_t *b = (uint8_t *)&s->u;
    for (uint32_t i = 0; i < sizeof s->u; i++) b[i] = 0;
    s->parity_seen = 0xffffffffu;
    if (m == M_TURN) s->u.t.reg = (uint16_t)(0xa5c3u ^ (uint32_t)t * 0x1f3du);
    if (m == M_MRKV) s->u.m.prev_in = s->u.m.prev_out = -1;
    if (m == M_LIFE) life_seed(t);
    if (m == M_WOLF) wolf_seed(t);
    if (m == M_MORS) morse_build(t);
    mode[t] = (int8_t)m;
    if (m == M_EROS) eros_restore(t);
    if (m == M_DRNK) { drunk_fill(t, 0); drunk_fill(t, 1); }
    rearm |= 1u << t;
}

/* YES: the sequencer's action */
static void action(int32_t t)
{
    switch (mode[t]) {
    case M_CORR: case M_MRKV: start_mode(t, mode[t]); break;          /* reset drift / forget */
    case M_TURN: st[t].u.t.reg = (uint16_t)(st[t].u.t.reg * 40503u + 0x6b43u); break;
    case M_DRNK: drunk_fill(t, 0); drunk_fill(t, 1); break;
    case M_LIFE: for (int32_t k = 0; k < NT; k++) if (mode[k] == M_LIFE) life_seed(k); break;
    case M_EROS: eros_restore(t); break;
    case M_CANN: case M_BERN: start_mode(t, mode[t]); break;
    case M_WOLF: wolf_seed(t); break;
    case M_MATH: st[t].u.x.pos = 0; break;
    case M_MORS: st[t].u.o.pos = 0; break;
    }
    rearm |= 1u << t;
}

static void loop_work(int32_t t, uint32_t par)
{
    switch (mode[t]) {
    case M_CORR: corrupt_loop(t); break;
    case M_DRNK: drunk_fill(t, par ^ 1u); break;   /* the next loop's path */
    case M_EROS: eros_loop(t); break;
    case M_WOLF: if (++st[t].u.w.loops >= P(t, M_WOLF, 1)) { st[t].u.w.loops = 0; wolf_step(t); } break;
    case M_GRID: st[t].u.g.pert = (uint8_t)(((roll(&dice) & 255u) * (uint32_t)(P(t, M_GRID, 4) >> 1)) >> 8); break;
    case M_LIFE: {
        int32_t first = 0;
        while (first < NT && mode[first] != M_LIFE) first++;
        if (t == first && ++l_loops >= P(t, M_LIFE, 1)) {
            l_loops = 0;
            life_step(P(t, M_LIFE, 0));
        }
        break;
    }
    }
}

/* ---- saving --------------------------------------------------------------------------------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define MAGIC     0x46515351u                       /* "FQSQ" */
#define OLD_MAGIC 0x53554253u                       /* "SUBS": a SUBSEQ save */
/* 0.2: two blocks (the settings outgrew one): SAVE_BLOCK then SAVE_BLOCK2. 0.1 saved one, with 30 a track */
struct save { uint32_t magic; uint8_t version; int8_t glob[GLOBALS]; int8_t mode[NT]; int8_t prm[NT][PER_TRACK];
              uint8_t pad[3]; uint32_t sum; };
struct save_v1 { uint32_t magic; uint8_t version; int8_t glob[GLOBALS]; int8_t mode[NT]; int8_t prm[NT][PER_TRACK_V1];
                 uint8_t pad[3]; uint32_t sum; };
static uint8_t block[1024] __attribute__((aligned(16)));
static int32_t save_wait, loaded, ticks;

static uint32_t checksum(const uint8_t *p, int32_t n)
{
    uint32_t s = 0x5eed1ed5u;
    for (int32_t i = 0; i < n; i++)
        s = (s << 5 | s >> 27) ^ p[i];
    return s;
}
static int8_t clampp(const struct prm *p, int32_t v) { return (int8_t)(v < p->min ? p->min : v > p->max ? p->max : v); }

static void defaults(void)
{
    for (int32_t t = 0; t < NT; t++)
        for (int32_t m = 1; m < MODES; m++)
            for (int32_t i = 0; i < mode_n[m]; i++)
                P(t, m, i) = mode_prm[m][i].def;
}

/* A SUBSEQ save: its POLY, CORRUPT and TURING tracks get those sequencers, its feel the FEEL page's */
static void load_old(const int8_t *c, int32_t rows, int32_t version)
{
    int32_t pt = c[0] - 1, ct = c[9] - 1;
    if (pt >= 0 && pt < NT) {
        for (int i = 0; i < 8; i++) P(pt, M_POLY, i) = clampp(&p_poly[i], c[1 + i]);
        start_mode(pt, M_POLY);
    }
    if (ct >= 0 && ct < NT) {
        P(ct, M_CORR, 0) = clampp(&p_corr[0], c[10]);
        P(ct, M_CORR, 1) = clampp(&p_corr[1], c[11]);
        start_mode(ct, M_CORR);
    }
    for (int i = 0; i < GLOBALS; i++) glob[i] = clampp(&p_feel[i], c[13 + i]);
    if (version >= 2 && rows >= 24) {
        int32_t tt = c[17] - 1;
        if (tt >= 0 && tt < NT) {
            for (int i = 0; i < 6; i++) P(tt, M_TURN, i) = clampp(&p_turn[i], c[18 + i]);
            start_mode(tt, M_TURN);
        }
    }
}

static void load_new(const int8_t *g, const int8_t *md, const int8_t *pr, int32_t per)
{
    for (int i = 0; i < GLOBALS; i++) glob[i] = clampp(&p_feel[i], g[i]);
    for (int32_t t = 0; t < NT; t++) {
        for (int32_t m = 1; m < MODES; m++)
            for (int32_t i = 0; i < mode_n[m]; i++)
                if (mode_off[m] + i < per)
                    P(t, m, i) = clampp(&mode_prm[m][i], pr[t * per + mode_off[m] + i]);
        if (md[t] > M_OFF && md[t] < MODES)
            start_mode(t, md[t]);
    }
}

static void load(void)
{
    struct save *s = (struct save *)block;
    struct save_v1 *s1 = (struct save_v1 *)block;
    for (int32_t i = 0; i < 1024; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0)
        return;
    if (s->magic == MAGIC && s->version == 2) {
        if (EMMC_READ(SAVE_BLOCK2, 512, block + 512) != 0 ||
            s->sum != checksum(block, (int32_t)((uint8_t *)&s->sum - block)))
            return;
        load_new(s->glob, s->mode, &s->prm[0][0], PER_TRACK);
    } else if (s1->magic == MAGIC && s1->version == 1 &&
               s1->sum == checksum(block, (int32_t)((uint8_t *)&s1->sum - block))) {
        load_new(s1->glob, s1->mode, &s1->prm[0][0], PER_TRACK_V1);
    } else if (s->magic == OLD_MAGIC) {             /* SUBSEQ 0.6 (17 rows) or 0.7 (24 rows) */
        const int8_t *c = (const int8_t *)block + 5;
        int32_t v = block[4], rows = v == 2 ? 24 : 17;
        int32_t at = (5 + rows + (v == 2 ? 1 : 2) + 3) & ~3;
        uint32_t sum = *(const uint32_t *)(block + at);
        if ((v == 1 || v == 2) && sum == checksum(block, at))
            load_old(c, rows, v);
    }
}

static void save(void)
{
    struct save *s = (struct save *)block;
    for (int32_t i = 0; i < 1024; i++) block[i] = 0;
    s->magic = MAGIC;
    s->version = 2;
    for (int i = 0; i < GLOBALS; i++) s->glob[i] = glob[i];
    for (int32_t t = 0; t < NT; t++) {
        s->mode[t] = mode[t];
        for (int32_t i = 0; i < PER_TRACK; i++) s->prm[t][i] = prm[t][i];
    }
    s->sum = checksum(block, (int32_t)((uint8_t *)&s->sum - block));
    EMMC_WRITE(SAVE_BLOCK, 512, block);
    EMMC_WRITE(SAVE_BLOCK2, 512, block + 512);
}

/* ---- the page ---------------------------------------------------------------------------------------------- */
typedef uint32_t (*fn0)(void);
typedef uint32_t (*fn1)(uint32_t);
#define APP()          (((fn0)0x40138882)())
#define PROJECT(a)     (((fn1)0x40014d86)(a))
#define ACTIVE(p)      (((fn1)0x4001d24e)(p))
#define HELD()         ((const uint8_t *)((fn0)0x40138928)())

#define K_FUNC 1
#define K_TRK  2
#define K_YES  12
#define K_NO   13
#define K_UP   14
#define K_DOWN 15
#define K_LEFT 16
#define K_RIGHT 17
#define K_PAGE 18
#define F_DOWN 0x01
#define F_REPEAT 0x08
#define F_RELEASE 0x10
#define KNOB_CLICK 4                                /* small turns per value step */

static int32_t ui_open, feel, trk_down, swallow_page, swallow_close;
static int32_t hl_k = -1, hl_ticks;                /* the knob just turned: its cell lit for a moment */

static int32_t active(void) { int32_t t = (int32_t)ACTIVE(PROJECT(APP())); return t < 0 ? 0 : t >= NT ? NT - 1 : t; }

int32_t ss_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    int32_t down = (fl & F_DOWN) && !(fl & F_RELEASE), press = down && !(fl & F_REPEAT);
    const uint8_t *h;
    (void)brain;
    if (key == K_TRK)
        trk_down = down;
    if (key == K_PAGE && swallow_page) {            /* the PAGE that opened or closed us */
        if (!down) swallow_page = 0;
        return 1;
    }
    if (key == K_PAGE && press && trk_down) {
        h = HELD();
        if (!h || !h[352]) {                        /* no trig held: that is MULTITRIG's */
            ui_open = !ui_open;
            swallow_page = 1;
            return 1;
        }
    }
    if (!ui_open)
        return 0;
    if (key == K_NO && swallow_close) {
        if (!down) swallow_close = 0;
        return 1;
    }
    int32_t t = active();
    switch (key) {
    case K_NO:
        if (press) { ui_open = 0; swallow_close = 1; }
        return 1;
    case K_LEFT: case K_RIGHT:
        if (trk_down)
            return 0;                               /* TRK + LEFT / RIGHT is LED colours' */
        if (press && !feel) {
            start_mode(t, (mode[t] + (key == K_RIGHT ? 1 : MODES - 1)) % MODES);
            save_wait = 60;
        }
        return 1;
    case K_UP: case K_DOWN:
        if (trk_down)
            return 0;                               /* TRK + UP / DOWN is track solo's */
        if (press) feel = key == K_DOWN;
        return 1;
    case K_YES:
        if (trk_down)
            return 0;                               /* TRK + YES is CTRL ALL's */
        if (press && !feel) action(t);
        return 1;
    }
    return 0;
}

static void set_value(int32_t t, int32_t k, int32_t dir)
{
    const struct prm *p;
    volatile int8_t *v;
    if (feel) {
        if (k >= GLOBALS) return;
        p = &p_feel[k]; v = &glob[k];
    } else {
        int32_t m = mode[t];
        if (k >= mode_n[m]) return;
        p = &mode_prm[m][k]; v = &prm[t][mode_off[m] + k];
        if (m == M_EROS && k == 1) {                /* fewer SOFT levels: none above them */
            int8_t nv = clampp(p, *v + dir * p->step);
            for (int32_t s = 0; s < SQ_STEPS; s++)
                if (st[t].u.e.level[s] > nv || st[t].u.e.level[s] == *v) st[t].u.e.level[s] = (uint8_t)nv;
        }
    }
    int8_t nv = clampp(p, *v + dir * p->step);
    hl_k = k;
    hl_ticks = 40;
    if (nv == *v) return;
    *v = nv;
    if (!feel && mode[t] == M_MORS) morse_build(t);
    save_wait = 60;
    rearm |= 1u << t;
}

int32_t ss_enc(void *brain, void *ev)
{
    int32_t enc = *(int32_t *)((uint8_t *)ev + 12), delta = *(int32_t *)((uint8_t *)ev + 16);
    static int32_t acc[9];
    (void)brain;
    if (!ui_open)
        return 0;
    if (enc >= 1 && enc <= 8 && delta) {
        if ((acc[enc] > 0) != (delta > 0))
            acc[enc] = 0;                           /* a change of direction starts afresh */
        acc[enc] += delta;
        while (acc[enc] >= KNOB_CLICK || acc[enc] <= -KNOB_CLICK) {
            int32_t dir = acc[enc] > 0 ? 1 : -1;
            acc[enc] -= dir * KNOB_CLICK;
            set_value(active(), enc - 1, dir);
        }
    }
    return 1;                                       /* every knob is ours while the page shows */
}

/* ---- drawing (y = 0 is the bottom row; 128 x 64) ---- */
static void text(void *bmp, int32_t x, int32_t y, const char *s) { fw_textf(bmp, fw_font5, x, y, 24, "%s", s); }

static void fmt(char *out, const struct prm *p, int32_t v)
{
    char *q = out;
    if (p->fmt == F_NAME) {
        const char *s = p->names[v];
        int i = 0;
        while (i < 3 && (*q++ = s[i])) i++;
        *q = 0;
        return;
    }
    if (p->fmt == F_DIV) {
        if (!v) { out[0] = '-'; out[1] = 0; return; }
        *q++ = '/';
    }
    if (p->fmt == F_SGN && v > 0) *q++ = '+';
    if (v < 0) { *q++ = '-'; v = -v; }
    if (v >= 100) { *q++ = (char)('0' + v / 100); v %= 100; *q++ = (char)('0' + v / 10); }
    else if (v >= 10) *q++ = (char)('0' + v / 10);
    *q++ = (char)('0' + v % 10);
    *q = 0;
}

static int32_t tlen(const char *s) { int32_t n = 0; while (s[n]) n++; return n; }
static void centred(void *bmp, int32_t x0, int32_t w, int32_t y, const char *s)
{
    int32_t n = tlen(s), x = x0 + (w - (4 * n - 1)) / 2;
    text(bmp, x < x0 ? x0 : x, y, s);
}

/* the knobs along the bottom: for each of A-H its name in a dark chip over its value; the one just
 * turned has its whole cell dark for a moment */
static void knob_strip(void *bmp, const struct prm *p, const volatile int8_t *v, int32_t n)
{
    char s[8];
    fw_fillrect(bmp, 0, 0, 127, 15, 0);
    for (int32_t k = 0; k < n; k++) {
        centred(bmp, k * 16, 15, 9, p[k].name);
        fw_fillrect(bmp, k * 16, 8, k * 16 + 14, 14, -1);   /* the name: a dark chip, a gap between */
        fmt(s, &p[k], v[k]);
        centred(bmp, k * 16, 15, 1, s);
        if (k == hl_k && hl_ticks > 0)
            fw_fillrect(bmp, k * 16, 0, k * 16 + 14, 7, -1);    /* just turned: the whole cell dark */
        if (k)
            fw_fillrect(bmp, k * 16 - 1, 0, k * 16 - 1, 6, 1);   /* a thin line between the values */
    }
}

/* the title: an inverted bar across the top, the track and the sequencer's name, LEFT/RIGHT arrows */
static void title(void *bmp, int32_t t, const char *what, const char *right)
{
    char line[32], *p = line;
    const char *a;
    *p++ = t < 8 ? 'T' : 'M';
    *p++ = (char)('1' + (t & 7));
    *p++ = ' '; *p++ = ' ';
    a = what; while (*a) *p++ = *a++;
    *p = 0;
    fw_fillrect(bmp, 0, 55, 127, 63, 0);
    text(bmp, 3, 57, line);
    if (right) text(bmp, 127 - 4 * tlen(right) - 2, 57, right);
    fw_fillrect(bmp, 0, 55, 127, 63, -1);
}

static int32_t cur_step(int32_t t)
{
    uint32_t c = sq_clock[t].last;
    return c < SQ_STEPS ? (int32_t)c : -1;
}

static void draw_steps(void *bmp, int32_t t, int32_t y)   /* the 16 steps playing, the current boxed */
{
    int32_t cur = cur_step(t), page = cur >= 0 ? cur / 16 : 0;
    for (int32_t s = 0; s < 16; s++)
        if (page * 16 + s == cur)
            fw_framerect(bmp, 12 + s * 7, y, 12 + s * 7 + 5, y + 36, 1);
}

static void draw_mode(void *bmp, int32_t t)
{
    struct st *s = &st[t];
    int32_t m = mode[t], cur = cur_step(t), page = cur >= 0 ? cur / 16 : 0;
    title(bmp, t, mode_name[m], "< >");
    switch (m) {
    case M_OFF:
        text(bmp, 8, 36, "NO SEQUENCER ON");
        text(bmp, 8, 28, "THIS TRACK:");
        text(bmp, 8, 18, "LEFT/RIGHT PICKS ONE");
        return;
    case M_POLY: {
        int32_t pos = 0;
        for (int32_t k = 0; k < page * 16; k++) pos += poly_fires(t, k);
        for (int32_t r = 0; r < 4; r++) {
            char lb[3] = { 'R', (char)('1' + r), 0 };
            text(bmp, 1, 47 - r * 6, lb);           /* each rhythm's row, named as its knob */
        }
        text(bmp, 1, 22, "S");
        for (int32_t r = 0; r < 4; r++)
            for (int32_t k = 0; k < 16; k++) {
                int32_t n = P(t, M_POLY, r), x = 14 + k * 7, y = 48 - r * 6;
                if (n && (page * 16 + k) % n == 0) fw_fillrect(bmp, x, y, x + 3, y + 3, 1);
                else fw_fillrect(bmp, x + 1, y + 1, x + 1, y + 1, 1);
            }
        for (int32_t k = 0; k < 16; k++)
            if (poly_fires(t, page * 16 + k)) {
                int32_t off = P(t, M_POLY, 4 + (pos & 3)), x = 14 + k * 7, hh = off > 0 ? off / 3 : -off / 3;
                fw_fillrect(bmp, x, 24, x + 3, 24, 1);
                if (off > 0) fw_fillrect(bmp, x + 1, 25, x + 2, 25 + hh, 1);
                if (off < 0) fw_fillrect(bmp, x + 1, 23 - hh, x + 2, 23, 1);
                pos++;
            }
        break;
    }
    case M_CORR:
        fw_fillrect(bmp, 12, 40, 125, 40, 1);
        text(bmp, 1, 38, "NT");
        text(bmp, 1, 21, "VL");
        for (int32_t k = 0; k < 16; k++) {
            int32_t x = 14 + k * 7, step = page * 16 + k, dn = s->u.c.note[step] * 3 / 2, dv = s->u.c.vel[step] / 2;
            if (dn > 0) fw_fillrect(bmp, x, 41, x + 3, 41 + dn, 1);
            if (dn < 0) fw_fillrect(bmp, x, 39 + dn, x + 3, 39, 1);
            fw_fillrect(bmp, x + 1, 22 + dv, x + 2, 22 + dv, 1);
        }
        text(bmp, 80, 8, "YES:RESET");
        break;
    case M_TURN: {
        int32_t len = P(t, M_TURN, 1);
        uint32_t r = s->u.t.reg;
        for (int32_t b = 0; b < 16; b++) {
            int32_t x = 2 + b * 8;
            if ((r >> b) & 1u) fw_fillrect(bmp, x, 44, x + 5, 49, 1);
            else fw_framerect(bmp, x + 2, 46, x + 3, 47, 1);
        }
        fw_framerect(bmp, 0, 42, 2 + len * 8 - 2, 51, 1);
        text(bmp, 1, 27, "OUT");
        for (int32_t i = 0; i < 16; i++) {
            int32_t v = s->u.t.hist[(s->u.t.hpos + i) & 15], x = 14 + i * 7, y = 18 + v * 20 / 255;
            fw_fillrect(bmp, x, y, x + 3, y + 1, 1);
        }
        break;
    }
    case M_DRNK: {
        const uint8_t *tab = s->u.d.tab[sq_clock[t].parity & 1u];
        text(bmp, 1, 46, "POS");
        int32_t len = track_len(t);
        for (int32_t k = 0; k < 16; k++) {          /* where each step's trig comes from */
            int32_t x = 14 + k * 7, v = tab[page * 16 + k], y = 18 + v * 32 / (len > 1 ? len - 1 : 1);
            fw_fillrect(bmp, x, y, x + 3, y + 2, 1);
        }
        break;
    }
    case M_LIFE: {
        int32_t row = 0;
        for (int32_t k = 0; k < NT && row < 8; k++) {
            if (mode[k] != M_LIFE) continue;
            for (int32_t c = 0; c < 16; c++) {
                int32_t x = 14 + c * 7, y = 50 - row * 5;
                if ((l_grid[k] >> c) & 1u) fw_fillrect(bmp, x, y, x + 4, y + 3, 1);
                else fw_fillrect(bmp, x + 2, y + 1, x + 2, y + 1, 1);
            }
            if (k == t) fw_fillrect(bmp, 9, 50 - row * 5, 11, 50 - row * 5 + 3, 1);
            row++;
        }
        break;
    }
    case M_EROS: {
        uint64_t trigs = sq_stored(t);
        int32_t soft = P(t, M_EROS, 1);
        text(bmp, 1, 31, "LV");
        for (int32_t k = 0; k < 16; k++) {
            int32_t step = page * 16 + k, x = 14 + k * 7;
            if (!bit64(trigs, step)) continue;
            int32_t h = s->u.e.level[step] * 24 / soft;
            fw_framerect(bmp, x, 20, x + 4, 46, 1);
            if (h) fw_fillrect(bmp, x, 21, x + 4, 21 + h, 1);
        }
        text(bmp, 80, 8, "YES:BACK");
        break;
    }
    case M_MRKV: {
        int32_t mx = 1;                             /* which note follows which: 12 x 12 */
        for (int32_t a = 0; a < 12; a++) for (int32_t b = 0; b < 12; b++) if (s->u.m.count[a][b] > mx) mx = s->u.m.count[a][b];
        for (int32_t a = 0; a < 12; a++)
            for (int32_t b = 0; b < 12; b++) {
                int32_t v = s->u.m.count[a][b] * 3 / mx, x = 34 + b * 5, y = 50 - a * 3;
                if (v >= 2) fw_fillrect(bmp, x, y, x + 3, y + 1, 1);
                else if (v == 1) fw_fillrect(bmp, x + 1, y, x + 2, y, 1);
            }
        text(bmp, 1, 40, "FROM");
        text(bmp, 1, 18, "TO>");
        text(bmp, 92, 8, "YES:CLR");
        break;
    }
    case M_CANN: {
        char ln[24], *q = ln;
        int32_t src = P(t, M_CANN, 0) - 1, dly = P(t, M_CANN, 1);
        const char *a = "COPIES ";
        while (*a) *q++ = *a++;
        *q++ = src < 8 ? 'T' : 'M'; *q++ = (char)('1' + (src & 7)); *q = 0;
        text(bmp, 4, 46, ln);
        for (int32_t k = 0; k < 16; k++) {          /* the source's hits, and here DLY later */
            int32_t x = 14 + k * 7, from = ((page * 16 + k - dly) % 64 + 64) % 64;
            if (bit_of(s->u.n.hit, page * 16 + k)) fw_fillrect(bmp, x, 34, x + 3, 37, 1);
            if (bit_of(s->u.n.hit, from)) fw_fillrect(bmp, x, 22, x + 3, 25, 1);
        }
        text(bmp, 100, 34, "SRC");
        text(bmp, 100, 22, "OUT");
        break;
    }
    case M_ROUL:
        text(bmp, 4, 40, "EACH TRIG PLAYS A RANDOM");
        text(bmp, 4, 30, "SAMPLE FROM LO TO HI");
        break;
    case M_SCAT: {
        int32_t g = P(t, M_SCAT, 2), n = g ? 1 << g : 32;
        text(bmp, 4, 44, "START POINTS");
        for (int32_t k = 0; k < n; k++) {
            int32_t x0 = 4 + k * 120 / n, x1 = 4 + (k + 1) * 120 / n - 2;
            fw_framerect(bmp, x0, 24, x1 < x0 ? x0 : x1, 34, 1);
        }
        break;
    }
    case M_WOLF:
        for (int32_t r = 0; r < 6; r++) {           /* the last rows, oldest at the top */
            uint16_t row = r == 5 ? s->u.w.row : s->u.w.hist[(s->u.w.hpos + 1 + r) % 6];
            for (int32_t c = 0; c < 16; c++)
                if ((row >> c) & 1u) fw_fillrect(bmp, 14 + c * 7, 50 - r * 5, 12 + c * 7, 53 - r * 5, 1);
        }
        break;
    case M_MATH:
        text(bmp, 1, 45, "TR");
        text(bmp, 1, 25, "VAL");
        for (int32_t k = 0; k < 16; k++) {          /* the next 16 values, and which would play */
            int32_t v = math_value(t, s->u.x.pos + k), x = 14 + k * 7;
            fw_fillrect(bmp, x, 22, x + 3, 22 + v * 2, 1);
            if (math_fires(t, s->u.x.pos + k)) fw_fillrect(bmp, x + 1, 46, x + 2, 48, 1);
        }
        break;
    case M_MORS:
        for (int32_t k = 0; k < s->u.o.len && k < 60; k++) {   /* the word: dots and dashes */
            int32_t x = 4 + (k % 30) * 4, y = k < 30 ? 44 : 32, v = s->u.o.stream[k];
            if (v == 1) fw_fillrect(bmp, x, y, x + 1, y + 1, 1);
            if (v == 2) fw_fillrect(bmp, x, y, x + 9, y + 1, 1);
        }
        break;
    case M_BERN: {
        char ln[24], *q = ln;
        int32_t o = P(t, M_BERN, 0) - 1;
        const char *a = "SPLITS WITH ";
        while (*a) *q++ = *a++;
        *q++ = o < 8 ? 'T' : 'M'; *q++ = (char)('1' + (o & 7)); *q = 0;
        text(bmp, 4, 40, ln);
        text(bmp, 4, 30, "PRB: CHANCE IT STAYS");
        break;
    }
    case M_GRID:
        for (int32_t k = 0; k < 32; k++) {          /* the part's 32 steps; accents tall */
            int32_t x = 1 + k * 4, lv = g_level(t, k), on = lv > 255 - P(t, M_GRID, 3) * 2;
            if (on) fw_fillrect(bmp, x, 24, x + 2, lv > 192 ? 44 : 34, 1);
            else fw_fillrect(bmp, x + 1, 24, x + 1, 24, 1);
            if (k == (cur & 31)) fw_framerect(bmp, x - 1, 20, x + 3, 48, 1);
        }
        break;
    }
    if (m != M_GRID && m != M_LIFE) draw_steps(bmp, t, 16);
    knob_strip(bmp, mode_prm[m], &prm[t][mode_off[m]], mode_n[m]);
}

void ss_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (!ui_open)
        return;
    fw_fillrect(bmp, 0, 0, 127, 63, 0);
    int32_t t = active();
    if (feel) {
        title(bmp, t, "FEEL (ALL TRACKS)", "UP");
        text(bmp, 4, 40, "SCL / KEY: MELODIC ONES IN KEY");
        text(bmp, 4, 30, "HUM / GRV: VELOCITY FEEL");
        knob_strip(bmp, p_feel, glob, GLOBALS);
    } else
        draw_mode(bmp, t);
}

void ss_tick(void *ctrl)
{
    if (hl_ticks > 0)
        hl_ticks--;
    if (ui_open)
        *((uint8_t *)ctrl + 0x20) = 1;              /* redraw: the playing step moves */
    if (!loaded && ++ticks > 30) {
        loaded = 1;
        defaults();
        load();
    }
    if (!loaded)
        return;
    if (save_wait > 0 && --save_wait == 0)
        save();
    if (rearm) {
        sq_rearm(rearm);
        rearm = 0;
    }
    for (int32_t t = 0; t < NT; t++) {
        if (mode[t] == M_OFF) continue;
        if (mode[t] == M_BERN) {                    /* its stored trigs, for the interrupt to read */
            uint64_t m = sq_stored(t);
            st[t].u.b.stored[0] = (uint32_t)m;
            st[t].u.b.stored[1] = (uint32_t)(m >> 32);
        }
        uint32_t par = sq_clock[t].parity;
        if (par != st[t].parity_seen) {             /* the track looped: its once-a-loop work */
            if (st[t].parity_seen != 0xffffffffu)
                loop_work(t, par);
            st[t].parity_seen = par;
        }
    }
}
