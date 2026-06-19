/*============================================================
 * NNUE training-data generator
 *
 * Plays self-play games with the PVS engine and records, for every
 * non-terminal position visited, a line:
 *
 *     <encoded_board> <side_to_move> <score_cp>
 *
 * where <score_cp> is the PVS search score at a fixed depth, from the
 * side-to-move's perspective (exactly what State::evaluate returns,
 * which is what the NNUE learns to predict).
 *
 * Opening plies are randomised and a small fraction of later moves are
 * played randomly, so the data set covers a wide spread of positions
 * rather than one repeated principal variation.
 *
 * Usage:
 *   minichess-datagen <out_file> [games] [depth] [seed] [open_plies] [max_plies]
 *============================================================*/
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <string>

#include "config.hpp"
#include "state.hpp"
#include "./policy/registry.hpp"
#include "./policy/game_history.hpp"

static const int   SCORE_CLAMP   = 3000;   /* clamp labels to +/- this (cp) */
static const double RANDOM_MOVE_P = 0.10;  /* chance to play a random (non-best) move */

/* A position is treated as "quiet" when the engine's chosen (best) move is
 * neither a capture nor a promotion: the principal continuation is positional,
 * so the static label is meaningful rather than tactically driven. */
static bool move_is_quiet(State* s, const Move& m){
    if((int)m.second.first >= BOARD_H){
        return false;   /* promotion (dest row encodes promo index) */
    }
    int to_r = (int)m.second.first, to_c = (int)m.second.second;
    if(s->piece_at(1 - s->player, to_r, to_c) > 0){
        return false;   /* capture */
    }
    return true;
}

int main(int argc, char* argv[]){
    if(argc < 2){
        std::cerr << "usage: " << argv[0]
                  << " <out_file> [games] [depth] [seed] [open_plies] [max_plies]\n";
        return 1;
    }

    const char* out_path = argv[1];
    int  n_games    = (argc > 2) ? std::atoi(argv[2]) : 2000;
    int  depth      = (argc > 3) ? std::atoi(argv[3]) : 5;
    unsigned seed   = (argc > 4) ? (unsigned)std::atoi(argv[4]) : 1234u;
    int  open_plies = (argc > 5) ? std::atoi(argv[5]) : 4;
    int  max_plies  = (argc > 6) ? std::atoi(argv[6]) : 60;
    bool quiet_only = (argc > 7) && std::string(argv[7]) == "quiet";

    srand(seed);

    const AlgoEntry* algo = find_algo("pvs");
    if(!algo){
        std::cerr << "pvs algorithm not found in registry\n";
        return 1;
    }

    std::ofstream out(out_path);
    if(!out){
        std::cerr << "cannot open output file: " << out_path << "\n";
        return 1;
    }

    long total_positions = 0;

    for(int g = 0; g < n_games; g++){
        GameHistory history;
        State* state = new State();
        state->get_legal_actions();
        history.push(state->hash());

        for(int ply = 0; ply < max_plies; ply++){
            if(state->game_state == WIN || state->game_state == DRAW){
                break;
            }
            if(state->legal_actions.empty()){
                break;
            }

            Move chosen;
            bool record = (ply >= open_plies);

            if(ply < open_plies){
                /* Random opening for diversity — no label needed. */
                int idx = rand() % (int)state->legal_actions.size();
                chosen = state->legal_actions[idx];
            }else{
                /* Run PVS to get both a move and a label score. */
                SearchContext ctx;
                ctx.params = algo->default_params;
                SearchResult res = algo->search(state, depth, history, ctx);

                int score = res.score;
                /* Skip near-mate / terminal-ish scores: clamp instead. */
                if(score >  SCORE_CLAMP) score =  SCORE_CLAMP;
                if(score < -SCORE_CLAMP) score = -SCORE_CLAMP;

                /* Only record quiescent positions when requested — static
                 * eval is only meaningful where the line isn't tactical. */
                if(!quiet_only || move_is_quiet(state, res.best_move)){
                    out << state->encode_board() << ' '
                        << state->player << ' '
                        << score << '\n';
                    total_positions++;
                }

                /* Mostly follow the engine's move; occasionally diverge. */
                if(((double)rand() / (double)RAND_MAX) < RANDOM_MOVE_P){
                    int idx = rand() % (int)state->legal_actions.size();
                    chosen = state->legal_actions[idx];
                }else{
                    chosen = res.best_move;
                }
                (void)record;
            }

            State* next = state->next_state(chosen);
            next->get_legal_actions();
            delete state;
            state = next;
            history.push(state->hash());
        }

        delete state;

        if((g + 1) % 50 == 0 || g + 1 == n_games){
            std::cerr << "\rgames " << (g + 1) << "/" << n_games
                      << "  positions " << total_positions << std::flush;
        }
    }

    std::cerr << "\nwrote " << total_positions << " positions to " << out_path << "\n";
    out.close();
    return 0;
}
