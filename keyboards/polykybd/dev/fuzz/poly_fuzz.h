// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Fuzz entry points for the firmware parsers that read host-controlled bytes.
// The libFuzzer harnesses (fuzz_main.c) and the googletest replay suite
// (base/tests/fuzz_replay_tests.cpp) call the same functions, so a crash input
// saved by a fuzzer can be added to the corpus and is then replayed by
// `make test:polykybd_fuzz_replay`.
//
// Each function takes one arbitrary byte string, feeds it to the parser the way
// the firmware does, and returns POLY_FUZZ_OK, or the number of the first
// invariant that did not hold. A memory error is reported by the sanitizers.
// The input layout of each target is described in README.md.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POLY_FUZZ_OK 0

// cmd 18/19: the ROI overlay header (set_fragment_context_from_buffer) and the
// fragment copy into one 360-byte overlay row (copy_rectangle_to_overlay), plain
// and RLE-compressed.
int poly_fuzz_overlay_roi(const uint8_t *data, size_t size);

// cmd 41: the PRC record list of one report (prc_parse_record) and the decode of
// every record into a keycap frame (prc_decode_roi).
int poly_fuzz_prc_report(const uint8_t *data, size_t size);

// The PlyF font pack loader (fontpack_load_at) and the font table assembly
// (fontpack_assemble). The harness writes the correct CRC32 into the header, so
// the input reaches the checks after the CRC.
int poly_fuzz_fontpack(const uint8_t *data, size_t size);

// cmd 42: the PlyI icon library loader (iconlib_load_at), the blit of every icon
// and the (pool slot, icon id) pair list (iconlib_fill_pairs). CRC as above.
int poly_fuzz_iconlib(const uint8_t *data, size_t size);

// The emoji search dictionary loader (emjdict_load_at), the letter lookup and the
// search (emjdict_search). CRC as above.
int poly_fuzz_emjdict(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
