// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// The keycap MAIN-legend size planner (HID cmd 34 / enum poly_glyph_size),
// extracted from poly_keymap.c as a PURE seam: the codepoint relocation into the
// latinbig tiers and the origin/baseline clamping are exactly the arithmetic that
// shipped subtle bugs (the dropped-vs-carried cursor ops, the clamp-vs-bbox
// disagreement), and none of it is observable from the rig — so it is
// unit-testable here (make test:polykybd_legend_plan) behind two callbacks
// instead of depending on g_all_fonts and the kdisp measurement directly.
//
// poly_keymap.c binds the callbacks to kdisp_gfx_glyph / kdisp_gfx_text_bbox and
// keeps thin wrappers with the old signatures, so the render path is unchanged.
//
// Size indices are the poly_glyph_size values (state.h). This header deliberately
// does not include state.h — poly_keymap.c _Static_asserts the two stay equal, so
// the planner remains includable from a standalone test with no keyboard config.
#define LEGEND_PLAN_SIZE_S 0
#define LEGEND_PLAN_SIZE_M 1
#define LEGEND_PLAN_SIZE_L 2
#define LEGEND_PLAN_SIZE_COUNT 3

// Longest legend the size override will relocate. A main legend is normally ONE
// glyph; a couple of codepoints covers the composed forms, and anything longer is
// not the kind of thing that wants to be drawn large anyway.
#define GLYPH_SIZE_MAX_LEN 4

// Everything the planner needs from the outside world. `has_glyph` answers "can
// the assembled font set draw this codepoint"; `bbox` measures a NUL-terminated
// legend the way the draw lays it out (kdisp_gfx_text_bbox semantics: x/y are
// relative to the draw origin/baseline). `win_x0..win_x1` are the visible window
// columns and `win_y1` the last visible row (rows are 0..win_y1).
typedef struct {
    bool (*has_glyph)(uint32_t cp, void* ctx);
    void (*bbox)(const uint32_t* text, int8_t* xmin, int8_t* xmax, int8_t* ymin, int8_t* ymax, void* ctx);
    void*  ctx;
    int8_t win_x0;
    int8_t win_x1;
    int8_t win_y1;
} legend_plan_env_t;

// A planned main legend: what to draw, where, and how much room it takes.
typedef struct {
    const uint32_t* text;      // the legend, relocated to the bigger face when big
    int8_t x, y;               // draw origin (y is the baseline)
    int8_t ink_min, ink_max;   // leftmost / rightmost lit pixel, in buffer coords
    bool   big;                // a bigger face was selected
} main_legend_t;

// Rewrites `text` into `out` at the requested size, returning false — leaving the
// caller on the normal face — if the size is S, the legend is too long, or ANY of
// its glyphs is missing at that size. All-or-nothing on purpose; see the comment
// on the implementation.
bool legend_plan_remap(const legend_plan_env_t* env, uint8_t size, const uint32_t* text,
                       uint32_t* out, uint8_t out_cap);

// Works out how a key's MAIN legend should be drawn at `size`, WITHOUT drawing it.
// `small_x`/`small_y` are the origin the small face has always used (the
// per-language offsets), taken verbatim when no bigger face applies. `scratch`
// must outlive the returned plan: it holds the relocated codepoints.
void legend_plan_main(const legend_plan_env_t* env, uint8_t size, const uint32_t* text,
                      int8_t small_x, int8_t small_y,
                      uint32_t* scratch, uint8_t scratch_cap, main_legend_t* out);
