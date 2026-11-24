# dev/fuzz/

libFuzzer harnesses for the firmware parsers that read bytes from the host (HID) or
from a bundle the host flashes. They run on a Linux PC, not on the keyboard.

## Targets

| target | parser | input layout |
|---|---|---|
| `overlay_roi` | cmd 18/19: `set_fragment_context_from_buffer()` and `copy_rectangle_to_overlay()` (`base/overlay.c`), plain and RLE (`polymod_rle`) | 5-byte ROI header, then the data. The first report takes 57 data bytes, each later report 62, as in `multicore_exec.c`. |
| `prc_report` | cmd 41: `prc_parse_record()` (`base/prc_record.c`) and `prc_decode_roi()` (`polymod_prc`) | One report, at most 62 bytes. |
| `fontpack` | `fontpack_load_at()` and `fontpack_assemble()` (`base/fontpack.c`) | A PlyF image. |
| `iconlib` | cmd 42: `iconlib_load_at()`, `iconlib_blit()`, `iconlib_fill_pairs()` (`base/icon_lib.c`) | `[width] [n] [n pair bytes] [PlyI image]` |
| `emjdict` | `emjdict_load_at()`, `emjdict_letter_code()`, `emjdict_search()` (`base/emoji_dict.c`) | `[n] [skip] [n query bytes] [dictionary image]`. A query byte `>= 0x80` is U+00C0 + its low 6 bits. |
| `host_tut` | cmd 49: `host_tut_parse_text()`, `host_tut_parse_spotlight()`, `host_tut_parse_arm()` and the condition matching (`base/host_tutorial_plan.c`) | One report, at most 64 bytes (`'P'`, cmd, op, arguments). Every input goes through all three parsers, whatever the op byte is. |
| `macro_decode` | `poly_macro_used()`, `poly_macro_buffer_intact()`, `poly_macro_find()`, `poly_macro_decode()` (`base/macro_decode.c`) | The macro body buffer, at most 1024 bytes. The read callback reads it from an exact-size copy. |
| `attn_guide` | cmd 47 op 2: `attn_guide_parse()`, `attn_guide_select()` (`base/attn_guide.c`) | One report, at most 64 bytes. |
| `ai_events` | cmd 45/46: `ai_set_keys_valid()`, `ai_set_keys_page_valid()`, `ai_card_header()`, `ai_ring_read()` (`base/ai_events.c`) | `[35 op 1 bytes] [3 page bytes] [2 cmd 46 header bytes, little endian] [ring script]`. In the script a byte `< 0x80` pushes that key, a byte `>= 0x80` reads with `since` = the next byte. |
| `combo_store` | cmd 48: `poly_combo_rec_valid()`, `poly_td_rec_valid()`, `poly_td_plan()`, `poly_combo_pick_slot()` (`base/combo_store.h`) | Chunks of 11 bytes: `[control] [10 record bytes]`. Control bit 0 is the kind (0 combo, 1 tap dance), bits 1..3 the tap count, bit 4 "held". Record bytes 0..4 also give the slot pick its clean bits, tried bits and newest sector. |
| `map_codec` | cmd 21/33: `map_codec_read()`, `map_codec_write()` and the no-legend split (`base/map_codec.h`) | `[width selector] [packed bytes, at most 62]`. The width is 8 + selector % 9. |

For the three bundle targets the harness writes the correct CRC32 into the header,
so a mutated input reaches the checks after the CRC.

The HID command targets (`host_tut`, `attn_guide`) parse every input twice, with the
split72 (10 x 8) and the split42 (8 x 6) matrix and 13 layers. These values are copied
in `poly_fuzz.c` from the variants' `config.h`.

Each harness also checks invariants and aborts when one does not hold, for example:
the ROI copy changes no pixel outside the ROI box, a PRC record the parser accepts is
one the decoder accepts, the assembled font table is in global index order, a refused
HID command leaves its output unchanged, the macro decoder reads nothing at or past
the end of the buffer, and every overlay-mapping value written back gives the same
bits.
`poly_fuzz.c` lists them.

## Files

- `poly_fuzz.c`, `poly_fuzz.h`: the entry points `poly_fuzz_<target>()`.
- `fuzz_main.c`: `LLVMFuzzerTestOneInput`, built once per target.
- `stubs/print.h`: replaces QMK's `print.h` (`uprintf` does nothing).
- `Makefile`: builds with `clang -fsanitize=fuzzer,address,undefined`.
- `make_seeds.py`: writes `corpus/`. The PRC seeds are the host encoder's golden
  records from `modules/polykybd/polymod_prc/tests/prc_vectors.h`, the dictionary
  seeds are built by `features/emoji/emoji_dict.py`, and the seeds of the HID command
  and macro targets are the fixtures of their googletests in `base/tests/`.
- `corpus/<target>/`: the seed corpus.

## Running

Needs clang and its sanitizer runtimes (Ubuntu: `apt-get install clang libclang-rt-18-dev`).

```sh
cd keyboards/polykybd/dev/fuzz
make                              # build/fuzz_<target>
make run-prc_report               # one target, 60 s
make run FUZZ_SECONDS=300         # every target, 300 s each
```

New corpus entries go to `build/corpus_<target>/`, crash inputs to
`build/artifacts_<target>/`. Replay one input with `./build/fuzz_<target> <file>`.

## Running the fuzzers in CI

`.github/workflows/polykybd-fuzz.yml` runs in two cases:

- **On a PR that changes a fuzzed parser, a harness or the workflow** (its `paths:`
  list): each target for 60 s. The jobs run in parallel with the builds and
  the unit tests, so they do not make CI take longer. A PR that changes none of
  these files does not run them.
- **From the Actions tab** → *PolyKybd fuzz* → *Run workflow*: choose the branch,
  the target (or `all`) and the run time per target (default 300 s). The job limit
  is 360 min, so stay at or below about 20 000 s (5 h 30 min).
- **Every Wednesday and Sunday at 01:43 UTC** on `PolyKybd`: every target for 5 h.
  A crash fails that target's job and uploads the input.

Manual and scheduled runs restore each target's corpus (`build/corpus_<target>/`)
from the previous run and save it at the end, so a long run continues where the
last one stopped. GitHub deletes a cache that has not been used for 7 days.

The replay suite (`make test:polykybd_fuzz_replay`) runs on every PR.

## When a fuzzer finds a crash

1. Fix the parser in its own commit.
2. Copy the crash input into `corpus/<target>/` with a descriptive name.
3. Run `make test:polykybd_fuzz_replay` from the repository root. That suite
   (`base/tests/fuzz_replay_tests.cpp`) replays the corpus and a fixed-seed set of
   mutated inputs through the same entry points, without sanitizers. Add a direct
   test of the bug there as well, since a memory error without ASan may not fail.

After a change to a seed generator, run `make seeds` and commit `corpus/`.

## Related

- [`../../base/tests/README.md`](../../base/tests/README.md): the googletest suites.
- [`../../docs/build/unit-tests.md`](../../docs/build/unit-tests.md)
