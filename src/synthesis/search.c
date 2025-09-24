#include "synthesis/search.h"


#include "formula.h"
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


FormulaSearchConfig formula_search_config_default(void) {
    FormulaSearchConfig cfg = {
        .max_candidates = 32,
        .max_terms = 4,
        .max_coefficient = 9,
        .max_formula_length = 64,
        .base_effectiveness = 0.1,
    };
    return cfg;
}

FormulaMutationConfig formula_mutation_config_default(void) {

    FormulaMutationConfig cfg = {
        .max_mutations = 8,
        .max_adjustment = 3,
    };
    return cfg;

    FormulaMutationConfig cfg;
    cfg.max_mutations = 6;
    cfg.max_adjustment = 2;
    return cfg;
}

FormulaScoreWeights formula_score_weights_default(void) {
    FormulaScoreWeights weights;
    weights.w1 = 0.6;
    weights.w2 = 0.3;
    weights.w3 = 0.07;
    weights.w4 = 0.03;
    return weights;
}

FormulaMctsConfig formula_mcts_config_default(void) {
    FormulaMctsConfig cfg;
    cfg.max_depth = 3;
    cfg.rollouts = 48;
    cfg.exploration = 1.2;
    return cfg;
}

double formula_search_compute_score(const FormulaScoreWeights *weights,
                                   double poe,
                                   double mdl,
                                   double runtime,
                                   double gas_used) {
    FormulaScoreWeights local = weights ? *weights : formula_score_weights_default();
    double poe_clamped = clamp01(poe);
    double mdl_clamped = clamp01(mdl);
    double runtime_clamped = clamp01(runtime);
    double gas_clamped = clamp01(gas_used);
    double score = local.w1 * poe_clamped - local.w2 * mdl_clamped -
                   local.w3 * runtime_clamped - local.w4 * gas_clamped;
    if (score < 0.0) {
        score = 0.0;
    }
    if (score > 1.0) {
        score = 1.0;
    }
    return score;
}

static double compute_formula_heuristic(const Formula *formula,
                                        const FormulaMemorySnapshot *memory) {
    if (!formula) {
        return 0.0;
    }
    double base = clamp01(formula->effectiveness);
    double alignment = compute_alignment(formula->content, memory);
    double novelty = 0.15;
    if (formula->created_at > 0) {
        double age = difftime(time(NULL), formula->created_at);
        if (age > 0.0) {
            novelty = 0.15 + 0.25 / (1.0 + age / 600.0);
        }
    }
    double heuristic = 0.6 * base + 0.3 * alignment + novelty;
    if (heuristic > 1.0) {
        heuristic = 1.0;
    }
    if (heuristic < 0.0) {
        heuristic = 0.0;
    }
    return heuristic;
int formula_search_plan_mcts(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMctsConfig *config,
                             FormulaSearchPlan *out_plan) {
    if (!library || library->count == 0 || !out_plan) {
        return -1;
    }

    FormulaMctsConfig cfg = config ? *config : formula_mcts_config_default();
    size_t actions = library->count;
    if (actions > ARRAY_SIZE(out_plan->actions)) {
        actions = ARRAY_SIZE(out_plan->actions);
    }
    if (actions == 0) {
        return -1;
    }

    double *totals = calloc(actions, sizeof(double));
    uint32_t *visits = calloc(actions, sizeof(uint32_t));
    double *heuristics = calloc(actions, sizeof(double));
    if (!totals || !visits || !heuristics) {
        free(totals);
        free(visits);
        free(heuristics);
        return -1;
    }

    for (size_t i = 0; i < actions; ++i) {
        heuristics[i] = compute_formula_heuristic(&library->formulas[i], memory);
    }

    size_t total_visits = 0;
    for (size_t rollout = 0; rollout < cfg.rollouts; ++rollout) {
        size_t best_index = 0;
        double best_score = -DBL_MAX;
        for (size_t i = 0; i < actions; ++i) {
            double mean = visits[i] ? totals[i] / (double)visits[i] : 0.0;
            double explore = visits[i]
                                  ? cfg.exploration *
                                        sqrt(log((double)(total_visits + 1)) /
                                             (double)visits[i])
                                  : DBL_MAX;
            double ucb = mean + explore;
            if (ucb > best_score) {
                best_score = ucb;
                best_index = i;
            }
        }

        double noise = (double)(rand() % 1000) / 1000.0;
        double reward = heuristics[best_index] + 0.05 * noise;
        if (reward > 1.0) {
            reward = 1.0;
        }
        totals[best_index] += reward;
        visits[best_index] += 1;
        total_visits += 1;
    }

    size_t depth = cfg.max_depth;
    if (depth == 0 || depth > actions) {
        depth = actions;
    }

    double aggregate = 0.0;
    int selected[64] = {0};
    out_plan->length = 0;
    for (size_t d = 0; d < depth; ++d) {
        double best_mean = -1.0;
        size_t best_index = SIZE_MAX;
        for (size_t i = 0; i < actions; ++i) {
            if (selected[i]) {
                continue;
            }
            double mean = visits[i] ? totals[i] / (double)visits[i] : heuristics[i];
            if (mean > best_mean) {
                best_mean = mean;
                best_index = i;
            }
        }
        if (best_index == SIZE_MAX) {
            break;
        }
        selected[best_index] = 1;
        out_plan->actions[out_plan->length++] = (uint32_t)best_index;
        aggregate += best_mean;
    }

    out_plan->value = out_plan->length ? aggregate / (double)out_plan->length : 0.0;

    free(totals);
    free(visits);
    free(heuristics);
    return 0;
}

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;

}

FormulaScoreWeights formula_score_weights_default(void) {
    FormulaScoreWeights weights = {
        .w1 = 1.0,
        .w2 = 1.0,
        .w3 = 0.0,
        .w4 = 0.0,
    };
    return weights;
}

double formula_search_compute_score(const FormulaScoreWeights *weights,
                                   double poe,
                                   double mdl,
                                   double runtime,
                                   double gas_used) {
    FormulaScoreWeights local = weights ? *weights : formula_score_weights_default();
    double score = 0.0;
    score += local.w1 * poe;
    score -= local.w2 * mdl;
    score -= local.w3 * runtime;
    score -= local.w4 * gas_used;
    return score;
}

FormulaMctsConfig formula_mcts_config_default(void) {
    FormulaMctsConfig cfg = {
        .max_depth = 1,
        .rollouts = 0,
        .exploration = 0.0,
    };
    return cfg;
}

int formula_search_plan_mcts(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMctsConfig *config,
                             FormulaSearchPlan *out_plan) {
    (void)library;
    (void)memory;
    if (!out_plan) {
        return -1;
    }
    FormulaMctsConfig local_cfg = config ? *config : formula_mcts_config_default();
    out_plan->length = local_cfg.max_depth > 0 ? 1 : 0;
    for (size_t i = 0; i < out_plan->length && i < sizeof(out_plan->actions) / sizeof(out_plan->actions[0]); ++i) {
        out_plan->actions[i] = 0;
    }
    out_plan->value = 0.0;
    return 0;
}

static size_t emit_library_formulas(const FormulaCollection *library,
                                    formula_search_emit_fn emit,
                                    void *user_data,
                                    size_t limit) {
    if (!library || !emit || limit == 0) {
        return 0;
    }
    size_t emitted = 0;
    for (size_t i = 0; i < library->count && emitted < limit; ++i) {
        if (emit(&library->formulas[i], user_data) != 0) {
            break;
        }
        emitted += 1;
    }
    return emitted;
}

size_t formula_search_enumerate(const FormulaCollection *library,
                                const FormulaMemorySnapshot *memory,
                                const FormulaSearchConfig *config,
                                formula_search_emit_fn emit,
                                void *user_data) {
    (void)memory;
    FormulaSearchConfig local_cfg = config ? *config : formula_search_config_default();
    return emit_library_formulas(library, emit, user_data, local_cfg.max_candidates);
}

size_t formula_search_mutate(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMutationConfig *config,
                             formula_search_emit_fn emit,
                             void *user_data) {
    (void)memory;
    FormulaMutationConfig local_cfg = config ? *config : formula_mutation_config_default();
    (void)local_cfg;
    return emit_library_formulas(library, emit, user_data, library ? library->count : 0);
}

static int emit_mutated_formula(search_context_t *ctx,
                                const char *content,
                                double novelty_bonus) {
    if (!ctx || !content) {
        return 0;
    }

    size_t len = strlen(content);
    if (ctx->config->max_formula_length > 0 &&
        len > ctx->config->max_formula_length) {
        return 0;
    }

    if (library_contains_content(ctx->library, content)) {
        return 0;
    }

    Formula candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.representation = FORMULA_REPRESENTATION_TEXT;
    generate_candidate_id(candidate.id, sizeof(candidate.id));
    strncpy(candidate.content, content, sizeof(candidate.content) - 1);
    candidate.created_at = time(NULL);

    double alignment = compute_alignment(candidate.content, ctx->memory);
    double base = ctx->config->base_effectiveness + novelty_bonus;
    candidate.effectiveness = clamp01(base + 0.35 * alignment);

    ctx->produced++;
    int should_stop = 0;
    if (ctx->emit) {
        should_stop = ctx->emit(&candidate, ctx->user_data);
    }
    if (ctx->limit > 0 && ctx->produced >= ctx->limit) {
        should_stop = 1;
    }
    return should_stop;
}

size_t formula_search_mutate(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMutationConfig *config,
                             formula_search_emit_fn emit,
                             void *user_data) {
    if (!library || library->count == 0 || !emit) {
        return 0;
    }

    FormulaMutationConfig cfg =
        config ? *config : formula_mutation_config_default();
    if (cfg.max_mutations == 0) {
        return 0;
    }

    FormulaSearchConfig search_cfg = formula_search_config_default();
    search_context_t ctx = {
        .library = library,
        .memory = memory,
        .config = &search_cfg,
        .emit = emit,
        .user_data = user_data,
        .produced = 0,
        .limit = cfg.max_mutations,
    };

    size_t order[64];
    size_t order_count = 0;
    FormulaSearchPlan plan;
    if (formula_search_plan_mcts(library, memory, NULL, &plan) == 0) {
        for (size_t i = 0; i < plan.length && order_count < ARRAY_SIZE(order); ++i) {
            uint32_t index = plan.actions[i];
            if (index < library->count) {
                order[order_count++] = index;
            }
        }
    }
    for (size_t i = 0; i < library->count && order_count < ARRAY_SIZE(order); ++i) {
        int seen = 0;
        for (size_t j = 0; j < order_count; ++j) {
            if (order[j] == i) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            order[order_count++] = i;
        }
    }

    int adjustments[8];
    size_t adjustment_count = 0;
    int limit = (int)(cfg.max_adjustment > 0 ? cfg.max_adjustment : 1);
    for (int delta = 1; delta <= limit && adjustment_count + 1 < ARRAY_SIZE(adjustments);
         ++delta) {
        adjustments[adjustment_count++] = delta;
        adjustments[adjustment_count++] = -delta;
    }
    if (adjustment_count == 0) {
        adjustments[adjustment_count++] = 1;
        adjustments[adjustment_count++] = -1;
    }

    char mutated[sizeof(((Formula *)0)->content)];
    for (size_t order_index = 0; order_index < order_count; ++order_index) {
        if (ctx.limit > 0 && ctx.produced >= ctx.limit) {
            break;
        }
        size_t index = order[order_index];
        if (index >= library->count) {
            continue;
        }
        const Formula *source = &library->formulas[index];
        if (source->representation != FORMULA_REPRESENTATION_TEXT ||
            source->content[0] == '\0') {
            continue;
        }

        for (size_t adj = 0; adj < adjustment_count; ++adj) {
            if (ctx.limit > 0 && ctx.produced >= ctx.limit) {
                break;
            }
            int adjustment = adjustments[adj];
            const char *cursor = source->content;
            size_t token_index = 0;
            while (*cursor) {
                const char *start = cursor;
                char *endptr = NULL;
                long value = 0;
                int is_number = 0;
                if (*cursor == '+' || *cursor == '-') {
                    if (isdigit((unsigned char)cursor[1])) {
                        value = strtol(cursor, &endptr, 10);
                        is_number = 1;
                    }
                } else if (isdigit((unsigned char)*cursor)) {
                    value = strtol(cursor, &endptr, 10);
                    is_number = 1;
                }

                if (is_number && endptr) {
                    long mutated_value = value + adjustment;
                    if (mutated_value > 999) {
                        mutated_value = 999;
                    } else if (mutated_value < -999) {
                        mutated_value = -999;
                    }

                    size_t prefix = (size_t)(start - source->content);
                    size_t suffix_len = strlen(endptr);
                    if (prefix >= sizeof(mutated)) {
                        break;
                    }
                    memcpy(mutated, source->content, prefix);
                    size_t pos = prefix;
                    int written = snprintf(mutated + pos,
                                           sizeof(mutated) - pos,
                                           "%ld",
                                           mutated_value);
                    if (written < 0) {
                        break;
                    }
                    pos += (size_t)written;
                    if (pos + suffix_len >= sizeof(mutated)) {
                        break;
                    }
                    memcpy(mutated + pos, endptr, suffix_len + 1);

                    double novelty_bonus = 0.04 * (double)(token_index + 1);
                    if (emit_mutated_formula(&ctx, mutated, novelty_bonus)) {
                        return ctx.produced;
                    }
                }

                if (is_number && endptr) {
                    cursor = endptr;
                    token_index++;
                } else {
                    cursor++;
                }

                if (ctx.limit > 0 && ctx.produced >= ctx.limit) {
                    break;
                }
            }
        }
    }

    return ctx.produced;
}
