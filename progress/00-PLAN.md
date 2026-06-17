# Mini Project 2 — MiniChess AI: Implementation Plan (revised)

**Date:** 2026-06-15 · **Report due:** 6/20 · **Demo:** 6/21
**Goal (from `spec/Mini-Project-2-introduction.pptx`):** Build a *strong*
MiniChess AI that beats the TA baselines, implementing Minimax → Alpha-Beta →
PVS → Quiescence on top of a good state value function. Submit the strongest
policy as `submission.cpp`, plus a report and (bonus) a git history.

> **Correction:** the first plan in this repo treated the task as "make the
> scaffold compile + pass the unit test." That is only the *hackathon TODO*
> portion. The real assignment is the AI and beating baselines. This file
> supersedes that.

## Grading targets (15 pts + bonus)

| # | Deliverable | Pts | Status |
|---|-------------|-----|--------|
| A | State value function | +1 | ✅ exists (will tune) |
| B | Minimax | +2 | ✅ exists |
| C | Alpha-Beta pruning | +1 | ⬜ |
| D | PVS | +2 | ⬜ |
| E | Quiescence | +1 | ⬜ |
| F | Beat 4 baselines, both colors (1.5 ea, +2 all) | +8 | ⬜ |
| — | Hackathon TODOs | incl. | ✅ |
| G | Report `<id>_project2.pdf` | req. | ⬜ |
| H | Bonus: git ≥3 commits + GitHub | +1 | ⬜ |
| I | Bonus: ranking / visible boss / secret boss | +5 | ⬜ |

## Constraints to respect in the submitted policy

- Std library only; **no** 3rd-party libs, inline ASM, threads, or AVX/SIMD.
- ≤10 s per move (use iterative deepening + a time check), ≤4 GB.
- Never emit an illegal move (instant loss). `0000` only when no legal move.
- Submission = one policy file copied to `src/policy/submission.cpp`; must
  compile on the TA's Linux `make`.

## Checkpoints (each tested 100% before the next; each gets a doc)

| # | Checkpoint | Test / success criterion | Doc |
|---|-----------|--------------------------|-----|
| 0 | Baseline harness | CLI can play my engine vs each baseline exe, both colors, and report W/D/L | `10-baseline-harness.md` |
| 1 | Alpha-Beta policy | New `alphabeta` policy; same move as minimax at equal depth but far fewer nodes; beats `minimax-weak` | `11-alphabeta.md` |
| 2 | Move ordering | MVV-LVA + PV move first; measurable node reduction vs plain AB | `12-move-ordering.md` |
| 3 | Iterative deepening + time control | Respects 10 s/move; returns best-so-far on timeout; never illegal | `13-time-control.md` |
| 4 | Quiescence | Captures-only extension at leaves; reduces tactical blunders; beats `minimax-strong` | `14-quiescence.md` |
| 5 | PVS | Null-window re-search; same result as AB, fewer nodes | `15-pvs.md` |
| 6 | Eval tuning | Tune material/PST/mobility/tropism; A/B match vs previous version | `16-eval-tuning.md` |
| 7 | Beat all baselines | ≥1W+1D vs each of weak/strong/AB/PVS, both colors; ideally 8–0 | `17-baseline-results.md` |
| 8 | Submission packaging | Copy strongest policy → `submission.cpp`; clean `make` on a Linux-like build | `18-submission.md` |
| 9 | Report + git | Draft `report.md`→PDF; `git init` + ≥3 commits + push | `19-report-git.md` |
| 10 | (Bonus) Boss | Try to beat `boss-ubgi.exe` | `20-boss.md` |

## Actual progress vs this plan (updated 2026-06-17)

Plan CPs 0–5 are done. Then, on an explicit "go max / make all the algorithms
come in" instruction, **5 extra search-strength checkpoints were inserted**
between CP5 and CP6 (these are beyond the original list). Doc filenames drifted
(11, 14–19; 10/12/13 folded into other steps) — cosmetic only.

| Plan CP | Item | Status | Notes |
|---|---|---|---|
| 0 | Baseline harness | ✅ done | proven by every CLI match (no standalone doc) |
| 1 | Alpha-Beta | ✅ done | `11-alphabeta.md` |
| 2 | Move ordering | ✅ done | MVV-LVA in AB; later upgraded with killers+history |
| 3 | Iterative deepening + time control | ✅ done | lives in `ubgi.cpp` (framework), verified |
| 4 | Quiescence | ✅ done | `14-quiescence.md` |
| 5 | PVS | ✅ done | `15-pvs.md` |
| +extra | Transposition table | ✅ done | `16-transposition-table.md` — boss swept 4–0 |
| +extra | Killer moves + history | ✅ done | `17-killers-history.md` |
| +extra | Null-move pruning | ✅ done | `18-null-move.md` |
| +extra | Late move reductions | ✅ done | `19-lmr.md` |
| +extra | Aspiration windows | ✅ done | `20-aspiration.md` |
| 6 | Eval tuning | ✅ done | `21-eval-tuning.md` (passed pawns + tempo) |
| 7 | Beat all baselines | 🟡 partial | weak ✅ strong ✅ boss ✅; AB/PVS baselines are TA-held (no local exe) |
| +extra | **Self-play tuning** | 🔄 in progress | `22-selfplay-plan.md` + `selfplay-journal.md` — push past boss for ranking |
| 8 | Submission packaging | ⬜ pending | self-contained `114062317_submission.{cpp,hpp}` |
| 9 | Report + git | 🟡 | git ✅ (11 commits → bonus secured); report drafting (`report.md` + `.tex`) |
| 10 | (Bonus) Boss | ✅ | sweeping 4–0 @ 2 s, won @ 10 s as White |

> Terminology note: **benchmark** (`build/minichess-benchmark`, from
> `src/benchmark.cpp`) is a *speed-timing tool for our own search* — NOT an
> opponent. **Baselines** are the opponent engines (weak / strong / AB / PVS)
> and the **boss** is the bonus opponent.

## Method notes

- Build each algorithm as a **separate registered policy** (`alphabeta`,
  `pvs`, …) so I can pit versions against each other and against baselines via
  the CLI (`python -m cli.cli --white <engine> --black <engine> --white-algo X
  --black-algo Y --games N`). Regression-test every change this way.
- Keep `minimax` untouched as the correctness oracle: AB/PVS must return the
  **same score and move** as minimax at the same depth on test positions.
- The strongest finished policy gets copied verbatim to `submission.cpp`.

## Known caveats carried over

- Run the CLI as a module: `python -m cli.cli ...` (not `python cli/cli.py`).
- Windows artifacts have a `.exe` suffix.
