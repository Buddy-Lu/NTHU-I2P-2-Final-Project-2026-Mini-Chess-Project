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

    /* Tunable numeric knobs (runtime, so they can be A/B-tested via UCI
     * options without recompiling). Defaults reproduce the baked-in engine. */
    int tempo = 6;          // eval: bonus for side to move
    int pp_scale = 100;     // eval: passed-pawn bonus scale, percent
    int null_r = 2;         // null-move reduction
    int lmr_min_move = 3;   // LMR: first move index eligible for reduction
    int lmr_min_depth = 3;  // LMR: minimum depth to reduce
    int asp_delta = 30;     // aspiration window half-width

    bool use_nnue = false;          // use the trained NNUE evaluation
    std::string nnue_path = "nnue.bin";

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
        p.tempo             = param_int(m, "Tempo", 6);
        p.pp_scale          = param_int(m, "PassedPawnScale", 100);
        p.null_r            = param_int(m, "NullR", 2);
        p.lmr_min_move      = param_int(m, "LMRMinMove", 3);
        p.lmr_min_depth     = param_int(m, "LMRMinDepth", 3);
        p.asp_delta         = param_int(m, "AspDelta", 30);
        p.use_nnue          = param_bool(m, "UseNNUE", false);
        p.nnue_path         = param_str(m, "NNUEPath", "nnue.bin");
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
