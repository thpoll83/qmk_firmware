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

#include <stdint.h>

#ifdef POLYKYBD_LTR559_DRIVE
// Slave-side payload for the generic slave->master pull channel
// (USER_SYNC_SLAVE_DATA, kind SLAVE_DATA_SENSOR): {avg lux, proximity}. The
// channel and its registration live in slave_data.c; this just fills the reply.
void poly_ltr559_slave_sensor_reply(void* out_data, uint8_t out_len);

// Master-side auto-brightness + idle-inhibit; call every housekeeping pass
// (it self-limits to one decision per LTR559_DRIVE_MS).
void poly_ltr559_drive(void);
#endif
