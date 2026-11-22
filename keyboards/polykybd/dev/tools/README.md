# dev/tools/

The Python tools for the PolyKybd firmware. They generate glyph headers, check that
generated tables and the source agree, draw previews of the displays, sign release
images and gate a release. Developers run most of them by hand. CI runs the signing
tools, `ram_report.py`, `require_fwapply_run.py` and `make_hand_uf2.py`, and runs
the tests in [`tests/`](tests/README.md). Each file's docstring has its exact command
line.

## Files

### Glyph and icon generators

These write C source that the firmware compiles. Each one is the only source of the
block it writes. Edit the tool, not the generated block.

- `ai_key_icon.py`: draws the AI key legend (`ICON_AI_KEY`, IconsFont U+100029) as a
  full 72x40 glyph. `--c` prints the `IconsBitmaps` block and glyph row; `--png`
  writes a preview from the same drawing.
- `midi_glyphs.py`: draws the MIDI keycap symbols and writes them into IconsFont in
  `fonts/gfx_icons.h` (between BEGIN/END markers) and their macros into
  `lang/named_glyphs.h`. `--check` exits 1 if a block is stale; `--sheet` writes a
  contact sheet.
- `hint_icons.py`: draws the OS shortcut-hint icons on a 34x34 grid and writes the
  `PolyHintIcons` font (U+100100 onwards) to `fonts/hint_icons.h`. It also removes
  any old hint block from the resident IconsFont in `fonts/gfx_icons.h`.
  `--check` also compares the
  `ICON_HINT_*` block in `lang/named_glyphs.h` with `--macros`. `--style a|b|c|d|e`
  selects the drawing style.
- `hint_icons_solid.py`, `hint_icons_bold.py`, `hint_icons_stage.py`,
  `hint_icons_retro.py`: styles b (solid), c (bold), d (stage and actor) and
  e (retro) for `hint_icons.py`. They only draw. `hint_icons.py` imports them and
  keeps the slot order.
- `gen_cascade_rows.py`: writes `features/anim/menu_cascade_rows.h`, the physical
  keyboard row of each keycap panel per half, taken from `keyboard.json`'s `y`.
  `--check` exits 1 if the header is stale.
- `gen_tutorial_names.py`: shapes the tutorial's native language names with
  HarfBuzz, renders them 1-bit with FreeType and writes 72x40 tiles to
  `features/anim/tutorial_names_gen.h`. Needs `--fonts <dir>` with the Noto `.ttf`
  files from `fonts/noto-fonts.yaml`, and `uharfbuzz`, `freetype-py`, `pillow`.
- `png_to_code.py`: Adafruit's image converter. Prints a 1-bit PNG as
  `<id>_width`/`<id>_height` and a `const uint8_t <id>_data[]` array of `0b` literals.

### Checks

Each check exits 1 on a failure, so it can gate a commit.

- `check_glyph_coverage.py`: walks every cell of `lang/lang_lut.c` and checks that
  each codepoint a legend draws is in a font, in the order of
  `fonts/generated/all_fonts_order.json`. CI runs it through
  `tests/check_glyph_coverage_test.py`.
- `check_disp_index.py`: replays the panel walk against the real keymap and checks
  that `key_display_index()` names the panel each key lands on. Manual, not in CI.
- `check_icon_slots.py`: reports which IconsFont codepoints in `fonts/gfx_icons.h`
  are taken, free or named wrong in `lang/named_glyphs.h`. `--free` prints the next
  free slot.
- `compare_codegen.py`: compares two ELFs function by function with address
  operands normalised away. Use it to prove that a code-motion refactor changed no
  generated code. Uses `arm-none-eabi-objdump`.
- `ram_report.py`: prints the RAM sections of one or more ELFs, the free RAM per bank
  (main RAM's `.heap` size is the headroom) and the `--top N` largest RAM symbols.
  `qmk-test.yml` runs it on the DOOM monolith and pack-flavour ELFs.

### Previews

- `status_oled_preview.py`: renders the split72 128x64 status OLED as
  `split72/status_oled.c` draws it, from the committed font headers. `--diag` marks
  every pixel outside the panel in red.
- `status_oled42_preview.py`: the same for the split42 portrait 32x128 layout of
  `split42/status_oled.c`.
- `tutorial_name_sheet.py`: renders the tutorial's language-name screens
  (`TUT_LANG_NAME`) with `PolyKybdHost/tools/oled_preview.py` and the tiles in
  `features/anim/tutorial_names_gen.h`.

### Signing and release

- `gen_signing_key.py`: generates the Ed25519 firmware-signing key pair. Writes the
  public key to `base/fw_pubkey.h` and the 32-byte seed to a file you keep secret.
  CI calls it with `--force` to bake an ephemeral key into test builds.
- `sign_firmware.py`: writes a detached 64-byte Ed25519 signature `<bin>.sig` over a
  firmware `.bin`. The host flasher sends it with the image.
- `sign_doompack.py`: appends a 64-byte Ed25519 signature over header and image to a
  DoomPack `.plyx`, in place, and verifies it before it writes.
- `make_hand_uf2.py`: builds a UF2 that writes one half's handedness stamp to the
  sector at `FW_HAND_STAMP_OFFSET` (`base/hand_stamp.h`). `--verify` checks a file.
- `require_fwapply_run.py`: the release gate. Refuses to publish unless a
  firmware-APPLY run of `qmk-test.yml` covers the release commit, or an ancestor
  whose delta is only the version bump. Needs `GH_TOKEN`, `REPO` and `SHA`;
  `--selftest` runs its offline checks.

## Subfolders

- [`hil_probes/`](hil_probes/README.md): one-off probes the HIL rig runs against a
  flashed keyboard (`qmk-test.yml`, `tier: debug`).
- [`tests/`](tests/README.md): offline `unittest` suites for these tools and for
  other Python generators in the keyboard.

## Core concepts

- Most tools are run from `keyboards/polykybd/`. The generators find their output
  paths from their own location.
- Most drawing tools need Pillow. The signing tools need `cryptography`. The ELF
  tools need the ARM binutils on `PATH`.
- The signing tools take the key as `--privkey <seed file>` (local) or as base64 in
  the `FW_SIGNING_KEY` environment variable (CI). Firmware built with
  `FW_REQUIRE_SIGNATURE` refuses an unsigned image or DoomPack.
- IconsFont (resident, U+100000 onwards) is in `fonts/gfx_icons.h`. `PolyHintIcons`
  is a pack font in the `symbol` bundle, so a hint change needs a bundle reship
  (`reship-fontpack-bundle` skill).
- `release.yml` builds the handedness UF2s with `make_hand_uf2.py` but does not
  publish them. Handedness is provisioned over HID cmd 25.

## Important

- ⚠️ Never commit the signing seed (`fw_signing_key.bin`). Only `base/fw_pubkey.h` is
  public.
- ⚠️ A hand-built UF2 must declare `payloadSize` 256 on every block, or the bootrom
  drops it. `make_hand_uf2.py` refuses to emit any other block; its test checks the
  bootrom's rules independently.
- ⚠️ Two PRs that extend IconsFont at the same time take the same codepoints. A
  generator starts after the font's last glyph that is not its own, and a test reads
  the codepoint from `lang/named_glyphs.h`, never a literal.
- ⚠️ `tutorial_name_sheet.py`'s `ITEMS` is a copy of `s_tut_preview_all[]` in
  `keys_com/poly_keymap.c`. Change both.
- ⚠️ Retiring a hint icon renumbers every slot after it. Re-paste the `ICON_HINT_*`
  block in `lang/named_glyphs.h` from `hint_icons.py --macros`.
- ⚠️ A gate that scans source must strip comments first, or it matches its own
  documentation.

## Related

- [`../../docs/display/hint-icons.md`](../../docs/display/hint-icons.md)
- [`../../docs/display/status-oled.md`](../../docs/display/status-oled.md)
- [`../../docs/display/display-pipeline.md`](../../docs/display/display-pipeline.md)
- [`../../docs/display/font-pack.md`](../../docs/display/font-pack.md)
- [`../../docs/features/midi.md`](../../docs/features/midi.md)
- [`../../docs/features/tutorial.md`](../../docs/features/tutorial.md)
- [`../../docs/fw-update/signing.md`](../../docs/fw-update/signing.md)
- [`../../docs/build/releases.md`](../../docs/build/releases.md)
- [`../../docs/build/ci-checks.md`](../../docs/build/ci-checks.md)
