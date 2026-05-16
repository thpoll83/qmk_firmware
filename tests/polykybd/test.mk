# Tests for keyboards/handwired/polykybd/base pure-C modules.
# test.mk doubles as the auto-discovery marker for the test framework.

# Include path so com.h / crc32.h can be found by the test .cpp files
$(TEST_OUTPUT)_INC += keyboards/handwired/polykybd/base

# Pull in the C source files under test
$(TEST_OUTPUT)_SRC += \
    keyboards/handwired/polykybd/base/com.c \
    keyboards/handwired/polykybd/base/crc32.c