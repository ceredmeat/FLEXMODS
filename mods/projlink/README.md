# PROJECT LINK for the Digitakt MKI

A helper for other mods: it lets them keep their settings with each project. On its own it does
nothing.

- **Mod:** `projlink` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later)

---

## What it does

It notes when the Digitakt **starts a new project**, **loads a project** or **saves one**, and in
which slot. Mods that use it then keep their settings with the project:

- **Save the project** and the mod's settings are saved for that slot.
- **Load a project** and its settings come back.
- **A new project** starts with the mod's defaults.

## Mods that use it

FREQSEQ, SCENES, DRIVE, REVERSE, DICE, SONIC MAX, G-EQ and GLUE. Without PROJECT LINK in the build,
they work as before: one set of settings, whatever project is loaded.

---

## Good to know

- **Where it hooks in:** three places in the firmware, Project::initializeNewProject,
  Project::loadProjectFromMmc and ProjectManager::interactiveSave. It only takes note and then lets
  the Digitakt carry on as usual.
- **Where the settings go:** each mod keeps its per-project copies in its own part of the internal
  storage (eMMC blocks from `0x62000`), which the stock firmware doesn't use.

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

GPL-2.0-or-later. Source: `projlink.s`, `mod.json`.
