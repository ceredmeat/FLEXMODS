# HELP for the Digitakt MKI

A mini manual for the mods in your build, on a master page: each mod gets a short card with its
keys and knobs.

- **Mod:** `help` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later)

---

## Quick start

1. Press **FUNC + LFO**, then **LEFT/RIGHT** to the **HLP** page.
2. **UP/DOWN** or any knob chooses a mod.
3. **YES** or a trig key opens its card. UP/DOWN scroll it.
4. **NO** goes back to the list.

---

## Good to know

- **Only the mods you built in show:** HELP looks for each mod's code in the build.
- It has cards for the FLEXMODS mods, LED COLORS, FX LOCKS, DigiARCADE, UNDERTONE and ZIBENS.

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

GPL-2.0-or-later. Source: `help.c`, `mod.json`.
