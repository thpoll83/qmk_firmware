// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// The status panel's idle screen (split72): demoscene plasma bands with "Poly Kybd"
// typed and edited away across both panels with an underscore cursor. See status_idle.c.
#pragma once

// Compose and hand one idle frame to the OLED driver once the previous frame has been
// sent; cheap to call every oled_task_user() pass. Restarts itself after a pause, so
// no separate start/stop call is needed.
void status_idle_screen(void);
