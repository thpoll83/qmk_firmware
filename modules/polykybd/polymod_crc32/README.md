# polymod_crc32/

A table-driven CRC-32 (the reflected polynomial `0xEDB88320`, the same value as
`zlib.crc32`). The firmware checks every staged artifact with it: the firmware image, the
font-pack bundles, the icon library and the DOOM pack. Callers in `keyboards/polykybd/`
include `lib/drivers/fw_staging.c`, `lib/fw_update/split_fw_up.c`,
`lib/split_link/split_sync.c`, `lib/split_link/bridge_helper.c`, `lib/hid/poly_pipe.c`,
`lib/drivers/crash_record.c`, `base/fontpack.c`, `base/icon_lib.c`, `base/emoji_dict.c`,
`base/hand_stamp.c`, `base/fw_up_verdict.c`, `keys_com/poly_combos.c` and the `doom/`
pack loader.

## Files

- `polymod_crc32.c` / `polymod_crc32.h`: `crc32_1byte(data, length, previousCrc32)`
  computes the CRC with a 256-entry lookup table (`crc32Lookup`, one byte per step).
  `length` is a `uint16_t`. `crc32_large(data, length, previousCrc32)` takes a `uint32_t`
  length and calls `crc32_1byte()` in chunks of `0x8000` bytes.
- `qmk_module.json`: module name `CRC32`, license GPL-2.0-or-later. The source is
  Stephan Brumme's code (copyright line in both files).

## Subfolders

- [`tests/`](tests/): the `polymod_crc32` unit-test suite.

## Core concepts

- Pass `0` as `previousCrc32` to start. Pass the previous result to continue the same
  CRC over the next block. Chaining gives exactly the one-shot CRC, so the chunk size of
  `crc32_large()` does not change the result.
- The functions are pure: no globals except the `const` table, no QMK headers. Both
  halves and both cores can call them.
- The module defines no QMK hooks. Listing `polykybd/polymod_crc32` in `keyboard.json`
  only adds the source file to the build. Both split72 and split42 list it.
- Tests: `make test:polymod_crc32` (this module), and several `base/tests` suites link
  `polymod_crc32.c` too (`fw_up_verdict`, `polykybd_icon_lib`, `polykybd_emoji_dict`).

## Important

- ⚠️ Use `crc32_large()` for any region that can exceed 65535 bytes. A `uint32_t`
  length passed to `crc32_1byte()` is truncated to 16 bits, and the CRC then covers only
  the first 64 KB without any error (comment in `polymod_crc32.h`).
- ⚠️ The host computes the same CRC with `zlib.crc32` (for example
  `polyhost/device/bulk_pipe.py` in PolyKybdHost). A change to the polynomial, the
  initial value or the final inversion makes the firmware reject what the host sends.

## Related

- [Community modules](../../../keyboards/polykybd/docs/build/community-modules.md)
- [Firmware staging](../../../keyboards/polykybd/docs/fw-update/fw-staging.md)
- [Split sync](../../../keyboards/polykybd/docs/protocol/split-sync.md)
