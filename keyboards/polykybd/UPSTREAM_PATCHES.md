# Upstream Patches

Files outside `keyboards/polykybd/` that the PolyKybd build depends
on. Re-apply these whenever upstream QMK master is merged into PolyKybd
and the patched file shows up as a merge conflict (or silently overwritten).

To check the current state at any time:

```sh
git diff master..HEAD -- tmk_core quantum platforms builddefs lib
```

## tmk_core/protocol/usb_descriptor.h

Wrap `RAW_EPSIZE` default in an `#ifndef` so per-keyboard `config.h` can
override it. Required for the 64-byte Raw HID endpoint used by both PolyKybd
variants.

```diff
-#define RAW_EPSIZE 32
+#ifndef RAW_EPSIZE
+    #define RAW_EPSIZE 32
+#endif
```

Originally introduced before this tracker existed. Single hunk around the
endpoint-size block (`#define KEYBOARD_EPSIZE 8` … `#define DIGITIZER_EPSIZE 8`).

## tmk_core/protocol/chibios/usb_main.c

Two changes to `raw_hid_task` for backpressure during overlay transfers:

1. Add a weak `bool raw_hid_pre_receive_kb(void)` hook so PolyKybd can refuse
   to pull a packet off the OUT queue while core1 is still chewing on the
   previous one. The packet stays queued (RAW_OUT_CAPACITY=4) and
   `matrix_task` gets to run instead of starving in the busy-wait inside
   `core1_decompress_fragment` / `core1_update_roi`.
2. Replace `while (receive_report(...))` with `if (receive_report(...))` —
   handle at most one packet per main-loop pass so matrix scan always gets a
   turn between two raw HID receives, even when the OUT queue is full.

The strong override of `raw_hid_pre_receive_kb` lives in
`keyboards/polykybd/multicore_exec.c` and calls `core1_is_busy()`.

If a future QMK upstream changes the signature or semantics of `raw_hid_task`,
re-apply by hand: keep the weak hook, keep the `if`-not-`while`.
