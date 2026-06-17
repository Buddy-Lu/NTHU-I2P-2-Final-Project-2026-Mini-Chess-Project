# Checkpoint 6 — Evaluation tuning ✅

**Status:** PASS (builds, unit tests pass, no boss regression)

## What was added (in `state.cpp::evaluate`, KP branch)

Two principled terms on top of the existing material + PST + king-tropism +
mobility evaluation:

1. **Passed-pawn bonus** — a pawn with no enemy pawn able to block or capture it
   on its own or adjacent files ahead is "passed". The bonus grows the closer it
   is to promotion (`passed_pawn_bonus[rows_to_go] = {0,70,45,28,16,8}`). On a
   6×5 board a pawn that promotes becomes a **Queen**, which is usually
   game-deciding, so passed pawns are weighted heavily.
2. **Tempo** — a small bonus (`+6`) for the side to move, nudging the engine
   toward active, initiative-keeping play.

Both are inside the `use_kp_eval` branch; the plain material-only eval is left
untouched as a baseline.

## Why these (for the demo)

- The eval's job is to estimate who is winning in *quiet* positions where search
  can't see the outcome. Material/PST/tropism already cover "who has more and
  better-placed pieces" and "is my attack near their king". The missing
  positional ideas most relevant to *this* game are (a) **promotion races** —
  hence passed pawns, and (b) keeping the **initiative** — hence tempo.

## Validation (and its honest limits)

- **Builds clean; unit test 12/12** (move generation unaffected).
- **Sane play**: depth-8 startpos returns a normal developing move.
- **No boss regression**: vs `boss-ubgi`, 2 s/move, 4 games → **+4 −0 =0
  (4.0/4)**, both colors. Sweep held.

> Measurement note: the engine already sweeps the boss 4–0 and self-play A/B is
> dominated by the White first-move advantage, so small eval gains can't be
> *measured* by match score here — only regressions can. These two terms are
> standard, conservative, and theory-backed; they target the AB/PVS baselines
> and class-ranking opponents we can't run locally. Values were kept moderate to
> avoid over-valuing pawns (which would invite bad sacrifices).
