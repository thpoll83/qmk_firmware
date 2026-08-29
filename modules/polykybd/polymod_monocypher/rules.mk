# The vendored Monocypher sources keep their upstream file names, so there is no
# polymod_monocypher.c for the module build's `$(wildcard <dir>/<name>.c)` to pick
# up (that wildcard tolerates the absence) — the two real sources are listed here
# instead, which the generated community_rules.mk `-include`s.
SRC += modules/polykybd/polymod_monocypher/monocypher.c \
       modules/polykybd/polymod_monocypher/monocypher-ed25519.c
