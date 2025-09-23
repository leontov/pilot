#include "tfidf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// Простая токенизация: разделители — ASCII-пробелы и пунктуация.
// Возвращает массив уникальных токенов и их частот.
static void tokenize_counts(const char* s, char*** toks_out, int** counts_out, int* n_out) {
    char *buf = NULL;
    char *p = NULL;
    size_t len = strlen(s);
    // Рабочая копия (модифицируемая)
    buf = malloc(len + 1);
    if (!buf) { *toks_out = NULL; *counts_out = NULL; *n_out = 0; return; }
    memcpy(buf, s, len + 1);

    // Список уникальных токенов
    char **tokens = NULL;
    int *counts = NULL;
    int n = 0;

    p = buf;
    while (*p) {
        // Пропускаем ASCII разделители
        while (*p && (unsigned char)*p <= 0x7F && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *start = p;
        // Собираем токен до следующего ASCII-разделителя
        while (*p) {
            unsigned char c = (unsigned char)*p;
            if (c <= 0x7F && (isspace(c) || ispunct(c))) break;
            p++;
        }
        // Обрезаем токен
        char saved = *p;
        *p = '\0';
        if (start[0] != '\0') {
            // Ищем в списке
            int found = 0;
            for (int i = 0; i < n; ++i) {
                if (strcmp(tokens[i], start) == 0) {
                    counts[i]++;
                    found = 1; break;
                }
            }
            if (!found) {
                char **nt = realloc(tokens, sizeof(char*) * (n+1));
                int *nc = realloc(counts, sizeof(int) * (n+1));
                if (!nt || !nc) { free(nt); free(nc); break; }
                tokens = nt; counts = nc;
                tokens[n] = strdup(start);
                counts[n] = 1;
                n++;
            }
        }
        if (saved == '\0') break;
        p++;
    }

    free(buf);
    *toks_out = tokens;
    *counts_out = counts;
    *n_out = n;
}

static void free_token_counts(char** toks, int* counts, int n) {
    if (!toks) return;
    for (int i = 0; i < n; ++i) free(toks[i]);
    free(toks);
    free(counts);
}

double tfidf_cosine_similarity(const char* a, const char* b) {
    if (!a || !b) return 0.0;
    if (a[0] == '\0' || b[0] == '\0') return 0.0;

    char **ta = NULL, **tb = NULL;
    int *ca = NULL, *cb = NULL;
    int na = 0, nb = 0;

    tokenize_counts(a, &ta, &ca, &na);
    tokenize_counts(b, &tb, &cb, &nb);

    if (na == 0 || nb == 0) {
        free_token_counts(ta, ca, na);
        free_token_counts(tb, cb, nb);
        return 0.0;
    }

    // Вычисляем скалярное произведение и нормы
    double dot = 0.0;
    double na_norm = 0.0;
    double nb_norm = 0.0;

    for (int i = 0; i < na; ++i) na_norm += (double)ca[i] * (double)ca[i];
    for (int j = 0; j < nb; ++j) nb_norm += (double)cb[j] * (double)cb[j];

    for (int i = 0; i < na; ++i) {
        for (int j = 0; j < nb; ++j) {
            if (strcmp(ta[i], tb[j]) == 0) {
                dot += (double)ca[i] * (double)cb[j];
            }
        }
    }

    free_token_counts(ta, ca, na);
    free_token_counts(tb, cb, nb);

    if (na_norm <= 0.0 || nb_norm <= 0.0) return 0.0;
    double sim = dot / (sqrt(na_norm) * sqrt(nb_norm));
    if (sim < 0.0) sim = 0.0;
    if (sim > 1.0) sim = 1.0;
    return sim;
}
