# Deja Vu 0.6: looping RND LFO (Digitakt MKI, OS 1.54)

The LFO's **RND** wave normally picks new random values forever. With Deja Vu it plays a **loop** of
random values that you can freeze or let slowly drift, like the Octatrack's Deja Vu mod (or a Turing
Machine module).

## New in 0.5: RND Lock and RND Loop in the DEST list

On the **audio tracks**, the LFO page's **DEST** list has a new group at the end, **RND**, with two
entries: **Lock** and **Loop**.

- Point one LFO at **RND:Lock** and it pushes the *other* LFO's lock amount up and down (around the
  value set by RND LOCK in SETTINGS, or by PHAS). Full depth swings it the whole 0-100 %.
- Point it at **RND:Loop** and it moves the loop length through the list (OFF, 2, 3 ... 32) around
  the RND LOOP setting.
- It acts on the RND LFO(s) of the same track. Typical use: LFO1 = RND on your target, LFO2 = a slow
  TRI on RND:Lock, so the random pattern freezes and melts in waves.

How: two unused parameter numbers in the firmware (11 and 12, named "Error") are renamed and given
two unused sound slots (46, 47). The firmware's own code then lists them as destinations; the mod
reads how hard each LFO is pushing them. If you ever combine this with a machine-pages machine that
uses extra parameters, those use the same slots: don't point an LFO at RND on such a track.

## In 0.4: RND LOCK per track, from the PHAS knob

Turn **RND LOCK** in SETTINGS one step past 100% and it reads **PHAS**. Now every LFO set to RND takes
its lock amount from **its own PHAS knob** on the LFO page: 0 = fully random, 127 = the loop repeats
exactly, in between it drifts.

Why PHAS: the Digitakt ignores the start phase when the wave is RND, so that knob does nothing
there. Because it is a normal sound parameter, the lock amount is now:
- **per track and per LFO** (LFO1 and LFO2 each have their own),
- **saved with the sound / pattern**,
- **parameter-lockable** (hold a trig, turn PHAS: that step freezes or frees the loop),
- **a modulation target**, wherever the stock DEST list lets an LFO or anything else reach the other
  LFO's start phase.

RND LOOP (the loop's length) and RND TRIG stay global settings for now.

## How to use it

1. Set an LFO's **WAVE** to **RND** as usual (LFO page). Pick a DEST, add some DEPTH.
2. Go to **SETTINGS** and scroll to the new rows:

| Row | Values | What it does |
|---|---|---|
| **RND LOOP** | OFF, 2, 3, 4, 5, 6, 7, 8, 12, 16, 32 | How many random values the loop holds. RND picks 16 values per LFO cycle, so **16 = the same random shape every cycle**. Short loops (2–4) make little riffs. |
| **RND LOCK** | 0–100% | How often a value is **kept** when its turn comes round again. **100%** = the loop repeats exactly. **70–90%** = it slowly mutates. **0%** = fully random, like stock. |
| **RND TRIG** | FREE, RESTART | **RESTART**: each trig starts the loop again from its first value, so the random pattern lines up with your notes. This only matters when the LFO's trig mode is TRG, HLD, ONE or HLF. |

- These settings apply to **every** LFO that's set to RND: both LFOs on the audio tracks, and the
  MIDI tracks' LFO. Each LFO still has its **own** loop, so they don't all play the same values.
- RND LOOP **OFF** = the LFO behaves exactly like stock.
- **New in 0.2:** settings are saved two seconds after you change them and come back at power-on.
- **Tip:** to get a new loop you like, set LOCK to 0% for a moment, then back to 100% to freeze it.

## How it works (plain version)

In the firmware, the LFO engine (`0x400ed766` for audio tracks, `0x400ed438` for MIDI tracks)
works out every LFO's value many times a second. Waves TRI–RMP each have their own small routine.
**RND** is different: each time the LFO moves on by 1/16 of its cycle, it asks a tiny random-number
routine (`0x400ed3fc`) for a new value.

The mod makes the first instruction of that tiny routine jump to the mod's code instead. When the
engine asks, the mod also gets told which LFO is asking. So it keeps a separate loop for every LFO:
it steps to the next value in that LFO's loop and hands that back. Depending on RND LOCK, it
sometimes swaps the value for a fresh random one first. With RND LOOP off, it does exactly what the
stock routine does, instruction for instruction.

This code runs inside the sound engine, many times a second, so it's kept very short and never
calls anything else in the firmware.

## Changes

| Version | What changed |
|---|---|
| 0.6 | The save buffer is borrowed from the Digitakt's spare memory only while loading or saving, which leaves more RAM for your other mods. |

## Mixing with other mods

It hooks one spot (`0x400ed3fc`) and adds two SETTINGS rows. If another mod hooks the same spot or
uses the same row names, elekloader refuses the combination instead of building a broken file.
