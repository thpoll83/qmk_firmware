# Crash diagnostics: the record, the watchdog and the phase breadcrumb

`base/crash_record.*` — how a HardFault, an unhandled exception or a main-loop hang
is recorded, rebooted through and announced on the next boot. Moved out of
`CLAUDE.md` on 2026-09-10: ~16 KB read while you are in that file, reading a crash
line, or adding a blocking path.

Before this, all three left **nothing**: the M0+ HardFault vector fell into
ChibiOS's `b .` loop, the board sat dead until a replug, and the only evidence was
"it stopped". Now each is announced on the console, over HID (cmd 39, protocol
v16) and — for the slave — over the split link, the host turns the console line
into a dialog, and the rig fails the run on it.

⚠️ **`WATCHDOG.REASON.TIMER` alone is NOT a hang.** The bootrom's reboot after a
UF2 copy is a watchdog reboot too, so the first boot after EVERY BOOTSEL flash
reads TIMER — which would have made the recovery path this whole feature points
users at open with a phantom crash dialog. The discriminator is the SDK's own
`watchdog_enable_caused_reboot()`.

---

## Crash diagnostics: the crash record, the watchdog and the phase breadcrumb (`base/crash_record.*`)

A HardFault, an unhandled exception or a main-loop hang used to leave **nothing**:
the M0+ HardFault vector fell into ChibiOS's `b .` loop, the board sat dead until
a replug, and the only evidence was "it stopped". Now every one of those is
**recorded, rebooted through, and announced on the next boot** — on the console
(`crash: side=master kind=hardfault core=0 pc=0x… lr=0x… sp=0x… psr=0x… icsr=0x…
phase=3:0x0015 up=123456ms n=1 reason=0x22 fw=0.18.0`, one line, in the boot
banner and its re-emits), over HID (**cmd 39**, protocol **v16**), and for the
slave over the split link (the master pulls it and prints `side=slave`). The host
turns the console line into a dialog (`PolyKybdHost/CLAUDE.md`), the rig fails the
run on it (`test_no_crash_record`). What is worth knowing:

- **The record lives in a NOLOAD RAM block** (`s_ram`, section `.ram0.crash_record`
  via ChibiOS's `rules_memory.ld` — the same trick as the bootloader magic) so it
  survives the `watchdog_reboot()` the handler ends with. It does NOT survive a
  power cycle, which is why it is **archived to flash** at boot:
  one 4 KB sector at `FW_CRASH_LOG_OFFSET` (`FW_APPLY_LOG_OFFSET - 4096`), page-
  appended, erased only when full or on cmd 39 sub-op 2. `FW_UP_MAX_SIZE` shrank
  by that sector (`0x1F7000 → 0x1F6000`; the host mirror in `hid_fw_up.py` moved
  with it).
  - ⚠️ **The archive is written INSIDE `crash_record_init()`, at the top of
    `keyboard_pre_init_user()`, WITHOUT the `fw_staging` core1 lockout — and
    both halves of that sentence are load-bearing.** It went through three
    shapes in review of #271, each catching the previous one:
    1. Archive from init, under the lockout, in post_init after QMK's init.
       Greptile: releasing the lockout does a bounded RELAUNCH of core1, so run
       before post_init's own `multicore_launch_core1()` the unbounded FIFO
       handshake finds core1 already running and blocks forever — a keyboard
       that hangs on the boot after every crash.
    2. Capture in `pre_init` (CodeRabbit: QMK's matrix / split / OLED init runs
       between the two hooks, so a fault there must be tagged `phase=boot` with
       the previous record already safe), park the copy in `.bss`, write it
       from `crash_record_archive_pending()` after the launch. CodeRabbit again:
       a reset in that boot window loses the parked copy, and a fault that
       recurs there **every** boot never archives at all — precisely the record
       worth having.
    3. Write it at `pre_init`, no lockout. Sound because at that point core1 has
       **never been launched** — it is parked in the bootrom, fetching nothing
       from XIP — so there is nothing to halt and no relaunch to trigger.
       `flash_guarded()` takes a `lockout` flag: `false` from init only, `true`
       from the runtime `crash_record_clear()` erase, where core1 is serving RLE
       from flash and must be parked. **Never move `crash_record_init()` after
       the core1 launch** — the no-lockout write is only correct ahead of it.
  - **Verify the placement in the ELF, not the linker script**: `nm` must show
    `s_ram` inside `.ram0` and `.ram0_init` must be **size 0** (nothing initialises
    it). `objdump -h` on the split72 build: `.ram0 00000040 @ 2003e12c`, `.ram0_init
    00000000`. And the vector table at `0x10000100` slot 3 must be OUR
    `HardFault_Handler` (`…dddd` = `1000dddc|1`), with slots 4+ still the shared
    weak body that `bl`s `_unhandled_exception` — which is now our strong override,
    so both routes land in `crash_record.c`.
- **The M0+ has only HardFault** — no BusFault/UsageFault, no CFSR/BFAR/MMFAR. So
  the record is the **stacked frame** (pc/lr/xpsr from the exception frame, sp =
  the frame address, `ICSR` for the active vector) plus what the firmware itself
  knew: the **phase breadcrumb** and uptime. The handler is naked asm: `tst lr,#4`
  picks PSP/MSP, then C. ⚠️ `frame_readable()` bounds the frame to SRAM before
  touching it — a corrupt SP would otherwise re-fault INSIDE the handler and the
  record would never be written.
- **The phase breadcrumb is what makes a watchdog timeout diagnosable.** A hang
  has no fault frame, so `crash_phase_enter(phase, arg)` / `crash_phase_leave(tag)`
  tag the code the main loop is in: `HID` (arg = cmd id, `hid_com.c`), `BRIDGE`
  (arg = transaction id, `send_to_bridge`), `CORE1_WAIT` (the two decompress waits
  in `multicore_exec.c`), `FLASH`, `SUSPEND`, `APPLY`, and `LOOP` re-armed every
  housekeeping pass. On `WATCHDOG.REASON.TIMER` with no fault record,
  `crash_record_init()` synthesises a `kind=watchdog` record **from the phase left
  in RAM** — the phase is written directly into the NOLOAD block for exactly that.
  ⚠️ The enum is mirrored in the host's `PHASE_NAMES` (`crash_report.py`); keep the
  numbers in step (the `CRASH_PHASE_RENDER` slot was removed before shipping, so
  SUSPEND=7, APPLY=8).
  - ⚠️ **`WATCHDOG.REASON.TIMER` alone is NOT a hang — the bootrom's reboot after
    a UF2 copy is a watchdog reboot too, so the FIRST BOOT AFTER EVERY BOOTSEL
    FLASH reads TIMER.** Found by the rig the first time the crash tests ran
    (run 33809919200, 2026-09-03): both halves reported a fresh
    `kind=watchdog phase=2:0x0000 up=0ms n=3 reason=0x12` — `0x12` is
    `HAD_RUN | WD_TIMER`, i.e. the rig's RUN-pin reset followed by the bootrom's
    post-copy reboot, and `n=3` because the NOLOAD block survives a reflash, so
    the pre-fix firmware had counted three consecutive rig flashes as three
    consecutive hangs (two more and it would have **halted** the rig in `wfi`).
    The discriminator is the SDK's own: `watchdog_enable()` leaves
    `0x6ab73121` in scratch4 and every deliberate reboot path clears it (the
    bootrom, `watchdog_reboot()`, our inlined self-apply reset, and now
    `crash_watchdog_stop()`), so `watchdog_enable_caused_reboot()` is true only
    for a timeout of the watchdog `crash_watchdog_start()` armed. Reading
    `REASON` without it would have made every UF2 flash — the recovery path this
    whole feature points users at — open with a phantom crash dialog.
- **The watchdog is 8 s** (`CRASH_WATCHDOG_MS`), started after the boot splash
  and fed from **housekeeping and the suspend loop only**. Two places disarm it,
  and both are load-bearing: `shutdown_user()` (the bootloader jump / `mcu_reset`
  would otherwise be racing an 8 s timer) and `fw_staging.c` right before
  `fw_staging_do_apply()` (the self-apply never returns to housekeeping; a
  watchdog reset mid-copy is the brick this whole area guards against). A new
  blocking path longer than 8 s needs a `crash_watchdog_feed()` inside it — or
  the record it produces will say so, which is the point.
- ⚠️ **EARLY BOOT IS THE UNWATCHED WINDOW, and that is why an early boot hang stays
  unexplained.** `crash_watchdog_start()` is the LAST line of
  `keyboard_post_init_user()` — deliberately, because the steps above it may block for
  seconds. `boot_guard_milestone()` arms a late-boot guard at step 5 (below), so only
  the boot path BEFORE step 5 runs with no watchdog. A stall there is PERMANENT: no reset, no record, no console line, and the board
  sits on the splash until it is unplugged. Every field report of "the master did not
  restart, unplugging brought it back" lands in this window, which is exactly why it
  keeps being re-reported as "still not clear why".

  ⚠️ **The span it actually hangs in is the FINAL RENDER**, and that one is worth
  naming because nothing about it looks dangerous: `splash_progress(SPLASH_DONE)`
  ends by handing the keycaps over to the real legends
  (`update_displays(ALL_AT_ONCE)`, `boot_diag.c`), which walks ~40 keycaps at
  roughly 2.5 ms each, and **every one of them is a blocking `spi_transmit()` ->
  `spiSend()`, i.e. an `osalThreadSuspendS()` with NO timeout**. One lost SPI/DMA
  completion parks the main thread there permanently. That is also the only
  unbounded wait in the whole render — the rest is array reads and XIP.

  ⚠️ **And "no console line" is not a shortage of prints, it is structural**:
  `console_task()` is called from the MAIN LOOP (`quantum/main.c`), which
  `keyboard_init()` has not reached yet, so every `uprintf` of this boot is still
  sitting in the report buffer. `usb_event_queue_task()` is in the same loop, so a
  bus reset or suspend from the host queues up and is never acted on either. Do not
  spend a round concluding "it printed nothing" from a channel that structurally
  cannot carry it — the STATUS PANEL is the only live channel a wedged board has.

  Five things now survive it, none of which needs the boot to finish:

  - `splash_progress()` stamps `crash_phase_enter(CRASH_PHASE_BOOT, step)` at each
    milestone, so any record written LATER says how far that boot got — the line reads
    `phase=1:0x0003`, i.e. boot step 3. (It does nothing for a board that is unplugged
    rather than reset; nothing written to flash can survive that.)
  - the status OLED shows **`Booting....` over a percent** at each milestone (25 / 38 /
    50 / 63 / 75 / 88 / 100, rounded to nearest). The only evidence a hang used to
    leave was the keycap splash's solidify count — "it was stuck with PO" — which
    localises the stall to one of seven gaps only if the letters are counted exactly,
    and a two-letter field report cannot be trusted to that precision. A percent can be
    read straight off the wedged board, and maps back to the milestone one-to-one.
    ⚠️ **TWO lines, and the face depends on the panel.** `"Booting.... 100%"` measures
    143 of the 128 px in the 19 px face and 119 in the 15 px one (4 px of margin, the
    fit-by-a-hair shape that already sent `"Restarting"` and `"no image staged"` back
    for a second pass). Split across two bands the widest parts are 88 and 48 px — but
    the 19 px face then clips 2 px off the top of split42's **32 px** panel, so the
    short panel uses the 15 px one. Both measured with
    `tools/status_oled_preview.py --boot <step>`; 0 off-panel pixels at every milestone
    on both heights.
  - `boot_render_mark()` (`boot_diag.c`) stamps the breadcrumb **per KEY** through that
    final render — `phase=1:0x08NN`, `NN = row*MATRIX_COLS + col + 1` — and repaints the
    panel once per ROW as a fraction. A sub-step screen reads `Booting 100%` over
    `17 / 40` over `100%`; it is drawn per row rather than per key because the paint is
    itself I2C traffic in the window being measured (see the cost note below). So the
    panel names the row on a board nobody can attach to, and the archived record names
    the exact key. ⚠️ A stop that MOVES between boots (key 16, then key 18) rules out a
    bad glyph or a missing font entry outright — those stop at the same key every time.
  - a **watchdog guard from boot step 5 to the end of post_init** (below; it covered
    the final render only until round 34 of the tutorial), which turns the wedge into a
    reset that records.
  - `usb_watch()` samples `USBD1.state` per key and, on a change, displaces the label
    line with `USB 4>2 @18` — ACTIVE(4) -> READY(2) is a bus reset, (5) a suspend. One
    volatile read, no hook in the USB stack. It exists because the boot-render hang
    reproduced only when a MacBook was COLD-BOOTED with the keyboard attached, i.e.
    while EFI enumerates and the kernel then resets the bus, all inside this window.

  ⚠️ **The instrument is not free, and it sits in the window it measures.** Five
  per-row panel paints add 25-40 ms of I2C to a ~100 ms render — enough to move a
  timing race, and the 2-in-3 repro stopped once the instrumented build was flashed
  (unproven either way, n is small). The fall-back if it stops reproducing for good is
  breadcrumb + watchdog with NO paints: the record still names the key after the reset,
  and only the live readout is lost.

  ⚠️ **Arming the watchdog earlier is NOT a free fix**, which is why it is still not
  armed across the whole of post_init. `crash_watchdog_start()` also sets
  `consecutive = 0`, and reaching it is the definition of "this boot succeeded" for the
  crash-loop halt; arming during boot changes what that counter means. And 8 s is the RP2040 MAXIMUM, so any single
  milestone gap that legitimately exceeds it turns a rare hang into a permanent reboot
  storm — on a path where the first gap spans QMK's split and USB init, i.e. the very
  thing that blocks when the other half is missing. It needs measured per-milestone
  boot timings first.

  What #301 did instead was arm it across the **final render alone**; round 34 of the
  tutorial widened that to **boot step 5 through the end of post_init**, final render
  included. Two deliberate pieces make that safe:

  - **`crash_watchdog_arm()` is `crash_watchdog_start()` without the bookkeeping.**
    It reprograms the same 8 s timeout and touches neither `consecutive` nor the phase,
    because post_init has NOT finished: the BOOT breadcrumb has to survive so the record
    reads `phase=1:0x08NN`, and the hang has to keep counting toward the halt. Never
    call `start()` for a guard inside boot.
  - ⚠️ **A watchdog reset runs no code, so it never reaches the crash-loop halt.**
    The halt lives in `record_and_reboot()`, which a timeout does not go through — the
    chip simply resets. A hang that recurs every boot would therefore reboot-loop
    forever. The guard is one-shot for exactly that reason: it skips itself when
    `crash_record_fresh()` plus the archived record say the previous boot already died
    under it (`kind=watchdog`, phase BOOT, step 5 or later). One reset, one record,
    then the old wedge — which BOOTSEL still recovers, and which leaves the panel
    readable for a photograph instead of resetting it away every 8 s.
  - ⚠️ **Widened to step 5 (2026-09-25)**, because a master wedged at "63%, 4 / 4":
    `fw_staging_init()` had returned and `splash_progress(6)` never repainted, the same
    63% -> 75% gap this file names, and outside the old guard there was no reset and so
    nothing for `polyctl crash show` to read. The guard is armed by
    `boot_guard_milestone()` at step 5 (core1 up) and fed at every milestone, sub-step
    and render key; the one-time keymap discard between steps 6 and 7 feeds it once per layer and
    before the macro clear (`dynamic_keymap_reset_poly()`), so no single span of its
    EEPROM writes has to fit the whole discard into 8 s.
    Inside each milestone, `splash_progress()` stamps two finer breadcrumbs:
    `0xSSE1` before the status-panel paint (I2C) and `0xSSE2` before the logo draw
    (keycap SPI), plus `0x08E3` before the final dwell. So `phase=1:0x06E1` reads
    "the 75% panel paint never returned". These three carry the core1 flag in bit 12
    as well (below), so from step 5 on they read `0x15E1` / `0x15E2` once core1 has
    reached `core1_entry()`. Firmware before 1.3.2 never sets it on them: a
    `0x05E2` from 1.0.0 (2026-09-30, a master hung in the logo draw right after
    the core1 launch) says nothing about core1 either way.
    ✅ **First rig observation of the guard recovering a stall (2026-09-30):** release
    firmware 1.3.1, `Build and HIL Test` run 36696216264 attempt 1 — the master's
    record read `kind=watchdog phase=boot 6.0xE1 consecutive=1`: the 75% panel paint
    stalled, the guard reset the chip once, and the next boot was clean (attempt 2
    passed the full suite and a real HID apply). So the recovery path works on
    hardware, and the stall is still OPEN on release firmware. Pre-1.3.2, so the stamp
    carries no core1 flag. With the 1.0.0 `0x05E2` above that is two stalls in the
    step-5/6 window on two firmwares; a `0x15E1`/`0x16E1` from 1.3.2+ would say
    whether core1 was up.
    ✅ **It was up (field, 2026-10-02, fw 1.3.2).** A user's master froze at
    "63%, 4 / 4", the guard reset it once and the next boot was clean. `polyctl crash
    show` read `kind=watchdog phase=1:0x16e1 up=0ms n=1 reason=0x11`: the step-6 (75%)
    panel paint never returned, with core1 already in `core1_entry()`. The panel still
    showed the last frame that finished, which is why the user saw 63% while the
    record names step 6. That is the third stall in the step-5/6 window and the first
    with core1 known to be running. Each panel write has a 100 ms I2C timeout, so the
    paint cannot hang on its own; something kept core0 from servicing that timeout
    while core1 ran. Contention between the cores (XIP flash, a spinlock, the bus) is
    the inference, not a measurement.
    - **The milestone paint now has per-call breadcrumbs too, `0xC0 | call`**
      (below). The record above could say no more than `0xE1` because only the
      SUB-step paint was instrumented call by call.
    - ⚠️ **The host never alerted on it.** The tray's crash dialog was raised only by
      the console line, which the firmware prints with the banner and its re-emits in
      the first ~30 s, and after a watchdog reset the host's probe is still
      debouncing the re-enumeration. PolyKybdHost now also reads cmd 39 on the
      GET_ID fresh-boot marker, and has a Help & About entry that reads both halves
      and copies the report.

- **A crash loop halts instead of looping forever**: `consecutive` counts
  back-to-back records and past `CRASH_LOOP_LIMIT` (5) the handler parks in `wfi`
  rather than rebooting — recoverable over BOOTSEL, and the archive still says
  what it was doing. Two details, both found in review of #271: the halt
  **disarms the watchdog first** (still armed from `crash_watchdog_start()`, it
  would otherwise reset the chip 8 s later and turn the halt into a reboot storm
  with a pause — CodeRabbit), and `crash_watchdog_start()`, i.e. a boot that got
  all the way through post_init, **resets the counter to 0** — what the field's
  comment always promised and the code did not do, so five unrelated crashes
  across weeks of uptime would have halted the board. The halt is for a firmware
  that never finishes booting; one that crashes at runtime reboots and announces
  itself each time.
- **The slave's record rides `USER_SYNC_SLAVE_DATA`**, which is now generic and
  unconditional: `slave_data.c` owns the op dispatch (`SLAVE_DATA_SENSOR` = the
  LTR-559 pull that used to be the whole handler, `SLAVE_DATA_CRASH`), and the
  master pulls the crash body once per link-up, three tries 2 s apart. ⚠️ It is
  printed **once, at the pull**, not with the banner re-emits — the link can come
  up long after those stop.
- ⚠️ **The slave's FRESH bit is relative to the SLAVE's boot, so the slave is told
  once the host has seen its record** (`base/crash_ack.h`, `SLAVE_DATA_CRASH_ACK`).
  Without that, a master-only reboot pulled an old slave crash as fresh again: the
  master printed `crash: side=slave` once more, cmd 39 half 1 said fresh, and the
  host alerted on a crash it had already shown. Now a cmd 39 read of a present,
  fresh slave record answers fresh that once, clears the master's cached flag and
  queues an ack. The pull tick sends it as `[kind][u32 crc]`, every 500 ms until the
  RPC lands. The slave applies it only when the CRC names the record it holds, and
  from then on answers that record as not fresh for the rest of its boot.
  - **A host READ acknowledges, not a master pull.** A master that pulled the record
    with no host running must not retire it, or a host started after a master-only
    reboot would never hear of the crash.
  - **`s_fresh` on the slave is untouched.** The late-boot guard reads it as "the
    previous boot died under me"; only the reply's FRESH bit changes.
  - **A link drop cancels a pending ack.** The CRC already refuses an ack for a
    different record, but two identical watchdog records (same breadcrumb,
    `up=0ms`) share a CRC; dropping the ack on the link drop that a slave reboot
    usually causes covers that case. If the drop is missed, the identical second
    crash reads as already seen. The host's per-process dedupe would hide it anyway.
- **cmd 39**: `data[2]` 0 = this half's archived record, 1 = the slave's, 2 =
  clear, else NACK. Body `[flags][48-byte poly_crash_record_t]`, flags bit0
  present / bit1 **fresh** (recorded by the boot before this one). Only a fresh
  record is announced on the console; the archive is history for `polyctl crash
  show`. `_Static_assert(sizeof == 48)` pins the struct the host unpacks with
  `struct.Struct("<IBBBBIIIIIIHH8sI")`.
- **Exercised on the rig by accident, not by a deliberate fault.** The UF2
  false positive above (run 33809919200) drove the whole reporting chain end to
  end on real hardware — boot-time capture, the console line on both halves, the
  slave pull over the split link, cmd 39 read on master and slave, and the
  clear — before any fault had been injected on purpose. What is still
  unverified is the **fault path itself**: the naked handler, the stacked frame
  and the `watchdog_reboot()` out of it. The `debug-firmware-on-rig` skill with a
  probe that dereferences a bad pointer over HID is the way to close that.
  - **`-e POLYKYBD_CRASH_TEST=yes` is the by-hand route** (`diag/crash_test.c`, a
    TEST-ONLY flag — a normal build compiles inline no-ops and pays nothing).
    Hold Ctrl+Shift+Alt and press a digit: **1** unaligned word store (the
    qmk#258 brick, reproduced), **2** a branch to an address with bit 0 clear,
    **3** a main-loop hang (~8 s to the watchdog), **4** `crash_record_halt()`,
    **5** a pended SPARE NVIC IRQ, i.e. the `_unhandled_exception` funnel rather
    than HardFault, **6** the same fault on **core1** (shared vector table;
    PRIMASK does not mask a HardFault, so `core` reads 1), **7** the **slave**,
    over a `SLAVE_DATA_CRASH_TEST` pull that deliberately never answers.
  - ⚠️ **The chord tests each modifier FAMILY, not a combined mask.**
    `MOD_MASK_CTRL` covers left and right, so `(mods & (CTRL|SHIFT|ALT)) == that`
    demands all SIX keys and can never fire off a normal keymap — written that
    way first, caught before the build.
  - ⚠️ **Every address is laundered through a `volatile`.** A plain
    `*(uint32_t *)1 = x` is undefined behaviour GCC may delete outright, and a
    deleted fault is a test that silently proves nothing.
    - ⚠️ **The mechanism that actually bit was NOT deletion — it was
      LEGALISATION, and it is quieter.** Trigger 6 wrote its address as a
      compile-time constant (`((uintptr_t)&core1_decomp_count) + 1u`), so GCC
      could see the misalignment and split the `volatile uint32_t` store into
      **four `strb`** — which never fault on ARMv6-M. Core1 wrote `EF BE AD DE`
      and carried on, so the trigger did nothing at all and reported nothing
      (measured 2026-09-04). A deleted store leaves no instructions to find; a
      legalised one leaves plausible-looking code that simply cannot fault. The
      laundering is what hides the alignment from the optimiser, and it is
      required even where the address is not literally a constant expression.
    - **Check it in the DISASSEMBLY, not the source**: the fault site must be a
      single `str`, and the address must be reloaded from the volatile
      (`ldr r4,[r2]` then `str r3,[r4,#0]`). Any `strb` on that path means the
      trigger is inert.
  - **`clear_keyboard()` + a 25 ms settle before every trigger**, the same rule
    the FW-2 prompt and `doom_begin()` follow: a crash is a path that does not
    return, so the held chord would otherwise auto-repeat on the host — for a
    full 8 s on the watchdog trigger, before the board even reboots.
  - ⚠️ **A pull that lands BEFORE the slave has archived used to count as DONE.**
    `crash_record_note_slave()` returns true for an empty reply as well (it
    clears the master's cached copy), so testing it alone closed the pull for
    that link-up with nothing reported — and right after a slave reboot the
    empty reply is the normal first answer. Only a reply with
    `CRASH_HID_FLAG_PRESENT` ends it now. Pre-existing, not specific to the
    crash test: it applied to the ordinary post-reboot pull too.
  - ⚠️ **The slave's line was printed exactly ONCE, so a single dropped console
    read lost the record for good.** The master's own line survives that because
    the boot banner repeats it; the slave's arrives long after those repeats have
    stopped, which is why it is not in the banner. It now schedules a few repeats
    of its own (`crash_record_emit_slave_line()`), which is safe because the host
    dedupes identical lines — the same property the banner's repeats rely on.
  - ⚠️ **Trigger 7 cannot rely on the master OBSERVING the slave's reboot as a
    link drop.** `slave_data_crash_pull_tick()` re-pulls only on a false->true
    transition of `is_transport_connected()`, which needs
    `SPLIT_MAX_CONNECTION_ERRORS` (200) consecutive failures to accumulate
    *before* the slave is back — a race, not a guarantee. So the request opens a
    bounded forced-retry window (`CRASH_FORCE_WINDOW_MS`, 30 s) instead.
    ⚠️ Do **not** "re-arm" by clearing `s_tries` at request time, which was tried
    first: with the link still reading connected, that spends all three ordinary
    tries into a half-rebooted slave and then gives up forever — the opposite of
    the intent. The window is additive; the drop path still re-arms normally.
  - ✅ **PROVEN ON HARDWARE 2026-09-04 (fw 0.18.4), triggers 1 and 2** — the
    fault path this section called unverified. Trigger 1's `pc` landed on the
    faulting store ITSELF (`crash_test.c:53`, `601a str r2,[r3,#0]`), not its
    successor, so one `addr2line` names the line. The disassembly also shows GCC
    emitting the `ldr` read-back of `s_addr` before the store, i.e. the volatile
    laundering held and the fault was not optimised away. Three readings that
    generalise to ANY fault record, not just these two:
    - **`psr` bit 24 (T) is a free discriminator, and both directions are now
      measured.** Trigger 1 (faulted executing an instruction) returned
      `psr=0xa1000000`, T **set**; trigger 2 (a `blx` to a bit-0-clear address)
      returned `psr=0x00000000`, T **clear**. So the record separates "faulted on
      an instruction" from "faulted trying to enter ARM state" with no symbols at
      all — the stacked xPSR is the state BEFORE the exception.
    - ⚠️ **`lr` is trustworthy only when the fault is AT a call.** Trigger 2
      faulted on the `blx`, so LR still held the return address that `blx` had
      just written and resolved to the calling line (`crash_test.c:66`). Trigger 1
      faulted a few instructions later, so LR was a stale leftover from the
      previous `bl` and resolved to `chThdSleep` — the `wait_ms(25)` in
      `release_the_chord()`, nowhere near the bug. The tell is whether
      `addr2line lr` lands adjacent to `pc`'s function; ARMv6-M stacks no call
      chain, so this is never a backtrace.
    - ⚠️ **`addr2line` maps a DATA address to a symbol and it reads like an
      answer.** Trigger 2's `pc=0x20000000` resolved to `__overlay_pool_base__`
      (`overlay.c:42`) — the overlay pool, which is not code. **Check the RANGE
      first**: code is XIP flash `0x10……`, SRAM is `0x20……`, so a `pc` in SRAM
      means "branched into nowhere" whatever symbol addr2line prints.
  - ⚠️ **`reason` carries a STICKY `HAD_POR`, so the host renders a watchdog
    reboot as "reset reason: POR, watchdog forced".** Both hardware records read
    `reason=0x21` = `HAD_POR | WD_FORCE` on a board that had been up 148 s and
    363 s and was never power-cycled — the fault handler's `watchdog_reboot()`
    brought it back. `WD_FORCE` set with `WD_TIMER` clear is the informative half
    (and is the qmk#271 discriminator working); leading with POR reads as a power
    cycle and would send a real diagnosis the wrong way.

## Sub-step paint breadcrumbs (0xSS80..0xSSBF, 0x1S80..0x1SBF)

A master wedged at "63%, 4 / 4" with the "4" half drawn left `phase=1:0x0504`: the
sub-step's status-panel paint started and never returned. Every panel write has a
100 ms I2C timeout, so the paint cannot hang by itself; something stopped core0
servicing that timeout. `core1_trampoline` already masks core1's IRQs as its first
instruction, so the core1 launch window the `cpsid i` fix was about is closed.

To name the stall, a sub-step paint renders one OLED block per call and stamps each
call first (`boot_paint_mark()`):

    arg = (step | core1_entered << 4) << 8 | 0x80 | ((sub - 1) & 3) << 4 | call

The core1 flag is bit 12, in the HIGH byte, so the low byte stays in `0x80..0xBF` and
never collides with the in-milestone marks `0xE1` / `0xE2`.

A MILESTONE's panel paint (`splash_progress()` → `oled_boot_progress()`, stamped
`0xSSE1` before it) is stamped the same way in `0xC0..0xCF`:

    arg = (step | core1_entered << 4) << 8 | 0xC0 | call

`0x16E1` therefore means "stamped, first render call not reached"; `0x16C3`, the
fourth call in flight. Firmware before this change only ever leaves `0xE1` there.

`call` is the 0-based ORDINAL of the render call, not a physical block number: QMK's
OLED driver keeps its dirty mask private, and each call renders the next dirty block in
ascending order. So `call = n` means n blocks had already gone out.

`core1_entered` is `g_core1_entered`, set by core1 inside `core1_entry()` once its IRQs
are masked and cleared by core0 before the launch. Reading a record:

| arg | means |
|---|---|
| `0x0504` | sub-step 4 stamped, paint not reached (or older firmware) |
| `0x15B3` | step 5, core1 in its entry, sub-step 4, fourth render call in flight |
| `0x05B3` | the same with core1 NOT yet in `core1_entry()` |
| `0x15E2` | step 5 logo draw (keycap SPI) in flight, core1 in its entry |
| `0x05E2` | the same with core1 NOT yet in `core1_entry()` (or firmware before 1.3.2) |
| `0x16E1` | step 6 milestone panel paint stamped, first render call not reached (or older firmware) |
| `0x16C3` | step 6 milestone panel paint, fourth render call in flight, core1 in its entry |

After the paint returns the tag goes back to the plain sub-step (`0x0504`), so a
record naming a call always means the paint was in flight.

## Boot hang: an IRQ nested into USB (the `0x16C1` / `0x16E1` stall)

**Symptom.** The master stops at "63%, 4 / 4" (or "75%"), the late-boot guard resets it
8 s later, and the record names the step 6 milestone panel paint: `phase=1:0x16E1` on
fw 1.3.2 in the field, `0x16C1` / `0x16C2` once the per-call stamps existed. Only the
master (the USB half), and far more often after a host-issued reboot (cmd 43) than
after a power-on, because the host re-enumerates and polls the keyboard while the boot
paints the status panel over I2C.

**Cause, as far as it was measured.** The I2C0 interrupt (priority 2) preempted a
running USB interrupt (priority 3), and core0 then executed from a garbage address
instead of the I2C handler. Nothing else ran: no timer interrupt (same priority as
I2C, so blocked), so even the 100 ms I2C timeout never fired. The fix is the priority:
I2C0 and SPI0/1 sit at the USB priority in both variants' `mcuconf.h`, so they can no
longer nest into it. What inside that nesting corrupts the jump was NOT established.

**Evidence, in the order it was gathered** (all on the host's boot-loop test,
`polyctl bootloop`, which reboots with cmd 43 and reads cmd 39 after each boot):

| Build | Result | What it ruled in or out |
|---|---|---|
| #337 (cmd 43), stock | hang in round 2-5, every run | baseline: ~1 boot in 3 |
| core1 launched after boot | hang, record `0x06C1` (core1 not running) | not core1 |
| + IRQ census (entries per vector since the last stamp, in NOLOAD RAM) | split-link PIO 0, timer 0, last IRQ never left | not the split link / slave, not a console print |
| ChibiOS `CH_CFG_SMP_MODE FALSE` | hang | not SMP (the core0 FIFO IRQ, the kernel spinlock) |
| + NMI on TIMER alarm 3, vector table in RAM | 1 hang in 43; NMI captured `pc=0x13AE165E lr=0xFFFFFFF1 IPSR=39 (I2C0)`, USB handler open, thread in WFI | core0 jumped to garbage on entering the I2C0 IRQ while USB was running |
| I2C0 priority 2 -> 3, otherwise stock | 0 hangs in 217 consecutive boots | the fix |
| I2C0 + SPI0/1 at 3 (as merged, #338) | 0 hangs in 845 consecutive boots | the shipped change; any remaining rate < 1 in 280 (95%) |

Hangs were also seen before the 200 MHz clock, so clock margin was not pursued.

**SPI is included as a precaution, not on evidence that it ever hung.** The keycap SPI driver completes through a
DMA interrupt at `RP_IRQ_SPIx_PRIORITY`, which was 2 as well, and an fw 1.0.0 master
hung at `0x05E2`, the keycap SPI draw. The 845-boot run shows the SPI change regresses
nothing; it cannot show SPI was ever a trigger. The timer alarms (2) and the split-link PIO
interrupt (`CORTEX_MAX_KERNEL_PRIORITY`) can still preempt USB; nothing has implicated
them, and lowering either changes kernel or split-link timing.

⚠️ **Do not raise I2C0 or SPI0/1 above the USB priority again** without re-running a
long boot loop (hundreds of rounds) against it.

## Instruments for a hang with no frame

A watchdog record has no stacked frame: `pc`, `lr`, `sp`, `xpsr` and `icsr` are zero,
and the breadcrumb is all there is. Two probes turned the `0x16C1` hang from "it stalled
in this paint call" into a captured program counter. Neither ships; both lived on local
experiment branches and are recorded here so the next hunt does not re-derive them.
Both write into a struct in `.ram0` (NOLOAD, survives a watchdog reset, random after a
power-on, so give it a magic) and `crash_record_init()` copies it into the frame words of
the synthesised watchdog record, where `polyctl crash show` prints them.

**1. IRQ census: which interrupts ran during the stall.** ChibiOS calls
`CH_CFG_IRQ_PROLOGUE_HOOK()` / `CH_CFG_IRQ_EPILOGUE_HOOK()` in every `OSAL_IRQ_HANDLER`.
A keyboard-level `split72/chconf.h` overrides them:

```c
#pragma once
#include_next <chconf.h>
#if !defined(_FROM_ASM_)
void irq_census_enter(void);
void irq_census_exit(void);
#endif
#undef  CH_CFG_IRQ_PROLOGUE_HOOK
#define CH_CFG_IRQ_PROLOGUE_HOOK() { irq_census_enter(); }
#undef  CH_CFG_IRQ_EPILOGUE_HOOK
#define CH_CFG_IRQ_EPILOGUE_HOOK() { irq_census_exit(); }
```

`irq_census_enter()` reads IPSR (`mrs %0, ipsr`), counts per vector (TIMER 16..19, USB
21, PIO1 25, I2C0 39), and keeps a nesting depth plus the vector last entered; the exit
hook decrements the depth. Reset the counters from `crash_phase_enter()` whenever the
phase is `CRASH_PHASE_BOOT`, so the record describes the window since the last
breadcrumb. Check the disassembly: every `VectorXX` must call the exit hook before
`__port_irq_epilogue` on every return path. ⚠️ After adding the `chconf.h`, clear
`.build/obj_*` (BUILD_ENVIRONMENT.md).
Read it like this: zero timer interrupts across an 8 s stall means core0 never left
interrupt context (the alarm has the same priority as I2C). A non-zero depth names the
handler that never returned.

**2. Timer NMI: the exact stuck PC.** Arm TIMER alarm 3 (`ALARM3 = TIMERAWL + 6 s`,
`INTE` bit 3) at each breadcrumb from step 5 on, and route its IRQ to core0's NMI with
`SYSCFG PROC0_NMI_MASK |= 1 << 3` (0x40004000). Only arm it while the late-boot
watchdog is running: earlier steps can legitimately take longer than 6 s. Disarm it in
`crash_watchdog_start()`. The handler must run from RAM and must chain:
- ⚠️ **ChibiOS uses the NMI for its own context switch** (`NMI_Handler`,
  `chcore.c`), so the entry checks `TIMER_INTS & 8` and branches to `NMI_Handler`
  untouched when it is clear.
- ⚠️ **`-Wl,--wrap=NMI_Handler` does nothing**: `vectors.S` binds the symbol locally,
  so the vector table keeps the original. Copy the 48-word table to a 256-aligned RAM
  array, replace entry 2 with the probe entry, and point `SCB->VTOR` at it. With the
  table and the handler (`.time_critical.*`) in RAM, the NMI still fires if a flash
  fetch has stalled. If the record still shows no PC, that itself says the bus or the
  core stopped.
- The capture reads the frame from MSP or PSP by `EXC_RETURN` bit 2: stacked PC, LR
  and xPSR, plus `psp[6]` for the interrupted thread. Then it lets the watchdog
  reset. If the watchdog is not running yet, enable it with the pico-sdk
  `WATCHDOG_NON_REBOOT_MAGIC` in scratch 4, so the next boot reads the reset as a
  hang.

⚠️ **The probe moves the vector table, and that changed the hang rate from 1 in 3 to
1 in 43.** A probe that alters timing can hide the fault it is aimed at. Keep the run
going until it does fire, and A/B the probe's side effects separately (the
`ramvtor-only` build) before reading a clean probe run as a fix.

## A crash, a hang, or a board that "just stopped"

_Moved verbatim from `CLAUDE.md` on 2026-10-10. CLAUDE.md keeps a short pointer._


**How a fault is recorded, rebooted through and announced on the next boot is
[`keyboards/polykybd/CRASH_DIAGNOSTICS.md`](CRASH_DIAGNOSTICS.md)** —
the phase breadcrumb, the 8 s watchdog, cmd 39 / `polyctl crash show`, and the boot
window's own instruments. Read it before chasing any "it froze and a replug fixed it".
Three rules that bind code outside it:

- ⚠️ **BOOT is the one unwatched window**, so a stall there is permanent: no reset, no
  record, **and no console output at all** — `console_task()` and
  `usb_event_queue_task()` are MAIN-LOOP calls and `keyboard_init()` has not reached
  the loop. The status panel is the only live channel a wedged board has; do not
  conclude "it printed nothing" from one that structurally cannot carry it.
- ⚠️ **The final boot render is ~40 blocking `spiSend()` calls with no timeout**
  (`osalThreadSuspendS`), which is where a cold-boot wedge has actually landed. A
  watchdog guard covers boot step 5 (core1 up, 63%) through that render, armed with
  `crash_watchdog_arm()` — never `crash_watchdog_start()`, which declares the boot
  survived. A new slow step in that window must feed it.
- ⚠️ **A watchdog reset runs NO code**, so it never reaches the crash-loop halt in
  `record_and_reboot()`. Any watchdog armed inside boot must be one-shot, or a hang
  that recurs every boot becomes a reboot loop.
- ⚠️ **No peripheral IRQ may preempt the USB IRQ** — I2C0 and SPI0/1 sit at the USB
  priority (3) in both `mcuconf.h` files. (`RP_IRQ_I2C1_PRIORITY` is 2, but I2C1 is
  disabled, `RP_I2C_USE_I2C1 FALSE`; SysTick and the timer alarms are also at 2.) I2C0 at 2 nesting into a running USB IRQ sent
  core0 to a garbage address: the `0x16C1`/`0x16E1` boot hang, ~1 boot in 3 under a
  reboot loop, 0 in 845 after the change (I2C0 + SPI at 3). The evidence is CRASH_DIAGNOSTICS.md →
  *Boot hang: an IRQ nested into USB*.
- **A boot hang is reproduced with the host's boot loop, not by waiting for the field**
  (`polyctl bootloop --rounds N`, cmd 43). It brought the `0x16C1` hang from "now and
  then" to about 1 boot in 3, because the host re-enumerates and polls the keyboard
  while the boot runs. ⚠️ **Size every A/B from the rate: ruling out a hang of 1 in N
  at ~95% takes about 3·N clean rounds.** 17 clean rounds against a 1-in-3 baseline is
  already conclusive; 845 clean rounds bound what is left at 1 in 280. The
  `hunt-boot-hang` skill drives the loop.

