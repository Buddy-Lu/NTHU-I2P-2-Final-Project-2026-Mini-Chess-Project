#include <utility>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "state.hpp"
#include "config.hpp"
#include "mcts.hpp"


/*============================================================
 * MCTS — internal tree node
 *
 * Each node owns the State it wraps (except the root, whose State
 * is owned by the caller). Statistics are stored from the node's
 * own side-to-move perspective: W is the running sum of simulation
 * values seen from this node looking out, so Q = W / N is "how good
 * is this position for the player to move here". Selection from a
 * parent therefore negates a child's Q (the players alternate).
 *============================================================*/
namespace {

struct Node {
    State* state;
    bool   owns_state;
    Node*  parent;
    Move   move;             // move from parent into this node
    bool   same_parent;      // does this node share the parent's side to move?
    std::vector<Node*> children;
    size_t next_untried = 0; // index of the next legal move to expand
    bool   ready = false;    // legal_actions / game_state computed
    bool   terminal = false;
    double term_value = 0.0; // value (this node's perspective) if terminal
    int    N = 0;
    double W = 0.0;
};

Node* make_node(State* s, bool owns, Node* parent, const Move& m){
    Node* n = new Node();
    n->state = s;
    n->owns_state = owns;
    n->parent = parent;
    n->move = m;
    n->same_parent = s->same_player_as_parent();
    return n;
}

void free_tree(Node* n){
    if(!n){
        return;
    }
    for(Node* c : n->children){
        free_tree(c);
    }
    if(n->owns_state){
        delete n->state;
    }
    delete n;
}

/* Compute legal moves / terminal status the first time a node is touched. */
void ensure_ready(Node* n, GameHistory& history){
    if(n->ready){
        return;
    }
    n->ready = true;
    if(n->state->legal_actions.empty() && n->state->game_state == UNKNOWN){
        n->state->get_legal_actions();
    }
    if(n->state->game_state == WIN){
        /* The side to move here can capture the enemy king: it wins. */
        n->terminal = true;
        n->term_value = 1.0;
        return;
    }
    int rep_score;
    if(n->state->check_repetition(history, rep_score)){
        n->terminal = true;
        n->term_value = 0.0;   /* draw */
        return;
    }
    if(n->state->legal_actions.empty()){
        n->terminal = true;
        n->term_value = 0.0;   /* no moves: treat as a draw */
    }
}

/* Value of moving from a parent into child `c`, in the parent's
 * perspective. c->W/c->N is the child's own perspective, so we flip
 * it unless the two nodes share a side to move. */
inline double child_q_for_parent(const Node* c){
    double q = c->W / (double)c->N;
    return c->same_parent ? q : -q;
}

/* Pick the child maximising UCT (exploitation + exploration). */
Node* best_uct_child(Node* parent, double c_uct){
    double log_parent = std::log((double)parent->N + 1.0);
    Node* best = nullptr;
    double best_score = -1e18;
    for(Node* c : parent->children){
        double q = child_q_for_parent(c);
        double u = c_uct * std::sqrt(log_parent / (double)c->N);
        double score = q + u;
        if(score > best_score){
            best_score = score;
            best = c;
        }
    }
    return best;
}

/* Estimate the value of `s` from its own side-to-move perspective,
 * in [-1, 1]. Either a static-eval shortcut or a random playout that
 * falls back to static eval at the depth cap. */
double simulate(State* s, const MCTSParams& p){
    int persp = s->player;

    if(p.eval_rollout){
        int e = s->evaluate(p.use_kp_eval, p.use_eval_mobility, nullptr);
        if(e >= P_MAX){
            return 1.0;
        }
        return std::tanh((double)e / (double)p.eval_scale);
    }

    /* Random playout. `s` is owned by a node — never mutate or delete it;
     * the first successor is heap-allocated and the chain is freed here. */
    State* owned = nullptr;   // most recent heap state we must delete
    State* cur = s;
    double result = 0.0;
    bool done = false;

    for(int d = 0; d < p.rollout_depth; d++){
        if(cur->legal_actions.empty() && cur->game_state == UNKNOWN){
            cur->get_legal_actions();
        }
        if(cur->game_state == WIN){
            result = (cur->player == persp) ? 1.0 : -1.0;
            done = true;
            break;
        }
        if(cur->legal_actions.empty()){
            result = 0.0;
            done = true;
            break;
        }
        int idx = rand() % (int)cur->legal_actions.size();
        State* nxt = static_cast<State*>(cur->next_state(cur->legal_actions[idx]));
        if(owned){
            delete owned;
        }
        owned = nxt;
        cur = nxt;
    }

    if(!done){
        int e = cur->evaluate(p.use_kp_eval, p.use_eval_mobility, nullptr);
        double t = (e >= P_MAX) ? 1.0 : std::tanh((double)e / (double)p.eval_scale);
        result = (cur->player == persp) ? t : -t;
    }

    if(owned){
        delete owned;
    }
    return result;
}

} // namespace


/*============================================================
 * MCTS — search
 *============================================================*/
SearchResult MCTS::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    MCTSParams p = MCTSParams::from_map(ctx.params);
    set_eval_params(p.tempo, p.pp_scale);   /* propagate eval knobs to evaluate() */

    SearchResult result;
    result.depth = depth;

    auto t_start = std::chrono::high_resolution_clock::now();

    if(state->legal_actions.empty()){
        state->get_legal_actions();
    }
    if(state->legal_actions.empty()){
        result.best_move = Move();
        return result;
    }

    double c_uct = (double)p.uct_c / 100.0;

    /* Per-call simulation budget grows with the driver's depth so the
     * UBGI iterative-deepening loop spends more time as it goes; the
     * movetime check between calls stops the deepening. */
    long budget = (long)p.sims_per_depth * (depth > 0 ? depth : 1);
    if(budget > p.max_sims){
        budget = p.max_sims;
    }
    if(budget < 1){
        budget = 1;
    }

    Node* root = make_node(state, /*owns_state=*/false, nullptr, Move());
    ensure_ready(root, history);

    result.best_move = state->legal_actions[0];
    Move last_reported = result.best_move;
    int total_moves = (int)state->legal_actions.size();

    for(long it = 0; it < budget; it++){
        if(ctx.stop){
            break;
        }

        /* --- Selection + expansion: descend to a leaf --- */
        Node* node = root;
        int path_depth = 0;
        while(true){
            ensure_ready(node, history);
            if(node->terminal){
                break;
            }
            size_t n_actions = node->state->legal_actions.size();
            if(node->next_untried < n_actions){
                /* Expansion: realise one untried move. */
                Move m = node->state->legal_actions[node->next_untried++];
                State* cs = static_cast<State*>(node->state->next_state(m));
                Node* child = make_node(cs, /*owns_state=*/true, node, m);
                node->children.push_back(child);
                node = child;
                path_depth++;
                break;
            }
            if(node->children.empty()){
                break;   /* defensive: no moves */
            }
            node = best_uct_child(node, c_uct);
            path_depth++;
        }

        if(path_depth > ctx.seldepth){
            ctx.seldepth = path_depth;
        }

        /* --- Simulation --- */
        ensure_ready(node, history);
        double v = node->terminal
            ? node->term_value
            : simulate(node->state, p);
        ctx.nodes++;

        /* --- Backup: propagate up, flipping sign between alternating turns. --- */
        for(Node* cur = node; cur != nullptr; cur = cur->parent){
            cur->N++;
            cur->W += v;
            if(!cur->same_parent){
                v = -v;
            }
        }

        /* --- Periodic root report (most-visited move) --- */
        if(p.report_partial && ctx.on_root_update && (it & 1023) == 1023
           && !root->children.empty()){
            Node* best = nullptr;
            for(Node* c : root->children){
                if(!best || c->N > best->N){
                    best = c;
                }
            }
            if(best){
                result.best_move = best->move;
                if(!(best->move == last_reported)){
                    last_reported = best->move;
                    double q = child_q_for_parent(best);
                    int cp = (int)std::lround(q * 1200.0);
                    if(cp > 3000) cp = 3000;
                    if(cp < -3000) cp = -3000;
                    ctx.on_root_update({best->move, cp, depth, 0, total_moves});
                }
            }
        }
    }

    /* --- Final move choice: most-visited root child (robust child). --- */
    Node* best = nullptr;
    for(Node* c : root->children){
        if(!best || c->N > best->N){
            best = c;
        }
    }

    int score_cp = 0;
    if(best){
        result.best_move = best->move;
        double q = child_q_for_parent(best);
        score_cp = (int)std::lround(q * 1200.0);
        if(score_cp > 3000) score_cp = 3000;
        if(score_cp < -3000) score_cp = -3000;

        /* Principal variation: walk most-visited children down the tree. */
        result.pv.clear();
        Node* node = best;
        while(node){
            result.pv.push_back(node->move);
            Node* nb = nullptr;
            for(Node* c : node->children){
                if(!nb || c->N > nb->N){
                    nb = c;
                }
            }
            if(!nb || nb->N == 0){
                break;
            }
            node = nb;
        }
    } else {
        result.pv = {result.best_move};
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    result.score = score_cp;
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    result.time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    free_tree(root);
    return result;
}


/*============================================================
 * MCTS — default_params / param_defs
 *============================================================*/
ParamMap MCTS::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
        {"McEvalRollout", "true"},
        {"McUctC", "141"},
        {"McSimsPerDepth", "1500"},
        {"McMaxSims", "400000"},
        {"McRolloutDepth", "24"},
        {"McEvalScale", "200"},
        {"Tempo", "6"},
        {"PassedPawnScale", "100"},
    };
}

std::vector<ParamDef> MCTS::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"McEvalRollout", ParamDef::CHECK, "true"},
        {"McUctC", ParamDef::SPIN, "141", 0, 1000},
        {"McSimsPerDepth", ParamDef::SPIN, "1500", 1, 1000000},
        {"McMaxSims", ParamDef::SPIN, "400000", 1, 100000000},
        {"McRolloutDepth", ParamDef::SPIN, "24", 1, 200},
        {"McEvalScale", ParamDef::SPIN, "200", 1, 10000},
        {"Tempo", ParamDef::SPIN, "6", -100, 100},
        {"PassedPawnScale", ParamDef::SPIN, "100", 0, 1000},
    };
}
