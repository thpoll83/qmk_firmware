---
paths:
  - "modules/polykybd/**/*.c"
  - "modules/polykybd/**/*.h"
  - "modules/polykybd/**/rules.mk"
---
# QMK community modules

Notes: `keyboards/polykybd/COMMUNITY_MODULES.md`; the `extract-qmk-module` skill drives
a conversion.

- **Listing a module in `keyboard.json` IS the enable** — the build defines
  `COMMUNITY_MODULE_<NAME>_ENABLE`. No `SRC +=`, no bespoke `-D`; gate consumer code on
  the generated define.
- ⚠️ **Module hooks run BEFORE `_kb`/`_user`.** That is the whole argument for deleting
  explicit init/task calls in favour of hooks — verify it before doing so, and don't
  double-probe.
- ⚠️ **Overriding the non-suffixed hook means calling the `_kb` link yourself**, or the
  keyboard/keymap specialisations are silently dropped.
- **This fork is on module API 1.1.2.** Read `data/constants/module_hooks/*.hjson`
  rather than the docs table, which stops at 1.1.0.
- ⚠️ **The LTR-559 is side-agnostic** — never re-gate it on `is_right_side()`.
