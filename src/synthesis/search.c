/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

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
#include <time.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

FormulaSearchConfig formula_search_config_default(void) {
    FormulaSearchConfig config;
    config.max_candidates = 8;
    config.max_terms = 3;
    config.max_coefficient = 5;
    config.max_formula_length = 96;
    config.base_effectiveness = 0.45;
    return config;
}

FormulaMutationConfig formula_mutation_config_default(void) {
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

static void to_lower_copy(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) {
        return;
    }
    size_t i = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; src[i] != '\0' && i + 1 < dst_size; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static double compute_token_overlap(const char *content_lower, const char *description) {
    if (!content_lower || !description || description[0] == '\0') {
        return 0.0;
    }

    char buffer[256];
    to_lower_copy(description, buffer, sizeof(buffer));

    const char *delim = " ,.;:-_";
    char *saveptr = NULL;
    char *token = strtok_r(buffer, delim, &saveptr);
    size_t tokens = 0;
    size_t matches = 0;

    while (token) {
        tokens++;
        if (strstr(content_lower, token)) {
            matches++;
        }
        token = strtok_r(NULL, delim, &saveptr);
    }

    if (tokens == 0) {
        return 0.0;
    }
    return (double)matches / (double)tokens;
}

static double compute_alignment(const char *content,
                                const FormulaMemorySnapshot *memory) {
    if (!content || !memory || memory->count == 0) {
        return 0.0;
    }

    char normalized[sizeof(((Formula *)0)->content)];
    to_lower_copy(content, normalized, sizeof(normalized));

    double weighted_sum = 0.0;
    double weight_total = 0.0;

    for (size_t i = 0; i < memory->count; ++i) {
        const FormulaMemoryFact *fact = &memory->facts[i];
        double weight = fact->importance > 0.0 ? fact->importance : 0.1;
        double overlap = compute_token_overlap(normalized, fact->description);
        double reward_bias = fact->reward > 0.0 ? fact->reward : 0.0;
        weighted_sum += weight * (0.6 * overlap + 0.4 * clamp01(reward_bias));
        weight_total += weight;
    }

    if (weight_total <= 0.0) {
        return 0.0;
    }

    return clamp01(weighted_sum / weight_total);
}

static int library_contains_content(const FormulaCollection *library, const char *content) {
    if (!library || !content) {
        return 0;
    }

    for (size_t i = 0; i < library->count; ++i) {
        const Formula *formula = &library->formulas[i];
        if (formula->representation != FORMULA_REPRESENTATION_TEXT) {
            continue;
        }
        if (strcmp(formula->content, content) == 0) {
            return 1;
        }
    }
    return 0;
}

typedef struct {
    const FormulaCollection *library;
    const FormulaMemorySnapshot *memory;
    const FormulaSearchConfig *config;
    formula_search_emit_fn emit;
    void *user_data;
    size_t produced;
    size_t limit;
} search_context_t;

static void generate_candidate_id(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }

    struct timespec ts;
    unsigned long long timestamp_mix = 0;
#if defined(CLOCK_REALTIME)
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        timestamp_mix = ((unsigned long long)ts.tv_sec << 32) ^
                        (unsigned long long)ts.tv_nsec;
    } else {
        timestamp_mix = (unsigned long long)time(NULL);
    }
#else
    timestamp_mix = (unsigned long long)time(NULL);
#endif

    static atomic_uint_fast64_t counter = 0;
    uint64_t local_counter =
        atomic_fetch_add_explicit(&counter, 1u, memory_order_relaxed) + 1u;

    uint64_t mix = timestamp_mix ^ (local_counter << 16);
    mix ^= (uint64_t)(uintptr_t)buffer;

    if (size >= 37) {
        uint32_t segment1 = (uint32_t)(mix & 0xffffffffu);
        uint16_t segment2 = (uint16_t)((mix >> 32) & 0xffffu);
        uint16_t segment3 = (uint16_t)((mix >> 48) & 0xffffu);
        uint16_t segment4 =
            (uint16_t)(((mix >> 20) ^ segment2 ^ segment3) & 0xffffu);
        uint64_t segment5 =
            ((timestamp_mix << 16) ^ local_counter ^ segment1) & 0xffffffffffffULL;

        snprintf(buffer, size, "%08x-%04x-%04x-%04x-%012llx", segment1, segment2,
                 segment3, segment4, (unsigned long long)segment5);
    } else {
        snprintf(buffer, size, "%016llx",
                 (unsigned long long)(mix ^ (timestamp_mix << 8)));
    }
}

static int emit_formula(search_context_t *ctx,
                        const int *coeffs,
                        size_t term_count) {
    if (!ctx || !coeffs || term_count == 0) {
        return 0;
    }

    char expression[256];
    size_t len = 0;
    int first_term_written = 0;

    len = snprintf(expression, sizeof(expression), "f(x) = ");
    if (len >= sizeof(expression)) {
        return 0;
    }

    for (size_t i = 0; i < term_count; ++i) {
        int coeff = coeffs[i];
        size_t power = term_count - i - 1;
        if (coeff == 0 && power > 0) {
            continue;
        }

        char term[64];
        int abs_coeff = coeff < 0 ? -coeff : coeff;
        const char *sign_prefix;

        if (!first_term_written) {
            sign_prefix = (coeff < 0) ? "-" : "";
        } else {
            sign_prefix = (coeff < 0) ? " - " : " + ";
        }

        if (power == 0) {
            if (abs_coeff == 0 && first_term_written) {
                continue;
            }
            if (abs_coeff == 0 && !first_term_written) {
                snprintf(term, sizeof(term), "%s0", sign_prefix);
            } else {
                snprintf(term, sizeof(term), "%s%d", sign_prefix, abs_coeff);
            }
        } else if (power == 1) {
            if (abs_coeff == 1) {
                snprintf(term, sizeof(term), "%sx", sign_prefix);
            } else {
                snprintf(term, sizeof(term), "%s%d*x", sign_prefix, abs_coeff);
            }
        } else {
            if (abs_coeff == 1) {
                snprintf(term, sizeof(term), "%sx^%zu", sign_prefix, power);
            } else {
                snprintf(term, sizeof(term), "%s%d*x^%zu", sign_prefix, abs_coeff, power);
            }
        }

        size_t term_len = strlen(term);
        if (len + term_len >= sizeof(expression)) {
            return 0;
        }
        memcpy(expression + len, term, term_len);
        len += term_len;
        expression[len] = '\0';
        first_term_written = 1;
    }

    if (!first_term_written) {
        snprintf(expression, sizeof(expression), "f(x) = 0");
        len = strlen(expression);
    }

    if (ctx->config->max_formula_length > 0 && len > ctx->config->max_formula_length) {
        return 0;
    }

    if (library_contains_content(ctx->library, expression)) {
        return 0;
    }

    Formula candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.representation = FORMULA_REPRESENTATION_TEXT;
    generate_candidate_id(candidate.id, sizeof(candidate.id));
    strncpy(candidate.content, expression, sizeof(candidate.content) - 1);
    candidate.created_at = time(NULL);

    double complexity_penalty = (term_count > 1) ? 0.05 * (double)(term_count - 1) : 0.0;
    double alignment = compute_alignment(candidate.content, ctx->memory);
    double effectiveness = ctx->config->base_effectiveness + 0.3 * alignment - complexity_penalty;
    candidate.effectiveness = clamp01(effectiveness);

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

static int generate_terms(search_context_t *ctx,
                          size_t term_count,
                          size_t depth,
                          int *coeffs,
                          int min_coeff,
                          int max_coeff) {
    if (depth == term_count) {
        return emit_formula(ctx, coeffs, term_count);
    }

    for (int coeff = min_coeff; coeff <= max_coeff; ++coeff) {
        coeffs[depth] = coeff;
        if (ctx->limit > 0 && ctx->produced >= ctx->limit) {
            return 1;
        }
        if (generate_terms(ctx, term_count, depth + 1, coeffs, min_coeff, max_coeff)) {
            return 1;
        }
    }
    return 0;
}

size_t formula_search_enumerate(const FormulaCollection *library,
                                const FormulaMemorySnapshot *memory,
                                const FormulaSearchConfig *config,
                                formula_search_emit_fn emit,
                                void *user_data) {
    FormulaSearchConfig local_config = config ? *config : formula_search_config_default();
    if (local_config.max_terms == 0) {
        local_config.max_terms = formula_search_config_default().max_terms;
    }
    if (local_config.max_coefficient == 0) {
        local_config.max_coefficient = formula_search_config_default().max_coefficient;
    }
    if (local_config.max_formula_length == 0) {
        local_config.max_formula_length = formula_search_config_default().max_formula_length;
    }
    if (local_config.max_candidates == 0) {
        local_config.max_candidates = formula_search_config_default().max_candidates;
    }

    search_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.library = library;
    ctx.memory = memory;
    ctx.config = &local_config;
    ctx.emit = emit;
    ctx.user_data = user_data;
    ctx.limit = local_config.max_candidates;

    int min_coeff = -(int)local_config.max_coefficient;
    int max_coeff = (int)local_config.max_coefficient;

    int coeffs[8];
    if (local_config.max_terms > ARRAY_SIZE(coeffs)) {
        local_config.max_terms = ARRAY_SIZE(coeffs);
    }

    for (size_t term_count = 1; term_count <= local_config.max_terms; ++term_count) {
        if (generate_terms(&ctx, term_count, 0, coeffs, min_coeff, max_coeff)) {
            break;
        }
    }

    return ctx.produced;
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
