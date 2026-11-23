# G-EQ for the Digitakt MKI

A four-band EQ for the whole mix, modelled on the SSL G-series channel EQ. Not made or endorsed by
Solid State Logic.

- **Mod:** `geq` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later) and **`digihealth`**

---

## Quick start

1. Press **FUNC + LFO**, then **LEFT/RIGHT** to the **GEQ** page.
2. The knobs come in pairs, gain then frequency, for the four bands from low to high.
3. **Push a knob** for its extra, like the G's buttons (below).

---

## The knobs

| Knobs | Band | Range | Push the knob |
|---|---|---|---|
| A, B | **LF** | 30–450 Hz | A: the high pass (OFF, 30, 60, 120, 240 Hz). B: bell or shelf. |
| C, D | **LMF** | 200 Hz – 2.5 kHz | D: the width (Q .7, 1.5 or 3). |
| E, F | **HMF** | 600 Hz – 7 kHz | E: EQ IN / OUT. F: the width (Q .7, 1.5 or 3). |
| G, H | **HF** | 1.5–16 kHz | H: bell or shelf. |

Each band's gain goes from −15 to +15 dB. The bells are proportional Q, as on the G: the more you
boost or cut, the narrower they get.

## The screen

The EQ curve with each band's number at its handle, and the band you touch, in words.

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

Tested on a Digitakt MKI, OS 1.54: the four bands, bell and shelf, the widths, the high pass, IN / OUT, and the settings saved, with DIGIHEALTH in the build.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `geq.c`, `tables.h`, `mod.json`.
