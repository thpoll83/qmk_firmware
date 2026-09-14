---
name: prune-claude-md
description: Shrink a CLAUDE.md that has grown past its useful size by moving whole subsystems into docs/ behind pointers and into path-scoped rules, WITHOUT losing a line — measure per-section bytes, classify each block as binds-outside vs read-only-while-inside, cut with a byte-identical verification, re-base the relative links the move breaks, and prove losslessness by diffing every original line back. Use when asked to "reduce the size of CLAUDE.md", "this file is too big", "trim the context", "move this into docs", "why does every session load 400 KB", or when a CLAUDE.md has grown past a few hundred lines. NOT for writing new CLAUDE.md content (that is session-retro) and NOT for deleting anything — this skill never removes information, it relocates it.
---

# Prune a CLAUDE.md without losing information

Every `CLAUDE.md` in an attached repo loads **in full at every session start**, so a
repo's accumulated notes are a per-session tax whether or not the session touches that
subsystem. These files are append-heavy by design and grow back; this is a recurring
job, not a one-off.

**Measured on 2026-09-14 across the four PolyKybd repos: 417,724 B -> 120,234 B (-71%),
zero prose lines lost.** Per repo: qmk 180,681 -> 52,108; host 136,850 -> 30,060; ctnd
73,396 -> 26,528; hardware 26,797 -> 11,538.

## The three destinations, and what decides between them

| destination | loads when | right for |
|---|---|---|
| stays in `CLAUDE.md` | every session | a rule that binds code **outside** its own file |
| `docs/*.md` + a pointer | read on demand | the subsystem's own reference and history |
| `.claude/rules/*.md` with `paths:` | a matching file is Read | a warning you want surfaced automatically while editing that file |
| a skill | invoked | a repeatable multi-step **procedure** |

**The single question for every block: does a reader who never opens this file still need
this?** A note about `fill_overlay.c` that only matters while editing `fill_overlay.c` is
a path-scoped rule. A note saying *"change this and a mirrored copy in two other repos
silently decodes wrong data"* must stay inline.

⚠️ **A path-scoped rule fires on the Read tool, NOT on `cat`/`sed` through Bash.** Under
an auto-mode session that reads with Bash it never loads. It **supplements** the
CLAUDE.md pointer; it never replaces one. Verify the mechanism once per environment
before trusting it: write a rule with `paths: ["some/file.py"]` and a unique marker
string, `Read` that file, and check the marker appears.

⚠️ **A shared `CLAUDE-SHARED.md` imported by both repos does NOT reduce context** —
imported files load at launch. Single-sourcing fixes drift and buys zero tokens.

## Procedure

### 1. Measure before deciding

```bash
python3 - <<'EOF'
import pathlib, re
for p in ["repo_a/CLAUDE.md", "repo_b/CLAUDE.md"]:
    lines = pathlib.Path(p).read_text().splitlines(keepends=True)
    idx = [i for i, l in enumerate(lines) if re.match(r'^## ', l)] + [len(lines)]
    print("="*70); print(p, sum(len(l) for l in lines), "bytes")
    for a, b in zip(idx, idx[1:]):
        print(f"   {sum(len(l) for l in lines[a:b]):7d}  {lines[a].strip()[:66]}")
EOF
```

Re-run it scoped to `^### ` inside whichever `##` section dominates. Byte counts, not
impressions, decide what is worth moving — and they tell you when to stop.

Also find cross-file duplication, which is pure deletion rather than relocation:

```bash
python3 - <<'EOF'
import pathlib, re, hashlib
from collections import defaultdict
blocks = defaultdict(list)
for p in ["repo_a/CLAUDE.md", "repo_b/CLAUDE.md"]:
    for para in re.split(r'\n\s*\n', pathlib.Path(p).read_text()):
        s = para.strip()
        if len(s) > 200: blocks[hashlib.md5(s.encode()).hexdigest()].append((p, len(s)))
for h, v in blocks.items():
    if len({x[0] for x in v}) > 1: print(f"{v[0][1]:6d} B x{len(v)}", sorted({x[0] for x in v}))
EOF
```

### 2. Cut with `move_blocks.py`

`move_blocks.py` (beside this SKILL.md) cuts blocks by line range, appends each
**verbatim** to its destination, inserts your pointer, and asserts the moved bytes are
byte-identical in the destination. Cuts are applied **bottom-up** so earlier line numbers
stay valid.

```bash
python3 .claude/skills/prune-claude-md/move_blocks.py spec.json
```

`spec.json` is a list of `{src, start, end, dst, title, pointer}` — `start`/`end` are
1-based line numbers (`end` exclusive; `null` = EOF), `title` is used only when `dst` does
not yet exist, `pointer` is the replacement text. Append to an existing doc by naming it
as `dst` with `title: null`.

⚠️ **Do NOT cut by `s.replace(anchor, new)`.** Python does exactly what it is told and
silently deletes whatever sits between the anchor and the replacement when the
replacement does not repeat the anchor's opening lines verbatim — nothing errors, and the
file still reads as a sentence. If you must, `assert s.count(anchor) == 1` first.

### 3. Write the pointer

A good pointer is 4–20 lines: one sentence saying what the subsystem is, the link, and
**only** the rules that reach outside it. Cut the narrative; keep the measurement and the
imperative.

Before: ten paragraphs on how a `uint8_t` page buffer bricked boards, the bisect that
blamed an unrelated commit, and the disassembly that misled for ten rounds.
After: *"The self-apply's page buffer must be `uint32_t` — a `uint8_t` one word-copied
through a cast has alignment 1 and HardFaults the M0+. When a bisect blames a commit that
cannot have touched the failing code, check whether it moved that code's DATA."* Plus the
link.

### 4. Re-base the links the move broke — this is the step that fails silently

A move re-bases every relative path in the moved text, and nothing runs prose.

```bash
python3 - <<'EOF'
import pathlib, re
for f in [pathlib.Path("CLAUDE.md")] + list(pathlib.Path("docs").rglob("*.md")):
    for m in re.finditer(r'\]\(([^)#][^)]*)\)', f.read_text()):
        t = m.group(1)
        if not t.startswith(("http", "#")) and not (f.parent / t).exists():
            print("BROKEN", f, "->", t, "=>", (f.parent / t).resolve())
EOF
```

Typical fix after moving `CLAUDE.md` text into `docs/`:
`sed -i 's|](docs/|](|g' docs/moved.md`. ⚠️ **Read the RESOLVED path**, not the exists()
bit — a link into a sibling repo always reports missing in a session that did not attach
it, and is not broken. Watch for `../` depths that changed by more than one level.

### 5. Promote the extracted headings

⚠️ **A section cut from under an `##` keeps its `###`, and standalone under a `#` title
that skips a level** (markdownlint MD001). This is not a lint nit: it happened on the
2026-09-10 pass (qmk#290), got fixed as a follow-up (qmk#291), and **the same procedure
reproduced it four days later** — 21 files, 30 headings. Expect to hit it; check for it.

```bash
# report any file whose first section heading skips a level
for f in $(git show --stat --name-only --format= HEAD | grep '\.md$'); do
  nxt=$(awk '/^```/{c=!c} !c && /^#{1,6} /{n++; if(n==2){print length($1); exit}}' "$f")
  [ "$nxt" -gt 2 ] 2>/dev/null && echo "SKIPS LEVEL: $f"
done
```

Three checks before promoting, all from #291:

- **Nothing nests past `###`**, or promotion collides two levels into one.
- **No heading sits inside a code fence** — a shell comment at line start (`# build it`)
  is indistinguishable from a heading to `sed`. Track fence state.
- **Anchors derive from heading TEXT, not level**, so no link moves — but confirm nothing
  links into these files with a `#fragment` anyway.

⚠️ **For a doc you APPENDED to, promote only the appended region** — the pre-existing
sections have their own level and must not move. Locate the region by asserting the
current file starts with the old content (`git show HEAD~1:<path>`), then count its lines.

### 6. Prove it lossless

```bash
python3 - <<'EOF'
import subprocess, pathlib, re
strip = lambda s: re.sub(r'\]\([^)]*\)', ']()', s)     # ignore re-based link targets
orig = strip(subprocess.run(["git","show","HEAD:CLAUDE.md"],capture_output=True,text=True).stdout)
hay  = strip(pathlib.Path("CLAUDE.md").read_text()
             + "".join(p.read_text() for p in pathlib.Path("docs").rglob("*.md"))
             + "".join(p.read_text() for p in pathlib.Path(".claude/rules").glob("*.md")))
miss = [l for l in orig.splitlines() if len(l.strip()) > 25 and l not in hay]
print("prose lines lost:", len(miss))
for m in miss[:20]: print("  ", m[:100])
EOF
```

**Expect 0.** Anything reported is either a line you deliberately reworded (check each one
by eye) or a real loss. Run this **before** committing, and again after any `sed` sweep.

### 7. Write the path-scoped rules

One rule per file or small group that carries real hazards. Keep each **under ~1 KB** —
the top 3–6 warnings plus a pointer to the full doc. It loads automatically when someone
opens that file, so it should read as "before you touch this, know these", not as
reference.

```markdown
---
paths:
  - "polyhost/device/hid_fw_up.py"
  - "polyhost/device/hid_fontpack.py"
---
# The flash transports

Full notes: `docs/hid-worker-refactor.md`.

- ⚠️ **Nothing the firmware prints during a flash is observable from the host** — the
  tell is a gap in the console timestamps spanning the flash.
- **`FW_UP_COMMIT` has FOUR status bytes.** Don't collapse `S` back into `!`.
```

Globs work (`tests/**/*_test.py`), but prefer explicit paths where the set is small — an
over-broad glob turns a rule into a second CLAUDE.md.

### 8. Commit per repo

State the before/after bytes, name the destination files, and say the losslessness check
passed and what the exceptions were. That commit message is what the next person reads
when they wonder whether something was dropped.

## Traps, all of which cost time on the 2026-09-14 pass

- ⚠️ **The link re-base is the only step that breaks things and reports nothing.** Seven
  links broke silently across two repos. Run step 4 for every repo, every time.
- ⚠️ **A block you append to an existing doc can end up linking to itself** (the moved
  text pointed at the file it now lives in). Harmless, but check it resolves.
- ⚠️ **Moving a "see below" reference orphans it.** After cutting, grep the remaining
  CLAUDE.md for `below`, `above`, and `see the ... section`.
- **Closed `[x]` work items are history, not instruction.** In one repo, 30 of 36 KB
  under "What still needs doing" was completed work. That belongs in a history doc; only
  the open `[ ]` items stay.
- **Don't move the small cross-binding sections just to hit a number.** A 1.2 KB note
  saying three repos must hold a byte-identical file is worth more per byte than anything
  else in the file.
