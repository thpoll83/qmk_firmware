# polymod_prc/tools/

The Python tool that trains PRC probability tables and encodes 1-bit images for the
`polymod_prc` decoder. Use it to build a table for your own images, or to check how
many bytes an image needs. The firmware build does not run it.

## Files

- `prc_tool.py`: a port of PolyKybdHost `polyhost/util/prc_codec.py`. Two subcommands:
  - `train IMAGES... --table-id N [--header FILE.h] [--bin FILE.bin] [--force]` counts
    zero and one pixels per context and writes the table. `header()` writes the C header
    (`prc_table_v<N>[1024]` and `PRC_TABLE_V<N>_SHA256`). Empty images and duplicates
    are skipped.
  - `encode IMAGE --table FILE` prints each image's ROI box and payload in hex, and
    decodes each payload again to check the round trip.
  - Both take `--cell WxH` (split a sprite sheet), `--threshold` (default 128) and
    `--invert`.
  - Library functions: `crop_to_roi()`, `contexts()`, `train()`, `encode()`,
    `decode()`, `load_table()` (a 1024-byte `.bin` or a generated `.h`),
    `load_masks()`.
- `__pycache__/`: Python bytecode cache. Not source.

## Core concepts

- The 10 already-coded neighbours in `TEMPLATE` select one of 1024 contexts
  (`CONTEXTS`). The table gives P(pixel is 0) times 256 for each context, clamped to
  1..255. A binary range coder (`_Encoder`) turns these probabilities into bytes.
- `TEMPLATE` must match `TEMPLATE_DY` / `TEMPLATE_DX` in `../polymod_prc.c`.
- Pixels outside the ROI count as 0, the same as in the decoder, which decodes into a
  cleared frame.
- `train()`, `encode()`, `decode()` and `contexts()` need only the standard library.
  Reading image files needs Pillow (`pip install pillow`).
- `train()` is deterministic and independent of the order of the images. It rounds half
  to even, like `numpy.rint` in the host trainer.
- Tests: `keyboards/polykybd/dev/tools/tests/prc_tool_test.py` checks that `encode()`
  reproduces the host's golden payloads in `../tests/prc_vectors.h`, and that `decode()`
  gives the golden images.

## Important

- ⚠️ The encoder and the decoder must use the same table. With a different table the
  decoder draws wrong pixels and reports no error.
- ⚠️ A table that has shipped is frozen. Give a retrained table a new `--table-id`.
  `train` refuses to overwrite an existing `.h` or `.bin` with different bytes unless
  you pass `--force`; use it only for a table that never shipped.
- ⚠️ `prc_tool.py` and the host's `prc_codec.py` must stay step for step identical to
  the decoder: the template order, the zero padding outside the ROI, the probability
  scale and the range coder.

## Related

- [Module README](../README.md)
- [Protocol history](../../../../keyboards/polykybd/docs/protocol/protocol-history.md)
