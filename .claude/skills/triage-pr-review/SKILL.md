---
name: triage-pr-review
description: Triage the review feedback on a PolyKybd pull request and drive it to a resolution — fetch the comments and reviews, work out which of the five reviewers actually reviewed and whether any of them read the CURRENT head (several post long comments without reviewing; a completed review can be pinned to a pre-push commit), verify every finding against the real code before acting, fix the true ones, decline the false ones with evidence, and post a single reply. Use when asked to "check the PR for feedback / comments / reviews", "what did CodeRabbit / Sourcery / Greptile / Qodo / CodeQL say", "address the review", "was this PR actually reviewed", or after opening a PR and waiting for the bots. NOT for CI failures (that's diagnose-hil-failure for HIL, or the build job's own log).
---

# Triage PR review feedback

**Five** reviewers can post on a PolyKybd PR — four LLM bots (**CodeRabbit**,
**Sourcery**, **Qodo**, **Greptile**) and one static analyser (**CodeQL**, on the
host repo; **cppcheck** on the firmware) — and their output is *not* uniformly
trustworthy or even uniformly a review. This skill is the loop: find out what
actually ran, verify each finding against the code, act, and reply once.

⚠️ **This skill said "three bots" long after there were five.** Greptile was
installed later, and CodeQL/cppcheck were never counted at all — so the roster
below is the part most likely to be stale again. Check `CLAUDE.md`'s code-review
conventions before trusting it, and update both when a reviewer is added or dies.

The standing repo rule is **verify, not dismiss**: on one PR, 3 of 7 CodeRabbit
findings were false and two were refuted by their own evidence — but the same round
produced a genuinely valuable one. Both halves of that matter.

## 1. Fetch what's there

```
mcp__github__pull_request_read  method=get_comments  owner=thpoll83 repo=<repo> pullNumber=<n>
mcp__github__pull_request_read  method=get_reviews   owner=thpoll83 repo=<repo> pullNumber=<n>
```

⚠️ **`get_comments` routinely exceeds the tool's token limit** (51k characters on a
medium PR — the bots' walkthroughs are enormous). It then saves to a file and tells
you the path. Don't read that file whole; parse it:

```bash
python3 - <<'EOF'
import json, re, html
items = json.loads(open("<saved path>").read())
for c in items:
    u = (c.get("user") or {}).get("login"); b = c.get("body") or ""
    print("---", u, c.get("created_at"), len(b), "chars")
    print(b[:400].replace("\n", " "))
EOF
```

To read one bot's finding in full, strip the HTML (`re.sub(r"<[^>]+>", "\n", body)`
+ `html.unescape`) — Qodo in particular wraps everything in `<details>`/`<dl>`.

## 2. Establish which bots ACTUALLY reviewed

A long, confident comment with a walkthrough and a file table is not a review.
Check each:

| Bot | Reviewed | Didn't review — tells |
|---|---|---|
| **CodeRabbit** | body contains **`Actionable comments posted: N`** | `> [!WARNING] Review limit reached … next review in N minutes`; a "Reviews paused … under active development" note (auto-pause) |
| **Sourcery** | a review with per-comment findings ("Hey - I've found N issues") | its **review object's body is the rate-limit notice** (`you have reached your weekly rate limit of 500000 diff characters`) while its *Reviewer's Guide comment still renders in full* and looks like a review |
| **Qodo** | a comment headed **`Code Review by Qodo`** with a bug count | only `PR Summary by Qodo` — that is a description, never a review |
| **Greptile** | a review object with findings | **announces a skip NOWHERE** — silence is indistinguishable from "no findings". Its `Greptile Review` **check run is anti-correlated with reality** (measured: a green one over no review at all), so it proves nothing. Account-wide refusals arrive as a review whose body is *"reached the 50-credit limit for trial accounts"* |
| **CodeQL** (host) | inline comments from **`github-advanced-security[bot]`** + a review object | judge the **`Analyze Python` job**, not the `CodeQL` check run. Absent on a PR that changed no analysed code |

The findings live in **`get_reviews`**, not only in `get_comments` — CodeRabbit's
actionable list is the review body, and Sourcery's rate-limit notice arrives as a
review too. Always pull both.

### 2b. A review only counts for the commit it READ — check the sha

The standing check is **two conditions together**, and each alone points the wrong
way:

1. the review's **`commit_id` equals the PR's head sha**, AND
2. its **body is not a refusal notice**.

Neither is sufficient. A **refusal is a review object carrying the head sha**
(Sourcery's budget notice, Greptile's credit limit), so a `commit_id`-only rule
marks the PR reviewed by a bot that read nothing. And a **clean CodeRabbit pass
creates no review object at all** — it reports *"No actionable comments were
generated 🎉"* by editing its summary comment — so a body-only rule reads a clean
pass as never reviewed. Run both, and when `get_reviews` is empty read the summary
comment's own text (`📥 Commits` range; *skipped* vs *no actionable comments*).

⚠️ **A completed review can still be pinned to a PRE-PUSH head, which is quieter
than the documented abort.** Measured on host#218 (2026-09-07): a push landed
inside a running review; the review finished normally — full walkthrough, three
findings, pre-merge checks — but its range read `c02a1fa..be78692` throughout and
its `commit_id` was the pre-push commit, so the pushed commit was never read. The
PR then carries a real review that does not cover its head. Two consequences:

- **Hold pushes while a review is in flight.** On an **under-10-stars repo**
  (`PolyKybdHost`, `polykybd-docs`) this is not a free mistake: auto-review is off
  there, so a push re-triggers **nothing** (observed — the summary re-renders as the
  under-10-stars skip), and re-reading the new commit spends the hour's only slot.
- **Say so in the PR body** when the fixes end up unreviewed. A green board plus a
  real-looking review is exactly what this skill exists to see through.

If nothing actually reviewed and it matters, comment `@coderabbitai review` (and
`@coderabbitai full review` after an aborted run) rather than merging on the
appearance of review.

⚠️ **Spend that request once.** A refused request costs a slot too, and the stated
wait *grows* with each one — host #170 answered "next review in 19 minutes", then
**52** after the retry that wait invited. So:

- **Never chase the clock.** "Wait N minutes, ask again" is the one loop that
  reliably starves the PR; each retry buys a longer wait than it cost.
- **On an under-10-stars repo** (`PolyKybdHost`, `polykybd-docs` — no auto-review,
  rendered as a *"Review available on request"* box) the **checkbox and the chat
  command are the same quota**. Ticking the box *and* commenting is two slots for
  one review.
- **A push re-triggers a review for free.** If more commits are coming anyway,
  push first and ask afterwards — ask on the commit you intend to merge.

## 2b. When nothing reviewed the PR at all

Sometimes the honest answer is that **no reviewer read this PR**. All three bots
can be unavailable at once, each in its own disguise (§2), and there is no
fallback: an on-demand Claude reviewer was tried across all three repos and
removed on 2026-08-20 — it published one review in its life and burned ~$4 of
subscription posting nothing the rest of the time, with the deciding detail
(which tool it was denied) unreadable from the log.

So when §2 says nothing reviewed it:

- **Say so on the PR**, rather than letting a page of bot output imply otherwise.
- **Spend the one CodeRabbit slot deliberately** — on the commit you intend to
  merge, per the rate-limit note above.
- **Lean on what actually catches things here**: the HIL rig, cppcheck (the only
  non-LLM reviewer, and the one with no quota), and the unit suites. That
  asymmetry is the point — the bots are a convenience, those are the coverage.

## 3. Verify every finding against the code — before touching anything

For each finding, reproduce or refute it. Cheapest first:

- **A crash/exception claim** → run the expression in isolation. A five-line
  `python3 -c` beats reading the code twice:
  ```bash
  .venv/bin/python -c '"{a} {stale}".format(stale="x", **{"stale": True, "a": 1})'
  # TypeError: got multiple values for keyword argument 'stale'  -> the finding is real
  ```
- **A contract/logic claim** → read the exact lines it cites and trace the caller.
  Ask what input reaches the branch (e.g. "`--dev 0` makes `verbosity` falsey, so
  the flag is dropped, so the daemon re-reads the setting" — real).
- **A "should use a worker / is blocking" claim** → check what the call actually
  does. A local socket round-trip to a cached comparison is not the network I/O the
  rule is about; the codebase's own docstrings often already state the design.
- **A "pre-existing" finding is still a finding** — decide on merit, not on blame.
  (The Developer submenu being unreachable while disconnected predated the PR and
  was still worth fixing in it.)

**If a finding is real and no test caught it, add the test in the same change** —
and confirm the test *fails against the old code* rather than assuming it would.
The one crash this loop caught existed precisely because nothing rendered that
string in a test.

## 4. Fix, decline, reply once

- Fix the true ones. Keep each fix minimal and commented where the reason is
  non-obvious.
- Decline the false/inapplicable ones **with the evidence**, so they aren't
  re-raised on the next push.
- Skip nitpicks that add coupling (e.g. caching a subprocess result across tests) —
  say so briefly rather than silently ignoring them.
- Post **one** comment, not one per finding. Table of findings → verdict, then a
  short section for what you declined and why. End with the attribution footer:

```
---
_Generated by [Claude Code](https://claude.ai/code)_
```

Suggested shape:

```markdown
Review addressed in <sha>. Each finding was reproduced against the code before acting on it.

| Finding | Verdict |
|---|---|
| <finding> (bot) | **Real.** <what actually happens> |
| <finding> (bot) | **Fair.** <the narrower true version> |

**Not taken**
> <quoted finding>
<why, in terms of what the code does — plus any residue you are choosing to leave>
```

## Pitfalls

- **Don't trust the walkthrough.** Every bot renders one; only some of them
  reviewed. Check §2 before reporting a PR as "reviewed, no findings".
- **Don't dump `get_comments` into context** — it will blow the token limit and
  cost you the turn. Parse the saved file.
- **Don't batch-accept.** The autofix checkbox ("Push a commit to this branch")
  applies findings unverified; that is the opposite of this skill.
- **Don't batch-reject either.** The two most valuable findings this loop has
  produced were a hard `TypeError` on a menu path and a security-adjacent flag
  being dropped — both from bots that were wrong elsewhere in the same round.
- **Re-run the affected suites after fixing** (`.venv/bin/python -m unittest
  discover -s tests/<pkg> -t .`), and the xvfb GUI ones if the GUI changed. Clear
  `__pycache__` if a fix appears not to take (see CLAUDE.md § Test discovery).
- **A verdict table is not a merge.** Note anything still unverified on hardware
  in the reply, rather than implying the PR is fully validated.

---

# Reviewer field guide (moved out of CLAUDE.md, 2026-09-10)

Everything below was in the `Code review conventions` section of
`qmk_firmware/CLAUDE.md` and `PolyKybdHost/CLAUDE.md`, ~51 KB that every session
paid for whether or not it was looking at a PR. It is **forensics** — how each bot
goes quiet, and which of its signals lie — so it belongs where triage happens.

The two blocks are kept as they were written rather than merged: they overlap in
subject and differ in measured detail, and merging prose is how a measurement gets
dropped. Read the one for the repo you are in first; check the other when a signal
does not match anything here.

⚠️ **Dates and counts are observations, not the current state.** Quotas, plans and
trial limits move. Every claim here is worth re-checking with `get_reviews` before
you act on it — that is the standing check the CLAUDE.md rules still carry.

## From `qmk_firmware/CLAUDE.md`

- **On a rapidly-iterating PR, keep CodeRabbit OFF and ask for ONE review at the
  end.** A design/layout PR that lands many small pushes (a preview render per
  tweak) makes CodeRabbit re-review from scratch on every one. Two costs, both hit
  in a single session (2026-07-29, PR #159): it burns the **per-developer review
  rate limit** — five pushes came back `Review limit reached … next review in
  31/41/46 minutes` and were never reviewed at all — and each landed review is
  against a head you have already moved past. So the reviews you *do* spend are
  the least useful ones.
  - CodeRabbit notices this itself and **auto-pauses** the branch ("this branch is
    under active development"), governed by
    `reviews.auto_review.auto_pause_after_reviewed_commits`. ⚠️ Its paused comment
    still renders a walkthrough + pre-merge checks, so it **reads like a completed
    clean review** — check for the "Reviews paused" note before concluding the PR
    was reviewed.
  - Workflow: let it pause (or pause it deliberately), iterate freely, then
    comment **`@coderabbitai review`** on the final commit for a single full-diff
    review; **`@coderabbitai resume`** turns automatic reviews back on. Both
    commands are listed in the paused comment itself.
  - ⚠️ **A push while a review is in flight ABORTS it** — "Review failed: The head
    commit changed during the review from `<a>` to `<b>`". The run is lost, not
    resumed, and re-triggering costs another slot against the rate limit. So once
    a review starts, **hold pushes until it reports** (2026-08, cost a full cycle).
    - ⚠️ **A lost run leaves NO REVIEW OBJECT AND NO VERDICT, and the only
      trace it does leave is a summary comment collapsed to a bare "Review
      Change Stack" link — which reads as "nothing to say".**
      Measured on #275 (2026-09-04, a docs PR touched five times in ten
      minutes): **four** runs each rendered the `> [!NOTE] Currently processing
      new changes…` block with a `📥 Commits` range, then had the block
      **removed** on a later edit leaving only the stack link — no walkthrough,
      no `📥 Commits`, no *"No actionable comments"*, and no error. No review
      object was created either, so `get_reviews` is empty — **indistinguishable
      from the CLEAN-pass false negative recorded below, where empty is also the
      answer.** The tell is the summary comment itself: a bare stack link is not
      a clean pass; *"No actionable comments were generated 🎉"*, or a
      walkthrough, is what a completed run leaves.
      - ✅ **The fifth run, left UNDISTURBED, completed** — full walkthrough plus
        `🚥 Pre-merge checks ✅ 5`, describing the real head. So the collapse is
        caused by the PR moving under a run, not by a quirk of rendering: three
        of the four dead runs started scoped to a head that a push had **already
        superseded** (`..39f693fa`, `..0f7ecef8`, `..73918d0` while head was
        `ad122c45`). That is the abort documented above, and this is what its
        aftermath looks like — the run is not merely "lost", it erases its own
        evidence.
      - ⚠️ **So the cost of ignoring "hold pushes until it reports" is not one
        wasted review, it is a PR that LOOKS unreviewed and cannot tell you
        why.** Four cycles were burned here by pushing and editing the body
        while runs were in flight; one quiet minute produced the review.
  - ⚠️ **Order matters: `resume` BEFORE `review` makes the review a no-op.**
    CodeRabbit is incremental and "does not re-review already reviewed commits";
    that guard is only relaxed *while reviews are paused*. Resuming first
    un-pauses, so the following `review` finds nothing to do and silently reviews
    nothing. Either `review` first and `resume` after, or use **`@coderabbitai
    full review`**, which re-reviews the whole diff regardless of state — that is
    also the command to reach for after an aborted run, since the failed run
    recorded nothing but the head has already moved.
  - ⚠️ **CodeRabbit SKIPS any PR over 100 changed files, so an upstream-merge PR
    gets NO review at all** — *"Review skipped — Too many files! This PR contains
    N files, which is M over the limit of 100."* This is a second, different tell
    from the rate-limit one above, with the same consequence and the difference
    that it is **guaranteed** on a catch-up merge rather than occasional. The
    0.33.13 merge (#197) was skipped on all four pushes (401 → 425 files), each
    time rendering as an ordinary status comment with a file table, so the PR read
    as reviewed. There is no way to get it reviewed short of splitting the PR — so
    for a merge PR, treat the **build + HIL checks and hardware testing as the only
    real verification**, and don't count the green board as review cover.
  - ⚠️ **A STACKED PR gets no automatic review at all** — *"Review skipped — Auto
    reviews are disabled on base/target branches other than the default branch."*
    This is a **fourth** no-review mode (alongside the rate limit, the <10-stars
    repo, and the >100-file skip) and it is **guaranteed, not occasional**: any PR
    whose base is another feature branch is silently unreviewed for as long as it is
    stacked. Seen on #211 (2026-08-17), stacked on #210. Two ways out, and prefer the
    first: **let the parent merge** — GitHub then retargets the child to `PolyKybd`
    and auto-review applies again (confirm a review actually lands; a base change may
    not itself trigger one). Otherwise spend a slot on `@coderabbitai review`, which
    works on a stacked PR but costs the same org-wide budget as any other request.
    ⚠️ Do **not** read the resulting quiet board as "no findings" — nothing read it.
  - ⚠️ **Sourcery's rate-limit is QUIETER than CodeRabbit's: the `Sourcery review`
    check run goes GREEN (`success`) while no review happened.** When its weekly
    diff-character budget is spent it submits a `COMMENTED` review whose entire body
    is *"you have reached your weekly rate limit of 500000 diff characters"* — and
    that still counts as a completed check. So the PR shows a green Sourcery tick
    with **zero findings**, which reads exactly like a clean review. CodeRabbit at
    least renders a `> [!WARNING] Review limit reached` banner. Both were
    simultaneously unavailable on #203 (2026-08-12), leaving a fully green board
    that **no reviewer had read**. To tell them apart, read the review *body* via
    `pull_request_read` `get_reviews` — do not infer from the check conclusion.
    (The sibling rule "a bot comment is not a review" is in `PolyKybdHost/CLAUDE.md`;
    this is the same failure with a green check instead of a long comment.)
    - ⚠️ **A THIRD shape, and the quietest yet: `Sourcery review` = `success`
      with NO REVIEW OBJECT AT ALL.** On #218 (2026-08-19) the check was green on
      the head commit while `get_reviews` returned exactly one review, submitted
      against the *first* commit of the branch — so the firmware fix and the two
      new CI workflows had been read by nothing. There is no rate-limit body to
      find here, because there is no review. **Check the review's `commit_id`
      against the head sha**, not just that a review exists: a stale review plus a
      fresh green check is indistinguishable from a current one at a glance.
      - ✅ **Sourcery auto-reviews a PR FIVE times and then WITHDRAWS its
        approval, which is the one reviewer behaviour here that self-corrects
        rather than going stale.** Measured on #275 (2026-09-04): on the sixth
        push it commented *"Sourcery has withdrawn its approval of this pull
        request. It auto-reviews a pull request 5 times, and this push is past
        that limit, so the approval no longer reflects code Sourcery has read"*,
        and the `APPROVED` review — pinned to the branch's FIRST commit while its
        check went green on every head since — stopped counting. Two things
        follow:
        - ⚠️ **A withdrawn approval is NOT a rejection**, and it arrives with no
          findings attached, so it reads like one. It means only that the head
          has outrun what Sourcery read. `@sourcery-ai review` gets a fresh one.
        - **It bounds the stale-approval trap above rather than removing it**:
          within the first five pushes an approval can still sit on a commit the
          head has left behind, and that is exactly the window most PRs live in.
          The `commit_id`-vs-head check is still the thing to run.
    - ⚠️ **CodeRabbit's COMMIT STATUS does the same thing, so "at least it renders a
      banner" only holds for the comment.** Its status context reads `state: success`
      with the description **"Review rate limited"** (`pull_request_read`
      `get_status`, head `2d61653d`, 2026-08-18) — so a head that nothing read shows
      a green CodeRabbit tick alongside the green build. The banner lives in the
      *comment*, which a status-only view never shows, and which vanishes on a
      re-render anyway (see the sticky-walkthrough note above). **`get_status` can
      only ever tell you a review RAN, never that it read the current head** — pair
      it with `get_reviews` and compare each review's `commit_id` against the head.

- ⚠️ **An on-demand Claude reviewer (`@claude review`) was tried and REMOVED
  (2026-08-20) — don't rebuild it.** `.github/workflows/claude-review.yml` +
  `claude-mention.yml` existed in all three PolyKybd repos to cover exactly the
  cases above — CodeRabbit rate-limited, Sourcery's green-check-but-empty weekly
  limit, and the >100-file upstream-merge skip. It published one usable review in
  its life and otherwise posted nothing while still billing the subscription
  (~$4 total for that one review); the deciding detail — which tool the runner
  denied it — is unreadable, because the action logs *"full output hidden for
  security"* and uploads no artifact. Workflows and the `CLAUDE_CODE_OAUTH_TOKEN`
  secret are gone from all three repos.
  - **So on an upstream-merge PR there is genuinely no LLM reviewer.** CodeRabbit
    skips it outright at >100 files, and **Sourcery has its own ceiling that lands
    on the same PRs** — it refuses any diff over **20,000 lines** outright (*"the
    GitHub API does not allow us to fetch diffs exceeding 20000 lines"*, a hard
    limit, not a quota, so waiting does nothing). Treat the
    **build + HIL checks and hardware testing as the only verification**, say so
    on the PR, and don't read the green board as review cover.
    - ⚠️ **A fourth bot, Greptile, now exists — see the Greptile entry in
      `PolyKybdHost/CLAUDE.md` before repeating "no LLM reviewer" as a fact.**
      Whether it reviews a catch-up merge is **untested**; what is measured is
      that it reviews only some PRs and announces a skip nowhere, so its silence
      is not evidence either way. ⚠️ **And its `Greptile Review` check run is not
      the answer** — measured, a green `success` one accompanied a PR it did not
      review, the same trap recorded above for Sourcery. Check `pull_request_read`
      `get_reviews` on the PR in front of you and require **both** that a review's
      `commit_id` equals the head sha **and** that its body is not a refusal
      notice — a Sourcery refusal is itself a review object carrying the head sha,
      so the sha alone reads as reviewed. Never infer from a check run or from
      this paragraph.
      - ⚠️ **As of 2026-09-04 Greptile is REFUSING ACCOUNT-WIDE, and that is a
        different thing from its documented silence.** It now submits a review
        whose entire body is *"`thpoll83` has reached the 50-credit limit for
        trial accounts"* — measured on #275, one review object, `commit_id`
        equal to the head sha. So the pair-check above catches it (the body is a
        refusal), and the "announces a skip nowhere" clause still holds for an
        ordinary skip: this is a **quota**, announced, not a skip. Two
        consequences while it lasts: it is **not review cover on any PolyKybd
        repo**,
        since the limit is on the account rather than the repo; and unlike
        CodeRabbit's hourly window it does **not** come back by waiting — the
        trial is spent until someone upgrades. Re-check with `get_reviews`
        rather than assuming either state persists.
      - ⚠️ **That pair-check has a FALSE NEGATIVE in the other direction, so it is
        not sufficient either: a CLEAN CodeRabbit review produces NO REVIEW OBJECT
        AT ALL.** It says *"No actionable comments were generated 🎉"* by editing
        its existing summary comment, so `get_reviews` is empty for that head and
        the check reads a clean pass as unreviewed (measured on qmk#268,
        2026-09-03). A refusal is an object that means nothing; a clean pass is no
        object that means everything. Read the summary comment's BODY — its
        `📥 Commits` range and whether it says *skipped* / *no actionable comments*
        — alongside `get_reviews`. Full write-up in `PolyKybdHost/CLAUDE.md`.
      - ⚠️ **A commit whose only reviewable file is GENERATED is skipped outright,
        so a per-language DATA pass is structurally unreviewable.** *"Review skipped
        as selected files did not have any reviewable changes"* — twice on qmk#268,
        where the diff was the cog-generated `lang_lut.c` plus `lang_lut.xlsx`, and
        the workbook is excluded by CodeRabbit's `!**/*.xlsx` path filter. No banner,
        no quota, nothing wrong: just no review. On such a change the verification
        has to be your own — assert the generated diff is confined to what you meant,
        and measure the rendered result.
  - **cppcheck has no quota, no star threshold and no file-count limit** — and
    is not an LLM, so it doesn't share the others' blind spots. That is why it
    was added, and it matters more now that it is the only automated reviewer
    left. ⚠️ **But it is NOT unconditional, and the exception lands exactly on
    the case above**: `cppcheck.yml` filters `pull_request` on
    `keyboards/polykybd/**`, `modules/polykybd/**` and the workflow itself, so a
    catch-up merge that touches only upstream paths gets **no cppcheck run at
    all** — the check is absent, not green. Don't "fix" that by broadening the
    trigger: analysing the whole upstream tree is the CodeQL trap this scope was
    chosen to avoid. It means an upstream merge really is verified by the build,
    the HIL rig and hardware alone.

## From `PolyKybdHost/CLAUDE.md`

- **A bot comment is not a review — check whether one actually ran before treating
  a PR as reviewed.** PR #127 (2026-08-01) collected four bot comments and **none
  was a review**: CodeRabbit was rate-limited across both pushes (a `> [!WARNING]
  Review limit reached … next review in 43 minutes` comment, re-queued and still
  unavailable at merge time), Sourcery had hit its weekly diff-character limit so
  it posted only its descriptive *Reviewer's Guide*, and Qodo only ever posts a
  *PR Summary* (and, since its subscription lapsed, only a *"reviews are paused
  because the subscription is no longer active"* notice — docs#48, 2026-08-17).
  All three render as long, confident-looking comments with
  walkthroughs and file tables, so the PR read as well-reviewed and merged with
  zero findings raised against it. Tells: CodeRabbit's rate-limit notice (and the
  "Reviews paused" one — see the same section in `qmk_firmware/CLAUDE.md`), and
  the absence of any *Actionable comments posted: N* line. When it matters, wait
  for the window or comment `@coderabbitai review`.
  - ⚠️ **Both of those tells failed on #156 (2026-08-11) — the only reliable check
    is whether the walkthrough describes the commit you are looking at.** Compare
    its file list against the head commit's: the walkthrough named 2 files while
    the head changed 4, and described the superseded change. Three ways it misleads:
    - **The rate-limit banner DISAPPEARS on a re-render.** Editing the PR body
      re-rendered the pre-merge-check block, and the `> [!WARNING] Review limit
      reached` notice vanished with it — leaving a clean-looking walkthrough plus
      "🚥 Pre-merge checks ✅ 5" over a commit nothing had read. A missing banner is
      not evidence a review ran.
    - **`@coderabbitai review` can answer WITHOUT a formal review.** The reply ran
      a real analysis chain (diffed the 4 files, read the callers, scripted its own
      cross-check) and returned *"I reviewed commit f14ae25 … no correctness,
      regression, or documentation issues"* — a genuine, verifiable read. But the
      same comment was then **edited in place** from *"Action performed: Review
      triggered"* to *"⚠️ Action not completed — Review rate limited"*, and the
      walkthrough stayed stale for good (an incremental system will not re-review
      a commit it has "seen"). So a substantive verdict and a missing formal review
      can coexist. Take the verdict, but don't expect the PR summary to describe
      the change — put the real description in the **PR body**, which you control.
    - **The FILE LIST is not enough on a single-file PR — compare what the
      walkthrough SAYS.** On docs#42 (same day) a follow-up commit touched the
      same one file the reviewed commit did, so the file-list check above passes
      while the review is still describing the superseded diff; only the absent
      mention of the new section gave it away. The mechanism: the walkthrough is
      **sticky** — it survives from the last *successful* review and is not
      refreshed by a rate-limited one, which replaces only the review-info block
      beneath it. Note the banner behaved *correctly* here (present, with the
      real `<old>..<new>` range), so the asymmetry is: **a banner that is present
      is trustworthy; a banner that is absent proves nothing.**
    - ✅ **There IS a cheap reliable tell after all: the Merge Risk block names the
      commit it covers** — *"Merge Risk: 🟡 Moderate · up to `351b8`"*.
      Read that sha instead of comparing walkthrough prose against the head
      commit. On docs#48 (2026-08-17) the summary re-rendered on **three**
      successive pushes, each time still scoped `up to 351b8` and still asserting
      the PR was "not merge-ready" over a concern two later commits had already
      fixed — while CodeRabbit's own chat reply confirmed the fix. Same sticky
      mechanism as above, but self-labelling: if that sha is not the head,
      something in the summary is stale. It is worth checking before believing
      any *later* re-render of a summary, including a scary one.
      - ⚠️ **But it is a POSITIVE tell only — a lagging Merge Risk sha does NOT
        prove the whole summary is old, and this line used to say it did.**
        On qmk#250 (2026-08-29) the sha read `up to b6c1b` while the **📥 Commits**
        line in the *same comment* correctly said *"between `b6c1b7f` and
        `386d74f`"* — i.e. the review genuinely covered the newer head and only
        the risk block lagged. So **read the range line, and settle it against the
        PR's actual head sha from the API.** Full case in the CodeRabbit sub-note
        under Sourcery's `✅ Addressed in <sha>` entry below.
        - ⚠️ **This used to say the reviewed-range line "was accurate every time".
          It is NOT — measured 2026-09-03 on qmk#268.** The range line advanced to
          `68e30d18..d03a1107` inside a summary re-render whose own body said
          *"Review skipped"*, i.e. it named a head range that no review had read.
          So the range line reports **what the render was scoped to**, not what was
          reviewed, and neither it nor the Merge Risk sha stands alone. The only
          thing either supports is *which* commit is being discussed; whether a
          review RAN is answered by `get_reviews` plus the body text, and by
          nothing else in the summary comment.
      - ⚠️ **And a MATCHING sha does not make the prose beside it current either —
        it dates the COVERAGE, not the text.** On qmk#259 (2026-09-01) the block
        read `up to 7dd4c`, the actual head, on a review that had genuinely just
        read that commit — while its risk prose re-raised two findings settled the
        round before, both of which **CodeRabbit had itself withdrawn and recorded
        repo learnings about** (a `config.h DESCRIPTION`/`config_common.h` request
        it agreed was wrong after its own `fd`/`rg` sweep, and a persist-the-
        migrated-idle-style ask it accepted as deliberate design). So the sha is
        only ever evidence about *which commit was read*; whether the summary text
        reflects the thread history is a separate question it cannot answer. The
        practical rule: **the Merge Risk prose is not a merge gate** — check it
        against the resolved threads before letting it hold up a PR, and expect it
        to re-litigate anything you settled by argument rather than by a code change.
    - ⚠️ **The limit is per-developer across the ORG, so pushes to a trivial PR
      starve the one that needs review.** Docs pushes on #42 consumed the window
      #159 was waiting for. When two PRs are open and one is real code, stop
      pushing cosmetic commits to the other until the important one is reviewed.
    - ⚠️ **A THIRD no-review mode: CodeRabbit does not auto-review a repo with
      fewer than 10 stars at all** — *"Reviews should be triggered manually for
      repositories with fewer than 10 stars"*, rendered as a *"Review available on
      request"* box with a Trigger-review checkbox. This is permanent, not a
      transient limit, so it is easy to mistake for "no findings". It applies to
      **`polykybd-docs`** (docs#48, 2026-08-17) **and to `PolyKybdHost`** (#172,
      2026-08-18) — i.e. do not assume the host repo auto-reviews; the fix is
      simply to comment `@coderabbitai review`. The same reply then states the quota
      outright — *"Your plan includes up to 1 review per rolling hour; 0 remain
      after this review"* — which is the number to plan around when a PR needs a
      re-review after a fix.
      - ⚠️ **On such a repo there is NO free retry, because the "a push
        re-triggers a review without spending a request" escape hatch recorded
        below relies on AUTO-review — which is exactly what being under 10 stars
        turns off.** So the two notes interact badly and the interaction is not
        obvious from either one: on `qmk_firmware` a push is the cheap way to get
        another look, and on `PolyKybdHost` / `polykybd-docs` a push buys nothing
        at all. Every review here costs a quota slot, so spend it on the commit
        you actually want read. **Observed, not inferred** (host#218,
        2026-09-07): two pushes after the review each re-rendered the summary as
        *"This repository does not receive automatic reviews because it has fewer
        than 10 stars"* with a Trigger-review checkbox — a SKIP, not a review.
      - ⚠️ **A push mid-run did NOT abort the review (host#218, 2026-09-07) — but
        the review stayed pinned to the PRE-PUSH head, which is the outcome that
        matters and is quieter than an abort.** `qmk_firmware/CLAUDE.md` records
        that a push while a review is in flight aborts it, erasing its own
        evidence. Measured here: the run started 10:16:08, a push landed 10:18:57,
        and the review completed normally at 10:25:04 — full walkthrough, 3
        findings, pre-merge checks. What it did **not** do is notice the new
        commit: its `📥 Commits` range read `c02a1fa..be78692` throughout and the
        review object carries `commit_id: be78692`, so the pushed commit was
        never read. Under this file's own standing check — a review counts only
        when its `commit_id` equals the PR head — **that review is not cover for
        the head it appears under**, and on an under-10-stars repo re-reading the
        new commit costs the hour's only slot. One observation is not a base rate,
        so do not read this as "the abort does not happen"; read it as **the
        failure can be silent non-coverage rather than a visible abort**, which
        the range line and `commit_id` will tell you and nothing else will.
    - ⚠️ **All THREE bots can be unavailable at once, each in its own disguise —
      #172 (2026-08-18) collected a full page of bot output and not one review.**
      CodeRabbit posted the under-10-stars "Review available on request" box;
      Sourcery submitted a review whose entire body was its weekly rate-limit
      notice **and a `Sourcery review` check run with conclusion `skipped`**
      (green-adjacent, not red); Qodo posted its subscription-lapsed notice. On top
      of that Sourcery still rendered a full *Reviewer's Guide* with two mermaid
      sequence diagrams and a file-level change table — pure description, zero
      findings — which is the most convincing-looking artifact of the three. Read
      the check-run *conclusion* and the review *body*, never the presence of
      output.
      - ⚠️ **An on-demand Claude reviewer was tried as the remedy for all
        three and REMOVED (2026-08-20) — don't rebuild it.** `claude-review.yml`
        + `claude-mention.yml` ran the `code-review` plugin off a `@claude
        review` comment, in all three repos. It published exactly **one** usable
        review in its life (host #185) and otherwise didn't: on #181 it spent 10
        turns and $2.76 and posted nothing, and three summonses on `polykybd-docs`
        each went green having published nothing, with a
        `permission_denials_count` the log would not let us identify (the action
        prints *"full output hidden for security"* and uploads no artifact, so
        naming the denied tool would have cost two more merge cycles per guess).
        Roughly $4 of subscription spend for one review. The workflows and the
        `CLAUDE_CODE_OAUTH_TOKEN` secret are gone from all three repos.
        **When no reviewer has run, the honest answer is that the PR is
        unreviewed** — say so in the PR rather than reaching for a bespoke
        opinion, and lean on the HIL rig, cppcheck and the unit suites, which is
        where the real coverage was all along. ⚠️ **It is FOUR bots now, not
        three** — Greptile was installed later and has its own entry below; check
        it too before concluding a PR is unreviewed. That does not weaken the
        conclusion, it sharpens it: on 2026-08-29 three of six PRs really were
        reviewed by nobody, and Greptile's silence is the one that leaves no
        trace to notice.
      - ⚠️ **On THIS repo "reviewed by nobody" is too strong, because CodeQL runs
        on every PR and nothing in this section said so** — `.github/workflows/
        codeql.yml`, `pull_request: [main]`, check name **Analyze Python**. Its own
        header comment states the reason it exists: the LLM reviewers "are all LLMs
        trained on much the same public code, so their blind spots overlap", while
        it "is also free on public repos with no quota, so unlike every bot on this
        PR it cannot go quiet at the moment it is needed". That is the exact gap the
        four preceding notes catalogue, and the remedy was already installed. It is
        the sibling of the firmware repo's cppcheck (`qmk_firmware/CLAUDE.md` even
        says *"The host repo runs CodeQL instead"*) — so the fact was written down,
        just not in the file describing this repo's own board.
        - It **earns the slot**: on host#218 (2026-09-07) it produced three findings
          before any bot had run, and the PR carried a green Sourcery-skipped board
          at the time. Its findings arrive as inline review comments from
          `github-advanced-security[bot]` plus a review object — i.e. `get_reviews`
          sees it, which the five-reviewer check below has to account for.
        - ⚠️ **NEITHER of its two green ticks means "no findings" — this line used to
          say "judge it by the `Analyze Python` JOB, not the `CodeQL` check run", and
          that is WRONG.** The job reports whether the ANALYSIS ran, not what it
          found, so it concludes `success` either way: measured on host#226
          (2026-09-09), run 204 concluded `success` while carrying all **12** alerts,
          and runs 207/208 concluded `success` carrying none. Nothing in
          `get_check_runs` separates those.
          - **What DOES answer it is `get_review_comments`.** Every alert is a review
            thread, and GitHub flips the thread to `is_resolved: true` +
            `is_outdated: true` once the alert is fixed — so twelve resolved threads
            is POSITIVE evidence the round is closed, where "no new review object on
            the new head" is only the absence of evidence (and is exactly the
            false-negative the clean-CodeRabbit note below warns about). Read the
            threads, not the ticks.
        - ⚠️ It is **not** an answer to a design question and does not read prose;
          it finds the class of defect dataflow finds. "CodeQL was green" is not
          review cover for a refactor, only for what its queries cover.
        - ⚠️ **`py/unused-import` on a RE-EXPORT module is right about the file and
          wrong about the remedy — declare `__all__`, never delete the name.**
          CodeQL reasons within one module, so a name re-exported for other callers
          reads as dead to it, and it cannot see attribute access from elsewhere:
          on host#218 it flagged `THEME_DARK`/`THEMES` in `polyhost/gui/theme.py`,
          and `THEME_DARK` is genuinely used — as `theme.THEME_DARK`, from
          `tests/gui/theme_test.py`. Deleting the names to clear the alert would
          have broken the tests while the alert went green, which is the worst
          available outcome. `__all__` silences it AND states the surface honestly;
          verified by re-scan (the next run posted nothing). Make the `__all__`
          complete while you are there — an `__all__` that omits real public names
          is a new false claim in place of the old one. This repo has several such
          front doors (`gui/theme.py`, `core/events.py`, the `server/` package), so
          expect it again.
        - ⚠️ **`py/unused-global-variable` on a Qt `_APP` keepalive is the SAME
          family and the remedy is NOT to decline it — the repo already carries the
          form that satisfies it.** Fifteen test modules hold a QApplication alive so
          Qt's runtime is not garbage-collected out from under the next widget; twelve
          assign it at module level, and `tests/gui/macro_tab_test.py` pairs that with a
          `setUpModule` asserting `_APP is not None`, whose docstring says it is there
          *"to a reader and to a static analyser alike"*. The three files CodeQL flagged
          on #226 had deferred the construction into `setUpModule` with a `global` write
          nothing ever read — which is precisely the shape the query looks for. **So a
          finding on an established idiom is worth one grep before it is worth a reply:**
          `grep -rn "QApplication.instance() or QApplication" tests/` would have shown
          twelve siblings the alert does not fire on, and the difference between them and
          mine WAS the bug. Same lesson as the ControlServer deadlock — the remedy was in
          the tree and the failure was search.
      - ⚠️ **`actions_list` blows the tool token cap — 130–220 KB per call, even
        at `per_page: 3`** (kept from the above, because it applies to reading
        *any* workflow run). It saves the JSON to a file and tells you the path;
        parse that rather than retrying with a smaller page size — it is the
        per-run payload that is large, not the count. Hit 3× in one session.
        ⚠️ Read the conclusion defensively — an **in-progress run has no
        `conclusion` key at all**, so the obvious parse raises a KeyError on
        exactly the run you are waiting for:
        ```python
        d = json.load(open(saved_path))
        for r in d.get("workflow_runs", d):
            print(r["id"], r["status"], r.get("conclusion", "<running>"))
        ```
      - **Reading a job log: ask for ≥300 `tail_lines`.** Every job ends in ~40
        lines of git post-job cleanup, and a failing tool often dumps
        diagnostics *after* its own error, so a smaller tail lands squarely in
        the noise.
      - ⚠️ **`pull_request_read` STRIPS HTML-like tags out of a PR body, so a body
        that reads as corrupted through the MCP is usually fine on GitHub.** A body
        naming XML elements (`<sheetData>`, `<mergeCell ref>`, `<hyperlink ref>`)
        came back with those collapsed to empty backticks, which looks exactly like
        a body mangled at write time. **Verify with WebFetch against the rendered
        page before "fixing" it** — 2026-09-03 cost an edit that repaired nothing.
        Note also that editing a PR body **re-renders the CodeRabbit summary**, which
        is its own source of confusion (see the sticky-walkthrough notes above).
  - ⚠️ **A review that DID run, on the right commit, with an accurate walkthrough,
    can still have SKIPPED the file you care about — read the "Files skipped from
    review" list before trusting a clean verdict.** On qmk PR #198 (2026-08-11)
    CodeRabbit reported *"No actionable comments were generated 🎉"* against the
    right head, and its walkthrough had correctly refreshed (it dropped a claim the
    latest commit had deleted), so every tell above says *reviewed* — while the
    same comment listed `poly_keymap.c` under *"🚧 Files skipped from review as they
    are similar to previous changes"*. That file held **all** the new logic (a
    modifier latch, an ownership-gated release swallow, an inverted render path);
    the incremental heuristic judged it similar to its own earlier review of the
    same file, so the green verdict covered only a header, a tool script and a
    keymap. This is the complement of the stale-walkthrough traps above: there the
    walkthrough lies about *which commit*, here it is honest and the **coverage** is
    the gap. The fix is **`@coderabbitai full review`**, which re-reviews the whole
    diff regardless of incremental state — and it worked: the full pass read the
    file and confirmed the latch ownership and release-swallow gating specifically.
    ⚠️ Spend the slot deliberately, per the org-wide-limit note above: each
    `@coderabbitai review` consumes one **even when it is refused** ("⚠️ Action not
    completed / Review rate limited"), so retries starve the window they wait on. A
    **push** re-triggers a review without spending a request — on that PR the next
    commit is what finally got one to run after two requests had been eaten.
  - ⚠️ **The rate-limit wait is NOT a fixed hour — it stretches as you spend
    requests, and the "Trigger review" checkbox spends one exactly like the chat
    command.** On host #170 (2026-08-18) the first refusal said *"next review in
    19 minutes"*; waiting that out and asking again returned **52 minutes**, i.e.
    the second request pushed the window further out than the wait it was issued
    for. So "wait for the stated time, then retry" is the one strategy that
    reliably starves you — each retry buys a longer wait than it costs. Two
    consequences: on an under-10-stars repo (host + docs, see below) do **not**
    tick the box *and* comment `@coderabbitai review`, that is two slots for one
    review; and when a review is genuinely needed, spend the slot on the final
    commit and otherwise let a **push** trigger it (pushes don't draw on the
    quota — the note above).

- ⚠️ **There is a FOURTH reviewer — Greptile — and its failure mode is the
  QUIETEST of the four: when it does not review, it says so NOWHERE.** Every other
  note in this section catalogues a way CodeRabbit / Sourcery / Qodo go quiet, and
  the set was written when those three were all there was. Greptile posts no
  rate-limit banner and no "review skipped" box, so a PR it never looked at reads
  exactly like one it had no findings on. CodeRabbit at least renders a notice;
  Sourcery at least submits a review object whose body says why. **So Greptile
  cannot be counted as cover unless you have confirmed a review object exists**,
  and its silence must never be read as a clean bill.
  - ⚠️ **The `Greptile Review` CHECK RUN is not that confirmation — measured, it
    pointed the WRONG WAY every time.** It is a check run like Sourcery's, and a
    green one is the same trap `qmk_firmware/CLAUDE.md` already records for
    Sourcery ("goes GREEN while no review happened"). Here it is stranger: across
    the three completed PRs of the session below, the check run and the review
    object were **perfectly anti-correlated** — qmk#251 had a green `success`
    check that ran two minutes and **no review object at all**, while qmk#253
    (two reviews) and host#202 (one) had **no check run**. Four data points is
    not a mechanism and this is deliberately not theorised into one; the
    operative rule is simply that **the check run answers a different question
    than "was this reviewed"**, and only `get_reviews` answers that one.
  - **Measured over one six-PR session** (2026-08-29, across `PolyKybdHost`,
    `qmk_firmware` and `Adafruit-GFX-Library` — `pull_request_read`
    `get_reviews` on each): Greptile reviewed **2 of 6**. Sourcery refused all
    six, CodeRabbit reviewed one. **Three PRs — Adafruit#11, host#201, qmk#251 —
    were reviewed by NOBODY**, and only the Sourcery refusal notice made that
    visible.
  - ⚠️ **There is no size or repo pattern to predict from.** It skipped a 2-line
    `requirements.txt` PR and a small workflow PR while reviewing a docs-only one,
    so "it was too big / too trivial" does not explain the gaps. Do not build an
    expectation of when it runs.
  - ✅ **What it does well, when it runs, is the thing the others get wrong:
    it re-reviews the follow-up commit.** On qmk#253 it reviewed `dcaaf27` and
    then `24aa1ed` on its own, both `commit_id`s matching the head — none of the
    stale-walkthrough / sticky-summary / false-`✅ Addressed in <sha>` bookkeeping
    catalogued above. It also reviews **`.md`-only PRs**, which CodeRabbit's
    incremental heuristics routinely skip, so it is often the only reviewer that
    reads a CLAUDE.md change — and given how much load-bearing detail lives in
    these files, that is not a lesser class of review.
  - **Its findings get the same scrutiny as anyone's** — verify, don't defer. Its
    one real catch that session was a *consistency* finding an LLM is well placed
    to make and a linter is not: a note being added to this very file
    **contradicted an entry two paragraphs above it**, which is what produced the
    reconciliation in the Merge-Risk-sha note below.
    - ⚠️ **It posts the SAME finding TWICE, and answering one leaves the other
      open** — once as an issue comment (its summary, carrying a "Prompt To Fix
      All With AI" block) and once as an inline **review thread**. They arrive as
      separate events minutes apart, so a reply to the comment does not resolve
      the thread. Reply on the **thread** and resolve it; the issue comment is
      the copy that can be left. Seen on host#200 and host#204.
    - It cites `CLAUDE.md` as "Context Used", i.e. it reads this file — which is
      a reason to keep these notes accurate about *intent*, not only about
      mechanism.
  - **The standing check is the same one every failure mode above is caught by**
    and it is two seconds: `pull_request_read` `get_reviews`, then **for each
    review, both** (a) its `commit_id` equals the PR's head sha, **and** (b) its
    body is not a refusal notice. No *check run*, bot comment or walkthrough adds
    anything to that pair — but ⚠️ **the pair alone is NOT sufficient either**: a
    clean CodeRabbit review leaves no review object at all, so it reads as "never
    read" (see the false-negative bullet below, which is the other half of this
    check). Run both together.
    - ⚠️ **(b) is not a refinement, it is half the check — a REFUSAL IS A REVIEW
      OBJECT, and it carries the head sha.** Sourcery's budget and diff-too-large
      notices arrive as `COMMENTED` reviews stamped with the current head, so a
      `commit_id`-only rule marks the PR reviewed. The six-PR table above proves
      it against itself: Sourcery submitted such an object on **every** one, each
      matching that PR's head — including the three the table records as reviewed
      by nobody. This entry asserted the `commit_id` rule alone anyway, two
      paragraphs below a Sourcery note that already said the tell is the body
      text (caught by Greptile in review, 2026-08-30). **A check that contradicts
      another entry in the same file is the failure mode this section keeps
      producing; grep for the sibling note before writing the rule.**
  - ⚠️ **The standing `get_reviews` check has a FALSE NEGATIVE pointing the OPPOSITE
    way from the refusal case: a CLEAN CodeRabbit review creates NO REVIEW OBJECT AT
    ALL.** It reports *"No actionable comments were generated 🎉"* by **editing its
    existing summary comment**, so `get_reviews` returns nothing for that head and
    the pair-check above reads a clean pass as "never reviewed". Measured on qmk#268
    (2026-09-03). The two failure modes therefore point in opposite directions — a
    refusal IS an object and means nothing, a clean pass is NO object and means
    everything — so **neither signal decides it alone**: read the summary comment's
    own body (its `📥 Commits` range, and whether it says *skipped* or *no actionable
    comments*) alongside `get_reviews`.
  - ⚠️ **A commit whose only reviewable file is GENERATED is skipped outright, which
    on a data change is every commit.** *"Review skipped as selected files did not
    have any reviewable changes"* — twice on qmk#268, where the diff was `lang_lut.c`
    (a cog-generated table) plus `lang_lut.xlsx`, and the workbook is excluded by
    CodeRabbit's own `!**/*.xlsx` path filter. So a per-language data pass is
    **structurally unreviewable** by it: no banner, no rate limit, nothing wrong —
    just no review, ever. Plan the verification accordingly (assert the generated
    diff is confined to what you meant, and measure the rendered result) rather than
    expecting a reviewer to catch a wrong cell.
  - ⚠️ **This entry has now been wrong THREE times, and every correction came
    from the API rather than from thinking harder.** The first draft claimed
    Greptile had reviewed all six PRs — it was 2. The second claimed it emits "no
    check run" — it emits one, and a green one at that. The third said Sourcery's
    countdown "says when a review is actually available again" — one was
    superseded three minutes later. Reviewer behaviour is exactly the kind of
    claim that feels observed and is not: every bot posts *something* on most PRs,
    so the impression of having been reviewed accrues without a single review, and
    a confidently-worded notice reads as a fact about the system rather than as
    one sample. **Nothing in this section should be written from recollection —
    `get_reviews`, `get_check_runs`, and the timestamps, every time.**

- ⚠️ **Sourcery refuses in TWO different ways, and only one of them is the weekly
  budget.** Both arrive as a `COMMENTED` review whose entire body is the notice, so
  the tell is the body text, not the presence of a review:
  - **Budget** — *"you've used your own review budget of 250,000 diff characters
    for the last 7 days ... You can request another review in 1 day and 16 hours by
    commenting `@sourcery-ai review`"*. ⚠️ Note the FIGURE MOVES — 250,000 here,
    500,000 in an older notice `qmk_firmware/CLAUDE.md` quotes, and **150,000** on
    host#226 (2026-09-08) — so it is per user and rolling 7 days, but the number is
    only ever the one in the message in front of you. ⚠️ **The countdown is NOT "come
    back then" — do not plan around it.** Measured across the four PRs of
    2026-08-30, four refusals issued **within 61 seconds of each other** quoted
    four different waits — 4 days, 1 day 3 hours, 1 day 3 hours, 19 hours 41
    minutes — so it is computed per PR, not from one global clock. And one of them
    was superseded almost immediately: qmk#255 was told *"1 day and 3 hours"* at
    08:58:18 and Sourcery submitted a real **`APPROVED`** review on its next
    commit at **09:02:21, three minutes later**. The other three pushed follow-up
    commits too and got nothing, so this is neither reliable nor universal —
    **1 of 4**. ⚠️ **Reproduced 2026-09-01 on docs#69, and the second instance names
    the trigger: a PUSH.** Refused on `76716da` at 12:44:37 quoting *"1 day and 21
    hours"*, then `APPROVED` on `fa641b1` at **12:48:41 — four minutes later**, with
    nothing in between but a commit. So the countdown tracks the PR state it was
    computed against, not a clock you can wait out, and a push is what re-tries it —
    the same shape as CodeRabbit's "a push re-triggers a review without spending a
    request" above. Both reviews carried the right head sha, so the standing
    `commit_id`-plus-body check reads them correctly.
    Treat the countdown as an upper bound of unknown tightness, keep
    pushing, and check `get_reviews` rather than waiting out the clock.
  - **Diff too large** — *"Sorry, we are unable to review this pull request. The
    GitHub API does not allow us to fetch diffs exceeding 20000 lines"*. A hard
    ceiling, not a quota, so waiting does nothing. This is Sourcery's version of
    CodeRabbit's >100-file skip (`qmk_firmware/CLAUDE.md`), and the two land on the
    same PRs: an upstream merge, or anything mechanical like untracking a committed
    build directory (Adafruit#11, 5,224 files). On such a PR **both** LLM reviewers
    are out by construction, so plan the review before opening it — split the
    mechanical change from the reviewable one, or accept that only cppcheck, the
    build and the rig will read it.

- ⚠️ **Sourcery's `✅ Addressed in <sha>` line names the CURRENT HEAD, not the commit
  that fixed anything — treat it as a timestamp, never as evidence.** On docs#67
  (2026-08-27) the same two findings were re-rendered three times, credited in turn to
  `c0f7928`, `896c3f6` and `0b859b2`. Only the first actually fixed them; the second
  touched one unrelated file and the third added a CLI flag. The claim is generated by
  re-checking the finding against the tree and stamping whatever head it re-checked at,
  so it is *right about the state* and arbitrary about the cause.
  - **Why it matters beyond tidiness:** the obvious reading is "my last push fixed
    this", which invites either a duplicate fix or — worse — the belief that a finding
    you have NOT addressed was handled by a commit that merely happened to land after
    it. `git show --stat <sha>` settles it in one command; do that before believing
    either direction.
  - The findings themselves were both real and both worth having (a Keymap Editor page
    contradicting the new Macros page, and a CLI reference missing a whole subcommand
    family). This is the same "conclusion sound, evidence invented" shape recorded for
    CodeRabbit below — take the finding, check the attribution.
  - ⚠️ **CodeRabbit does this too, and its version is WORSE: it AUTO-RESOLVES the
    thread on the strength of the false attribution.** On qmk#250 (2026-08-29) its one
    inline finding was stamped `✅ Addressed in commit 9357ee4` and the thread closed —
    but `git show 9357ee4 -- <path> | grep -c _WIN32` returns **0**: that commit never
    touched the flagged sentence, which was still at head. The real fix landed two
    commits later in `386d74f`. Sourcery's version merely misleads; this one *files the
    thread away*, and a resolved thread is one nobody re-reads — so an unaddressed
    finding can disappear silently. **Check the sha before trusting a resolution, not
    just before trusting a claim.**
    - **Three of its self-reports on that one PR were false, while the finding itself
      was genuine and improved the change** (it correctly caught that `#ifndef _WIN32`
      does not exclude an RP2040 build, so citing it as proof of "desktop-only" was bad
      reasoning). The other two: it said *"I couldn't resolve this review thread … so it
      remains open"* while the API reported `is_resolved: true`, and its **Merge Risk**
      line still read `up to b6c1b` two pushes later, in the *same comment* that
      correctly named the reviewed range `b6c1b7f..386d74f`. So the sha-in-the-Merge-Risk
      tell recorded above is itself only as good as the render you are reading — it went
      stale while the commit range beside it stayed accurate.
    - ✅ **Replying with the evidence is worth it.** CodeRabbit conceded every point
      (including "The fix belongs to `386d74f`. The previous attribution to `9357ee4`
      was incorrect") and recorded a durable repo learning about the file. Same payoff
      as the PolyKybd#35 stale-netlist reply — one reply, a permanent correction.

- **CodeRabbit's plan is PER-REPOSITORY, and Pro Plus does NOT mean a fixed
  number of included reviews per hour — read the figure off the run footer.**
  Private repos here get the summary-only Free tier; public ones
  (PolyKybd, qmk_firmware) report `Plan: Pro Plus` — but the run footer then says
  *"Your plan provides up to N included reviews per hour; M remain after this review"*.
  ⚠️ **Read N off the run footer rather than trusting a number written here — it is not
  a constant.** This line said "2" for a while; the `qmk_firmware` footer on 2026-08-29
  read **1**, on the same Pro Plus plan.
  ⚠️ **A request consumes a slot even when it is refused**, so retries starve the window
  they are waiting on (already recorded for the org-wide limit; it applies to the hourly
  one too). A **push** re-triggers a review without spending a request. And after a few
  quick commits CodeRabbit **auto-pauses** the branch — from then on nothing is reviewed
  until you comment `@coderabbitai review`, which is easy to miss because the paused
  comment still renders a full walkthrough and green pre-merge checks.
