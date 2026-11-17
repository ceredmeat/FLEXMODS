# flexmods

Elekloader mods for the **Elektron Digitakt MKI, OS 1.54**, made by Cere (with Claude). Each mod is
its own `.elemod`: pick the ones you want in Elekloader, build your OS and flash it.

| Mod | File | What it does | Needs |
|---|---|---|---|
| LFO+ | `lfoplus-0.5.elemod` | 22 new LFO waves after RND (incl. 16 shapes after the Jomox Alphabase), each with its own picture on the LFO page, plus slower speeds | core |
| Deja Vu | `dejavu-0.5.elemod` | The RND LFO plays a repeating loop of random values (RND LOOP), with RND LOCK / RND LOOP as LFO destinations | core |
| Kit A/B | `kitab-0.2.elemod` | TRK+NO flips between your edited kit and the pattern's temp-saved kit (after the Analog Rytm mod) | core |
| Ctrl All | `ctrlall-0.2.elemod` | TRK+YES: a lock you set on a step goes to that step on every track (after the Analog Rytm mod) | core |
| Real Units | `realunits-0.2.elemod` | Filter FREQ in Hz, envelope times in ms/s, LFO speed as a cycle length or Hz | core |
| Mute Snapshots | `mutesnap-0.1.elemod` | YES+trig 1-4 saves the mutes, NO+trig 1-4 recalls them at the end of the bar | core, seq-events |
| Track Solo | `tracksolo-0.2.elemod` | TRK+UP solos the active track while held, TRK+DOWN latches it (after the Octatrack mod) | core, seq-events |
| Velocity Humanise | `humanise-0.2.elemod` | Random velocity variation per track, amount set in SETTINGS (after the Analog Rytm mod) | core, seq-events |
| Euclid Accents | `euclidacc-0.2.elemod` | A Euclidean rhythm accents the trigs you already have (1-16 accents per track, DEPTH sets how much); the pattern never changes (after the Analog Rytm mod) | core, seq-events |
| MULTITRIG | `multitrig-0.5.elemod` | In grid recording, hold a trig and tap TRK (whole track) or PAGE (that page), then turn a knob to edit all those trigs at once; SETTINGS > MULTI EDIT: REL moves them together (transpose), ABS gives them the held trig's value | core |
| MOD SETTINGS | `modsettings-0.1.elemod` | Optional: folds the Deja Vu, Humanise and Euclid Accents rows in SETTINGS into one MOD SETTINGS row | core |

The `.elemod` files are in the [releases](../../releases). The source of each mod, with a README
explaining how to use it, is in `mods/<name>/`.

All of these mods were tested together on a real Digitakt MKI running OS 1.54. OS 1.53 is not
supported. No `.syx` is shared here: it would contain Elektron's firmware.

Licence: GPL-2.0-or-later (see LICENSE).
