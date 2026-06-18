# Day 2 (2026-06-18) — Boss analysis & course-correction

## TL;DR
- The **real grading condition is 2 s/move with NO depth limit** (TA confirmed).
  Fixed-depth tests are *not* representative.
- The boss is a full-stack engine with an **NNUE** evaluation. Its NNUE file
  (`models/nnue.bin`) is **missing locally**, so every local test runs a
  **weakened (NNUE-off) boss**.
- The **GUI** (persistent engine process → warm transposition table) **draws**
  the boss; the **CLI** (fresh process every move → cold TT) "wins". The CLI
  flattered us; **the GUI is the honest preview** → we are roughly **equal** to
  the (weakened) boss, i.e. draw.
- Reverted the runtime-param refactor for cleaner code; out of time for tuning
  experiments.

## What we learned (in detail)
1. **Real condition:** 2 s/move, no depth cap; an illegal/late move = instant
   loss. Never benchmark with fixed `--depth` vs the boss — equal depth → draw.
2. **GUI quirk:** if you set a **Depth** in the GUI it runs depth-mode with **no
   time cap** (the engine grinds for minutes; the boss's slow eval makes it
   worse). **Depth = 0** in the GUI uses the 2 s time limit — that's the real
   condition.
3. **Boss internals** (from its Params panel): `UseNNUE` + `NNUEFile
   models/nnue.bin`, plus TT / quiescence / killers / null-move / LMR — the same
   search stack we have. It does **not** have aspiration windows (we do).
4. **NNUE missing:** `models/nnue.bin` does not exist in the project, so the boss
   prints `NNUE failed to load ... (UseNNUE disabled)` and runs with a weak
   fallback eval everywhere locally. The **real grading boss will have NNUE and
   be stronger** than anything we can measure here.
5. **GUI vs CLI difference = the harness, not the engine.** GUI keeps one
   persistent process per side (warm TT across moves → both engines search
   deeper → draw). The CLI spawns a fresh process per move (cold TT → our raw
   depth edge wins). CPU contention was ruled out (CPU at 6 %).
6. **The four baselines** (Weak MiniMax, Strong MiniMax, AlphaBeta, PVS) are
   **TA-held opponent engines**, not our files. They are weaker than the boss,
   so **beating the boss implies beating all four baselines**. The baseline
   "AlphaBeta"/"PVS" are *not* our `alphabeta`/`pvs` policies — same algorithm
   names, different programs (theirs = opponents, ours = our submission).

## Current standing
- vs `minimax-weak` / `minimax-strong` (local): **win** (verified earlier).
- vs the boss as the **GUI** runs it (warm, NNUE-off): **≈ draw**.
- Demo target: open the GUI, beat the boss.

## Decision this session
- Reverted the runtime-tunable-knobs refactor (commit `f9f38d0`) back to clean
  **hardcoded constants** — easier to read and explain at the demo, and we are
  out of time for tuning experiments. **Engine strength is unchanged** (the
  param defaults were identical to the constants).

## If we later pursue the win (optional, needs validation)
To beat the warm/persistent boss we'd need a genuine edge, e.g. a **contempt
factor** (stop accepting dead-draw shuffles) and/or **extra search depth**
(late-move / futility pruning). Each would have to be validated, which costs
experiment time we may not have.
