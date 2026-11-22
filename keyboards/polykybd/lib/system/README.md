# lib/system/

The RAM state that both halves share, its storage in the emulated EEPROM, and the boot
instrumentation. Almost every folder includes `state.h`. `keys_com/poly_keymap.c`
calls the boot banner, the splash and the flush points. `lib/split_link/` sends the
state to the slave.

## Files

- `state.h` / `state.c`: the state types and their RAM policy. `poly_sync_t` is the
  state the master sends to the slave on `USER_SYNC_POLY_DATA`. `poly_layer_t`,
  `poly_last_t` and `latin_sync_t` go on their own transactions. `poly_eeconf_t` is
  the persisted user datablock (`EECONFIG_USER_DATA_SIZE`, 167 bytes on both variants). The
  file holds the getters, setters and dirty flags for brightness, idle style and
  timeout, glyph script, size and placement, tap terms, Auto Shift and the latin
  picks. It also holds `save_all_dirty()`, `request_eeprom_save()` /
  `save_all_if_requested()` and the host change counter `poly_state_touch()`.
- `state_store.c`: the persistence half of the state module. It is the only file that
  knows the `poly_eeconf_t` layout and calls `eeconfig_*`: `load_user_eeconf()`,
  `save_user_settings()`, `save_user_latin()`, the MRU load and save, and the format
  stamps (`stamp_keymap_layers_fmt()`, `stamp_tail_fmt()`, `stamp_tail2_fmt()`,
  `stamp_keymap_mig()`). It reads the values through the getters in `state.h`.
- `boot_diag.c` / `boot_diag.h`: the boot banner on the HID console
  (`emit_boot_banner()`, re-emitted by `boot_banner_housekeeping_tick()` and
  `boot_banner_on_host_probe()`), the keymap-storage and apply-breadcrumb lines, and
  the boot splash. `splash_progress()` draws milestones 1..7 and `SPLASH_DONE`
  (`POLY_SPLASH_STEPS` is 8). `boot_substep()` and `boot_render_mark()` add finer
  breadcrumbs inside a step. `emit_boot_timing_line()` prints the time of each
  milestone.
- `poly_util.c` / `poly_util.h`: display helpers (`select_all_displays()`,
  `clear_all_displays()`, `display_message()`, `display_message_progressive()`), the
  bootloader message (`poly_announce_bootloader()`), `poly_heavy_font()`, and a strong
  `mcu_reset()` override that resets the whole chip with `watchdog_reboot(0, 0, 0)`.

## Core concepts

- Each synced type has a local copy (`l_state`, `l_layer`, `l_last`) and a global
  copy (`g_state`, ...). On the master, housekeeping compares the two with `differ()`
  and sends the local copy. The global copy advances only after `sync_succeeded()`,
  so a failed sync is sent again on the next pass.
- `poly_sync_t` is sent whole on every change and must stay at most
  `RPC_M2S_BUFFER_SIZE` (252) bytes and padding-free. Both rules are
  `static_assert`s in `state.h`.
- Settings in `poly_eeconf_t` are written only at flush points: suspend, the host
  shutdown signal, the store key and a reset (`save_all_dirty()`). The slave gets the
  store key as the `SAVE_EEPROM` flag and flushes from housekeeping.
- The `set_*()` function of a persisted setting marks it dirty. Some also
  call `poly_state_touch()` (for example `set_idle_style()`, `set_glyph_script()`).
  A `note_*()` function is the boot-time EEPROM load and marks nothing dirty.
- The stored keymap format is `KEYMAP_STORAGE_CURRENT` (now
  `KEYMAP_STORAGE_MIDI_LAYER`, 0x73). One-time key migrations use `KEYMAP_MIG_*`
  markers. The tail bytes from `tapping_term` to `emj_search_mode` count only when
  `tail_fmt` is `TAIL_FMT_OK` (0xA5). `auto_shift` counts only when `tail2_fmt` is
  `TAIL2_FMT_OK` (0xB6).
- Host tests for the pure encodings that `state.c` uses: `make test:polykybd_idle_timeout`,
  `make test:polykybd_tap_terms`, `make test:polykybd_idle_style`,
  `make test:polykybd_mode_byte`.

## Important

- ⚠️ `EECONFIG_USER_DATA_VERSION` is fixed at 159 (keymap `config.h`). Never change it:
  a change zeroes all settings and then discards the stored keymap and macros.
- ⚠️ An unwritten EEPROM byte reads `0x00`, not `0xFF`, after wear levelling. Gate a
  new field on a format marker, and retire a broken marker value instead of reusing
  it (0xC3 for the latin assignment map is the example in `state.h`).
- ⚠️ A new tail byte in `poly_eeconf_t` must default to 0. On a board older than
  2026-08-13, offsets 159 and up can hold old keymap bytes, so the field also needs the
  `tail_fmt` reset in `stamp_tail_fmt()`.
- ⚠️ Never write EEPROM in a split-transaction handler. Call `mark_latin_dirty()` or
  `request_eeprom_save()` and let housekeeping write.
- ⚠️ Call `poly_state_touch()` from the setter, and only for state a host view shows.
  Do not derive it from the dirty flags: the LTR-559 marks brightness dirty about twice
  a second.
- ⚠️ `g_user_brightness` changes only at deliberate set-points, never from idle or
  suspend. The save path reads it, not the live contrast.

## Related

- [`../../docs/features/keymap-storage.md`](../../docs/features/keymap-storage.md)
- [`../../docs/protocol/split-sync.md`](../../docs/protocol/split-sync.md)
- [`../../docs/display/idle-styles.md`](../../docs/display/idle-styles.md)
- [`../../docs/debugging/crash-diagnostics.md`](../../docs/debugging/crash-diagnostics.md)
- [`../../docs/debugging/investigation-history.md`](../../docs/debugging/investigation-history.md)
