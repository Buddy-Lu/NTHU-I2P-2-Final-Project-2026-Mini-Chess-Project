#pragma once
/*============================================================
 * NNUE evaluation (MiniChess 6x5)
 *
 * A small, side-to-move-relative neural evaluation trained by
 * train/train_nnue.py. The weight file layout and the 360-feature
 * encoding here must stay in lock-step with that script.
 *
 *   360 sparse inputs
 *     -> Linear(360, ACC) + clipped ReLU   (the accumulator)
 *     -> Linear(ACC, L2)  + clipped ReLU
 *     -> Linear(L2, 1)    (linear)         -> value, *scale -> cp
 *
 * Feature index:
 *     feat = is_own*180 + (piece_type-1)*30 + square
 *     is_own : 0 if the piece belongs to the side to move, else 1
 *     square : row flipped when side-to-move == 1, so the side to move
 *              always advances "up" the board (color symmetry).
 *
 * The accumulator is rebuilt from the (sparse, ~12) active features on
 * every call — O(pieces * ACC), the cheap NNUE-style forward pass —
 * rather than incrementally across make/unmake, which the fresh-State
 * search model here does not expose.
 *============================================================*/
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#include "config.hpp"

namespace nnue {

/* Fixed upper bounds so the eval hot path uses stack buffers, never the
 * heap. load() rejects any weight file exceeding these. */
constexpr int MAX_ACC = 512;
constexpr int MAX_L2  = 128;

struct Net {
    int num_feat = 0;
    int acc = 0;
    int l2 = 0;
    float scale = 300.0f;
    std::vector<float> w1;   // [num_feat * acc], input-major
    std::vector<float> b1;   // [acc]
    std::vector<float> w2;   // [acc * l2], acc-major
    std::vector<float> b2;   // [l2]
    std::vector<float> w3;   // [l2 * 1]
    std::vector<float> b3;   // [1]
    bool ready = false;
};

inline Net& net(){
    static Net n;
    return n;
}

inline bool& enabled(){
    static bool e = false;
    return e;
}

/* Load a weight file written by train_nnue.py. Returns false on any
 * mismatch; leaves the net unloaded so eval falls back to hand-crafted. */
inline bool load(const std::string& path){
    Net& n = net();
    n.ready = false;

    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f){
        return false;
    }
    char magic[4];
    if(std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "NNUE", 4) != 0){
        std::fclose(f);
        return false;
    }
    int32_t version = 0, num_feat = 0, acc = 0, l2 = 0;
    float scale = 0.0f;
    bool ok = std::fread(&version,  sizeof(int32_t), 1, f) == 1
           && std::fread(&num_feat, sizeof(int32_t), 1, f) == 1
           && std::fread(&acc,      sizeof(int32_t), 1, f) == 1
           && std::fread(&l2,       sizeof(int32_t), 1, f) == 1
           && std::fread(&scale,    sizeof(float),   1, f) == 1;
    if(!ok || num_feat != 2 * 6 * (BOARD_H * BOARD_W)
       || acc < 1 || acc > MAX_ACC || l2 < 1 || l2 > MAX_L2){
        std::fclose(f);
        return false;
    }

    n.num_feat = num_feat;
    n.acc = acc;
    n.l2 = l2;
    n.scale = scale;

    auto rd = [&](std::vector<float>& v, size_t count) -> bool {
        v.resize(count);
        return std::fread(v.data(), sizeof(float), count, f) == count;
    };
    ok  = rd(n.w1, (size_t)num_feat * acc);
    ok &= rd(n.b1, (size_t)acc);
    ok &= rd(n.w2, (size_t)acc * l2);
    ok &= rd(n.b2, (size_t)l2);
    ok &= rd(n.w3, (size_t)l2);
    ok &= rd(n.b3, (size_t)1);
    std::fclose(f);

    n.ready = ok;
    return ok;
}

/* Enable/disable NNUE and lazily (re)load the weight file. On a failed
 * load the net stays unready, so State::evaluate falls back to the
 * hand-crafted evaluation automatically. */
inline void configure(bool on, const std::string& path){
    enabled() = on;
    static std::string cur_path;
    if(on && (!net().ready || cur_path != path)){
        load(path);
        cur_path = path;
    }
}

/* Collect active feature indices for board[2][H][W] with side to move stm. */
inline void active_features(const char board[2][BOARD_H][BOARD_W], int stm,
                            int* out, int& count){
    count = 0;
    for(int owner = 0; owner < 2; owner++){
        int is_own = (owner == stm) ? 0 : 1;
        for(int r = 0; r < BOARD_H; r++){
            int sr = (stm == 0) ? r : (BOARD_H - 1 - r);
            for(int c = 0; c < BOARD_W; c++){
                int pt = board[owner][r][c];
                if(pt >= 1 && pt <= 6){
                    int sq = sr * BOARD_W + c;
                    out[count++] = is_own * (6 * BOARD_H * BOARD_W)
                                 + (pt - 1) * (BOARD_H * BOARD_W) + sq;
                }
            }
        }
    }
}

inline float clipped(float x){
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

/* Evaluate the position; returns a score in centipawns from the side to
 * move's perspective (same contract as State::evaluate). */
inline int eval(const char board[2][BOARD_H][BOARD_W], int stm){
    Net& n = net();

    int feats[64];
    int nf = 0;
    active_features(board, stm, feats, nf);

    /* Accumulator: b1 + sum of active rows of w1. */
    float h1[MAX_ACC];
    const float* b1 = n.b1.data();
    for(int j = 0; j < n.acc; j++){
        h1[j] = b1[j];
    }
    for(int i = 0; i < nf; i++){
        const float* row = &n.w1[(size_t)feats[i] * n.acc];
        for(int j = 0; j < n.acc; j++){
            h1[j] += row[j];
        }
    }
    for(int j = 0; j < n.acc; j++){
        h1[j] = clipped(h1[j]);
    }

    /* Hidden layer 2. */
    float h2[MAX_L2];
    const float* w2 = n.w2.data();
    for(int k = 0; k < n.l2; k++){
        float s = n.b2[k];
        for(int j = 0; j < n.acc; j++){
            s += h1[j] * w2[(size_t)j * n.l2 + k];
        }
        h2[k] = clipped(s);
    }

    /* Output. */
    float out = n.b3[0];
    for(int k = 0; k < n.l2; k++){
        out += h2[k] * n.w3[k];
    }

    int cp = (int)std::lround(out * n.scale);
    if(cp >  30000) cp =  30000;
    if(cp < -30000) cp = -30000;
    return cp;
}

} // namespace nnue
