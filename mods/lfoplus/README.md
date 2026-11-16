# LFO+ 0.5: new waves and slow speeds (Digitakt MKI, OS 1.54)

**New in 0.5:** clean-up. The small 19x17 pictures and the two picture hooks from 0.2 and 0.3 are
gone: the LFO page never used them (it draws the graph below). Smaller, two fewer firmware hooks,
nothing you can see changes.

**0.4: the LFO page's wave graph.** The big bracketed graph under WAVE/PHAS is drawn by its own
routine (0x4009700a), not by the knob-picture code 0.2 and 0.3 changed. That routine cut every wave
above 6 back to RND. 0.4 lets it through and gives it a one-cycle graph for each of the 22 new waves
(4 versions each, as the stock ones have: normal, upside down for negative depth, reversed, both), so
the graph scrolls with PHAS just like TRI or SIN. See `jomox.h.graphs.png`. The generator draws the
stock TRI graph exactly as the firmware has it, as a check.

**Earlier, 0.3:**

- **16 shapes from the Jomox Alphabase**, made from its LFO wave file (ABWV0300.wav), after BNC:
  DIP, HSU, HSD, DCY, GRW, SFL, BWL, NTC, TSQ, TTH, DBV, ZIG, SEQ, CHS, NSE, SPK.

## New waves

Turn **WAVE** past RND on the LFO page (works on audio **and** MIDI tracks):

| Wave | Shape | Good for |
|---|---|---|
| **PLS** | Pulse: high for the first quarter of the cycle, low for the rest | Rhythmic gating, chopping a pad |
| **STP** | Staircase: 4 steps up, then back to the start | Arpeggio-like pitch steps, stepped filter |
| **TRP** | Trapezoid: rise, hold high, fall, hold low | Smooth on/off swells |
| **BMP** | Double bump: two smooth humps per cycle | "Breathing" pulses at double speed |
| **FIN** | Shark fin: fast curved rise, curved fall | Pumping, sidechain-like movement |
| **BNC** | Bouncing ball: 4 bounces, each half as high and shorter | Ping-pong delays, bouncing filter blips |

Then the Alphabase shapes (its shape number in brackets):

| Wave | Shape |
|---|---|
| **DIP** (7) | Two dips down per cycle |
| **HSU** (8) | Half a sine up, then flat |
| **HSD** (9) | Half a sine down, then flat |
| **DCY** (12) | Sine that dies away |
| **GRW** (13) | Sine that grows |
| **SFL** (49) | Sine with flat steps at the middle |
| **BWL** (52) | Bowl: down, flat at the bottom, back up |
| **NTC** (53) | Notch: slide down, jump to the top, slide back |
| **TSQ** (54) | Triangle with a square drop in the middle |
| **TTH** (55) | Triangle with teeth |
| **DBV** (56) | Double V |
| **ZIG** (57) | Fast zigzag (three per cycle) |
| **SEQ** (59) | Stepped sequence |
| **CHS** (61) | Chaos: jumpy pattern |
| **NSE** (63) | Smooth noise |
| **SPK** (30) | Very thin pulse: a short spike at the start of each cycle |

The Alphabase's other shapes are left out because the Digitakt or LFO+ already has them (saw, sine,
triangle, square...) or because they are the same pulse at 30 different widths.

## Slow speeds

Turn **MULT** (audio tracks) past **2k** and you get two more sets of four:
- **/2 /4 /8 /16** synced to the tempo. The LFO takes 2, 4, 8 or 16 times longer than at MULT 1,
  which is great for slow sweeps over many bars.
- Then the same four again, **free-running** (not tied to tempo), like the stock free MULT values.

## How it works (plain version)

The Digitakt's LFO engine picks the wave's little routine from a list of six, plus RND, which it
handles separately. The mod gives the engine a longer list with six new routines on the end.
It also raises the WAVE knob's top value from 6 to 12 so you can reach them, and teaches the screen
their names.

For speed, the engine shrinks the speed number by a power of two set by MULT. The new MULT values
just shrink it by 2–16 times more than MULT 1 does.

The LFO page draws WAVE as a scrolling graph: one cycle of the wave, repeated and shifted by PHAS. The
mod gives that routine a one-cycle graph for each new wave, made by a script from the same wave maths
that makes the sound, so the graph matches what you hear.

The new wave routines run inside the sound engine, so they're plain arithmetic: no memory, no
calls to anything else.

## Good to know

- Patterns using new waves or slow MULT values still load on stock OS. There the wave falls back
  to RND and MULT to its top value, so you won't get errors.
- Trig modes ONE and HLF hold new waves at the centre value, which is what most stock waves do too.
- If the LFO page ever shows "ERR" for WAVE or MULT, tell me.
