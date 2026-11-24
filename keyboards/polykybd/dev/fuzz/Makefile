# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
#
# libFuzzer harnesses for the parsers of host-controlled bytes. Run from this
# directory (README.md):
#   make            build every fuzzer into build/
#   make run        run every fuzzer for FUZZ_SECONDS (default 60) each
#   make run-<t>    run one fuzzer, e.g. make run-prc_report
#   make seeds      rewrite corpus/ with make_seeds.py

ROOT    := ../../../..
POLY    := $(ROOT)/keyboards/polykybd
ifeq ($(origin CC),default)
CC      := clang
endif
SAN     := -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined
CFLAGS  := -g -O1 -std=gnu11 -Wall -Wno-unused-function $(SAN) \
           -DFONTPACK_HOST_TEST \
           -Istubs -I. -I$(POLY) -I$(POLY)/base \
           -I$(ROOT)/modules/polykybd/polymod_prc \
           -I$(ROOT)/modules/polykybd/polymod_rle \
           -I$(ROOT)/modules/polykybd/polymod_crc32 \
           -I$(ROOT)/platforms

TARGETS := overlay_roi prc_report fontpack iconlib emjdict
FUZZ_SECONDS ?= 60

SRC := poly_fuzz.c \
       $(POLY)/base/overlay.c \
       $(POLY)/base/prc_record.c \
       $(POLY)/base/fontpack.c \
       $(POLY)/base/icon_lib.c \
       $(POLY)/base/emoji_dict.c \
       $(ROOT)/modules/polykybd/polymod_prc/polymod_prc.c \
       $(ROOT)/modules/polykybd/polymod_rle/polymod_rle.c \
       $(ROOT)/modules/polykybd/polymod_crc32/polymod_crc32.c

# Every header the sources include, directly or not, as the compiler reports
# it (-MM), so a change to any of them rebuilds the fuzzers.
HDRS := $(filter %.h,$(shell $(CC) $(filter-out $(SAN),$(CFLAGS)) \
          -DPOLY_FUZZ_TARGET=poly_fuzz_prc_report -MM fuzz_main.c $(SRC) 2>/dev/null))

.PHONY: all run seeds clean $(addprefix run-,$(TARGETS))

all: $(addprefix build/fuzz_,$(TARGETS))

build/fuzz_%: fuzz_main.c $(SRC) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -DPOLY_FUZZ_TARGET=poly_fuzz_$* fuzz_main.c $(SRC) -o $@

# New inputs go to build/corpus_<t>/, so corpus/ (in git) changes only by hand.
$(addprefix run-,$(TARGETS)): run-%: build/fuzz_%
	@mkdir -p build/corpus_$* build/artifacts_$*
	./build/fuzz_$* -max_total_time=$(FUZZ_SECONDS) -print_final_stats=1 \
	    -artifact_prefix=build/artifacts_$*/ build/corpus_$* corpus/$*

run: $(addprefix run-,$(TARGETS))

seeds:
	python3 make_seeds.py

clean:
	rm -rf build
