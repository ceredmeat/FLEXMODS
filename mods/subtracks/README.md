# SUBTRACKS 0.1 (test build)

After the Tonverk's Subtracks machine: every audio track gets **four lanes** that share its one voice.

- **Lane 1** is the track itself: its own trigs and its own sound, exactly as stock.
- **Lanes 2-4** are 16-step lanes, each with its own sample and settings.
- One voice: a new hit cuts off the one before it, like a choke group.
- Two lanes on the same step: the **lowest lane plays**.

## How to use

| Action | How |
|---|---|
| Open the SUB page for the active track | Press **TRK twice** quickly |
| Pick a lane | **UP / DOWN** |
| Turn the lane's steps on and off | Trig keys **1-16** (lanes 2-4) |
| Set the lane's sound | Knobs **A-G**: SAMP, TUNE, STRT, LEV, FREQ, RESO, PAN. `--` (turned all the way left) follows the track's own sound |
| Mute or unmute a lane | **FUNC + trig 1-4** |
| Close | **NO** (or TRK twice) |

The page shows the four lanes at the top (filled box = playing, hollow = muted), the selected lane's
16 steps with the playhead underneath, and the lane's settings along the bottom.

## Test build: what to check

1. Lanes 2-4 play their own samples, in time, with no clicks beyond a normal choke.
2. A lane-1 trig after a lane-2 trig goes back to the track's own sound.
3. Two lanes on one step: only the lower one plays.
4. FILL, FREQSEQ, REVERSE and the other sequencer mods still work on a SUB track.
5. The values shown for TUNE and PAN are the raw numbers the locks send. Note what they sound
   like, so 0.2 can show them in semitones and L/R.

## Not in 0.1

- The lanes are shared by all patterns, and **not saved**: they're gone at power off.
- Lanes are 16 steps long and repeat over longer tracks.
- No lane colours on the trig LEDs and no all-lanes overview yet.

## How it works (plain version)

The mod adds a trig wherever an unmuted lane has a step and the track doesn't. When that trig
plays, it sends the lane's settings as parameter locks, the same way FREQSEQ's ROULETTE swaps
samples. The track's next trig of its own loads its own sound again.

Needs `core` 4.1 or newer and `seq-events`.
