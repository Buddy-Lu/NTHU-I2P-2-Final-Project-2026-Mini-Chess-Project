#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Self-play A/B match: candidate engine vs reference engine, over PAIRED
# random-opening games (cancels White's first-move advantage so small strength
# differences become measurable). Prints the candidate's score %.
#
# Run from the project root in Git Bash:
#   bash tools/selfplay.sh <candidate.exe> <reference.exe> [games] [depth] [open] [seed] [white_param]
#
# Examples
#   # Runtime-toggle A/B (no rebuild): null-move OFF (candidate) vs ON (reference)
#   bash tools/selfplay.sh build/minichess-ubgi.exe build/minichess-ubgi.exe 40 7 4 7 "UseNullMove=false"
#
#   # Two-binary A/B (e.g. an eval variant built with build_variant.sh)
#   bash tools/selfplay.sh build/tempo12-ubgi.exe build/minichess-ubgi.exe 40 7 4 7
#
# Decision rule: keep a change only if the candidate scores >= 55% over >= 40
# games (and it still sweeps boss-ubgi 4-0). 30 games is within noise (SE ~9%).
# ---------------------------------------------------------------------------
set -u

CAND=${1:?need candidate engine path}
REF=${2:?need reference engine path}
GAMES=${3:-40}
DEPTH=${4:-7}
OPEN=${5:-4}
SEED=${6:-7}
WPARAM=${7:-}

PY=.venv/Scripts/python.exe

ARGS=(-m cli.cli
  --white "$CAND" --black "$REF"
  --white-algo pvs --black-algo pvs
  --games "$GAMES" --depth "$DEPTH"
  --open-random "$OPEN" --seed "$SEED" --quiet)

# The candidate is engine1 (--white path); --white-param follows the candidate
# across color swaps, so this A/Bs the param vs the reference's defaults.
if [ -n "$WPARAM" ]; then
  ARGS+=(--white-param "$WPARAM")
fi

echo "candidate=$CAND  reference=$REF  games=$GAMES depth=$DEPTH open=$OPEN seed=$SEED ${WPARAM:+param=$WPARAM}"
"$PY" "${ARGS[@]}"
