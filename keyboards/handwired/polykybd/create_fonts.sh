#!/usr/bin/env bash
# Deprecated thin wrapper — kept for muscle memory.
#
# Font generation is now config-driven: edit fonts/fonts.yaml and run the
# Python generator, which drives `fontconvert` and writes the per-category
# headers plus base/fonts/gfx_used_fonts.h.  See fonts/README.md for details.
#
# This script just forwards to that generator (any args are passed through,
# e.g. --check or --fontconvert PATH).
set -e
cd "$(dirname "$0")"
echo "note: create_fonts.sh is deprecated; running fonts/generate_fonts.py instead." >&2
exec python3 fonts/generate_fonts.py "$@"
