/* SPDX-License-Identifier: MIT
 * In LFO+ 0.6 (Cere) as WAVE 29-35, after LFO+'s own; from DEJA VU 0.3 for the Digitakt mk1 (OS 1.54): two random-loop LFO waveforms, three chaotic ones
 * (chaos.c) and two that follow another track (follow.c).
 *   DVF (WAVE 7): a free-running loop of random values, one a 1/16 of the LFO's cycle.
 *   DVT (WAVE 8): the same, its read position back at the start on each trig.
 * MODE sets the loop's length (2, 4, 8, 16, 32 steps); SPH its randomness: 0 a new value
 * every step, 64 the loop locked, 127 the same values reshuffled; between, the odds blend.
 * The idea is Mutable Instruments Marbles' Deja Vu, by way of the Digitakt II Deja Vu mod
 * (Kari Gardarsson, MIT) and Bezronczek's Syntakt Firmware Workbench (MIT); this is its own
 * code for the mk1's LFO engine.
 */
#include <stdint.h>

#define PERIOD   0x5265c000u           /* the stock LFO phase's cycle */
#define BINW     (PERIOD / 16u)        /* one step */
#define STATE0   0x421f4e34u           /* voice 0's a2 (track 1, LFO 1); +0x28 a voice */
#define VOICES   16
#define STOCK_RNG_A ((volatile uint32_t *)0x4020df1c)
#define STOCK_RNG_B ((volatile uint32_t *)0x4020df20)

struct dv_voice {
    uint32_t rng;
    uint8_t init, cursor, bin, pad;
    int8_t slot[32];
};

static struct dv_voice dv[VOICES];
static uint32_t dv_salt;

static uint32_t rnd(struct dv_voice *v)
{
    uint32_t x = v->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return v->rng = x;
}

static int8_t level(struct dv_voice *v)
{
    return (int8_t)(rnd(v) >> 24);
}

static uint32_t mix(uint32_t x)
{
    x = (x ^ (x >> 16)) * 0x7feb352du;
    x = (x ^ (x >> 15)) * 0x846ca68bu;
    x ^= x >> 16;
    return x ? x : 0x6d2b79f5u;
}

static uint32_t bin_of(uint32_t phase)
{
    uint32_t b = phase / BINW;
    return b > 15 ? 15 : b;
}

static void step(struct dv_voice *v, uint32_t len, uint32_t sph)
{
    v->cursor = (v->cursor + 1) & (len - 1);
    if (sph < 64) {
        if (rnd(v) % 64u < 64u - sph)
            v->slot[v->cursor] = level(v);
    } else if (sph > 64) {
        if (rnd(v) % 63u < sph - 64u) {
            uint32_t o = rnd(v) & (len - 1);
            int8_t t = v->slot[v->cursor];
            v->slot[v->cursor] = v->slot[o];
            v->slot[o] = t;
        }
    }
}

/* The LFO tick, at the waveform dispatch: a2 the voice's stock state, phase its post-wrap
 * phase, delta this tick's signed phase step, trig the track's trig bit this tick, p the
 * LFO's parameters (8.8 halfwords). -> the waveform's value, signed Q31, as the stock ones. */
int32_t ch_gen(uint32_t idx, int32_t wave, uint32_t phase, int32_t delta, int32_t trig,
               const int16_t *p);
int32_t fol_gen(uint32_t idx, int32_t wave);

int32_t dv_gen(uint32_t a2, uint32_t phase, int32_t delta, int32_t trig, const int16_t *p)
{
    uint32_t off = a2 - STATE0, idx = off / 0x28u;
    if (off % 0x28u || idx >= VOICES)
        return 0;
    struct dv_voice *v = &dv[idx];
    const int8_t *pb = (const int8_t *)p;
    int32_t wave = pb[2] - 22, mode = pb[6];   /* LFO+ 0.6: WAVE 29..35 are 7..13 here */
    if (wave >= 12)
        return fol_gen(idx, wave);
    if (wave >= 9)
        return ch_gen(idx, wave, phase, delta, trig, p);
    uint32_t sph = (uint16_t)p[2] >> 8;
    if (mode < 0) mode = 0;
    if (mode > 4) mode = 4;
    if (sph > 127) sph = 127;
    uint32_t len = 2u << mode;
    uint32_t b = bin_of(phase);

    if (!v->init) {
        dv_salt += 0x9e3779b9u;
        v->rng = mix(*STOCK_RNG_A ^ (*STOCK_RNG_B << 1) ^ dv_salt ^ ((idx + 1) * 0x85ebca6bu));
        for (int i = 0; i < 32; i++)
            v->slot[i] = level(v);
        v->cursor = 0;
        v->bin = (uint8_t)b;
        v->init = 1;
        return (int32_t)v->slot[0] << 24;
    }
    v->cursor &= (uint8_t)(len - 1);
    if (wave == 8 && trig) {
        v->cursor = 0;
        v->bin = (uint8_t)b;
        return (int32_t)v->slot[0] << 24;
    }
    uint32_t steps, mag;
    if (delta >= 0) {
        steps = (b - v->bin) & 15u;
        mag = (uint32_t)delta;
    } else {
        steps = (v->bin - b) & 15u;
        mag = (uint32_t)-delta;
    }
    steps += 16u * (mag / PERIOD);
    if (steps > 64)
        steps = 64;
    v->bin = (uint8_t)b;
    while (steps--)
        step(v, len, sph);
    return (int32_t)v->slot[v->cursor] << 24;
}
