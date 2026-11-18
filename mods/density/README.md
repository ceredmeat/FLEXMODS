# DENSITY for the Digitakt MKI

One knob for how busy your whole pattern is. Turn left and trigs drop out, right and ghost notes
fill the gaps. Your pattern itself never changes, so the middle always plays it exactly as
written.

- **Mod:** `density` 0.2, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later) and `seq-events`

---

## Quick start

1. Press **FUNC + LFO** to open the master pages, then use **LEFT/RIGHT** to reach **DNS**.
2. Play a pattern and turn knob **A (DNS)**:
   - **left of the middle:** trigs drop out, more and more, down to almost nothing;
   - **middle:** the pattern as written;
   - **right of the middle:** ghost notes fill the empty steps, more and more.

It's made for performing: strip a beat down for a breakdown, then sweep past the middle for a
busy build-up.

---

## The knobs

| Knob | Name | What it does |
|---|---|---|
| A | **DNS** | The density: left thinner, middle as written, right fuller. |
| B | **DWN** | ON/OFF. ON: trigs on steps 1, 5, 9 and 13 (the beats) always play, however far left you go, so the pulse survives. It only protects trigs that are in your pattern. |
| C | **GHV** | How loud the added ghost notes are, compared with the track's own hits. |
| D | **NEW** | ON: a fresh choice of dropped and added steps every loop, so it keeps changing. OFF: the same choice every loop, a steady variation. |
| E | **TRK** | Which tracks it affects: **ALL**, **AUD** (audio only), **MID** (MIDI only) or **NO1** (everything except track 1, so the kick stays steady). |

**CLEAR** on the page (FUNC + held LFO + PLAY) puts DNS back in the middle.

---

## The screen

The page shows the first 16 steps of the eight audio tracks:
- a **dot** is a trig in your pattern;
- a **box** is a trig that's playing now;
- a **small box** is an added ghost note.

---

## Good to know

- **Nothing is written to your pattern.** Turn DNS back to the middle and it's exactly as before.
- **The settings aren't saved.** DENSITY starts in the middle at every power-on.
- Works with the other sequencer mods (FREQSEQ, EUCLID ACCENTS and others) through `seq-events`.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: thinning and filling with DNS, DWN, NEW and TRK.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `density.c`, `mod.json`.
