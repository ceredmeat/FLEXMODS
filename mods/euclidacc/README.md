# Euclid Accents (Digitakt MKI, OS 1.54)

A Euclidean rhythm decides which of your **existing** trigs are accented (louder). It never adds
or removes trigs.

## How to use it

**SETTINGS** has new rows:

| Row | Values | What it does |
|---|---|---|
| **ACC T1** … **ACC T8** | OFF, 1–16 | How many accents to spread evenly over that audio track's length. The first step is always one. |
| **ACC DEPTH** | 0–64 (steps of 4) | How strong the effect is. Accented trigs play DEPTH÷2 louder, other trigs DEPTH÷2 softer. |

Example: a 16-step hi-hat track with a trig on every step, ACC T3 = 5, ACC DEPTH = 32. Steps
**1, 5, 8, 11, 14** play 16 louder, and the rest play 16 softer. That's an instant groove.

Try ACC 3 on a 16-step track for a tresillo feel (steps 1, 7, 12), or ACC 5 or 7 for
Afro-Cuban-style patterns.

- If a trig sits on an accent step it gets louder. If not, softer. Empty steps stay empty.
- It follows each track's own length, so polyrhythmic track lengths work.
- Your pattern is never changed. Settings are saved and come back at power-on.

## How it works (plain version)

The shared seq-events mod lets a mod adjust a trig's velocity right before it plays. For each trig,
the mod checks whether its step lands on a Euclidean accent (a simple bit of arithmetic) and nudges
the velocity up or down.

Needs **seq-events** in the same build.

## Changes

| Version | What changed |
|---|---|
| 0.3 | The save buffer is borrowed from the Digitakt's spare memory only while loading or saving, which leaves more RAM for your other mods. |

