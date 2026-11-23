/* SPDX-License-Identifier: GPL-2.0-or-later */
/* HELP for the Digitakt mk1 (core 4.2, OS 1.54): a mini manual on a master page (FUNC + LFO). It lists the
 * mods that are in this build (it looks for a piece of each one's code: what's not in the build doesn't
 * show) and shows each one's keys and knobs.
 *
 *   UP / DOWN or any knob    choose a mod, or scroll its card
 *   YES or a trig key        open its card
 *   NO                       back to the list
 *   LEFT / RIGHT             the other master pages
 *
 * The cards are written for this screen: 31 characters a line, in the firmware's font (no commas). */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "../common/mpui.h"

/* a piece of each mod's code, to see whether it's in the build (weak: a missing one is core_zero) */
#define W(s) extern void s(void) __attribute__((weak));
W(kitab_key) W(ctrlall_key) W(lfo_wave_fmt) W(hum_vel) W(acc_vel) W(solo_key) W(ledc_key) W(ms_settings)
W(ru_val_text_s) W(msn_key) W(ma_gen_trig) W(fxl_tick) W(mt_key) W(ut_render) W(sc_key) W(fl_gen_trig)
W(ss_gen_trig) W(zb_render) W(df_render) W(tm_key) W(dn_gen_trig) W(dr_render) W(rv_tick) W(pl_tick)
W(mx_render) W(ge_render) W(gl_render) W(vs_tick) W(gm_tick) W(dc_tick) W(dash_tick) W(pl_new_s)
#undef W

struct card { void (*here)(void); const char *name, *text; };
/* the text: lines split by '\n', 31 characters at most */
static const struct card cards[] = {
    { kitab_key, "KIT A/B",
      "TRK + NO   HEAR THE TEMP-SAVED\n"
      "           KIT (B). AGAIN: BACK\n"
      "           TO YOUR EDITS (A)\n"
      "B SHOWS TOP RIGHT WHILE B PLAYS\n"
      "FUNC+YES / FUNC+NO AS BEFORE" },
    { ctrlall_key, "CTRL ALL",
      "TRK + YES  CTRL ALL ON / OFF\n"
      "ALL SHOWS TOP RIGHT\n"
      "WHILE ON: A LOCK YOU SET WITH A\n"
      "KNOB GOES ON THE SAME STEP OF\n"
      "EVERY TRACK WITH A TRIG THERE" },
    { lfo_wave_fmt, "LFO+",
      "LFO WAVE: NEW WAVES AFTER RND\n"
      "LFO MULT: SLOWER /2 - /16\n"
      "LFO PAGE 3 (PRESS LFO 3 TIMES):\n"
      " RESTART ON ANOTHER TRACK'S\n"
      " TRIGS / ENV FOLLOWER SETTINGS\n"
      "SETTINGS: RND LOOP/LOCK/TRIG" },
    { hum_vel, "HUMANISE",
      "SETTINGS > MOD SETTINGS:\n"
      "HUMAN T1-T8   RANDOM VELOCITY\n"
      "HUMAN MIDI    PER TRACK 2-32\n"
      "THE PATTERN NEVER CHANGES" },
    { acc_vel, "EUCLID ACCENTS",
      "SETTINGS > MOD SETTINGS:\n"
      "ACC T1-T8   ACCENTS 1-16 SPREAD\n"
      "            OVER THE TRACK\n"
      "ACC DEPTH   HOW MUCH LOUDER" },
    { solo_key, "TRACK SOLO",
      "TRK + HOLD UP   SOLO THE TRACK\n"
      "                WHILE HELD\n"
      "TRK + DOWN      LATCH THE SOLO\n"
      "                (AGAIN: OFF)\n"
      "S AND THE TRACK SHOW TOP RIGHT" },
    { ledc_key, "LED COLORS",
      "SETTINGS > LED COLORS:\n"
      " THEME / EDIT / RED GREEN BLUE\n"
      " LED IDLE: LIGHT SHOW AFTER\n"
      " OFF 30S 1M 2M 5M / LED SHOW\n"
      "TRK + LEFT/RIGHT  TRACK COLOUR\n"
      "WHILE RECORDING ONLY RIPPLE" },
    { ms_settings, "MOD SETTINGS",
      "SETTINGS > MOD SETTINGS\n"
      "OPENS / CLOSES THE ROWS OF\n"
      "DEJA VU / HUMANISE / ACCENTS /\n"
      "MIDI ARP" },
    { ru_val_text_s, "REAL UNITS",
      "FILTER FREQ IN HZ\n"
      "ENVELOPE TIMES IN MS / S\n"
      "LFO SPD + MULT POP-UPS SHOW\n"
      "THE CYCLE: BARS / STEPS / HZ" },
    { msn_key, "MUTE SNAPSHOTS",
      "YES + TRIG 1-4   SAVE MUTES\n"
      "NO + TRIG 1-4    RECALL AT THE\n"
      "                 END OF THE BAR\n"
      "SAME KEYS AGAIN: CANCEL\n"
      "M1-M4 SHOWS WHILE IT WAITS" },
    { ma_gen_trig, "MIDI ARP",
      "SETTINGS > MOD SETTINGS:\n"
      "ARP TRACK  M1-M8\n"
      "ARP MODE   OFF UP DOWN UPDN RND\n"
      "ARP RATE   1/16 1/8 1/4\n"
      "ARP OCT    1-3\n"
      "A TRIG'S CHORD IS ARPEGGIATED" },
    { fxl_tick, "FX LOCKS",
      "HOLD A TRIG AND TURN DELAY\n"
      "TIME / WID / FDBK OR REVERB\n"
      "DEC: A LOCK ON THE SHARED FX\n"
      "IT HOLDS TILL THE NEXT TRIG" },
    { mt_key, "MULTI TRIG",
      "GRID RECORDING:\n"
      "HOLD A TRIG + TAP PAGE  PAGE\n"
      "HOLD A TRIG + TAP TRK   TRACK\n"
      "THEN TURN A KNOB: EDITS ALL\n"
      "SETTINGS > MULTI EDIT: REL ABS" },
    { ut_render, "UNDERTONE",
      "MACHINE 35 (SRC PAGE)\n"
      "TWO OSCS WITH SUBHARMONICS\n"
      "/1-/16 + WAVEFOLDER + DRIFT\n"
      "PAGE 2 VIEW: PITCH LADDER" },
    { sc_key, "SCENES",
      "TRK + FUNC   OPEN / CLOSE\n"
      "KNOBS        THE CROSSFADER\n"
      "YES          JUMP A / B\n"
      "LEFT / RIGHT RECORD A / B:\n"
      " TURN KNOBS + TRK + FUNC\n"
      "FUNC+L / R   CLEAR A / B\n"
      "DOWN         SCENE INFO:\n"
      " FUNC + L/R/YES CLEAR ONE\n"
      "NO           CLOSE\n"
      "PAGE KEYS GO STRAIGHT THERE" },
    { fl_gen_trig, "FILL",
      "HOLD YES THEN HOLD NO:\n"
      "THE PATTERN PLAYS A RANDOM FILL\n"
      "ANY KNOB WHILE HELD: HOW WILD\n"
      "LET GO: BACK AS IT WAS" },
    { ss_gen_trig, "FREQSEQ",
      "TRK + PAGE   OPEN / CLOSE\n"
      "PICK A SEQUENCER PER TRACK:\n"
      " POLY CORRUPT TURING DRUNK\n"
      " LIFE EROSION MARKOV GRIDS\n"
      " CANON ROULETTE SCATTER\n"
      " WOLFRAM MATHS MORSE BERNOULLI\n"
      "FEEL PAGE: SCALE / ROOT / SWING\n"
      "PAGE KEYS GO STRAIGHT THERE" },
    { zb_render, "ZIBENS",
      "MACHINE 34 (SRC PAGE)\n"
      "VOIC  KICK LOW SNARE HATS\n"
      "ALGO  THREE PER VOICE\n"
      "PAGE 2: PITCH ENV / DELAY /\n"
      "        COMPRESSOR / VIEW" },
    { df_render, "DJ FILTER",
      "MASTER PAGE (FUNC + LFO)\n"
      "A DJ KNOB / CUTOFF  B RESO\n"
      "C TYPE  D BUZZ  E LFO WAVE\n"
      "F RATE  G DEPTH  H MIX\n"
      "YES  LFO FREE / SYNC\n"
      "HOLD FUNC + LFO: LAST PAGE" },
    { tm_key, "TRIG MODES",
      "NO + PAGE   NEXT TRIG MODE:\n"
      "TRKS  KEYS PLAY TRACKS\n"
      "VEL   16 VELOCITIES\n"
      "RTRG  HOLD: RETRIG\n"
      "RTRK  RETRIG PER TRACK" },
    { dn_gen_trig, "DENSITY",
      "MASTER PAGE: ONE KNOB\n"
      "LEFT  FEWER TRIGS\n"
      "RIGHT GHOST NOTES\n"
      "DWN GHV NEW TRK: OPTIONS" },
    { dr_render, "DRIVE",
      "MASTER PAGE: THE ACTIVE TRACK'S\n"
      "TAPE TUBE FUZZ FOLD + AMOUNT\n"
      "TONE MIX LEVEL\n"
      "LPG: LP / VCA / BOTH + DECAY" },
    { rv_tick, "REVERSE",
      "MASTER PAGE: KNOBS A-H\n"
      "CHANCE TRACKS 1-8 PLAY A TRIG\n"
      "BACKWARDS" },
    { pl_tick, "POLYMETER",
      "MASTER PAGE: TRACK LENGTHS +\n"
      "SPEEDS / SCALE MODE / PRESETS\n"
      "IT EDITS THE PATTERN ITSELF" },
    { mx_render, "SONIC MAX",
      "MASTER PAGE (822A STYLE)\n"
      "PRC PROCESS  LOW LO CONTOUR\n"
      "BBE IN / OUT  OUT LEVEL\n"
      "XOV TOP BAND  PHS PHASE\n"
      "SPD SPEED  SAT SATURATION" },
    { ge_render, "G-EQ",
      "MASTER PAGE: 4 BANDS\n"
      "KNOBS: GAIN + HZ IN PAIRS\n"
      "PUSH LF/HF HZ  BELL / SHELF\n"
      "PUSH MID HZ    Q .7 1.5 3\n"
      "PUSH LF GAIN   HIGH PASS\n"
      "PUSH HMF GAIN  EQ IN / OUT" },
    { gl_render, "GLUE COMP",
      "MASTER PAGE (G BUS STYLE)\n"
      "THR RAT ATK REL  AS ON THE G\n"
      "MKP MAKE-UP  MIX PARALLEL\n"
      "SCF SIDECHAIN HIGH PASS\n"
      "CMP IN / OUT" },
    { vs_tick, "VISUALS",
      "MASTER PAGES:\n"
      "RDR RADAR  PET  SCR EFFECTS\n"
      "SAV SAVER  CLK  SCP SCOPE\n"
      "ART ART / RAIN / STORM\n"
      "STORM FLASHES ONLY STOPPED" },
    { gm_tick, "DIGIARCADE",
      "MASTER PAGE ARC\n"
      "UP/DOWN OR TRIG KEY  PICK\n"
      "YES PLAY   NO BACK" },
    { dc_tick, "DICE",
      "MASTER PAGE DCE PER TRACK\n"
      "YES  ROLL A RHYTHM\n"
      "NO   BACK AS PROGRAMMED\n"
      "AUT  AUTOPILOT EVOLVES IT" },
    { dash_tick, "DASH",
      "MASTER PAGE DSH: THE ACTIVE\n"
      "TRACK'S VALUES AS BARS\n"
      "THE LAST ONE MOVED NAMED BELOW\n"
      "NO  CLEARS THE LIST" },
    { pl_new_s, "PROJECT LINK",
      "MOD SETTINGS GO WITH THE\n"
      "PROJECT: SAVE PROJECT KEEPS\n"
      "THEM / LOADING BRINGS THEM\n"
      "BACK / A NEW ONE: DEFAULTS" },
};
#define NC ((int32_t)(sizeof cards / sizeof cards[0]))

static int32_t idx[NC], n_here, sel, open_card, scroll, turned;

/* Elekloader points a weak name that nothing in the build defines at core_zero (16 zero bytes), not at 0 */
extern const uint8_t core_zero[];
static void find_here(void)
{
    n_here = 0;
    for (int32_t i = 0; i < NC; i++)
        if (cards[i].here && (const void *)cards[i].here != (const void *)core_zero) idx[n_here++] = i;
}
static int32_t line_count(const char *t) { int32_t n = 1; while (*t) if (*t++ == '\n') n++; return n; }

/* ---- the page (y 0 the bottom row; the title bar is 53-63, the core's) ---- */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
static void hp_draw(void *view, void *bmp)
{
    char line[40];
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    if (!n_here) find_here();
    if (!open_card) {
        int32_t rows = 6, top = sel - rows / 2;
        if (top > n_here - rows) top = n_here - rows;
        if (top < 0) top = 0;
        for (int32_t r = 0; r < rows && top + r < n_here; r++) {
            int32_t y = 45 - r * 7, i = top + r;
            char *p = ui_num(line, i + 1);
            *p++ = ' ';
            for (const char *s = cards[idx[i]].name; *s; ) *p++ = *s++;
            *p = 0;
            ui_text(bmp, 2, y, line);
            if (i == sel) fw_fillrect(bmp, 0, y - 1, 127, y + 5, -1);
        }
        ui_text(bmp, 2, 1, "YES OPEN  UP/DOWN PICK");
        return;
    }
    /* a card: its name, then its lines, scrolled */
    const struct card *c = &cards[idx[sel]];
    ui_text(bmp, 2, 46, c->name);
    fw_fillrect(bmp, 0, 43, 127, 43, 1);
    const char *t = c->text;
    int32_t ln = 0, shown = 0;
    while (*t && shown < 6) {
        char *p = line;
        while (*t && *t != '\n' && p < line + 32) *p++ = *t++;
        *p = 0;
        if (*t == '\n') t++;
        if (ln++ < scroll) continue;
        ui_text(bmp, 1, 36 - shown * 7, line);
        shown++;
    }
    if (line_count(c->text) > 6) {                       /* more below or above: a small arrow */
        if (scroll + 6 < line_count(c->text)) fw_fillrect(bmp, 124, 0, 126, 1, 1);   /* more below */
        if (scroll > 0) fw_fillrect(bmp, 124, 40, 126, 41, 1);                          /* more above */
    }
}

static void move(void *view, int32_t dir)
{
    if (!open_card) sel = ui_clamp(sel + dir, 0, n_here - 1);
    else scroll = ui_clamp(scroll + dir, 0, ui_clamp(line_count(cards[idx[sel]].text) - 6, 0, 99));
    fw_invalidate(view);
}

static int32_t hp_key(void *view, void *event)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    int32_t press = (fl & 1) && !(fl & 0x10);
    if (!n_here) find_here();
    if (key == 14 || key == 15) { if (press) move(view, key == 14 ? -1 : 1); return 1; }   /* UP / DOWN */
    if (key == 12) { if (press && !(fl & 8) && n_here) { open_card = 1; scroll = 0; fw_invalidate(view); } return 1; }   /* YES */
    if (key == 13) { if (press && open_card) { open_card = 0; fw_invalidate(view); return 1; } return 0; }   /* NO */
    if (key >= 24 && key <= 39) {                          /* a trig key: that mod's card */
        if (press && key - 24 < n_here) { sel = key - 24; open_card = 1; scroll = 0; fw_invalidate(view); }
        return 1;
    }
    return mp_key(view, event);                            /* LEFT / RIGHT: the stock page step */
}

static int32_t hp_enc(void *view, void *event)
{
    int32_t turn = *(int32_t *)((char *)event + 16);
    if ((turned > 0) != (turn > 0)) turned = 0;
    turned += turn;
    while (turned >= 4 || turned <= -4) { int32_t d = turned > 0 ? 1 : -1; turned -= 4 * d; move(view, d); }
    return 1;
}

static void hp_clear(void *view) { open_card = 0; sel = 0; scroll = 0; fw_invalidate(view); }

/* The stock master view keeps UP / DOWN / YES / NO / the trig keys from a page, so they're caught here,
 * before it, while this page shows (as DIGIARCADE does). Whether it shows: the master view was drawn in the
 * last few ticks with this page in it; a redraw now and then keeps that true while it stays on screen. */
struct core_mpage hp_page;
static int32_t shown;
void hp_tick(void *ctrl)
{
    static uint32_t now, draws, drawn_at;
    (void)ctrl;
    now++;
    if (core_mview && core_mview_draws != draws) { draws = core_mview_draws; drawn_at = now; }
    shown = core_mview && now - drawn_at < 12 && core_mpage_shown(core_mview) == &hp_page;
    if (shown && now - drawn_at >= 8) fw_invalidate(core_mview);
}
int32_t hp_ev_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12);
    (void)brain;
    if (!shown || !core_mview) return 0;
    if (key == 14 || key == 15 || key == 12 || (key == 13 && open_card) || (key >= 24 && key <= 39))
        return hp_key(core_mview, ev);
    return 0;
}

/* the last of the master pages */
struct core_mpage hp_page = { 99, "HLP", "Help", hp_draw, hp_key, hp_enc, 0, 0, hp_clear };
