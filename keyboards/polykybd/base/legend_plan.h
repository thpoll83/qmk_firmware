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
    // The measured box RELATIVE to the origin, kept so a caller that MOVES the plan
    // (render_key's overlap stagger lifts a small base 6 px) can re-clamp it without
    // measuring the legend a second time.
    int8_t box_xmin, box_xmax, box_ymin, box_ymax;
    bool   big;                // a bigger face was selected
} main_legend_t;

// Slides a draw origin so a measured ink box lands inside the visible window.
// `x`/`y` are the draw origin (y is the baseline) and `xmin..ymax` the box the
// bbox callback returned for that legend, i.e. relative to the origin.
//
// ⚠️ This is the ONE definition of "keep the ink on the panel", used by every
// element of a keycap (the main legend, the Shift preview, the AltGr hint) so
// they cannot disagree about where the edge is. Do not re-inline an edge test at
// a call site.
//
// ⚠️ The two clamps of an axis FIGHT when the ink is bigger than the window, and
// the order decides which edge loses: horizontally the WEST edge wins (the left
// of the glyph is kept, the right clips), vertically the SOUTH edge wins (the
// baseline area is kept, the top clips). Measured across all 160 layouts, exactly
// one element is ever over-size — he-IL's 43 px standalone nikud on KC_BACKSLASH
// — so this only decides where those 3 px go; every other legend fits outright.
void legend_plan_clamp(const legend_plan_env_t* env, int8_t* x, int8_t* y,
                       int8_t xmin, int8_t xmax, int8_t ymin, int8_t ymax);

// Anti-burn-in travel range for an idle legend, as an INCLUSIVE offset range per
// axis to add to the draw origin (kdisp_set_draw_offset). `ink_*` is the legend's
// ABSOLUTE ink box — the box of everything the display list draws, at the origin
// the planner already clamped — and the returned range is what keeps that box on
// the panel. An axis whose `lo` exceeds its `hi` has no usable range; the caller
// leaves that axis at 0.
//
// The range is derived per glyph and is never a fixed +/-N envelope: a slim "i"
// roams its whole free width while a wide "w" moves only as far as it can. A cap
// would throttle the slim glyph and edge-bias the wide one.
//
// `overhang` is what a legend with NO free space of its own is allowed to borrow.
// A 40 px tall icon fills the window exactly, so its own slack is zero and it
// cannot move a single row — it lights the same pixels for the whole idle session,
// which is the burn an idle style exists to prevent. Letting it hang `overhang` px
// off an edge buys it that many pixels of travel for `overhang` px of clipping, and
// the clipping costs nothing in memory: the scratch buffer really extends there and
// kdisp_send_window() sends the window only (see disp_array.h's BUFFER_SLACK_*).
//
// Two limits on that borrowing, both deliberate:
//   * Only the SHORTFALL is borrowed, and only up to `overhang` px of travel. A
//     legend that already has room is untouched — it must not start clipping just
//     because a rule exists, and 3 px off the west edge deletes the stem of an "i".
//   * NORTH is not on offer (BUFFER_SLACK_N is 0), so a glyph that needs vertical
//     room takes it all from the south.
// An OVER-SIZE legend — ink taller or wider than the window, which legend_plan_clamp
// has already decided an edge for — is left alone: its range is empty in that axis
// and widening it would only move the clip from one edge to the other.
void legend_plan_idle_travel(const legend_plan_env_t* env,
                             int8_t ink_xmin, int8_t ink_xmax, int8_t ink_ymin, int8_t ink_ymax,
                             uint8_t overhang,
                             int8_t* dx_lo, int8_t* dx_hi, int8_t* dy_lo, int8_t* dy_hi);

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
