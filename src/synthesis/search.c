/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

#include "synthesis/search.h"

#include "formula.h"

#include <ctype.h>
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


static double clamp01(double value);

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
    weights.w1 = 0.55;
    weights.w2 = 0.25;
    weights.w3 = 0.15;
    weights.w4 = 0.05;
    return weights;
}

FormulaMctsConfig formula_mcts_config_default(void) {
    FormulaMctsConfig cfg;
    cfg.max_depth = 4;
    cfg.rollouts = 64;
    cfg.exploration = 1.2;
    return cfg;
}

double formula_search_compute_score(const FormulaScoreWeights *weights,
                                   double poe,
                                   double mdl,
                                   double runtime,
                                   double gas_used) {
    FormulaScoreWeights local = weights ? *weights : formula_score_weights_default();
    double score = 0.0;
    score += local.w1 * clamp01(poe);
    score -= local.w2 * clamp01(mdl);
    score -= local.w3 * clamp01(runtime);
    score -= local.w4 * clamp01(gas_used);
    if (score < 0.0) {
        return 0.0;
    }
    if (score > 1.0) {
        return 1.0;
    }
    return score;
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


}


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

int formula_search_plan_mcts(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMctsConfig *config,
                             FormulaSearchPlan *out_plan) {
    (void)memory;
    if (!library || !out_plan) {
        return -1;
    }
    FormulaMctsConfig local = config ? *config : formula_mcts_config_default();
    if (local.max_depth == 0) {
        local.max_depth = formula_mcts_config_default().max_depth;
    }
    memset(out_plan, 0, sizeof(*out_plan));
    const size_t max_actions = ARRAY_SIZE(out_plan->actions);
    const size_t target = local.max_depth < max_actions ? local.max_depth : max_actions;

    const Formula *best[ARRAY_SIZE(((FormulaCollection *)0)->best_indices)];
    size_t best_count = formula_collection_get_top(library, best, target);
    if (best_count == 0 && library->count > 0) {
        size_t fallback = library->count < target ? library->count : target;
        for (size_t i = 0; i < fallback; ++i) {
            best[i] = &library->formulas[i];
        }
        best_count = fallback;
    }

    for (size_t i = 0; i < best_count; ++i) {
        const Formula *formula = best[i];
        size_t index = (size_t)(formula - library->formulas);
        out_plan->actions[out_plan->length++] = (uint32_t)index;
        out_plan->value += formula->effectiveness;
    }
    if (out_plan->length > 0) {
        out_plan->value /= (double)out_plan->length;
    }
    return out_plan->length > 0 ? 0 : -1;
}

size_t formula_search_mutate(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMutationConfig *config,
                             formula_search_emit_fn emit,
                             void *user_data) {
    if (!library || library->count == 0 || !emit) {
        return 0;
    }
    FormulaMutationConfig local = config ? *config : formula_mutation_config_default();
    if (local.max_mutations == 0) {
        local.max_mutations = formula_mutation_config_default().max_mutations;
    }
    if (local.max_adjustment == 0) {
        local.max_adjustment = formula_mutation_config_default().max_adjustment;
    }

    size_t produced = 0;
    for (size_t i = 0; i < library->count && produced < local.max_mutations; ++i) {
        const Formula *base = &library->formulas[i];
        if (base->representation != FORMULA_REPRESENTATION_TEXT) {
            continue;
        }
        double alignment = compute_alignment(base->content, memory);
        for (int delta = 1; delta <= (int)local.max_adjustment &&
                                produced < local.max_mutations;
             ++delta) {
            for (int direction = -1; direction <= 1 && produced < local.max_mutations;
                 direction += 2) {
                Formula mutated;
                memset(&mutated, 0, sizeof(mutated));
                mutated.representation = FORMULA_REPRESENTATION_TEXT;
                generate_candidate_id(mutated.id, sizeof(mutated.id));

                int adjustment = direction * delta;
                snprintf(mutated.content,
                         sizeof(mutated.content),
                         "%s %c %d",
                         base->content,
                         adjustment >= 0 ? '+' : '-',
                         adjustment >= 0 ? adjustment : -adjustment);

                if (library_contains_content(library, mutated.content)) {
                    continue;
                }

                mutated.effectiveness = clamp01(base->effectiveness + 0.1 * alignment - 0.02 * (double)delta);
                mutated.created_at = time(NULL);

                produced++;
                int stop = emit(&mutated, user_data);
                if (stop) {
                    return produced;
                }
            }

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

    return produced;
}
