#ifndef EMBED_H
#define EMBED_H

#include <stddef.h>

int embed_init(size_t dim);
void embed_text(const char* text, double* out, size_t dim);
void embed_free(void);

#endif // EMBED_H
