/* SPDX-License-Identifier: MIT
 * DEJA VU 0.2: three chaotic LFO waveforms for the Digitakt mk1 (OS 1.54).
 *   LOG (WAVE 9):  the logistic map x' = r x (1 - x), a new value each 1/16 of the LFO's cycle.
 *   HEN (WAVE 10): the Henon map x' = 1 - a x^2 + y, y' = 0.3 x, the same rate.
 *   LOR (WAVE 11): the Lorenz attractor's x, integrated smoothly, 256 Euler steps a cycle.
 * SPH is the amount of chaos, one knob from order to chaos:
 *   LOG r 2.80 .. 4.00 (fixed point, period 2, 4, 8 ... chaos from about SPH 64),
 *   HEN a 0.90 .. 1.40 (periodic orbits, then the strange attractor from about SPH 100),
 *   LOR rho 15 .. 45 (a damped spiral that settles, then the butterfly from about SPH 40).
 * MODE: FRE runs free; TRG starts the same orbit again on each trig (one phrase, repeatable);
 * HLD runs free, its value sampled on each trig and held; ONE and HLF start again on each
 * trig and stop after one or half a cycle, holding their last value.
 * Fixed point only, no 64-bit arithmetic (no libgcc): a few multiplies a step. */
#include <stdint.h>

#define PERIOD   0x5265c000u           /* the stock LFO phase's cycle */
#define BIN16    (PERIOD / 16u)        /* LOG, HEN: one iteration */
#define BIN256   (PERIOD / 256u)       /* LOR: one Euler step */
#define VOICES   16
#define STOCK_RNG_A ((volatile uint32_t *)0x4020df1c)
#define STOCK_RNG_B ((volatile uint32_t *)0x4020df20)

enum { M_FRE, M_TRG, M_HLD, M_ONE, M_HLF };

struct ch_voice {
    int32_t x, y, z;                    /* Q16 */
    int32_t out, held;                  /* Q31 */
    uint32_t rng, run;                  /* run: steps since the last restart */
    uint32_t dith;                      /* the LSB dither's generator: fixed on a restart */
    uint8_t init, bin, wave, stopped;
};

static struct ch_voice ch[VOICES];
static uint32_t ch_salt;

static uint32_t mixr(uint32_t x)
{
    x = (x ^ (x >> 16)) * 0x7feb352du;
    x = (x ^ (x >> 15)) * 0x846ca68bu;
    x ^= x >> 16;
    return x ? x : 0x6d2b79f5u;
}

/* 0..3: fixed-point maps fall into short loops; two bits of noise keep a chaotic orbit chaotic
 * and leave a stable or periodic one as it is (it attracts). Reset with the orbit, so a restart
 * plays the same phrase. */
static int32_t dither(struct ch_voice *v)
{
    v->dith = v->dith * 1664525u + 1013904223u;
    return (int32_t)(v->dith >> 30);
}

static uint32_t rnd(struct ch_voice *v)
{
    uint32_t x = v->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return v->rng = x;
}

/* Q16 x Q16 -> Q16, operands to 24 bits each side (|a|, |b| < 128.0) */
static int32_t mul16(int32_t a, int32_t b)
{
    return (a >> 8) * (b >> 8);
}

static int32_t clamp31(int32_t v, int32_t lim)
{
    return v > lim ? lim : v < -lim ? -lim : v;
}

static void iter_log(struct ch_voice *v, uint32_t sph);
static void iter_hen(struct ch_voice *v, uint32_t sph);
static void iter_lor(struct ch_voice *v, uint32_t sph);

static void iterate(struct ch_voice *v, int32_t wave, uint32_t sph)
{
    if (wave == 9) iter_log(v, sph);
    else if (wave == 10) iter_hen(v, sph);
    else iter_lor(v, sph);
}

/* The orbit's start, and its first value: the same each time for a restart, a little apart per
 * voice running free */
static void seed(struct ch_voice *v, int32_t wave, int32_t jitter, uint32_t sph)
{
    int32_t j = jitter ? (int32_t)(rnd(v) >> 20) : 0;       /* 0 .. 0.0625 in Q16 */
    v->dith = jitter ? rnd(v) : 0x2545f491u;
    switch (wave) {
    case 9:  v->x = 0x5a3d + j; v->y = 0; v->z = 0; break;  /* x 0.352 */
    case 10: v->x = 0x1999 + j; v->y = 0x1999; v->z = 0; break;
    default: v->x = 0x10000 + j; v->y = 0x10000; v->z = 20 << 16; break;
    }
    v->run = 0;
    v->stopped = 0;
    iterate(v, wave, sph);
}

static void iter_log(struct ch_voice *v, uint32_t sph)
{
    /* r in Q13: 2.80 .. 4.00 */
    uint32_t r = 22938u + sph * 9830u / 127u;
    uint32_t x = (uint32_t)v->x;
    uint32_t q = (x * (65536u - x)) >> 16;                  /* x (1 - x), Q16, <= 0.25 */
    x = ((q * r) >> 13) + (uint32_t)dither(v);
    if (x >= 65535u || x < 2u)                              /* the map's traps: start again */
        x = 4096u + (rnd(v) >> 18);
    v->x = (int32_t)x;
    v->out = ((int32_t)x - 32768) << 16;
}

static void iter_hen(struct ch_voice *v, uint32_t sph)
{
    /* a in Q16: 0.90 .. 1.40; b 0.3 */
    int32_t a = 58982 + (int32_t)(sph * 32768u / 127u);
    int32_t x = v->x, y = v->y;
    int32_t x2 = ((x >> 4) * (x >> 4)) >> 8;                /* Q16 */
    int32_t nx = 65536 - (((a >> 4) * (x2 >> 4)) >> 8) + y;   /* a x^2, 12-bit operands */
    int32_t ny = (19661 * (x >> 4)) >> 12;
    if (nx > (3 << 16) || nx < -(3 << 16)) {               /* left the basin: start again */
        nx = 0x1999 + (int32_t)(rnd(v) >> 22);
        ny = 0x1999;
    }
    v->x = nx + dither(v);
    v->y = ny;
    v->out = clamp31(nx, 85000) * 25206;                    /* 1.3 -> about full scale */
}

static void iter_lor(struct ch_voice *v, uint32_t sph)
{
    /* sigma 10, beta 8/3, rho 15 .. 45; dt 1/256 */
    int32_t rho = (15 << 16) + (int32_t)(sph * (30u << 16) / 127u);
    int32_t x = v->x, y = v->y, z = v->z;
    int32_t dx = 10 * (y - x);
    int32_t dy = mul16(x, rho - z) - y;
    int32_t dz = mul16(x, y) - mul16(174763, z);
    x += dx >> 8;
    y += dy >> 8;
    z += dz >> 8;
    if (x > (60 << 16) || x < -(60 << 16) || z > (120 << 16) || z < -(10 << 16)) {
        x = 0x10000; y = 0x10000; z = 20 << 16;
    }
    v->x = x; v->y = y; v->z = z;
    v->out = clamp31(x, 24 << 16) * 1365;                   /* +-24 -> about full scale */
}

/* -> the waveform's value, signed Q31; see dv_gen for the arguments */
int32_t ch_gen(uint32_t idx, int32_t wave, uint32_t phase, int32_t delta, int32_t trig,
               const int16_t *p)
{
    struct ch_voice *v = &ch[idx];
    const int8_t *pb = (const int8_t *)p;
    int32_t mode = pb[6];
    uint32_t sph = (uint16_t)p[2] >> 8, binw, b, steps, mag, stop;
    int lor = wave == 11;
    if (sph > 127) sph = 127;
    if (mode < 0) mode = 0;
    if (mode > 4) mode = 4;
    binw = lor ? BIN256 : BIN16;
    b = phase / binw;
    b &= lor ? 255u : 15u;

    if (!v->init || v->wave != (uint8_t)wave) {
        if (!v->init) {
            ch_salt += 0x9e3779b9u;
            v->rng = mixr(*STOCK_RNG_A ^ (*STOCK_RNG_B << 1) ^ ch_salt ^ ((idx + 1) * 0x85ebca6bu));
        }
        v->wave = (uint8_t)wave;
        seed(v, wave, mode == M_FRE || mode == M_HLD, sph);
        v->bin = (uint8_t)b;
        v->init = 1;
        v->held = v->out;
    }

    if (trig) {
        if (mode == M_HLD) {
            v->held = v->out;
        } else if (mode != M_FRE) {
            seed(v, wave, 0, sph);
            v->bin = (uint8_t)b;
        }
    }

    if (delta >= 0) {
        steps = (b - v->bin) & (lor ? 255u : 15u);
        mag = (uint32_t)delta;
    } else {
        steps = (v->bin - b) & (lor ? 255u : 15u);
        mag = (uint32_t)-delta;
    }
    steps += (lor ? 256u : 16u) * (mag / PERIOD);
    v->bin = (uint8_t)b;
    if (steps > (lor ? 24u : 8u))                           /* audio-rate LFOs: a cap a tick */
        steps = lor ? 24u : 8u;

    stop = mode == M_ONE ? (lor ? 256u : 16u) : mode == M_HLF ? (lor ? 128u : 8u) : 0;
    while (steps-- && !v->stopped) {
        iterate(v, wave, sph);
        if (stop && ++v->run >= stop)
            v->stopped = 1;
    }
    return mode == M_HLD ? v->held : v->out;
}
