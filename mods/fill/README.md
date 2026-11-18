# FILL for the Digitakt MKI

Hold a combo and your pattern plays a random variation of itself, different every loop. Let go and
it's back to normal. Your pattern never changes.

- **Mod:** `fill` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later) and `seq-events`

---

## Quick start

1. Play a pattern.
2. **Hold YES, then hold NO** as well. For as long as NO is held, the pattern plays a variation of
   itself, a new one each loop.
3. **Let go of NO** and it plays exactly as written again.

**While you hold the fill, turn any knob** to set how wild it is: AMOUNT, 0–100%. It's saved.

---

## What the variation does

On the audio tracks:
- **Shuffle:** a step plays a neighbouring step's trig instead (one or two steps either side, or
  a beat back).
- **Ghost notes:** some empty steps play softly.
- **Drops:** some trigs are left out, but never the first step of each half bar, so the groove
  keeps its anchor.
- **Accents:** the velocities move up and down.

AMOUNT sets how much of all this happens.

---

## Good to know

- **Nothing is written to your pattern.** It only changes what plays while you hold the fill.
- **A different variation every loop and every time** you start the fill.
- **YES on its own** still does what it always did, and so do YES + a trig (MUTE SNAPSHOTS) and
  YES on the DJ FILTER page (FREE/SYNC). The fill only starts once NO joins YES.
- Works alongside the other sequencer mods (DENSITY, FREQSEQ and others) through `seq-events`.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: the fill while held, a new variation each loop, AMOUNT, and the
pattern back to normal on release.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `fill.c`, `mod.json`.
