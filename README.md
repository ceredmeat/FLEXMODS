# flexmods

Twenty-eight Elekloader mods for the **Elektron Digitakt MKI, OS 1.54**, made by Cere (with Claude).
Each mod is its own `.elemod`: pick the ones you want in Elekloader, build your OS and flash it.

**Download:** the `.elemod` files are in the [latest release](../../releases/latest).
**Instructions:** each mod has a README in `mods/<name>/` explaining every knob and key.

---

## The mods

### Play and perform

| Mod | File | What it does | Needs |
|---|---|---|---|
| TRIG MODES | `trigmodes-0.5.elemod` | NO + PAGE turns the trig keys into the Digitakt II's trig modes: TRKS (finger drumming), VEL (16 velocities), RTRG (retrig rolls, 1/4 to 1/64 with triplets and fade-outs) and RTRK (Model:Samples-style retrig on any track, several at once) | core, seq-events |
| SCENES | `scenes-0.3.elemod` | Octatrack-style scenes A and B with a crossfader: record knob moves on any tracks into a scene, then morph between them (TRK + FUNC). SCENE INFO (DOWN) shows and clears single values | core |
| DENSITY | `density-0.2.elemod` | One knob for how busy the pattern is: left drops trigs, right adds ghost notes, the middle plays it as written; can keep the downbeats | core, seq-events |
| KIT A/B | `kitab-0.2.elemod` | TRK + NO flips between your edited kit and the pattern's temp-saved kit (after the Analog Rytm) | core |
| FILL | `fill-0.3.elemod` | Hold YES, then NO: the pattern plays a random variation of itself (shuffles, ghost notes, drops, accents), new each loop; let go and it's back. Any knob sets how wild | core, seq-events |
| MUTE SNAPSHOTS | `mutesnap-0.2.elemod` | YES + trig 1–4 saves the mutes, NO + trig 1–4 recalls them at the end of the bar | core, seq-events |
| TRACK SOLO | `tracksolo-0.2.elemod` | TRK + UP solos the active track while held, TRK + DOWN latches it (after the Octatrack) | core, seq-events |

### Sequencing

| Mod | File | What it does | Needs |
|---|---|---|---|
| FREQSEQ | `freqseq-0.6.elemod` | A generative sequencer on every track: 15 of them, from a Turing Machine and Grids to Game of Life, sample roulette and Morse code | core, seq-events |
| POLYMETER | `polymeter-0.1.elemod` | Every track's length and speed on one page, with moving playheads and one-turn polymeter presets | core, seq-events |
| REVERSE | `reverse-0.5.elemod` | A chance per track that a trig plays its sample backwards | core, seq-events |
| DICE + AUTOPILOT | `dice-0.1.elemod` | Per track: roll a random rhythm (YES), or let AUTOPILOT change a step or two every few loops so the pattern slowly evolves. NO puts it back exactly as programmed | core, seq-events |
| MIDI ARP | `midiarp-0.2.elemod` | An arpeggiator on each MIDI track, in time: a trig's chord or the notes you hold, UP/DOWN/UPDN/RND, 1/16-1/4, 1-3 octaves | core, seq-events |
| EUCLID ACCENTS | `euclidacc-0.3.elemod` | A Euclidean rhythm accents the trigs you already have (1–16 accents per track, DEPTH sets how much) | core, seq-events |
| VELOCITY HUMANISE | `humanise-0.3.elemod` | Random velocity variation per track, set in SETTINGS (after the Analog Rytm) | core, seq-events |
| MULTITRIG | `multitrig-0.6.elemod` | In grid recording, hold a trig and tap TRK (whole track) or PAGE (that page), then turn a knob to edit all those trigs' locks at once | core |
| CTRL ALL | `ctrlall-0.2.elemod` | TRK + YES: a lock you set on a step goes to that step on every track (after the Analog Rytm) | core |

### Sound

| Mod | File | What it does | Needs |
|---|---|---|---|
| DJ FILTER | `djfilter-0.8.elemod` | A stereo master filter after the Electrix FilterFactory: one-knob DJ sweeps, resonance, BUZZ drive, an LFO or envelope follower, tempo sync | core, seq-events |
| DRIVE | `drive-0.6.elemod` | Per-track saturation before the filter (tape, tube, fuzz or wavefolder) and a Buchla-style low pass gate after it, struck by every hit | core |
| G-EQ | `geq-0.1.elemod` | A four-band EQ for the whole mix after the SSL G-series channel EQ: bells or shelves, proportional Q, a high pass | core, **digihealth** |
| SONIC MAX | `maximizer-0.1.elemod` | An enhancer for the whole mix after the BBE 822A Sonic Maximizer: the top arrives first and comes up when the mix lacks it | core, **digihealth** |
| GLUE | `glue-0.1.elemod` | A bus compressor for the whole mix after the SSL G-series centre-section compressor, with a moving-needle meter, parallel MIX and a side-chain high pass | core |

### LFOs and modulation

| Mod | File | What it does | Needs |
|---|---|---|---|
| LFO+ | `lfoplus-0.6.elemod` | 29 new LFO waves (incl. 16 shapes after the Jomox Alphabase, chaos waves and an envelope follower for ducking), slower speeds, Deja Vu's looping RND built in, and an LFO 3/3 page to restart an LFO on another track's trigs | core, seq-events |

### Display and settings

| Mod | File | What it does | Needs |
|---|---|---|---|
| VISUALS | `visuals-0.5.elemod` | A beat radar, a pet that lives on your music, screen effects (CRT, flashes, VHS, snow, scanlines), screensavers, a clock, a scope and live art with a storm | core, seq-events |
| DASH | `dash-0.2.elemod` | The active track's SRC, LFO, filter and amp values on one screen, as bars, with the last one that moved named | core |
| HELP | `help-0.1.elemod` | A mini manual on a master page: a card of keys and knobs for each mod in your build | core |
| REAL UNITS | `realunits-0.2.elemod` | Filter FREQ in Hz, envelope times in ms/s, LFO speed as a cycle length or Hz | core |
| MOD SETTINGS | `modsettings-0.1.elemod` | Optional: folds the RND LOOP (LFO+), HUMANISE, EUCLID ACCENTS and MIDI ARP rows in SETTINGS into one MOD SETTINGS row | core |
| PROJECT LINK | `projlink-0.1.elemod` | A helper: with it, FREQSEQ, SCENES, DRIVE, REVERSE, DICE, SONIC MAX, G-EQ and GLUE keep their settings with each project (defaults for a new one) | core |

---

## What you need

- **Elekloader's `core`**, version 4.4 is in the release. It's also in elekloader's own releases.
- **`seq-events`** for every mod marked so above. It's in the shop, from the
  [FX LOCKS](https://github.com/ceredmeat/digitakt-fx-locks) release.
- **DIGIHEALTH** (irpina's mod, in the Elekloader shop) for SONIC MAX and
  G-EQ: its FAST AUDIO memory runs their busiest code. It's a good idea in any build with the master
  effects.
- **A Digitakt MKI on OS 1.54.** OS 1.53 isn't supported.

---

## Good to know

- **Mix and match:** every mod works on its own and together with the others.
- **Memory:** with many of these in one build, the OS gets close to its memory limit. If Elekloader
  says the mods don't fit, leave out the ones you don't use. VISUALS (about 11 KB) and FREQSEQ are
  the biggest.
- **Key combinations:** several mods use TRK + a key. They're chosen not to clash: TRK + UP/DOWN
  is TRACK SOLO, TRK + YES is CTRL ALL, TRK + NO is KIT A/B, TRK + FUNC is SCENES, TRK + PAGE is FREQSEQ, 
  NO + PAGE is TRIG MODES, and YES then NO (held) is FILL.
- **Master pages:** DJ FILTER, G-EQ, SONIC MAX, GLUE, DENSITY, DRIVE, REVERSE, DICE, POLYMETER,
  VISUALS, DASH and HELP live after the stock master pages: press FUNC + LFO, then step with
  LEFT/RIGHT. Hold FUNC + LFO for half a second to go straight back to the page you used last.

All the mods were tested on a real Digitakt MKI running OS 1.54; each mod's README says what was
tested. No `.syx` is shared here: it would
contain Elektron's firmware.

---

## Deja Vu is now part of LFO+

Since LFO+ 0.6, the separate DEJA VU mod is gone: its looping RND, SETTINGS rows and RND destinations
are built into LFO+, and saved settings carry over. Don't put an old `dejavu` file in the same build:
Elekloader refuses the pair.

---

## Licence

GPL-2.0-or-later (see LICENSE), except **FREQSEQ**, which is GPL-3.0-or-later because of the Grids
drum maps (see `mods/freqseq/LICENSE`). LFO+ includes MIT-licensed parts from Arvid Solander's
DEJA VU (see `mods/lfoplus/NOTICE-MIT.txt`). `mods/common/` holds the shared code several mods include: `mpui.h` (page drawing), `heapbuf.h`
(save buffers), `projsave.h` (settings per project) and `fmul.h` (fast fixed-point maths).
