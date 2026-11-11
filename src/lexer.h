#ifndef IDEM_LEXER_H
#define IDEM_LEXER_H

#include <stddef.h>

#include "arena.h"
#include "platform.h"
#include "token.h"

typedef struct Lexer {
    const char *file;
    const unsigned char *src;
    size_t len;
    size_t pos;
    u32 line;
    u32 column;
    Arena *arena;

    int utf8_error;
    int utf8_error_emitted;
    u32 utf8_line;
    u32 utf8_column;
    size_t utf8_offset;
} Lexer;

void lexer_init(Lexer *lx, const char *file, const unsigned char *src,
                size_t len, Arena *arena);
Token lexer_next(Lexer *lx);

#endif /* IDEM_LEXER_H */
