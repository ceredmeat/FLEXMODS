# DASH for the Digitakt MKI

Several pages of a track's sound on one screen, to read at a glance: on the unit, or in a screen
dump (DigiWall, Elek View).

- **Mod:** `dash` 0.2, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later)

---

## Quick start

Press **FUNC + LFO**, then **LEFT/RIGHT** to the **DSH** page. It shows the active audio track.

---

## What it shows

- **SRC:** the machine's eight knobs, as bars.
- **LFO:** the LFO's eight knobs.
- **Filter and amp:** their values, as bars.
- **The bottom line:** the last value that moved, named, with its value (a slot finder).

The bar that last moved is marked.

---

## Good to know

- **It only reads.** DASH never changes your sound.

---

## Tested on a unit

Not yet confirmed on a unit: it has been run in an emulator of the Digitakt's firmware only.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `dash.c`, `mod.json`.
