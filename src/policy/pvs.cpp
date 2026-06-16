#include <utility>
#include <chrono>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "state.hpp"
#include "config.hpp"
#include "pvs.hpp"


/*============================================================
 * Transposition table
 *
 * Caches the result of searching a position (keyed by its Zobrist
 * hash) so transpositions — different move orders reaching the
 * same position — are not re-searched. Each entry also stores the
 * best move found, which is tried first next time: by far the
 * strongest move-ordering signal there is.
 *
 * Replacement: depth-preferred (keep the deeper search). The full
 * 64-bit key is stored so hash collisions are detected, not used.
 *============================================================*/
namespace {

enum TTFlag : uint8_t { TT_NONE = 0, TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    uint64_t key   = 0;     // full Zobrist key (collision check)
    int32_t  score = 0;
    uint16_t move  = 0;     // packed from/to (0 = none)
    int16_t  depth = -1;
    uint8_t  flag  = TT_NONE;
};

std::vector<TTEntry> g_tt;
uint64_t g_tt_mask = 0;

constexpr int TT_BITS = 22;             // 2^22 entries (~80 MB), well under 4 GB
constexpr int MATE_ZONE = P_MAX - 1000; // scores above this are mate-distance

void tt_init(){
    if(g_tt.empty()){
        g_tt.assign(static_cast<size_t>(1) << TT_BITS, TTEntry{});
        g_tt_mask = (static_cast<uint64_t>(1) << TT_BITS) - 1;
    }
}

inline uint16_t pack_move(const Move& m){
    int from = static_cast<int>(m.first.first)  * BOARD_W + static_cast<int>(m.first.second);
    int to   = static_cast<int>(m.second.first) * BOARD_W + static_cast<int>(m.second.second);
    return static_cast<uint16_t>((from << 8) | to);
}

inline Move unpack_move(uint16_t pm){
    int from = pm >> 8, to = pm & 0xFF;
    return Move(Point(from / BOARD_W, from % BOARD_W),
                Point(to   / BOARD_W, to   % BOARD_W));
}

/* Mate scores are stored relative to the node (ply-independent) so a
 * cached mate is still correct when probed at a different ply. */
inline int tt_score_to_store(int score, int ply){
    if(score >=  MATE_ZONE) return score + ply;
    if(score <= -MATE_ZONE) return score - ply;
    return score;
}
inline int tt_score_from_probe(int score, int ply){
    if(score >=  MATE_ZONE) return score - ply;
    if(score <= -MATE_ZONE) return score + ply;
    return score;
}

/* Probe: returns true and sets `out` if a usable cutoff exists for
 * this depth/window. Always sets `tt_move` (0 if none) for ordering. */
bool tt_probe(uint64_t key, int depth, int ply, int alpha, int beta,
              int& out, uint16_t& tt_move){
    tt_move = 0;
    const TTEntry& e = g_tt[key & g_tt_mask];
    if(e.key != key || e.flag == TT_NONE){
        return false;
    }
    tt_move = e.move;
    if(e.depth < depth){
        return false;                 /* shallower: use move, not the score */
    }
    int s = tt_score_from_probe(e.score, ply);
    if(e.flag == TT_EXACT){ out = s; return true; }
    if(e.flag == TT_LOWER && s >= beta){ out = s; return true; }
    if(e.flag == TT_UPPER && s <= alpha){ out = s; return true; }
    return false;
}

void tt_store(uint64_t key, int depth, int ply, int score, uint8_t flag, uint16_t move){
    TTEntry& e = g_tt[key & g_tt_mask];
    /* depth-preferred replacement; always replace a different position */
    if(e.key == key && e.depth > depth && e.flag != TT_NONE){
        if(move && e.move == 0) e.move = move;  /* keep deeper, refresh move */
        return;
    }
    e.key   = key;
    e.score = tt_score_to_store(score, ply);
    e.move  = move ? move : e.move;
    e.depth = static_cast<int16_t>(depth);
    e.flag  = flag;
}

/*============================================================
 * Move-ordering heuristics: killers + history
 *
 * Captures are ordered by MVV-LVA, but most moves in a chess
 * search are *quiet* (non-captures) and MVV-LVA says nothing about
 * them. Two cheap, powerful heuristics fix that:
 *
 *  - Killer moves: a quiet move that caused a beta-cutoff at a
 *    given ply is likely to cut off sibling nodes at the same ply
 *    too, so we try the last two such moves early.
 *  - History heuristic: a per (side, from, to) table of how often
 *    a quiet move caused a cutoff (weighted by depth^2). Quiet
 *    moves are then ordered by this score.
 *============================================================*/
constexpr int MAX_PLY = 128;
uint16_t g_killers[MAX_PLY][2];
int      g_history[2][30][30];

void clear_heuristics(){
    for(int i = 0; i < MAX_PLY; i++){
        g_killers[i][0] = g_killers[i][1] = 0;
    }
    for(int s = 0; s < 2; s++){
        for(int f = 0; f < 30; f++){
            for(int t = 0; t < 30; t++){
                g_history[s][f][t] = 0;
            }
        }
    }
}

inline bool is_capture(const State* s, const Move& m){
    return s->piece_at(1 - s->player,
                       (int)m.second.first, (int)m.second.second) > 0;
}

inline int mvv_lva(const State* s, const Move& m){
    int victim   = s->piece_at(1 - s->player, (int)m.second.first, (int)m.second.second);
    int attacker = s->piece_at(s->player,     (int)m.first.first,  (int)m.first.second);
    return PIECE_VALUES[victim] * 16 - PIECE_VALUES[attacker];
}

/* Order this node's moves: TT move, then captures (MVV-LVA), then
 * killers, then quiet moves by history score. */
void order_moves(State* state, int ply, uint16_t tt_move){
    auto& moves = state->legal_actions;
    const size_t n = moves.size();
    if(n < 2){
        return;
    }
    int self = state->player;
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(n);
    for(const auto& m : moves){
        uint16_t pm = pack_move(m);
        int from = (int)m.first.first  * BOARD_W + (int)m.first.second;
        int to   = (int)m.second.first * BOARD_W + (int)m.second.second;
        int key;
        if(pm == tt_move){
            key = 2000000;
        }else if(is_capture(state, m)){
            key = 1000000 + mvv_lva(state, m);
        }else if(ply < MAX_PLY && pm == g_killers[ply][0]){
            key = 900000;
        }else if(ply < MAX_PLY && pm == g_killers[ply][1]){
            key = 800000;
        }else{
            key = g_history[self][from][to];   /* quiet: history score */
        }
        /* pawn promotion (to back rank) is always worth trying early */
        int attacker = state->piece_at(self, (int)m.first.first, (int)m.first.second);
        if(attacker == 1 && ((int)m.second.first == 0 || (int)m.second.first == BOARD_H - 1)){
            key += 500000;
        }
        scored.emplace_back(key, m);
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const std::pair<int, Move>& a, const std::pair<int, Move>& b){
            return a.first > b.first;
        });
    for(size_t i = 0; i < n; i++){
        moves[i] = scored[i].second;
    }
}

/* Record a quiet move that caused a beta cutoff. */
void update_cutoff_heuristics(const State* state, const Move& m, int ply, int depth){
    if(is_capture(state, m)){
        return;                          /* captures handled by MVV-LVA */
    }
    uint16_t pm = pack_move(m);
    if(ply < MAX_PLY && g_killers[ply][0] != pm){
        g_killers[ply][1] = g_killers[ply][0];
        g_killers[ply][0] = pm;
    }
    int from = (int)m.first.first  * BOARD_W + (int)m.first.second;
    int to   = (int)m.second.first * BOARD_W + (int)m.second.second;
    g_history[state->player][from][to] += depth * depth;
}

} // namespace


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

    uint64_t key = state->hash();
    history.push(key);

    if(depth <= 0){
        int score = p.use_quiescence
            ? AlphaBeta::qsearch(state, history, ply, ctx, alpha, beta, p)
            : state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(key);
        return score;
    }

    /* === Transposition-table probe === */
    int alpha_orig = alpha;
    uint16_t tt_move = 0;
    if(p.use_tt){
        int tt_val;
        if(tt_probe(key, depth, ply, alpha, beta, tt_val, tt_move)){
            history.pop(key);
            return tt_val;
        }
    }

    if(p.order_moves){
        order_moves(state, ply, tt_move);
    }

    int best_score = M_MAX;
    Move best_move = state->legal_actions[0];
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
            best_move = action;
        }
        if(best_score > alpha){
            alpha = best_score;
        }
        if(alpha >= beta){
            update_cutoff_heuristics(state, best_move, ply, depth);
            break;                  /* beta cutoff */
        }
        first = false;
    }

    /* === Transposition-table store === */
    if(p.use_tt){
        uint8_t flag = (best_score <= alpha_orig) ? TT_UPPER
                     : (best_score >= beta)       ? TT_LOWER
                                                  : TT_EXACT;
        tt_store(key, depth, ply, best_score, flag, pack_move(best_move));
    }

    history.pop(key);
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

    uint64_t key = state->hash();
    uint16_t tt_move = 0;
    if(p.use_tt){
        tt_init();
        int tt_val;
        /* probe for the hash move only (no cutoff at the root) */
        tt_probe(key, 0, 0, alpha, beta, tt_val, tt_move);
    }
    clear_heuristics();

    if(p.order_moves){
        order_moves(state, 0, tt_move);
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

    /* Store the root result (EXACT — full window) so the next
     * iterative-deepening pass searches this move first. */
    if(p.use_tt && !ctx.stop){
        tt_store(key, depth, 0, best_score, TT_EXACT, pack_move(result.best_move));
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
        {"UseTT", "true"},
    };
}

std::vector<ParamDef> PVS::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"OrderMoves", ParamDef::CHECK, "true"},
        {"UseQuiescence", ParamDef::CHECK, "true"},
        {"UseTT", ParamDef::CHECK, "true"},
    };
}
