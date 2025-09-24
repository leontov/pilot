/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

#include "synthesis/search.h"

#include "formula.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>

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
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, candidate.id);
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
