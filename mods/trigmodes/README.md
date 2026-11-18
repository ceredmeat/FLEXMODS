# TRIG MODES for the Digitakt MKI

The Digitakt II's trig modes on the Digitakt MKI: the 16 trig keys become something to play,
including a Model:Samples-style retrig.

- **Mod:** `trigmodes` 0.5, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.2 or later) and `seq-events`

---

## Quick start

1. Hold **NO** and press **PAGE**. A box in the middle of the screen names the mode.
2. Press PAGE again (still holding NO) to step to the next mode: OFF → TRKS → VEL → RTRG → RTRK → OFF.
3. While a mode is on, a small label in the top-right corner shows it.

Everything you play is **played, not recorded**. Your pattern never changes.

---

## The modes

| Mode | What the trig keys do |
|---|---|
| **OFF** | The normal Digitakt trig keys. |
| **TRKS** | Finger drumming: keys 1–8 play audio tracks 1–8, keys 9–16 play MIDI tracks 1–8. |
| **VEL** | The keys play the active track at 16 velocities: key 1 the softest, key 16 the loudest. |
| **RTRG** | Retrig: hold a key and the active track repeats in time. |
| **RTRK** | Retrig per track, after the Model:Samples: hold keys 1–8 to repeat tracks 1–8. |

### RTRG: retrig the active track

Hold a key and the active track repeats until you let go.

| Keys | Rate |
|---|---|
| 1 / 9 | 1/4 |
| 2 / 10 | 1/8 |
| 3 / 11 | 1/8 triplets |
| 4 / 12 | 1/16 |
| 5 / 13 | 1/16 triplets |
| 6 / 14 | 1/32 |
| 7 / 15 | 1/32 triplets |
| 8 / 16 | 1/64 |

Keys 9–16 are the same rates, but each hit is a little softer than the one before: a fade-out
roll.

### RTRK: retrig any track

- **Keys 1–8:** each repeats its own track (1–8) while held. Hold as many as you like at once.
- **Keys 9–16:** pick the rate for every track, in the same order as RTRG's keys 1–8 (1/4 to
  1/64). The rate stays until you pick another. It starts at 1/16, and a box shows the rate you
  picked.
- Works playing or stopped.

---

## Good to know

- **The rolls follow the tempo.** While the pattern plays, the mod measures how long a step is.
  Stopped, it keeps the last tempo it measured (120 BPM until something has played).
- **On-grid rates are exact.** 1/4, 1/8 and 1/16 rolls land exactly on the grid while the
  pattern plays. Triplets, 1/32 and 1/64 can be a hair late when the screen is busy, but no hit is
  ever skipped.
- **Your other shortcuts still work in every mode:** TRK + a trig picks a track, FUNC + a trig does
  what it always does, and YES/NO + a trig is MUTESNAP's (if you have it).
- **Changing mode stops** any roll that's playing.

---

## FAQ

**I hold a key but nothing plays.**
Check the corner label: in OFF the keys are normal trig keys. Also check the track has a sound
and isn't muted.

**The roll doesn't keep time with my pattern.**
Let the pattern play for a couple of steps first, so the mod can measure the tempo.

**Can I record what I play?**
Not yet. The modes play live only.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: all five modes, including RTRK with several tracks held at once,
rate changes while rolling, and rolling while stopped.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `trigmodes.c`, `mod.json`.
