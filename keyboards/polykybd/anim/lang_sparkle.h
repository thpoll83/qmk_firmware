// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SPARKLES over a language layout in the first-run tutorial (TUT_LANG_SHOW): a few
// keys at a time twinkle a small four-point star beside their legend, so the preview
// is not a still picture. Each half picks its own keys; nothing is synced, because
// which key twinkles carries no meaning.
#pragma once

// Housekeeping, both halves; self-gating (a single test while the preview is not up).
void lang_sparkle_tick(void);
