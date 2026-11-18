# SCENES for the Digitakt MKI

Two scenes, A and B, with a crossfader between them, after the Octatrack's. A scene is a set of
sound settings on any of the eight audio tracks. Slide from A to B and every setting in the
scenes morphs at once.

- **Mod:** `scenes` 0.1, for Elekloader
- **Digitakt MKI, OS 1.54 only**
- **Needs in the build:** `core` (4.1 or later)

---

## Quick start

1. Hold **TRK** and press **FUNC** to open the SCENES page.
2. Press **LEFT** to record **scene A**. The page closes and **REC A** shows at the top.
3. Turn knobs on any audio tracks (filter, amp, SRC, LFO and so on). While recording, the knob
   turns go into the scene, not into your sound.
4. Press **TRK + FUNC** again to stop recording. Your sounds go back to how they were, and the
   page comes back.
5. Do the same with **RIGHT** for **scene B**.
6. Turn **any knob** on the page to move the crossfader from A to B.

---

## On the SCENES page

| Key | What it does |
|---|---|
| **Any knob** | Moves the crossfader. |
| **YES** | Jumps the fader to the other end. |
| **LEFT / RIGHT** | Record scene A / B. |
| **FUNC + LEFT / RIGHT** | Clear scene A / B. |
| **NO** | Close the page. |
| **TRK + FUNC** | Open or close the page, or end recording. |

---

## How the fade works

- **Fader at A:** the tracks play scene A's values. **At B:** scene B's. **In between:** a mix.
- **A setting only one scene has** fades between that scene's value and the track's own setting.
- **Your sounds are never overwritten.** Trigs, p-locks and knob turns on the track still work,
  and scenes sit on top of them.
- **What a scene can't hold:** the machine, the sample (SAMP) and the LFO destinations.

---

## Good to know

- **Saved:** both scenes are saved and come back at power-on (internal storage, eMMC block
  `0x60047`, which the stock firmware doesn't use). The fader starts at A.
- **TRK + FUNC** is SCENES' shortcut. On a stock Digitakt that combination is quick mute, so with
  SCENES in your build, use the global mute mode instead.

---

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54: recording scenes A and B on several tracks, crossfading, the
YES jump and clearing.

---

## Flash and undo

- **Flash:** build with Elekloader, then send the `.syx` with Elektron Transfer and confirm with
  YES on the unit.
- **Undo:** flash the stock `Digitakt_OS1.54.syx`.

Share the `.elemod`, never a `.syx`: a `.syx` contains Elektron's firmware.

## Licence

GPL-2.0-or-later. Source: `scenes.c`, `mod.json`.
