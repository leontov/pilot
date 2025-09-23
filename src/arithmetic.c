#include "arithmetic.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

typedef enum { TOK_NUMBER, TOK_OP, TOK_LPAREN, TOK_RPAREN } TokenType;
typedef struct { TokenType type; double value; char op; } Token;

int evaluate_arithmetic(const char *task, char *out, size_t out_sz) {
    if (!task || !out) return 0;
    Token toks[512]; int nt = 0;
    const char *p = task;
    while (*p && nt < (int)(sizeof(toks)/sizeof(toks[0]))) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '(') { toks[nt++] = (Token){TOK_LPAREN,0.0,0}; p++; continue; }
        if (*p == ')') { toks[nt++] = (Token){TOK_RPAREN,0.0,0}; p++; continue; }
        if (strchr("+-*/^", *p)) {
            char op = *p; int is_unary = 0;
            if (op == '+' || op == '-') {
                if (nt == 0) is_unary = 1;
                else if (toks[nt-1].type == TOK_OP || toks[nt-1].type == TOK_LPAREN) is_unary = 1;
            }
            if (is_unary) {
                char *endptr = NULL; errno = 0; double v = strtod(p, &endptr);
                if (endptr == p) return 0; toks[nt++] = (Token){TOK_NUMBER, v, 0}; p = endptr; continue;
            } else { toks[nt++] = (Token){TOK_OP, 0.0, op}; p++; continue; }
        }
        if (isdigit((unsigned char)*p) || *p == '.') {
            char *endptr = NULL; errno = 0; double v = strtod(p, &endptr);
            if (endptr == p) return 0; toks[nt++] = (Token){TOK_NUMBER, v, 0}; p = endptr; continue;
        }
        return 0;
    }
    if (nt == 0) return 0;

    Token outq[512]; int oq = 0; Token opstack[512]; int ops = 0;
    for (int i = 0; i < nt; ++i) {
        Token t = toks[i];
        if (t.type == TOK_NUMBER) outq[oq++] = t;
        else if (t.type == TOK_OP) {
            int prec; int right_assoc = 0;
            switch (t.op) { case '+': case '-': prec=1; break; case '*': case '/': prec=2; break; case '^': prec=3; right_assoc=1; break; default: return 0; }
            while (ops > 0 && opstack[ops-1].type == TOK_OP) {
                char top = opstack[ops-1].op; int tprec; int tright = 0;
                switch (top) { case '+': case '-': tprec=1; break; case '*': case '/': tprec=2; break; case '^': tprec=3; tright=1; break; default: tprec=0; }
                if ((right_assoc == 0 && prec <= tprec) || (right_assoc == 1 && prec < tprec)) { outq[oq++] = opstack[--ops]; } else break;
            }
            opstack[ops++] = t;
        } else if (t.type == TOK_LPAREN) opstack[ops++] = t;
        else if (t.type == TOK_RPAREN) { int found = 0; while (ops > 0) { Token top = opstack[--ops]; if (top.type == TOK_LPAREN) { found = 1; break; } outq[oq++] = top; } if (!found) return 0; }
    }
    while (ops > 0) { Token top = opstack[--ops]; if (top.type == TOK_LPAREN || top.type == TOK_RPAREN) return 0; outq[oq++] = top; }

    double stackv[512]; int sv = 0;
    for (int i = 0; i < oq; ++i) {
        Token t = outq[i]; if (t.type == TOK_NUMBER) stackv[sv++] = t.value; else if (t.type == TOK_OP) {
            if (sv < 2) return 0; double b = stackv[--sv]; double a = stackv[--sv]; double r = 0.0; switch (t.op) { case '+': r=a+b; break; case '-': r=a-b; break; case '*': r=a*b; break; case '/': if (b==0.0) return 0; r=a/b; break; case '^': r=pow(a,b); break; default: return 0; } stackv[sv++] = r; }
    }
    if (sv != 1) return 0; double res = stackv[0]; if (!isfinite(res)) return 0;
    if (fabs(res - round(res)) < 1e-9) snprintf(out, out_sz, "%.0f", round(res)); else { snprintf(out, out_sz, "%.*f", 8, res); size_t L = strlen(out); while (L>0 && out[L-1]=='0') out[--L]='\0'; if (L>0 && out[L-1]=='.') out[--L]='\0'; }
    return 1;
}
