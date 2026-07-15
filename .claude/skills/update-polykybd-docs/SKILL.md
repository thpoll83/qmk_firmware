---
name: update-polykybd-docs
description: When you add or change a USER-FACING feature in the firmware or host — a new HID command / PROTOCOL_VERSION, a polyctl subcommand, a host setting or tray menu item, a keyboard feature (glyph script, idle style, brightness, font pack), or a language/layout — also extend the public documentation site (the polykybd-docs repo). Use whenever a change would make an existing docs page wrong or leave a new capability undocumented, at the end of feature work, or when the session-retro skill flags a docs gap. NOT for internal-only refactors, bug fixes with no user-visible change, or the firmware/host code change itself.
---

# Extend the PolyKybd docs when a feature lands

PolyKybd's user documentation is a **separate Astro Starlight site** in the
`polykybd-docs` repo (published at https://www.polykybd.org). A feature is not
"done" until the docs describe it — otherwise the site silently drifts (e.g. the
HID reference sat at PROTOCOL_VERSION 3 while the firmware was at 11, and glyph
scripts / font packs shipped undocumented for months).

This skill is the checklist for keeping the docs in lockstep. It is **cross-repo**:
the docs change is its **own branch + PR in `polykybd-docs`**, never part of the
firmware/host PR.

## When it applies (and when it doesn't)

Update the docs when the change is **user-visible**:

- A new / changed **HID command** or a **PROTOCOL_VERSION** bump.
- A new **`polyctl`** subcommand or flag.
- A new **host setting**, tray menu entry, daemon behaviour, or install/startup step.
- A new **keyboard feature** (idle style, glyph script, brightness/sensor, font pack,
  overlay behaviour, multi-machine).
- A new **language / layout** (the `add-polykybd-language` skill covers the firmware
  side; this covers the docs mention).
- Any change that makes an existing docs statement **wrong** (renamed thing, changed
  default, removed option).

Skip it for internal refactors, bug fixes with no user-visible effect, or
firmware/host build-plumbing.

## Where the docs live

Sibling checkout `../polykybd-docs` (clone `thpoll83/polykybd-docs` if absent).
Default branch **`main`**. Astro Starlight; pages are Markdown/MDX under
`src/content/docs/<section>/<page>.{md,mdx}`. The URL is the file path
(`using/glyph-scripts.mdx` → `/using/glyph-scripts/`).

Sections (nav order): **Introduction · Assembly · Setup · Using the Keyboard ·
Host Software · Firmware · Development · Reference**. The sidebar is **hand-curated**
in `astro.config.mjs` (`sidebar: [...]`, `slug`-based) — a new page must be added
there or it won't appear in the nav.

## Feature → page map (where to write)

| You changed… | Update these pages |
|---|---|
| HID command / `PROTOCOL_VERSION` | `reference/hid-protocol.mdx` — the command table **and** the per-version history list. Bump the "current" version. |
| A `polyctl` subcommand | `software/cli.mdx` (subcommand table) |
| Host setting / daemon / startup | `software/usage.mdx`, `software/architecture.mdx`, or `setup/installation.mdx` |
| Keyboard feature (glyph script, idle, brightness, font pack, overlays) | the matching page under `using/` or `firmware/` (add a new page if it's a new feature area) |
| A new language / layout | `using/languages.mdx` |
| New term worth defining | `reference/glossary.mdx` |
| Anything genuinely new | add a page **and** a sidebar entry in `astro.config.mjs`; consider a cross-link from a related page so it's discoverable |

Match the site's voice: **user-facing**, friendly, `<Aside>` callouts and tables,
no internal debugging detail. If you move/rename a page, add a `redirects` entry in
`astro.config.mjs` for the old URL.

## Procedure

1. **Get the docs repo current**: `git -C ../polykybd-docs fetch origin main &&
   git -C ../polykybd-docs checkout -B claude/docs-<feature-slug> origin/main`.
   (Docs are their own PR — do this even mid-firmware-work.)
2. **Find the affected page(s)** from the map above; grep the docs for the old
   fact if you're correcting drift (`grep -rn "PROTOCOL_VERSION 3" src/`).
3. **Edit / add** the page(s). For a new page, add the `slug` to the sidebar in
   `astro.config.mjs`; add a cross-link from a related page.
4. **Verify** (see below).
5. **Commit + push** the docs branch and open a PR **in `polykybd-docs`** (base
   `main`). Keep it separate from the firmware/host PR; note the companion PR in
   both descriptions.

## Verify

Cheap checks first (no build needed):

```bash
cd ../polykybd-docs
# every internal /section/page link resolves to a file:
grep -rhoE '\]\(/[a-z0-9-]+/[a-z0-9-]+' src/content/docs --include='*.md' --include='*.mdx' \
  | sed -E 's/^\]\(//' | sort -u \
  | while read -r l; do [ -f "src/content/docs$l.mdx" ] || [ -f "src/content/docs$l.md" ] || echo "BROKEN: $l"; done
```

Then a full build. ⚠️ **`npm ci` fails in the sandbox because `sharp` can't fetch
its prebuilt binary through the proxy (HTTP 403)** — this is an environment limit,
not your change. Work around it:

```bash
npm ci --ignore-scripts            # installs deps, skips sharp's postinstall
# temporarily swap in a passthrough image service so the build needs no sharp:
cp astro.config.mjs /tmp/ac.bak
node -e 'let s=require("fs").readFileSync("astro.config.mjs","utf8");
 s=s.replace("import mermaid from \x27astro-mermaid\x27;","import mermaid from \x27astro-mermaid\x27;\nimport { passthroughImageService } from \x27astro/config\x27;");
 s=s.replace("export default defineConfig({","export default defineConfig({\n  image: { service: passthroughImageService() },");
 require("fs").writeFileSync("astro.config.mjs",s);'
node_modules/.bin/astro build      # expect "N page(s) built", no errors
cp /tmp/ac.bak astro.config.mjs    # RESTORE — never commit the passthrough tweak
```

If you added heading anchors in links, confirm each target heading exists (Starlight
slugs headings as lowercase, non-alphanumerics dropped, spaces→`-`; an em-dash "—"
between words yields a double hyphen).

## Pitfalls

- **Keep the HID protocol reference authoritative.** On any command / version
  change, update BOTH the command table and the version history, and the "current
  version" wording. This is the page most prone to silent drift.
- **A new page needs a sidebar entry** in `astro.config.mjs` — Starlight won't
  auto-add it, and an orphan page is invisible.
- **Docs are a separate PR** in `polykybd-docs` (base `main`) — do not bury a docs
  edit inside a firmware/host PR; they merge on different branches.
- **Never commit the passthrough image-service tweak** — it's only to let the build
  run in the sandbox; restore `astro.config.mjs` before committing.
- **User voice, not dev notes.** These pages are for keyboard owners; put deep
  mechanism in the firmware/host `CLAUDE.md` or the `development/` section, not the
  user pages.
- Add a **redirect** when you move/rename a page (old bookmarks are live).
