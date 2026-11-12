#include <math.h>
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "lexer.h"
#include "platform.h"
#include "token.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n",                         \
                    __FILE__, __LINE__, #cond);                         \
            failures += 1;                                              \
        }                                                               \
    } while (0)

static void test_keywords(void)
{
    const char *src;
    TokenKind expected[] = {
        TOK_KW_TYPE, TOK_KW_STRUCT, TOK_KW_ENUM, TOK_KW_FN,
        TOK_KW_LET, TOK_KW_IF, TOK_KW_THEN, TOK_KW_ELSE,
        TOK_KW_MATCH, TOK_KW_IMPORT, TOK_KW_TRUE, TOK_KW_FALSE,
        TOK_IDENT, TOK_UNDERSCORE, TOK_IDENT, TOK_EOF
    };
    Arena arena;
    Lexer lx;
    Token t;
    size_t i;

    src = "type struct enum fn let if then else match import true false foo _ _bar";
    arena_init(&arena);
    lexer_init(&lx, "<keywords>", (const unsigned char *)src,
               strlen(src), &arena);

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        t = lexer_next(&lx);
        CHECK(t.kind == expected[i]);
    }

    arena_free(&arena);
}

static void test_identifiers(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;

    src = "foo _bar _";
    arena_init(&arena);
    lexer_init(&lx, "<identifiers>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.as.ident.len == 3);
    CHECK(memcmp(t.as.ident.text, "foo", 3) == 0);
    CHECK(t.span.line == 1);
    CHECK(t.span.column == 1);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.as.ident.len == 4);
    CHECK(memcmp(t.as.ident.text, "_bar", 4) == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_UNDERSCORE);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);
}

static void test_punctuation(void)
{
    const char *src;
    TokenKind expected[] = {
        TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
        TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
        TOK_AND_AND, TOK_OR_OR, TOK_BANG,
        TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_SHL, TOK_SHR,
        TOK_ASSIGN, TOK_ARROW, TOK_FAT_ARROW,
        TOK_COLON, TOK_COMMA, TOK_SEMI, TOK_DOT,
        TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET,
        TOK_LBRACE, TOK_RBRACE, TOK_EOF
    };
    Arena arena;
    Lexer lx;
    Token t;
    size_t i;

    src = "+ - * / % == != < <= > >= && || ! & | ^ ~ << >> = -> => : , ; . ( ) [ ] { }";
    arena_init(&arena);
    lexer_init(&lx, "<punctuation>", (const unsigned char *)src,
               strlen(src), &arena);

    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        t = lexer_next(&lx);
        CHECK(t.kind == expected[i]);
    }

    arena_free(&arena);
}

static void test_numbers(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;

    src = "0 42 1_000 0xFF 0b1010 1.5 1e9 1.5e-3";
    arena_init(&arena);
    lexer_init(&lx, "<numbers>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_INT);
    CHECK(t.as.int_lit.value.w[0] == 0);
    CHECK(t.as.int_lit.value.w[1] == 0);
    CHECK(t.as.int_lit.value.w[2] == 0);
    CHECK(t.as.int_lit.value.w[3] == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_INT);
    CHECK(t.as.int_lit.value.w[0] == 42);
    CHECK(t.as.int_lit.value.w[1] == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_INT);
    CHECK(t.as.int_lit.value.w[0] == 1000);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_INT);
    CHECK(t.as.int_lit.value.w[0] == 255);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_INT);
    CHECK(t.as.int_lit.value.w[0] == 10);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_FLOAT);
    CHECK(t.as.float_lit.value == 1.5);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_FLOAT);
    CHECK(t.as.float_lit.value == 1000000000.0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_FLOAT);
    CHECK(fabs(t.as.float_lit.value - 0.0015) < 1e-15);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);
}

static void test_int_boundaries(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;

    src = "9223372036854775807 0x7FFFFFFFFFFFFFFF";
    arena_init(&arena);
    lexer_init(&lx, "<boundaries>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_INT);
    CHECK(t.as.int_lit.value.w[0] == 0xFFFFu);
    CHECK(t.as.int_lit.value.w[1] == 0xFFFFu);
    CHECK(t.as.int_lit.value.w[2] == 0xFFFFu);
    CHECK(t.as.int_lit.value.w[3] == 0x7FFFu);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_INT);
    CHECK(t.as.int_lit.value.w[0] == 0xFFFFu);
    CHECK(t.as.int_lit.value.w[1] == 0xFFFFu);
    CHECK(t.as.int_lit.value.w[2] == 0xFFFFu);
    CHECK(t.as.int_lit.value.w[3] == 0x7FFFu);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);
}

static void test_comments_and_newlines(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;

    src = "# leading\r\na # trailing\nb\r\nc # eof";
    arena_init(&arena);
    lexer_init(&lx, "<comments>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_NEWLINE);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.span.line == 2);
    CHECK(t.span.column == 1);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_NEWLINE);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.span.line == 3);
    CHECK(t.span.column == 1);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_NEWLINE);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.span.line == 4);
    CHECK(t.span.column == 1);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);

    /* A comment does not have to start at the beginning of a line. */
    src = "a#comment\nb";
    arena_init(&arena);
    lexer_init(&lx, "<inline-comment>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_NEWLINE);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.span.line == 2);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);

    /* A comment at EOF still produces EOF, with no newline required. */
    src = "# only a comment";
    arena_init(&arena);
    lexer_init(&lx, "<comment-eof>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);

    /* '#' inside strings and characters is not a comment. */
    src = "\"# not a comment\" '#' r\"# raw\" # real comment";
    arena_init(&arena);
    lexer_init(&lx, "<comment-in-string>",
               (const unsigned char *)src, strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_STRING);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_CHAR);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_STRING);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);
}

static void test_no_block_comments(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;

    /* '{-' is not a comment: it is an open brace followed by minus. */
    src = "{- -}";
    arena_init(&arena);
    lexer_init(&lx, "<no-block-comments>",
               (const unsigned char *)src, strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_LBRACE);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_MINUS);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_MINUS);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_RBRACE);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);

    src = "#\n{-";
    arena_init(&arena);
    lexer_init(&lx, "<no-block-comment-error>",
               (const unsigned char *)src, strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_NEWLINE);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_LBRACE);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_MINUS);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);
}

static void test_strings_and_chars(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;

    src = "\"hi\\n\" 'a' '\\n' '\\u{1F600}' r\"raw\\n\" b\"A\\x42\"";
    arena_init(&arena);
    lexer_init(&lx, "<strings>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_STRING);
    CHECK(t.as.str_lit.len == 3);
    CHECK(memcmp(t.as.str_lit.data, "hi\n", 3) == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_CHAR);
    CHECK(t.as.char_lit.codepoint == (u32)'a');

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_CHAR);
    CHECK(t.as.char_lit.codepoint == (u32)'\n');

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_CHAR);
    CHECK(t.as.char_lit.codepoint == 0x1F600ul);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_STRING);
    CHECK(t.as.str_lit.len == 5);
    CHECK(memcmp(t.as.str_lit.data, "raw\\n", 5) == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_BYTES);
    CHECK(t.as.str_lit.len == 2);
    CHECK(memcmp(t.as.str_lit.data, "AB", 2) == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);
}

static void test_more_strings(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;

    src = "r\"a\nb\" c \"\\x41\\u{42}\"";
    arena_init(&arena);
    lexer_init(&lx, "<more-strings>", (const unsigned char *)src,
               strlen(src), &arena);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_STRING);
    CHECK(t.as.str_lit.len == 3);
    CHECK(memcmp(t.as.str_lit.data, "a\nb", 3) == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.span.line == 2);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_STRING);
    CHECK(t.as.str_lit.len == 2);
    CHECK(memcmp(t.as.str_lit.data, "AB", 2) == 0);

    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);

    arena_free(&arena);
}

static void test_utf8_and_positions(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Token t;
    unsigned char bad[] = { '"', 0xFF, '"' };
    const char *bom;
    const char *crlf;

    src = "\"\xE2\x98\x83\"";
    arena_init(&arena);
    lexer_init(&lx, "<utf8>", (const unsigned char *)src,
               strlen(src), &arena);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_STRING);
    CHECK(t.as.str_lit.len == 3);
    CHECK(memcmp(t.as.str_lit.data, "\xE2\x98\x83", 3) == 0);
    arena_free(&arena);

    src = "# \xE2\x98\x83\nfoo";
    arena_init(&arena);
    lexer_init(&lx, "<utf8-comment>", (const unsigned char *)src,
               strlen(src), &arena);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_NEWLINE);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.span.line == 2);
    CHECK(t.span.column == 1);
    arena_free(&arena);

    arena_init(&arena);
    lexer_init(&lx, "<bad-utf8>", bad, sizeof(bad), &arena);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_ERROR);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_EOF);
    arena_free(&arena);

    bom = "\xEF\xBB\xBF" "fn";
    arena_init(&arena);
    lexer_init(&lx, "<bom>", (const unsigned char *)bom,
               strlen(bom), &arena);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_KW_FN);
    CHECK(t.span.line == 1);
    CHECK(t.span.column == 1);
    arena_free(&arena);

    crlf = "a\r\nb";
    arena_init(&arena);
    lexer_init(&lx, "<crlf>", (const unsigned char *)crlf,
               strlen(crlf), &arena);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_NEWLINE);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_IDENT);
    CHECK(t.span.line == 2);
    CHECK(t.span.column == 1);
    arena_free(&arena);
}

static void expect_error(const char *src)
{
    Arena arena;
    Lexer lx;
    Token t;

    arena_init(&arena);
    lexer_init(&lx, "<error>", (const unsigned char *)src,
               strlen(src), &arena);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_ERROR);
    arena_free(&arena);
}

static void test_errors(void)
{
    unsigned char bad_bytes[] = { 'b', '"', 0xE2, '"' };
    Arena arena;
    Lexer lx;
    Token t;

    expect_error("@");
    expect_error("\"abc");
    expect_error("\"a\nb\"");
    expect_error("\"\\q\"");
    expect_error("\"\\xFF\"");
    expect_error("9223372036854775808");
    expect_error("999999999999999999999999");
    expect_error("01");
    expect_error("0b2");
    expect_error("1e+");
    expect_error("0x");
    expect_error("''");
    expect_error("'ab'");
    expect_error("b\"\\u{41}\"");
    expect_error("1e999");
    expect_error("0xFFFFFFFFFFFFFFFF");

    arena_init(&arena);
    lexer_init(&lx, "<bad-bytes>", bad_bytes, sizeof(bad_bytes), &arena);
    t = lexer_next(&lx);
    CHECK(t.kind == TOK_ERROR);
    arena_free(&arena);
}

int lexer_test_run(void)
{
    test_keywords();
    test_identifiers();
    test_punctuation();
    test_numbers();
    test_int_boundaries();
    test_comments_and_newlines();
    test_no_block_comments();
    test_strings_and_chars();
    test_more_strings();
    test_utf8_and_positions();
    test_errors();

    if (failures != 0) {
        fprintf(stderr, "%d lexer test failure(s)\n", failures);
        return 1;
    }

    printf("lexer tests passed\n");
    return 0;
}
