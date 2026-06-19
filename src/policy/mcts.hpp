#pragma once
#include "search_types.hpp"
#include "game_history.hpp"

/*============================================================
 * MCTS — Monte Carlo Tree Search (UCT)
 *
 * A game-agnostic upper-confidence-bound tree search. Unlike the
 * negamax family (minimax / alphabeta / pvs) it does not enumerate
 * a fixed-depth subtree; it grows an asymmetric search tree by
 * repeatedly running:
 *
 *   1. Selection  — descend the tree by UCT until a leaf.
 *   2. Expansion  — add one untried child.
 *   3. Simulation — estimate the leaf's value (random playout to a
 *                   depth cap, or a static-eval shortcut).
 *   4. Backup     — propagate the value up the path, flipping sign
 *                   between alternating players (negamax convention).
 *
 * It honours the same fixed-depth `search()` contract as the other
 * policies: the UBGI layer drives it with growing `depth`, which here
 * scales the per-call simulation budget (SimsPerDepth * depth). Time
 * control still lives in the UBGI layer.
 *============================================================*/

struct MCTSParams {
    bool use_kp_eval = true;
    bool use_eval_mobility = true;
    bool report_partial = true;

    bool eval_rollout = true;   // value leaves by static eval instead of a random playout
    int  uct_c = 141;           // exploration constant * 100 (1.41 ~= sqrt(2))
    int  sims_per_depth = 1500; // simulations per unit of driver depth
    int  max_sims = 400000;     // hard cap on simulations per search() call
    int  rollout_depth = 24;    // random-playout move cap before falling back to eval
    int  eval_scale = 200;      // static-eval -> [-1,1] via tanh(eval / eval_scale)

    /* Eval knobs shared with the negamax evaluation (see ABParams). */
    int  tempo = 6;
    int  pp_scale = 100;

    static MCTSParams from_map(const ParamMap& m){
        MCTSParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        p.eval_rollout      = param_bool(m, "McEvalRollout", true);
        p.uct_c             = param_int(m, "McUctC", 141);
        p.sims_per_depth    = param_int(m, "McSimsPerDepth", 1500);
        p.max_sims          = param_int(m, "McMaxSims", 400000);
        p.rollout_depth     = param_int(m, "McRolloutDepth", 24);
        p.eval_scale        = param_int(m, "McEvalScale", 200);
        p.tempo             = param_int(m, "Tempo", 6);
        p.pp_scale          = param_int(m, "PassedPawnScale", 100);
        return p;
    }
};

class MCTS{
public:
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};
