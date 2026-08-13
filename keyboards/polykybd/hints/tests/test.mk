# NOTE: this file is test.mk, NOT rules.mk, and must stay that way.
# qmk ci-validate-keyboard-targets flags every rules.mk under keyboards/
# whose directory has no keyboard.json as a "Legacy target", so naming it
# rules.mk (the convention used by quantum/*/tests and modules/*/tests, which
# live outside keyboards/) fails the lint job. builddefs/build_test.mk includes
# this by explicit path, so the name is free.

POLYKYBD_HINTS_PATH := keyboards/polykybd/hints

# The hint table is pure and depends only on keycodes + modifier bits, so it links
# standalone — no keyboard, no split state, no display. os_hints_reference.c is the
# pre-extraction copy of the same table; both are compiled in so the test can prove
# the extraction was behaviour-preserving by exhaustion.
polykybd_os_hints_SRC := \
	$(POLYKYBD_HINTS_PATH)/os_hints.c \
	$(POLYKYBD_HINTS_PATH)/tests/os_hints_reference.c \
	$(POLYKYBD_HINTS_PATH)/tests/os_hints_tests.cpp

polykybd_os_hints_INC := \
	$(POLYKYBD_HINTS_PATH) \
	keyboards/polykybd
