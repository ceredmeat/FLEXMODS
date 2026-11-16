/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mute Snapshots for the Digitakt mk1 (OS 1.54), after the Octatrack's mute scenes.
 *
 *   Hold YES, press trig 1-4   save the 16 tracks' mutes as snapshot 1-4
 *   Hold NO,  press trig 1-4   recall snapshot 1-4: at the end of the bar while the sequencer plays
 *                              (just before the next bar's first step), at once when it is stopped.
 *                              The same keys again before then cancel it.
 * A box in the middle of the screen says what happened; "M1".."M4" in the top-right corner while a
 * recall waits for the bar. Snapshots are kept on the unit and come back at power-on.
 *
 * How it works (OS 1.54, read from its code):
 *   - the trig keys are key events 24-39 (the mute screen's handler, 0x40036xxx, takes key - 24 as
 *     the track), YES 12, NO 13.
 *   - the global mutes are a 16-bit mask in the project's settings: 0x4001d5f2(project, track) reads
 *     a track's bit; 0x4001d636(project, track, muted) sets it and tells the rest of the firmware
 *     (the mute screen, the LEDs, MIDI), as a mute from MIDI does. The project is 0x40014d86(app),
 *     the app 0x40138882().
 *   - the bar: seq-events' ev_fire_step gives every track's step as the sequencer plays it; track 1
 *     reaching the last 16th of a bar (step % 16 == 15, or its last step on a shorter track) arms the recall, and the next UI tick (about
 *     every 33 ms, quicker than a 16th at any tempo the Digitakt plays) sets the mutes, before the
 *     next bar's first step. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"

typedef uint32_t (*fn0)(void);
typedef uint32_t (*fn1)(uint32_t);
typedef int32_t (*mute_get_fn)(uint32_t project, int32_t track);
typedef void (*mute_set_fn)(uint32_t project, int32_t track, int32_t muted);
#define APP()          (((fn0)0x40138882)())
#define PROJECT()      (((fn1)0x40014d86)(APP()))
#define MUTE_GET       ((mute_get_fn)0x4001d5f2)
#define MUTE_SET       ((mute_set_fn)0x4001d636)

typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)

#define E_KEY      12
#define E_FLAGS    16
#define F_DOWN     0x01
#define F_REPEAT   0x08
#define F_RELEASE  0x10
#define IS_UP(f)   (!((f) & F_DOWN) || ((f) & F_RELEASE))
#define K_YES      12
#define K_NO       13
#define K_TRIG1    24
#define SLOTS      4

static uint16_t snap[SLOTS];
static uint8_t used[SLOTS];
static int32_t yes_held, no_held;
static uint32_t swallow;                        /* trig keys whose release we take */
static volatile int32_t pending = -1;           /* the snapshot waiting for the bar, or -1 */
static volatile int32_t bar_end;                /* set by the sequencer at the bar's last 16th */
static volatile int32_t steps_seen;             /* counts fires: stopped when it stops moving */
static int32_t last_seen, idle_ticks;
static const char *msg;
static int32_t msg_slot, msg_ticks, redraw, save_wait;

/* ---- the sequencer, at interrupt level -------------------------------------------------------------- */
int32_t msn_step(int32_t track, int32_t step, int32_t len)
{
    if (track == 0) {
        steps_seen++;
        if (((step & 15) == 15 || step == len - 1) && pending >= 0)
            bar_end = 1;
    }
    return step;
}

/* ---- the mutes --------------------------------------------------------------------------------------- */
static uint16_t read_mutes(void)
{
    uint32_t p = PROJECT();
    uint16_t m = 0;
    int32_t t;
    for (t = 0; t < 16; t++)
        if (MUTE_GET(p, t) & 0xff)
            m |= (uint16_t)(1u << t);
    return m;
}

static void apply(int32_t s)
{
    uint32_t p = PROJECT();
    uint16_t now = read_mutes();
    int32_t t;
    for (t = 0; t < 16; t++) {
        int32_t want = (snap[s] >> t) & 1;
        if (want != ((now >> t) & 1))
            MUTE_SET(p, t, want);
    }
}

static void say(const char *m, int32_t slot)
{
    msg = m;
    msg_slot = slot;
    msg_ticks = 30;
    redraw = 1;
}

/* ---- keys (UI task) ---------------------------------------------------------------------------------- */
int32_t msn_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((char *)ev + E_KEY);
    int32_t flags = *(int32_t *)((char *)ev + E_FLAGS);
    int32_t up = IS_UP(flags);
    (void)brain;
    if (key == K_YES) { yes_held = !up; return 0; }
    if (key == K_NO)  { no_held = !up;  return 0; }
    if (key < K_TRIG1 || key >= K_TRIG1 + SLOTS)
        return 0;
    if (swallow & (1u << (key - K_TRIG1))) {      /* the trig we took: its repeats and release */
        if (up)
            swallow &= ~(1u << (key - K_TRIG1));
        return 1;
    }
    if (up || (flags & F_REPEAT) || !(yes_held || no_held))
        return 0;
    {
        int32_t s = key - K_TRIG1;
        swallow |= 1u << s;
        if (yes_held) {
            snap[s] = read_mutes();
            used[s] = 1;
            save_wait = 60;
            say("SAVED", s);
        } else if (!used[s]) {
            say("EMPTY", s);
        } else if (pending == s) {
            pending = -1;
            say("CANCELLED", s);
        } else if (idle_ticks > 15) {            /* stopped: at once */
            apply(s);
            say("RECALLED", s);
        } else {
            bar_end = 0;
            pending = s;
            say("NEXT BAR", s);
        }
    }
    return 1;
}

/* ---- saving ------------------------------------------------------------------------------------------ */
#define MAGIC 0x4d534e50u                       /* "MSNP" */
struct save { uint32_t magic; uint16_t snap[SLOTS]; uint8_t used[SLOTS]; uint32_t sum; };
static uint8_t block[512] __attribute__((aligned(4)));
static int32_t loaded, ticks;

static uint32_t sum_of(const struct save *s)
{
    uint32_t x = s->magic;
    int32_t i;
    for (i = 0; i < SLOTS; i++)
        x = x * 31 + s->snap[i] + ((uint32_t)s->used[i] << 16);
    return x;
}

static void load(void)
{
    struct save *s = (struct save *)block;
    int32_t i;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0 || s->magic != MAGIC || s->sum != sum_of(s))
        return;
    for (i = 0; i < SLOTS; i++) {
        snap[i] = s->snap[i];
        used[i] = s->used[i] ? 1 : 0;
    }
}

static void save(void)
{
    struct save *s = (struct save *)block;
    int32_t i;
    for (i = 0; i < 512; i++)
        block[i] = 0;
    s->magic = MAGIC;
    for (i = 0; i < SLOTS; i++) {
        s->snap[i] = snap[i];
        s->used[i] = used[i];
    }
    s->sum = sum_of(s);
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

/* ---- the UI tick -------------------------------------------------------------------------------------- */
void msn_tick(void *ctrl)
{
    int32_t seen = steps_seen;
    if (!loaded && ++ticks > 30) {
        loaded = 1;
        load();
    }
    if (save_wait > 0 && --save_wait == 0)
        save();
    if (seen != last_seen) {
        last_seen = seen;
        idle_ticks = 0;
    } else if (idle_ticks < 1000) {
        idle_ticks++;
    }
    if (pending >= 0 && (bar_end || idle_ticks > 15)) {   /* the bar's end, or the sequencer stopped */
        int32_t s = pending;
        pending = -1;
        bar_end = 0;
        apply(s);
        redraw = 1;
    }
    if (msg_ticks > 0 && --msg_ticks == 0)
        redraw = 1;
    if (redraw) {
        redraw = 0;
        *((uint8_t *)ctrl + 0x20) = 1;
    }
}

static int32_t text_len(const char *s)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n;
}

void msn_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (pending >= 0) {                         /* M1..M4: a recall waiting for the bar */
        fw_fillrect(bmp, 71, 55, 85, 63, 0);
        fw_framerect(bmp, 71, 55, 85, 63, 1);
        fw_textf(bmp, fw_font5, 73, 57, 4, "M%d", pending + 1);
    }
    if (msg_ticks > 0 && msg) {
        int32_t w = (text_len(msg) + 8) * 4;
        int32_t x0 = 64 - w / 2 - 5, x1 = 64 + w / 2 + 4;
        fw_fillrect(bmp, x0, 24, x1, 38, 0);
        fw_framerect(bmp, x0, 24, x1, 38, 1);
        fw_textf(bmp, fw_font5, 64 - w / 2, 29, 24, "MUTES %d %s", msg_slot + 1, msg);
    }
}
