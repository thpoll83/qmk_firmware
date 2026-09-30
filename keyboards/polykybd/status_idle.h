// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The status panel's idle screen (split72): a procedural "Poly Kybd" marquee in
// the Eden splash face, filled with Eden's plasma. See status_idle.c.
#pragma once

// Compose and hand one idle frame to the OLED driver, at most every
// STATUS_IDLE_FRAME_MS; cheap to call every oled_task_user() pass. Restarts
// itself after a pause, so no separate start/stop call is needed.
void status_idle_screen(void);
