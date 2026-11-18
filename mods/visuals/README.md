# VISUALS for the Digitakt MKI

Eye candy for the Digitakt: a beat radar, a little creature that lives on your music, Snake and
Pong (1 or 2 players), and old-TV screen effects.

- **Mod:** `visuals` 0.3, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later) and `seq-events`

---

## Quick start

Press **FUNC + LFO** to open the master pages, then use **LEFT/RIGHT** to reach the pages: **RDR**,
**PET**, **SNK**, **PNG** and **SCR**.

---

## RDR: beat radar

A radar sweep goes round once per loop of the active track. Every hit leaves a blip at its own
track's place in its own loop, which fades as the sweep comes round again.

- **Track 1** is the inner ring and **track 8** the outer one.
- **Loud hits** make bigger blips.
- **With a polymeter** (tracks of different lengths), the blips drift around the circle.
- Stopped, the sweep turns slowly on its own.

## PET: a creature that lives on your music

Meet **BIT**:
- **bounces** on the beat, **throws its arms up** on loud hits and **sings** when the music is
  busy;
- gets **hungry** and sad in silence. Its MOOD and FOOD bars fill up while music plays;
- **falls asleep** after 20 seconds of silence (watch for the Zzz);
- **turn any knob** to pet it: it smiles and a heart pops up.

CLEAR (FUNC + held LFO + PLAY) resets its mood and food.

## SNK: Snake

- **YES** starts and **NO** pauses.
- **The arrows** steer.
- **It moves in time with your pattern** while it plays, one cell per step of the active track.
  Stopped, it moves at its own pace.
- Eat the blinking food to grow. Hitting a wall or yourself ends the game. Your high score is
  kept until power-off. CLEAR resets it.

## PNG: Pong

- **Knob B** switches between **1P** (against the Digitakt) and **2P**, between games.
- **Player 1** (left paddle): **knob A**, or **UP/DOWN**.
- **Player 2** (right paddle, 2P only): **knob H**.
- **YES** starts and **NO** pauses. First to 9 points wins.

**In both games:** knob **D** picks a track (OFF, 1–8) whose sound plays on every catch or hit.
While a game is running, the arrows belong to the game. Paused or over, LEFT/RIGHT step the pages
again.

## SCR: screen effects

These work on **every screen** of the Digitakt, not only the VISUALS pages. They're saved, and
they start OFF.

| Knob | Name | What it does |
|---|---|---|
| A | **CRT** | **OFF**; **SFT**: a refresh line rolls slowly down the screen; **FUL**: that line, plus the picture switching off to a line and a dot when you press STOP and back on when you press PLAY, like an old TV, and the odd glitch. |
| B | **INV** | The whole screen flashes inverted for a moment: **OFF**, **ACC** (on loud hits), **BAR** (on beat 1 of each bar) or **KCK** (on every track 1 hit). |
| C | **THR** | For ACC: how loud a hit must be to count (velocity 64–127). |

---

## Good to know

- **CRT redraws the screen more often** to move its line. If the screen ever feels slow, turn CRT
  to OFF.
- **Saved:** the SCR settings are saved in one block of the internal storage (eMMC block
  `0x6004c`) that the stock firmware doesn't use. The games' scores and the pet's mood aren't
  saved.
- **Big build?** VISUALS is about 11 KB. With many other mods, Elekloader may say there isn't
  enough room. Leave out something you don't use.

---

## Changes

| Version | What changed |
|---|---|
| 0.3 | 2-player Pong (knob B). The sound picker moved to knob D. |
| 0.2 | A little less memory used (radar and Snake buffers). |
| 0.1 | First release. |

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: the radar, the pet, Snake, Pong, the CRT effects and the
inverted flashes.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `visuals.c`, `mod.json`.
