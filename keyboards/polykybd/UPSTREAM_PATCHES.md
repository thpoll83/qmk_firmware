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

## drivers/oled/oled_driver.c

Defer the status-OLED `DISPLAY_ON` until the framebuffer has been pushed, so
the boot no longer flashes the panel's random power-on GDDRAM before the splash
appears (the SSD1306 status OLED is on I2C @ 400 kHz).

Two hunks in `oled_init()`:

1. Drop `DISPLAY_ON` from the `display_setup2[]` init command list so the panel
   stays physically OFF after the init commands:

   ```diff
   -    static const uint8_t PROGMEM display_setup2[] = {…, DEACTIVATE_SCROLL, DISPLAY_ON};
   +    static const uint8_t PROGMEM display_setup2[] = {…, DEACTIVATE_SCROLL};
   ```

2. At the tail of `oled_init()`, after `oled_clear()`, flush the cleared
   (all-black) buffer to GDDRAM with the panel still off, then enable it:

   ```diff
        oled_clear();
        oled_initialized = true;
   -    oled_active      = true;
        oled_scrolling   = false;
   +    oled_active = true;          // suppress oled_render_dirty()'s internal oled_on()
   +    oled_render_dirty(true);     // flush all-black GDDRAM while the panel is off
   +    oled_active = false;         // make the next oled_on() actually emit DISPLAY_ON
   +    oled_on();                   // light the panel — GDDRAM already black
        return true;
   ```

`oled_render_dirty()` calls `oled_on()` at its top when there is dirty data;
holding `oled_active = true` across the flush makes that a no-op, so exactly one
`DISPLAY_ON` is emitted (by the final `oled_on()`) after GDDRAM is clean.

If upstream restructures `oled_init()` / the init command list, re-apply by
hand: no `DISPLAY_ON` in the command list; flush-then-enable at the tail. End
state is unchanged (`oled_active == true`, panel on).
