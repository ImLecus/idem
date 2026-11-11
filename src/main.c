#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "lexer.h"
#include "platform.h"
#include "resolver.h"
#include "token.h"

static unsigned char *read_file(const char *path, size_t *out_len)
{
    FILE *f;
    long size;
    unsigned char *buf;
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "idem: cannot open '%s'\n", path);
        return NULL;
    }

    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "idem: cannot seek '%s'\n", path);
        return NULL;
    }

    size = ftell(f);
    if (size < 0) {
        fclose(f);
        fprintf(stderr, "idem: cannot determine size of '%s'\n", path);
        return NULL;
    }

    rewind(f);

    buf = (unsigned char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        fprintf(stderr, "idem: out of memory\n");
        return NULL;
    }

    n = fread(buf, 1, (size_t)size, f);
    fclose(f);

    if (n != (size_t)size) {
        free(buf);
        fprintf(stderr, "idem: cannot read '%s'\n", path);
        return NULL;
    }

    buf[n] = 0;
    *out_len = n;
    return buf;
}

static int dump_tokens(Lexer *lx)
{
    Token t;
    int errors;

    errors = 0;
    for (;;) {
        t = lexer_next(lx);

        printf("%lu:%lu: %s",
               (unsigned long)t.span.line,
               (unsigned long)t.span.column,
               token_kind_name(t.kind));

        switch (t.kind) {
        case TOK_IDENT:
            printf(" %.*s", (int)t.as.ident.len, t.as.ident.text);
            break;
        case TOK_INT:
            printf(" ");
            u64_print(t.as.int_lit.value);
            break;
        case TOK_FLOAT:
            printf(" %.17g", t.as.float_lit.value);
            break;
        case TOK_CHAR:
            printf(" U+%04lX", (unsigned long)t.as.char_lit.codepoint);
            break;
        case TOK_STRING:
        case TOK_BYTES:
            printf(" len=%lu", (unsigned long)t.as.str_lit.len);
            break;
        case TOK_ERROR:
            printf(" %s", t.as.error);
            break;
        default:
            break;
        }

        printf("\n");

        if (t.kind == TOK_ERROR) {
            errors += 1;
            break;
        }
        if (t.kind == TOK_EOF) {
            break;
        }
    }

    return errors != 0 ? 1 : 0;
}

int main(int argc, char **argv)
{
    unsigned char *src;
    size_t len;
    Arena arena;
    Lexer lx;
    const char *path;
    int tokens_only;

    tokens_only = 0;
    if (argc == 3 && strcmp(argv[1], "--tokens") == 0) {
        tokens_only = 1;
        path = argv[2];
    } else if (argc == 2) {
        path = argv[1];
    } else {
        fprintf(stderr, "usage: idem [--tokens] <file.idem>\n");
        return 2;
    }

    if (tokens_only) {
        int status;

        src = read_file(path, &len);
        if (src == NULL) {
            return 1;
        }

        arena_init(&arena);
        lexer_init(&lx, path, src, len, &arena);
        status = dump_tokens(&lx);
        arena_free(&arena);
        free(src);
        return status;
    } else {
        Resolver resolver;
        Span span;

        arena_init(&arena);
        resolver_init(&resolver, &arena);

        if (!resolver_resolve(&resolver, path)) {
            span = resolver_error_span(&resolver);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    resolver_error_message(&resolver));
            arena_free(&arena);
            return 1;
        }

        ast_dump_program(resolver_root(&resolver)->ast);
        arena_free(&arena);
        return 0;
    }
}
