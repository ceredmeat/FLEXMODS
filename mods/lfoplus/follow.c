/* SPDX-License-Identifier: MIT
 * DEJA VU 0.3 for the Digitakt mk1 (OS 1.54): LFOs that follow another track.
 *
 *   AEN (WAVE 12): the source track's own AMP envelope, as its voice plays it (0 .. +1).
 *                  VEL on: scaled by the velocity of the trig that started it.
 *   ENV (WAVE 13): an envelope follower on the source track's sound: the block's peak, times its
 *                  AMP envelope (the blocks are taken before the AMP stage), through GAIN and an
 *                  ATK/REL smoother (0 .. +1).
 *   TRG source (any WAVE, stock ones too): the LFO restarts on another track's trigs instead of
 *                  its own, as if that track's trig were its own (TRG, ONE, HLF, HLD, DVT, the
 *                  chaos waves' TRG...).
 *
 * The sources are set per track and per LFO on the LFO (3/3) page (ui.c). Trigs come from the
 * sequencer's fire (seq-events' ev_fire_vel: a note trig that plays, after its condition), so a
 * muted or silent source still drives them; AEN/ENV come from the render's own per-track state
 * (core: fw_amp_env, fw_voice_vel, fw_track_blocks).
 *
 * Voices: a2 = STATE0 + 0x28 idx; idx = 2 track + 0 for LFO 1, + 1 for LFO 2. The tick does
 * LFO 2 (odd idx) first, then LFO 1, with one trig bit for the track at 0x34(sp).
 */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

#define STATE0   0x421f4e34u
#define VOICES   16

/* the LFO (3/3) page's settings, per track (ui.c writes them, the render reads them) */
struct dv3_cfg {
    uint8_t trg[2];             /* LFO 1, 2: 0 its own track's trigs, 1-8 track 1-8's */
    uint8_t fol[2];             /* LFO 1, 2: AEN/ENV's source, track 0-7 */
    uint8_t atk, rel;           /* ENV's smoother, 0-127 */
    uint8_t vel;                /* AEN scaled by the trig's velocity: 0 off, 1 on */
    uint8_t gain;               /* ENV's gain, 0-127 (x1 .. x16, 32 an octave) */
};
volatile struct dv3_cfg dv3_cfg[8];
volatile uint8_t dv3_any_trg;   /* some LFO takes another track's trigs: the mode hook works */

/* ev_fire_vel: a note trig on an audio track plays; its velocity */
volatile uint32_t dv3_seq[8];
volatile uint8_t dv3_vel[8];

void dv3_on_vel(int32_t track, int32_t step, int32_t *vel)
{
    (void)step;
    if ((uint32_t)track < 8u) {
        dv3_vel[track] = (uint8_t)*vel;
        dv3_seq[track]++;
    }
}

/* the trig bit each voice sees */
static uint32_t seen[VOICES];
static int32_t own[8];

/* From the mode hook, every voice, every tick while dv3_any_trg: the track's own trig bit as
 * the tick left it (cur, at 0x34(sp)), -> the bit this voice should see. */
int32_t dv3_trig(uint32_t a2, int32_t cur)
{
    uint32_t off = a2 - STATE0, idx = off / 0x28u, t, l, src, s;
    if (off % 0x28u || idx >= VOICES)
        return cur;
    t = idx >> 1;
    l = idx & 1u;
    if (l)                      /* LFO 2 runs first: the track's own bit, before we touch it */
        own[t] = cur;
    src = dv3_cfg[t].trg[l];
    if (!src || src > 8u)
        return own[t];
    s = dv3_seq[src - 1];
    if (s != seen[idx]) {
        seen[idx] = s;
        return 1;
    }
    return 0;
}

/* AEN and ENV ------------------------------------------------------------------------------- */
struct fol_voice {
    int32_t env;                /* ENV's smoothed level, Q31 */
};
static struct fol_voice fv[VOICES];

/* The AMP envelope's level -> its amplitude, Q31 0 .. 0x7fffffff. The render (0x40073304):
 * stage 2, the attack, adds to the level from 0 until it wraps to 0x80000000, the peak; HOLD
 * keeps it there; the decay (1) and the release after the gate (0) multiply it toward 0 from
 * below. So the amplitude is |level|, 0x80000000 full. */
static int32_t amp_q31(uint32_t t)
{
    int32_t a = fw_amp_env[3 * t + 1];
    if (a < 0)
        a = a == (int32_t)0x80000000 ? 0x7fffffff : -a;
    return a;
}

/* a * b, Q31 x Q31 -> Q31, both >= 0, no 64-bit arithmetic */
static int32_t qmul(int32_t a, int32_t b)
{
    uint32_t ah = (uint32_t)a >> 16, al = (uint32_t)a & 0xffffu;
    uint32_t bh = (uint32_t)b >> 16, bl = (uint32_t)b & 0xffffu;
    uint32_t r = ah * bh * 2u + ((ah * bl) >> 15) + ((al * bh) >> 15);
    return r > 0x7fffffffu ? 0x7fffffff : (int32_t)r;
}

/* ATK/REL 0-127 -> the one-pole's step, Q16: 0 instant; then a time constant of 2^(v / 10)
 * ticks (the LFO ticks about 1900 times a second): 24 about 3 ms, 64 about 45 ms, 96 about
 * 0.4 s, 127 about 2 s */
static uint32_t coef(uint32_t v)
{
    uint32_t c, r;
    if (!v)
        return 65536u;
    c = 65536u >> (v / 10u);
    r = v % 10u;
    c -= c * r / 20u;                       /* about 2^(-r / 10) between octaves */
    return c ? c : 1u;
}

int32_t fol_gen(uint32_t idx, int32_t wave)
{
    uint32_t t = idx >> 1, l = idx & 1u;
    volatile struct dv3_cfg *c = &dv3_cfg[t];
    uint32_t src = c->fol[l] & 7u;
    struct fol_voice *f = &fv[idx];

    if (wave == 12) {                                        /* AEN */
        int32_t a = amp_q31(src);
        if (c->vel)
            a = qmul(a, (int32_t)((uint32_t)(fw_voice_vel[src] & 0x7fff) << 16) + 0xffff);
        return a;
    } else {                                                 /* ENV */
        const int32_t *b = &fw_track_blocks[32 * src];
        int32_t pk = 0, x;
        uint32_t g = c->gain, m;
        int i;
        for (i = 0; i < 32; i++) {
            x = b[i];
            if (x < 0) x = x == (int32_t)0x80000000 ? 0x7fffffff : -x;
            if (x > pk) pk = x;
        }
        pk = qmul(pk, amp_q31(src));
        m = (256u + (g & 31u) * 8u) << (g >> 5);             /* Q8: x1 .. x~16 */
        if (pk >= (int32_t)(0x7fffffffu / m * 256u))
            pk = 0x7fffffff;
        else
            pk = (int32_t)(((uint32_t)pk >> 8) * m);
        {
            int32_t d = pk - f->env;
            uint32_t k = coef(d > 0 ? c->atk : c->rel);
            int32_t step = (int32_t)(((d >> 16) * (int32_t)k) + (((d & 0xffff) * (int32_t)k) >> 16));
            if (k >= 65536u || step == 0)
                f->env = (k >= 65536u) ? pk : f->env + (d > 0 ? 1 : d < 0 ? -1 : 0);
            else
                f->env += step;
        }
        if (f->env < 0) f->env = 0;
        return f->env;
    }
}
