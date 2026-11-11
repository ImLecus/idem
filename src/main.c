#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

#include "arena.h"
#include "ast.h"
#include "check.h"
#include "codegen.h"
#include "ir.h"
#include "lexer.h"
#include "platform.h"
#include "resolver.h"
#include "token.h"

#ifndef IDEM_RUNTIME_DIR
#define IDEM_RUNTIME_DIR "runtime"
#endif

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

static int print_file(const char *path)
{
    FILE *f;
    char buffer[4096];
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "idem: cannot read generated C\n");
        return 0;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), f)) != 0) {
        fwrite(buffer, 1, n, stdout);
    }
    fclose(f);
    return 1;
}

static void append_text(char *buffer, size_t cap, const char *text)
{
    size_t used;
    used = strlen(buffer);
    if (used + 1 < cap) {
        strncat(buffer, text, cap - used - 1);
    }
}

static void append_quoted(char *buffer, size_t cap, const char *text)
{
    size_t used;
    const char *p;

    used = strlen(buffer);
    if (used + 2 >= cap) {
        return;
    }
    buffer[used++] = '\'';
    buffer[used] = '\0';

    for (p = text; *p != '\0'; p++) {
        if (*p == '\'') {
            append_text(buffer, cap, "'\\''");
        } else {
            char one[2];
            one[0] = *p;
            one[1] = '\0';
            append_text(buffer, cap, one);
        }
    }
    append_text(buffer, cap, "'");
}

static int command_status(int status)
{
#ifdef WIFEXITED
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
#else
    return status == 0 ? 0 : 1;
#endif
}

static int compile_and_run(const char *c_path, const char *exe_path,
                           int argc, char **argv, int first_arg)
{
    char command[8192];
    const char *cc;
    int status;
    int i;

    cc = getenv("CC");
    if (cc == NULL || *cc == '\0') {
        cc = "cc";
    }

    sprintf(command, "%s -std=c89 -pedantic -Wall -Wextra -O2 -I", cc);
    append_quoted(command, sizeof(command), IDEM_RUNTIME_DIR);
    append_text(command, sizeof(command), " -o ");
    append_quoted(command, sizeof(command), exe_path);
    append_text(command, sizeof(command), " ");
    append_quoted(command, sizeof(command), c_path);
    append_text(command, sizeof(command), " ");
    append_quoted(command, sizeof(command), IDEM_RUNTIME_DIR);
    append_text(command, sizeof(command), "/*.c -lm");

    status = system(command);
    if (status != 0) {
        fprintf(stderr, "idem: C compilation failed\n");
        return 1;
    }

    command[0] = '\0';
    append_quoted(command, sizeof(command), exe_path);
    for (i = first_arg; i < argc; i++) {
        append_text(command, sizeof(command), " ");
        append_quoted(command, sizeof(command), argv[i]);
    }

    status = system(command);
    return command_status(status);
}

int main(int argc, char **argv)
{
    const char *path;
    int tokens_only;
    int emit_c;
    int first_arg;

    tokens_only = 0;
    emit_c = 0;
    first_arg = 2;

    if (argc >= 3 && strcmp(argv[1], "--tokens") == 0) {
        tokens_only = 1;
        path = argv[2];
    } else if (argc >= 3 && strcmp(argv[1], "--emit-c") == 0) {
        emit_c = 1;
        path = argv[2];
    } else if (argc == 2 &&
               (strcmp(argv[1], "--tokens") == 0 ||
                strcmp(argv[1], "--emit-c") == 0)) {
        fprintf(stderr,
                "usage: idem [--tokens|--emit-c] <file.idem> [args...]\n");
        return 2;
    } else if (argc >= 2) {
        path = argv[1];
        first_arg = 2;
    } else {
        fprintf(stderr,
                "usage: idem [--tokens|--emit-c] <file.idem> [args...]\n");
        return 2;
    }

    if (tokens_only || emit_c) {
        unsigned char *src;
        size_t len;
        Arena arena;
        Lexer lx;
        Resolver resolver;
        TypeChecker checker;
        IrProgram program;
        Codegen codegen;
        char c_temp[L_tmpnam];
        char c_path[L_tmpnam + 4];

        src = read_file(path, &len);
        if (src == NULL) {
            return 1;
        }

        arena_init(&arena);

        if (tokens_only) {
            int status;
            lexer_init(&lx, path, src, len, &arena);
            status = dump_tokens(&lx);
            arena_free(&arena);
            free(src);
            return status;
        }

        resolver_init(&resolver, &arena);
        if (!resolver_resolve(&resolver, path)) {
            Span span;
            span = resolver_error_span(&resolver);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    resolver_error_message(&resolver));
            arena_free(&arena);
            free(src);
            return 1;
        }

        checker_init(&checker, &resolver, &arena);
        if (!checker_check_program(&checker)) {
            Span span;
            span = checker_error_span(&checker);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    checker_error_message(&checker));
            arena_free(&arena);
            free(src);
            return 1;
        }

        ir_init(&program, &resolver, &arena);
        if (!ir_lower(&program)) {
            Span span;
            span = ir_error_span(&program);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    ir_error_message(&program));
            arena_free(&arena);
            free(src);
            return 1;
        }

        if (tmpnam(c_temp) == NULL) {
            fprintf(stderr, "idem: cannot create a temporary file name\n");
            arena_free(&arena);
            free(src);
            return 1;
        }
        sprintf(c_path, "%s.c", c_temp);

        codegen_init(&codegen, &program, &arena);
        if (!codegen_emit(&codegen, c_path)) {
            fprintf(stderr, "idem: %s\n", codegen_error_message(&codegen));
            arena_free(&arena);
            free(src);
            return 1;
        }

        if (emit_c) {
            int ok;
            ok = print_file(c_path);
            remove(c_path);
            arena_free(&arena);
            free(src);
            return ok ? 0 : 1;
        }

        remove(c_path);
        arena_free(&arena);
        free(src);
        return 0;
    }

    {
        unsigned char *src;
        size_t len;
        Arena arena;
        Resolver resolver;
        TypeChecker checker;
        IrProgram program;
        Codegen codegen;
        char c_temp[L_tmpnam];
        char c_path[L_tmpnam + 4];
        char exe_path[L_tmpnam];
        const char *keep;
        int status;

        src = read_file(path, &len);
        if (src == NULL) {
            return 1;
        }

        arena_init(&arena);
        resolver_init(&resolver, &arena);

        if (!resolver_resolve(&resolver, path)) {
            Span span;
            span = resolver_error_span(&resolver);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    resolver_error_message(&resolver));
            arena_free(&arena);
            free(src);
            return 1;
        }

        checker_init(&checker, &resolver, &arena);
        if (!checker_check_program(&checker)) {
            Span span;
            span = checker_error_span(&checker);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    checker_error_message(&checker));
            arena_free(&arena);
            free(src);
            return 1;
        }

        ir_init(&program, &resolver, &arena);
        if (!ir_lower(&program)) {
            Span span;
            span = ir_error_span(&program);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    ir_error_message(&program));
            arena_free(&arena);
            free(src);
            return 1;
        }

        if (tmpnam(c_temp) == NULL || tmpnam(exe_path) == NULL) {
            fprintf(stderr, "idem: cannot create temporary file names\n");
            arena_free(&arena);
            free(src);
            return 1;
        }
        sprintf(c_path, "%s.c", c_temp);

        codegen_init(&codegen, &program, &arena);
        if (!codegen_emit(&codegen, c_path)) {
            fprintf(stderr, "idem: %s\n", codegen_error_message(&codegen));
            arena_free(&arena);
            free(src);
            return 1;
        }

        keep = getenv("IDEM_KEEP_C");
        if (keep != NULL && *keep != '\0') {
            fprintf(stderr, "idem: generated C at %s\n", c_path);
        }

        status = compile_and_run(c_path, exe_path, argc, argv, first_arg);

        if (keep == NULL || *keep == '\0') {
            remove(c_path);
            remove(exe_path);
        }

        arena_free(&arena);
        free(src);
        return status;
    }
}
