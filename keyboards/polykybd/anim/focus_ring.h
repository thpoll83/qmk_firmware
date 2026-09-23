// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A circular FOCUS RIPPLE that points at a key, over the board's ordinary legends.
//
// Not a tutorial feature: "point at this key" is what the host's layout editor, a
// shortcut-discovery mode and any future prompt all want. The tutorial is simply its
// first caller.
//
// ⚠️ IT COSTS NOTHING WHEN IDLE. Both hooks below start with poly_focus_active(), so a
// keyboard with no ripple running pays one boolean test per keycap render and nothing
// else. The per-frame cost measured in the design note (TUTORIAL.md §15) — 6 keys in the
// band on average, 15 at worst — is only ever paid while a ripple is live.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "startup_anim.h"   // sa_geom_t

// Arm a ripple centred on a display slot (TUT_SLOT packing: side bit + index). A second
// call restarts it. TUT_SLOT_NONE cancels.
void poly_focus_start(uint8_t slot);
void poly_focus_cancel(void);
bool poly_focus_active(void);

// ---- the two hooks --------------------------------------------------------
// (1) Compositing: draw this key's arc into the CURRENT scratch buffer, on top of a
//     legend the caller has already drawn. Called from update_displays()' per-key path
//     so a full refresh mid-ripple keeps the arc, and from the sliced driver below so
//     the arc moves between full refreshes. ONE arc implementation, two callers.
void poly_focus_overlay(uint8_t disp_idx, const sa_geom_t *g);

// (2) The driver: advance the wavefront and redraw the keys whose band membership
//     CHANGED, within a bounded slice. Call once per housekeeping pass.
//
// ⚠️ Bounded by construction, not by hope. The band touches 15 keys at worst and a key
// render is ~3 ms, so a redraw-everything-now loop would blow a frame by 3x. This drains
// a dirty set at POLY_FOCUS_SLICE_MS per pass instead; on a dense frame the arc lags a
// pass or two, which is invisible at this speed.
void poly_focus_tick(void);

// Provided by poly_keymap.c: redraw one key's ordinary legend into the scratch buffer.
// The panel is already selected and the buffer cleared; the caller sends.
//
// ⚠️ Draws NOTHING and returns false when the key is hidden by a mode. The ripple still
// draws its ARC there — a hidden keycap must not show its legend, but the ring has to
// cross the whole board or it is not a ring. (Gating the arc on this too is what left
// "only ring artifacts on the actual key"; the return value is informational.)
bool poly_focus_draw_legend(uint8_t slot);
