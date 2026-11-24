// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Replaces QMK's quantum/logging/print.h in the libFuzzer build only: the
// parsers log through uprintf(), and the fuzzers run without QMK's console.
// The googletest build uses the real print.h and print.c.
#pragma once
#define uprintf(...) ((void)0)
#define uprint(s) ((void)0)
