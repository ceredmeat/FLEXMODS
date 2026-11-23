# VISUALS for the Digitakt MKI

Eye candy for the Digitakt: a beat radar, a little creature that lives on your music, old-TV screen
effects, screensavers, a clock, a scope and live art. (Snake and Pong moved to
[DigiARCADE](https://github.com/ceredmeat/DigiArcade) in 0.4.)

- **Mod:** `visuals` 0.5, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later) and `seq-events`
- **Works with:** LED COLORS 1.0+ (the trig keys follow the screensaver and the storm), FILL and
  DJ FILTER 0.6+ (screen glitches when they play)

---

## Quick start

Press **FUNC + LFO** to open the master pages, then use **LEFT/RIGHT** to reach the pages: **RDR**,
**PET**, **SCR**, **SAV**, **CLK**, **SCP** and **ART**. On the pages with settings, turn a knob and
the line above the knobs says what it does.

---

## RDR: beat radar

A radar sweep goes round once per loop of the active track. Every hit leaves a blip at its own
track's place in its own loop. Like a submarine sonar, a new blip pings with a growing ring, then
shrinks, flickers and fades before the sweep comes round again.

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

## SCR: screen effects

These work on **every screen** of the Digitakt, not only the VISUALS pages. They're saved, and
they start OFF.

| Knob | Name | What it does |
|---|---|---|
| A | **CRT** | **OFF**; **SFT**: a refresh line rolls slowly down the screen; **FUL**: that line, plus the picture switching off to a line and a dot when you press STOP and back on when you press PLAY, like an old TV, and the odd glitch. |
| B | **INV** | The whole screen flashes inverted for a moment: **OFF**, **ACC** (on loud hits), **BAR** (on beat 1 of each bar) or **KCK** (on every track 1 hit). |
| C | **THR** | For ACC: how loud a hit must be to count (velocity 64–127). |
| D | **VHS** | Worn-tape tracking noise. |
| E | **SNW** | TV static (snow) while the pattern is stopped. |
| F | **BRD** | A frame round the screen that flashes on the beats. |
| G | **SCN** | Scanlines: OFF, 1 (light) to 3 (heavy). |
| H | **LNK** | A glitch while FILL plays (**FIL**), while the DJ FILTER moves (**DJF**), or both (**BTH**). |

## SAV: screensaver

After a while without touching the Digitakt, a screensaver takes over the screen; any key or knob
brings your screen back.

| Knob | Name | What it does |
|---|---|---|
| A | **SAV** | OFF, **STR** starfield, **LGO** a bouncing logo, **PIP** pipes, **RAN** falling rain, **LAV** lava lamp, **FER** ferrofluid, **LIF** Game of Life (your hits plant new cells), **LIS** Lissajous, **STM** a storm. |
| B | **TIM** | How long before it starts: 30 s, 1, 2, 5 or 10 minutes. |
| C | **PLY** | ON lets it start while the pattern plays too; OFF only when stopped. |
| D | **TXT** | The bouncing logo's word, or **OWN** for your own. |
| E, F | **POS**, **CHR** | With OWN: pick a letter (POS) and change it (CHR), up to 8 letters. |

With LED COLORS 1.0 or later in the build, the trig keys light up with the screensaver.

## CLK: clock

The tempo in big numbers, PLAY or STOP, the pattern, the active track, and bar and beat. **Tap YES**
in time to measure a tempo (a tap meter: it doesn't change the Digitakt's tempo); **NO** clears it.

## SCP: scope

Knob **A** picks **SCOPE** (the waveform of the mix), **XY** (left against right) or **SPECTRUM**
(eight bands from 60 Hz up, with held peaks).

## ART: art and overlays

| Knob | Name | What it does |
|---|---|---|
| A | **ART** | The art on this page: LAV, FER, LIF, LIS or **STM** (a storm: clouds, hills, slanting rain and lightning). |
| B | **RAIN** | Over every screen: **RAN** rain, or **STM** a storm whose lightning flashes the screen inverted. |
| C | **NAME** | A track's name in big letters on its hits: **ACC** (loud hits) or **ALL**. |
| D | **INV** | The whole screen inverted. |
| E | **BOOT** | A splash when the mods start. |
| F | **TAG** | Your SAV word as a name tag at the top right. |

**Storm lightning** (and its flash on the trig keys, with LED COLORS) only strikes **while the
pattern is stopped**, so it never distracts you while you play or record.

---

## Good to know

- **CRT redraws the screen more often** to move its line. If the screen ever feels slow, turn CRT
  to OFF.
- **Saved:** the SCR, SAV and ART settings are saved in one block of the internal storage (eMMC
  block `0x6004c`) that the stock firmware doesn't use. The pet's mood isn't saved.
- **Screensavers and effects redraw the screen** more often. If the screen ever feels slow, turn
  them off.
- **Big build?** VISUALS is one of the bigger mods. With many other mods, Elekloader may say there isn't
  enough room. Leave out something you don't use.

---

## Changes

| Version | What changed |
|---|---|
| 0.5 | New pages: SAV (screensavers), CLK (clock and tap meter), SCP (scope, XY, spectrum) and ART (live art, rain and storm overlays, big track names, splash, name tag). SCR gets VHS, snow, a beat border, scanlines and the FILL / DJ FILTER glitch link. The radar's blips ping and fade like a sonar. The trig keys follow the screensaver with LED COLORS. |
| 0.4 | Snake and Pong moved to DigiARCADE. |
| 0.3 | 2-player Pong (knob B). The sound picker moved to knob D. |
| 0.2 | A little less memory used (radar and Snake buffers). |
| 0.1 | First release. |

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: the radar, the pet, the screen effects, the
screensavers, the clock, the scope and the ART page with its storm.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `visuals.c`, `mod.json`.
