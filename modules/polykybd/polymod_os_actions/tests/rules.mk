POLYMOD_OS_ACTIONS_PATH := modules/polykybd/polymod_os_actions

# OS_ACTIONS_UNIT_TEST swaps quantum.h for the shim header, so the emitter links
# against the suite's recording mock instead of the keyboard. keyboards/polykybd
# is on the include path for poly_os.h alone (stdint-only by design), so the
# suite can also pin the GNOME/KDE → LINUX column fold.
polymod_os_actions_DEFS := \
	-DOS_ACTIONS_UNIT_TEST

polymod_os_actions_SRC := \
	$(POLYMOD_OS_ACTIONS_PATH)/polymod_os_actions.c \
	$(POLYMOD_OS_ACTIONS_PATH)/tests/os_actions_tests.cpp

polymod_os_actions_INC := \
	$(POLYMOD_OS_ACTIONS_PATH) \
	$(POLYMOD_OS_ACTIONS_PATH)/tests \
	keyboards/polykybd
