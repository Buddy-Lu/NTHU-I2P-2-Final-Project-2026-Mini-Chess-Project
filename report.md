# Mini Project 2 — MiniChess AI

**Student ID:** 114062317
**Date:** June 2026

> This report explains the design of my MiniChess engine: the state value
> function and the search algorithms (Minimax → Alpha-Beta → PVS) together with
> the optimizations layered on top (move ordering, quiescence, transposition
> table, null-move pruning, late move reductions, aspiration windows). It is my
> reference for the demo.

---

## 1. Overview

The engine is a game-agnostic search core talking to a MiniChess `State`
(6×5 board, king-capture win, pawns promote to Queen). My submission policy
(`submission.cpp`, a copy of my `pvs` policy) implements the search; the state
value function lives in `state.cpp::evaluate`.

The decision pipeline is the classic one:

1. **Iterative deepening** searches depth 1, 2, 3, … until the 10 s budget runs
   out, always keeping the best move from the last completed depth.
2. At each depth, **PVS** (an Alpha-Beta refinement) explores the game tree.
3. At the leaves, **quiescence** resolves pending captures, then the
   **evaluation** scores the position.

All code uses only the C++ standard library — no third-party libraries, no
inline assembly, no multithreading, no SIMD/AVX in the policy.

---

## 2. State value function (`evaluate`)

The score is returned **from the side-to-move's perspective** (positive = good
for the player to move). A won position returns `P_MAX`. Otherwise:

```
score = (my material + my positional terms) − (opponent's) + mobility + tempo
```

- **Material** — piece values on a fine (×10) scale: P=20, R=60, N=70, B=80,
  Q=200, K=1000. Capturing the king ends the game, so the king value is only a
  fallback.
- **Piece-Square Tables (PST)** — every (piece, square) has a positional bonus
  (knights/bishops like the centre; pawns are rewarded for advancing; the king
  is encouraged to stay back early). Tables are mirrored for the two colors.
- **King tropism** — pieces near the enemy king get a bonus (weighted by piece
  type), encouraging attacks on the king.
- **Mobility** — `2 × (my legal moves − opponent legal moves)`: more options is
  usually better.
- **Passed pawns** *(my addition)* — a pawn with no enemy pawn able to block or
  capture it ahead on its own/adjacent files gets a bonus that grows as it nears
  promotion (`{0,70,45,28,16,8}` by rows-to-go). Because promotion yields a
  Queen, passed pawns are decisive on a small board.
- **Tempo** *(my addition)* — a small bonus (+6) for the side to move, favoring
  active play.

---

## 3. Search

### 3.1 Minimax (negamax) — the baseline

Minimax assumes both sides play optimally: I maximize my score, the opponent
minimizes it. Using the **negamax** identity (`score = −opponent's score`), one
routine handles both sides: the child is evaluated and its sign flipped. This is
my correctness oracle — every faster algorithm below must return the **same
score** at the same depth. Winning terminals are scored `P_MAX − ply` so the
engine prefers *faster* wins.

### 3.2 Alpha-Beta pruning

Alpha-Beta keeps a window `[α, β]`: α is the best score I'm already guaranteed,
β the best the opponent is guaranteed. When a move makes `α ≥ β`, the opponent
would never allow this line, so the remaining sibling moves are **pruned**. This
returns the *exact same result as minimax* but visits far fewer nodes.

> Measured (start position, depth 6): minimax 621,470 nodes → alpha-beta 26,392
> nodes — same move, same score, ~23× fewer nodes.

### 3.3 Move ordering

Pruning only works well if good moves are tried first. My ordering, best to
worst:

1. **Transposition-table move** (see 3.6) — the best move found last time.
2. **Captures**, by **MVV-LVA** (Most-Valuable-Victim − Least-Valuable-Attacker).
3. **Killer moves** — the last two *quiet* moves that caused a cutoff at this
   ply (they often refute sibling moves too).
4. **History heuristic** — remaining quiet moves ranked by a `[side][from][to]`
   table accumulated (`+= depth²`) whenever a quiet move causes a cutoff.

Plus a bonus for pawn promotions.

### 3.4 Quiescence search

A fixed-depth search can stop in the middle of a trade (the "horizon effect")
and misjudge the position. At the leaf, quiescence keeps searching **captures
only** until the position is quiet. A **stand-pat** score (the static eval) lets
the side to move decline to capture, bounding the search so we never force a bad
capture.

> Measured (depth 6): quiescence raises the selective depth from 6 to ~18.

### 3.5 PVS (Principal Variation Search)

After good ordering, the first move is almost always best. PVS searches it with
the full window, then probes every later move with a **null window**
`[α, α+1]` — a cheap "can this beat α?" test. Only if a probe unexpectedly
succeeds do we re-search that move with the full window. Same result as
Alpha-Beta, fewer nodes.

### 3.6 Transposition table

Different move orders often reach the same position. A **Zobrist hash** keys a
table (2²² entries) storing each position's score, bound type (exact / lower /
upper), depth, and best move. On revisiting a position searched at least as
deep, the stored bound can cut off immediately; otherwise its best move is tried
first. Mate scores are stored ply-relative so a cached mate stays correct.
Replacement is depth-preferred, and the full 64-bit key is checked to reject
collisions.

> Measured (depth 6): alpha-beta 26,392 → with TT 9,954 nodes; and in a 3 s
> budget the search reaches depth 11 instead of 9.

### 3.7 Null-move pruning

If I let the opponent move twice (I "pass") and I'm *still* winning above β, the
real position is almost certainly a cutoff — so I prune it after a shallow
(reduced by R=2) verification search. Guards prevent the known failure modes:
not when in check, not in pawn-only endgames (zugzwang), not on mate-range
windows, and never two null moves in a row.

### 3.8 Late Move Reductions (LMR)

Late, quiet moves (after the first few) are unlikely to be best, so they're
searched at **reduced depth** first; only if such a move beats α is it
re-searched at full depth. This concentrates effort on the promising lines.

> Measured (3 s budget): LMR took the reachable depth from 12 to 14.

### 3.9 Aspiration windows

Each iterative-deepening pass is searched in a **narrow window** around the
previous depth's score (`prev ± 30`). A tighter window prunes more; if the true
score falls outside, we detect the fail and re-search wider — always
correctness-safe, only ever a time cost.

> Measured (3 s budget): depth 14 → 15.

### 3.10 Iterative deepening & time control

The protocol layer drives depths 1, 2, 3, …, emitting the best move after each
completed depth and stopping when ~half the move-time is used (so the next,
~2× costlier, depth won't overrun). The transposition table and the previous
iteration's PV make each deeper pass start from the best-known move. A search is
abortable, so we never exceed the 10 s limit and never return an illegal move.

---

## 4. Results

Cumulative depth reached from the start position in a 3 s budget, as each
optimization was added:

| Configuration | Depth reached |
|---|---|
| Alpha-Beta + quiescence | 9 |
| + transposition table | 11 |
| + null-move | 12 |
| + LMR | 14 |
| + aspiration | 15 |

Against the provided opponents at 2 s/move, 4 games (both colors):

| Opponent | Result (my engine) |
|---|---|
| `minimax-weak` | beats (4–0 at depth 6) |
| `minimax-strong` | beats (3–1 at 2 s; 2–0 at depth 7) |
| `boss-ubgi` | **sweeps 4–0, both colors** |

*(Self-play tuning results to be added after the tuning phase.)*

---

## 5. Constraints compliance

- **Standard library only** — no third-party code, inline asm, threads, or AVX
  in the policy.
- **≤ 10 s/move** via iterative deepening with an abortable search.
- **≤ 4 GB** — the transposition table is ~80 MB.
- **Never an illegal move** — moves come from the verified generator; `0000`
  only when there is genuinely no legal move.

---

## 6. Build & run

```bash
mingw32-make all                 # engine + benchmark + unit tests
./build/minichess-ubgi.exe       # UBGI engine on stdin/stdout
```

The submission policy (`submission.cpp`) is self-contained: it compiles with
`state.cpp`/`state.hpp` alone and exposes the standard
`search(State*, depth, GameHistory&, SearchContext&)` interface used by the
benchmark.
