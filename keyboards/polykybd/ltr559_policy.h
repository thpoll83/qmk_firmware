// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// LTR-559 auto-brightness + idle-inhibit POLICY. The DRIVER is the
// polykybd/polymod_ltr559 community module (it probes and polls the part from
// its own hooks); everything here is the PolyKybd-side decision layer on top of
// it — mapping the 5 s average lux onto the volatile/host-auto brightness path
// and turning a near proximity reading into a display wake. Extracted from
// poly_keymap.c so the policy is one findable unit; behaviour is unchanged.
//
// Everything is gated on POLYKYBD_LTR559_DRIVE, exactly as it was in
// poly_keymap.c — a board can carry the driver module without this policy.

#ifdef POLYKYBD_LTR559_DRIVE
// Registers the generic slave->master pull channel (USER_SYNC_SLAVE_DATA) whose
// slave-side handler serves {avg lux, proximity}. Call once from
// keyboard_post_init_user(), alongside the other transaction registrations —
// the handler stays private to ltr559_policy.c, the same way every other
// user_sync_* handler stays private to the file that owns its data.
void poly_ltr559_register_split_handler(void);

// Master-side auto-brightness + idle-inhibit; call every housekeeping pass
// (it self-limits to one decision per LTR559_DRIVE_MS).
void poly_ltr559_drive(void);
#endif
