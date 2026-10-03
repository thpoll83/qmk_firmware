## Summary
<!-- What does this PR change and why? -->


## Version bump label
<!-- The CI automatically bumps the firmware version when this PR merges.
     Default (no label) = patch bump.  Add ONE label to override: -->

| Label | When to use |
|---|---|
| *(none)* | **The default since 1.0.** Bug fix, diagnostic, developer tool or small addition, even one that bumps the protocol — patch bump `x.y.Z` |
| `bump:minor` | A feature an owner would call new, the kind that names a release — minor bump `x.Y.0`. When unsure, leave it off |
| `bump:major` | Breaking change or major redesign — major bump `X.0.0` |
| `bump:protocol` | Only when the PR does NOT already edit `PROTOCOL_VERSION` in source — increments it after merge. Omit it when the source bumps it, or it is bumped twice |
| `bump:none` | Docs, skills or `scripts/` only: cannot change the firmware image — no bump |

> **Protocol changes** need a matching `__protocol__` change in
> [PolyKybdHost](https://github.com/thpoll83/PolyKybdHost) so both sides stay in sync.

## Testing
- [ ] Compiled successfully (`qmk compile -kb polykybd/split72 -km default`)
- [ ] Flashed and tested on hardware
- [ ] If protocol changed: host updated and tested together
