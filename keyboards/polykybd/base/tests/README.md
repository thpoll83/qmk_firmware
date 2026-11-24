# base/tests/

The host googletest suites for the pure logic in `base/`, plus three suites for pure
files in other folders (`keys_com/layer_names.c`, `features/modes/midi_legend.c`,
`doom/doom_pack_gate.h`). Each suite links one source file (or only a header) and its
test file, with no keyboard, display or split link. CI runs every suite through
`.github/workflows/polykybd-unit-test.yml`.

## Files

- `testlist.mk`: registers each suite name with `TEST_LIST +=`.
  `builddefs/testlist.mk` includes it.
- `test_rules.mk`: the `<suite>_SRC` and `<suite>_INC` lists of each suite.
  `builddefs/build_test.mk` includes it by this name.
- `<name>_tests.cpp`: one test file per suite. The suites are listed below.

Suites (run with `make test:<suite>` from the repo root):

- `fw_up_verdict`: the COMMIT-failure classification (bad data or lost ack), the
  `sync_ack.h` vocabulary and its Hamming spacing, the STATUS CRC guard and the combined
  bundle version. Links `fw_up_verdict.c` and `polymod_crc32.c`.
- `polykybd_glyph_meta`: signed offsets and column-native pixel decode of
  `glyph_meta.h`, on a hand-written glyph that crosses a page boundary.
- `polykybd_macro_decode`: finding macro N, buffer use and step decoding of the
  send-string format (`macro_decode.c`).
- `polykybd_map_codec`: the packed `width`-bit overlay-mapping codec, with guard bytes
  against reads and writes past the value (`map_codec.h`).
- `polykybd_bitset`: `bitset_get` / `bitset_put` (`bitset.h`).
- `polykybd_prc_record`: the cmd 41 record header (`prc_record.c`), decoded end to end
  with `polymod_prc.c` against the module's golden vectors from the host encoder.
- `polykybd_mode_byte`: the flag + known + value EEPROM byte (`mode_byte.h`).
- `polykybd_glyph_place_op`: the cmd 30 placement op byte: keep, set, invalid
  (`glyph_place_op.h`).
- `polykybd_idle_update`: the idle-activity timestamp in `update.c` against the mock
  clock, including the bit-31 and early-boot underflow regressions. Links
  `platforms/timer.c` and `platforms/test/timer.c`.
- `polykybd_idle_timeout`: the cmd 40 presets and their EEPROM encoding
  (`idle_timeout.h`).
- `polykybd_tap_terms`: the cmd 44 ranges and the biased EEPROM encoding
  (`tap_terms.h`).
- `polykybd_latin_bits`: every pick and assignment field with every value, no bleed
  into the neighbouring fields, no write past the array, and the fallback of an
  unassigned key (`latin_bits.h`).
- `polykybd_caps_word_letter`: Caps Word's letter test (`caps_word_letter.h`).
- `polykybd_combo_store`: the cmd 48 combo and tap-dance records and the tap-dance plan
  (`combo_store.h`).
- `polykybd_layer_names`: the cmd 35 payload encoder in `keys_com/layer_names.c`:
  `[total][count]` then NUL-terminated names.
- `polykybd_midi_legend`: the MIDI keycap legend text for a packed settings word
  (`features/modes/midi_legend.c`).
- `polykybd_legend_plan`: the main-legend size planner and its clamps
  (`legend_plan.c`).
- `polykybd_font_bbox`: glyph lookup and the display-list bounding box
  (`font_lookup.c`) on synthetic in-memory fonts.
- `polykybd_macro_record`: the on-keyboard macro encoder and splice (`macro_record.c`).
  Links `macro_decode.c` too, because `poly_macro_rec_finish()` reads back what it wrote.
- `polykybd_hand_stamp`: the handedness ordering `poly_hand_decide()`. The flash I/O in
  `hand_stamp.c` is not linked.
- `polykybd_status_brightness`: the keycap-contrast to status-panel register map
  (`status_brightness.h`).
- `polykybd_doom_pack_gate`: the FW-9 policy of `doom/doom_pack_gate.h`: unsigned versus
  invalid, interactive versus idle, and the binding of an accept to one pack. The crypto
  is not retested.
- `polykybd_tutorial_plan`: the first-run tutorial phase machine, curves and letter
  choice (`tutorial_plan.c`).
- `polykybd_demo_plan`: the demo playlist and timeline (`demo_plan.c`, with
  `idle_poem_plan.c`).
- `polykybd_idle_poem_plan`: the poem texts, typing timeline and pick
  (`idle_poem_plan.c`).
- `polykybd_icon_lib`: the PlyI loader, blit and pair list (`icon_lib.c`). Most tests
  corrupt one field and expect a refusal.
- `polykybd_crash_ack`: when the slave's crash record stops reading fresh
  (`crash_ack.h`).
- `polykybd_fw_sig_policy`: the refusal of the all-zero placeholder key
  (`fw_sig_policy.h`).
- `polykybd_ime_key_plan`: what KC_IME sends per language, OS and Shift state
  (`ime_key_plan.c`).
- `polykybd_ime_held_slots`: the KC_IME held-key table and stale slots
  (`ime_held_slots.c`) with a faked `ime_slot_io_t`.
- `polykybd_core1_suspend`: the core1 hold and stall decision across a USB suspend
  (`core1_suspend.c`).
- `polykybd_fw_stage`: the fw_staging stream stage transitions (`fw_stage.c`).
- `polykybd_mines_plan`: the mine sweeper rules (`mines_plan.c`).
- `polykybd_ai_events`: the AI mode event ring, key mapping and op 1 / cmd 46 checks
  (`ai_events.c`).
- `polykybd_attn_guide`: the cmd 47 op 2 parse and step selection (`attn_guide.c`).
- `polykybd_host_tutorial_plan`: the cmd 49 parsing, matching and watchdog
  (`host_tutorial_plan.c`).
- `polykybd_idle_style`: the idle style table (`idle_style.c`).
- `polykybd_emoji_dict`: the emoji dictionary loader and search (`emoji_dict.c`)
  against the reference search of `features/emoji/emoji_dict.py`, plus refusals of bad
  bundles.
- `polykybd_host_unicode_mode`: the cmd 20 mode values and the MacCompose flag
  (`host_unicode_mode.h`).
- `polykybd_fuzz_replay`: the fuzz entry points of `dev/fuzz/poly_fuzz.c` (ROI overlay,
  PRC records, font pack, icon library, emoji dictionary) on the fuzz corpus, 2000
  mutated and 2000 random inputs per target from a fixed seed, and the font-pack sort
  regression. No sanitizers here: run the fuzzers for memory errors
  ([`../../dev/fuzz/README.md`](../../dev/fuzz/README.md)).

## Core concepts

- A suite is a name in `testlist.mk` plus `<name>_SRC` and `<name>_INC` in
  `test_rules.mk`. CI reads the names from the `TEST_LIST +=` lines, so a new suite
  needs no workflow edit.
- A header-only suite lists only its `_tests.cpp` file. A suite with a `.c` lists that
  file and, where it checks a CRC, `modules/polykybd/polymod_crc32/polymod_crc32.c`.
- `polykybd_emoji_dict` builds its test bundles with `python3` and
  `features/emoji/emoji_dict.py` at test time.
- Setup: `export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"`, and the
  `lib/googletest` submodule must be checked out
  ([`../../docs/build/unit-tests.md`](../../docs/build/unit-tests.md)).

## Important

- ⚠️ `make test:<name>` with a name that is not in `TEST_LIST` exits 0 and prints
  nothing. Judge a run by the `[  PASSED  ] N tests.` line, not by the exit code.
- ⚠️ Do not rename `test_rules.mk` to `rules.mk`. `qmk ci-validate-keyboard-targets`
  treats every `keyboards/**/rules.mk` as a keyboard and the lint job fails with
  "Legacy target detected".
- ⚠️ The test files are C++. A header that uses C11 `_Static_assert` needs the
  `static_assert` spelling under `__cplusplus` (see `combo_store.h`, `idle_timeout.h`).
- ⚠️ Check that a suite tests something with the `mutation-test-suite` skill. A green
  run alone does not show it.

## Related

- [`../README.md`](../README.md)
- [`../../docs/build/unit-tests.md`](../../docs/build/unit-tests.md)
- [`../../docs/build/ci-checks.md`](../../docs/build/ci-checks.md)
