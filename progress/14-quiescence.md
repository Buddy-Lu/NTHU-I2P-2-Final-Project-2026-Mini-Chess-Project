# Checkpoint — Quiescence search ✅ (implemented; strength validation ongoing)

**Status:** implemented, builds, functionally correct · rubric **+1 point**
(Quiescence)

## What was added

- `AlphaBeta::qsearch()` in `src/policy/alphabeta.cpp` — a captures-only
  negamax search used at the leaf (`depth <= 0`) instead of a plain static eval.
- Toggle param **`UseQuiescence`** (default `true`) on the `alphabeta` policy so
  it can be A/B-tested on vs off.

## How it works (for the demo)

The horizon effect: a fixed-depth search can stop in the *middle of a trade*
(e.g. right after we grab a pawn, before the recapture), giving a wildly wrong
score. Quiescence fixes this by, at the leaf, continuing to search **only
captures (and promotions)** until the position is "quiet", then evaluating.

- **Stand-pat**: the side to move may decline to capture and take the static
  score. If `stand_pat >= beta` we cut off; otherwise `stand_pat` raises alpha.
  This bounds the search so we never *force* a losing capture.
- Same negamax window as the main search; ordered by MVV-LVA so the best capture
  is tried first.

## Functional check (depth 6, startpos)

| | seldepth | nodes | score | move |
|---|---|---|---|---|
| quiescence ON  | **18** | 144,161 | cp 0  | c2c3 |
| quiescence OFF | 6  | 26,392 | cp −19 | b2b3 |

Quiescence extends the selective depth to 18 (following capture chains) and
returns a more stable score — exactly the intended behaviour.

## Measurement note (important)

A head-to-head **q-ON vs q-OFF** self-play match (2 s/move, 4 games) came out
**2–2 with White winning all 4 games**. Conclusion: at this depth/time the
**first-move (White) advantage dominates** and swamps the quiescence delta — so
self-play between two near-identical engines is a poor way to measure a small
improvement. The proper test is **vs a fixed external opponent**:

- vs `minimax-strong` / `boss-ubgi` — see `17-baseline-results.md` (running).

Quiescence is kept on by default: it is a standard, correctness-improving
feature (and a rubric item), most valuable in sharp tactical positions and
against the boss, where the horizon effect bites hardest.
