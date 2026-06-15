# Checkpoint 3 — Unit tests ✅

**Status:** PASS (12 passed, 0 failed)

## Command

```bash
mingw32-make test
./unittest/build/state_test.exe
```

## Output (summary)

```
=== Static position tests ===
PASS initial_white / initial_black
PASS midgame_white / midgame_black
PASS king_capture_win
PASS endgame_white / endgame_black
PASS pawn_promote_white
PASS bishop_open
PASS queen_center
PASS knight_corner

=== Game playthrough test (50 steps) ===
PASS game playthrough (18 steps, all matched)

=== Benchmark (100000 iterations) ===
  Naive:    11968 us
  Bitboard:  7877 us
  Speedup:   1.52x

=== Results: 12 passed, 0 failed ===
```

## Confirms

- The **naive vs bitboard** differential move-generation check passes on every
  position and across a full playthrough — i.e. TODO group 2 (naive knight move
  table + knight case) is correct: the two generators produce identical move
  lists.
- Bitboard generator is ~1.5× faster than the naive one, as expected.
