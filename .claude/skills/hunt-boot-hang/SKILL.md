---
name: hunt-boot-hang
description: Chase an intermittent PolyKybd boot hang or watchdog reset to its cause with the host's boot-loop test — reproduce it at a measurable rate, then A/B one change per build (sha-named `.bin`s), size each run from the base rate, and when the crash record has no frame escalate to the IRQ-census and timer-NMI probes. Use when a user reports "it froze at 63% / 75%", "it hung during boot and recovered", a `kind=watchdog` crash record with `pc=0` and a `phase=1:…` boot breadcrumb, or asks "how do we find out why it hangs". NOT for a HardFault record with a real PC (read the PC against the ELF instead), NOT for a red HIL check (diagnose-hil-failure), and NOT for a one-off rig probe (debug-firmware-on-rig).
---

# Hunt a boot hang

A boot hang that "happens sometimes" cannot be fixed by reasoning about code: the
`0x16C1` hunt (2026-10-02, fixed by qmk#338) ruled out five plausible causes, and
each of them looked convincing on paper. What worked was **a reproducer with a known
rate, one change per build, and run lengths sized from that rate.** This skill is
that loop. Background and the probe code: `keyboards/polykybd/CRASH_DIAGNOSTICS.md`
("Boot hang: an IRQ nested into USB", "Instruments for a hang with no frame").

## 0. Read the record first

```
polyctl crash show
```

Decode `phase=1:0xSSLL`: high byte = boot step (bit 12 = core1 entered), low byte =
where inside it (`0x80..0xBF` sub-step paint, `0xC0|call` milestone paint, `0xE1`
panel paint, `0xE2` logo, `0xE3` final dwell, `1..40` final-render key). A
`kind=watchdog` record has `pc=lr=sp=0` — the breadcrumb is all you know. Note which
**half** (`side=master` = the USB half) and whether it repeats at the same breadcrumb:
a fixed spot means a deterministic trigger, a wandering one means a freeze wherever
core0 happened to be.

## 1. Reproduce at a measurable rate

The host's boot-loop test reboots with cmd 43 (protocol v22+) and stops at the first
fresh crash record or a reboot that does not come back in 60 s:

```
polyctl bootloop --rounds 50          # or Developer → Firmware → "Boot-loop test…"
polyctl bootloop --cancel
```

Up to 9999 rounds (host 1.15.0+). ⚠️ After pulling a host branch, restart the
**daemon** too — it enforces the round limit and runs the loop.

Run the **unchanged** build 2–3 times first and write down the round each hang hit.
That is your base rate `p` (the `0x16C1` hang: rounds 2–5, so ~1/3). A host reboot
loop is far harsher than a power-on (the host re-enumerates and polls while the boot
runs), which is exactly why it reproduces what the field sees once a week.

## 2. Size every run from the rate

To claim "fixed" at ~95%, a build must survive about `3 / p` clean rounds; to bound a
residual rate at 1 in N, run about `3·N`. With `p = 1/3`, 17 clean rounds is already a
0.1% fluke and 50 is conclusive; 845 bounds what is left at 1 in 280. Say this number
to the user **before** they start, so a run is not stopped too early or dragged on.

## 3. One change per build

Each experiment is ONE change on top of the reproducing build, on a local `exp/…`
branch (do not push scratch branches: the proxy refuses branch deletion). Build with
the `deliver-test-firmware` mechanics and put the sha in the name:

```bash
export QMK_HOME=$PWD PATH="/root/.qmk_venv/bin:$PATH"
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM_PACK=yes
arm-none-eabi-objcopy -O binary .build/polykybd_split72_default.elf \
  "$SCRATCH/polykybd_split72_<ver>-EXP-<what>-$(git rev-parse --short=7 HEAD).bin"
cp .build/polykybd_split72_default.elf "$SCRATCH/<what>-<sha>.elf"   # keep it: a captured PC needs it
```

Confirm the change is IN the binary (disassemble the function, grep the constant,
`arm-none-eabi-nm` the symbol) before sending it; an experiment flag that silently did
not apply wastes a whole run. Experiment flags go in as a `#define` in the commit, not
`-e EXTRAFLAGS=…` (that breaks the DOOM-pack build).

State, per build, what each outcome would mean ("clean → X is the trigger; hangs with
breadcrumb Y → X is cleared"). Ask the user the round count and the record each time.

## 4. When the record has no frame: probes

If two or three A/B rounds have not isolated it, stop guessing and instrument
(CRASH_DIAGNOSTICS.md → "Instruments for a hang with no frame"):

1. **IRQ census** (ChibiOS IRQ hooks → counters in `.ram0`, written into the record's
   frame words). Tells you which interrupts ran during the stall, whether the timer
   ever fired, and which handler never returned.
2. **Timer NMI** (alarm 3 → core0 NMI, handler and vector table in RAM). Gives the
   exact stuck PC, LR, IPSR and the interrupted thread's PC. Symbolise with
   `arm-none-eabi-addr2line -f -e <kept .elf> <pc>`.

⚠️ A probe changes timing. Moving the vector table to RAM took the `0x16C1` hang from
1 in 3 to 1 in 43; a clean probe run is NOT a fix until the probe's side effect has its
own A/B build.

## 5. Ship the fix

Once one change holds for `3/p`+ rounds and the probe data explains why: open the PR
from a fresh branch with the A/B table in the body and in CRASH_DIAGNOSTICS.md, give
the user a `.bin` of the exact PR head (merged with anything it needs, e.g. cmd 43) for
the long soak, and put the soak count in the docs once it is in.

## Pitfalls

- **Clock, SMP, core1 and the slave all looked guilty and were not.** Test the
  suspect; do not argue it. Also ask the user what they already know ("we had this
  before 200 MHz" removed a whole build).
- **Orange on the slave at a reboot is normal** — the slave's reboot path paints it
  (`poly_board_unusable_cue()`); it is not a hang predictor.
- **The first boot after flashing a probe build reports nothing** when the `.ram0`
  struct moved — expected, not a broken probe.
- **A record that arrives "right away" may be from the previous build's last hang**;
  check the census fields match the build you flashed before reading them.
- **A watchdog record can be archived by a boot that then hung again** before
  repainting the panel (the panel keeps the old "63%" image). If the board "did not
  reboot", read `polyctl crash show` after a replug — the archive may still hold it.
