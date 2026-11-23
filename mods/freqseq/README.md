# FREQSEQ for the Digitakt MKI

A generative sequencer on every track. Each of the 16 tracks (8 audio, 8 MIDI) can run one of 15
sequencers, from a Turing Machine to Mutable Instruments' Grids, each with its own settings. Your
pattern itself never changes: FREQSEQ changes what plays.

- **Mod:** `freqseq` 0.6, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later) and `seq-events`
- **Licence:** GPL-3.0-or-later (see below)

---

## Quick start

1. Select a track (TRK + a trig key).
2. Hold **TRK** and press **PAGE** (with no trig held). The FREQSEQ page opens for that track.
3. Press **LEFT/RIGHT** to choose the track's sequencer. The knobs set it.
4. Play. Change track as usual (TRK + trig) to give another track its own sequencer.

| Key | What it does on the page |
|---|---|
| **LEFT / RIGHT** | Choose the track's sequencer (OFF = the track plays normally). |
| **Knobs** | That sequencer's settings (shown on screen). |
| **YES** | The sequencer's action, e.g. a new loop, a new board, restore (see the table). |
| **DOWN / UP** | The FEEL page and back. |
| **NO** | Close the page. |

Your choices are saved and come back at power-on.

---

## The sequencers

| Sequencer | What it does | YES |
|---|---|---|
| **POLYRHYTHM** | After the Moog Subharmonicon: four rhythms (R1–R4, every 1–16 steps) play the track, and each time they fire, a 4-step pitch sequence (S1–S4) moves on. | |
| **CORRUPT** | After the Labyrinth: each loop a few steps drift: notes, then velocities, then filter, resonance and pan (WHT). AMT sets how much. It mutates loop after loop. | Reset |
| **TURING** | After Music Thing's Turing Machine: a loop of bits makes notes and/or rhythm (MOD). LCK: how locked the loop is. LEN, RNG (note range), DNS (density), VEL. | New loop |
| **DRUNK** | The playhead wanders: **REV** backwards, **PNG** ping-pong, **RND** anywhere, **DRK** a staggering walk. AMT: how often. | New path |
| **LIFE** | Conway's Game of Life: every LIFE track is one row of a shared board. Each loop (LPS) is the next generation. RUL picks the rules. | New board |
| **EROSION** | Each loop some trigs fade a step until the pattern is gone. With CYC on, it grows back again. AMT, SFT (how far). | Restore |
| **MARKOV** | Learns which note follows which, from the track's notes and what you play (LRN), then plays notes picked from that. AMT: how much. | Forget |
| **GRIDS** | After Mutable Instruments' Grids: a kick, snare or hat part (PRT) read from a map of drum patterns at X/Y. DNS sets how busy, CHA adds chaos. | |
| **CANON** | Plays what another track (SRC) played, DLY steps later, transposed by TRN: a round. | |
| **ROULETTE** | Each hit plays a random sample from slots LO–HI, PRB of the time. | |
| **SCATTER** | Each hit starts from a random point near the track's STRT (AMT), PRB of the time. GRD snaps it to a 2, 4, 8 or 16-slice grid, great for breaks. | |
| **WOLFRAM** | A one-row cellular automaton (rules 30, 45, 54, 60, 90, 105, 110, 150) makes the rhythm, a new row every LPS loops. | |
| **MATHS** | The digits of **PI**, **FIB** (Fibonacci) or **PRM** (primes) as notes and/or trigs (MOD), RNG wide, starting at OFS. | |
| **MORSE** | A word (SOS, DUB, HIT…) tapped out in Morse code as rhythm, dashes accented. UNT: the length of a dot. | |
| **BERNOULLI** | A coin toss on each trig: it stays on this track or goes to track TRK, PRB sets the odds. | |

### FEEL (press DOWN on the page)

Shared by all the sequencers:
- **SCALE** and **ROOT:** the melodic ones play in key;
- **HUMAN:** a random velocity nudge;
- **GROOVE:** off-beats softer.

---

## Good to know

- **Nothing is written to your pattern.** Set a track to OFF and it plays as written.
- **Works with the other sequencer mods** (DENSITY, EUCLID ACCENTS and others) through `seq-events`.
- **An old UNDERTONE/SUBSEQ save** is carried over: its POLY, CORRUPT and TURING tracks get those
  sequencers.

---

## Changes

| Version | What changed |
|---|---|
| 0.6 | With PROJECT LINK in the build, the settings go with the project: saved when you save it, back when you load it, defaults for a new project (not yet confirmed on a unit). Page keys (TRIG, SRC, FLTR, AMP, LFO, GLOBAL, SAMPLE) close the FREQSEQ screen and go straight to that page. The save buffer is borrowed from the Digitakt's spare memory only while loading or saving, which leaves more RAM for your other mods. |
| 0.5 | Fixed: ROULETTE and SCATTER hits that landed on sample 0 or start 0, and wobbles to 0, are no longer skipped. |
| Earlier | A sequencer on every track at once, seven more sequencers (CANON to BERNOULLI), clearer screens. |

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: all fifteen sequencers, including ROULETTE and SCATTER after the
0.5 fix.

---

## Licence

**GPL-3.0-or-later.** GRIDS' drum maps (`grids.h`) are Emilie Gillet's, from Mutable Instruments'
Grids, under the GPL v3, so FREQSEQ as a whole is GPL v3 or later. See `LICENSE` in this folder.

Source: `freqseq.c`, `grids.h`, `mod.json`.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.
