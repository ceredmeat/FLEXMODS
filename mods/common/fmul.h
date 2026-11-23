/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fm(a, x) = a x / 2^31 for a in Q31 (-1 .. 1), on the ColdFire's EMAC: one multiply-accumulate and a move,
 * instead of the two multiplies and four shifts and adds of a 32-bit split product.
 *
 * Only inside ev_render_master / ev_render_voices: core sets the EMAC there to fractional and saturating
 * (MACSR 0xA0) and clears ACC0 after the handlers. fm_start() clears ACC0 at a handler's start too, in case
 * one before us left something in it. Checked in digiemu with a probe: 0.5 x 1e6 = 500000, -0.5 x 123456 =
 * -61728 (it truncates toward minus infinity, as >> does), 0.99.. x 0.99.. saturates to 0x7ffffffe.
 * On a PC (the test harnesses) the same sum in 64 bits. */
#ifndef FMUL_H
#define FMUL_H
#include <stdint.h>
#if defined(__m68k__) || defined(__mcoldfire__)
static inline int32_t fm(int32_t a, int32_t x)
{
    int32_t r;
    __asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(x) : "cc");
    return r;
}
/* a x + b y, and a x + b y + c z, in one read of the accumulator */
static inline int32_t fm2(int32_t a, int32_t x, int32_t b, int32_t y)
{
    int32_t r;
    __asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
                     : "=d"(r) : "r"(a), "r"(x), "r"(b), "r"(y) : "cc");
    return r;
}
static inline int32_t fm3(int32_t a, int32_t x, int32_t b, int32_t y, int32_t c, int32_t z)
{
    int32_t r;
    __asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmovclr.l %%acc0,%0"
                     : "=d"(r) : "r"(a), "r"(x), "r"(b), "r"(y), "r"(c), "r"(z) : "cc");
    return r;
}
/* two channels side by side, in ACC0 and ACC1, so neither waits on the other's result (a read of an
 * accumulator right after a MAC waits 4 cycles; with the other channel's MACs between, it doesn't):
 *   fmx1: r0 = a x0,              r1 = a x1
 *   fmx2: r0 = a x0 + b y0,       r1 = a x1 + b y1
 *   fmx3: r0 = a x0 + b y0 + c z0, r1 = a x1 + b y1 + c z1 */
#define fmx1(a, x0, x1, r0, r1) \
    __asm__ volatile("mac.l %2,%3,%%acc0\n\tmac.l %2,%4,%%acc1\n\tmovclr.l %%acc0,%0\n\tmovclr.l %%acc1,%1" \
                     : "=&d"(r0), "=&d"(r1) : "r"(a), "r"(x0), "r"(x1) : "cc")
#define fmx2(a, b, x0, y0, x1, y1, r0, r1) \
    __asm__ volatile("mac.l %2,%4,%%acc0\n\tmac.l %2,%6,%%acc1\n\tmac.l %3,%5,%%acc0\n\tmac.l %3,%7,%%acc1\n\t" \
                     "movclr.l %%acc0,%0\n\tmovclr.l %%acc1,%1" \
                     : "=&d"(r0), "=&d"(r1) : "r"(a), "r"(b), "r"(x0), "r"(y0), "r"(x1), "r"(y1) : "cc")
#define fmx3(a, b, c, x0, y0, z0, x1, y1, z1, r0, r1) \
    __asm__ volatile("mac.l %2,%5,%%acc0\n\tmac.l %2,%8,%%acc1\n\tmac.l %3,%6,%%acc0\n\tmac.l %3,%9,%%acc1\n\t" \
                     "mac.l %4,%7,%%acc0\n\tmac.l %4,%10,%%acc1\n\tmovclr.l %%acc0,%0\n\tmovclr.l %%acc1,%1" \
                     : "=&d"(r0), "=&d"(r1) : "r"(a), "r"(b), "r"(c), "r"(x0), "r"(y0), "r"(z0), "r"(x1), "r"(y1), "r"(z1) : "cc")
static inline void fm_start(void)
{
    int32_t r, q;
    __asm__ volatile("movclr.l %%acc0,%0\n\tmovclr.l %%acc1,%1" : "=d"(r), "=d"(q) :: "cc");
    (void)r; (void)q;
}
#else
static inline int32_t fm(int32_t a, int32_t x) { return (int32_t)(((int64_t)a * x) >> 31); }
static inline int32_t fm2(int32_t a, int32_t x, int32_t b, int32_t y) { return (int32_t)(((int64_t)a * x + (int64_t)b * y) >> 31); }
static inline int32_t fm3(int32_t a, int32_t x, int32_t b, int32_t y, int32_t c, int32_t z)
{ return (int32_t)(((int64_t)a * x + (int64_t)b * y + (int64_t)c * z) >> 31); }
static inline void fm_start(void) {}
#define fmx1(a, x0, x1, r0, r1) do { r0 = fm(a, x0); r1 = fm(a, x1); } while (0)
#define fmx2(a, b, x0, y0, x1, y1, r0, r1) do { r0 = fm2(a, x0, b, y0); r1 = fm2(a, x1, b, y1); } while (0)
#define fmx3(a, b, c, x0, y0, z0, x1, y1, z1, r0, r1) do { r0 = fm3(a, x0, b, y0, c, z0); r1 = fm3(a, x1, b, y1, c, z1); } while (0)
#endif
/* the hottest audio code goes in the 2304 bytes of fast SRAM (.fast, copied there by core at the first
 * tick), where it runs without waiting on the DDR or fighting the UI for the instruction cache */
#if defined(__m68k__) || defined(__mcoldfire__)
#define FAST_CODE __attribute__((section(".fast"), noinline))
#else
#define FAST_CODE
#endif
/* FAST_PAIR(name, args, call): a hot function twice, from one body: name_fast in the fast SRAM and name_ram
 * in RAM, the same code. fast_check() compares the SRAM copy with the RAM one (the first 48 bytes; the code
 * has no PC-relative references, so they're byte for byte the same) and gives the one to call: the fast one
 * only while it's really there (the copy comes from DIGIHEALTH's fa_copies at its first tick; if FAST AUDIO
 * is off, or the SRAM gets overwritten, the RAM one is used). Call it from the tick, now and then. */
static inline int32_t fast_same(const void *a, const void *b)
{
    const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
    for (int32_t i = 0; i < 12; i++) if (x[i] != y[i]) return 0;
    return 1;
}
#if defined(__m68k__) || defined(__mcoldfire__)
#define FAST_PICK(fast, ram) (fast_same((const void *)(fast), (const void *)(ram)) ? (fast) : (ram))
#else
#define FAST_PICK(fast, ram) (ram)
#endif
/* a Q15 value (up to 32768, 1.0) as Q31 */
#define Q31(q15) ((q15) >= 32768 ? 0x7fffffff : (int32_t)(q15) * 65536)
#endif
