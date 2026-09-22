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
- ⚠️ **BOOT IS THE ONE UNWATCHED WINDOW, and that is why a boot hang stays
  unexplained.** `crash_watchdog_start()` is the LAST line of
  `keyboard_post_init_user()` — deliberately, because the steps above it may block for
  seconds — so the whole of `pre_init` + `post_init` runs with no watchdog. A stall
  anywhere in there is PERMANENT: no reset, no record, no console line, and the board
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

  Four things now survive it, none of which needs the boot to finish:

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
  - a **watchdog guard across that render only** (below), which turns the wedge into a
    reset that records.
  - `usb_watch()` samples `USBD1.state` per key and, on a change, displaces the label
    line with `USB 4>2 @18` — ACTIVE(4) -> READY(2) is a bus reset, (5) a suspend. One
    volatile read, no hook in the USB stack. It exists because the boot-render hang
    reproduced only when a MacBook was COLD-BOOTED with the keyboard attached, i.e.
    while EFI enumerates and the kernel then resets the bus, all inside this window.
  - ⚠️ **The instrument is not free, and it is in the window it measures.** Five
    per-row panel paints add 25-40 ms of I2C to a ~100 ms render — enough to move a
    timing race, and the 2-in-3 repro stopped once the instrumented build was flashed
    (unproven either way, n is small). The fall-back if it stops reproducing for good
    is breadcrumb + watchdog with NO paints: the record still names the key after the
    reset, and only the live readout is lost.

  ⚠️ **Arming the watchdog earlier is NOT a free fix**, which is why it is still not
  armed across the whole of post_init. `crash_watchdog_start()` also sets
  `consecutive = 0`, and reaching it is the definition of "this boot succeeded" for the
  crash-loop halt; arming during boot changes what that counter means. And 8 s is the RP2040 MAXIMUM, so any single
  milestone gap that legitimately exceeds it turns a rare hang into a permanent reboot
  storm — on a path where the first gap spans QMK's split and USB init, i.e. the very
  thing that blocks when the other half is missing. It needs measured per-milestone
  boot timings first.

  What #301 does instead is arm it across the **final render alone** — every slow step
  is above that line — through two deliberate pieces:

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
    under it (`kind=watchdog`, phase BOOT, high byte `POLY_SPLASH_STEPS`). One reset,
    one record, then the old wedge — which BOOTSEL still recovers, and which leaves the
    panel readable for a photograph instead of resetting it away every 8 s.

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
  - **`-e POLYKYBD_CRASH_TEST=yes` is the by-hand route** (`crash_test.c`, a
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
