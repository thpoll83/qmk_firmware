# base/

Logic that the host unit tests can link. Most files here have no `quantum.h`, no
display and no keyboard config. The firmware binding of each one is in another folder
(`keys_com/`, `lib/`, `features/`), and that binding passes in the clock, the flash
pointer or the I/O callbacks. The firmware compiles most `base/*.c` files through
`POLY_SRC` in `../rules.mk`. `update.c`, `com.c`, `text_helper.c`, `overlay.c` and
`fontpack.c` are listed in each variant's `rules.mk` (`../split72/rules.mk`,
`../split42/rules.mk`). A few files here are not pure (see *Core concepts*).

## Files

Macros:

- `macro_decode.c` / `macro_decode.h`: decodes the dynamic-macro buffer in QMK's
  send-string encoding (`poly_macro_find`, `poly_macro_used`, `poly_macro_decode`,
  `poly_macro_buffer_intact`). Used by `keys_com/poly_macro.c`.
- `macro_record.c` / `macro_record.h`: records a macro on the keyboard. The encoder
  (`poly_macro_rec_begin`, `poly_macro_rec_key`, `poly_macro_rec_finish`) and the splice
  into the shared buffer (`poly_macro_commit_begin`/`_step`/`_done`). Bytes go through
  reader/writer callbacks, and the caller passes the clock.
- `combo_store.h`: header-only. The runtime combo and tap-dance record (HID cmd 48,
  5 × uint16), its validation (`poly_combo_rec_valid`, `poly_td_rec_valid`) and the
  tap-dance decision (`poly_td_plan`). The flash sectors and QMK hooks are in
  `keys_com/poly_combos.c`.
- `tap_terms.h`: header-only. Ranges and EEPROM encoding of the tapping term, the Flow
  Tap term and Auto Shift (HID cmd 44): `tapping_term_valid`, `tap_term_pack`,
  `auto_shift_pack`.
- `caps_word_letter.h`: header-only. Caps Word's letter test (`legend_first_char`,
  `is_case_pair`), used by `caps_word_press_user()` in `keys_com/poly_keymap.c`.

Legends and fonts:

- `legend_plan.c` / `legend_plan.h`: the main-legend size planner (HID cmd 34). Moves
  codepoints into the latinbig tiers and clamps origin and baseline
  (`legend_plan_main`, `legend_plan_remap`, `legend_plan_clamp`). The font lookup and the
  bbox measurement come in as callbacks from `poly_keymap.c`.
- `font_lookup.c` / `font_lookup.h`: the glyph resolver and the display-list bounding
  box, taken out of `lib/drivers/disp_array.c` (`kdisp_gfx_glyph_font`,
  `kdisp_gfx_text_bbox_in`, `kdisp_gfx_text_bbox_abs_in`). `disp_array.h` re-exports it.
- `glyph_meta.h`: header-only `static inline` accessors for column-native GFX glyphs
  (`glyph_x_offset`, `glyph_y_offset`, `glyph_pixel_lit`, `glyph_col_bytes`). They read
  the signed offsets as `int8_t`.
- `glyph_place_op.h`: header-only. Decodes the placement op byte of HID cmd 30
  (`glyph_place_op_decode`). 0 means keep, 1 centre, 2 top-left.
- `text_helper.c` / `text_helper.h`: status-OLED text for RGB effects and hues
  (`get_led_matrix_text`, `get_hue_name`, `hue_to_degrees`). Includes `QMK_KEYBOARD_H`,
  so no unit test links it.
- `status_brightness.h`: header-only. Maps the keycap contrast scale onto the status
  panel register (`poly_status_brightness`). Includes `config.h`.

Emoji, icons and font packs:

- `fontpack.c` / `fontpack.h`: the "PlyF" font-pack format (position-independent, byte
  offsets from the pack base) and its loader (`fontpack_load_at`, `fontpack_assemble`,
  `fontpack_bundle_version`, `fontpack_reload`). The resident fonts stay in the firmware.
- `icon_lib.c` / `icon_lib.h`: the "PlyI" overlay icon library (HID cmd 42). Uses the PlyF
  byte layout with magic "PlyI" and row-major bitmaps (`iconlib_load_at`, `iconlib_blit`,
  `iconlib_fill_pairs`).
- `emoji_dict.c` / `emoji_dict.h`: loader and keyword search of the `emojidict` bundle
  (`emjdict_load_at`, `emjdict_search`, `emjdict_letter_code`). The reference
  implementation is `features/emoji/emoji_dict.py`.

Tutorial, demo and idle plans:

- `tutorial_plan.c` / `tutorial_plan.h`: phase machine, fade curve, ripple envelope and
  letter choice of the first-run tutorial (`tut_tick`, `tut_press`, `tut_ripple_radius`,
  `tut_fade_contrast`). The binding is `features/anim/tutorial.c`.
- `demo_plan.c` / `demo_plan.h`: playlist and timeline of the showroom demo (KC_DEMO,
  KC_DEMO_KEYS): `demo_locate`, `demo_type_keys`, `demo_host_usage`. The binding is
  `features/anim/demo_mode.c`.
- `idle_poem_plan.c` / `idle_poem_plan.h`: the poems of the split72 status idle screen,
  their typing and Backspace timeline and the random pick (`idle_poem_frame`,
  `idle_poem_pick`). The binding is `lib/display/status_idle.c`.
- `host_tutorial_plan.c` / `host_tutorial_plan.h`: the pure part of HID cmd 49 (host
  tutorial): text, spotlight and condition parsing, matching, the watchdog
  (`host_tut_parse_arm`, `host_tut_note_key`, `host_tut_expired`). The binding is
  `features/anim/host_tutorial.c`.
- `attn_guide.c` / `attn_guide.h`: the GUIDE of HID cmd 47 op 2. Parses up to
  `ATTN_GUIDE_MAX_STEPS` steps and picks the step for the active layer
  (`attn_guide_parse`, `attn_guide_select`). The binding is `features/anim/key_attention.c`.
- `mines_plan.c` / `mines_plan.h`: the rules of the idle-screen mine sweeper
  (`mines_reveal`, `mines_chord`, `mines_check_win`). The binding is
  `features/modes/mines_mode.c`.
- `ai_events.c` / `ai_events.h`: the pure part of AI mode (HID cmd 45/46): the event ring
  (`AI_RING_SIZE` 8), the key-index mapping and the checks on op 1 (`ai_ring_push`,
  `ai_key_index`, `ai_set_keys_valid`). The binding is `features/modes/ai_mode.c`.
- `idle_style.c` / `idle_style.h`: the idle styles of HID cmd 28 (`enum poly_idle_style`:
  PULSE, JITTER, IDDQD, EDEN) as one table, and `idle_style_next_in_cycle`.
- `idle_timeout.h`: header-only. The closed set of idle-timeout presets (HID cmd 40) and
  their EEPROM encoding (`idle_timeout_ms_of`, `idle_timeout_pack`, `idle_timeout_unpack`).
- `update.c` / `update.h`: the idle-activity timestamp (`update_performed`,
  `get_last_update`, `disable_idle_tracking`). Needs only the QMK timer.

Firmware update verdicts:

- `fw_up_verdict.c` / `fw_up_verdict.h`: classifies a COMMIT that did not return
  `SYNC_ACK` as bad data or a lost answer (`fw_up_classify_commit_failure`,
  `fw_up_status_crc_ok`, `fw_up_combined_bundle_version`). `lib/fw_update/split_fw_up.c`
  keeps the RPC and the logging.
- `fw_stage.c` / `fw_stage.h`: the stage machine of an fw_staging stream
  (`FW_STAGE_IDLE`, `_ERASING`, `_RECEIVING`; `fw_stage_next`, `fw_stage_accepts_chunk`).
- `fw_sig_policy.h`: header-only. `fw_sig_key_provisioned` refuses the all-zero
  placeholder key before the Ed25519 check is called.
- `fw_pubkey.h`: the Ed25519 public key `FW_SIGNING_PUBKEY`. Generated by
  `dev/tools/gen_signing_key.py`. Do not edit it by hand; re-run that tool to rotate the
  key.
- `sync_ack.h`: header-only. The split-link ack vocabulary (`SYNC_ACK`, `SYNC_ACK_SIG`,
  the failure values) and `sync_succeeded`, `sync_is_link_fault`, `sync_retry_may_help`.
  `lib/split_link/split_sync.h` re-exports it.

core1:

- `core1_suspend.c` / `core1_suspend.h`: holds core1 in reset during a USB suspend and
  records a stall when core1 still owes work after `CORE1_SUSPEND_STALL_MS` (500 ms)
  (`core1_suspend_step`, `core1_resume_step`). The hardware side is
  `lib/display/multicore_exec.c`.

IME:

- `ime_key_plan.c` / `ime_key_plan.h`: what KC_IME sends per language and OS: NUBS, Right
  Alt, the Korean toggle or a Japanese absolute key (`ime_key_stroke`, `ime_ja_next`).
- `ime_held_slots.c` / `ime_held_slots.h`: per-key-position table of the usage each held
  KC_IME press registered (`IME_HELD_SLOTS` 4; `ime_slots_press`, `ime_slots_release`).
  QMK access goes through `ime_slot_io_t`.

Overlays, the HID wire format and shared flags:

- `overlay.c` / `overlay.h`: the keycap image pool (`overlay_pool`, `NUM_OVERLAY_SLOTS`
  600 slots of 360 bytes), the `display_to_pool` map and the has-overlay, dim and
  no-legend bits (`get_overlay`, `set_display_pool_slot`, `copy_rectangle_to_overlay`).
  Includes `config.h` and `disp_array.h`, so it is firmware-only.
- `map_codec.h`: header-only. The packed `width`-bit codec of the overlay mapping (HID cmd
  21 and 33, slave-repair frames): `map_codec_read`, `map_codec_write`. Used by
  `lib/display/fill_overlay.c`.
- `prc_record.c` / `prc_record.h`: parses the 6-byte record header of HID cmd 41
  (`prc_parse_record`). The image codec is `modules/polykybd/polymod_prc`.
- `com.c` / `com.h`: the `enum poly_flag` and `enum overlay_flag` bits, the cmd 13
  brightness flags (`BR_FLAG_*`) and the flag helpers (`test_flag`, `set_flag`,
  `flag_turned_on`).
- `host_unicode_mode.h`: header-only. Decodes the cmd 20 unicode input mode values and the
  MacCompose flag (`host_unicode_mode_decode`).

Other:

- `hand_stamp.c` / `hand_stamp.h`: handedness in its own flash sector
  (`FW_HAND_STAMP_OFFSET`), so it survives an EEPROM wipe. `poly_hand_decide` (static
  inline in the header) is the pure ordering. `hand_stamp.c` does the flash I/O
  (`poly_hand_boot_init`, `poly_hand_flush_pending`, `poly_hand_force_stamp`) and needs
  `quantum.h`.
- `crash_ack.h`: header-only. `crash_ack_slave_fresh` decides when the slave's crash
  record stops reading fresh after the host read it over cmd 39.
- `mode_byte.h`: header-only. The one-byte "flag + known + 6-bit value" EEPROM encoding
  used by auto brightness and OS state in `lib/system/state.c` (`mode_byte_pack`).
- `bitset.h`: header-only packed bit array (`BITSET_BYTES`, `bitset_get`, `bitset_put`).
  No bounds checks.

## Subfolders

- [`tests/`](tests/README.md): the host googletest suites for this folder and the suite
  registration (`testlist.mk`, `test_rules.mk`).

## Core concepts

- The pattern is a pure decision function here and a thin binding elsewhere. The binding
  owns the timer, the flash, the matrix and the logging, and passes values in. A test
  links only the `base/` file (and `polymod_crc32` where a CRC is checked).
- Not every file is pure. `hand_stamp.c`, `text_helper.c` and `overlay.c` need QMK or
  the keyboard config, and no suite links them. `update.c` needs the timer; its suite
  links the mock clock `platforms/test/timer.c`.
- Both halves run the plans. `demo_plan.c` functions take "ms into the segment" because
  the two MCUs share no time base. The mine board (`mines_board_t`) is inside
  `poly_sync_t`: the master changes it and the split sync carries it to the slave.
- Several files define values that are on the HID wire or in EEPROM: the cmd 44 tap
  terms, the cmd 40 idle presets, `enum poly_idle_style` (append-only, also in
  `poly_sync_t.idle_style`), the mode byte. An unwritten EEPROM byte reads back as zero,
  so each encoding makes zero the factory default.
- Flash, not EEPROM: the combo store (`FW_COMBO_STORE_OFFSET`) and the handedness stamp
  (`FW_HAND_STAMP_OFFSET`) use sectors at the top of the staging region.
- Build switches: `host_tutorial_plan.c` is compiled only outside the DOOM monolith
  (`-DPOLY_HOST_TUTORIAL` in `../rules.mk`).
- Tests: `make test:<suite>` from the repo root, for example `make test:fw_up_verdict` or
  `make test:polykybd_macro_record`. The list is in [`tests/README.md`](tests/README.md).

## Important

- ⚠️ A file a suite links must build on the host: no `quantum.h`, ChibiOS or pico-sdk
  headers. `config.h` works only where the suite puts `keyboards/polykybd` on its include
  path (`status_brightness`). `legend_plan.h` keeps its own size indices instead of
  including `state.h`, and `poly_keymap.c` checks them against `state.h` with
  `_Static_assert`.
- ⚠️ `prc_record.h` has one format with the host's `polyhost/util/prc_codec.py`
  `pack_record()`. Change both together.
- ⚠️ `map_codec.h`: the second and third byte reads depend on the width. A read must
  never touch a byte past the value. The guard-byte tests check this; do not verify a
  change by eye.
- ⚠️ `sync_ack.h`: a new ack value must keep the minimum pairwise Hamming distance of 4.
  `SyncAckTest.AckValuesStayHammingSpaced` fails otherwise.
- ⚠️ `fw_sig_policy.h`: the all-zero key is a valid low-order curve point, so a forgery is
  easy. It is refused before the crypto runs. Do not remove that check.
- ⚠️ `font_lookup.c` and `legend_plan.c`: adding a display-list op needs the same change
  in the draw walker, the bbox walker and PolyKybdHost's `tools/oled_preview.py`
  (`.claude/rules/display.md`).

## Related

- [`../docs/build/unit-tests.md`](../docs/build/unit-tests.md)
- [`../docs/display/legend-rendering.md`](../docs/display/legend-rendering.md)
- [`../docs/display/font-pack.md`](../docs/display/font-pack.md)
- [`../docs/design/overlay-icon-library.md`](../docs/design/overlay-icon-library.md)
- [`../docs/features/emoji-search.md`](../docs/features/emoji-search.md)
- [`../docs/features/macros.md`](../docs/features/macros.md) and
  [`../docs/design/macro-record.md`](../docs/design/macro-record.md)
- [`../docs/features/tutorial.md`](../docs/features/tutorial.md),
  [`../docs/features/demo-mode.md`](../docs/features/demo-mode.md),
  [`../docs/features/mines.md`](../docs/features/mines.md)
- [`../docs/display/idle-styles.md`](../docs/display/idle-styles.md)
- [`../docs/fw-update/fw-staging.md`](../docs/fw-update/fw-staging.md) and
  [`../docs/fw-update/signing.md`](../docs/fw-update/signing.md)
- [`../docs/protocol/split-sync.md`](../docs/protocol/split-sync.md)
