// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// libFuzzer entry. The Makefile builds this file once per target, with
// POLY_FUZZ_TARGET set to the poly_fuzz_<target> function (poly_fuzz.h).
// A broken invariant aborts, so libFuzzer saves the input as a crash.

#include "poly_fuzz.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef POLY_FUZZ_TARGET
#    error "build with -DPOLY_FUZZ_TARGET=poly_fuzz_<target>"
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int rc = POLY_FUZZ_TARGET(data, size);
    if (rc != POLY_FUZZ_OK) {
        fprintf(stderr, "invariant %d broken\n", rc);
        abort();
    }
    return 0;
}
