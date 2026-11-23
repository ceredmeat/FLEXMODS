# DRIVE for the Digitakt MKI

A saturation of its own on each audio track, before the track's filter: tape warmth, tube grit,
fuzz or a wavefolder. New in 0.5: a **low pass gate** after it, the Buchla-style gate from VILNIS, on
any track.

- **Mod:** `drive` 0.6, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later)

---

## Quick start

1. Select the track you want to drive (TRK + a trig key).
2. Press **FUNC + LFO** to open the master pages, then use **LEFT/RIGHT** to reach **DRV**.
3. Turn knob **A (TYP)** to pick a type, then turn up **B (DRV)**.

The page always edits the **active** track. Change track and the knobs show that track's drive.

---

## The knobs

| Knob | Name | What it does |
|---|---|---|
| A | **TYP** | **OFF**, **TAP** (tape: soft and warm), **TUB** (tube: lopsided, even harmonics), **FUZ** (fuzz: hard and square-ish) or **FLD** (a wavefolder: bright and metallic). |
| B | **DRV** | How hard it's driven. |
| C | **TON** | The tone after the drive: a low pass from dark to fully open. |
| D | **MIX** | Dry to fully driven. |
| E | **LVL** | The output level. The types are already kept at about the same loudness; use this to fine-tune. |
| F | **LPG** | The low pass gate: **OFF**, **LP** (the sound darkens as it dies), **VCA** (it fades) or **BTH** (both: the classic Buchla "bongo"). |
| G | **DCY** | How fast the gate closes, from short plucks to long rings. |
| H | **RES** | The gate's resonance, for a sharper, wetter pluck. |

---

## The low pass gate (F–H)

A low pass gate is a filter and a volume control that move together, opened by each hit and
closing with a soft, organic decay, like the vactrol in a Buchla.

- **Every hit on the track strikes it:** sequenced trigs, live presses and retrigs. Harder hits
  open it further.
- **It comes after the drive**, so you can drive into it, or set TYP to OFF and use only the gate.
- **With VCA or BTH, the sound only lives inside the gate's decay.** That's how a low pass gate
  works: turn up DCY for longer sounds.
- **Try it on sustained sounds:** a pad, a chord sample or a held synth note on BTH with DCY
  around 40 turns every hit into a woody pluck.

---

## The screen

- **The curve in the middle** shows what comes out for what goes in. It's straight with no drive,
  bends with TAPE and TUBE, flattens with FUZZ and folds back on itself with FOLD.
- **The lists on the left and right** show all eight tracks' types, with the active track
  highlighted.

---

## Good to know

- **It's per track:** each of the eight audio tracks has its own drive and gate settings.
- **It sits before the filter**, so the track's filter can tame a harsh drive.
- **Saved:** all eight tracks' settings are saved about 2 seconds after a change (settings saved
  by 0.4 carry over, with the gate OFF), in one block of
  the internal storage (eMMC block `0x6004a`) that the stock firmware doesn't use, and come back
  at power-on.
- **Processing cost:** driving all eight tracks uses some extra processing. If you hear crackles
  in a very busy project, turn the drive off on tracks that don't need it.

---

## Changes

| Version | What changed |
|---|---|
| 0.6 | With PROJECT LINK in the build, the settings go with the project: saved when you save it, back when you load it, defaults for a new project (not yet confirmed on a unit). The save buffer is borrowed from the Digitakt's spare memory only while loading or saving, which leaves more RAM for your other mods. |
| 0.5 | The low pass gate (F–H). |
| 0.4 | Smaller page code; the sound is unchanged. |

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: all four types on drum tracks, levels kept even, settings
surviving a power cycle, and the low pass gate (0.5).

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `drive.c`, `mod.json`.
