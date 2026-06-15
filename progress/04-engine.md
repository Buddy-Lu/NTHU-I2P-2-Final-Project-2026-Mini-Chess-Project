# Checkpoint 4 — Engine UBGI protocol ✅

**Status:** PASS

## Command

```bash
{ printf 'ubgi\nposition startpos\ngo depth 6\n'; sleep 4; printf 'quit\n'; } \
  | ./build/minichess-ubgi.exe
```

(The search runs on a background thread, so `quit` must be delayed until after
`bestmove`, otherwise the process exits mid-search.)

## Observed

- Handshake replies with `id`, all `option` lines, and `ubgiok`; `isready` →
  `readyok`.
- Iterative deepening emits `info depth 1..6` lines with `seldepth`, `score cp`,
  `nodes`, `time`, `nps`, `currmove`, and `pv`.
- Depth-6 search from the start position:

```
info depth 6 seldepth 6 score cp -19 nodes 621470 time 300 nps 2077569 pv b2b3
bestmove b2b3
```

## Confirms

- TODO group 3 (`eval_ctx` recursion / negamax) and group 4 (root `search`)
  produce a valid principal variation and best move.
- TODO group 1 (`evaluate()` — material + PST + tropism + mobility) yields
  sensible centipawn scores.
- ~2.0 Mnps — performance is healthy.
