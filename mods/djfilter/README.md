# DJ FILTER for the Digitakt MKI

A stereo master filter for the whole mix, after the Electrix FilterFactory: one-knob DJ sweeps,
resonance, a BUZZ drive, and an LFO or envelope follower that can sync to the tempo.

- **Mod:** `djfilter` 0.7, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later) and `seq-events` (new in 0.7, for the tempo sync)

---

## Quick start

1. Press **FUNC + LFO** to open the master pages, then use **LEFT/RIGHT** to reach **DJF**.
2. Turn knob **A (DJ)**:
   - **middle:** no filter;
   - **left:** a low pass closing (the highs go);
   - **right:** a high pass opening (the lows go).
3. Turn up **B (RES)** for a sharper, singing sweep.

It filters the whole mix in stereo, on every output: main, headphones and USB.

---

## The knobs

| Knob | Name | What it does |
|---|---|---|
| A | **DJ / FRQ** | In DJ and DJ4 mode, the one-knob sweep described above. In the other types, the cutoff, 20 Hz–20 kHz. |
| B | **RES** | Resonance. |
| C | **TYP** | **DJ**, **DJ4**, **LP**, **LP4**, **HP**, **HP4**, **BP** (band pass), **NCH** (notch). The 4 types are 24 dB (steeper, like the FilterFactory's 4 POLE), the others 12 dB. |
| D | **BUZ** | Drive before the filter, like the FilterFactory's BUZZ, kept at about the same loudness. |
| E | **WAV** | What moves the cutoff: an LFO shaped **TRI**, **SAW**, **SQR** or **RND** (a new random level each cycle), or **ENV** (the mix's own loudness: louder opens it more). |
| F | **SPD** | The LFO's speed, in Hz or in note values (see "Tempo sync"). |
| G | **DEP** | How far the LFO or ENV moves the cutoff. The middle is none; left of the middle moves it the other way. |
| H | **MIX** | Dry to fully filtered. |

**CLEAR** on the page (FUNC + held LFO + PLAY) puts everything back and the filter off.

---

## Tempo sync

**Tap YES** on the DJF page to switch the LFO between **FREE** and **SYNC**. The SPD knob lights up
briefly to show it changed.

- **FREE:** SPD is in Hz, 0.05–20 Hz. The readout at the top ends in "HZ".
- **SYNC:** SPD picks a note value, slow to fast: 8BAR, 4BAR, 2BAR, 1BAR, 1/2D, 1/2, 1/4D, 1/4,
  1/8D, 1/4T, 1/8, 1/8T, 1/16, 16T, 1/32 (D = dotted, T = triplet).

In SYNC:
- the tempo is measured from **track 1**'s steps, so it follows tempo changes by itself;
- the LFO is kept exactly in place in the bar, counting from PLAY, so a 1BAR sweep always starts on
  beat 1 and never drifts;
- while stopped, it keeps running at the last tempo (120 BPM until something has played).

If track 1 runs at a different speed (for example with the POLYMETER mod), SYNC follows track 1.

Only a YES tapped on its own switches FREE/SYNC. YES combinations, such as FILL's YES then NO, or
MUTESNAP's YES + trig, still work as before.

---

## Good to know

- **It starts off at every power-on.** The settings aren't saved, so you never power up into a
  closed filter.
- **With LED COLORS 0.8 or later**, its HEAT effect follows this filter: cutting lows darkens the
  cool keys, cutting highs the hot ones.
- **Arrow keys:** on any master page, LEFT/RIGHT step through the stock pages and the mod pages.

---

## Changes

| Version | What changed |
|---|---|
| 0.7 | Tempo sync (YES toggles FREE/SYNC). Now needs `seq-events`. |
| 0.6 | Smaller page code (the sound is unchanged). The knobs are shared with other mods, for LED COLORS' HEAT. |
| 0.5 | Dial knobs, a shaded curve, clearer labels, arrow-key page stepping. |

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: all filter types, resonance, BUZZ, the LFO shapes and ENV, the
FREE/SYNC toggle and synced rates, and the LED COLORS HEAT link.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `djfilter.c`, `tables.h`, `mod.json`.
