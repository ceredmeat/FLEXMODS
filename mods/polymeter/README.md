# POLYMETER for the Digitakt MKI

Every track's length and speed on one page, with a moving playhead per track, so you can see the
tracks drift against each other. It also has one-turn polymeter presets.

- **Mod:** `polymeter` 0.2, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later) and `seq-events`

---

## Quick start

1. Press **FUNC + LFO** to open the master pages, then use **LEFT/RIGHT** to reach **PLY**.
2. Play a pattern.
3. Turn knob **E (SET)** to **PRM** and listen to the tracks drift apart and come back together.

---

## The screen

- **Tracks 1–8** are drawn as lines, top to bottom. Each line is as long as that track, with one
  dash per step.
- **A thick mark** moves along each line: where that track is playing right now.
- **A small arrow** marks the active track.
- **On the right:** the active track's number, length (**L**) and speed (**S**).

---

## The knobs

| Knob | Name | What it does |
|---|---|---|
| A | **LEN** | The active track's length, 1–64 steps. |
| B | **SPD** | The active track's speed: 2X, 3/2, 1X, 3/4, 1/2, 1/4, 1/8. Only in TRK mode, as on the stock Digitakt. |
| C | **MOD** | The pattern's scale mode: **PAT** (one length for every track) or **TRK** (a length and speed per track). |
| D | **CHG** | In TRK mode, how long before the pattern counts as finished, for pattern changes: **INF** (never), or 2–64. |
| E | **SET** | A polymeter in one turn, for tracks 1–8 (it switches to TRK mode too). |

**SET presets:**

| Preset | Track lengths 1–8 |
|---|---|
| **16** | All 16. |
| **DWN** | 16, 15, 14, 13, 12, 11, 10, 9 |
| **PRM** | 16, 13, 11, 7, 5, 17, 19, 23: odd lengths that take a long time to line up again. |
| **3V4** | 16, 12, 16, 12…: 3 against 4. |

**CLEAR** on the page (FUNC + held LFO + PLAY) puts tracks 1–8 back to 16 steps at 1X.

---

## Good to know

- **It edits your pattern**, exactly like the stock scale menu (FUNC + PTN) does, so the changes
  **save with the pattern**. The stock scale menu shows the same values.
- **SET overwrites** tracks 1–8's lengths. Save the pattern before experimenting if you want to
  keep the old ones.
- The page shows audio tracks 1–8. MIDI tracks keep whatever you set in the stock menu.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: lengths and speeds matching the stock scale menu, the presets,
the moving playheads, and the changes saving with the pattern.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `polymeter.c`, `mod.json`.
