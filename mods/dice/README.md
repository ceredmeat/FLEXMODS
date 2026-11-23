# DICE + AUTOPILOT for the Digitakt MKI

Random rhythms and a slowly evolving pattern, live. DICE rolls a new rhythm for a track;
AUTOPILOT changes a step or two every few loops so the pattern drifts. Your pattern is never
written over: NO puts it back exactly as programmed.

- **Mod:** `dice` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later) and `seq-events`

---

## Quick start

1. Press **FUNC + LFO**, then **LEFT/RIGHT** to the **DCE** page.
2. Pick a track with **TRK** and set **MOD** to **DCE**, then press **YES** to roll a rhythm.
3. Or set **MOD** to **AUT** and let the pattern evolve on its own.
4. **NO** puts the track back exactly as you programmed it.

---

## The knobs

| Knob | Name | What it does |
|---|---|---|
| A | **TRK** | The track (1–8 audio, 9–16 MIDI). The grid shows its steps. |
| B | **MOD** | **OFF**, **DCE** (DICE: a random rhythm, rolled with YES) or **AUT** (AUTOPILOT). |
| C | **HIT** | How many hits a roll makes (DICE), or how busy the autopilot lets it get (AUTO). |
| D | **EVR** | AUTO: a change every 1, 2, 4 or 8 loops of the track. |
| E | **MOV** | AUTO: how many steps change each time, 1–4. |
| F | **HOM** | AUTO: how strongly it drifts back toward what you programmed, 0–100 %. |
| G | **ADD** | DICE: **RPL** (only the dice plays) or **ADD** (the dice on top of your pattern). |

| Key | What it does |
|---|---|
| **YES** | Roll the dice for the track (DICE), or start the autopilot afresh (AUTO). |
| **NO** | The track back to exactly what you programmed. The mode stays. |

The rolls lean toward the downbeats, so they groove.

---

## Good to know

- **Live:** it changes what plays, not the pattern in memory.
- **Saved,** and with **PROJECT LINK** in the build the settings go with the project: saved when
  you save the project, back when you load it, and the defaults for a new project.
- Works alongside the other sequencer mods (DENSITY, FREQSEQ, FILL and others) through
  `seq-events`.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: rolling rhythms in REPL and ADD, the autopilot evolving a pattern with HOME pulling it back, NO restoring the track, and the settings saved.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `dice.c`, `mod.json`.
