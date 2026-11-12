#include "lexer.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Character and UTF-8 helpers                                         */
/* ------------------------------------------------------------------ */

static int is_ascii_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int is_ascii_alpha(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_ident_start(unsigned char c)
{
    return is_ascii_alpha(c) || c == '_';
}

static int is_ident_continue(unsigned char c)
{
    return is_ascii_alpha(c) || is_ascii_digit(c) || c == '_';
}

static int is_newline_char(unsigned char c)
{
    return c == '\n' || c == '\r';
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a') + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return (int)(c - 'A') + 10;
    }
    return -1;
}

static int digit_value(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a') + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return (int)(c - 'A') + 10;
    }
    return -1;
}

static int utf8_decode(const unsigned char *s, size_t len,
                       size_t *out_len, u32 *out_cp)
{
    unsigned char c;
    u32 cp;
    size_t n;
    size_t i;

    if (len == 0) {
        return 0;
    }

    c = s[0];
    if (c < 0x80) {
        *out_len = 1;
        *out_cp = (u32)c;
        return 1;
    }

    if (c >= 0xC2 && c <= 0xDF) {
        n = 2;
        cp = (u32)(c & 0x1F);
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        cp = (u32)(c & 0x0F);
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        cp = (u32)(c & 0x07);
    } else {
        return 0;
    }

    if (n > len) {
        return 0;
    }

    for (i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (u32)(s[i] & 0x3F);
    }

    if (n == 2 && cp < 0x80) {
        return 0;
    }
    if (n == 3 && cp < 0x800) {
        return 0;
    }
    if (n == 4 && cp < 0x10000) {
        return 0;
    }
    if (cp > 0x10FFFF) {
        return 0;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        return 0;
    }

    *out_len = n;
    *out_cp = cp;
    return 1;
}

static size_t utf8_encode(u32 cp, unsigned char *out)
{
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

static int utf8_validate(const unsigned char *s, size_t len)
{
    size_t p;
    size_t n;
    u32 cp;

    p = 0;
    while (p < len) {
        if (!utf8_decode(s + p, len - p, &n, &cp)) {
            return 0;
        }
        p += n;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lexer cursor helpers                                                */
/* ------------------------------------------------------------------ */

static Span span_at(Lexer *lx)
{
    Span s;
    s.file = lx->file;
    s.line = lx->line;
    s.column = lx->column;
    s.offset = lx->pos;
    return s;
}

static void advance_ascii(Lexer *lx)
{
    if (lx->pos < lx->len) {
        lx->pos += 1;
        lx->column += 1;
    }
}

static void advance_char(Lexer *lx)
{
    size_t n;
    u32 cp;

    if (lx->pos >= lx->len) {
        return;
    }

    if (utf8_decode(lx->src + lx->pos, lx->len - lx->pos, &n, &cp)) {
        lx->pos += n;
    } else {
        cp = (u32)lx->src[lx->pos];
        lx->pos += 1;
    }

    if (cp == '\n') {
        lx->line += 1;
        lx->column = 1;
    } else {
        lx->column += 1;
    }
}

static void advance_newline(Lexer *lx)
{
    if (lx->pos < lx->len) {
        if (lx->src[lx->pos] == '\r' &&
            lx->pos + 1 < lx->len &&
            lx->src[lx->pos + 1] == '\n') {
            lx->pos += 2;
        } else {
            lx->pos += 1;
        }
        lx->line += 1;
        lx->column = 1;
    }
}

static Token make_token(TokenKind kind, Span span)
{
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = kind;
    t.span = span;
    return t;
}

static Token make_error(Span span, const char *message)
{
    Token t;
    t = make_token(TOK_ERROR, span);
    t.as.error = message;
    return t;
}

/* ------------------------------------------------------------------ */
/* Keywords                                                            */
/* ------------------------------------------------------------------ */

typedef struct Keyword {
    const char *name;
    TokenKind kind;
} Keyword;

static const Keyword KEYWORDS[] = {
    { "type",   TOK_KW_TYPE   },
    { "struct", TOK_KW_STRUCT },
    { "enum",   TOK_KW_ENUM   },
    { "fn",     TOK_KW_FN     },
    { "let",    TOK_KW_LET    },
    { "if",     TOK_KW_IF     },
    { "then",   TOK_KW_THEN   },
    { "else",   TOK_KW_ELSE   },
    { "match",  TOK_KW_MATCH  },
    { "import", TOK_KW_IMPORT },
    { "true",   TOK_KW_TRUE   },
    { "false",  TOK_KW_FALSE  }
};

static TokenKind keyword_kind(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        if (strlen(KEYWORDS[i].name) == n &&
            memcmp(KEYWORDS[i].name, s, n) == 0) {
            return KEYWORDS[i].kind;
        }
    }
    return TOK_IDENT;
}

/* ------------------------------------------------------------------ */
/* 64-bit integer accumulation                                         */
/* ------------------------------------------------------------------ */

static void u64_zero(u64 *v)
{
    int i;
    for (i = 0; i < 4; i++) {
        v->w[i] = 0;
    }
}

static int u64_mul_add(u64 *v, unsigned base, unsigned digit)
{
    unsigned carry;
    int i;

    carry = digit;
    for (i = 0; i < 4; i++) {
        unsigned long t;
        t = (unsigned long)v->w[i] * (unsigned long)base + (unsigned long)carry;
        v->w[i] = (u16)(t & 0xFFFFul);
        carry = (unsigned)(t >> 16);
    }
    return carry == 0;
}

static int u64_le_signed_max(const u64 *v)
{
    return v->w[3] <= 0x7FFFu;
}

/* ------------------------------------------------------------------ */
/* Escape decoding                                                     */
/* ------------------------------------------------------------------ */

/*
 * The cursor is positioned just after the backslash.
 * On success, is_byte is 1 for single-byte escapes (including \xNN) and
 * 0 for \u{...}, value holds the byte or code point.
 */
static int decode_escape(Lexer *lx, int allow_unicode, int *is_byte,
                         u32 *value, const char **err)
{
    unsigned char c;

    if (lx->pos >= lx->len) {
        *err = "unterminated escape sequence";
        return 0;
    }

    c = lx->src[lx->pos];
    switch (c) {
    case 'n':
        advance_ascii(lx);
        *is_byte = 1;
        *value = (u32)'\n';
        return 1;
    case 'r':
        advance_ascii(lx);
        *is_byte = 1;
        *value = (u32)'\r';
        return 1;
    case 't':
        advance_ascii(lx);
        *is_byte = 1;
        *value = (u32)'\t';
        return 1;
    case '\\':
        advance_ascii(lx);
        *is_byte = 1;
        *value = (u32)'\\';
        return 1;
    case '\'':
        advance_ascii(lx);
        *is_byte = 1;
        *value = (u32)'\'';
        return 1;
    case '"':
        advance_ascii(lx);
        *is_byte = 1;
        *value = (u32)'"';
        return 1;
    case '0':
        advance_ascii(lx);
        *is_byte = 1;
        *value = 0;
        return 1;
    case 'x': {
        int h1;
        int h2;
        advance_ascii(lx);
        if (lx->pos + 1 >= lx->len) {
            *err = "incomplete hexadecimal escape";
            return 0;
        }
        h1 = hex_value(lx->src[lx->pos]);
        h2 = hex_value(lx->src[lx->pos + 1]);
        if (h1 < 0 || h2 < 0) {
            *err = "invalid hexadecimal escape";
            return 0;
        }
        advance_ascii(lx);
        advance_ascii(lx);
        *is_byte = 1;
        *value = (u32)((h1 << 4) | h2);
        return 1;
    }
    case 'u': {
        u32 cp;
        unsigned count;

        if (!allow_unicode) {
            *err = "Unicode escape is not allowed here";
            return 0;
        }

        advance_ascii(lx);
        if (lx->pos >= lx->len || lx->src[lx->pos] != '{') {
            *err = "expected '{' after \\u";
            return 0;
        }
        advance_ascii(lx);

        cp = 0;
        count = 0;
        while (lx->pos < lx->len && lx->src[lx->pos] != '}') {
            int h;
            h = hex_value(lx->src[lx->pos]);
            if (h < 0) {
                *err = "invalid Unicode escape";
                return 0;
            }
            if (count >= 6) {
                *err = "Unicode escape is too long";
                return 0;
            }
            cp = (cp << 4) | (u32)h;
            count += 1;
            advance_ascii(lx);
        }

        if (lx->pos >= lx->len || lx->src[lx->pos] != '}') {
            *err = "unterminated Unicode escape";
            return 0;
        }
        advance_ascii(lx);

        if (count == 0) {
            *err = "empty Unicode escape";
            return 0;
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            *err = "invalid Unicode scalar value";
            return 0;
        }

        *is_byte = 0;
        *value = cp;
        return 1;
    }
    default:
        *err = "unknown escape sequence";
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Numbers                                                             */
/* ------------------------------------------------------------------ */

static Token lex_int_base(Lexer *lx, Span start, unsigned base,
                          size_t prefix_len)
{
    u64 value;
    int saw_digit;
    int overflow;
    size_t i;
    Token t;

    u64_zero(&value);
    saw_digit = 0;
    overflow = 0;

    for (i = 0; i < prefix_len; i++) {
        advance_ascii(lx);
    }

    while (lx->pos < lx->len) {
        unsigned char c;
        int d;

        c = lx->src[lx->pos];
        if (c == '_') {
            advance_ascii(lx);
            continue;
        }

        d = digit_value(c);
        if (d < 0 || (unsigned)d >= base) {
            break;
        }

        saw_digit = 1;
        if (!u64_mul_add(&value, base, (unsigned)d)) {
            overflow = 1;
        }
        advance_ascii(lx);
    }

    if (!saw_digit) {
        return make_error(start, "integer literal requires at least one digit");
    }
    if (overflow) {
        return make_error(start, "integer literal overflows 64 bits");
    }
    if (!u64_le_signed_max(&value)) {
        return make_error(start, "integer literal is outside the Int range");
    }

    t = make_token(TOK_INT, start);
    t.as.int_lit.value = value;
    return t;
}

static Token lex_float(Lexer *lx, Span start, size_t begin, size_t end)
{
    size_t n;
    size_t i;
    size_t j;
    char *buf;
    char *endptr;
    double value;
    Token t;

    n = end - begin;
    buf = (char *)arena_alloc(lx->arena, n + 1);
    if (buf == NULL) {
        return make_error(start, "out of memory");
    }

    j = 0;
    for (i = begin; i < end; i++) {
        if (lx->src[i] != '_') {
            buf[j++] = (char)lx->src[i];
        }
    }
    buf[j] = '\0';

    errno = 0;
    value = strtod(buf, &endptr);
    if (endptr != buf + j ||
        errno == ERANGE ||
        value == HUGE_VAL ||
        value == -HUGE_VAL) {
        lx->pos = end;
        lx->column += (u32)(end - begin);
        return make_error(start, "float literal is out of range");
    }

    lx->pos = end;
    lx->column += (u32)(end - begin);

    t = make_token(TOK_FLOAT, start);
    t.as.float_lit.value = value;
    return t;
}

static Token lex_number(Lexer *lx, Span start)
{
    size_t begin;
    size_t p;
    unsigned char c;
    int is_float;

    begin = lx->pos;
    c = lx->src[begin];

    if (c == '0' && begin + 1 < lx->len &&
        (lx->src[begin + 1] == 'x' || lx->src[begin + 1] == 'X')) {
        return lex_int_base(lx, start, 16, 2);
    }
    if (c == '0' && begin + 1 < lx->len &&
        (lx->src[begin + 1] == 'b' || lx->src[begin + 1] == 'B')) {
        return lex_int_base(lx, start, 2, 2);
    }

    p = begin;
    while (p < lx->len &&
           (is_ascii_digit(lx->src[p]) || lx->src[p] == '_')) {
        p += 1;
    }

    is_float = 0;
    if (p < lx->len && lx->src[p] == '.' &&
        p + 1 < lx->len && is_ascii_digit(lx->src[p + 1])) {
        is_float = 1;
        p += 1;
        while (p < lx->len &&
               (is_ascii_digit(lx->src[p]) || lx->src[p] == '_')) {
            p += 1;
        }
    }

    if (p < lx->len && (lx->src[p] == 'e' || lx->src[p] == 'E')) {
        is_float = 1;
        p += 1;
        if (p < lx->len && (lx->src[p] == '+' || lx->src[p] == '-')) {
            p += 1;
        }
        if (p >= lx->len || !is_ascii_digit(lx->src[p])) {
            lx->pos = p;
            lx->column += (u32)(p - begin);
            return make_error(start, "malformed float exponent");
        }
        while (p < lx->len &&
               (is_ascii_digit(lx->src[p]) || lx->src[p] == '_')) {
            p += 1;
        }
    }

    if (is_float) {
        return lex_float(lx, start, begin, p);
    }

    if (c == '0' && p > begin + 1) {
        lx->pos = p;
        lx->column += (u32)(p - begin);
        return make_error(start, "leading zero in integer literal");
    }

    {
        u64 value;
        size_t q;
        int saw_digit;
        int overflow;
        Token t;

        u64_zero(&value);
        saw_digit = 0;
        overflow = 0;

        for (q = begin; q < p; q++) {
            int d;
            if (lx->src[q] == '_') {
                continue;
            }
            d = digit_value(lx->src[q]);
            if (d < 0 || d >= 10) {
                lx->pos = p;
                lx->column += (u32)(p - begin);
                return make_error(start, "invalid digit in integer literal");
            }
            saw_digit = 1;
            if (!u64_mul_add(&value, 10, (unsigned)d)) {
                overflow = 1;
            }
        }

        lx->pos = p;
        lx->column += (u32)(p - begin);

        if (!saw_digit) {
            return make_error(start, "integer literal requires at least one digit");
        }
        if (overflow) {
            return make_error(start, "integer literal overflows 64 bits");
        }
        if (!u64_le_signed_max(&value)) {
            return make_error(start, "integer literal is outside the Int range");
        }

        t = make_token(TOK_INT, start);
        t.as.int_lit.value = value;
        return t;
    }
}

/* ------------------------------------------------------------------ */
/* Strings, bytes, and characters                                      */
/* ------------------------------------------------------------------ */

static Token lex_string(Lexer *lx, Span start)
{
    size_t cap;
    unsigned char *buf;
    size_t out;
    Token t;

    cap = lx->len - lx->pos + 1;
    buf = (unsigned char *)arena_alloc(lx->arena, cap);
    if (buf == NULL) {
        return make_error(start, "out of memory");
    }

    out = 0;
    advance_ascii(lx); /* opening quote */

    for (;;) {
        unsigned char c;

        if (lx->pos >= lx->len) {
            lx->pos = lx->len;
            return make_error(start, "unterminated string literal");
        }

        c = lx->src[lx->pos];

        if (c == '"') {
            advance_ascii(lx);
            break;
        }

        if (c == '\n' || c == '\r') {
            return make_error(start, "newline in string literal");
        }

        if (c == '\\') {
            int is_byte;
            u32 value;
            const char *err;

            advance_ascii(lx);
            if (!decode_escape(lx, 1, &is_byte, &value, &err)) {
                return make_error(start, err);
            }
            if (is_byte) {
                buf[out++] = (unsigned char)value;
            } else {
                out += utf8_encode(value, buf + out);
            }
        } else {
            size_t n;
            u32 cp;

            if (!utf8_decode(lx->src + lx->pos, lx->len - lx->pos,
                             &n, &cp)) {
                return make_error(start, "invalid UTF-8 in string literal");
            }
            memcpy(buf + out, lx->src + lx->pos, n);
            out += n;
            lx->pos += n;
            lx->column += 1;
        }
    }

    buf[out] = '\0';
    if (!utf8_validate(buf, out)) {
        return make_error(start, "string literal is not valid UTF-8");
    }

    t = make_token(TOK_STRING, start);
    t.as.str_lit.data = (const char *)buf;
    t.as.str_lit.len = out;
    return t;
}

static Token lex_raw_string(Lexer *lx, Span start)
{
    size_t data_start;
    size_t len;
    Token t;

    advance_ascii(lx); /* r */
    advance_ascii(lx); /* opening quote */
    data_start = lx->pos;

    while (lx->pos < lx->len && lx->src[lx->pos] != '"') {
        if (is_newline_char(lx->src[lx->pos])) {
            advance_newline(lx);
        } else {
            advance_char(lx);
        }
    }

    if (lx->pos >= lx->len) {
        return make_error(start, "unterminated raw string literal");
    }

    len = lx->pos - data_start;
    advance_ascii(lx); /* closing quote */

    t = make_token(TOK_STRING, start);
    t.as.str_lit.data = (const char *)(lx->src + data_start);
    t.as.str_lit.len = len;
    return t;
}

static Token lex_bytes(Lexer *lx, Span start)
{
    size_t cap;
    unsigned char *buf;
    size_t out;
    Token t;

    cap = lx->len - lx->pos + 1;
    buf = (unsigned char *)arena_alloc(lx->arena, cap);
    if (buf == NULL) {
        return make_error(start, "out of memory");
    }

    out = 0;
    advance_ascii(lx); /* b */
    advance_ascii(lx); /* opening quote */

    for (;;) {
        unsigned char c;

        if (lx->pos >= lx->len) {
            lx->pos = lx->len;
            return make_error(start, "unterminated byte literal");
        }

        c = lx->src[lx->pos];

        if (c == '"') {
            advance_ascii(lx);
            break;
        }

        if (c == '\n' || c == '\r') {
            return make_error(start, "newline in byte literal");
        }

        if (c == '\\') {
            int is_byte;
            u32 value;
            const char *err;

            advance_ascii(lx);
            if (!decode_escape(lx, 0, &is_byte, &value, &err)) {
                return make_error(start, err);
            }
            if (!is_byte) {
                return make_error(start, "Unicode escape in byte literal");
            }
            buf[out++] = (unsigned char)value;
        } else {
            if (c >= 0x80) {
                return make_error(start, "non-ASCII byte in byte literal");
            }
            buf[out++] = c;
            advance_ascii(lx);
        }
    }

    buf[out] = '\0';

    t = make_token(TOK_BYTES, start);
    t.as.str_lit.data = (const char *)buf;
    t.as.str_lit.len = out;
    return t;
}

static Token lex_char(Lexer *lx, Span start)
{
    u32 cp;
    int is_byte;
    const char *err;
    Token t;

    cp = 0;
    is_byte = 0;
    advance_ascii(lx); /* opening quote */

    if (lx->pos >= lx->len) {
        return make_error(start, "unterminated character literal");
    }
    if (lx->src[lx->pos] == '\'') {
        return make_error(start, "empty character literal");
    }
    if (is_newline_char(lx->src[lx->pos])) {
        return make_error(start, "newline in character literal");
    }

    if (lx->src[lx->pos] == '\\') {
        advance_ascii(lx);
        if (!decode_escape(lx, 1, &is_byte, &cp, &err)) {
            return make_error(start, err);
        }
    } else {
        size_t n;

        if (!utf8_decode(lx->src + lx->pos, lx->len - lx->pos,
                         &n, &cp)) {
            return make_error(start, "invalid UTF-8 in character literal");
        }
        lx->pos += n;
        lx->column += 1;
    }

    if (lx->pos >= lx->len || lx->src[lx->pos] != '\'') {
        return make_error(start,
                          "character literal must contain exactly one character");
    }

    advance_ascii(lx); /* closing quote */

    t = make_token(TOK_CHAR, start);
    t.as.char_lit.codepoint = cp;
    return t;
}

/* ------------------------------------------------------------------ */
/* Trivia                                                              */
/* ------------------------------------------------------------------ */

static void skip_trivia(Lexer *lx)
{
    for (;;) {
        unsigned char c;

        if (lx->pos >= lx->len) {
            return;
        }

        c = lx->src[lx->pos];

        if (c == ' ' || c == '\t') {
            advance_ascii(lx);
            continue;
        }

        if (is_newline_char(c)) {
            return;
        }

        if (c == '#') {
            advance_ascii(lx);
            while (lx->pos < lx->len &&
                   !is_newline_char(lx->src[lx->pos])) {
                advance_char(lx);
            }
            continue;
        }

        break;
    }
}

/* ------------------------------------------------------------------ */
/* Source validation and initialization                                */
/* ------------------------------------------------------------------ */

static void validate_utf8_source(Lexer *lx)
{
    size_t p;
    u32 line;
    u32 column;

    p = lx->pos;
    line = 1;
    column = 1;

    while (p < lx->len) {
        size_t n;
        u32 cp;

        if (!utf8_decode(lx->src + p, lx->len - p, &n, &cp)) {
            lx->utf8_error = 1;
            lx->utf8_line = line;
            lx->utf8_column = column;
            lx->utf8_offset = p;
            return;
        }
        p += n;
        if (cp == '\n') {
            line += 1;
            column = 1;
        } else if (cp == '\r') {
            if (!(p < lx->len && lx->src[p] == '\n')) {
                line += 1;
                column = 1;
            } else {
                column += 1;
            }
        } else {
            column += 1;
        }
    }
}

void lexer_init(Lexer *lx, const char *file, const unsigned char *src,
                size_t len, Arena *arena)
{
    lx->file = file;
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->line = 1;
    lx->column = 1;
    lx->arena = arena;

    lx->utf8_error = 0;
    lx->utf8_error_emitted = 0;
    lx->utf8_line = 1;
    lx->utf8_column = 1;
    lx->utf8_offset = 0;

    if (len >= 3 &&
        src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF) {
        lx->pos = 3;
    }

    validate_utf8_source(lx);
}

Token lexer_next(Lexer *lx)
{
    Span span;
    unsigned char c;

    if (lx->utf8_error) {
        Span bad;
        bad.file = lx->file;
        bad.line = lx->utf8_line;
        bad.column = lx->utf8_column;
        bad.offset = lx->utf8_offset;

        if (!lx->utf8_error_emitted) {
            lx->utf8_error_emitted = 1;
            return make_error(bad, "invalid UTF-8 in source");
        }
        return make_token(TOK_EOF, bad);
    }

    skip_trivia(lx);

    span = span_at(lx);

    if (lx->pos >= lx->len) {
        return make_token(TOK_EOF, span);
    }

    c = lx->src[lx->pos];

    if (is_newline_char(c)) {
        advance_newline(lx);
        return make_token(TOK_NEWLINE, span);
    }

    if ((c == 'r' || c == 'b') &&
        lx->pos + 1 < lx->len &&
        lx->src[lx->pos + 1] == '"') {
        if (c == 'r') {
            return lex_raw_string(lx, span);
        }
        return lex_bytes(lx, span);
    }

    if (is_ident_start(c)) {
        size_t start;
        const char *text;
        size_t len;
        TokenKind kind;
        Token t;

        start = lx->pos;
        while (lx->pos < lx->len &&
               is_ident_continue(lx->src[lx->pos])) {
            advance_ascii(lx);
        }

        text = (const char *)(lx->src + start);
        len = lx->pos - start;
        kind = (len == 1 && text[0] == '_')
                   ? TOK_UNDERSCORE
                   : keyword_kind(text, len);

        t = make_token(kind, span);
        if (kind == TOK_IDENT) {
            t.as.ident.text = text;
            t.as.ident.len = len;
        }
        return t;
    }

    if (is_ascii_digit(c)) {
        return lex_number(lx, span);
    }

    if (c == '"') {
        return lex_string(lx, span);
    }

    if (c == '\'') {
        return lex_char(lx, span);
    }

    switch (c) {
    case '+':
        advance_ascii(lx);
        return make_token(TOK_PLUS, span);
    case '-':
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '>') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_ARROW, span);
        }
        advance_ascii(lx);
        return make_token(TOK_MINUS, span);
    case '*':
        advance_ascii(lx);
        return make_token(TOK_STAR, span);
    case '/':
        advance_ascii(lx);
        return make_token(TOK_SLASH, span);
    case '%':
        advance_ascii(lx);
        return make_token(TOK_PERCENT, span);
    case '=':
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '=') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_EQ, span);
        }
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '>') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_FAT_ARROW, span);
        }
        advance_ascii(lx);
        return make_token(TOK_ASSIGN, span);
    case '!':
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '=') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_NE, span);
        }
        advance_ascii(lx);
        return make_token(TOK_BANG, span);
    case '<':
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '=') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_LE, span);
        }
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '<') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_SHL, span);
        }
        advance_ascii(lx);
        return make_token(TOK_LT, span);
    case '>':
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '=') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_GE, span);
        }
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '>') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_SHR, span);
        }
        advance_ascii(lx);
        return make_token(TOK_GT, span);
    case '&':
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '&') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_AND_AND, span);
        }
        advance_ascii(lx);
        return make_token(TOK_AMP, span);
    case '|':
        if (lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '|') {
            advance_ascii(lx);
            advance_ascii(lx);
            return make_token(TOK_OR_OR, span);
        }
        advance_ascii(lx);
        return make_token(TOK_PIPE, span);
    case '^':
        advance_ascii(lx);
        return make_token(TOK_CARET, span);
    case '~':
        advance_ascii(lx);
        return make_token(TOK_TILDE, span);
    case ':':
        advance_ascii(lx);
        return make_token(TOK_COLON, span);
    case ';':
        advance_ascii(lx);
        return make_token(TOK_SEMI, span);
    case ',':
        advance_ascii(lx);
        return make_token(TOK_COMMA, span);
    case '.':
        advance_ascii(lx);
        return make_token(TOK_DOT, span);
    case '(':
        advance_ascii(lx);
        return make_token(TOK_LPAREN, span);
    case ')':
        advance_ascii(lx);
        return make_token(TOK_RPAREN, span);
    case '{':
        advance_ascii(lx);
        return make_token(TOK_LBRACE, span);
    case '}':
        advance_ascii(lx);
        return make_token(TOK_RBRACE, span);
    case '[':
        advance_ascii(lx);
        return make_token(TOK_LBRACKET, span);
    case ']':
        advance_ascii(lx);
        return make_token(TOK_RBRACKET, span);
    default:
        break;
    }

    if (c < 0x80) {
        advance_ascii(lx);
        return make_error(span, "unexpected character");
    }

    {
        size_t n;
        u32 cp;

        if (utf8_decode(lx->src + lx->pos, lx->len - lx->pos, &n, &cp)) {
            lx->pos += n;
            lx->column += 1;
        } else {
            lx->pos += 1;
            lx->column += 1;
        }
        return make_error(span, "unexpected non-ASCII character");
    }
}
