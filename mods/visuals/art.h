/* SPDX-License-Identifier: GPL-2.0-or-later */
/* VISUALS 0.5 (included by visuals.c): big letters, four art screensavers (a lava lamp, a ferrofluid,
 * the Game of Life and Lissajous figures), and the ART page's overlays: rain and lightning, big track names,
 * the start-up splash, a name tag and the whole screen inverted. */

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* ---- big letters: a 5 x 7 font, each pixel a square of sc x sc; y the bottom ---------------------------- */
static const char big_chars[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-!.:?";
static const uint8_t big_font[42][7] = {
    {0,0,0,0,0,0,0}, {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2},
    {31,16,30,1,1,17,14}, {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}, {14,17,17,31,17,17,17},
    {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14}, {28,18,17,17,17,18,28}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, {14,17,16,23,17,17,15},
    {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31}, {17,27,21,21,17,17,17},
    {17,17,25,21,19,17,17}, {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17}, {17,17,10,4,4,4,4},
    {31,1,2,4,8,16,31}, {0,0,0,31,0,0,0}, {4,4,4,4,4,0,4}, {0,0,0,0,0,12,12}, {0,12,12,0,12,12,0}, {14,17,1,2,4,0,4} };
static int32_t big_w(const char *s, int32_t sc) { return ui_len(s) * 6 * sc - sc; }
static void big_text(void *bmp, int32_t x, int32_t y, const char *s, int32_t sc)
{
    for (; *s; s++, x += 6 * sc) {
        int32_t k = 0;
        while (big_chars[k] && big_chars[k] != *s) k++;
        if (!big_chars[k]) continue;
        for (int32_t r = 0; r < 7; r++) {
            uint8_t row = big_font[k][r];
            for (int32_t c = 0; c < 5; c++)
                if ((row >> (4 - c)) & 1) fw_fillrect(bmp, x + c * sc, y + (6 - r) * sc, x + c * sc + sc - 1, y + (6 - r) * sc + sc - 1, 1);
        }
    }
}

/* ---- how loud the master is (vs_audio keeps the peak) --------------------------------------------------- */
static int32_t lvl;                              /* 0-32767, falling back slowly */
static void lvl_tick(void) { int32_t p = lvl_pk; lvl_pk = 0; lvl = p > lvl ? p : lvl - lvl / 12; }

/* ---- the lava lamp: five blobs drifting, swelling with the music ---------------------------------------- */
static uint32_t lava_t;
static void art_lava(void *bmp, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    int32_t bx[5], by[5], r2[5];
    static const uint8_t sp[5] = { 7, 11, 5, 9, 13 }, ph[5] = { 0, 80, 150, 40, 200 };
    for (int32_t i = 0; i < 5; i++) {
        bx[i] = w / 2 + (w / 2 - 6) * isin((int32_t)(lava_t * sp[i] / 8) + ph[i]) / 127;
        by[i] = h / 2 + (h / 2 - 3) * isin((int32_t)(lava_t * sp[(i + 2) % 5] / 10) + ph[(i + 1) % 5] + 64) / 127;
        int32_t r = h / 9 + 1 + lvl * h / (8 * 32767);
        r2[i] = r * r;
    }
    for (int32_t cy = 0; cy < h; cy += 2) {      /* 2 x 2 cells, drawn as runs */
        int32_t run = -1;
        for (int32_t cx = 0; cx <= w; cx += 2) {
            int32_t on = 0;
            if (cx < w) {
                int32_t f = 0;
                for (int32_t i = 0; i < 5; i++) { int32_t dx = cx - bx[i], dy = cy - by[i]; f += r2[i] * 256 / (dx * dx + dy * dy + 1); }
                on = f > 256;
            }
            if (on && run < 0) run = cx;
            if (!on && run >= 0) { fw_fillrect(bmp, x0 + run, y0 + cy, x0 + cx - 1, y0 + cy + 1, 1); run = -1; }
        }
    }
}

/* ---- the ferrofluid: a pool whose spikes stand up with the sound ---------------------------------------- */
static int32_t fer_len[13];
static void art_ferro(void *bmp, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    int32_t cx = x0 + w / 2, cy = y0 + h / 5, R = h / 5 + 1;
    for (int32_t dy = 0; dy <= R; dy++) {         /* the pool: a half disc on the floor */
        int32_t hw = 0;
        while ((hw + 1) * (hw + 1) + dy * dy <= R * R * 2) hw++;
        fw_fillrect(bmp, cx - hw, cy + dy, cx + hw, cy + dy, 1);
    }
    fw_fillrect(bmp, x0, y0, x0 + w - 1, cy, 1);
    for (int32_t k = 0; k < 13; k++) {
        int32_t a = 16 + k * 8;                   /* the upper half: 16 .. 112 */
        int32_t want = lvl * (h - R - 2) / 32767 * (40 + (int32_t)rnd(60)) / 100;
        fer_len[k] += (want - fer_len[k]) / 3;
        int32_t L = R + 1 + fer_len[k];
        int32_t tx = cx + L * isin(a + 64) / 127 * 3 / 2, ty = cy + L * isin(a) / 127;
        for (int32_t s = -2; s <= 2; s++) {       /* a spike: a fan of lines to its tip */
            int32_t b = a + s * 3, bxx = cx + R * isin(b + 64) / 127 * 3 / 2, byy = cy + R * isin(b) / 127;
            ui_line(bmp, bxx, byy, tx, ty);
        }
    }
}

/* ---- the Game of Life: hits plant new life; it steps on the active track's steps ------------------------ */
#define LFW 64
#define LFH 32
static uint8_t life[LFH][LFW / 8], life_n[LFH][LFW / 8];
static int32_t life_pop, life_quiet;
static int32_t life_get(int32_t x, int32_t y) { x &= LFW - 1; y &= LFH - 1; return (life[y][x >> 3] >> (x & 7)) & 1; }
static void life_put(int32_t x, int32_t y) { x &= LFW - 1; y &= LFH - 1; life[y][x >> 3] |= (uint8_t)(1u << (x & 7)); }
static void life_plant(int32_t t)
{
    int32_t x = t * 8 + (int32_t)rnd(6), y = (int32_t)rnd(LFH);
    if (rnd(2)) { life_put(x + 1, y); life_put(x + 2, y + 1); life_put(x, y + 2); life_put(x + 1, y + 2); life_put(x + 2, y + 2); }   /* a glider */
    else { life_put(x, y); life_put(x + 1, y); life_put(x + 2, y); life_put(x + 1, y + 1); life_put(x, y + 2); }     /* a small burst */
}
static void life_step(void)
{
    int32_t pop = 0;
    for (int32_t y = 0; y < LFH; y++)
        for (int32_t x = 0; x < LFW; x++) {
            int32_t n = 0;
            for (int32_t dy = -1; dy <= 1; dy++) for (int32_t dx = -1; dx <= 1; dx++) if (dx || dy) n += life_get(x + dx, y + dy);
            int32_t on = n == 3 || (n == 2 && life_get(x, y));
            if (on) { life_n[y][x >> 3] |= (uint8_t)(1u << (x & 7)); pop++; }
            else life_n[y][x >> 3] &= (uint8_t)~(1u << (x & 7));
        }
    for (int32_t y = 0; y < LFH; y++) for (int32_t i = 0; i < LFW / 8; i++) life[y][i] = life_n[y][i];
    life_quiet = pop == life_pop ? life_quiet + 1 : 0;
    life_pop = pop;
    if (pop < 6 || life_quiet > 40) { for (int32_t k = 0; k < 6; k++) life_plant((int32_t)rnd(8)); life_quiet = 0; }   /* never quite dies */
}
static void art_life(void *bmp, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    int32_t c = w >= 100 ? 2 : 1;                 /* 2-pixel cells full screen, 1 in a preview */
    for (int32_t y = 0; y < LFH && y * c < h; y++) {
        int32_t run = -1;
        for (int32_t x = 0; x <= LFW && x * c <= w; x++) {
            int32_t on = x < LFW && x * c < w && life_get(x, y);
            if (on && run < 0) run = x;
            if (!on && run >= 0) { fw_fillrect(bmp, x0 + run * c, y0 + y * c, x0 + x * c - 1, y0 + y * c + c - 1, 1); run = -1; }
        }
    }
}

/* ---- Lissajous figures: the master against itself a moment later ---------------------------------------- */
static void art_liss(void *bmp, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    uint32_t at = sc_at;
    int32_t cx = x0 + w / 2, cy = y0 + h / 2, px = 0, py = 0;
    for (int32_t i = 0; i < SCOPE_N - 12; i++) {
        uint32_t j = (at + i) % SCOPE_N, k = (at + i + 12) % SCOPE_N;
        int32_t a = (sc_l[j] + sc_r[j]) / 2, b = (sc_l[k] + sc_r[k]) / 2;
        int32_t x = cx + a * (w / 2 - 1) / 64, y = cy + b * (h / 2 - 1) / 64;
        x = ui_clamp(x, x0, x0 + w - 1); y = ui_clamp(y, y0, y0 + h - 1);
        if (i) ui_line(bmp, px, py, x, y);
        px = x; py = y;
    }
}
static void art_storm(void *bmp, int32_t x0, int32_t y0, int32_t w, int32_t h);
static void art_frame(void *bmp, int32_t mode, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    if (mode == SV_LAVA) art_lava(bmp, x0, y0, w, h);
    else if (mode == SV_FERRO) art_ferro(bmp, x0, y0, w, h);
    else if (mode == SV_LIFE) art_life(bmp, x0, y0, w, h);
    else if (mode == SV_LISS) art_liss(bmp, x0, y0, w, h);
    else if (mode == SV_STORM) art_storm(bmp, x0, y0, w, h);
}

/* ---- rain and lightning --------------------------------------------------------------------------------- */
#define DROPS 28
static uint8_t drop_x[DROPS], drop_y[DROPS], drop_v[DROPS];
static int32_t bolt_t, bolt_x[8];
static int32_t storm_shown;                      /* the STORM art is on screen (a saver, the ART or SAV page) */
static void rain_tick(void)
{
    int32_t heavy = rain_mode == 2 || storm_shown, n = heavy ? DROPS : DROPS / 2;
    for (int32_t i = 0; i < n; i++) {
        int32_t y = drop_y[i] - drop_v[i];
        if (y < 0 || !drop_v[i]) { y = 63 + (int32_t)rnd(20); drop_x[i] = (uint8_t)rnd(128); drop_v[i] = (uint8_t)(2 + rnd(heavy ? 4 : 2)); }
        drop_y[i] = (uint8_t)y;
    }
    if (bolt_t > 0) bolt_t--;
}
/* a strike: close (a bolt, the screen flashes, the keys blaze and fade), far off (the keys glow dimly and fade),
 * or far off twice in quick succession; then at least 2-6 seconds before the next */
#define PULSES 3
static uint32_t lt_next, lt_at[PULSES];
static int8_t lt_peak[PULSES], lt_on[PULSES];
static void pulse(int32_t k, uint32_t at, int32_t peak) { lt_at[k] = at; lt_peak[k] = (int8_t)peak; lt_on[k] = 1; }
static void bolt_strike(void)
{
    uint32_t now = ticks;
    if (playing_now()) return;                    /* only while the sequencer is stopped: no flashes while you play */
    if ((int32_t)(now - lt_next) < 0) return;    /* the sky is still resting */
    lt_next = now + 120 + rnd(240);
    int32_t kind = (int32_t)rnd(100);
    for (int32_t k = 0; k < PULSES; k++) lt_on[k] = 0;
    if (kind < 40) {                              /* close */
        int32_t x = 20 + (int32_t)rnd(88);
        for (int32_t k = 0; k < 8; k++) { x += (int32_t)rnd(13) - 6; bolt_x[k] = ui_clamp(x, 2, 125); }
        bolt_t = 8;
        pulse(0, now, 0);
        if (rnd(2)) pulse(1, now + 5 + rnd(4), 2); /* an afterglow */
    } else if (kind < 75) pulse(0, now, 2 + (int32_t)rnd(2));          /* far off */
    else { pulse(0, now, 2 + (int32_t)rnd(2)); pulse(1, now + 5 + rnd(5), 1 + (int32_t)rnd(3)); }   /* far off, twice */
}
/* the original flash: the screen inverted for a pulse's first frames (a close strike's a little longer) */
static int32_t lt_inv(void)
{
    if (playing_now()) return 0;                  /* the sequencer started: a strike under way stops flashing */
    for (int32_t k = 0; k < PULSES; k++) {
        if (!lt_on[k]) continue;
        int32_t age = (int32_t)(ticks - lt_at[k]);
        if (age >= 0 && age < (lt_peak[k] == 0 ? 3 : 2)) return 1;
    }
    return 0;
}
/* how bright the sky is now: 0 the brightest .. 4 the dimmest, 5 dark; each pulse fades a step every 3 frames */
static int32_t sky(void)
{
    int32_t best = 5;
    if (playing_now()) return best;
    for (int32_t k = 0; k < PULSES; k++) {
        if (!lt_on[k]) continue;
        int32_t age = (int32_t)(ticks - lt_at[k]);
        if (age < 0) continue;
        int32_t lv = lt_peak[k] + age / 3;
        if (lv > 4) { lt_on[k] = 0; continue; }
        if (lv < best) best = lv;
    }
    return best;
}

/* ---- the storm: clouds drifting over hills, slanting rain, and lightning that inverts the sky ----------- */
static void art_storm(void *bmp, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    int32_t top = y0 + h - 1, cl = h / 4 + 2;
    for (int32_t x = 0; x < w; x++) {             /* the cloud bank: two waves sliding at their own speeds */
        int32_t d = cl / 2 + isin(x * 5 + (int32_t)(lava_t / 2)) * cl / 400 + isin(x * 11 - (int32_t)lava_t) * cl / 600;
        fw_fillrect(bmp, x0 + x, top - ui_clamp(d, 1, cl), x0 + x, top, 1);
        if (((x + (int32_t)(lava_t / 3)) & 7) == 0) fw_fillrect(bmp, x0 + x, top - d - 1, x0 + x, top - d - 1, 1);
    }
    for (int32_t x = 0; x < w; x++) {             /* the hills */
        int32_t g = 2 + (isin(x * 3 + 40) + 127) * (h / 6) / 254 + (isin(x * 9) + 127) / 100;
        fw_fillrect(bmp, x0 + x, y0, x0 + x, y0 + g, 1);
    }
    for (int32_t i = 0; i < DROPS; i++) {         /* the rain, slanting in the wind */
        int32_t y = y0 + drop_y[i] * h / 64, x = x0 + (drop_x[i] + (63 - drop_y[i]) / 4) * w / 128;
        if (y < top - cl / 2 && x < x0 + w - 1) ui_line(bmp, x, y, x + 1, y + 2);
    }
    if (bolt_t > 0) for (int32_t k = 0; k < 7; k++)   /* a close strike: the bolt, from the clouds to the hills */
        ui_line(bmp, x0 + bolt_x[k] * w / 128, top - cl / 2 - k * (h - cl / 2) / 7,
                x0 + bolt_x[k + 1] * w / 128, top - cl / 2 - (k + 1) * (h - cl / 2) / 7);
    if (lt_inv()) fw_fillrect(bmp, x0, y0, x0 + w - 1, top, -1);   /* every strike: the sky inverted */
}

/* ---- the overlays, drawn last ----------------------------------------------------------------------------- */
static int32_t name_show, name_t, boot_done;
static void logo_text(char *t);
static void overlays(void *bmp)
{
    if (rain_mode) {
        int32_t n = rain_mode == 2 ? DROPS : DROPS / 2;
        for (int32_t i = 0; i < n; i++) if (drop_y[i] < 64) fw_fillrect(bmp, drop_x[i], drop_y[i], drop_x[i], drop_y[i] + 2, -1);
        if (bolt_t > 0) for (int32_t k = 0; k < 7; k++) ui_line(bmp, bolt_x[k], 63 - k * 9, bolt_x[k + 1], 63 - (k + 1) * 9);
        if (rain_mode == 2 && lt_inv()) fw_fillrect(bmp, 0, 0, 127, 63, -1);  /* the flash: the screen inverted */
    }
    if (name_t > 0 && name_show) {               /* a track's name, big, for a moment */
        char s[10], *p = put(s, name_show > 8 ? "MIDI " : "TRACK ");
        *p++ = (char)('0' + ((name_show - 1) & 7) + 1); *p = 0;
        int32_t w = big_w(s, 2), x = 64 - w / 2;
        fw_fillrect(bmp, x - 3, 21, x + w + 2, 40, 0); fw_framerect(bmp, x - 3, 21, x + w + 2, 40, 1);
        big_text(bmp, x, 24, s, 2);
    }
    if (tag) {                                   /* your word, top right */
        char t[10];
        logo_text(t);
        int32_t w = 4 * ui_len(t) + 2;
        fw_fillrect(bmp, 125 - w, 55, 127, 63, 0);
        fw_framerect(bmp, 125 - w, 55, 127, 63, 1);
        ui_text(bmp, 127 - w, 57, t);
    }
    if (boot_t >= 0 && boot_t < BOOT_LEN) {      /* the start-up splash, after the mods load */
        char t[10];
        fw_fillrect(bmp, 0, 0, 127, 63, 0);
        if (boot_t < 16) {                        /* a picture tube warming up: a line, then opening */
            int32_t hh = boot_t * 2;
            fw_fillrect(bmp, 64 - boot_t * 4, 32 - hh, 63 + boot_t * 4, 32 + hh, 1);
        } else {
            logo_text(t);
            const char *word = tag ? t : "FLEXMODS";
            int32_t sc = big_w(word, 2) <= 124 ? 2 : 1, w = big_w(word, sc);
            int32_t shake = boot_t < 40 && (boot_t & 2) ? 1 : 0;
            big_text(bmp, 64 - w / 2 + shake, 34, word, sc);
            for (int32_t y = 34 + (int32_t)(boot_t & 3); y < 50; y += 4) fw_fillrect(bmp, 0, y, 127, y, 0);   /* scanlines through it */
            ui_centred(bmp, 0, 128, 22, "DIGITAKT MK1  OS 1.54");
            fw_framerect(bmp, 14, 8, 113, 13, 1);
            fw_fillrect(bmp, 16, 10, 16 + (boot_t - 16) * 95 / (BOOT_LEN - 16), 11, 1);   /* loading ... */
        }
    }
    if (invt) fw_fillrect(bmp, 0, 0, 127, 63, -1);
}

/* ---- the ART page: the art live above, the switches below ------------------------------------------------- */
static int32_t art_turned[6], art_hl = -1, art_hl_ticks;
static const char *const art_nm[5] = { "LAV", "FER", "LIF", "LIS", "STM" };
static const char *const art_long[5] = { "LAVA LAMP", "FERROFLUID", "GAME OF LIFE", "LISSAJOUS", "STORM" };
static void art_draw(void *view, void *bmp)
{
    static const char *const rn[3] = { "OFF", "RAN", "STM" }, *const nn[3] = { "OFF", "ACC", "ALL" }, *const oo[2] = { "OFF", "ON" };
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    art_frame(bmp, SV_LAVA + art_mode, 0, 27, 128, 26);
    ui_text(bmp, 2, 46, art_long[art_mode]);
    ui_knob(bmp, 0, "ART", art_nm[art_mode], art_mode, 4, 2, art_hl == 0 && art_hl_ticks > 0);
    ui_knob(bmp, 1, "RAIN", rn[rain_mode], rain_mode, 2, 2, art_hl == 1 && art_hl_ticks > 0);
    ui_knob(bmp, 2, "NAME", nn[names], names, 2, 2, art_hl == 2 && art_hl_ticks > 0);
    ui_knob(bmp, 3, "INV", oo[invt], invt, 1, 2, art_hl == 3 && art_hl_ticks > 0);
    ui_knob(bmp, 4, "BOOT", oo[boot_on], boot_on, 1, 2, art_hl == 4 && art_hl_ticks > 0);
    ui_knob(bmp, 5, "TAG", oo[tag], tag, 1, 2, art_hl == 5 && art_hl_ticks > 0);
    static const char *const help[6] = { "THE ART: ALSO SCREENSAVERS", "RAIN OR A STORM WITH LIGHTNING", "BIG TRACK NAMES ON HITS",
                                         "THE WHOLE SCREEN INVERTED", "A SPLASH WHEN THE MODS START", "YOUR SAV WORD AT THE TOP RIGHT" };
    if (art_hl >= 0 && art_hl_ticks > 0) { fw_fillrect(bmp, 0, 27, 127, 34, 0); ui_text(bmp, 2, 28, help[art_hl]); }
}
static int32_t art_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > 6) return 0;
    int32_t k = knob - 1;
    static const uint8_t mx[6] = { 4, 2, 2, 1, 1, 1 };
    static int32_t *const v[6] = { &art_mode, &rain_mode, &names, &invt, &boot_on, &tag };
    art_hl = k; art_hl_ticks = 90;
    if ((art_turned[k] > 0) != (turn > 0)) art_turned[k] = 0;
    art_turned[k] += turn;
    while (art_turned[k] >= 16 || art_turned[k] <= -16) {
        int32_t d = art_turned[k] > 0 ? 1 : -1;
        art_turned[k] -= d * 16;
        *v[k] = ui_clamp(*v[k] + d, 0, mx[k]);
        if (k == 4 && boot_on) boot_t = 0;        /* turned on: show it now */
    }
    save_wait = 60;
    fw_invalidate(view);
    return 1;
}
static void art_clear(void *view) { rain_mode = names = invt = tag = 0; boot_on = 1; save_wait = 60; fw_invalidate(view); }

/* ---- the trig lights while a screensaver runs (LED COLORS 1.0+ paints them) ------------------------------ */
volatile uint8_t visuals_leds[17];
static void saver_leds(void)
{
    int32_t lit = rain_mode == 2 || storm_shown ? sky() : 5;
    if (!sv_on && lit > 4) { visuals_leds[16] = 0; return; }
    for (int32_t i = 0; i < 16; i++) visuals_leds[i] = 0;
    if (lit <= 4) {                               /* lightning: white, some keys dimmer, fading back to black */
        for (int32_t i = 0; i < 16; i++) {
            int32_t lv = lit + (int32_t)((i * 7 + ticks / 3) % 3 == 0) + (int32_t)(rnd(4) == 0);
            visuals_leds[i] = lv <= 4 ? (uint8_t)(11 | (lv << 4)) : 0;
        }
        visuals_leds[16] = 1;
        return;
    }
    uint32_t t = ticks;
    switch (saver) {
    case SV_STAR: for (int32_t i = 0; i < 16; i++) if (rnd(9) == 0) visuals_leds[i] = (uint8_t)(rnd(2) ? 11 : 7 | (rnd(3) << 4)); break;   /* twinkles */
    case SV_LOGO: visuals_leds[ui_clamp((logo_x >> 4) * 16 / 90, 0, 15)] = (uint8_t)(1 + (logo_hits % 10)); if (logo_flash) for (int32_t i = 0; i < 16; i++) visuals_leds[i] = (uint8_t)(1 + ((i + t) % 10)); break;
    case SV_PIPE: if (pipe_n) visuals_leds[ui_clamp(pipe_x[pipe_n - 1] * 16 / 128, 0, 15)] = 4; break;
    case SV_RAIN: for (int32_t c = 0; c < 16; c++) { int32_t hd = (int8_t)rain_y[c]; if (hd >= 0 && hd < 16) visuals_leds[c] = (uint8_t)(4 | ((hd / 4) << 4)); } break;   /* heads near the bottom */
    case SV_LAVA: for (int32_t i = 0; i < 16; i++) visuals_leds[i] = (uint8_t)((isin((int32_t)(t * 2) + i * 16) > 0 ? 1 : 2) | ((2 + isin((int32_t)t + i * 20) / 64) << 4)); break;
    case SV_FERRO: { int32_t n = lvl * 16 / 20000; for (int32_t i = 0; i < 16; i++) if (iabs(i * 2 - 15) <= n) visuals_leds[i] = 10; } break;
    case SV_LIFE: for (int32_t c = 0; c < 16; c++) { int32_t n = 0; for (int32_t y = 0; y < LFH; y++) for (int32_t x = c * 4; x < c * 4 + 4; x++) n += life_get(x, y); visuals_leds[c] = n ? (uint8_t)(4 | ((n > 12 ? 0 : n > 5 ? 2 : 4) << 4)) : 0; } break;
    case SV_STORM: for (int32_t i = 0; i < DROPS; i++) if (drop_y[i] < 6) visuals_leds[(drop_x[i] >> 3) & 15] = (uint8_t)(8 | ((2 + (i & 1) * 2) << 4)); break;   /* drops landing: dim blue */
    case SV_LISS: { int32_t i = ui_clamp((sc_l[sc_at % SCOPE_N] + 128) * 16 / 256, 0, 15); visuals_leds[i] = 6; } break;
    }
    visuals_leds[16] = 1;
}
