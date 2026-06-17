# Self-Play Tuning — Journal

Every experiment gets one row. Reference = current committed best engine.
Candidate = same binary with one changed param (via `--*-param`), unless noted.
"Score" is the candidate's points / total from its perspective (W=1, D=0.5).
Keep rule: ≥ 55% over ≥ 40 paired games. Sanity: kept changes must still sweep
boss 4–0 + pass unit test.

| # | Date | Change (candidate vs reference) | Settings (games / time / open) | W–L–D | Score % | Decision |
|---|------|----------------------------------|--------------------------------|-------|---------|----------|
| — | 2026-06-17 | (harness/baseline setup — no change yet) | — | — | — | pending |
