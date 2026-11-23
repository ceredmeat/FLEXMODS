# MIDI Arp 0.2 (Digitakt MKI, OS 1.54)

An arpeggiator on each MIDI track. It runs in time with the sequencer, for the trigs in your pattern
and for notes you hold.

## Settings (SETTINGS > MOD SETTINGS)

| Row | Values | What it does |
|---|---|---|
| **ARP TRACK** | M1-M8 | Which MIDI track the next three rows show and change |
| **ARP MODE** | OFF, UP, DOWN, UPDN, RND | The order the notes are played in. OFF = the track is stock |
| **ARP RATE** | 1/16, 1/8, 1/4 | A note every 1, 2 or 4 steps of the track |
| **ARP OCT** | 1-3 | How many octaves the pattern climbs through |

Each MIDI track has its own three settings; they are saved on the unit.

## In the pattern

- Put a **note trig** on the MIDI track. Its notes are the chord: **NOTE** plus **NOT2-NOT4** on the
  trig page.
- That trig plays the first note. Then one note plays every RATE steps, walking through the chord,
  **until the track's next trig**.
- A new note trig starts the arp again with its own chord.
- A **lock trig** (FUNC + trig) stops the arp without playing anything.
- Each arp note is a normal trig to the Digitakt: it uses the track's channel, length and velocity.

## Live

- With the sequencer **playing**, hold notes on the MIDI track (trig keys in chromatic mode, or a
  keyboard sending on the track's channel). They are arpeggiated instead of sounding at once, with
  the velocity you played. Let go and it stops.
- With the sequencer **stopped**, the keys play as normal (the arp needs the sequencer's clock).

## Good to know

- The arp's timing is the sequencer's own (it adds trigs as the step plays), so it is as tight as
  any trig, and follows the track's scale/speed.
- The arp's extra notes are not written into the pattern.
- Needs **seq-events** and core 4.2 or newer.

## Changes

| Version | What changed |
|---|---|
| 0.2 | The save buffer is borrowed from the Digitakt's spare memory only while loading or saving, which leaves more RAM for your other mods. |

