# MOD SETTINGS section (Digitakt MKI, OS 1.54)

Folds the SETTINGS rows of **Deja Vu**, **Velocity Humanise** and **Euclid Accents** away under
one row, so SETTINGS stays short (just like LED COLORS does for the LED rows).

## How to use it

- In **SETTINGS**, find the **MOD SETTINGS** row. It shows **CLOSED**.
- Press **YES** on it, or turn the knob right: it shows **OPEN**, and the RND LOOP / LOCK / TRIG,
  HUMAN T1–MIDI and ACC DEPTH / T1–T8 rows appear under it.
- Press **YES** again, or turn left, to hide them.
- It starts **CLOSED** each time you power on. Your values inside are still saved as usual.

## Good to know

- If you build without this mod, the Deja Vu / Humanise / Euclid rows just show in SETTINGS as
  before. Nothing breaks.
- Needs Deja Vu 0.3, Humanise 0.2 and Euclid Accents 0.2 or newer. Older versions ignore it and
  always show their rows.

## How it works (plain version)

This mod owns one on/off switch, "closed". The other three mods each check that switch before
adding their rows. They mark it as optional, so if MOD SETTINGS isn't in the build, the switch
reads as "not closed" and their rows show normally.

When you flip it, the mod asks the Digitakt to rebuild the SETTINGS list. It uses the same
rebuild routine the stock firmware uses when a setting changes what's in the list, and the
selected row stays where it was.
