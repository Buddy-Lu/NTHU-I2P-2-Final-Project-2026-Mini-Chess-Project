# Checkpoint — Null-Move Pruning ✅

**Status:** PASS (deeper search; boss regression-checked)

## What was added (in `pvs.cpp`)

Null-move pruning, behind a **`UseNullMove`** toggle (default on). Before
searching a node's moves, we let the opponent "move twice" (we pass, via
`State::create_null_state()`) and search that to reduced depth `depth-1-R`
(R = 2) with a null window. If even after giving the opponent a free move our
score still beats beta, the real position is almost certainly a cutoff, so we
prune it immediately.

### Safety guards (this is the one heuristic that can be *unsound*)

- Only when `depth >= 3` (need reduced depth to spend).
- **Not in check** — we check `create_null_state()->game_state != WIN` (in the
  king-capture model, "in check" = the opponent can capture our king next).
- **Non-pawn material present** — skip in pawn/king endgames to avoid
  **zugzwang** (where passing is artificially good).
- **Non-mate window** — don't prune when beta is a mate score.
- **No back-to-back nulls** — the null search is called with `null_ok = false`.

## Why it helps (for the demo)

In most positions, doing *nothing* is worse than your best real move (the "null
move observation"). So if a search with a free opponent move still fails high,
the real move surely does too — we skip a full-width search there and spend the
saved time going deeper.

## Effect

startpos, `go movetime 3000`:

| | depth | nodes | move |
|---|---|---|---|
| null off | 11 | 3.70M | b2b3 |
| **null on** | **12** | 4.75M | b2b3 |

+1 ply in the same budget. Tactical sanity (after `a2a3 e5e4 b2b3 d6e4`, d8):
returns `d2d3`, correctly attacking the knight on e4.

## Strength vs boss

- vs `boss-ubgi`, 2 s/move, 4 games: **+4 −0 =0 (4.0/4)**, both colors — sweep
  held, no regression. Null move kept on by default.
