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
4. **Open-PR sweep** → every PR the session touched, checked for red CI and for
   review feedback that arrived while you were working (§3c).

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

## 3c. The open-PR sweep

Bot reviewers land minutes-to-hours after a push, so a PR opened early in the
session almost always has feedback nobody has read by the end of it — and a late
push may have gone red. Sweep every PR the session opened or pushed to:

```bash
# per repo touched this session — the PR for the current branch
gh pr view --json number,title,url,mergeable 2>/dev/null   # or the GitHub MCP tools
```

With the GitHub MCP tools, for each PR: `pull_request_read` with `get_check_runs`
(CI), `get_comments` (top-level, incl. bot summaries) and **`get_review_comments`**
(the inline threads — these are where real findings live and are easy to miss).

For each finding, in order:

1. **Verify it against the code before acting.** Confidently-wrong findings are
   common — see "Code review conventions" in the repo `CLAUDE.md` for the
   2026-08-01 tally (3 of 7 false, two refuted by their own cited evidence). Read
   the actual lines; do not take a severity label at face value.
2. **Fix** what is real, in a commit that says which finding it answers.
3. **Reply with the evidence** to what is not, so it is not re-raised. Skipping a
   valid-but-out-of-scope finding is fine — say *why* (pre-existing, different PR).
4. **Refresh the PR description** if the scope grew during the session. A body
   written at hour one routinely no longer describes what the branch does, and it
   is the first thing a human reviewer reads.

Report the outcome in the retro proposal as a table: PR → CI state → findings
fixed / refuted / skipped. The sweep REPORTS; the approval step (§4.5)
decides what gets pushed. That gate is separate from §4.6, which is about the
retro's OWN output.

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
   - Investigation index → `add_memory`, ONE entry per investigation that cost
     real effort: the question, the verdict in a sentence, and where the full
     write-up landed (`repo/file §heading`, or `owner/repo#N` — ⚠️ a bare `#N`
     resolves to the wrong PR across the nine repos).
     ⚠️ **A DEAD END has no write-up, and it is the case this index pays for
     best** — real effort spent, verdict *"nothing to change"*, so nothing was
     committed and there is no file to point at. Write the entry anyway: it IS
     the whole record, and the **where** field then names what was checked
     rather than where the answer lives (*"checked every `.github/workflows/`
     hit against upstream, 2026-08-29"*). `SECURITY_AUDIT.md` §
     *"Checked and NOT vulnerable — don't re-litigate"* is the same idea kept
     locally for one domain, and it exists because a dismissed finding that
     leaves no artifact gets re-raised in full by the next scan. A dead end is
     dated and closed by construction, so it clears the test below outright.
     ⚠️ **A finding may ride along ONLY if it is DATED AND CLOSED** — a
     measurement, the root cause of one incident, a verdict about a run that
     already happened. Those cannot go
     stale, because the fact is fixed in the past, and re-deriving one is exactly
     what this index exists to save. A **STANDING claim about how the code
     behaves now** (*"cmd 34's range is closed"*, *"the C1 icon band is full at
     32/32"*) goes in CLAUDE.md and mem0 gets only the pointer — that copy has an
     expiry date and no expiry mechanism. Two things make it worse here than in
     the mirror cases this project already tracks: **no `cmp` is possible**
     (`iso_lang_country.py`, `noto-fonts.yaml` and the five mirrored skills all
     drifted, and every one is recoverable because ONE command compares the
     copies — a cloud store against prose in git has no such command, so nothing
     will ever notice), and **the retrieval path IS the stale case** (CLAUDE.md
     directs the next session to search mem0 for *what CLAUDE.md no longer
     carries*, so the moment a claim is edited out, the mem0 copy stops being a
     duplicate and becomes the only answer — carrying the superseded version).
     The precedence rule bounds the damage to one verification round, but only
     while it is followed; this file already records a stale prose note that
     *"reads as authoritative and the next session copies it"*.
     ⚠️ **The test is the CLAIM's tense, not its depth.** "We measured X on
     date D" is closed however technical it is; "X behaves like Y" is standing
     however trivial. When in doubt, write the dated form — *"2026-09-04: run
     992's apply job failed with the link dead; cause not established"* is safe,
     *"the apply job kills the link"* is not.
     ⚠️ **Phrase the entry as the QUESTION, not as the conclusion.**
     `search_memories` is semantic, so it scores against what the next session
     *asks*, and an entry led by its verdict (*"the template injection is
     unreachable"*) matches *"is this scanner finding real?"* far worse than one
     that opens with the question. Lead with what was asked, then the verdict,
     then where it lives — the retrieval key and the write are the same string.
     ⚠️ **`search_memories` for the question BEFORE writing** and skip if it is
     already indexed: a retro re-run over an overlapping session would otherwise
     index it twice.
     ⚠️ **Write it raw — `add_memory(..., infer=False)`.** The default runs an
     LLM extractor that rewrites the entry into third-person narrative prose
     (*"User explained that …"*), losing the question / verdict / write-up
     structure this bullet prescribes. ⚠️ Measured 2026-09-09, the
     fully-qualified PR ref **did** survive that rewrite — so write raw for the
     structure, and do not repeat a claim that the extractor eats the refs.
     ⚠️ **Read `status` first, then `results` — and note which MODE each
     observation belongs to.** The two paths differ, and conflating them is how
     the first version of this note came out wrong. Measured 2026-09-09:
     `infer=False` returns `SUCCEEDED` **synchronously** with the stored text in
     `results`, and re-writing identical text is a **no-op that returns the
     EXISTING memory's id**, leaving its metadata and timestamps untouched. The
     **default** is asynchronous — `status: PENDING` plus an `event_id`, nothing
     stored yet — so there a written entry and a discarded one look identical
     until you poll `get_event_status`, and a near-duplicate resolves
     `SUCCEEDED` with `results: []` and nothing written. So `status` says
     whether the call finished and `results` says whether anything landed;
     neither answers alone, and the empty-`results` dedupe belongs to the
     default path, NOT to the raw write this bullet prescribes. Skip entirely if
     nothing this session took more than a handful of files to answer.
   - **Commit, push and OPEN A PULL REQUEST — every time, in every repo the
     retro touched.** The user's approval of the proposal IS the authorization:
     they asked for this standing behaviour explicitly (2026-09-10), so it
     overrides the default "do not create a pull request unless asked". Do not
     stop at a local commit, and do not ask a second time.
     - **One PR per repo.** A retro routinely writes into two or three
       `CLAUDE.md`s and maybe a skill; each repo gets its own branch, commit and
       PR against its own default — `PolyKybd` for the firmware, `master` for
       `Adafruit-GFX-Library` and `PolyKybd` (hardware), `main` for the rest.
     - ⚠️ **Cut the branch fresh from the updated default FIRST.** A retro runs
       at the end of a session, which is exactly when the branch you are
       standing on has just merged — and a push to a merged branch **succeeds
       silently and orphans the commit** (see Branching in the repo `CLAUDE.md`).
       `git fetch origin <default> && git checkout -B <branch> origin/<default>`,
       then confirm with `git merge-base --is-ancestor origin/<default> HEAD`.
     - **The PR body carries the EVIDENCE, not a summary** — for each note, what
       happened in the session that justifies it, so a reviewer can check the
       claim rather than the prose. That is the same evidence the proposal
       already cites, so it costs nothing to carry over.
     - ⚠️ **A mirrored skill is TWO PRs** (`qmk_firmware` ↔ `PolyKybdHost` keep
       five byte-identical copies — see Mirrored skills in either `CLAUDE.md`).
       Cross-link them in both bodies, or each reads as half a change; and
       `cmp` the pair before opening either, since a retro is also when the
       drift gets noticed.
     - **Report the PR links when done.** The retro is not finished until they
       exist.

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

MEMORY INDEX (→ mem0)
  •. <question investigated> → <verdict in one line>
     write-up: <repo/file §heading | owner/repo#N>

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
- **mem0** — an INDEX of investigations: one entry pointing at where the real
  write-up lives, so a later session can find it without re-deriving it. A
  **dated, closed** finding may ride along in the entry; a **standing claim about
  current behaviour** may not, because nothing can ever compare it against the
  repo (§4). ⚠️ CLAUDE.md wins on any disagreement — a mem0 hit is a lead to
  verify against the repo, never an authority.
- **The gap this closes**: a finding can be too narrow to earn permanent space in
  CLAUDE.md (every session pays for that file) and still expensive to re-derive.
  Dated findings are what mem0 is *for*; before this they had nowhere to live but
  a PR nobody would find.

## Pitfalls

- **Don't codify one-offs.** The bar for a skill is "will run again"; most session
  work is not a skill. Be conservative — a wrong skill is noise forever.
- **Don't duplicate.** Always list existing skills + grep docs first; prefer
  editing an existing item over adding a near-dup.
- **Keep each skill single-purpose** with a sharp `description` — the description
  is the only thing that decides whether it ever fires.
- **Cite evidence.** Every proposal should point at what in the session justifies
  it; if you can't, it's probably not worth keeping.
- **Nothing sensitive in mem0, and that includes the SEARCH.** No credentials,
  keys, file contents, or anything you would not put in a public issue. ⚠️ The
  pre-write `search_memories` sends the question text to the same cloud service
  `add_memory` writes to — so a question that cannot leave the machine means
  skipping the index entry altogether, not sanitising it afterwards.
- **Approval before writing — but approval is the ONLY gate.** Once the user
  picks what to keep, write it, push it and open the PR (§4.6) without asking
  again. Asking twice is what this instruction exists to stop.
- This skill is repo-agnostic; if useful beyond this project, copy it to
  `~/.claude/skills/`.
