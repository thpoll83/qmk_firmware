# split72 boot / idle animation — design previews

Offline mockups of the proposed demoscene-style **boot** ("wow") and **idle**
(anti-burn-in) animations, rendered on the *real* per-keycap OLED layout so the
look can be signed off before firmware work.

## Generate

    python3 -m venv env && env/bin/pip install numpy pillow
    env/bin/python preview_boot_anim.py --mode boot --out boot_preview.gif
    env/bin/python preview_boot_anim.py --mode idle --out idle_preview.gif

Useful flags: `--frames N`, `--fps N`, `--scale S`, `--downscale F`, `--font PATH`.

## Fidelity

- Key positions are the real `g_led_config` coordinates (from
  `doom/tools/keycap_dispmap.py` `POS_RAW`), so the stagger, stacked thumbs and
  split-gap are the true geometry. Splash letters are placed on the logical
  display grid exactly like `show_splash_screen` (`POLY`/`KYBD`, `SPLIT`/`7 2`).
- Every panel is native 72x40, 1-bit, thresholded through the **same 4x4 ordered
  Bayer matrix** the firmware blitter uses (`doom_blit.c`).
- The effect is sampled in one continuous board space (the "render locally from a
  shared clock" model), so sparkles/plasma flow across panels.

**This is a design mock, not the firmware renderer.** It uses numpy/floats for
convenience; the firmware port would be fixed-point (`sin8`/`cos8`) + the same
dither. `*.gif`/`*.png` outputs are git-ignored — regenerate as needed.
