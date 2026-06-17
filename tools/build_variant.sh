#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build a LABELLED engine variant from a one-line source edit, WITHOUT leaving
# the canonical source or the canonical binary modified. Use this for eval /
# search-constant experiments (things that aren't runtime params).
#
# Run from the project root in Git Bash:
#   bash tools/build_variant.sh <name> <file> <sed_expr>
#
# Example: build an engine with tempo bumped 6 -> 12, as build/tempo12-ubgi.exe
#   bash tools/build_variant.sh tempo12 src/games/minichess/state.cpp \
#        's/kp_tempo = 6/kp_tempo = 12/'
#
# Then compare it to the reference (current build):
#   bash tools/selfplay.sh build/tempo12-ubgi.exe build/minichess-ubgi.exe 40 7 4 7
#
# The script backs up the file, applies the edit, builds the variant, then
# ALWAYS restores the original source and rebuilds the canonical binary — so
# your working tree is exactly as it was before.
# ---------------------------------------------------------------------------
set -eu

NAME=${1:?need a variant name}
FILE=${2:?need a source file to edit}
SED=${3:?need a sed expression}

BK="${FILE}.bak.$$"
cp "$FILE" "$BK"
restore() { mv -f "$BK" "$FILE" 2>/dev/null || true; }
trap restore EXIT          # safety: restore source even on error

sed -i "$SED" "$FILE"
if cmp -s "$FILE" "$BK"; then
  echo "ERROR: sed expression matched nothing (file unchanged): $SED" >&2
  exit 1
fi

mingw32-make minichess >/dev/null 2>&1
cp build/minichess-ubgi.exe "build/${NAME}-ubgi.exe"

restore
trap - EXIT
mingw32-make minichess >/dev/null 2>&1   # restore canonical binary too

echo "built build/${NAME}-ubgi.exe   (canonical source + binary restored)"
