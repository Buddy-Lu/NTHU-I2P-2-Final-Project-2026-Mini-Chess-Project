# Checkpoint — Late Move Reductions (LMR) ✅

**Status:** PASS (deeper search; boss regression-checked)

## What was added (in `pvs.cpp`)

Late Move Reductions, behind a **`UseLMR`** toggle (default on). After the first
move and the early (well-ordered) moves, **late + quiet** moves are searched at
**reduced depth** with a null window. The reduction is undone (full-depth
re-search) only if the reduced search beats alpha.

- Applies when: `move_index >= 3`, `depth >= 3`, and the move is quiet
  (not a capture, not a promotion).
- Reduction: 1 ply, or 2 for very late moves at higher depth
  (`move_index >= 6 && depth >= 6`).
- Re-search ladder: reduced null-window → (if it beat alpha) full-depth
  null-window → (if inside the window) full-depth full-window PVS re-search.

## Why it helps (for the demo)

With strong move ordering, the best move is almost always near the front. The
many moves searched later are very unlikely to be best, so spending full depth
on each is wasteful. LMR searches them shallowly first and only "pays full
price" for the rare late move that turns out to be good — letting the engine
spend its time going deeper on the lines that matter.

## Effect

startpos, `go movetime 3000`:

| | depth | nodes | move |
|---|---|---|---|
| LMR off | 12 | 4.75M | b2b3 |
| **LMR on** | **14** | 3.53M | b2b3 |

**+2 plies and fewer nodes** — the biggest single depth jump so far. Unit test
still **12/12**.

## Strength vs boss

- vs `boss-ubgi`, 2 s/move, 4 games: **+4 −0 =0 (4.0/4)**, both colors — sweep
  held, no regression. LMR kept on.
