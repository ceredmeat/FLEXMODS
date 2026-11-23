/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Settings that go with the project, through PROJECT LINK (the projlink mod), following the Digitakt's
 * own rules for a project:
 *   - while you work they're kept as now, in the mod's own block (SAVE_BLOCK), like the project's
 *     unsaved state, and back at power-on;
 *   - SAVE PROJECT (or SAVE PROJECT AS) puts a copy with that project's slot;
 *   - loading a project brings back the copy saved with it, or the defaults if it has none;
 *   - a new project starts from the defaults.
 * Without PROJECT LINK in the build it's as before: one set of settings for everything.
 *
 * Include it after the mod's EMMC_READ / EMMC_WRITE and before its load() and save(), which keep using
 * SAVE_BLOCK (and SAVE_BLOCK2) as they did: those become variables here, pointed at the project's copy
 * while one is read or written. The mod defines, before the include:
 *   PJ_BASE   the first block of its area for the projects' copies: PJ_NB blocks for each of the 128 slots
 *   PJ_NB     1 or 2 (SAVE_BLOCK, and SAVE_BLOCK2 if it has one)
 * and provides pj_defaults() (the settings a new project starts with, without saving); and calls
 * pj_tick() from its tick once it has loaded its own settings at power-on. pj_tick() returns 1 when it
 * changed the settings (to redraw a page). */
#ifndef PROJSAVE_H
#define PROJSAVE_H
#include <stdint.h>

extern volatile int32_t proj_slot __attribute__((weak));
extern volatile int32_t proj_gen __attribute__((weak));
extern volatile int32_t proj_saves __attribute__((weak));

enum { PJ_WORK1 = SAVE_BLOCK };
#undef SAVE_BLOCK
static int32_t pj_blk1 = PJ_WORK1;
#define SAVE_BLOCK pj_blk1
#ifdef SAVE_BLOCK2
enum { PJ_WORK2 = SAVE_BLOCK2 };
#undef SAVE_BLOCK2
static int32_t pj_blk2 = PJ_WORK2;
#define SAVE_BLOCK2 pj_blk2
#endif

static void load(void);
static void save(void);
static void pj_defaults(void);

static void pj_use(int32_t slot)                     /* -1: the working copy; 0-127: that project's */
{
    pj_blk1 = slot < 0 ? PJ_WORK1 : PJ_BASE + slot * PJ_NB;
#ifdef PJ_WORK2
    pj_blk2 = slot < 0 ? PJ_WORK2 : PJ_BASE + slot * PJ_NB + 1;
#endif
}

static int32_t pj_tick(void)
{
    static int32_t seen_gen = -1, seen_saves;
    if (!&proj_gen || !&proj_slot || !&proj_saves) return 0;
    if (seen_gen < 0) { seen_gen = proj_gen; seen_saves = proj_saves; return 0; }
    int32_t changed = 0;
    if (proj_saves != seen_saves) {                  /* saved: a copy goes with the project's slot */
        seen_saves = proj_saves;
        int32_t s = proj_slot;
        if (s >= 0 && s < 128) { pj_use(s); save(); pj_use(-1); }
    }
    if (proj_gen != seen_gen) {                      /* a new project, or another loaded */
        seen_gen = proj_gen;
        int32_t s = proj_slot;
        pj_defaults();
        if (s >= 0 && s < 128) { pj_use(s); load(); pj_use(-1); }
        save();                                      /* and that's the working copy now */
        changed = 1;
    }
    return changed;
}
#endif
