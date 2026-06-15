#pragma once
#include "search_types.hpp"
#include "game_history.hpp"

/*============================================================
 * Alpha-Beta — negamax with alpha-beta pruning + MVV-LVA
 *
 * Same fixed-depth contract as MiniMax::search (iterative
 * deepening + time control live in the UBGI layer). Returns the
 * identical best move / score as plain minimax at equal depth,
 * but prunes branches that cannot affect the result.
 *============================================================*/

struct ABParams {
    bool use_kp_eval = true;
    bool use_eval_mobility = true;
    bool report_partial = true;
    bool order_moves = true;   // MVV-LVA capture ordering

    static ABParams from_map(const ParamMap& m){
        ABParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        p.order_moves       = param_bool(m, "OrderMoves", true);
        return p;
    }
};

class AlphaBeta{
public:
    static int eval_ctx(
        State *state,
        int depth,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        int alpha,
        int beta,
        const ABParams& p
    );
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};
