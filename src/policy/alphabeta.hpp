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
    bool order_moves = true;    // MVV-LVA capture ordering
    bool use_quiescence = true; // captures-only search at the leaf
    bool use_tt = true;         // transposition table (PVS policy)
    bool use_null = true;       // null-move pruning (PVS policy)
    bool use_lmr = true;        // late move reductions (PVS policy)
    bool use_asp = true;        // aspiration windows at the root (PVS policy)

    static ABParams from_map(const ParamMap& m){
        ABParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        p.order_moves       = param_bool(m, "OrderMoves", true);
        p.use_quiescence    = param_bool(m, "UseQuiescence", true);
        p.use_tt            = param_bool(m, "UseTT", true);
        p.use_null          = param_bool(m, "UseNullMove", true);
        p.use_lmr           = param_bool(m, "UseLMR", true);
        p.use_asp           = param_bool(m, "UseAspiration", true);
        return p;
    }
};

class AlphaBeta{
public:
    /* Shared MVV-LVA move ordering (reused by the PVS policy). */
    static void order_moves(State* state);

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
    static int qsearch(
        State *state,
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
