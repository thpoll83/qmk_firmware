# Cirque trackpad — the PolyKybd gesture layer (split72)

split72 carries a Cirque Pinnacle pad (I2C, `CIRQUE_PINNACLE_ADDR` default 0x2A) on
the same I2C0 bus (GP0/GP1) as the status OLED and the optional LTR-559. split42 has
no pad, so everything here is split72-only.

## The layer, and why the stock one is not used

The ASIC's own gestures place their zones in the **sensor's** frame, and
`POINTING_DEVICE_ROTATION_90` rotates the reported x/y long after the ASIC has already
chosen its corner. So the stock corner-tap right click landed at 11 o'clock, moved to
10 when `CIRQUE_PINNACLE_CURVED_OVERLAY` retuned the edges under it, and neither stock
scroll ever fired. Tap force is the ASIC's, with no z-threshold register exposed.

The replacement splits in two:

- **`base/cirque_gesture_fsm.c`** — the decision. Pure: no `quantum.h`, no timer, no
  I2C, no `report_mouse_t`. Samples plus a millisecond clock in, deltas / wheel /
  buttons out. A least-squares affine fit (Q12 fixed point) maps sensor coordinates to
  a 896-unit pad frame, and the zones are tested there.
- **`cirque_gestures.c`** — the adapter. Reads `cirque_pinnacle_read_data()`, calls
  `poly_gest_feed()` with `timer_read32()`, copies the answer into a mouse report, and
  installs `poly_cirque_driver` from `pointing_device_init_kb()` — which QMK calls
  *after* `driver->init()`, so the stock driver still configures the ASIC and nothing
  upstream is patched.

That split is not tidiness. "Tapping does not work" was reported four times for four
different causes, and each cost a hardware round because the arithmetic shared a
function with the sensor. `make test:polykybd_cirque_gesture` now replays taps, drags
and dials built from measured pad numbers; it is mutation-checked.

**It is the DEFAULT build.** `split72/rules.mk` sets `-DPOLYKYBD_CIRQUE_ABSOLUTE` and
`-DPOLYKYBD_CIRQUE_GESTURES` unless `-e POLYKYBD_CIRQUE_RELATIVE=yes` opts out, and the
two are one switch because absolute mode implies the gesture layer. The relative flavour
survives only as an A/B escape hatch against stock behaviour; it compiles neither source.
The tunables and the reasoning behind each number live in `base/cirque_gesture_fsm.h` and
`split72/config.h`.

⚠️ **It was opt-in until 2026-09-15, and that was a real gap rather than caution.** PR CI
and the release workflow both build the *default*, so every hardware round ran a flavour
no automated build produced and no release would have shipped. A flavour that only a
hand-typed `-e` reaches is not covered by anything.

## Where a dial can START, and the accidental-scroll knob

Two zones arm a dial, and they are not equally easy to hit by mistake:

- the **ring**, `POLY_GEST_RING_R` (405), tested against the touchdown radius in the
  896-unit pad frame;
- a **wedge** at 10–11 o'clock, `POLY_GEST_DIAL_WEDGE_R`, which reaches INWARD from
  its radius over the 29..61° band of the top-left quadrant.

⚠️ **The wedge is what fires by accident, and its radius is the knob — not the ring's.**
The pad reaches 448 units on an axis and 633 into a corner, so the same number means
very different things in the two zones. The ring at 405 leaves a 43-unit annulus on an
axis: starting a dial there means touching the rim on purpose. The wedge runs up the
45° diagonal, so its radius is a fraction of a run half as long again — at 200 it armed
from 32 % of the way out and owned the **outer two thirds** of that diagonal, and a
finger resting at the top left scrolled instead of pointing (field report 2026-09-16).
Raised to **320**, i.e. 51 % of 633, so the wedge is the outer half of its own diagonal
and is still 313 units deep.

Pushing the RING outward instead does not work: past ~440 the axes become unreachable
and the dial degenerates into a corners-only gesture. That asymmetry is the whole reason
the wedge exists.

⚠️ **The gesture tests DERIVE their sample radii from the constant** (`WEDGE_IN` /
`WEDGE_OUT` in `cirque_gesture_tests.cpp`, with a `static_assert` that `WEDGE_IN` stays
inside the ring). They used to hardcode 260 and 140, and moving the wedge outward would
have left the two NEGATIVE tests passing for the **wrong reason** — their sample would
have fallen inside the new minimum radius, so "a dial armed at the wrong angle" and "at
the wrong quadrant" would silently have become "too close to the centre". A test that
still passes after the change it should have caught is worse than no test.

## A dial can start any distance outside the ring

The drag-vs-dial test aborts a pending dial when the radius changes by
`POLY_GEST_SCROLL_DR` (120). INWARD it is measured from `r_floor`, the smaller of the
touchdown radius and the radius that armed the dial (the ring, or the wedge for a wedge
start). It used to be measured from the touchdown radius, so a thumb that landed in a
corner at ~600 and slid in to ~430 before turning moved 170 units inward and became a
drag, while the same stroke started at 440 scrolled (field report 2026-09-25).
`DialStartedFarOutsideTheRingStillScrolls` pins it. OUTWARD it is still measured from
the touchdown radius.

## Pointer acceleration and lift-off

Acceleration is quadratic above the knee, `(mag - 6)^2 / 20` percent on top of the 65 %
base, capped at 130 %. The old linear ramp reached the cap at 30 motion units per sample,
so a quick short move already ran at double speed ("acceleration starts too fast",
2026-09-25). The table and the measured strokes are in `base/cirque_gesture_fsm.h`.

The cursor also ignores samples whose z sits in the release band (`Z_RELEASE..Z_TOUCH`).
As a finger lifts, the reported position wanders, and on a short move that wander was
larger than the move. The tap test already ignored those samples.

## One image for either side — analysed 2026-09-15, NOT implemented

Question: now that the gesture layer is ours, can the pad be soldered to either half
and detected at runtime, instead of `POINTING_DEVICE_RIGHT` deciding at compile time?

**Yes — and our layer is not what makes it possible.** The side test was already a
runtime call, which is the part that is easy to get backwards.

### `POINTING_DEVICE_RIGHT` does not bake in a side

It selects which runtime expression the three gates use
(`quantum/pointing_device/pointing_device.c:73`):

```c
#elif defined(POINTING_DEVICE_RIGHT)
#    define POINTING_DEVICE_THIS_SIDE !is_keyboard_left()
```

`is_keyboard_left()` is evaluated every boot at the init gate (`:183`), the status
check (`:220`) and the task's side pick (`:337`), and the two split handlers guard
themselves the same way (`quantum/split_common/transactions.c:706` master, `:736`
slave). The firmware already asks which half it is. It just asks in a form that
hardcodes the answer.

The probe exists too: `cirque_pinnacle_init()` ends with `cirque_pinnacle_connected()`
(`drivers/sensors/cirque_pinnacle.c:220`, called at `:293`), which writes `ZIDLE` and
reads it back. ⚠️ **Every ERA and calibration loop in that driver is bounded** — by
`CIRQUE_PINNACLE_TIMEOUT` (20 ms, `cirque_pinnacle.h:12`) and by a 200 ms cap on the
calibration poll — so a missing pad makes init slow, never hung. Checked, because an
unbounded spin there would have been a boot hang on the padless half.

### The change

`POINTING_DEVICE_COMBINED` makes `THIS_SIDE` the constant `true` and compiles **both**
`transactions.c` early-returns out. Both halves then probe and poll; the padless one
contributes a zero report, which `pointing_device_combine_reports()` (`:496`) adds in
harmlessly — it sums x/y/h/v and ORs buttons, so no
`pointing_device_task_combined_kb()` override is needed.

Three things then need fixing:

1. ⚠️ **A padless master would kill the mouse entirely.**
   `pointing_device_get_status()` returns the *real* status once `THIS_SIDE` is `true`,
   so a half whose init failed reports `INIT_FAILED`, and `pointing_device_task()`
   early-returns on it — including on the master that is meant to be forwarding the
   slave's report. It is `__attribute__((weak))`, so we override it. **This same line
   is why the current build works**: today the non-pad side gets a hardcoded
   `POINTING_DEVICE_STATUS_SUCCESS` from the `?:` at `:220`.
2. **A padless half must not poll.** `RAP_ReadBytes`/`RAP_Write`
   (`drivers/sensors/cirque_pinnacle_i2c.c:17`, `:26`) call
   `pointing_device_set_status(POINTING_DEVICE_STATUS_FAILED)` on every failed
   transfer, and nothing resets it. The adapter should record the init result and have
   `poly_cirque_get_report()` return the report untouched — one probe at boot, no I2C
   afterwards.
3. **The pad view is nailed to the right half** (`split72/status_oled.c`, the
   `is_right_side() && oled_pad_view_active()` test). That test can simply go:
   `oled_pad_view_active()` is driven by `s_last_touch_ms`, which only moves on the
   half that has the pad.

Rotation needs no work in the absolute flavour, which defines none — both
`pointing_device_adjust_by_defines()` and its `_right` twin are identity there. In
**relative** mode `POINTING_DEVICE_COMBINED` would additionally need
`POINTING_DEVICE_ROTATION_90_RIGHT`.

### What cannot be settled from the code

⚠️ **The affine fit is right-hand-specific.** `POLY_GEST_PX_A..PY_C` were fitted to
four corners measured with the pad mounted on the right half, **mounting skew
included** — the skew is deliberate and was kept at the user's request, because the
thumb approaches at an angle. A left-hand mount mirrors that frame, so the fit mirrors
with it, and the skew angle on a left mount is a hardware fact rather than arithmetic.

Handling it is cheap: a second coefficient set chosen by `is_left_side()`, fitted from
four corners measured on that half. `poly_gest_to_pad()` is already exported to the
test suite, so a mirrored fit can be checked off-hardware instead of through a flash
round — the same reason the fit was extracted in the first place.

Separately, an **ergonomic** choice and not a correctness one: with a corrected fit the
right-click corner stays in the same *physical* place. For a left-hand thumb you would
probably want it mirrored to the top-left, which is a sign flip in the corner test.

### If this is picked up

Making the absolute flavour the default belongs in the same change. Side-agnostic is
only meaningful for the layer that has been tested on hardware, and that layer is
currently the one a plain `qmk compile` does not build.
