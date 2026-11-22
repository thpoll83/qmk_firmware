# doom/pack/

The DoomPack build. It re-links the unmodified DOOM engine objects of a monolithic
`POLYKYBD_DOOM=yes` build into a standalone image, and wraps that image in a `PlyX`
header as `doom_pack_v<N>.plyx`. The firmware built with `POLYKYBD_DOOM_PACK=yes`
loads the pack from its flash slot (`doom/doom_pack_load.c`). `release.yml`, the
"Build release shape" job of `qmk-test.yml` and `doom/build_all_doom.sh` run
`build_pack.sh`.

## Files

- `build_pack.sh`: the build driver, in five steps. (1) Monolith build, and the
  engine objects are copied to `build/obj/`. (2) Pack-flavour firmware build. The
  overlay pool address and size are read from `__overlay_pool_base__` and
  `__overlay_pool_end__` in that ELF. (3) `pack_init.c` is compiled and the pack is
  linked with `build/pack.ld`. (4) `mkpack.py` writes the `.plyx`. (5) Budget check:
  the image must fit the slot. Options: `--version N`, `--out FILE`.
- `mkpack.py`: writes the `PlyX` header (`doom_pack_hdr_t` in `doom/doom_pack_abi.h`)
  in front of the objcopy'd image. Entry and arena offsets come from the ELF symbol
  table. The CRC is zlib `crc32`, the same value as the firmware's `crc32_1byte`
  with seed 0. The output is unsigned.
- `pack.ld.in`: the linker script template. `build_pack.sh` replaces `@RAM_BASE@`,
  `@RAM_SIZE@` and `@PACK_LEN@`. Engine `.data`, `.bss` and COMMON go at the front of
  the borrowed overlay pool; the game arena follows them. Entry point is
  `doom_pack_init`.
- `pack_init.c`: compiled only into the pack, never into a firmware
  image. Defines `doom_pack_init` (crt0, stores the firmware's `doom_fw_api_t`,
  returns the export table), the forwarders the engine imports from the firmware
  (`doom_arena_at`, `doom_arena_zone`, `doom_pop_key_event`, ...) and the small
  runtime the objects expect (`panic`, `time_us_64`, the `__real_*` halves of the
  monolith's `--wrap` set).
- `PACK_VERSION`: the pack content version (now `4`). `build_pack.sh`,
  `build_all_doom.sh` and `release.yml` all read this file. `--version` overrides it.
- `build/`, `doom_pack_v<N>.plyx`: build output, ignored by git (`.gitignore`).
  `build/` holds `monolith.elf`, `firmware.elf`, `doom_pack.elf`, the image, the
  generated `pack.ld` and the link map.

## Core concepts

- RAM pairing: the pack's statics are linked at the overlay pool address of the
  pack-flavour firmware. That flavour pins the pool at the RAM origin
  (`ld/RP2040_FLASH_TIMECRIT_DOOMPACK.ld`), so an installed pack keeps working
  across firmware rebuilds. `build_pack.sh` fails if the pool is not at
  `0x20000000`, or if its size is not a positive multiple of 360.
- The slot size is derived, never hard-coded: 256 KB minus `FW_EEPROM_RESERVE_SIZE`,
  read from `lib/drivers/fw_staging.h` (`FW_DOOMPACK_SLOT_SIZE`, 248 KB).
- Both firmware builds share `.build/`. The script stashes the monolith's objects
  before the pack-flavour build replaces them.
- Releases sign the pack with `dev/tools/sign_doompack.py`, which appends a 64-byte
  Ed25519 signature. Firmware built with `FW_REQUIRE_SIGNATURE` refuses an unsigned
  pack.
- Install on a keyboard: `polyctl doom install-pack doom_pack_v<N>.plyx`.

## Important

- ⚠️ Bump the pack by editing `PACK_VERSION` only. Ship a new pack when the engine,
  the ABI or the arena layout changes. The loader refuses a stale pack and the egg
  falls back to the fire demo.
- ⚠️ The DOOM monolith is the tightest RAM flavour. CI's gcc 15.2 needs 400-500 B
  more RAM than local gcc 13.2. Trust the headroom that `dev/tools/ram_report.py`
  prints in the "Build release shape" job, not a local build.
- ⚠️ Do not run another `qmk compile` while `build_pack.sh` runs. All flavours share
  `.build/`.
- ⚠️ The DoomPack slot must stay below the EEPROM reservation. A slot that reached
  the end of flash would erase the user's keymap and settings when a large pack is
  written.

## Related

- [`../README.md`](../README.md)
- [`../../docs/design/doom-pack.md`](../../docs/design/doom-pack.md)
- [`../../docs/fw-update/signing.md`](../../docs/fw-update/signing.md)
- [`../../docs/fw-update/fw-staging.md`](../../docs/fw-update/fw-staging.md)
