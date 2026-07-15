---
name: session-retro
description: Scan the current (or a past) Claude Code session for things worth keeping — (1) durable LEARNINGS (gotchas, root causes, environment/toolchain quirks, design decisions, hard-won exact commands) that belong in a CLAUDE.md / docs / memory but aren't recorded yet, and (2) repeatable multi-step WORKFLOWS done ad-hoc that should be codified as new skills. Produces a ranked proposal; on approval, writes the CLAUDE.md edits and scaffolds each new skill's SKILL.md. Use at the end of a meaty session, or when the user asks "what did we learn", "should this be a skill", "capture this", "turn X into a skill", "scan this session".
---

# Session retro — harvest learnings & new skills

A session often discovers things that took real iteration to find, and runs
workflows that will recur. Both are cheap to lose and valuable to keep. This
skill turns a session into three concrete outputs:

1. **Learnings** → appended to the right `CLAUDE.md` / `docs/*.md` (or saved as
   memory) so the next session starts knowing them.
2. **New skills** → scaffolded `SKILL.md`s for repeatable workflows that were
   done by hand this time.
3. **Docs gaps** → user-facing features added/changed this session that the
   public docs (the `polykybd-docs` site) don't yet describe. Run the
   **`update-polykybd-docs`** skill to close them (a separate PR in that repo).

The deliverable is a *ranked proposal you approve*, then the files. Don't write
anything until the user picks what to keep.

## 1. Get the session

You usually already have it in context — start there. For a long or summarized
session, read the full transcript so nothing early is missed:

```bash
# Claude Code stores one JSONL per session under the (root-owned) projects dir.
ls -t /root/.claude/projects/*/*.jsonl ~/.claude/projects/*/*.jsonl 2>/dev/null | head
```

The newest file under the munged-cwd directory is the current session. Each line
is a JSON event (`type` = user/assistant/tool_use/tool_result). Skim assistant
turns and tool calls; the *corrections, retries, and "aha" moments* are where the
learnings hide. If the user named a specific past session, match by date/uuid.

## 2. What counts as a LEARNING (record-worthy)

Keep a fact only if it is **non-obvious and would save the next session time**:

- A **root cause** that took debugging to find ("X clipped because font yAdvance
  is 44 vs 40, so descenders need voffset 5").
- An **environment / toolchain quirk** ("`lib/printf` header lives at
  `src/printf/printf.h`"; "system python lacks numpy — use the venv").
- A **"don't do X, do Y"** discovered the hard way (the `_patch_xlsx` vs
  openpyxl-save trap).
- A **decision + rationale** that will be questioned again ("staging moved 1→2 MB
  because the linker region is 2 MB and the boundary was unenforced").
- An **exact working command / invocation** that took iteration to get right.

**Not** learnings: routine steps, anything already in a `CLAUDE.md`/doc (grep
first), or one-true-fact-per-line trivia. When in doubt, ask "would I want this
*before* starting the same task again?"

For each learning: pick the **target file** (the most specific `CLAUDE.md` or doc
in the relevant repo/dir) and draft the exact text, matching that file's
existing voice and section structure.

## 3. What counts as a SKILL (codify-worthy)

Codify a workflow only if **all** hold:

- It is **multi-step** (a single command is a learning/alias, not a skill).
- It is **repeatable** — it'll be run again on different inputs, with stable
  steps. Strong signal: it was done **≥2× this session**, or it's an obvious
  recurring chore.
- It has a **clear trigger** (you can write the "use this when…" description).
- **No existing skill already covers it** (list them first — see below).

**Not** skills: genuine one-offs, exploratory debugging with no stable recipe, or
a thin wrapper around one command.

List what already exists so you don't duplicate:

```bash
for f in $(find . /root/.claude/skills ~/.claude/skills -name SKILL.md 2>/dev/null); do
  awk -F': ' '/^name:/{n=$2} /^description:/{print "  "n": "substr($2,1,80)}' "$f"; done
```

## 3b. What counts as a DOCS gap (document-worthy)

A **user-facing** change this session that would make the public docs wrong or
leave a new capability undocumented — a new/changed HID command or
`PROTOCOL_VERSION`, a new `polyctl` subcommand, a host setting or tray entry, a
keyboard feature (glyph script, idle style, brightness, font pack), or a new
language. These belong on the `polykybd-docs` **site**, not in a `CLAUDE.md`.

Don't flag internal refactors, bug fixes with no visible effect, or dev plumbing.
For each gap, name the change and the likely target page, then hand it to the
**`update-polykybd-docs`** skill (it owns the page map, the build/verify, and the
separate-PR flow). The retro's job is only to *notice* the gap and trigger that
skill — it does not itself edit the docs site.

## 4. Procedure

1. **Map the session** into its main threads/tasks (2–6 of them). For each, note
   what was *figured out* and what *workflow* was executed.
2. **Extract candidates** — learnings (§2), skills (§3), and docs gaps (§3b) —
   citing the evidence (what happened, ideally which step/message).
3. **Dedup**: grep the relevant `CLAUDE.md`/docs for each learning; list existing
   skills for each workflow. Drop anything already captured (or propose an *edit*
   to the existing item instead of a new one).
4. **Rank** by reuse value (how often × how much pain it saves).
5. **Present the proposal** (format below) and stop for approval. Recommend a
   default ("I'd keep #1 and #3").
6. **On approval, materialize:**
   - Learnings → `Edit` the target doc (or store memory if the user prefers),
     matching its style. Keep edits surgical.
   - Skills → for each, `mkdir -p <repo>/.claude/skills/<name>/` and write
     `SKILL.md` (YAML frontmatter `name` + a trigger-rich `description`, then a
     procedural body with concrete commands, an output format, and a
     **Pitfalls** section). Put reusable helper scripts beside it. Model the
     depth on `qmk_firmware/.claude/skills/firmware-size-diff/SKILL.md`.
   - Docs gaps → invoke the **`update-polykybd-docs`** skill for each (it edits
     the `polykybd-docs` site on its own branch + PR). This retro only surfaces
     them; that skill does the writing.
   - Offer to commit + push the new/edited files (don't unless asked, per repo
     rules).

## 5. Output format

```
LEARNINGS (record)
  1. <one line> → <target file> §<section>           [reuse: high]
     evidence: <what happened this session>
  2. ...

SKILL CANDIDATES (codify)
  A. <skill-name> — <when to use>                    [reuse: high]
     steps: <3–6 word outline>; done <N>× this session
     location: <repo>/.claude/skills/  (or ~/.claude/skills for cross-repo)
  B. ...

DOCS GAPS (→ update-polykybd-docs)
  i. <feature added/changed> → <likely polykybd-docs page>
  ii. ...

ALREADY COVERED (skipped): <item> → <existing doc/skill>

Recommendation: <which to keep, and why>
```

## 6. Where things go

- **Repo `.claude/skills/`** — committed + shared; use for project-specific
  workflows (this is where the PolyKybd dev skills live).
- **`~/.claude/skills/`** — user-level, available in every session regardless of
  repo; use for genuinely cross-project meta-skills.
- **CLAUDE.md** — the most *specific* one wins (a `lang/FUTURE_LANGUAGES.md`-style
  doc over the top-level CLAUDE.md when the learning is narrow).

## Pitfalls

- **Don't codify one-offs.** The bar for a skill is "will run again"; most session
  work is not a skill. Be conservative — a wrong skill is noise forever.
- **Don't duplicate.** Always list existing skills + grep docs first; prefer
  editing an existing item over adding a near-dup.
- **Keep each skill single-purpose** with a sharp `description` — the description
  is the only thing that decides whether it ever fires.
- **Cite evidence.** Every proposal should point at what in the session justifies
  it; if you can't, it's probably not worth keeping.
- **Approval before writing**, and **don't push** unless asked.
- This skill is repo-agnostic; if useful beyond this project, copy it to
  `~/.claude/skills/`.
