# Checkpoint — Transposition Table ✅

**Status:** PASS (correctness verified; strength measured vs boss)

## What was added (in `pvs.cpp`)

- A Zobrist-keyed **transposition table** (2^22 entries, ~80 MB ≪ 4 GB limit):
  caches each searched position's score, bound type, depth, and **best move**.
- **TT-move ordering**: the stored best move is tried first (the single
  strongest ordering signal), promoted ahead of the MVV-LVA order.
- **Cutoffs**: an entry searched to ≥ the current depth returns immediately when
  its bound (EXACT / LOWER / UPPER) resolves the `[alpha, beta]` window.
- Toggle param **`UseTT`** (default on). Depth-preferred replacement; full
  64-bit key stored so collisions are detected, not trusted. Mate scores are
  stored ply-relative so a cached mate stays correct when probed elsewhere.

## How it works (for the demo)

Different move orders often reach the **same** position (a "transposition").
Without a TT we re-search it every time. The TT remembers the result keyed by
the Zobrist hash, so the second time we either return the cached score (if it
was searched deep enough) or at least try its best move first. Across iterative
deepening, the previous iteration's best move is in the TT — so each deeper pass
starts from the best-known move and prunes far more.

## Correctness (score-preserving)

depth 6, startpos, quiescence off — identical result, fewer nodes:

| | score | move | nodes |
|---|---|---|---|
| minimax | −19 | b2b3 | 621,470 |
| pvs (no TT) | −19 | b2b3 | 24,304 |
| **pvs + TT** | −19 | b2b3 | **9,954** |

Unit test: **12/12**.

## Payoff: depth in the same time

startpos, `go movetime 3000`, quiescence on:

| | depth | seldepth | score |
|---|---|---|---|
| pvs (no TT) | 9 | 26 | cp 5 |
| **pvs + TT** | **11** | 26 | cp 5 |

**+2 plies** for the same time and the same evaluation — pure efficiency.

## Strength vs boss

- `pvs + TT` vs `boss-ubgi`, 2 s/move, 4 games: **+4 −0 =0 (4.0/4, 100%)** —
  **2 wins with each color**, a clean sweep (up from alpha-beta+quiescence's
  3–1). The boss is beaten with both colors at 2 s/move.
