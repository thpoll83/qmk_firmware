# ld/

The linker scripts of the two DOOM builds. The keyboard-level `rules.mk` selects one by
setting `MCU_LDSCRIPT`. A normal build uses QMK's default RP2040 script
(`RP2040_FLASH_TIMECRIT`, from `platforms/chibios/mcu_selection.mk`) and none of these
files.

## Files

- `RP2040_FLASH_TIMECRIT_DOOM.ld`: the script of the DOOM monolith (`-e POLYKYBD_DOOM=yes`
  without the pack flavour). It is a copy of
  `platforms/chibios/boards/common/ld/RP2040_FLASH_TIMECRIT.ld` with the
  `RP2040_rules_data_with_timecrit.ld` rules inlined. The engine's zero-initialised
  statics (`doom/engine/*.o` `.bss` and `COMMON`) go into a `.doom_shared` block that is
  also the keycap overlay pool (`__doom_shared_base__`, used by `base/overlay.c`). The
  block is `NOLOAD`, `ALIGN(8)` and padded to 216000 bytes.
- `RP2040_FLASH_TIMECRIT_DOOMPACK.ld`: the script of the pack flavour
  (`-e POLYKYBD_DOOM_PACK=yes`). It is a copy of the same upstream script with a
  `.overlay_pool` section at the RAM origin (0x20000000), padded to 216000 bytes. The
  engine pack (`doom/pack/`, the `.plyx`) links its statics at that address.

## Core concepts

- QMK looks for a linker script only in an `ld/` folder. `platforms/chibios/platform.mk`
  checks `$(KEYBOARD_PATH_1..5)/ld/$(MCU_LDSCRIPT).ld` first, then the ChibiOS board
  and common folders. `KEYBOARD_PATH_1..5` are the keyboard folder and its parents, so
  `keyboards/polykybd/ld/` is found by both the `split72` and the `split42` build. It
  must stay at the keyboard root: QMK does not look in `lib/` or another subfolder, and
  no upstream folder has a script of these names.
- Both scripts keep the pool outside `__bss_base__`..`__bss_end__` (`NOLOAD`), so crt0
  does not zero it. The overlay usage bits gate every read, and the reset and refill
  paths clear the pool. DOOM clears it before the game starts.
- The pool size is `NUM_OVERLAY_SLOTS` (600, `config.h`) × 360 bytes (one 72×40 1-bit
  keycap) = 216000 bytes. If the pool contents grow past the padding, the link fails
  with "cannot move location counter backwards".
- `flash1` is 2048 KB unless `FLASH_LEN` is defined. `FW_STAGING_OFFSET`
  (`lib/drivers/fw_staging.h`, 0x200000) is kept equal to this length, so an image
  larger than the firmware partition fails to link.
- Pinning the pool at 0x20000000 makes the pack's RAM base the same in every pack-flavour
  build, so a flashed `.plyx` keeps working after a firmware rebuild. The loader still
  checks `hdr.ram_base` against the live pool.

## Important

- ⚠️ Do not move this folder out of the keyboard root (see *Core concepts*).
- ⚠️ The header comments of both scripts give the pool size as 226,800 bytes. The
  scripts pad to 216000, which matches `NUM_OVERLAY_SLOTS` × 360. The padding value must
  follow the pool size in `base/overlay.c`.
- ⚠️ `doom/doom_mode.c` takes the core1 stack from the end of `.doom_shared`. The block
  stays `ALIGN(8)` because AAPCS needs an 8-byte aligned stack pointer.
- ⚠️ If the overlay pool is made smaller, keep it at about 205 KB or more, or the DOOM
  build no longer fits (`base/overlay.c`). Check both flavours: the monolith fails at
  link time, but the pack depends on the pool address.

## Related

- [`../docs/design/doom-pack.md`](../docs/design/doom-pack.md)
- [`../doom/README.md`](../doom/README.md)
