# Velocity Humanise (Digitakt MKI, OS 1.54)

Every note trig plays a little louder or softer at random, so the groove feels less robotic.

## How to use it

**SETTINGS** has new rows:

| Row | Values | What it does |
|---|---|---|
| **HUMAN T1** … **HUMAN T8** | OFF, 2, 4, 6, 8, 12, 16, 24, 32 | How far each trig's velocity can move up **or** down, for that audio track |
| **HUMAN MIDI** | same | The same, for all MIDI tracks |

Try 8–16 on hi-hats and percussion, and leave kicks OFF.

- Your pattern is never changed: the random amount is added as each trig plays. Turn it OFF and
  everything plays exactly as programmed.
- Velocity never goes below 0 or above 127.
- Settings are saved two seconds after you change them, and come back at power-on.

## How it works (plain version)

The shared seq-events mod lets a mod adjust a trig's velocity right before it plays. Humanise
rolls a die for each trig and adds or subtracts up to your amount.

Needs **seq-events** in the same build.
