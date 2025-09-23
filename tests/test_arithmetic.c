#include <stdio.h>
#include <string.h>
#include "../src/arithmetic.h"

static void expect(const char *expr, const char *want) {
    char out[128];
    int ok = evaluate_arithmetic(expr, out, sizeof(out));
    if (!ok) {
        printf("FAIL parse: %s -> no parse\n", expr);
        return;
    }
    if (strcmp(out, want) != 0) {
        printf("FAIL: %s -> %s (want %s)\n", expr, out, want);
    } else {
        printf("OK: %s -> %s\n", expr, out);
    }
}

int main(void) {
    expect("2+2", "4");
    expect("3*(4+5)", "27");
    expect("2^10", "1024");
    expect("-5+3", "-2");
    expect("1/0", ""); // should fail to parse or compute
    return 0;
}
