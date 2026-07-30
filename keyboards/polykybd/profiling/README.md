# Main-loop timing profiler (`POLYKYBD_LOOP_PROFILE`)

A **compile-gated, off-by-default** diagnostic that answers one question with facts
instead of theory: **does handling an overlay transfer stall the QMK main loop long
enough to miss a matrix scan (a dropped keystroke)?** — and, when it does, **where
the stall time actually goes** (the master→slave bridge, the per-keycap re-render, or
the rest of the loop).

Why the main-loop iteration time *is* the thing to measure: QMK scans the matrix
exactly once per `keyboard_task()` iteration, and `housekeeping_task_user()` runs once
per iteration too. So the wall-clock time between two `housekeeping_task_user()` calls
**is** the matrix-scan interval. A key tap that both begins and ends inside one long
iteration is never sampled — a missed keystroke. This profiler measures that
per-iteration time in microseconds, buckets it, and splits everything by whether the
iteration handled a bulk overlay/mapping HID command.

## Enabling

```bash
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_LOOP_PROFILE=yes
```

`rules.mk` turns that `-e` into `OPT_DEFS += -DPOLYKYBD_LOOP_PROFILE` and adds
`profiling/loop_profile.c` to the build. The readout is printed over the **HID
console** (`uprintf`), so the build also needs `CONSOLE_ENABLE = yes` (the default
keymap already has it). View it with `qmk console` or any HID-console viewer.

In a **normal build** `POLYKYBD_LOOP_PROFILE` is undefined and every hook is an empty
`static inline` (see `loop_profile.h`) — **zero code, zero image growth, no timer
reads**. That is why the call sites in `poly_keymap.c` / `hid_com.c` / `bridge_helper.c`
are unconditional: they compile to nothing unless the switch is on. So it is safe to
leave the hooks in the shipping source; only a `-e POLYKYBD_LOOP_PROFILE=yes` build
pays for them.

It reads the raw 1 MHz microsecond counter directly (`timer_hw->timerawl` via
`hardware/structs/timer.h` — the lightweight register struct, **not** the pico alarm
API, which trips the strict-build `-Werror`). QMK's own `timer_read32()` is 1 ms
resolution — too coarse for sub-millisecond render/bridge slices.

## Reading the output

A summary is emitted every `LOOP_PROFILE_LOG_EVERY` iterations (default **8192** — the
loop runs at ~1 kHz, so roughly one block every few seconds). One block is three or
four lines:

```
LoopProf: iters=278528 ovl=206 worst=105ms(ovl br=5ms rn=67ms)
  norm    <1=0 1-2=264900 2-5=13317 5-10=51 10-20=0 20-50=0 50+=54
  ovl     <1=0 1-2=0     2-5=17    5-10=153 10-20=1 20-50=7 50+=28
  ovltot  wall=3668ms bridge=783ms render=1927ms rest=956ms
```

### Line 1 — `LoopProf:` headline (all-time)

| Field | Meaning |
|-------|---------|
| `iters=N` | Total main-loop iterations measured since boot. |
| `ovl=N` | Of those, how many handled a **bulk overlay/mapping HID command** (cmds 10/11/12/16/17/18/19/21). The rest are "norm". |
| `worst=Nms` | The **single longest** iteration seen all-time, in ms (the worst matrix-scan gap). |
| `(ovl` / `norm)` | Whether that worst iteration was overlay-handling or normal. |
| `br=Mms` | Of that worst iteration, milliseconds spent **blocking inside `send_to_bridge()`** (the master→slave UART relay). |
| `rn=Rms` | Of that worst iteration, milliseconds spent inside **`update_displays()`** (the per-keycap OLED re-render). |

`worst − br − rn` is everything else in that one iteration (HID copy, RLE kick, core1
wait, matrix scan, the rest of housekeeping).

### Lines 2–3 — `norm` / `ovl` histograms

Per-iteration wall time, bucketed (milliseconds), counted separately for normal vs
overlay-handling iterations:

```
<1   1-2   2-5   5-10   10-20   20-50   50+
```

Each number is **how many iterations fell in that band**. A healthy idle/typing loop
lives almost entirely in `<1` / `1-2`. Anything in `50+` is a ≥50 ms iteration — a
window in which a fast tap can be missed. Split by norm/ovl, this tells you whether the
long iterations are overlay-driven (a program switch) or something else (e.g. a
wake-from-idle render shows up in the **norm** `50+` column, because no overlay command
ran that pass).

### Line 4 — `ovltot` attribution (aggregate over ALL overlay iterations)

This is the line that settles *what to fix*. It sums, across **every** overlay
iteration (not just the single worst one):

| Field | Meaning |
|-------|---------|
| `wall=` | Total wall-clock time spent in overlay-handling iterations. |
| `bridge=` | Of that, total time blocking in `send_to_bridge()` (the master→slave relay). |
| `render=` | Of that, total time in `update_displays()` (the keycap re-render). |
| `rest=` | `wall − bridge − render` — HID copy, RLE kick, core1 wait, matrix scan, the rest of the loop. |

Read the shares:

- **`render` dominant** ⇒ the stall is **render-bound**. `update_displays()` redraws the
  keycaps regardless of where the overlay bytes came from, so baking overlays into a
  keyboard-side resource pack would **not** help — the fix is in the render path
  (coalesce redundant renders, chunk a render across passes).
- **`bridge` dominant** ⇒ the stall is **transfer-bound** (the master→slave relay), which
  a resource pack / fewer-reports approach *would* help.
- **`rest` dominant** ⇒ look at the HID copy / RLE / core1 path.

Totals are `uint32_t` **microseconds** internally (they wrap only after ~71 min of
*accumulated overlay-iteration* wall time, far beyond a measurement session), so no
ms-rounding loses the sub-millisecond-per-iteration render slices. `bridge` is clamped
to the iteration wall and `render` into the remaining wall, so the derived `rest` can
never underflow on a measurement artefact.

## How to take a clean measurement

- **Type / switch programs while watching the console.** The counters are all-time
  cumulative, so let it run and compare successive `ovltot` blocks (subtract to get the
  per-interval numbers).
- **Set the idle style to `pulse` (not Eden) if you are measuring the awake
  program-switch stall.** The Eden screensaver renders full procedural frames while idle
  and will pile ~50–166 ms iterations into the **norm** `50+` bucket, contaminating the
  sample. (Eden only runs while idle and the first keypress stops it, so it is not the
  program-switch stall — but it muddies the histogram.)
- **The worst-iteration line keeps only the single all-time maximum.** For per-switch
  attribution use the `ovltot` aggregate, not `worst`.

## Where the hooks live

All are no-ops unless `POLYKYBD_LOOP_PROFILE` is defined:

| Hook | Call site | Purpose |
|------|-----------|---------|
| `loop_profile_tick()` | `poly_keymap.c` `housekeeping_task_user()`, at the very top | Closes the previous iteration's measurement, updates the histograms, emits the summary. At the top so it measures the FULL previous iteration (matrix scan, HID, bridge, render). |
| `loop_profile_note_overlay_cmd()` | `hid_com.c` `raw_hid_receive()`, the classifier `switch` | Tags this iteration as overlay-handling (cmds 10/11/12/16/17/18/19/21). |
| `loop_profile_add_bridge_us(us)` | `bridge_helper.c` `send_to_bridge()`, around `transaction_rpc_exec()` | Accumulates blocking bridge microseconds for this iteration. |
| `loop_profile_add_render_us(us)` | `poly_keymap.c` `sync_and_refresh_displays()`, around the `update_displays()` calls | Accumulates render microseconds for this iteration. |

## Tuning the profiler itself

- **`LOOP_PROFILE_LOG_EVERY`** (`loop_profile.c`, default `8192`) — iterations between
  emitted summaries. Lower it for a denser readout while actively poking the keyboard;
  raise it to keep the console quiet.

That is the only knob the profiler has. It does not change any device behaviour — it
only reads timers and prints. The buckets, the overlay-command set, and the
bridge/render attribution are fixed in `loop_profile.c` / the call sites above.
