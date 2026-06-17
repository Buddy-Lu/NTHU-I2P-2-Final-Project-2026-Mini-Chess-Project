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

> Method (locked in): experiments build a **labelled variant binary** with
> `tools/build_variant.sh` (canonical source/binary auto-restored) and compare it
> to the reference with `tools/selfplay.sh`. Runtime-toggle experiments
> (`UseNullMove`, `UseLMR`, …) need no rebuild — same binary, `--white-param`.
> Keep a change only at **≥55% over ≥40 games** (and it must still sweep boss 4–0).
