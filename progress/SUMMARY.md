# Mini Project 2 — MiniChess AI: Project Summary

**Updated:** 2026-06-17 · **Report/upload due:** 6/20 23:59 · **Demo:** 6/21
**Student:** 114062317

## Where the project stands

A strong MiniChess (6×5) engine is built and fully committed/pushed to GitHub
(`main`, 11 commits). The strongest policy is **`pvs`**, which carries the full
modern search stack and **sweeps the local boss `boss-ubgi` 4–0 with both
colors** at 2 s/move, reaching ~depth 15 in 3 s.

## What was built (each its own commit + `progress/` doc)

| Layer | What | Doc |
|---|---|---|
| Eval | material + PST + king tropism + mobility, **+ passed pawns + tempo** | `21-eval-tuning.md` |
| Search base | Minimax (oracle), Alpha-Beta + MVV-LVA | `11-alphabeta.md` |
| Quiescence | captures-only leaf search (horizon effect) | `14-quiescence.md` |
| PVS | null-window scout + re-search | `15-pvs.md` |
| Transposition table | Zobrist-keyed, depth-preferred, TT-move ordering | `16-transposition-table.md` |
| Move ordering | killers + history heuristic | `17-killers-history.md` |
| Pruning | null-move pruning (guarded) | `18-null-move.md` |
| Reductions | late move reductions (LMR) | `19-lmr.md` |
| Root | aspiration windows | `20-aspiration.md` |

Every feature has a `UseX` toggle (default on) for A/B testing. Correctness
oracle: AB/PVS return the same score as minimax at equal depth; unit test
(`state_test`) stays **12/12**.

## Rubric status (15 pts + bonus)

| Item | Pts | Status |
|---|---|---|
| State value function | +1 | ✅ |
| Minimax | +2 | ✅ |
| Alpha-Beta | +1 | ✅ |
| Quiescence | +1 | ✅ |
| PVS | +2 | ✅ |
| Beat baselines (weak/strong/AB/PVS, both colors) | +8 | 🟢 weak/strong/boss beaten locally; AB/PVS baselines TA-held (untestable here) |
| git ≥3 commits + GitHub | +1 bonus | ✅ (11 commits) |
| Visible / secret boss, class ranking | +bonus | 🟢 boss swept; ranking via self-play tuning (in progress) |

## Remaining work

1. **Self-play tuning** (in progress) — randomized paired-opening matches to
   push past boss level for ranking; see `22-selfplay-plan.md` +
   `selfplay-journal.md`.
2. **Submission packaging** — self-contained `114062317_submission.{cpp,hpp}`
   (= the PVS engine) + `114062317_state.{cpp,hpp}` + report.
3. **Report** — `114062317_report.pdf` (drafting `report.md` + `report.tex`).

## How to build & run (quick reference)

```bash
mingw32-make all                       # engine + benchmark + tests
./build/minichess-ubgi.exe             # UBGI engine (default algo: minimax)
./unittest/build/state_test.exe        # 12/12

# strongest engine vs boss, both colors, 2s/move:
.venv/Scripts/python.exe -m cli.cli \
    --white build/minichess-ubgi.exe --black build/boss-ubgi.exe \
    --white-algo pvs --black-algo minimax --games 4 --time 2000 --quiet
```
