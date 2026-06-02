## Summary
<!-- What does this PR change and why? -->


## Version bump label
<!-- The CI automatically bumps the firmware version when this PR merges.
     Default (no label) = patch bump.  Add ONE label to override: -->

| Label | When to use |
|---|---|
| *(none)* | Bug fix, small tweak — patch bump `0.0.x` |
| `bump:minor` | New feature, backwards-compatible firmware change — minor bump `0.x.0` |
| `bump:major` | Breaking change or major redesign — major bump `x.0.0` |
| `bump:protocol` | HID protocol change — increments `PROTOCOL_VERSION` only (must also update host `__protocol__`) |

> **Protocol changes** require a matching `bump:protocol` PR in
> [PolyKybdHost](https://github.com/thpoll83/PolyKybdHost) so both sides
> stay in sync.

## Testing
- [ ] Compiled successfully (`qmk compile -kb handwired/polykybd/split72 -km default`)
- [ ] Flashed and tested on hardware
- [ ] If protocol changed: host updated and tested together
