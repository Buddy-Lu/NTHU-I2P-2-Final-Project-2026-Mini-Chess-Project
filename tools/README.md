# Self-play tuning tools

Isolated helper scripts for measuring strength changes. They live here so the
**canonical engine source is never mutated by an experiment** — variants are
built into their own labelled binaries and the working tree is restored.

Run everything from the **project root** in **Git Bash**.

## Two kinds of experiment

### 1. Runtime toggle (no rebuild)
The PVS policy exposes these on/off params: `UseTT`, `UseNullMove`, `UseLMR`,
`UseAspiration`, `UseQuiescence`, `OrderMoves`, `UseKPEval`, `UseEvalMobility`.
A/B them directly — same binary, candidate gets the param:

```bash
bash tools/selfplay.sh build/minichess-ubgi.exe build/minichess-ubgi.exe 40 7 4 7 "UseNullMove=false"
```
(Score < 50% for the candidate means the feature helps — keep it on.)

### 2. Eval / search constant (needs a rebuild)
Build a labelled variant, then compare it to the current build:

```bash
bash tools/build_variant.sh tempo12 src/games/minichess/state.cpp 's/kp_tempo = 6/kp_tempo = 12/'
bash tools/selfplay.sh build/tempo12-ubgi.exe build/minichess-ubgi.exe 40 7 4 7
```
`build_variant.sh` restores the canonical source and binary automatically.

## Reading the result
The last line prints `Engine1 (pvs) score: X/N (P%)`. Engine1 is the candidate.

- **≥ 55% over ≥ 40 games** → real improvement, keep it (then re-verify it still
  sweeps `boss-ubgi` 4–0 before committing).
- **~50% (45–55%)** → within noise; discard. (At n=30 the standard error is ~9%,
  so 30 games can't distinguish 55% from 50% — use ≥ 40, ideally 60–100.)

## Notes
- `--depth 7` is used (not time) so results are deterministic per opening and
  isolate *evaluation* quality from machine speed. For time-sensitive search
  tuning, swap to a time control by editing the script's `--depth` to `--time`.
- Openings are seeded (`seed` arg); change it for an independent sample.
- These scripts do **not** modify any original project file.
