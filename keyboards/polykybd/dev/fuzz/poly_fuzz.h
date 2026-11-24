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

// cmd 49: the host tutorial's TEXT, SPOTLIGHT and ARM parsers
// (host_tut_parse_text, host_tut_parse_spotlight, host_tut_parse_arm) and the
// matching of the conditions ARM sets. Input: one report.
int poly_fuzz_host_tut(const uint8_t *data, size_t size);

// The dynamic-macro buffer: poly_macro_used, poly_macro_buffer_intact,
// poly_macro_find and poly_macro_decode, through the read callback.
int poly_fuzz_macro_decode(const uint8_t *data, size_t size);

// cmd 47 op 2: the key attention guide (attn_guide_parse, attn_guide_select).
int poly_fuzz_attn_guide(const uint8_t *data, size_t size);

// cmd 45/46: AI mode's op 1 checks (ai_set_keys_valid, ai_set_keys_page_valid),
// the cmd 46 card header (ai_card_header) and the event ring (ai_ring_read).
int poly_fuzz_ai_events(const uint8_t *data, size_t size);

// cmd 48: combo and tap-dance records (poly_combo_rec_valid, poly_td_rec_valid),
// the tap-dance plan (poly_td_plan) and the store slot pick (poly_combo_pick_slot).
int poly_fuzz_combo_store(const uint8_t *data, size_t size);

// cmd 21/33: the packed overlay-mapping codec (map_codec_read, map_codec_write).
int poly_fuzz_map_codec(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
