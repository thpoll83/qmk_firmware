# The module build compiles polymod_cirque_gestures.c (the adapter) by itself. The
# pure gesture decision is a second file, listed here; the generated
# community_rules.mk `-include`s this file. Without a Cirque Pinnacle in absolute
# mode the adapter compiles to nothing, and the linker drops the unused decision code.
SRC += modules/polykybd/polymod_cirque_gestures/polymod_cirque_gesture_fsm.c
