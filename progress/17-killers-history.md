# Checkpoint — Killer moves + History heuristic ✅

**Status:** PASS (correctness verified; efficiency improved; no boss regression)

## What was added (in `pvs.cpp`)

A unified move-ordering routine that replaces the capture-only MVV-LVA sort,
scoring every move by:

1. **TT / hash move** (from the transposition table) — highest.
2. **Captures** — MVV-LVA.
3. **Killer moves** — the last two *quiet* moves that caused a beta cutoff at
   this ply (`g_killers[ply][2]`).
4. **History** — quiet moves ranked by `g_history[side][from][to]`, accumulated
   `+= depth^2` each time a quiet move causes a cutoff.

Plus a promotion bonus. Heuristic tables are cleared at the start of each root
search.

## Why it helps (for the demo)

MVV-LVA only orders *captures*, but most chess moves are quiet. Killers and
history give quiet moves a sensible order too, so good quiet moves are tried
earlier → earlier beta cutoffs → fewer nodes → deeper search in the same time.

- **Killer**: a quiet refutation that worked at one node usually refutes its
  siblings (same ply) as well.
- **History**: a global memory of which quiet moves have been refuting cutoffs
  all over the tree.

## Correctness (score-preserving)

depth 6, startpos, quiescence off: **cp −19 / b2b3** — unchanged from
minimax/PVS. Pure reordering.

## Efficiency gain

| Stage | depth-6 nodes (q off) | depth-11 search nodes (q on) |
|---|---|---|
| PVS + TT | 9,954 | 6.41M |
| **+ killers + history** | **7,979** | **3.70M** |

~45% fewer nodes to reach depth 11 — i.e. more time left to go deeper.

## Strength vs boss

- vs `boss-ubgi`, 2 s/move, 4 games: **+4 −0 =0 (4.0/4)**, both colors — boss
  still swept, no regression. The gain is efficiency (deeper search), which pays
  off most at the 10 s limit and against the unseen AB/PVS baselines.
