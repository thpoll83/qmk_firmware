// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The status panel's idle screen (split72): demoscene plasma bands with "Poly Kybd"
// typed and edited away across both panels with an underscore cursor. See status_idle.c.
#pragma once

#include <stdbool.h>

// oled_task_user() hands the panel to the idle screen with status_idle_screen() on each
// pass it picks the idle branch, and takes it back with status_idle_release() at the top
// of every pass. A session restarts itself after a pause, so there is no start/stop call.
void status_idle_screen(void);
void status_idle_release(void);
// Every main-loop pass: compose the next frame when it is due, the previous one has been
// sent, and Eden is not mid-frame. Does nothing while the panel is not the idle screen's.
void status_idle_task(void);
// True while one of our frames is still going out over I2C: Eden starts no new keycap
// frame then, so the two take turns on the main loop (bounded, see SI_HOLD_MAX_MS).
bool status_idle_holds_bus(void);
