# MiniChess Engine — Study Guide (English)

A function-by-function walkthrough of everything implemented in the four core files:
`pvs.hpp`, `pvs.cpp`, `state.hpp`, `state.cpp`.

> Game: MiniChess — a 6×5 board (`BOARD_H=6`, `BOARD_W=5`), **king-capture** win (not
> checkmate). Pawns promote to Queen only. Piece codes: `1=Pawn 2=Rook 3=Knight
> 4=Bishop 5=Queen 6=King`. Player `0` = White (moves up, toward row 0),
> player `1` = Black (moves down, toward row `BOARD_H-1`).
>
> Scores use **negamax** convention: every node returns the score from the side-to-move's
> point of view, and a parent negates the child (`-eval_ctx(...)`).

---

## Part 1 — `pvs.hpp` (interface)

The header declares the `PVS` class and documents the core idea.

### The PVS idea (the comment block)
Plain alpha-beta searches every move with the full `[alpha, beta]` window. **PVS**
(Principal Variation Search) assumes the *first* move — the best one after good move
ordering — is the principal variation, and searches it with the full window. Every
**later** move is first probed with a **null window** `[alpha, alpha+1]`: a cheap yes/no
"can this move possibly beat alpha?" test that prunes much faster. Only if the probe
*surprises us* (`alpha < score < beta`) do we pay for a full-window re-search. Because good
ordering makes most probes fail low, PVS visits fewer nodes than alpha-beta at the same
depth and returns the identical result.

### `static int eval_ctx(...)`
The recursive negamax search of an interior node. Parameters:
- `state` — position to search; `depth` — plies remaining; `history` — repetition stack;
- `ply` — distance from root (for mate scoring + killer indexing);
- `ctx` — node counter / stop flag / root-update callback;
- `alpha, beta` — the search window;
- `p` — the `ABParams` bundle of feature toggles + numeric knobs;
- `null_ok` — whether null-move pruning is permitted here (set false inside a null search to forbid two passes in a row).

### `static SearchResult search(...)`
The **root** entry point. Drives one fixed-depth search and returns the best move +
score + statistics. (Iterative deepening and the 2s/move time control live one layer up
in the UBGI protocol code, which calls this repeatedly with increasing `depth`.)

### `static ParamMap default_params()` / `static std::vector<ParamDef> param_defs()`
Advertise the engine's runtime-tunable options to the UCI/UBGI layer (so they can be
A/B-tested via `setoption` without recompiling). The defaults exactly reproduce the
baked-in engine.

---

## Part 2 — `pvs.cpp` (implementation)

This file is organized as: **(A)** an anonymous-namespace block of search infrastructure
(transposition table, killers/history, helpers), then **(B)** the three public `PVS`
methods.

### A. Transposition Table (TT)

A cache, keyed by a position's Zobrist hash, that stops us re-searching positions reached
by different move orders (*transpositions*). Each entry also stores the best move found,
which is the single strongest move-ordering signal we have.

- **`enum TTFlag`** — `TT_NONE / TT_EXACT / TT_LOWER / TT_UPPER`. Tells us whether a stored
  score is exact, a lower bound (a beta cutoff happened), or an upper bound (no move beat alpha).
- **`struct TTEntry`** — one slot: full 64-bit `key` (for collision detection), `score`,
  packed `move`, `depth`, and `flag`. ~16 bytes.
- **`g_tt` / `g_tt_mask`** — the table (a `vector`) and the index mask.
- **`TT_BITS = 22`** — 2²² ≈ 4 million entries (~80 MB).
- **`MATE_ZONE`** — scores above this are mate-distance scores needing ply correction.

#### `tt_init()`
Lazily allocates the 2²² entry table on first use and sets the index mask.

#### `pack_move(m)` / `unpack_move(pm)`
Compress a `Move` (from-square, to-square) into a 16-bit int (`(from<<8)|to`) for compact
storage, and decode it back. Each square is `row*BOARD_W + col`.

#### `tt_score_to_store(score, ply)` / `tt_score_from_probe(score, ply)`
**Mate-score correction.** A "mate in N" score is relative to the node it was found at. If
we store it raw and later read it at a different ply, the distance is wrong. So on store we
make mate scores ply-independent (`+ply` for our mates, `-ply` for the opponent's), and on
probe we re-anchor them to the current ply. Non-mate scores pass through untouched.

#### `tt_probe(key, depth, ply, alpha, beta, out, tt_move)`
Look up a position. Always returns the stored best move via `tt_move` (for ordering, even
if no cutoff). Returns `true` with a usable `out` score **only** when the entry's search was
at least as deep as ours **and** its bound is usable for this window:
- `TT_EXACT` → always usable;
- `TT_LOWER` and `score >= beta` → beta cutoff;
- `TT_UPPER` and `score <= alpha` → alpha cutoff.

A shallower entry is rejected as a score but its move is still used.

#### `tt_store(key, depth, ply, score, flag, move)`
Save a result with **depth-preferred replacement**: if the existing entry is for the *same*
position and was searched *deeper*, keep it (but backfill its best move if it lacked one).
Otherwise overwrite. Different positions always replace.

### B. Move-ordering heuristics: killers + history

Captures get MVV-LVA ordering, but most chess moves are *quiet* (non-captures) where MVV-LVA
is silent. Two cheap heuristics order them:

- **`g_killers[MAX_PLY][2]`** — the last two quiet moves that caused a beta cutoff at each ply.
  Such a move often cuts off sibling nodes at the same ply.
- **`g_history[2][30][30]`** — a per `(side, from-square, to-square)` table of how often a quiet
  move caused a cutoff, weighted by `depth²`.
- **`g_prev_score` / `g_prev_depth`** — last completed iteration's root score/depth, used to seed
  the aspiration window across the iterative-deepening loop.

#### `clear_heuristics()`
Zeroes the killer and history tables at the start of each root search.

#### `is_capture(s, m)`
True if the move's destination holds an enemy piece.

#### `mvv_lva(s, m)`
**Most Valuable Victim − Least Valuable Attacker.** Returns `victim*16 − attacker` so that
capturing a big piece with a small one sorts first (e.g. pawn-takes-queen before queen-takes-pawn).

#### `order_moves(state, ply, tt_move)`
The heart of move ordering. Scores every legal move and stable-sorts descending:
1. **TT move** → `2,000,000` (try the hash move first);
2. **Captures** → `1,000,000 + mvv_lva`;
3. **First killer** → `900,000`; **second killer** → `800,000`;
4. **Quiet moves** → their history score;
5. **Pawn promotion** gets a `+500,000` bump on top (always worth trying early).

#### `has_non_pawn_material(s)`
True if the side to move owns at least one rook/knight/bishop/queen. Used to **gate
null-move pruning**, which is unsound in pawn/king-only endgames (zugzwang).

#### `update_cutoff_heuristics(state, m, ply, depth)`
After a quiet move causes a beta cutoff, record it: push it onto the killer slots for this
ply and add `depth²` to its history score. Captures are skipped (MVV-LVA already handles them).

### C. `PVS::eval_ctx(...)` — the recursive search

Walks through the body in order:

1. **Bookkeeping** — increment node count, track `seldepth`, bail out immediately if the
   `stop` flag is set (time ran out).
2. **Terminal checks** — generate legal actions if needed; return `P_MAX - ply` for a win
   (closer mates score higher), `0` for a draw.
3. **Repetition** — `check_repetition` returns a draw score if this position has occurred enough.
4. **Hash + history push** — compute the Zobrist key, push it for repetition tracking.
5. **Leaf (`depth <= 0`)** — call quiescence search (`AlphaBeta::qsearch`) if enabled, else a
   static `evaluate()`. Quiescence keeps searching captures so we don't stop in the middle of
   a trade (the *horizon effect*).
6. **TT probe** — on a usable hit, return the cached score immediately.
7. **Null-move pruning** — if allowed (`null_ok`, `depth>=3`, non-mate window, not in check,
   has non-pawn material): hand the opponent a *free move* via `create_null_state()` and search
   it shallower by `R=null_r`. If our position is still `>= beta` even after passing, the real
   position is almost certainly a cutoff → prune and return `beta`.
8. **Order moves** — call `order_moves` with the TT move.
9. **Main loop** — for each move, make the child state and recurse. Three cases:
   - **`same_player_as_parent`** (a player chains multiple moves — game-specific): window is
     *not* negated; PVS scout with `[alpha, alpha+1]`, re-search full window if it surprises.
   - **First move (`move_index==0`)** — full window `-eval_ctx(..., -beta, -alpha)` to establish the PV.
   - **Later moves** — **Late Move Reductions (LMR)** + PVS scout:
     - quiet late moves (`move_index >= lmr_min_move`, `depth >= lmr_min_depth`) are searched
       at `depth-1-reduction` (reduction 1, or 2 deep in the move list);
     - null-window scout `[-alpha-1, -alpha]`; if a *reduced* move beats alpha, re-search at full depth;
     - if it's a genuine new PV candidate (`alpha < child < beta`), full-window re-search.
10. **Update best** — track best score/move, raise alpha, and on `alpha >= beta` record the
    cutoff heuristics and break (beta cutoff).
11. **TT store** — classify the result as `UPPER` (no move beat the original alpha), `LOWER`
    (beta cutoff), or `EXACT`, and store it.
12. **Pop history**, return `best_score`.

### D. `PVS::search(...)` — the root

1. `ctx.reset()`, parse `ABParams` from the param map, and `set_eval_params(tempo, pp_scale)`
   so the eval knobs reach `State::evaluate`.
2. Probe the TT for the **hash move only** (depth 0, ignore the score) to seed root ordering;
   `clear_heuristics()`; `order_moves` at ply 0.
3. **Aspiration window** — if we have a trustworthy previous score (depth≥4, prior iteration was
   exactly one ply shallower, non-mate), search inside a narrow `[prev−delta, prev+delta]` window
   instead of `[-INF, INF]`. A tighter window prunes far more.
4. **Root loop** — same PVS structure as `eval_ctx` (full window for the first move, scout +
   re-search for the rest), calling `ctx.on_root_update(...)` so the UI shows the best line as it improves.
5. **Fail-low / fail-high handling** — if the score lands on or outside the aspiration bound,
   widen that side to `±INF` and re-search. This makes aspiration always *correctness-safe* — it
   can only ever cost time, never change the answer.
6. Save `g_prev_score/g_prev_depth`, store the EXACT root result in the TT (so the next
   iterative-deepening pass searches this move first), fill in timing/nodes, return.

### E. `PVS::default_params()` / `PVS::param_defs()`
Define the 9 boolean toggles (`UseKPEval, UseEvalMobility, ReportPartial, OrderMoves,
UseQuiescence, UseTT, UseNullMove, UseLMR, UseAspiration`) and 6 numeric knobs (`Tempo=6,
PassedPawnScale=100, NullR=2, LMRMinMove=3, LMRMinDepth=3, AspDelta=30`) with their UI ranges.
Defaults reproduce the engine exactly.

---

## Part 3 — `state.hpp` (interface)

- **`void set_eval_params(int tempo, int passed_pawn_scale_pct)`** — free function letting a
  search policy push runtime eval knobs into `evaluate()` without recompiling.
- **`class Board`** — the raw `char[2][BOARD_H][BOARD_W]` two-plane board (one plane per player),
  with the standard MiniChess starting position baked into its initializer.
- **`class State : public BaseState`** — the game state. Holds a `Board`, a cached `score`, and a
  **lazily computed Zobrist hash** (`zobrist_hash` + `zobrist_valid` flag). Declares: `evaluate`,
  `next_state`, the two move generators (`get_legal_actions_naive` / `_bitboard`) + dispatcher,
  serialization (`encode_*` / `decode_board`), `check_repetition`, `create_null_state`, and small
  inline accessors (`piece_at`, `hash`, board dimensions, `game_name`).

---

## Part 4 — `state.cpp` (implementation)

### A. Evaluation tables & helpers

- **`kp_material[7]`** — positional material values at 10× scale (`Pawn 20 … Queen 200, King 1000`)
  for fine granularity.
- **`simple_material[7]`** — plain material values for the simple eval mode.
- **`pst[6][BOARD_H][BOARD_W]`** — **Piece-Square Tables** (White's perspective, mirrored for
  Black). Encodes positional preferences: pawns want to advance, knights/bishops want the centre,
  the king wants to stay back early.
- **`tropism_w[7]`** — **king-tropism** weights; how much each piece type is rewarded for being near
  the *enemy* king.

#### `king_tropism(piece_type, pr, pc, ekr, ekc)`
Chebyshev distance from a piece to the enemy king; within distance 2, returns
`weight * (3 - distance)` — closer attackers score more. This drives the engine to swarm the
enemy king (which matters a lot in a king-capture game).

#### `set_eval_params(tempo, passed_pawn_scale_pct)`
Writes the two file-static knobs `g_eval_tempo` and `g_eval_pp_scale`, read later in `evaluate`.

- **`passed_pawn_bonus[BOARD_H] = {0,70,45,28,16,8}`** — bonus by rows-to-promotion; bigger the
  closer to queening, because promotion to a Queen is decisive on this small board.

#### `passed_pawns(self_b, enemy_b, owner)`
For each of `owner`'s pawns, checks whether any enemy pawn lies ahead on the same or adjacent
file (which would block/capture it). If none, it's a *passed pawn* — add the bonus indexed by
its distance to promotion. Handles both directions (owner 0 advances toward row 0; owner 1 toward
the last row).

#### `State::evaluate(use_kp_eval, use_mobility, history)`
The static position score from the side-to-move's view. Steps:
1. Win shortcut → `P_MAX`.
2. **KP mode** (`use_kp_eval`): locate both kings; for every piece add `material + PST`, and if the
   enemy king is on the board add `king_tropism`; then add scaled passed-pawn bonuses and a small
   `g_eval_tempo` bump for the side to move.
3. **Simple mode**: just sum `simple_material`.
4. **Mobility** (`use_mobility`): `+2 * (own_moves − opponent_moves)`, where the opponent count
   comes from a `create_null_state()`. Rewards having more options.
5. Returns `self_score − oppn_score + bonus`.

### B. Zobrist hashing

- **`zobrist_piece[2][7][BOARD_H][BOARD_W]`, `zobrist_side`** — random 64-bit keys, one per
  (player, piece-type, square) plus one for side-to-move.

#### `init_zobrist()`
Fills those tables once using a fast xorshift PRNG with a fixed seed (deterministic across runs).

#### `State::compute_hash_full()`
XORs together the keys of every piece on the board, plus `zobrist_side` if it's Black to move —
the position's full 64-bit fingerprint. Used the first time `hash()` is called on a state.

### C. Move application

#### `State::next_state(move)`
Builds the position after a move **and updates the hash incrementally** (far cheaper than a full
recompute):
- copy the board, read the moving piece, promote a pawn reaching the back rank to a Queen;
- toggle `zobrist_side`; XOR the moving piece *out* of its origin; if there's a capture, XOR the
  victim out and clear it; XOR the moved piece *in* at the destination;
- write the board changes, construct the child `State` for the opponent with the precomputed hash
  marked valid.

### D. Move generation

- **`move_table_rook_bishop[8][7][2]`** — ray offsets for the 8 sliding directions.
- **`move_table_knight[8][2]`** / **`move_table_king[8][2]`** — the 8 leaper offsets for knight and king.

#### `State::get_legal_actions_naive()`
The straightforward array-based generator. Loops the board, and per piece:
- **Pawn** — forward push if empty; diagonal only to capture; direction depends on colour.
- **Rook/Bishop/Queen** — slide along the relevant ray slice until blocked.
- **Knight/King** — the 8 fixed offsets.
- **King-capture short-circuit**: the moment any move can capture the enemy king (`piece==6`), it
  sets `game_state = WIN` and returns immediately — that's a winning move, no need to look further.

#### Bitboard generator — `bb_*` tables, `bb_init()`, `get_legal_actions_bitboard()`
A faster generator: the 30 squares fit in a `uint32_t`, square `(r,c) → bit r*5+c`.
- `bb_init()` precomputes attack masks for knight, king, and pawn push/capture per square.
- `get_legal_actions_bitboard()` builds occupancy masks, then iterates own pieces with a
  `__builtin_ctz` bit-scan; leapers AND a precomputed mask with `~self_occ`; sliders walk rays;
  the same king-capture short-circuit applies. Functionally identical to the naive version but
  branch-light.

#### `State::get_legal_actions()`
Dispatcher: picks the bitboard generator under `#ifdef USE_BITBOARD`, else the naive one.

### E. Serialization, display, null state, repetition

- **`piece_table` / `encode_output()`** — render the board with Unicode chess glyphs for the CLI.
- **`encode_state()`** — dump side-to-move + both piece planes as numbers (engine ↔ harness protocol).
- **`create_null_state()`** — a copy of the board with the **side to move flipped** and legal moves
  regenerated. Powers both mobility eval and null-move pruning.
- **`encode_board()` / `decode_board(s, side_to_move)`** — FEN-like string serialization
  (uppercase = White, lowercase = Black, `.` = empty) and its inverse.
- **`cell_display(row, col)`** — single-cell ASCII for the protocol's `d` (display) command.
- **`check_repetition(history, out_score)`** — returns a draw (`0`) if the current hash has appeared
  **≥ 3** times in the game history (threefold-repetition rule).

---

## How it all fits together

```
UBGI layer (time control + iterative deepening)
        │  repeatedly calls with depth = 1, 2, 3, ...
        ▼
PVS::search (root: aspiration window + PVS root loop)
        │  recurses
        ▼
PVS::eval_ctx  ──TT probe/store──►  Transposition Table
        │  ├─ null-move pruning ──► State::create_null_state
        │  ├─ order_moves ──► killers / history / MVV-LVA / TT move
        │  ├─ LMR + PVS scout/re-search
        │  └─ leaf ──► AlphaBeta::qsearch ──► State::evaluate
        ▼
State::next_state (incremental Zobrist) / get_legal_actions
```

Every search feature is independently toggleable via `ABParams`, and the numeric knobs are
runtime UCI options — so any single technique can be turned off or A/B-tested without recompiling,
while the defaults reproduce the full-strength engine.
