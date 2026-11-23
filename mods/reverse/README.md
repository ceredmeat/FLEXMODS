# REVERSE for the Digitakt MKI

A chance, per track, that a trig plays its sample backwards. Set a track to 30% and roughly one
hit in three flips around, differently every time.

- **Mod:** `reverse` 0.5, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later) and `seq-events`

---

## Quick start

1. Press **FUNC + LFO** to open the master pages, then use **LEFT/RIGHT** to reach **REV**.
2. Knobs **A–H** are tracks **1–8**. Turn one up from OFF to 100%.
3. Play the pattern. Each time a trig on that track comes up reversed, its arrow on the page
   flashes.

---

## How it works

- Each trig on a track rolls the dice. If it comes up, that one hit plays with the track's play
  mode flipped:
  - **forward** plays as **reverse**, and **forward loop** as **reverse loop**;
  - a track you've already set to play backwards flips to **forward** on those hits.
- **Your pattern never changes.** Turn the knob to OFF and everything plays as written.
- **Saved:** the eight chances are saved about 2 seconds after a change (internal storage, eMMC
  block `0x6004b`, a block the stock firmware doesn't use) and come back at power-on.
- **CLEAR** on the page (FUNC + held LFO + PLAY) turns every track back to OFF.

---

## Getting the best sound

A reversed sample plays from its **end** back to its start. Many drum samples have a quiet tail
after the hit, so the reversed hit can arrive a moment late, as a swell.

- **Sounds with a clear attack and some body** work best: snares, claps, cymbals, vocal chops.
- **Lower END** on the track's SRC page to cut off the silent tail. The reversed hit then lands
  closer to the beat.

---

## FAQ

**The arrow flashes but I can't hear a difference.**
The sample may be too short or too symmetrical to sound different backwards. Try a snare or a
cymbal, or lower END.

**Older versions sounded like a delay or echo.**
That was a bug in 0.2 and 0.3: they switched trigs to forward *loop* instead of reverse, so a
one-shot repeated. 0.4 fixes it.

---

## Changes

| Version | What changed |
|---|---|
| 0.5 | With PROJECT LINK in the build, the settings go with the project: saved when you save it, back when you load it, defaults for a new project (not yet confirmed on a unit). The save buffer is borrowed from the Digitakt's spare memory only while loading or saving, which leaves more RAM for your other mods. |
| 0.4 | Fixed: trigs really play backwards. 0.2 switched to forward loop, and 0.3's reverse value was ignored by the firmware. |
| 0.2 | The page, saving and the flashing arrows. |

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: trigs playing backwards at different chances, the flashing
arrows, and settings surviving a power cycle.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `reverse.c`, `mod.json`.
