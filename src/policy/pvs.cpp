#include <utility>
#include <chrono>
#include "state.hpp"
#include "config.hpp"
#include "pvs.hpp"


/*============================================================
 * PVS — eval_ctx
 *
 * Negamax with principal-variation (null-window) search on top
 * of alpha-beta. Prologue (terminals, repetition, quiescence
 * leaf) matches AlphaBeta::eval_ctx; the difference is the loop:
 * full window for the first move, null-window probe + optional
 * re-search for the rest.
 *============================================================*/
int PVS::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    int alpha,
    int beta,
    const ABParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply;
    }
    if(state->game_state == DRAW){
        return 0;
    }

    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    if(depth <= 0){
        int score = p.use_quiescence
            ? AlphaBeta::qsearch(state, history, ply, ctx, alpha, beta, p)
            : state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    if(p.order_moves){
        AlphaBeta::order_moves(state);
    }

    int best_score = M_MAX;
    bool first = true;

    for(auto& action : state->legal_actions){
        State* next = static_cast<State*>(state->next_state(action));
        bool same = next->same_player_as_parent();

        int child;
        if(same){
            /* same player keeps moving: window not flipped */
            if(first){
                child = eval_ctx(next, depth - 1, history, ply + 1, ctx, alpha, beta, p);
            }else{
                child = eval_ctx(next, depth - 1, history, ply + 1, ctx, alpha, alpha + 1, p);
                if(child > alpha && child < beta){
                    child = eval_ctx(next, depth - 1, history, ply + 1, ctx, alpha, beta, p);
                }
            }
        }else{
            /* opponent to move: negate score, flip & negate window */
            if(first){
                child = -eval_ctx(next, depth - 1, history, ply + 1, ctx, -beta, -alpha, p);
            }else{
                /* null-window scout around alpha */
                child = -eval_ctx(next, depth - 1, history, ply + 1, ctx, -alpha - 1, -alpha, p);
                if(child > alpha && child < beta){
                    /* probe surprised us: this move may be a new PV — re-search full */
                    child = -eval_ctx(next, depth - 1, history, ply + 1, ctx, -beta, -alpha, p);
                }
            }
        }

        delete next;

        if(child > best_score){
            best_score = child;
        }
        if(best_score > alpha){
            alpha = best_score;
        }
        if(alpha >= beta){
            break;                  /* beta cutoff */
        }
        first = false;
    }

    history.pop(state->hash());
    return best_score;
}


/*============================================================
 * PVS — search (root)
 *
 * The first root move establishes the PV with a full window;
 * later root moves are scouted with a null window and only
 * re-searched (full window) if they beat the current best.
 *============================================================*/
SearchResult PVS::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    ABParams p = ABParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    auto t_start = std::chrono::high_resolution_clock::now();

    if(!state->legal_actions.size()){
        state->get_legal_actions();
    }

    const int INF = 1000000;
    int alpha = -INF, beta = INF;
    int best_score = -INF;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    if(p.order_moves){
        AlphaBeta::order_moves(state);
    }

    for(auto& action : state->legal_actions){
        State* next = static_cast<State*>(state->next_state(action));
        bool same = next->same_player_as_parent();

        int child;
        if(move_index == 0){
            /* first move: full window establishes the PV baseline */
            child = same
                ? eval_ctx(next, depth - 1, history, 1, ctx, alpha, beta, p)
                : -eval_ctx(next, depth - 1, history, 1, ctx, -beta, -alpha, p);
        }else{
            /* scout with a null window, re-search if it beats alpha */
            if(same){
                child = eval_ctx(next, depth - 1, history, 1, ctx, alpha, alpha + 1, p);
                if(child > alpha && child < beta){
                    child = eval_ctx(next, depth - 1, history, 1, ctx, alpha, beta, p);
                }
            }else{
                child = -eval_ctx(next, depth - 1, history, 1, ctx, -alpha - 1, -alpha, p);
                if(child > alpha && child < beta){
                    child = -eval_ctx(next, depth - 1, history, 1, ctx, -beta, -alpha, p);
                }
            }
        }

        delete next;

        if(move_index == 0){
            result.best_move = action;
        }

        if(child > best_score){
            best_score = child;
            result.best_move = action;
            if(best_score > alpha){
                alpha = best_score;
            }
            if(p.report_partial && ctx.on_root_update){
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }
        move_index++;
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    result.score = best_score;
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    result.time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    result.pv = {result.best_move};

    return result;
}


/*============================================================
 * PVS — default_params / param_defs (same knobs as AlphaBeta)
 *============================================================*/
ParamMap PVS::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
        {"OrderMoves", "true"},
        {"UseQuiescence", "true"},
    };
}

std::vector<ParamDef> PVS::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"OrderMoves", ParamDef::CHECK, "true"},
        {"UseQuiescence", ParamDef::CHECK, "true"},
    };
}
