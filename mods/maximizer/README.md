# SONIC MAX for the Digitakt MKI

An enhancer for the whole mix, modelled on the BBE 822A Sonic Maximizer. It makes the top end
arrive first and turns it up when the music lacks it, for a clearer, punchier mix. Not made or
endorsed by BBE Sound.

- **Mod:** `maximizer` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later) and **`digihealth`**

---

## Quick start

1. Press **FUNC + LFO**, then **LEFT/RIGHT** to the **SMX** page.
2. Turn **PRC** up for more clarity and **LOW** for more low end, as on the 822A.
3. **BBE** switches the effect IN or OUT, to compare.

---

## The knobs

| Knob | Name | What it does |
|---|---|---|
| A | **PRC** | PROCESS, as on the panel: how much the top is brought forward. |
| B | **LOW** | LO CONTOUR, as on the panel: more low end. |
| C | **BBE** | IN / OUT. |
| D | **OUT** | Output level. |
| E | **XOV** | Where the top band starts: 1.2K (like the 822A), 2.4K or 4.8K. |
| F | **PHS** | How much of the phase alignment, 0–200 %. |
| G | **SPD** | How fast the process follows the music. |
| H | **SAT** | A soft saturation. |

## What it does

Like the 822A, it splits the sound in three (below 150 Hz, the middle, the top), holds the lows and
mids back a little so the top arrives first, and turns the top up by how much the music lacks it
right now.

## The screen

The response curve, drawn live: the top shelf moves as the process works, and the three bands'
levels show as meters.

---

## Good to know

- **On every output** (main, headphones, USB), in stereo, after the Digitakt's own master
  section. With the other master effects, the order is DJ FILTER, then G-EQ, then
  SONIC MAX, then GLUE.
- **Needs DIGIHEALTH in the build** (irpina's mod, in the Elekloader shop). DIGIHEALTH's FAST AUDIO
  memory runs this effect's busiest code, so it doesn't slow the Digitakt down. Elekloader adds it
  for you when you pick this mod.
- **Saved,** and with **PROJECT LINK** in the build the settings go with the project: saved when
  you save the project, back when you load it, and the defaults for a new project.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: the enhancer on the whole mix, PRC and LOW, BBE IN / OUT, the live curve and meters, and the settings saved, with DIGIHEALTH in the build.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `maximizer.c`, `mod.json`.
