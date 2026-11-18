# flexmods

Twenty Elekloader mods for the **Elektron Digitakt MKI, OS 1.54**, made by Cere (with Claude).
Each mod is its own `.elemod`: pick the ones you want in Elekloader, build your OS and flash it.

**Download:** the `.elemod` files are in the [latest release](../../releases/latest).
**Instructions:** each mod has a README in `mods/<name>/` explaining every knob and key.

---

## The mods

### Play and perform

| Mod | File | What it does | Needs |
|---|---|---|---|
| TRIG MODES | `trigmodes-0.5.elemod` | NO + PAGE turns the trig keys into the Digitakt II's trig modes: TRKS (finger drumming), VEL (16 velocities), RTRG (retrig rolls, 1/4 to 1/64 with triplets and fade-outs) and RTRK (Model:Samples-style retrig on any track, several at once) | core, seq-events |
| SCENES | `scenes-0.1.elemod` | Octatrack-style scenes A and B with a crossfader: record knob moves on any tracks into a scene, then morph between them (TRK + FUNC) | core |
| DENSITY | `density-0.2.elemod` | One knob for how busy the pattern is: left drops trigs, right adds ghost notes, the middle plays it as written; can keep the downbeats | core, seq-events |
| KIT A/B | `kitab-0.2.elemod` | TRK + NO flips between your edited kit and the pattern's temp-saved kit (after the Analog Rytm) | core |
| MUTE SNAPSHOTS | `mutesnap-0.1.elemod` | YES + trig 1–4 saves the mutes, NO + trig 1–4 recalls them at the end of the bar | core, seq-events |
| TRACK SOLO | `tracksolo-0.2.elemod` | TRK + UP solos the active track while held, TRK + DOWN latches it (after the Octatrack) | core, seq-events |

### Sequencing

| Mod | File | What it does | Needs |
|---|---|---|---|
| FREQSEQ | `freqseq-0.5.elemod` | A generative sequencer on every track: 15 of them, from a Turing Machine and Grids to Game of Life, sample roulette and Morse code | core, seq-events |
| POLYMETER | `polymeter-0.1.elemod` | Every track's length and speed on one page, with moving playheads and one-turn polymeter presets | core, seq-events |
| REVERSE | `reverse-0.4.elemod` | A chance per track that a trig plays its sample backwards | core, seq-events |
| EUCLID ACCENTS | `euclidacc-0.2.elemod` | A Euclidean rhythm accents the trigs you already have (1–16 accents per track, DEPTH sets how much) | core, seq-events |
| VELOCITY HUMANISE | `humanise-0.2.elemod` | Random velocity variation per track, set in SETTINGS (after the Analog Rytm) | core, seq-events |
| MULTITRIG | `multitrig-0.5.elemod` | In grid recording, hold a trig and tap TRK (whole track) or PAGE (that page), then turn a knob to edit all those trigs' locks at once | core |
| CTRL ALL | `ctrlall-0.2.elemod` | TRK + YES: a lock you set on a step goes to that step on every track (after the Analog Rytm) | core |

### Sound

| Mod | File | What it does | Needs |
|---|---|---|---|
| DJ FILTER | `djfilter-0.7.elemod` | A stereo master filter after the Electrix FilterFactory: one-knob DJ sweeps, resonance, BUZZ drive, an LFO or envelope follower, tempo sync | core, seq-events |
| DRIVE | `drive-0.4.elemod` | Per-track saturation before the filter: tape, tube, fuzz or wavefolder, with tone, mix and level | core |

### LFOs and modulation

| Mod | File | What it does | Needs |
|---|---|---|---|
| LFO+ | `lfoplus-0.5.elemod` | 22 new LFO waves (incl. 16 shapes after the Jomox Alphabase), each with its own picture, plus slower speeds | core |
| DEJA VU | `dejavu-0.5.elemod` | The RND LFO plays a repeating loop of random values, with RND LOCK / RND LOOP as LFO destinations | core |

### Display and settings

| Mod | File | What it does | Needs |
|---|---|---|---|
| VISUALS | `visuals-0.3.elemod` | A beat radar, a pet that lives on your music, Snake, Pong (1 or 2 players), and CRT and flash screen effects | core, seq-events |
| REAL UNITS | `realunits-0.2.elemod` | Filter FREQ in Hz, envelope times in ms/s, LFO speed as a cycle length or Hz | core |
| MOD SETTINGS | `modsettings-0.1.elemod` | Optional: folds the DEJA VU, HUMANISE and EUCLID ACCENTS rows in SETTINGS into one MOD SETTINGS row | core |

---

## What you need

- **Elekloader's `core`**, version 4.4 is in the release. It's also in elekloader's own releases.
- **`seq-events`** for every mod marked so above. It's in the shop, from the
  [FX LOCKS](https://github.com/ceredmeat/digitakt-fx-locks) release.
- **A Digitakt MKI on OS 1.54.** OS 1.53 isn't supported.

---

## Good to know

- **Mix and match:** every mod works on its own and together with the others.
- **Memory:** with all twenty in one build, the OS is close to its memory limit. If Elekloader
  says the mods don't fit, leave out the ones you don't use. VISUALS (about 11 KB) and FREQSEQ are
  the biggest.
- **Key combinations:** several mods use TRK + a key. They're chosen not to clash: TRK + UP/DOWN
  is TRACK SOLO, TRK + YES is CTRL ALL, TRK + NO is KIT A/B, TRK + FUNC is SCENES, TRK + PAGE is FREQSEQ, and
  NO + PAGE is TRIG MODES.
- **Master pages:** DJ FILTER, DENSITY, DRIVE, REVERSE, POLYMETER and VISUALS live after the stock
  master pages: press FUNC + LFO, then step with LEFT/RIGHT.

All the mods were tested on a real Digitakt MKI running OS 1.54. No `.syx` is shared here: it would
contain Elektron's firmware.

---

## Licence

GPL-2.0-or-later (see LICENSE), except **FREQSEQ**, which is GPL-3.0-or-later because of the Grids
drum maps (see `mods/freqseq/LICENSE`). `mods/common/mpui.h` is the shared page drawing that
several mods include.
