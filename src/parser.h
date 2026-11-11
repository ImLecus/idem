#ifndef IDEM_PARSER_H
#define IDEM_PARSER_H

#include <stddef.h>

#include "arena.h"
#include "ast.h"
#include "lexer.h"
#include "token.h"

typedef struct Parser {
    Lexer *lexer;
    Arena *arena;
    Token current;

    int had_error;
    Span error_span;
    char error_message[256];
} Parser;

void parser_init(Parser *p, Lexer *lx, Arena *arena);
AstProgram *parser_parse_program(Parser *p);

int parser_had_error(const Parser *p);
Span parser_error_span(const Parser *p);
const char *parser_error_message(const Parser *p);

#endif /* IDEM_PARSER_H */
