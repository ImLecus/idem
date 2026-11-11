#ifndef IDEM_TOKEN_H
#define IDEM_TOKEN_H

#include <stddef.h>

#include "platform.h"

typedef struct Span {
    const char *file;
    u32 line;
    u32 column;
    size_t offset;
} Span;

typedef enum TokenKind {
    TOK_EOF,
    TOK_ERROR,
    TOK_NEWLINE,
    TOK_IDENT,

    TOK_INT,
    TOK_FLOAT,
    TOK_CHAR,
    TOK_STRING,
    TOK_BYTES,

    TOK_KW_TYPE,
    TOK_KW_STRUCT,
    TOK_KW_ENUM,
    TOK_KW_FN,
    TOK_KW_LET,
    TOK_KW_IF,
    TOK_KW_THEN,
    TOK_KW_ELSE,
    TOK_KW_MATCH,
    TOK_KW_IMPORT,
    TOK_KW_TRUE,
    TOK_KW_FALSE,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_SEMI,
    TOK_COLON,
    TOK_DOT,
    TOK_UNDERSCORE,

    TOK_ARROW,
    TOK_FAT_ARROW,
    TOK_ASSIGN,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,

    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,

    TOK_AND_AND,
    TOK_OR_OR,
    TOK_BANG,

    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE,
    TOK_SHL,
    TOK_SHR
} TokenKind;

typedef struct Token {
    TokenKind kind;
    Span span;
    union {
        struct {
            const char *text;
            size_t len;
        } ident;
        struct {
            u64 value;
        } int_lit;
        struct {
            double value;
        } float_lit;
        struct {
            u32 codepoint;
        } char_lit;
        struct {
            const char *data;
            size_t len;
        } str_lit;
        const char *error;
    } as;
} Token;

const char *token_kind_name(TokenKind kind);

#endif /* IDEM_TOKEN_H */
