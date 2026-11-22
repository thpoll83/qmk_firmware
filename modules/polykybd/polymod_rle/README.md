# polymod_rle/

The bit-level run-length decoder for keycap overlay bitmaps. The host sends RLE-coded
overlays over HID, and the master forwards them to the slave half. Two files in
`keyboards/polykybd/` call it: `base/overlay.c` (`copy_rectangle_to_overlay()`, for a
compressed ROI update) and `lib/display/multicore_exec.c` (the `CORE1_CMD_DECOMPRESS`
job, which decodes into a 360-byte overlay on core1).

## Files

- `polymod_rle.c` / `polymod_rle.h`:
  - `rle_decompress(dest, max, compressed, len, bit_index)` writes the runs into `dest`,
    most significant bit first, starting at sub-byte offset `bit_index % 8`. It returns
    the number of bits written, or `max * 8` when the next bit would pass `max` bytes.
  - `rle_count(max, compressed, len)` returns the number of bits the stream expands to,
    without writing. It saturates at `max * 8` in the same way.
- `qmk_module.json`: module name `RLE`, license GPL-2.0-or-later.

## Core concepts

- Format: one byte per run. Bit 7 clear means the low 7 bits count zero bits. Bit 7 set
  means the low 7 bits count one bits. A run is at most 127 bits.
- Decoding can resume across fragments. The caller passes `dest = buffer + bit_index/8`
  and the same `bit_index`, then adds the return value to `bit_index` for the next
  fragment (`multicore_exec.c` does this with `core1_bit_index`).
- `max` is counted from `dest`, not from the start of the buffer. It is the number of
  writable bytes at `dest`.
- `compressed` is `volatile const` because core1 reads it from a buffer that core0
  fills (`core1_buffer` in `multicore_exec.c`).
- The module defines no QMK hooks. Both split72 and split42 list it in `keyboard.json`.
- There is no unit-test suite for this module.

## Important

- ⚠️ The bound check includes the start offset: `(count + start_off) / 8 >= max`. A
  guard without `start_off` wrote one byte past `dest[max - 1]` when a fragment started
  at a non-byte-aligned bit (SECURITY note FW-8 in `polymod_rle.c`). Keep `dest`,
  `bit_index` and `max` on the same origin.
- ⚠️ The encoder is on the host (`polyhost/util/rle_util.py` in PolyKybdHost). A change
  to the run format needs the same change there.

## Related

- [Display pipeline](../../../keyboards/polykybd/docs/display/display-pipeline.md)
- [Split sync](../../../keyboards/polykybd/docs/protocol/split-sync.md)
- [Community modules](../../../keyboards/polykybd/docs/build/community-modules.md)
