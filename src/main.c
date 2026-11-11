#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "lexer.h"
#include "platform.h"
#include "token.h"

static int u64_is_zero(const u64 *v)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (v->w[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static unsigned u64_div10(u64 *v)
{
    unsigned long carry;
    int i;

    carry = 0;
    for (i = 3; i >= 0; i--) {
        unsigned long cur;
        cur = (carry << 16) | (unsigned long)v->w[i];
        v->w[i] = (u16)(cur / 10ul);
        carry = cur % 10ul;
    }
    return (unsigned)carry;
}

static void print_u64(u64 v)
{
    char buf[32];
    int n;

    n = 0;
    if (u64_is_zero(&v)) {
        putchar('0');
        return;
    }

    while (!u64_is_zero(&v)) {
        unsigned digit;
        digit = u64_div10(&v);
        buf[n++] = (char)('0' + digit);
    }

    while (n > 0) {
        n -= 1;
        putchar(buf[n]);
    }
}

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

int main(int argc, char **argv)
{
    unsigned char *src;
    size_t len;
    Arena arena;
    Lexer lx;
    Token t;
    int errors;

    if (argc != 2) {
        fprintf(stderr, "usage: idem <file.idem>\n");
        return 2;
    }

    src = read_file(argv[1], &len);
    if (src == NULL) {
        return 1;
    }

    arena_init(&arena);
    lexer_init(&lx, argv[1], src, len, &arena);

    errors = 0;
    for (;;) {
        t = lexer_next(&lx);

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
            print_u64(t.as.int_lit.value);
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

    arena_free(&arena);
    free(src);
    return errors != 0 ? 1 : 0;
}
