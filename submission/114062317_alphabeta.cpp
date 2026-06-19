#include <utility>
#include <chrono>
#include <algorithm>
#include "state.hpp"
#include "config.hpp"
#include "alphabeta.hpp"


/*============================================================
 * Move ordering (MVV-LVA)
 *
 * Good move ordering is what makes alpha-beta prune well: if we
 * try the best move first, the [alpha, beta] window tightens
 * immediately and most sibling branches get cut.
 *
 * Heuristic: search winning captures first, ordered by
 * Most-Valuable-Victim / Least-Valuable-Attacker, then pawn
 * promotions, then quiet moves. PIECE_VALUES comes from the
 * game's config.hpp (game-agnostic: we only read piece_at()).
 *============================================================*/
static inline int move_order_key(const State* s, const Move& m){
    int self = s->player;
    int opp  = 1 - self;
    int victim   = s->piece_at(opp,  (int)m.second.first, (int)m.second.second);
    int attacker = s->piece_at(self, (int)m.first.first,  (int)m.first.second);

    int key = 0;
    if(victim > 0){
        /* MVV-LVA: prize the victim, penalise spending a big attacker */
        key = 100000 + PIECE_VALUES[victim] * 16 - PIECE_VALUES[attacker];
    }
    /* Pawn reaching the back rank promotes (to Queen here) — very strong */
    if(attacker == 1 &&
       ((int)m.second.first == 0 || (int)m.second.first == BOARD_H - 1)){
        key += 9000;
    }
    return key;
}

void AlphaBeta::order_moves(State* state){
    auto& moves = state->legal_actions;
    const size_t n = moves.size();
    if(n < 2){
        return;
    }
    /* Precompute keys once, then sort (n is small, ~10-30). */
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(n);
    for(const auto& m : moves){
        scored.emplace_back(move_order_key(state, m), m);
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const std::pair<int, Move>& a, const std::pair<int, Move>& b){
            return a.first > b.first;
        });
    for(size_t i = 0; i < n; i++){
        moves[i] = scored[i].second;
    }
}


static inline bool is_capture(const State* s, const Move& m){
    /* a capture lands on a square occupied by an opponent piece */
    return s->piece_at(1 - s->player,
                       (int)m.second.first, (int)m.second.second) > 0;
}

/*============================================================
 * Quiescence search
 *
 * At a normal leaf the static eval can be badly wrong mid-trade
 * ("horizon effect": we stop right after grabbing a pawn but
 * before the recapture). Quiescence keeps searching *only
 * captures* (and promotions) until the position is quiet, so the
 * eval is taken at a stable point.
 *
 * "Stand pat": the side to move may decline to capture and accept
 * the static score, so it bounds the search (we never force a bad
 * capture). Negamax window, same as eval_ctx.
 *============================================================*/
int AlphaBeta::qsearch(
    State *state,
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

    /* Stand-pat: static score if we stop capturing here. */
    int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
    if(stand_pat >= beta){
        return stand_pat;                 /* opponent won't enter this line */
    }
    if(stand_pat > alpha){
        alpha = stand_pat;
    }

    /* Safety cap on quiescence depth (capture chains are short, but
     * guard against pathological positions). */
    if(ply > 64){
        return alpha;
    }

    if(p.order_moves){
        order_moves(state);
    }

    int best = stand_pat;
    for(auto& action : state->legal_actions){
        if(!is_capture(state, action)){
            continue;                     /* quiescence: captures only */
        }
        State* next = static_cast<State*>(state->next_state(action));
        bool same = next->same_player_as_parent();

        int child;
        if(same){
            child = qsearch(next, history, ply + 1, ctx, alpha, beta, p);
        }else{
            child = -qsearch(next, history, ply + 1, ctx, -beta, -alpha, p);
        }
        delete next;

        if(child > best){
            best = child;
        }
        if(best > alpha){
            alpha = best;
        }
        if(alpha >= beta){
            break;
        }
    }
    return best;
}

/*============================================================
 * AlphaBeta — eval_ctx
 *
 * Negamax with alpha-beta pruning. Mirrors MiniMax::eval_ctx
 * (terminal/leaf handling, repetition, same_player_as_parent)
 * but carries an [alpha, beta] window and cuts off branches.
 *============================================================*/
int AlphaBeta::eval_ctx(
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

    /* === Lazy move generation (sets game_state) === */
    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    /* === Terminal checks === */
    if(state->game_state == WIN){
        return P_MAX - ply;          /* prefer faster wins */
    }
    if(state->game_state == DRAW){
        return 0;
    }

    /* === Repetition check (game-specific) === */
    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    /* === Leaf: quiescence (or plain static eval) === */
    if(depth <= 0){
        int score = p.use_quiescence
            ? qsearch(state, history, ply, ctx, alpha, beta, p)
            : state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    /* === Negamax loop with alpha-beta === */
    if(p.order_moves){
        order_moves(state);
    }

    int best_score = M_MAX;
    for(auto& action : state->legal_actions){
        State* next = static_cast<State*>(state->next_state(action));
        bool same = next->same_player_as_parent();

        int child;
        if(same){
            /* same player keeps moving (multi-stone turn): same window */
            child = eval_ctx(next, depth - 1, history, ply + 1, ctx, alpha, beta, p);
        }else{
            /* opponent to move: negate score, flip & negate window */
            child = -eval_ctx(next, depth - 1, history, ply + 1, ctx, -beta, -alpha, p);
        }

        delete next;

        if(child > best_score){
            best_score = child;
        }
        if(best_score > alpha){
            alpha = best_score;
        }
        if(alpha >= beta){
            break;                   /* beta cutoff: prune the rest */
        }
    }

    history.pop(state->hash());
    return best_score;
}


/*============================================================
 * AlphaBeta — search (root)
 *
 * Full-window root. Raising alpha as better root moves are found
 * tightens the window for the remaining children (the pruning
 * payoff), while every root move is still examined.
 *============================================================*/
SearchResult AlphaBeta::search(
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
        order_moves(state);
    }

    for(auto& action : state->legal_actions){
        State* next = static_cast<State*>(state->next_state(action));
        bool same = next->same_player_as_parent();

        int child;
        if(same){
            child = eval_ctx(next, depth - 1, history, 1, ctx, alpha, beta, p);
        }else{
            child = -eval_ctx(next, depth - 1, history, 1, ctx, -beta, -alpha, p);
        }

        delete next;

        if(move_index == 0){
            result.best_move = action;
        }

        if(child > best_score){
            best_score = child;
            result.best_move = action;
            if(best_score > alpha){
                alpha = best_score;   /* narrows window for later root moves */
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
 * AlphaBeta — default_params / param_defs
 *============================================================*/
ParamMap AlphaBeta::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
        {"OrderMoves", "true"},
        {"UseQuiescence", "true"},
    };
}

std::vector<ParamDef> AlphaBeta::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"OrderMoves", ParamDef::CHECK, "true"},
        {"UseQuiescence", ParamDef::CHECK, "true"},
    };
}
