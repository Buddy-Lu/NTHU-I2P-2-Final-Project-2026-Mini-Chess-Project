# Checkpoint — Aspiration Windows ✅

**Status:** PASS (deeper search; boss regression-checked)

## What was added (in `pvs.cpp`)

Aspiration windows at the root, behind a **`UseAspiration`** toggle (default
on). Instead of searching each iterative-deepening pass with the full
`[-INF, +INF]` window, we open a **narrow window around the previous
iteration's score** (`prev ± 30`). The previous score/depth are remembered in
`g_prev_score` / `g_prev_depth` across the UBGI deepening loop.

If the true score lands **outside** the window we detect the fail-low / fail-high
and **re-search with a widened window** — so it is always correctness-safe; the
only downside of a bad guess is a little wasted time. Applied for `depth >= 4`
(once scores are stable) and skipped for mate-range scores.

## Why it helps (for the demo)

A search runs faster the narrower its `[alpha, beta]` window, because more
branches fall outside it and get cut. The score rarely jumps much between one
depth and the next, so betting that depth N's score is close to depth N−1's is
usually right — and when it's wrong we just re-search. Net: a bit deeper in the
same time.

## Effect

startpos, `go movetime 3000`:

| | depth | move |
|---|---|---|
| aspiration off | 14 | b2b3 |
| **aspiration on** | **15** | c2c3 |

+1 ply (the move differs only because the opening is near-equal at this depth).
Fixed depth 8 still returns a sane move (`d2d3`).

## Note: check extensions (considered, deferred)

The plan paired aspiration with *check extensions*. In this king-capture model,
detecting "in check" at every node requires a full opponent move-generation
(`create_null_state`), which costs more than it returns here — the king-capture
terminal handling plus quiescence already resolve the tactics that check
extensions target. Deferred as not worth the per-node cost.

## Strength vs boss

- vs `boss-ubgi`, 2 s/move, 4 games: **+4 −0 =0 (4.0/4)**, both colors — sweep
  held, no regression. Aspiration kept on.
