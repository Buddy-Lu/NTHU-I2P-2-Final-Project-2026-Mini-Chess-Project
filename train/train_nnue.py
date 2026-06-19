#!/usr/bin/env python
"""Train a small NNUE-style evaluation network for MiniChess (6x5).

Input  : a text file of "<board> <side> <score_cp>" lines produced by
          build/minichess-datagen (PVS search scores, side-to-move POV).
Output : a little-endian binary weight file consumed by the C++ engine
          (src/games/minichess/nnue.hpp).

Network (side-to-move relative, color-symmetric features):

    360 sparse binary inputs
      -> Linear(360, ACC)   + clipped-ReLU      (the "accumulator")
      -> Linear(ACC, L2)    + clipped-ReLU
      -> Linear(L2, 1)      (linear)            -> value (scaled cp)

The 360 features mirror nnue.hpp exactly:
    feat = is_own*180 + (piece_type-1)*30 + square
    is_own : 0 if the piece belongs to the side to move else 1
    square : row is flipped when side-to-move == 1 so "my" pieces always
             advance up the board  ->  color symmetry.
"""
import argparse
import struct
import numpy as np
import torch
import torch.nn as nn

H, W = 6, 5
NUM_SQ = H * W            # 30
NUM_FEAT = 2 * 6 * NUM_SQ # 360
PIECES = ".PRNBQK"        # index 1..6 == P R N B Q K  (matches piece_chars)


def board_to_active(board_str, stm):
    """Return the list of active feature indices for one position."""
    feats = []
    rows = board_str.split("/")
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch == ".":
                continue
            if ch.isupper():
                owner, pt = 0, PIECES.index(ch)
            else:
                owner, pt = 1, PIECES.index(ch.upper())
            is_own = 0 if owner == stm else 1
            sr = r if stm == 0 else (H - 1 - r)
            sq = sr * W + c
            feats.append(is_own * 180 + (pt - 1) * NUM_SQ + sq)
    return feats


def load_dataset(paths, scale):
    """Parse one or more data files into a dense feature matrix X / targets y."""
    xs, ys = [], []
    for path in paths:
        with open(path) as f:
            for line in f:
                parts = line.split()
                if len(parts) != 3:
                    continue
                board, side, score = parts[0], int(parts[1]), float(parts[2])
                row = np.zeros(NUM_FEAT, dtype=np.float32)
                for fi in board_to_active(board, side):
                    row[fi] = 1.0
                xs.append(row)
                ys.append(score / scale)
    X = np.asarray(xs, dtype=np.float32)
    y = np.asarray(ys, dtype=np.float32).reshape(-1, 1)
    return X, y


class NNUE(nn.Module):
    def __init__(self, acc, l2):
        super().__init__()
        self.l1 = nn.Linear(NUM_FEAT, acc)
        self.l2 = nn.Linear(acc, l2)
        self.l3 = nn.Linear(l2, 1)

    def forward(self, x):
        x = torch.clamp(self.l1(x), 0.0, 1.0)
        x = torch.clamp(self.l2(x), 0.0, 1.0)
        return self.l3(x)


def export_weights(model, path, acc, l2, scale):
    """Write the weight file in the exact layout nnue.hpp expects."""
    w1 = model.l1.weight.detach().numpy().T.copy()   # [NUM_FEAT, acc] (input-major)
    b1 = model.l1.bias.detach().numpy().copy()       # [acc]
    w2 = model.l2.weight.detach().numpy().T.copy()   # [acc, l2] (acc-major)
    b2 = model.l2.bias.detach().numpy().copy()       # [l2]
    w3 = model.l3.weight.detach().numpy().T.copy()   # [l2, 1]
    b3 = model.l3.bias.detach().numpy().copy()       # [1]

    with open(path, "wb") as f:
        f.write(b"NNUE")
        f.write(struct.pack("<i", 1))          # version
        f.write(struct.pack("<i", NUM_FEAT))
        f.write(struct.pack("<i", acc))
        f.write(struct.pack("<i", l2))
        f.write(struct.pack("<f", scale))      # output scale (cp per net unit)
        for arr in (w1, b1, w2, b2, w3, b3):
            f.write(arr.astype("<f4").tobytes())
    print(f"exported {path}: 360x{acc}x{l2}x1, scale={scale}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, nargs="+")
    ap.add_argument("--out", default="nnue.bin")
    ap.add_argument("--acc", type=int, default=128)
    ap.add_argument("--l2", type=int, default=32)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--batch", type=int, default=2048)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--scale", type=float, default=300.0)
    ap.add_argument("--val-frac", type=float, default=0.08)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    print(f"loading {args.data} ...")
    X, y = load_dataset(args.data, args.scale)
    n = X.shape[0]
    print(f"  {n} positions, label range [{y.min()*args.scale:.0f}, "
          f"{y.max()*args.scale:.0f}] cp")

    perm = np.random.permutation(n)
    X, y = X[perm], y[perm]
    n_val = int(n * args.val_frac)
    Xtr = torch.from_numpy(X[n_val:]);  ytr = torch.from_numpy(y[n_val:])
    Xva = torch.from_numpy(X[:n_val]);  yva = torch.from_numpy(y[:n_val])

    model = NNUE(args.acc, args.l2)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=1e-4)
    loss_fn = nn.MSELoss()

    best_vloss = float("inf")
    best_state = None
    ntr = Xtr.shape[0]
    for ep in range(1, args.epochs + 1):
        model.train()
        idx = torch.randperm(ntr)
        running = 0.0
        for s in range(0, ntr, args.batch):
            b = idx[s:s + args.batch]
            opt.zero_grad()
            pred = model(Xtr[b])
            loss = loss_fn(pred, ytr[b])
            loss.backward()
            opt.step()
            running += loss.item() * len(b)
        model.eval()
        with torch.no_grad():
            vpred = model(Xva)
            vloss = loss_fn(vpred, yva).item()
            # report in centipawns (RMSE)
            vrmse_cp = (vloss ** 0.5) * args.scale
        tag = ""
        if vloss < best_vloss:
            best_vloss = vloss
            best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
            tag = "  *best"
        print(f"epoch {ep:3d}  train_mse {running/ntr:.4f}  "
              f"val_mse {vloss:.4f}  val_rmse {vrmse_cp:.1f}cp{tag}")

    if best_state is not None:
        model.load_state_dict(best_state)
        print(f"restored best model (val_mse {best_vloss:.4f}, "
              f"val_rmse {(best_vloss**0.5)*args.scale:.1f}cp)")
    export_weights(model, args.out, args.acc, args.l2, args.scale)


if __name__ == "__main__":
    main()
