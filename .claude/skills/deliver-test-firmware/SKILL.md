---
name: deliver-test-firmware
description: Build the PolyKybd split72 firmware and hand the user a flashable `.bin` (plus the paired DoomPack `.plyx`) for testing on real hardware. Use whenever the user asks for "a bin", "a build to test", "give me firmware", "can you build that so I can flash it", or after fixing something that needs a hardware round. Handles the parts that are easy to get wrong: the container's off-PATH `qmk`, the pack-flavour/`.plyx` RAM pairing, `.uf2`-vs-`.bin` (the HID updater takes the `.bin`), the post-build size sanity check, and re-verifying the checkout after a container reclaim. NOT for release builds (use `polykybd-github-release`) or size comparisons (use `firmware-size-diff`).
---

# Deliver a test firmware build

The user flashes over HID via PolyKybdHost's firmware updater, which takes the
**raw `.bin`** — not the `.uf2`. If the DOOM easter egg is in play they also need
the **`.plyx` engine pack built from the same tree**, because the pack's RAM
contract is pinned to that firmware's overlay pool.

## 0. Verify the checkout first

The remote container can silently roll `HEAD` back to an older commit (see the
warning in `CLAUDE.md` § Building & flashing). Building a reverted tree and
shipping it as "the fix" is the failure this step exists to prevent.

```bash
cd /home/user/qmk_firmware && git log --oneline -1 && git status --short
```

If `HEAD` is behind what you pushed:

```bash
git fetch origin <branch> && git reset --hard origin/<branch>
```

## 1. Build

`qmk` is **not on `PATH`** in the session container — it lives in the setup venv.
Every build needs both exports:

```bash
cd /home/user/qmk_firmware
export QMK_HOME=$PWD
export PATH="/root/.qmk_venv/bin:$PATH"
```

**If the tree has DOOM support, build via the pack script** — it builds *both*
flavours (monolith + pack), which is also the only local check that the RAM-tight
monolith still links (PR CI never builds it):

```bash
keyboards/polykybd/doom/pack/build_pack.sh          # ~5 min; 5 stages
```

Otherwise a plain build is enough:

```bash
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM_PACK=yes
```

`build_pack.sh` prints its own failure output (`run_quiet`) — if it dies, the real
compiler/linker error is in the captured dump, not in gmake's `Error 1`.

## 2. Extract the deliverables

```bash
mkdir -p /tmp/deliver          # a reclaim wipes this — always recreate it
arm-none-eabi-objcopy -O binary \
    .build/polykybd_split72_default.elf \
    /tmp/deliver/polykybd_split72_default.bin
cp keyboards/polykybd/doom/pack/doom_pack_v*.plyx /tmp/deliver/   # DOOM builds only
```

## 3. Sanity-check before sending

```bash
arm-none-eabi-size -A .build/polykybd_split72_default.elf | grep -E "overlay_pool|\.heap|\.text"
ls -l /tmp/deliver/
```

Confirm:

- `.overlay_pool` matches `NUM_OVERLAY_SLOTS × 360` (216,000 B at 600 slots) and
  the `RAM_SIZE` in `build_pack.sh` — a mismatch means the `.plyx` is paired to a
  different pool and will be refused at game entry.
- `.heap` is non-trivially positive (free SRAM). A near-zero value means the next
  small feature will fail to link.
- The `.bin` is a plausible size (~430 KB) and **newer than the source edit**.

## 4. Send

```
SendUserFile(files=[".../polykybd_split72_default.bin", ".../doom_pack_v3.plyx"],
             display="attach", caption="<what changed, one line>")
```

State the flash order in the reply: **`.bin` first** (it reboots), then
`polyctl doom install-pack doom_pack_vN.plyx`.

## Pitfalls

- **`.uf2` is not the deliverable.** It is only for manual bootloader-drive
  recovery. The HID updater takes the `.bin`.
- **`qmk: command not found`** → the `PATH` export in §1 was skipped. With the old
  `>/dev/null` this surfaced as a bare `Error 1`.
- **`/tmp/deliver: No such file or directory`** → a container reclaim wiped it.
  `mkdir -p` every time; do not assume it survived from an earlier build.
- **Never ship a build you have not re-verified after a reclaim** (§0). A reverted
  tree compiles perfectly and silently ships the pre-fix code.
- **Bump `PACK_VERSION` when the pool size changes**, and re-ship the `.plyx` with
  the `.bin`. A stale pack fails closed (`doom_pack_load.c` rejects
  `ram_size > pool_size`), but the user then sees the fire demo instead of DOOM
  and will report it as a regression.
- **The pack flavour pins the pool at `0x20000000`.** `build_pack.sh` hard-fails if
  that regresses — do not "fix" that assert by relaxing it.
- Say plainly what was and was not tested. These builds are unflashed by you; the
  hardware round is the user's.
