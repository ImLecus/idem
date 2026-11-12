#ifndef IDEM_CHECK_H
#define IDEM_CHECK_H

#include <stddef.h>

#include "arena.h"
#include "ast.h"
#include "resolver.h"
#include "token.h"
#include "types.h"

typedef struct Binding Binding;

typedef struct TypeChecker {
    Resolver *resolver;
    Arena *arena;
    Module *module;
    Symbol *function;
    Binding *locals;

    int had_error;
    Span error_span;
    char error_message[512];
} TypeChecker;

void checker_init(TypeChecker *c, Resolver *r, Arena *arena);
int checker_check_program(TypeChecker *c);

int checker_had_error(const TypeChecker *c);
Span checker_error_span(const TypeChecker *c);
const char *checker_error_message(const TypeChecker *c);

#endif /* IDEM_CHECK_H */
