# GLUE for the Digitakt MKI

A bus compressor for the whole mix, modelled on the SSL G-series console's centre-section
compressor: the classic "glue" that makes a mix sit together. Not made or endorsed by Solid State
Logic.

- **Mod:** `glue` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later)

---

## Quick start

1. Press **FUNC + LFO**, then **LEFT/RIGHT** to the **GLU** page.
2. Bring **THR** down until the needle moves a few dB on the loud hits.
3. Set **MKP** to bring the level back up, and **CMP** IN / OUT to compare.

---

## The knobs

| Knob | Name | What it does |
|---|---|---|
| A | **THR** | Threshold, −40 to 0 dB. |
| B | **RAT** | Ratio: 2, 4 or 10. |
| C | **ATK** | Attack: .1, .3, 1, 3, 10 or 30 ms, as on the G. |
| D | **REL** | Release: .1, .3, .6 or 1.2 s, or **AUT** (auto), as on the G. |
| E | **MKP** | Make-up gain, up to +15 dB. |
| F | **MIX** | Dry and compressed mixed, for parallel compression. |
| G | **SCF** | A high pass on what it listens to (OFF, 60, 90, 150 Hz), so the kick doesn't pump the mix. |
| H | **CMP** | IN / OUT. |

## The screen

A moving-needle gain reduction meter like the G's, and the compression curve with a dot where the
mix is right now.

---

## Good to know

- **On every output** (main, headphones, USB), in stereo, after the Digitakt's own master
  section. With the other master effects, the order is DJ FILTER, then G-EQ, then
  SONIC MAX, then GLUE.
- **Saved,** and with **PROJECT LINK** in the build the settings go with the project: saved when
  you save the project, back when you load it, and the defaults for a new project.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: compression on the whole mix, the needle meter, AUTO release, MIX, the side-chain high pass, IN / OUT, and the settings saved.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `glue.c`, `mod.json`.
