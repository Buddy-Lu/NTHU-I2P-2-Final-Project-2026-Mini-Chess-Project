# NNUE — trained neural evaluation

A small NNUE-style (Efficiently Updatable Neural Network) evaluation, trained
to mimic the PVS engine's search scores, and wired into the engine behind a
`UseNNUE` toggle. This documents the full pipeline: data → train → infer.

## Architecture

Side-to-move-relative, color-symmetric. 360 sparse binary inputs:

```
feat = is_own*180 + (piece_type-1)*30 + square
  is_own : 0 if the piece belongs to the side to move, else 1
  square : row flipped when side-to-move == 1, so "my" pieces always
           advance up the board (halves the data needed via symmetry)
```

Network (all weights float32):

```
360 inputs
  -> Linear(360, 128) + clipped ReLU   (the "accumulator")
  -> Linear(128, 32)  + clipped ReLU
  -> Linear(32, 1)    (linear)         -> value, *scale(300) -> centipawns
```

The accumulator is recomputed from the (sparse, ~12) active features on every
`evaluate()` call — O(pieces × 128), the cheap NNUE-style forward pass. True
incremental make/unmake updates aren't done because the search allocates a
fresh `State` per move (no make/unmake seam); recomputing from the sparse
feature set is still fast.

## Pipeline

1. **Data generation** — `src/nnue_datagen.cpp` (`make datagen`).
   Self-play with PVS; randomised openings + 10% random moves for diversity;
   every non-opening position is labelled with its PVS depth-6 search score
   (side-to-move POV), clamped to ±3000 cp.

   ```bash
   build/minichess-datagen data/train.txt 4500 6 <seed> 4 60
   # -> 149,444 positions
   ```

2. **Training** — `train/train_nnue.py` (PyTorch, CPU).
   MSE regression on `score/300`; Adam, weight_decay 1e-4; best-val
   checkpoint restored before export (early stop ~epoch 23).

   ```bash
   .venv/Scripts/python train/train_nnue.py --data data/train.txt \
       --out nnue.bin --acc 128 --l2 32 --epochs 45 --scale 300
   # best val RMSE ~498 cp
   ```

3. **Inference** — `src/games/minichess/nnue.hpp` (header-only).
   Loads `nnue.bin` (little-endian: magic `NNUE`, version, dims, scale, then
   the six weight/bias arrays). `State::evaluate()` returns `nnue::eval(...)`
   when `nnue::enabled() && net().ready`; otherwise the hand-crafted eval. A
   failed/missing load leaves the net unready, so the engine degrades
   gracefully to the hand-crafted eval.

## Engine controls

PVS and AlphaBeta expose:

| Option | Type | Default | Notes |
|---|---|---|---|
| `UseNNUE` | check | `false` | Use the neural eval instead of hand-crafted KP. |
| `NNUEPath` | string | `nnue.bin` | Weight file (set via raw `setoption`; not advertised). |

```
setoption name Algorithm value pvs
setoption name UseNNUE value true
```

CLI: `--white-param UseNNUE=true`.

## Getting NNUE to beat the hand-crafted eval (2 s/move)

The first model lost **0–5** at 2 s/move. Two fixes turned that around:

1. **Inference speed** — the eval was heap-allocating two `std::vector`s per
   call (millions of calls). Switched to fixed stack buffers (`MAX_ACC`,
   `MAX_L2`). NPS recovered to ~720k; the search-depth deficit vs hand-crafted
   shrank to **1 ply** (depth 12 vs 13), so the loss was eval *quality*, not
   speed.
2. **Quiet-position training** — depth-N *search* scores are dominated by
   tactics a static net can't predict (val RMSE stuck ~498 cp even with 383k
   mixed-depth positions). Re-generated **265k quiet positions** (data only
   recorded when the engine's best move is neither a capture nor a promotion).
   This both de-noises the labels (RMSE → 468 cp) and — crucially — matches the
   distribution where the eval is actually used: the engine's quiescence search
   only evaluates quiet leaves. Generate with:

   ```bash
   build/minichess-datagen data/quiet.txt 12000 6 <seed> 4 60 quiet
   ```

## Results

- Loads correctly and changes play (different PV / node counts vs hand-crafted).
- **PVS+NNUE beats `random` 2–0.**
- **PVS+NNUE vs PVS+hand-crafted, 5 games @ 2 s/move: +2 −0 =3 (3.5/5, 70%),
  undefeated.** (First, pre-quiet model lost this same match 0–5.)
- Confirmation, 5 games @ 2 s/move with randomised openings: +2 −3 (2.0/5).
- Combined 10 games ≈ **55%**: the neural eval is now ~at parity with the tuned
  hand-crafted eval (up from a 0–5 rout), winning the standard 5-game match but
  within variance of even over a larger sample.

## Notes

The win comes from training on quiet positions (matching qsearch leaves) plus
fast stack-buffer inference, not from a low absolute RMSE (468 cp is still high
because labels are search scores). Further gains available: deeper teacher,
blended win-prob/outcome target, int16 quantization, larger accumulator.
