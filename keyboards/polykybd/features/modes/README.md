# features/modes/

Modes that change what a group of keys does and shows: AI mode (the host's agent
registry on the number row), the legends of the MIDI keys, and the Mines game on the
idle screen. `keys_com/poly_keymap.c` calls their `*_process_record()`, `*_tick()` and
render functions, and `lib/hid/hid_com.c` sends HID cmd 45 and 46 to AI mode. The pure
logic of AI mode and Mines is in `base/`.

## Files

- `ai_mode.c` / `ai_mode.h`: AI mode, a PROTOTYPE. `KC_AI` tapped sends a Hyper tap; held,
  it turns the mode on, and the number keys 1..9,0 and ESC then show the host's agent
  cards. `ai_hid_cmd()` handles cmd 45 (op 1 sets per-key states and card slots, op 2
  reads the key events), `ai_hid_card()` handles cmd 46 (card upload).
  `ai_render_key()` draws a card, `ai_rgb_paint()` paints the key LEDs, and
  `ai_attention_tick()` runs the focus ripple from a waiting agent's key. The event ring
  and the checks on host data are `base/ai_events.c`.
- `midi_legend.c` / `midi_legend.h`: keycap legends for QMK's `QK_MIDI_*` keycodes.
  `midi_legend()` returns the note name with its octave and a treble staff, or the
  value of a setting key (OCT, TRNS, VEL, CHAN). It reads only the packed word from
  `midi_cfg_pack()`, which the master syncs in `poly_sync_t.midi_cfg`. The symbols are
  IconsFont glyphs drawn by `dev/tools/midi_glyphs.py`.
- `mines_mode.c` / `mines_mode.h`: "Mines", a mine sweeper with one key per cell. Hold
  Hyper or the AI key for `MINES_START_HOLD_MS` (1 s) on the idle screen to start or
  resume it (`mines_try_start()`). The Hide key or `MINES_INACTIVE_MS` (120 s) without
  a key press gives the board back to the idle animation. The rules are
  `base/mines_plan.c`; the board shape per variant is at the top of `mines_mode.c`.

## Core concepts

- The master handles the keys. The slave draws its own half from `poly_sync_t`:
  `ai_mode`, `ai_slot[12]`, `ai_state[10]`, `ai_flags`, `ai_page`, `ai_pages`,
  `ai_done_seq` for AI mode, `mines` (a `mines_board_t`) for the game, and `midi_cfg`
  for the MIDI legends.
- AI state and the Mines board are RAM only and are lost at a reboot.
  `ai_reset_keys()` clears the agent states at USB suspend. They are also cleared after
  `AI_KEYS_HOST_TIMEOUT_MS` (30 s) without a host update.
- AI mode finds its keys by keycode (`KC_1`..`KC_0`, `KC_ESC`) on the active default
  layer, so a remap moves them. split42 has no number row, so only ESC is found there.
  `ai_attention_tick()` does nothing on split42.
- Mines sends nothing to the host while it runs. The board is 14 x 4 cells (56) on split72
  and 12 x 3 cells (36) on split42 (`MINES_HALF_W`, `MINES_ROWS`).
- Build switch: `midi_legend.c` and `MIDI_ENABLE` are built for every flavour except the
  DOOM monolith (`POLYKYBD_DOOM=yes` without `POLYKYBD_DOOM_PACK`), see `rules.mk`.
- Tests (host, `make test:<suite>`): `polykybd_midi_legend` links `midi_legend.c`
  directly and checks every keycode stays below `MIDI_LEGEND_MAX`.
  `polykybd_ai_events` and `polykybd_mines_plan` test the pure parts in `base/`.

## Important

- ⚠️ `midi_legend()` and the emoji layer return a pointer to one static buffer. Use it
  before the next call.
- ⚠️ `MIDI_LEGEND_MAX` (24) is one above the longest legend (23 codepoints). A legend
  that reaches the cap is a truncated one.
- ⚠️ A `midi_cfg` of 0 has no `MIDI_CFG_VALID` bit. It means a build without MIDI or a
  slave before its first sync, and the legends then show QMK's start values.
- ⚠️ `cmd 46` carries the overlay slot in 10 bits (`_Static_assert` on
  `NUM_OVERLAY_SLOTS` in `ai_mode.c`).

## Related

- [`../../docs/features/midi.md`](../../docs/features/midi.md)
- [`../../docs/features/mines.md`](../../docs/features/mines.md)
- [`../../docs/protocol/protocol-history.md`](../../docs/protocol/protocol-history.md): cmd 45 and 46
- [`../../docs/protocol/split-sync.md`](../../docs/protocol/split-sync.md)
