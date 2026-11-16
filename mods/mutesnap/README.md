# Mute Snapshots 0.1 (Digitakt MKI, OS 1.54)

Four saved sets of track mutes you can jump between in time, like the Octatrack's mute scenes.

| Keys | What it does |
|---|---|
| Hold **YES**, press **trig 1-4** | Save the current mutes of all 16 tracks as snapshot 1-4 |
| Hold **NO**, press **trig 1-4** | Recall snapshot 1-4 at the **end of the bar** (at once if the sequencer is stopped) |
| The same again before the bar ends | Cancel the waiting recall |

- A box in the middle of the screen says what happened ("MUTES 2 SAVED", "MUTES 2 NEXT BAR").
- **M1-M4** shows in the top-right corner while a recall waits for the bar.
- Snapshots are kept on the unit and come back at power-on.
- It uses the Digitakt's own **global** mutes, so the mute screen and the LEDs show the result, and
  you can still mute by hand as usual.
- "The bar" is counted from track 1: every 16 steps, or at the end of track 1's pattern if it is
  shorter than 16 steps.

## How it works (plain version)

- The firmware keeps the global mutes as one bit per track. The mod reads them with the firmware's
  own routine (0x4001d5f2) and sets them with the one a MIDI mute uses (0x4001d636), so the rest
  of the Digitakt (screens, LEDs, MIDI) hears about each change exactly as it would from MIDI.
- The trig keys are key numbers 24-39 (found in the mute screen's own key handler).
- seq-events tells the mod every step track 1 plays. On the last 16th of a bar it raises a flag; the
  next screen update (every ~33 ms, faster than a 16th at any tempo) sets the mutes, so they're in
  place for the next bar's first step.
- Saved in one eMMC block (0x60043), like the other mods' settings.

Needs **seq-events**. Takes YES/NO + trig 1-4; nothing else.
