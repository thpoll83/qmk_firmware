// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#define ENABLE_COMPILE_KEYCODE

#define EECONFIG_USER_DATA_SIZE 154  // +39 latin_ex_wide +1 fmt +20 latin_assign, then +18/+9/+1 for the punctuation targets
                                     // +20 latin_assign (6-bit base-letter per key).
                                     // POLY_EECONFIG_USER_RESERVED was raised 128->256 for
                                     // this: 126 still fits 128, but with 2 bytes left the
                                     // next field of any kind would relocate the keymap, so
                                     // the one-time reset is paid here rather than twice.

