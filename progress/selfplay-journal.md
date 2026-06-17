# Self-Play Tuning — Journal

Every experiment gets one row. Reference = current committed best engine.
Candidate = same binary with one changed param (via `--*-param`), unless noted.
"Score" is the candidate's points / total from its perspective (W=1, D=0.5).
Keep rule: ≥ 55% over ≥ 40 paired games. Sanity: kept changes must still sweep
boss 4–0 + pass unit test.

| # | Date | Change (candidate vs reference) | Settings (games / time / open) | W–L–D | Score % | Decision |
|---|------|----------------------------------|--------------------------------|-------|---------|----------|
| — | 2026-06-17 | (harness/baseline setup — no change yet) | — | — | — | done |
| 1 | 2026-06-17 | tempo **12** vs 6 (ref) | 30 / depth 7 / open 4 / seed 7 | 15–12–3 | 55.0% | **inconclusive** — within noise at n=30 (SE≈9%); reverted, canonical stays tempo=6. Experiments now run via isolated variant binaries (`tools/`). |

## Planned experiment matrix (fill in results)

Command: `bash tools/selfplay.sh build/minichess-ubgi.exe build/minichess-ubgi.exe 40 7 4 7 "<PARAM>"`
Keep: candidate >55% over ≥40 games (confirm borderline with 60+ / new seed).

| # | `<PARAM>` | Tests | Score % | Decision |
|---|-----------|-------|---------|----------|
| 1 | `Tempo=0` | tempo off | | |
| 2 | `Tempo=3` | weaker tempo | | |
| 3 | `Tempo=12` | stronger tempo | | |
| 4 | `Tempo=20` | aggressive tempo | | |
| 5 | `PassedPawnScale=50` | half passed-pawn | | |
| 6 | `PassedPawnScale=75` | slightly less | | |
| 7 | `PassedPawnScale=150` | 1.5x passed-pawn | | |
| 8 | `PassedPawnScale=200` | 2x passed-pawn | | |
| 9 | `NullR=3` | deeper null-move | | |
| 10 | `LMRMinMove=2` | LMR more aggressive | | |
| 11 | `LMRMinMove=5` | LMR safer | | |
| 12 | `LMRMinMove=6` | LMR much later | | |
| 13 | `LMRMinDepth=2` | LMR shallower | | |
| 14 | `LMRMinDepth=4` | LMR deeper only | | |
| 15 | `AspDelta=15` | tighter window | | |
| 16 | `AspDelta=50` | wider window | | |
| 17 | `AspDelta=100` | much wider window | | |
| 18 | combine winners | best-of | | |

> Method (locked in): experiments build a **labelled variant binary** with
> `tools/build_variant.sh` (canonical source/binary auto-restored) and compare it
> to the reference with `tools/selfplay.sh`. Runtime-toggle experiments
> (`UseNullMove`, `UseLMR`, …) need no rebuild — same binary, `--white-param`.
> Keep a change only at **≥55% over ≥40 games** (and it must still sweep boss 4–0).
