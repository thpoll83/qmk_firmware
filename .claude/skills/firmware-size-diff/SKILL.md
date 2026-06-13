---
name: firmware-size-diff
description: Build the PolyKybd firmware twice — at HEAD (baseline) and with the current working-tree changes — then diff the resulting binaries. Reports (a) size deltas: total .uf2 bytes plus ELF section sizes (.text/.data/.bss, ROM/RAM); and (b) code-identity: whether the .text section is byte-for-byte identical, with a disassembly diff when it isn't. Use for both feature work ("did this bloat flash?") and refactors ("did this produce identical machine code?").
---

# Firmware byte/size diff

Two questions this skill answers:

1. **Size delta** — for new features / optimisations: did flash/RAM usage move, and by how much?
2. **Code identity** — for refactors that should be behaviour-preserving: is the generated `.text` byte-for-byte identical to baseline? If not, *where* did it diverge?

Repo root: `/home/thomaspollak/Repos/qmk_firmware`
Build output: `.build/handwired_polykybd_split72_default.{elf,uf2}`
Toolchain on PATH: `arm-none-eabi-size`, `arm-none-eabi-objcopy`, `arm-none-eabi-objdump`, `arm-none-eabi-nm`.

## When to expect which result

| Change kind | Expected size delta | Expected .text identity |
|---|---|---|
| Pure refactor (rename, extract function, reorder) | ~0 B | **Identical** — if not, investigate |
| Inlining / `static` change | small | Likely differs |
| New feature / new code path | >0 B | Differs |
| Compiler-flag change | varies | Differs everywhere |

Tell the user up-front which mode applies based on the nature of the change (or ask if ambiguous).

## Procedure

1. **Sanity check** in `qmk_firmware/`:
   - `git status --porcelain` — if no tracked changes, stop with "nothing to diff".
   - Note current branch + HEAD SHA so you can confirm restoration at the end.

2. **Stash the working changes** so HEAD is the baseline:
   ```bash
   git stash push -u -m "firmware-size-diff-after"
   ```
   If the stash is empty, abort.

3. **Clean + build baseline.** A clean build is required for trustworthy `.text` comparison — incremental builds can leave stale objects from the "after" state in `.build/`:
   ```bash
   make handwired/polykybd/split72:default:clean
   make handwired/polykybd/split72:default -j$(nproc)
   cp .build/handwired_polykybd_split72_default.elf /tmp/fw-baseline.elf
   cp .build/handwired_polykybd_split72_default.uf2 /tmp/fw-baseline.uf2
   arm-none-eabi-objcopy -O binary --only-section=.text /tmp/fw-baseline.elf /tmp/fw-baseline.text.bin
   ```
   On failure: `git stash pop` and report.

4. **Restore working changes and clean-build candidate:**
   ```bash
   git stash pop
   make handwired/polykybd/split72:default:clean
   make handwired/polykybd/split72:default -j$(nproc)
   cp .build/handwired_polykybd_split72_default.elf /tmp/fw-after.elf
   cp .build/handwired_polykybd_split72_default.uf2 /tmp/fw-after.uf2
   arm-none-eabi-objcopy -O binary --only-section=.text /tmp/fw-after.elf /tmp/fw-after.text.bin
   ```

5. **Identity check (`.text`)** — for refactors this is the headline result:
   ```bash
   sha256sum /tmp/fw-baseline.text.bin /tmp/fw-after.text.bin
   cmp /tmp/fw-baseline.text.bin /tmp/fw-after.text.bin && echo IDENTICAL || echo DIFFERS
   ```
   - If **identical**: report it plainly — the refactor produced bit-exact machine code. Still print the size table from step 6 for completeness (will be all zeros for `.text`).
   - If **differs**: in addition to the size table, produce a disassembly diff scoped to the changed region:
     ```bash
     arm-none-eabi-objdump -d --no-show-raw-insn /tmp/fw-baseline.elf > /tmp/fw-baseline.asm
     arm-none-eabi-objdump -d --no-show-raw-insn /tmp/fw-after.elf    > /tmp/fw-after.asm
     diff -u /tmp/fw-baseline.asm /tmp/fw-after.asm | head -200
     ```
     Also list the symbols whose size changed:
     ```bash
     arm-none-eabi-nm --size-sort --print-size /tmp/fw-baseline.elf > /tmp/syms-baseline.txt
     arm-none-eabi-nm --size-sort --print-size /tmp/fw-after.elf    > /tmp/syms-after.txt
     diff /tmp/syms-baseline.txt /tmp/syms-after.txt | head -80
     ```
     Summarise the top 5 functions by `|Δ size|`.

6. **Size delta** (always report):
   - `arm-none-eabi-size -A /tmp/fw-baseline.elf /tmp/fw-after.elf` → table with absolute and delta for `.text`, `.data`, `.bss`, plus computed **ROM** (`.text + .data`) and **RAM** (`.data + .bss`).
   - `wc -c /tmp/fw-baseline.uf2 /tmp/fw-after.uf2` → uf2 byte count + delta + percentage.
   - `cmp -l /tmp/fw-baseline.uf2 /tmp/fw-after.uf2 | wc -l` → count of differing bytes in the uf2.
   - Highlight any section that moved by >1 KB.

7. **Restore state**: working tree was popped in step 4. Confirm `git status --porcelain` matches what it was before the skill ran, and confirm HEAD is unchanged.

## Output format

Lead with the verdict appropriate to the change kind:

**Refactor mode:**
```
.text:  IDENTICAL  (sha256 6e2a…)  — refactor is bit-exact ✓
ROM:    ±0 B       238144 B
RAM:    ±0 B       54912  B
UF2:    ±0 B       482304 B
```

Or, if not identical:
```
.text:  DIFFERS  (baseline 6e2a…  after 91c4…)
        Top symbol deltas:
          fill_overlay_decompress  +24 B
          poly_suspend             -8  B
ROM:    +16 B   238144 → 238160 B
RAM:    ±0  B    54912 → 54912  B
UF2:    +32 B   482304 → 482336 B   (12 differing bytes)
```

**Feature/size mode:**
```
ROM:  +312 B (.text +312, .data ±0)   238144 → 238456 B
RAM:  ±0   B (.data ±0, .bss ±0)       54912 → 54912  B
UF2:  +624 B (0.13%)                  482304 → 482928 B
Differing bytes in .uf2: 1184
```

Also add a last line with a percentage number of how much RAM of the total 264kB is already used.

## Notes / pitfalls

- **Spurious `.text` differences in refactor mode**: macros like `__FILE__`, `__DATE__`, `__TIME__`, `__LINE__` and build timestamps embedded by QMK can make `.text` differ even when behaviour is unchanged. Common culprits:
  - QMK build date string in `version.h` — rebuild within the same minute or check whether the only differing region is the version string.
  - `__FILE__` paths inside `assert()` / log calls — if a refactor moves code between files, expect string-table churn.
  - If the disassembly diff is only inside `.rodata`-referencing loads with no instruction changes, the bytes differ in `.rodata`, not the code. Confirm with `arm-none-eabi-size -A` showing `.text` unchanged but `.rodata` moved.
- **Variants**: default to `split72`. Diff `split42` too if the user asks, or if `git stash show --name-only stash@{0}` (run before the stash pop) lists files under `keyboards/handwired/polykybd/split42/` or the shared `keyboards/handwired/polykybd/poly_keymap.c` (the latter affects both variants). (`split42` was formerly `corne42`.)
- **Codegen outputs**: `lang/lang_lut.c` and `base/fonts/generated/*.h` come from cog/`create_fonts.sh`. If they're touched, mention that size changes may reflect regenerated data rather than hand-written code.
- **Clean builds are required** for both passes — see step 3 rationale. Don't try to optimise this away.
- **Don't** modify the user's branch, push, or amend. The skill only uses `git stash`.
