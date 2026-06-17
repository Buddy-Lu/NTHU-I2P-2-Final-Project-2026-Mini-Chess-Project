# Self-Play Tuning — Plan

**Goal:** Push the engine *past* boss level (for class ranking) by measuring and
keeping only changes that genuinely make it stronger. We can't use external
engines (no 6×5 variant exists + plagiarism rule), so **our own committed
engine is the sparring partner**.

## The measurement problem (why a plain match won't do)

Self-play from the start position is dominated by White's first-move advantage
(we observed White winning every game), so it can't detect small improvements.
**Fix: randomized, *paired* openings.**

## Harness design

Extend `cli/cli.py` with:
- `--open-random N` — before handing the game to the engines, play `N` random
  *legal* plies from the start (applied to both sides equally).
- `--seed S` — deterministic openings, so a run is reproducible.
- **Paired openings**: each random opening is played **twice** — candidate as
  White, then candidate as Black — so color/opening bias cancels out. A net
  score > 50% then means the candidate is really stronger.

Reference engine = the current committed build. Candidate = a variant (one eval
or search-param change at a time, toggled via the existing `UseX` / value
params, so **no rebuild needed** to A/B — both sides are the same binary with
different `--white-param`/`--black-param`).

## Protocol per experiment

1. Pick **one** change (e.g. tempo value, passed-pawn weights, LMR threshold,
   null-move R, a new eval term).
2. Run candidate vs reference: **≥ 20 paired games (40 total)**, random openings
   (e.g. `--open-random 4`), fixed time (start at **1 s/move** for speed; confirm
   winners at 2 s).
3. **Decision rule:** keep the change only if candidate scores **≥ 55%**
   (clearly positive, not noise). Otherwise revert. Borderline (50–55%) → re-run
   with more games or discard.
4. **Log every run** in `selfplay-journal.md` (date, change, settings, W/D/L,
   score %, keep/revert). One row per experiment — no exceptions.
5. Sanity gate: any kept change must still **sweep boss 4–0** and pass the unit
   test before committing.

## Candidate changes to try (in priority order)

1. **Tempo value** (currently 6) — sweep {0, 4, 8, 12}.
2. **Passed-pawn weights** — scale the table up/down.
3. **Null-move R** (2) vs adaptive (2–3 by depth).
4. **LMR aggressiveness** (reduction formula / thresholds).
5. **Aspiration window width** (30) — {16, 30, 50}.
6. New eval terms if warranted (king-shelter, rook-on-open-file, pawn structure).

## Stop condition

Stop when changes stop yielding ≥ 55% (diminishing returns) or we run low on
time before 6/20. Then lock the best config into `submission.cpp` and finalize
the report.

## Commit discipline

Every *kept* change → its own commit + push (same as the checkpoint sprint).
Reverted experiments are logged in the journal but not committed.
