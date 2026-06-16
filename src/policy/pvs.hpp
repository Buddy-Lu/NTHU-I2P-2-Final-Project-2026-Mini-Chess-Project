#pragma once
#include "search_types.hpp"
#include "game_history.hpp"
#include "alphabeta.hpp"   // reuses ABParams, AlphaBeta::qsearch, order_moves

/*============================================================
 * PVS — Principal Variation Search
 *
 * Alpha-beta's refinement: assume the first (best-ordered) move
 * is the principal variation and search it with the full window.
 * Every later move is first probed with a *null window*
 * [alpha, alpha+1] — just a yes/no "can this beat alpha?" test,
 * which prunes much faster. Only if that probe surprises us
 * (alpha < score < beta) do we re-search it with the full window.
 *
 * With good move ordering most probes fail low (no re-search), so
 * PVS searches fewer nodes than plain alpha-beta at the same
 * depth, returning the identical result. Reuses the alpha-beta
 * quiescence leaf and MVV-LVA ordering (ABParams).
 *============================================================*/
class PVS{
public:
    static int eval_ctx(
        State *state,
        int depth,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        int alpha,
        int beta,
        const ABParams& p,
        bool null_ok = true
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
