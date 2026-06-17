# Self-play tuning tools

Isolated helper scripts for measuring strength changes. They live here so the
**canonical engine source is never mutated by an experiment** — variants are
built into their own labelled binaries and the working tree is restored.

Run everything from the **project root** in **Git Bash**.

## Two kinds of experiment

### 1. Runtime params (no rebuild) — preferred
The PVS policy exposes its knobs as runtime params, so **most tuning needs no
rebuild and no source edit** — same binary, the candidate just gets the param:

On/off toggles: `UseTT`, `UseNullMove`, `UseLMR`, `UseAspiration`,
`UseQuiescence`, `OrderMoves`, `UseKPEval`, `UseEvalMobility`.

Numeric knobs (defaults reproduce the baked-in engine exactly):
`Tempo` (6), `PassedPawnScale` (100 = ×1, in %), `NullR` (2),
`LMRMinMove` (3), `LMRMinDepth` (3), `AspDelta` (30).

```bash
# does a higher tempo help?  (candidate Tempo=12 vs reference Tempo=6)
bash tools/selfplay.sh build/minichess-ubgi.exe build/minichess-ubgi.exe 40 7 4 7 "Tempo=12"

# stronger passed-pawn weighting?
bash tools/selfplay.sh build/minichess-ubgi.exe build/minichess-ubgi.exe 40 7 4 7 "PassedPawnScale=150"

# deeper null-move reduction?
bash tools/selfplay.sh build/minichess-ubgi.exe build/minichess-ubgi.exe 40 7 4 7 "NullR=3"
```
(For the candidate, score **> 55%** = the change helps; **< 45%** = it hurts;
in between = noise.)

### 2. Source change not exposed as a param (needs a rebuild)
Only for things that aren't runtime params (e.g. PST tables, a new eval term).
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
