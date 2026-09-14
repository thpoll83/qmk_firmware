# Glyph-script override

Moved out of `CLAUDE.md` 2026-09-14. Verbatim.

### Glyph-script override (`poly_keymap.c`, HID cmd 30, protocol v9+; expanded v10)
An OS-independent **override** of the language-layer legends with an alternative
script (fantasy / retro). State: `poly_eeconf_t.glyph_script` (persisted, appended
tail byte like `os_state`; `EECONFIG_USER_DATA_SIZE` grew 64→65, still ≤ the 128-byte
`POLY_EECONFIG_USER_RESERVED` so **no keymap relocation / user reset**) +
`poly_sync_t.glyph_script` (master-authoritative, synced like `active_os`;
`housekeeping_task_user()` sets it and `request_disp_refresh()`s on change). `enum
poly_glyph_script` in `state.h` — append-only: `GLYPH_STD=0`, `GLYPH_TENGWAR=1`, then
the v10 expansion `GLYPH_RUNES=2, GLYPH_AUREBESH=3, GLYPH_SGA=4, GLYPH_CIRTH=5,
GLYPH_IBMVGA=6, GLYPH_C64=7, GLYPH_AMIGA=8, GLYPH_APL=9, GLYPH_BRAILLE=10`.
- **Open-ended index (v10+): cmd 30 accepts ANY value `0..0xFE`; unknown → normal.**
  `set_glyph_script()`/`note_glyph_script()`/`load_user_eeconf()` store the byte
  verbatim (only the erased-EEPROM `0xFF` maps to `GLYPH_STD`); `hid_com.c` case 30 no
  longer NACKs an out-of-range index. `glyph_script_codepoint()` returns 0 for any
  `script >= GLYPH_SCRIPT_COUNT`, so an index this firmware doesn't know falls through
  to the normal legend (same path as a known script whose font isn't flashed). This is
  what lets the host offer scripts a given keyboard lacks and lets **new font faces ship
  without a protocol bump** — DON'T re-add a range NACK. Storing verbatim also means a
  choice made before the matching font-pack update survives it. Adding a `GLYPH_*` value
  therefore needs NO `PROTOCOL_VERSION` change — just the enum entry, the
  `glyph_script_blocks[]` row, the font, and the host `GlyphScript`/label.
- **Render hook — one choke point in `render_key()`** (`poly_keymap.c`): right after
  `local_state` is fetched, when `glyph_script != GLYPH_STD` and the key is a plain
  letter/digit on the normal layer (not the `_ADDLANG1` latin-variation layer), it
  draws the override glyph centered and **returns**, so it replaces the *whole* base
  legend — including the unshifted view's shift-preview (Tengwar is caseless, so the
  shift preview is deliberately dropped). Overlays and OS-hints
  (`keycode_to_disp_overlay`) are drawn on **separate paths** (`update_displays` /
  overlay memory) and are genuinely untouched. Two fall-throughs to the real legend:
  when an **AltGr** key is held (`mods & MOD_RALT` — the AltGr symbol is a different
  character, not a cased letter, so it wins), and when the glyph isn't in `g_all_fonts`
  (the `fantasy` bundle isn't flashed), so a pack-less keyboard shows Latin, never blanks.
- **Codepoints are relocated, NOT native.** The `flags` bundle already occupies the
  CSUR PUA `0xE000+`, so raw script codepoints would render a language flag. Each
  script's font is emitted (fontconvert sequence `-F` remap, `fonts.yaml`) into its
  own **dense private PUA block** matching `glyph_script_blocks[]` (a table indexed by
  `poly_glyph_script`) in `poly_keymap.c`: Tengwar `0xE800`, Runes `0xE840`, Aurebesh
  `0xE880`, SGA `0xE8C0`, Cirth `0xE900`, IBM VGA `0xE940`, C64 `0xE980`, Amiga `0xE9C0`,
  APL `0xEA00`, Braille `0xEA40` (0x40 apart). Letters `a..z` → `base+0..25`; scripts
  with their own numerals (`digits:true`) put `1..0` at `base+26..35`, others leave the
  digit keys as the normal numeral (runes/Aurebesh/Cirth have no native numbers). The
  per-key glyph choice lives only in the font's generation sequence, so the firmware
  just needs the base + dense index.
- **Fonts** (all in the `fantasy` bundle; keep user-facing strings generic — trademark
  caveat on the fictional scripts, though the *fonts* are fine to embed): Tengwar =
  Alcarin (OFL, no Noto Tengwar exists); Runes = Noto Sans Runic (OFL); Aurebesh /
  Cirth = GNU Unifont CSUR (GPL + font-embedding exception; kept on the blocky 16 px
  bitmap because no license-clean smooth outline font exists for those CSUR blocks —
  the free Aurebesh/Cirth outline fonts are personal-use-only); APL / Braille = DejaVu
  Sans (Bitstream Vera + Arev, permissive — smooth outline, replacing Unifont's 16 px
  bitmap; the APL quad U+2395, absent from DejaVu, maps to U+25A1 □); SGA = the CC0
  `standardgalactic/alphabet` font; IBM VGA/CP437 = VileR PxPlus (CC-BY-SA-4.0, Debian
  `fonts-pc`); C64 = KreativeKorp **PetMe64** (KSRFL, solid ROM font — the OFL
  Homecomputer "Sixtyfour" was rejected for its baked-in CRT scanlines); Amiga = OFL
  Homecomputer "Workbench" (Debian `fonts-amiga`; scanline look kept for a clean
  license — solid Topaz conversions were license-uncertain). ZX Spectrum was dropped
  (no license-clean font found). Sources fetched by `fonts/dl-fonts.sh` (google/fonts
  + CC0 raw URLs; the Debian-packaged ones via `apt-get download` + `dpkg-deb -x`, no
  root). Host: HID cmd 30 in `PolyKybd.get/set_glyph_script`, tray "Glyph Script"
  submenu (`GLYPH_SCRIPT_LABELS`) + a "Reset glyph script to Standard" button in the
  settings dialog; `polyctl glyph-script [standard|tengwar|runes|…|braille]`. Rig:
  `test_glyph_script_round_trip` (`min_protocol: 9`) + `test_glyph_script_expansion`
  (`min_protocol: 10`, walks values 2/6/10 + out-of-range NACK).

