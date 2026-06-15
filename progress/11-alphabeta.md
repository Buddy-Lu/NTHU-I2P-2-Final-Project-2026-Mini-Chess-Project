# Checkpoint 1 — Alpha-Beta pruning ✅

**Status:** PASS · rubric **+1 point** (Alpha-Beta)

## What was added

- `src/policy/alphabeta.hpp` / `alphabeta.cpp` — negamax with alpha-beta
  pruning + **MVV-LVA** move ordering. Same fixed-depth contract as minimax
  (iterative deepening + the 10 s time control stay in the UBGI layer).
- Registered `"alphabeta"` in `src/policy/registry.hpp`.
- Added `alphabeta` to `ALGO_CHOICES` in `cli/cli.py` so matches can select it.

## How it works (for the demo)

- **Negamax**: one routine scores from the side-to-move's view; the child is
  searched with the window negated/flipped (`-eval_ctx(.., -beta, -alpha)`), so
  the opponent's "minimise" is just our "maximise" on the negated score.
- **Alpha** = best the side to move is already guaranteed; **beta** = best the
  opponent is already guaranteed. When `alpha >= beta` the opponent would never
  allow this line, so we **cut off** (skip the remaining sibling moves).
- **Move ordering (MVV-LVA)**: try captures first (Most-Valuable-Victim minus
  Least-Valuable-Attacker), then promotions, then quiet moves. Trying the best
  move first tightens the window early, which is what makes pruning pay off.

## Correctness (must equal minimax at equal depth)

Same **score** and **best move** as plain minimax, with far fewer nodes:

| Position (depth 6) | minimax: score / move / nodes | alphabeta: score / move / nodes | node ↓ |
|---|---|---|---|
| startpos | −19 / b2b3 / 621,470 | −19 / b2b3 / **26,392** | 23.5× |
| after a2a3 e5e4 b2b3 | −15 / c5c4 / 1,015,020 | −15 / c5c4 / **24,467** | 41.5× |

(When several moves share the best score, AB and minimax may pick different
equally-good moves because the ordered move list differs — both are correct.)

## Strength vs baselines

| Opponent | Condition | Result (this engine) |
|---|---|---|
| `minimax-weak`   | depth 6     | **+4 −0 =0** (4.0/4, 100%) |
| `minimax-strong` | depth 7     | **+2 −0** (both colors won) |
| `minimax-strong` | 2 s / move  | **+3 −1 =0** (3.0/4, 75%) |

→ **weak and strong baselines both cleared**, including under the real
time-control condition. (The AlphaBeta and PVS baselines are TA-held — no local
exe — so they can't be benchmarked here; `boss-ubgi.exe` is the local hard
target still to face.)

## Why this matters under the real 10 s/move limit

Plain minimax has no pruning, so under a time budget it only reaches shallow
depth and can lose. Alpha-beta visits ~20–40× fewer nodes for the same depth,
so in the same time it searches several plies deeper — the basis for beating the
stronger baselines, and the foundation for quiescence + PVS next.

## Next steps

- Quiescence search (rubric +1): extend captures at the leaf to avoid horizon
  blunders.
- PVS (rubric +2): null-window re-search on top of alpha-beta.
- Then face `boss-ubgi.exe`; tune eval; write report.
