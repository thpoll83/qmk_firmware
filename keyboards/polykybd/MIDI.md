# USB MIDI and the MIDI layer

The keyboard is also a class-compliant USB MIDI input. The MIDI layer `_ML` turns it
into a small piano with live note names on the keycaps. It is reached with `TO(_ML)`
(the treble-clef key on the utility layer) and left with `KC_BASE`.

## What is built

| part | where |
|---|---|
| switch | `rules.mk` (`POLYKYBD_MIDI`, on), `config.h` (`MIDI_ADVANCED`, `MIDI_SEND_ONLY`, `MIDI_STREAM_IN_CAPACITY 2`) |
| send-only interface | QMK core patch, `UPSTREAM_PATCHES.md` → `MIDI_SEND_ONLY` |
| layer | `layers.h` (`_ML` = stored layer 8), both keymaps, `layer_names.c` ("MIDI") |
| legends | `midi_legend.c` (pure, unit-tested: `make test:polykybd_midi_legend`) |
| symbols | `tools/midi_glyphs.py` → `base/fonts/gfx_icons.h` U+100029..U+100049 and the `ICON_MIDI_*` macros in `lang/named_glyphs.h` |
| sync | `poly_sync_t.midi_cfg`, packed by `midi_cfg_pack()` |

- **MIDI is off in the DOOM monolith** (`POLYKYBD_DOOM=yes` without `POLYKYBD_DOOM_PACK`).
  That build only produces the engine objects for the `.plyx` pack, and it has the
  least RAM. `midi_legend.c` is not compiled there either; the `_ML` keys draw blank.
- ⚠️ **`rules.mk` assigns `MIDI_ENABLE` on every path.** QMK's data-driven step reads
  `rules.mk` without evaluating conditionals, saw `MIDI_ENABLE = yes` and generated
  `MIDI_ENABLE ?= yes` in `.build/…/info_rules.mk`, which put MIDI back into the
  monolith until the explicit `MIDI_ENABLE = no`.

## Notes and octaves

`MI_C` plays middle C (MIDI 60, shown as C4). QMK starts at octave index 4, where
`MI_C` is C3, so `keyboard_post_init_user()` starts one index higher. Every octave a
keycap shows is in scientific pitch notation, including the `OCT` presets: `MI_OC3`
reads "OCT 4" because it makes `MI_C` play C4. A note legend includes the transpose,
so it always names the note the key sends.

## The slave half

Only the master runs `process_midi()`, so the slave never sees `midi_config`. The
master packs octave, transpose, velocity and channel into `poly_sync_t.midi_cfg` every
housekeeping pass; a change is a state diff and redraws both halves. The word is packed
by hand rather than copied from QMK's `midi_config_t` bitfield union, whose layout is
the compiler's choice. Bit 31 marks it as set; a slave that has not had its first sync
draws QMK's start values.

## The symbols

Each symbol is an `IconsFont` glyph that carries its own position on the keycap
(xOffset, yOffset) and has xAdvance 0. A legend draws the symbol first and its text
after it. Change a symbol in `tools/midi_glyphs.py` and rerun it; `--check` fails when
a header is out of date and `--sheet out.png` shows every glyph on a keycap. Dynamics,
flat, sharp, the clef and the text marks are rendered from GNU FreeFont FreeSerif
(GPLv3 with the font exception, Debian `fonts-freefont-ttf`); the staff, hairpins,
slur, glide, wave and bend curves are drawn by the script.

The firmware draws a static legend in two passes (`kdisp_write_gfx_text_cy`), so a
glyph's courtyard never removes ink from its own legend. What must hold is that no
glyph leaves the 72x40 panel and that every codepoint is in a font; both were checked
for all 144 keycodes at three settings with `PolyKybdHost/tools/oled_preview.py`.

## Cost

Measured on split72 with local gcc 13.2 (`tools/ram_report.py`), against the build
without MIDI:

| flavour | RAM headroom | flash |
|---|---|---|
| default | 9052 → 8492 B | +8.6 KB |
| DOOM pack (shipped) | 8132 → 7580 B | +8.6 KB |
| DOOM monolith (no MIDI) | 3824 → 3816 B | +3.5 KB |

Of the 560 B: 448 B is the USB MIDI interface (QMK's default MIDI would be 1184 B),
100 B the legend buffer, 8 B the synced settings word. The `_ML` layer also took 160 B
of keymap and 8 B of encoder map from the macro buffer.
