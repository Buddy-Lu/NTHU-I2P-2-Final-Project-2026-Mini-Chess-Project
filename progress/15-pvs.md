# Checkpoint — PVS (Principal Variation Search) ✅

**Status:** PASS · rubric **+2 points** (PVS)

## What was added

- `src/policy/pvs.hpp` / `pvs.cpp` — a new `pvs` policy: negamax + alpha-beta +
  **null-window (scout) search**. Reuses the alpha-beta MVV-LVA ordering
  (`AlphaBeta::order_moves`) and quiescence leaf (`AlphaBeta::qsearch`), so PVS
  inherits every earlier improvement.
- `AlphaBeta::order_moves` promoted to a public static so PVS can share it (no
  duplication).
- Registered `"pvs"` in `registry.hpp`; added to `ALGO_CHOICES` in `cli.py`.

## How it works (for the demo)

After good move ordering, the **first** move is almost always the best (the
"principal variation"). PVS exploits that:

1. Search the first move with the **full window** `[alpha, beta]`.
2. Search every later move with a **null window** `[alpha, alpha+1]` — this only
   asks "can this move beat alpha?", which prunes far faster than a full search.
3. If a probe *unexpectedly* beats alpha (`alpha < score < beta`), that move
   might be a new PV, so **re-search it** with the full window.

With strong ordering, almost all probes fail low (no re-search), so PVS visits
fewer nodes than plain alpha-beta for the same result — and therefore reaches a
greater depth in the same time.

## Correctness (must equal minimax at equal depth)

depth 6, startpos, quiescence off:

| Algorithm | score | move | nodes |
|---|---|---|---|
| minimax   | −19 | b2b3 | 621,470 |
| alphabeta | −19 | b2b3 | 26,392 |
| **pvs**   | −19 | b2b3 | **24,304** |

Same score & move; PVS uses the fewest nodes. Unit test (`state_test`) still
**12/12**.

## Payoff: deeper in the same time

startpos, `go movetime 2000` (quiescence on):

| Algorithm | depth reached | seldepth | nodes |
|---|---|---|---|
| alphabeta | 8 | 23 | 2.31M |
| **pvs**   | **9** | 26 | 5.03M |

PVS finishes each depth faster, so within the budget it completes a ply deeper —
a direct strength gain for the 10 s boss fight and class ranking.

## Next

- Benchmark `pvs` vs `boss-ubgi` at 10 s/move (both colors).
- Pick the strongest policy → copy to `submission.cpp`.
- Eval tuning; write the report.
