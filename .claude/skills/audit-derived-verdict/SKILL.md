---
name: audit-derived-verdict
description: Audit a decision that is computed from several inputs and then CACHED (a latch, memo, "pristine" flag, one-shot refusal) or DELEGATED to the other split half over poly_sync_t — checking that the cache key carries every input the decision reads, that every site which invalidates an input also drops the cache, and that a delegated verdict names the subject it was given for. Use when adding or reviewing such a cache or sync field, and when a symptom reads "it refused/accepted when it should not have", "the prompt never appeared", "it cached the wrong answer", "the slave disagrees with the master", or "re-flashing changed nothing". NOT for ordinary data caches where the value IS the key (overlay images, font glyphs), NOT for a cache's SIZE or cost (that is measure-firmware-perf), and NOT for test coverage of one (that is mutation-test-suite).
---

# Audit a cached or delegated verdict

A verdict is a decision computed from several inputs — `doom_pack_gate(sig, entry,
auth, crc)`, "is the F-row still pristine", "has the user accepted this image". Two
things routinely get done to one, and both can silently drop an input:

- **Cached** — the verdict is expensive, so it is latched under a key and reused.
- **Delegated** — the verdict was reached on the master and shipped to the slave,
  which never saw the evidence.

The bug is the same in both: **the stored or shipped form carries fewer inputs than
the decision read.** It never errors. The cache returns a confident wrong answer, or
the slave applies the master's answer to a different subject. This repo has been
caught by each, one PR apart (#298), and by the same shape in three other places.

## The rule, in one line each

1. **A cache key is the decision's whole argument list.** Every parameter the
   verdict function reads belongs in the key, or the cache serves one question's
   answer to a different question.
2. **A key derived from content must cover the content the verdict is about.** A
   CRC over part of a blob does not key a decision about the rest of it.
3. **Every site that changes an input must drop the cache** — including the sites
   that fail. A half-written update invalidates a cached verdict as thoroughly as a
   good one.
4. **A delegated verdict must name its subject.** Ship "I accepted THIS" (an id,
   CRC, version), never "I accepted something". The receiver must re-check the
   subject against what it actually holds and fail closed on a mismatch.
5. **Only the widened verdict may be delegated.** Whatever the receiver can decide
   for itself — an invalid signature, a size overflow — it still decides locally.

## Procedure

### 1. Name the verdict function and enumerate its real inputs

Find the function that produces the decision and list **every** value it reads —
parameters *and* globals/state it consults. The parameter list is the floor, not the
answer: a verdict that calls `get_local_state()` has that state as an input too.

```bash
# the decision
sed -n '/enum .*_gate(/,/^}/p' keyboards/polykybd/doom/doom_pack_gate.h
# everything it touches
grep -n "get_local_state\|is_usb_host_side\|eeconf\|timer_read" <verdict>.c
```

Write the list down. It is the specification the next two steps check against.

### 2. Diff the list against the cache key

```bash
grep -n "s_.*_valid\|_latch(\|_still_stands(\|_clear(" <file>.c
```

For each input from §1, ask: is it in the key? A missing one is the bug. The
classic misses:

- **A mode/entry/caller flag.** The verdict differs by who asked, and the key does
  not say who asked.
- **State that changes between calls** — an authorisation, a layer, a connection.
- **The content the verdict is about**, when the key is a digest of only part of it.

### 3. Prove the key with two calls that must differ

Before fixing anything, write the test that makes the property explicit, in the
pure-decision suite (`base/tests/`):

```cpp
// The key must respect this: the same subject, differing only in <input>,
// must be able to give different verdicts.
EXPECT_NE(verdict(subject, INPUT_A), verdict(subject, INPUT_B));
```

If that passes, a key without `<input>` is provably unsound. Add the converse too —
the case where the input stops mattering — so a later reader knows the key is
minimal as well as sufficient.

### 4. Find every invalidation site

```bash
grep -rn "<the thing the key is derived from>" keyboards/polykybd --include=*.c
```

Every write to it needs a `*_clear()`. Check especially:

- the **failure** paths (a partial write still changes the bytes);
- the **other half** — a slave-side write needs the slave-side clear;
- **staging/finalize** hooks, where the content lands in flash.

⚠️ Keep the clear **O(1)** if it can run inside a split-transaction handler. Never
do real work there: re-CRCing a pack inside a ~20 ms RPC callback once made a
perfect flash report as a CRC failure.

### 5. For a delegated verdict, check the binding and the receiver

```bash
grep -n "<sync field>" keyboards/polykybd/state.h keyboards/polykybd/poly_keymap.c \
                      keyboards/polykybd/<consumer>.c
```

- Is the field a **bare flag**? Then it names no subject, and the receiver is
  applying it to whatever it happens to hold. Carry the id/CRC instead.
- Does the receiver **re-check** it against its own copy, and **fail closed** on a
  mismatch or on the sentinel?
- Is the reserved "none" value reachable as a real value? Say which way it fails
  and pin it with a test. Fail-closed is the only acceptable direction for a
  security gate.
- ⚠️ A new 4-byte member in `poly_sync_t` goes **directly after `crc32`** — see
  `SPLIT_SYNC.md`; the split CRC covers every byte from offset 4, so a `uint32_t`
  dropped into the `uint8_t` run puts alignment padding inside the checksummed
  range. And `state.h`'s `static_assert` against `RPC_M2S_BUFFER_SIZE` must still
  hold.

### 6. Re-read the rationale comment

If the cache or the delegation carries a comment explaining why it is sound, that
argument is now suspect too — it was written by whoever made the assumption. Verify
it against the code rather than trusting it (see the worked example below, where the
comment named a mechanism that cannot occur).

### 7. Validate

Run the pure-decision suite plus the ones around it, and build every flavour the
change can reach:

```bash
export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"
GTEST_COLOR=no make test:<suite> 2>&1 | sed -e 's/\x1b\[[0-9;]*m//g' | tail -5
for a in "-e POLYKYBD_DOOM_PACK=yes" "-e POLYKYBD_DOOM=yes" ""; do
    qmk compile -kb polykybd/split72 -km default $a || break   # NEVER in parallel
done
qmk compile -kb polykybd/split42 -km default
```

## Worked example — the DOOM pack gate (#298, 2026-09-19)

Both halves of this skill, found by CodeRabbit in one review and both real.

**The cache.** `doom_pack_gate(sig, entry, auth, crc)` reads four inputs. The
refusal latch keyed on two — `(image_crc, auth)` — omitting `entry`. An unsigned
pack is `REFUSE`d on `AUTOMATIC` and `PROMPT`ed on `INTERACTIVE`, so those are
different questions with one cache slot between them. On a board whose idle style is
the DOOM attract demo, the first idle timeout ran the screensaver's automatic
attempt, latched the refusal, and every later `KC_IDDQD` press returned at the guard
without reaching the gate. **The prompt was unreachable for the rest of the boot —
on exactly the boards most likely to be running an unsigned pack.**

**The content half.** The key is the pack's *declared* `image_crc`, and the Ed25519
trailer sits **outside** it. Signing the same image therefore leaves the key
byte-identical: a developer who flashed a signed build over a refused one kept
getting the refusal. `fw_staging`'s `FW_TARGET_DOOMPACK` finalize now calls
`doom_pack_slot_rewritten()` — unconditionally, not gated on `ok`.

**The delegation.** `poly_sync_t.doom_pack_auth` was a `uint8_t` 0/1: *"something
unsigned was accepted"*. The slave applied it to whatever pack it held. One host
command writes both halves, but it can land on the master and fail on the slave with
nothing reporting it — **the GET_ID slot block covers the master's slots only** — so
accepting pack B ran the slave's leftover pack A, which nobody accepted. The field
is now `doom_pack_auth_crc` (`uint32_t`), and the slave honours it only when it
matches its own header; `0` means "nothing accepted" and a pack whose CRC is
genuinely 0 is refused rather than admitted.

⚠️ **The comment defending the old design was wrong, and it was wrong in the
direction that made the design look safe.** It said the only way to diverge the two
halves was a BOOTSEL flash — "physical access, which is exactly what the prompt asks
for". A UF2 over BOOTSEL writes the **firmware partition** (0–2 MB); the pack lives
in the resource region at `FW_RESOURCE_OFFSET + FW_DOOMPACK_SLOT_OFF`, which it
never touches. The reviewer repeated the same wrong mechanism when reporting the
bug. **§6 exists because of this**: the rationale is part of what you audit.

## Other instances in this repo

Worth checking against §2 and §4 whenever they are touched:

| Cache / delegation | Key or payload | What it must track |
|---|---|---|
| `fl_row_is_pristine` (`poly_keycode_at`) | the 14 compiled F-row slots | every dynamic-keymap write — the `_poly` wrapper invariant |
| overlay mapping repair | one-shot chunk state | nothing re-fires it; the master holds the tables |
| `poly_sync_t.fw_confirm` | a KIND, not a bool | which dialog the board is being |
| `s_auth` (master, FW-9) | `{valid, image_crc}` | already bound — the model the slave now copies |

## Pitfalls

- **Do not "simplify" the key back down** once the tests pass. Each field in it
  bought a bug.
- **A bare flag across the split link is the default mistake**, because it is the
  cheapest thing to add and it compiles. Ask what it names.
- **An unreachable-looking sentinel** (`0` for "none") needs a stated failure
  direction and a test, not an assumption that the collision cannot happen.
- **Do not cache at all** if the verdict is cheap. The latch here exists only
  because the alternative was a ~210 KB CRC walk plus a SHA-512 on every
  housekeeping pass — which, before the cheap blank-trailer test, cost the slave an
  8-second split-link outage.
- **The clear is not a display nicety.** `reset_overlay_mapping()`'s identity
  default is load-bearing for writes; the same is true of these.
