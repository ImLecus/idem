#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Diagnostics and token movement                                      */
/* ------------------------------------------------------------------ */

static void set_error_message(Parser *p, const char *msg)
{
    size_t n;
    n = strlen(msg);
    if (n >= sizeof(p->error_message)) {
        n = sizeof(p->error_message) - 1;
    }
    memcpy(p->error_message, msg, n);
    p->error_message[n] = '\0';
}

static void parser_error(Parser *p, Span span, const char *msg)
{
    if (!p->had_error) {
        p->had_error = 1;
        p->error_span = span;
        set_error_message(p, msg);
    }
}

static void parser_error_expected(Parser *p, const char *what)
{
    char buf[256];
    if (!p->had_error) {
        sprintf(buf, "expected %s, found %s", what,
                token_kind_name(p->current.kind));
        parser_error(p, p->current.span, buf);
    }
}

static void parser_load_next(Parser *p)
{
    for (;;) {
        p->current = lexer_next(p->lexer);
        if (p->current.kind != TOK_ERROR) {
            break;
        }
        parser_error(p, p->current.span, p->current.as.error);
    }
}

static void advance(Parser *p)
{
    parser_load_next(p);
}

static int at(Parser *p, TokenKind kind)
{
    return p->current.kind == kind;
}

static int match_token(Parser *p, TokenKind kind)
{
    if (at(p, kind)) {
        advance(p);
        return 1;
    }
    return 0;
}

static int expect(Parser *p, TokenKind kind, const char *what)
{
    if (match_token(p, kind)) {
        return 1;
    }
    parser_error_expected(p, what);
    return 0;
}

static void skip_newlines(Parser *p)
{
    while (at(p, TOK_NEWLINE)) {
        advance(p);
    }
}

static void skip_separators(Parser *p)
{
    while (at(p, TOK_NEWLINE) || at(p, TOK_SEMI)) {
        advance(p);
    }
}

/* ------------------------------------------------------------------ */
/* Arena-backed nodes                                                  */
/* ------------------------------------------------------------------ */

static void *parser_alloc(Parser *p, size_t size)
{
    void *x;
    x = arena_alloc(p->arena, size);
    if (x == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    memset(x, 0, size);
    return x;
}

static AstType *new_type(Parser *p, AstTypeKind kind, Span span)
{
    AstType *t;
    t = (AstType *)parser_alloc(p, sizeof(AstType));
    t->kind = kind;
    t->span = span;
    return t;
}

static AstTypeList *new_type_list(Parser *p, AstType *type)
{
    AstTypeList *l;
    l = (AstTypeList *)parser_alloc(p, sizeof(AstTypeList));
    l->type = type;
    return l;
}

static AstExpr *new_expr(Parser *p, AstExprKind kind, Span span)
{
    AstExpr *e;
    e = (AstExpr *)parser_alloc(p, sizeof(AstExpr));
    e->kind = kind;
    e->span = span;
    return e;
}

static AstExprList *new_expr_list(Parser *p, AstExpr *expr)
{
    AstExprList *l;
    l = (AstExprList *)parser_alloc(p, sizeof(AstExprList));
    l->expr = expr;
    return l;
}

static AstPattern *new_pattern(Parser *p, AstPatternKind kind, Span span)
{
    AstPattern *pat;
    pat = (AstPattern *)parser_alloc(p, sizeof(AstPattern));
    pat->kind = kind;
    pat->span = span;
    return pat;
}

static AstPatternList *new_pattern_list(Parser *p, AstPattern *pattern)
{
    AstPatternList *l;
    l = (AstPatternList *)parser_alloc(p, sizeof(AstPatternList));
    l->pattern = pattern;
    return l;
}

static AstDecl *new_decl(Parser *p, AstDeclKind kind, Span span)
{
    AstDecl *d;
    d = (AstDecl *)parser_alloc(p, sizeof(AstDecl));
    d->kind = kind;
    d->span = span;
    return d;
}

static AstParam *new_param(Parser *p)
{
    return (AstParam *)parser_alloc(p, sizeof(AstParam));
}

static AstField *new_field(Parser *p)
{
    return (AstField *)parser_alloc(p, sizeof(AstField));
}

static AstVariant *new_variant(Parser *p)
{
    return (AstVariant *)parser_alloc(p, sizeof(AstVariant));
}

static AstFieldInit *new_field_init(Parser *p)
{
    return (AstFieldInit *)parser_alloc(p, sizeof(AstFieldInit));
}

static AstMapEntry *new_map_entry(Parser *p)
{
    return (AstMapEntry *)parser_alloc(p, sizeof(AstMapEntry));
}

static AstMatchArm *new_match_arm(Parser *p)
{
    return (AstMatchArm *)parser_alloc(p, sizeof(AstMatchArm));
}

static AstBlockItem *new_block_item(Parser *p, AstBlockItemKind kind, Span span)
{
    AstBlockItem *item;
    item = (AstBlockItem *)parser_alloc(p, sizeof(AstBlockItem));
    item->kind = kind;
    item->span = span;
    return item;
}

static AstPatternField *new_pattern_field(Parser *p)
{
    return (AstPatternField *)parser_alloc(p, sizeof(AstPatternField));
}

static AstImport *new_import(Parser *p)
{
    return (AstImport *)parser_alloc(p, sizeof(AstImport));
}

static void type_list_add(Parser *p, AstTypeList **head, AstTypeList **tail,
                          AstType *type)
{
    AstTypeList *n;
    n = new_type_list(p, type);
    if (*tail != NULL) {
        (*tail)->next = n;
    } else {
        *head = n;
    }
    *tail = n;
}

static void expr_list_add(Parser *p, AstExprList **head, AstExprList **tail,
                          AstExpr *expr)
{
    AstExprList *n;
    n = new_expr_list(p, expr);
    if (*tail != NULL) {
        (*tail)->next = n;
    } else {
        *head = n;
    }
    *tail = n;
}

static void pattern_list_add(Parser *p, AstPatternList **head,
                             AstPatternList **tail, AstPattern *pattern)
{
    AstPatternList *n;
    n = new_pattern_list(p, pattern);
    if (*tail != NULL) {
        (*tail)->next = n;
    } else {
        *head = n;
    }
    *tail = n;
}

static int token_ident_is(Token t, const char *s, size_t n)
{
    return t.kind == TOK_IDENT && t.as.ident.len == n &&
           memcmp(t.as.ident.text, s, n) == 0;
}

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

static AstType *parse_type(Parser *p);

static AstType *parse_primary_type(Parser *p)
{
    if (at(p, TOK_IDENT)) {
        Token id;
        AstType *t;

        id = p->current;
        advance(p);
        t = new_type(p, TYPE_NAMED, id.span);
        t->as.named.name = id.as.ident.text;
        t->as.named.name_len = id.as.ident.len;
        t->as.named.args = NULL;

        if (match_token(p, TOK_LBRACKET)) {
            AstTypeList *head;
            AstTypeList *tail;

            head = NULL;
            tail = NULL;
            skip_newlines(p);
            if (at(p, TOK_RBRACKET)) {
                parser_error_expected(p, "a type argument");
            }
            if (!at(p, TOK_RBRACKET)) {
                for (;;) {
                    AstType *arg;
                    arg = parse_type(p);
                    type_list_add(p, &head, &tail, arg);
                    skip_newlines(p);
                    if (match_token(p, TOK_COMMA)) {
                        skip_newlines(p);
                        if (at(p, TOK_RBRACKET)) {
                            break;
                        }
                        continue;
                    }
                    break;
                }
            }
            t->as.named.args = head;
            expect(p, TOK_RBRACKET, "']'");
        }

        return t;
    }

    parser_error_expected(p, "a type");
    if (!at(p, TOK_EOF) && !at(p, TOK_RPAREN) && !at(p, TOK_RBRACKET) &&
        !at(p, TOK_RBRACE) && !at(p, TOK_COMMA) && !at(p, TOK_SEMI) &&
        !at(p, TOK_NEWLINE) && !at(p, TOK_ASSIGN) && !at(p, TOK_ARROW) &&
        !at(p, TOK_PIPE)) {
        advance(p);
    }
    return new_type(p, TYPE_ERROR, p->current.span);
}

static AstType *parse_tuple_type(Parser *p)
{
    if (at(p, TOK_LPAREN)) {
        Span start;
        AstType *first;
        AstType *tuple;
        AstTypeList *head;
        AstTypeList *tail;

        start = p->current.span;
        advance(p);
        skip_newlines(p);
        if (match_token(p, TOK_RPAREN)) {
            return new_type(p, TYPE_UNIT, start);
        }

        first = parse_type(p);
        skip_newlines(p);

        if (!match_token(p, TOK_COMMA)) {
            expect(p, TOK_RPAREN, "')'");
            return first;
        }

        tuple = new_type(p, TYPE_TUPLE, first->span);
        head = NULL;
        tail = NULL;
        type_list_add(p, &head, &tail, first);
        skip_newlines(p);

        if (at(p, TOK_RPAREN)) {
            parser_error_expected(p, "a second tuple element");
        }

        if (!at(p, TOK_RPAREN)) {
            for (;;) {
                AstType *item;
                item = parse_type(p);
                type_list_add(p, &head, &tail, item);
                skip_newlines(p);
                if (match_token(p, TOK_COMMA)) {
                    skip_newlines(p);
                    if (at(p, TOK_RPAREN)) {
                        break;
                    }
                    continue;
                }
                break;
            }
        }

        tuple->as.tuple.items = head;
        expect(p, TOK_RPAREN, "')'");
        return tuple;
    }

    return parse_primary_type(p);
}

static AstType *parse_arrow_type(Parser *p)
{
    AstType *left;
    AstType *result;
    AstType *fn;
    AstTypeList *single;

    left = parse_tuple_type(p);
    if (!match_token(p, TOK_ARROW)) {
        return left;
    }

    skip_newlines(p);
    result = parse_type(p);

    fn = new_type(p, TYPE_FUNCTION, left->span);
    fn->as.fn.result = result;

    if (left->kind == TYPE_TUPLE) {
        fn->as.fn.params = left->as.tuple.items;
    } else if (left->kind == TYPE_UNIT) {
        fn->as.fn.params = NULL;
    } else {
        single = new_type_list(p, left);
        fn->as.fn.params = single;
    }

    return fn;
}

static AstType *parse_union_type(Parser *p)
{
    AstType *first;
    AstType *u;
    AstTypeList *head;
    AstTypeList *tail;

    first = parse_arrow_type(p);
    if (!at(p, TOK_PIPE)) {
        return first;
    }

    u = new_type(p, TYPE_UNION, first->span);
    head = NULL;
    tail = NULL;
    type_list_add(p, &head, &tail, first);

    while (match_token(p, TOK_PIPE)) {
        AstType *item;
        skip_newlines(p);
        item = parse_arrow_type(p);
        type_list_add(p, &head, &tail, item);
    }

    u->as.union_.items = head;
    return u;
}

static AstType *parse_type(Parser *p)
{
    skip_newlines(p);
    return parse_union_type(p);
}

/* ------------------------------------------------------------------ */
/* Literal expressions                                                 */
/* ------------------------------------------------------------------ */

static AstExpr *parse_literal_expr(Parser *p)
{
    Token t;
    AstExpr *e;

    t = p->current;

    switch (t.kind) {
    case TOK_INT:
        advance(p);
        e = new_expr(p, EXPR_INT, t.span);
        e->as.int_lit.value = t.as.int_lit.value;
        return e;
    case TOK_FLOAT:
        advance(p);
        e = new_expr(p, EXPR_FLOAT, t.span);
        e->as.float_lit.value = t.as.float_lit.value;
        return e;
    case TOK_CHAR:
        advance(p);
        e = new_expr(p, EXPR_CHAR, t.span);
        e->as.char_lit.codepoint = t.as.char_lit.codepoint;
        return e;
    case TOK_STRING:
        advance(p);
        e = new_expr(p, EXPR_STRING, t.span);
        e->as.str_lit.data = t.as.str_lit.data;
        e->as.str_lit.len = t.as.str_lit.len;
        return e;
    case TOK_BYTES:
        advance(p);
        e = new_expr(p, EXPR_BYTES, t.span);
        e->as.str_lit.data = t.as.str_lit.data;
        e->as.str_lit.len = t.as.str_lit.len;
        return e;
    case TOK_KW_TRUE:
        advance(p);
        e = new_expr(p, EXPR_BOOL, t.span);
        e->as.bool_lit.value = 1;
        return e;
    case TOK_KW_FALSE:
        advance(p);
        e = new_expr(p, EXPR_BOOL, t.span);
        e->as.bool_lit.value = 0;
        return e;
    default:
        parser_error_expected(p, "a literal");
        return new_expr(p, EXPR_ERROR, t.span);
    }
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

static AstExpr *parse_expr(Parser *p);
static AstExpr *parse_binary(Parser *p, int min_prec);
static AstExpr *parse_unary(Parser *p);
static AstExpr *parse_postfix(Parser *p);
static AstExpr *parse_primary(Parser *p);
static AstBlockItem *parse_let(Parser *p);
static AstBlockItem *parse_block_item(Parser *p);
static AstExpr *parse_block(Parser *p);
static AstPattern *parse_pattern_ex(Parser *p, int allow_refutable,
                                    int allow_type_pattern);

static int binary_precedence(TokenKind kind)
{
    switch (kind) {
    case TOK_OR_OR:      return 1;
    case TOK_AND_AND:    return 2;
    case TOK_EQ:
    case TOK_NE:         return 3;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:         return 4;
    case TOK_PIPE:       return 5;
    case TOK_CARET:      return 6;
    case TOK_AMP:        return 7;
    case TOK_SHL:
    case TOK_SHR:        return 8;
    case TOK_PLUS:
    case TOK_MINUS:      return 9;
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT:    return 10;
    default:             return 0;
    }
}

static AstBinaryOp binary_op(TokenKind kind)
{
    switch (kind) {
    case TOK_PLUS:    return BIN_ADD;
    case TOK_MINUS:   return BIN_SUB;
    case TOK_STAR:    return BIN_MUL;
    case TOK_SLASH:   return BIN_DIV;
    case TOK_PERCENT: return BIN_MOD;
    case TOK_EQ:      return BIN_EQ;
    case TOK_NE:      return BIN_NE;
    case TOK_LT:      return BIN_LT;
    case TOK_LE:      return BIN_LE;
    case TOK_GT:      return BIN_GT;
    case TOK_GE:      return BIN_GE;
    case TOK_AND_AND: return BIN_AND;
    case TOK_OR_OR:   return BIN_OR;
    case TOK_AMP:     return BIN_BIT_AND;
    case TOK_PIPE:    return BIN_BIT_OR;
    case TOK_CARET:   return BIN_BIT_XOR;
    case TOK_SHL:     return BIN_SHL;
    case TOK_SHR:     return BIN_SHR;
    default:          return BIN_ADD;
    }
}

static int is_comparison(TokenKind kind)
{
    return kind == TOK_EQ || kind == TOK_NE ||
           kind == TOK_LT || kind == TOK_LE ||
           kind == TOK_GT || kind == TOK_GE;
}

static AstExpr *parse_expr(Parser *p)
{
    return parse_binary(p, 0);
}

static AstExpr *parse_binary(Parser *p, int min_prec)
{
    AstExpr *left;

    left = parse_unary(p);

    for (;;) {
        int prec;
        TokenKind op_kind;
        AstBinaryOp op;
        AstExpr *right;
        AstExpr *node;

        prec = binary_precedence(p->current.kind);
        if (prec == 0 || prec < min_prec) {
            break;
        }

        op_kind = p->current.kind;
        op = binary_op(op_kind);
        advance(p);
        skip_newlines(p);
        right = parse_binary(p, prec + 1);

        node = new_expr(p, EXPR_BINARY, left->span);
        node->as.binary.op = op;
        node->as.binary.left = left;
        node->as.binary.right = right;
        left = node;

        if (is_comparison(op_kind) && is_comparison(p->current.kind)) {
            parser_error(p, p->current.span,
                         "comparison operators are not associative");
            break;
        }
    }

    return left;
}

static AstExpr *parse_unary(Parser *p)
{
    if (at(p, TOK_BANG) || at(p, TOK_MINUS) || at(p, TOK_TILDE)) {
        Token op;
        AstUnaryOp uop;
        AstExpr *operand;
        AstExpr *e;

        op = p->current;
        advance(p);
        skip_newlines(p);

        if (op.kind == TOK_BANG) {
            uop = UN_NOT;
        } else if (op.kind == TOK_MINUS) {
            uop = UN_NEG;
        } else {
            uop = UN_BIT_NOT;
        }

        operand = parse_unary(p);
        e = new_expr(p, EXPR_UNARY, op.span);
        e->as.unary.op = uop;
        e->as.unary.operand = operand;
        return e;
    }

    skip_newlines(p);
    return parse_postfix(p);
}

static AstExpr *parse_call(Parser *p, AstExpr *callee)
{
    AstExpr *e;
    AstExprList *head;
    AstExprList *tail;
    int saved_flag;

    expect(p, TOK_LPAREN, "'('");
    skip_newlines(p);

    saved_flag = p->no_struct_literal;
    p->no_struct_literal = 0;

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RPAREN)) {
        for (;;) {
            AstExpr *arg;
            arg = parse_expr(p);
            expr_list_add(p, &head, &tail, arg);
            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RPAREN)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RPAREN, "')'");

    p->no_struct_literal = saved_flag;

    e = new_expr(p, EXPR_CALL, callee->span);
    e->as.call.callee = callee;
    e->as.call.args = head;
    return e;
}

static AstExpr *parse_postfix(Parser *p)
{
    AstExpr *e;

    e = parse_primary(p);

    for (;;) {
        if (at(p, TOK_DOT)) {
            Token field;
            AstExpr *access;
            advance(p);
            if (!at(p, TOK_IDENT)) {
                parser_error_expected(p, "a field name");
                return e;
            }
            field = p->current;
            advance(p);
            access = new_expr(p, EXPR_FIELD, e->span);
            access->as.field.object = e;
            access->as.field.field = field.as.ident.text;
            access->as.field.field_len = field.as.ident.len;
            e = access;
        } else if (at(p, TOK_LBRACKET)) {
            AstExpr *index;
            AstExpr *access;
            int saved_flag;
            advance(p);
            skip_newlines(p);
            saved_flag = p->no_struct_literal;
            p->no_struct_literal = 0;
            index = parse_expr(p);
            p->no_struct_literal = saved_flag;
            skip_newlines(p);
            expect(p, TOK_RBRACKET, "']'");
            access = new_expr(p, EXPR_INDEX, e->span);
            access->as.index.object = e;
            access->as.index.index = index;
            e = access;
        } else if (at(p, TOK_LPAREN)) {
            e = parse_call(p, e);
        } else {
            break;
        }
    }

    return e;
}

static AstExpr *parse_array_literal(Parser *p);
static AstExpr *parse_list_literal(Parser *p, Span span);
static AstExpr *parse_map_literal(Parser *p, Span span);
static AstExpr *parse_struct_literal(Parser *p, Token name);
static AstExpr *parse_if(Parser *p);
static AstExpr *parse_match(Parser *p);
static AstExpr *parse_fn_expr(Parser *p);

static AstExpr *parse_primary(Parser *p)
{
    if (at(p, TOK_INT) || at(p, TOK_FLOAT) || at(p, TOK_CHAR) ||
        at(p, TOK_STRING) || at(p, TOK_BYTES) ||
        at(p, TOK_KW_TRUE) || at(p, TOK_KW_FALSE)) {
        return parse_literal_expr(p);
    }

    if (at(p, TOK_IDENT)) {
        Token id;
        AstExpr *e;

        id = p->current;
        advance(p);

        if (at(p, TOK_LBRACE) && !p->no_struct_literal) {
            if (token_ident_is(id, "Map", 3)) {
                return parse_map_literal(p, id.span);
            }
            return parse_struct_literal(p, id);
        }

        if (at(p, TOK_LBRACKET) && token_ident_is(id, "List", 4)) {
            return parse_list_literal(p, id.span);
        }

        e = new_expr(p, EXPR_VAR, id.span);
        e->as.var.name = id.as.ident.text;
        e->as.var.name_len = id.as.ident.len;
        return e;
    }

    if (at(p, TOK_LPAREN)) {
        Span start;
        AstExpr *first;
        AstExpr *tuple;
        AstExprList *head;
        AstExprList *tail;
        int saved_flag;

        start = p->current.span;
        advance(p);
        skip_newlines(p);

        if (match_token(p, TOK_RPAREN)) {
            return new_expr(p, EXPR_UNIT, start);
        }

        saved_flag = p->no_struct_literal;
        p->no_struct_literal = 0;

        first = parse_expr(p);
        skip_newlines(p);

        if (!match_token(p, TOK_COMMA)) {
            expect(p, TOK_RPAREN, "')'");
            p->no_struct_literal = saved_flag;
            return first;
        }

        tuple = new_expr(p, EXPR_TUPLE, first->span);
        head = NULL;
        tail = NULL;
        expr_list_add(p, &head, &tail, first);
        skip_newlines(p);

        if (at(p, TOK_RPAREN)) {
            parser_error_expected(p, "a second tuple element");
        }

        if (!at(p, TOK_RPAREN)) {
            for (;;) {
                AstExpr *item;
                item = parse_expr(p);
                expr_list_add(p, &head, &tail, item);
                skip_newlines(p);
                if (match_token(p, TOK_COMMA)) {
                    skip_newlines(p);
                    if (at(p, TOK_RPAREN)) {
                        break;
                    }
                    continue;
                }
                break;
            }
        }

        tuple->as.tuple.items = head;
        expect(p, TOK_RPAREN, "')'");
        p->no_struct_literal = saved_flag;
        return tuple;
    }

    if (at(p, TOK_LBRACKET)) {
        return parse_array_literal(p);
    }

    if (at(p, TOK_KW_IF)) {
        return parse_if(p);
    }

    if (at(p, TOK_KW_MATCH)) {
        return parse_match(p);
    }

    if (at(p, TOK_KW_FN)) {
        return parse_fn_expr(p);
    }

    if (at(p, TOK_LBRACE)) {
        return parse_block(p);
    }

    parser_error_expected(p, "an expression");
    if (!at(p, TOK_EOF) && !at(p, TOK_RPAREN) && !at(p, TOK_RBRACKET) &&
        !at(p, TOK_RBRACE) && !at(p, TOK_COMMA) && !at(p, TOK_SEMI) &&
        !at(p, TOK_NEWLINE)) {
        advance(p);
    }
    return new_expr(p, EXPR_ERROR, p->current.span);
}

static AstExpr *parse_array_literal(Parser *p)
{
    AstExpr *e;
    AstExprList *head;
    AstExprList *tail;
    Span start;
    int saved_flag;

    start = p->current.span;
    expect(p, TOK_LBRACKET, "'['");
    skip_newlines(p);

    saved_flag = p->no_struct_literal;
    p->no_struct_literal = 0;

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RBRACKET)) {
        for (;;) {
            AstExpr *item;
            item = parse_expr(p);
            expr_list_add(p, &head, &tail, item);
            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACKET)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RBRACKET, "']'");

    p->no_struct_literal = saved_flag;

    e = new_expr(p, EXPR_ARRAY, start);
    e->as.array.items = head;
    return e;
}

static AstExpr *parse_list_literal(Parser *p, Span span)
{
    AstExpr *e;
    AstExprList *head;
    AstExprList *tail;
    int saved_flag;

    expect(p, TOK_LBRACKET, "'['");
    skip_newlines(p);

    saved_flag = p->no_struct_literal;
    p->no_struct_literal = 0;

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RBRACKET)) {
        for (;;) {
            AstExpr *item;
            item = parse_expr(p);
            expr_list_add(p, &head, &tail, item);
            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACKET)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RBRACKET, "']'");

    p->no_struct_literal = saved_flag;

    e = new_expr(p, EXPR_LIST, span);
    e->as.list.items = head;
    return e;
}

static AstExpr *parse_map_literal(Parser *p, Span span)
{
    AstExpr *e;
    AstMapEntry *head;
    AstMapEntry *tail;
    int saved_flag;

    expect(p, TOK_LBRACE, "'{'");
    skip_newlines(p);

    saved_flag = p->no_struct_literal;
    p->no_struct_literal = 0;

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RBRACE)) {
        for (;;) {
            AstMapEntry *entry;
            AstExpr *key;
            AstExpr *value;

            key = parse_expr(p);
            skip_newlines(p);
            expect(p, TOK_COLON, "':'");
            skip_newlines(p);
            value = parse_expr(p);

            entry = new_map_entry(p);
            entry->span = key->span;
            entry->key = key;
            entry->value = value;
            if (tail != NULL) {
                tail->next = entry;
            } else {
                head = entry;
            }
            tail = entry;

            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACE)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RBRACE, "'}'");

    p->no_struct_literal = saved_flag;

    e = new_expr(p, EXPR_MAP, span);
    e->as.map.entries = head;
    return e;
}

static AstExpr *parse_struct_literal(Parser *p, Token name)
{
    AstExpr *e;
    AstFieldInit *head;
    AstFieldInit *tail;
    int saved_flag;

    expect(p, TOK_LBRACE, "'{'");
    skip_newlines(p);

    saved_flag = p->no_struct_literal;
    p->no_struct_literal = 0;

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RBRACE)) {
        for (;;) {
            Token field;
            AstFieldInit *init;
            AstExpr *value;

            if (!at(p, TOK_IDENT)) {
                parser_error_expected(p, "a field name");
                break;
            }
            field = p->current;
            advance(p);
            skip_newlines(p);
            expect(p, TOK_COLON, "':'");
            skip_newlines(p);
            value = parse_expr(p);

            init = new_field_init(p);
            init->span = field.span;
            init->name = field.as.ident.text;
            init->name_len = field.as.ident.len;
            init->value = value;
            if (tail != NULL) {
                tail->next = init;
            } else {
                head = init;
            }
            tail = init;

            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACE)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RBRACE, "'}'");

    p->no_struct_literal = saved_flag;

    e = new_expr(p, EXPR_STRUCT, name.span);
    e->as.struct_.name = name.as.ident.text;
    e->as.struct_.name_len = name.as.ident.len;
    e->as.struct_.fields = head;
    return e;
}

static AstExpr *parse_if(Parser *p)
{
    AstExpr *e;
    AstExpr *cond;
    AstExpr *then_branch;
    AstExpr *else_branch;
    Span start;

    start = p->current.span;
    expect(p, TOK_KW_IF, "'if'");
    skip_newlines(p);
    cond = parse_expr(p);
    skip_newlines(p);
    expect(p, TOK_KW_THEN, "'then'");
    skip_newlines(p);
    then_branch = parse_expr(p);
    skip_newlines(p);
    expect(p, TOK_KW_ELSE, "'else'");
    skip_newlines(p);
    else_branch = parse_expr(p);

    e = new_expr(p, EXPR_IF, start);
    e->as.if_.cond = cond;
    e->as.if_.then_branch = then_branch;
    e->as.if_.else_branch = else_branch;
    return e;
}

static AstExpr *parse_match(Parser *p)
{
    AstExpr *e;
    AstExpr *scrutinee;
    AstMatchArm *head;
    AstMatchArm *tail;
    int saved_flag;
    Span start;

    start = p->current.span;
    expect(p, TOK_KW_MATCH, "'match'");
    skip_newlines(p);
    saved_flag = p->no_struct_literal;
    p->no_struct_literal = 1;
    scrutinee = parse_expr(p);
    p->no_struct_literal = 0;
    skip_newlines(p);
    expect(p, TOK_LBRACE, "'{'");
    skip_newlines(p);

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RBRACE)) {
        for (;;) {
            AstMatchArm *arm;
            AstPattern *pattern;
            AstExpr *body;

            pattern = parse_pattern_ex(p, 1, 1);
            skip_newlines(p);
            expect(p, TOK_FAT_ARROW, "'=>'");
            skip_newlines(p);
            body = parse_expr(p);

            arm = new_match_arm(p);
            arm->span = pattern->span;
            arm->pattern = pattern;
            arm->body = body;
            if (tail != NULL) {
                tail->next = arm;
            } else {
                head = arm;
            }
            tail = arm;

            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACE)) {
                    break;
                }
                continue;
            }
            if (at(p, TOK_RBRACE)) {
                break;
            }

            parser_error_expected(p, "',' or '}'");
            while (!at(p, TOK_EOF) && !at(p, TOK_COMMA) &&
                   !at(p, TOK_RBRACE) && !at(p, TOK_NEWLINE)) {
                advance(p);
            }
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACE)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RBRACE, "'}'");

    p->no_struct_literal = saved_flag;

    e = new_expr(p, EXPR_MATCH, start);
    e->as.match.scrutinee = scrutinee;
    e->as.match.arms = head;
    return e;
}

static AstParam *parse_param_list(Parser *p)
{
    AstParam *head;
    AstParam *tail;

    expect(p, TOK_LPAREN, "'('");
    skip_newlines(p);

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RPAREN)) {
        for (;;) {
            AstParam *param;
            Token name;

            if (!at(p, TOK_IDENT)) {
                parser_error_expected(p, "a parameter name");
                break;
            }
            name = p->current;
            advance(p);
            skip_newlines(p);
            expect(p, TOK_COLON, "':'");
            skip_newlines(p);

            param = new_param(p);
            param->span = name.span;
            param->name = name.as.ident.text;
            param->name_len = name.as.ident.len;
            param->type = parse_type(p);

            if (tail != NULL) {
                tail->next = param;
            } else {
                head = param;
            }
            tail = param;

            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RPAREN)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RPAREN, "')'");
    return head;
}

static AstExpr *parse_fn_expr(Parser *p)
{
    AstExpr *e;
    AstParam *params;
    AstType *result;
    AstExpr *body;
    Span start;

    start = p->current.span;
    expect(p, TOK_KW_FN, "'fn'");
    params = parse_param_list(p);
    skip_newlines(p);
    expect(p, TOK_ARROW, "'->'");
    skip_newlines(p);
    result = parse_type(p);
    skip_newlines(p);
    expect(p, TOK_ASSIGN, "'='");
    skip_newlines(p);
    body = parse_expr(p);

    e = new_expr(p, EXPR_LAMBDA, start);
    e->as.lambda.params = params;
    e->as.lambda.result = result;
    e->as.lambda.body = body;
    return e;
}

/* ------------------------------------------------------------------ */
/* Patterns                                                            */
/* ------------------------------------------------------------------ */

static AstPattern *parse_pattern_ex(Parser *p, int allow_refutable,
                                    int allow_type_pattern)
{
    skip_newlines(p);

    if (at(p, TOK_UNDERSCORE)) {
        Span span;
        span = p->current.span;
        advance(p);
        return new_pattern(p, PAT_WILDCARD, span);
    }

    if (at(p, TOK_IDENT)) {
        Token id;
        AstPattern *pat;

        id = p->current;
        advance(p);

        if (at(p, TOK_LBRACE)) {
            AstPatternField *head;
            AstPatternField *tail;

            advance(p);
            skip_newlines(p);

            head = NULL;
            tail = NULL;

            if (!at(p, TOK_RBRACE)) {
                for (;;) {
                    Token field;
                    AstPatternField *pf;
                    AstPattern *sub;

                    if (!at(p, TOK_IDENT)) {
                        parser_error_expected(p, "a field name");
                        break;
                    }
                    field = p->current;
                    advance(p);
                    skip_newlines(p);

                    if (match_token(p, TOK_COLON)) {
                        skip_newlines(p);
                        sub = parse_pattern_ex(p, allow_refutable,
                                               allow_type_pattern);
                    } else {
                        sub = new_pattern(p, PAT_VAR, field.span);
                        sub->as.var.name = field.as.ident.text;
                        sub->as.var.name_len = field.as.ident.len;
                    }

                    pf = new_pattern_field(p);
                    pf->span = field.span;
                    pf->name = field.as.ident.text;
                    pf->name_len = field.as.ident.len;
                    pf->pattern = sub;

                    if (tail != NULL) {
                        tail->next = pf;
                    } else {
                        head = pf;
                    }
                    tail = pf;

                    skip_newlines(p);
                    if (match_token(p, TOK_COMMA)) {
                        skip_newlines(p);
                        if (at(p, TOK_RBRACE)) {
                            break;
                        }
                        continue;
                    }
                    break;
                }
            }

            expect(p, TOK_RBRACE, "'}'");
            pat = new_pattern(p, PAT_STRUCT, id.span);
            pat->as.struct_.name = id.as.ident.text;
            pat->as.struct_.name_len = id.as.ident.len;
            pat->as.struct_.fields = head;
            return pat;
        }

        if (at(p, TOK_LPAREN)) {
            AstPatternList *head;
            AstPatternList *tail;

            if (!allow_refutable) {
                parser_error(p, id.span, "refutable pattern in let binding");
            }

            advance(p);
            skip_newlines(p);

            head = NULL;
            tail = NULL;

            if (!at(p, TOK_RPAREN)) {
                for (;;) {
                    AstPattern *item;
                    item = parse_pattern_ex(p, allow_refutable,
                                            allow_type_pattern);
                    pattern_list_add(p, &head, &tail, item);
                    skip_newlines(p);
                    if (match_token(p, TOK_COMMA)) {
                        skip_newlines(p);
                        if (at(p, TOK_RPAREN)) {
                            break;
                        }
                        continue;
                    }
                    break;
                }
            }

            expect(p, TOK_RPAREN, "')'");
            pat = new_pattern(p, PAT_ENUM, id.span);
            pat->as.enum_.name = id.as.ident.text;
            pat->as.enum_.name_len = id.as.ident.len;
            pat->as.enum_.payload = head;
            return pat;
        }

        if (at(p, TOK_COLON) && allow_type_pattern) {
            AstType *type;
            advance(p);
            skip_newlines(p);
            type = parse_type(p);
            pat = new_pattern(p, PAT_TYPE, id.span);
            pat->as.type.name = id.as.ident.text;
            pat->as.type.name_len = id.as.ident.len;
            pat->as.type.type = type;
            return pat;
        }

        pat = new_pattern(p, PAT_VAR, id.span);
        pat->as.var.name = id.as.ident.text;
        pat->as.var.name_len = id.as.ident.len;
        return pat;
    }

    if (at(p, TOK_INT) || at(p, TOK_FLOAT) || at(p, TOK_CHAR) ||
        at(p, TOK_STRING) || at(p, TOK_BYTES) ||
        at(p, TOK_KW_TRUE) || at(p, TOK_KW_FALSE)) {
        AstExpr *literal;
        AstPattern *pat;

        if (!allow_refutable) {
            parser_error(p, p->current.span,
                         "literal pattern is not allowed in let binding");
        }
        literal = parse_literal_expr(p);
        pat = new_pattern(p, PAT_LITERAL, literal->span);
        pat->as.literal.literal = literal;
        return pat;
    }

    if (at(p, TOK_LPAREN)) {
        Span start;
        AstPattern *first;
        AstPattern *tuple;
        AstPatternList *head;
        AstPatternList *tail;

        start = p->current.span;
        advance(p);
        skip_newlines(p);

        if (match_token(p, TOK_RPAREN)) {
            return new_pattern(p, PAT_UNIT, start);
        }

        first = parse_pattern_ex(p, allow_refutable, allow_type_pattern);
        skip_newlines(p);

        if (!match_token(p, TOK_COMMA)) {
            expect(p, TOK_RPAREN, "')'");
            return first;
        }

        tuple = new_pattern(p, PAT_TUPLE, first->span);
        head = NULL;
        tail = NULL;
        pattern_list_add(p, &head, &tail, first);
        skip_newlines(p);

        if (at(p, TOK_RPAREN)) {
            parser_error_expected(p, "a second tuple element");
        }

        if (!at(p, TOK_RPAREN)) {
            for (;;) {
                AstPattern *item;
                item = parse_pattern_ex(p, allow_refutable,
                                        allow_type_pattern);
                pattern_list_add(p, &head, &tail, item);
                skip_newlines(p);
                if (match_token(p, TOK_COMMA)) {
                    skip_newlines(p);
                    if (at(p, TOK_RPAREN)) {
                        break;
                    }
                    continue;
                }
                break;
            }
        }

        tuple->as.tuple.items = head;
        expect(p, TOK_RPAREN, "')'");
        return tuple;
    }

    parser_error_expected(p, "a pattern");
    if (!at(p, TOK_EOF) && !at(p, TOK_RPAREN) && !at(p, TOK_RBRACKET) &&
        !at(p, TOK_RBRACE) && !at(p, TOK_COMMA) && !at(p, TOK_FAT_ARROW) &&
        !at(p, TOK_NEWLINE)) {
        advance(p);
    }
    return new_pattern(p, PAT_ERROR, p->current.span);
}

/* ------------------------------------------------------------------ */
/* Blocks and let                                                      */
/* ------------------------------------------------------------------ */

static AstBlockItem *parse_let(Parser *p)
{
    AstBlockItem *item;
    AstPattern *pattern;
    AstType *type;
    AstExpr *value;

    expect(p, TOK_KW_LET, "'let'");
    skip_newlines(p);
    pattern = parse_pattern_ex(p, 0, 0);
    skip_newlines(p);

    type = NULL;
    if (match_token(p, TOK_COLON)) {
        skip_newlines(p);
        type = parse_type(p);
    }

    skip_newlines(p);
    expect(p, TOK_ASSIGN, "'='");
    skip_newlines(p);
    value = parse_expr(p);

    item = new_block_item(p, BLOCK_LET, pattern->span);
    item->as.let.pattern = pattern;
    item->as.let.type = type;
    item->as.let.value = value;
    return item;
}

static AstBlockItem *parse_block_item(Parser *p)
{
    AstBlockItem *item;
    AstExpr *expr;

    if (at(p, TOK_KW_LET)) {
        return parse_let(p);
    }

    expr = parse_expr(p);
    item = new_block_item(p, BLOCK_EXPR, expr->span);
    item->as.expr = expr;
    return item;
}

static AstExpr *parse_block(Parser *p)
{
    AstExpr *e;
    AstBlockItem *head;
    AstBlockItem *tail;
    Span start;
    int saved_flag;

    start = p->current.span;
    expect(p, TOK_LBRACE, "'{'");
    skip_separators(p);

    saved_flag = p->no_struct_literal;
    p->no_struct_literal = 0;

    head = NULL;
    tail = NULL;

    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        AstBlockItem *item;

        item = parse_block_item(p);

        if (tail != NULL) {
            tail->next = item;
        } else {
            head = item;
        }
        tail = item;

        if (at(p, TOK_NEWLINE) || at(p, TOK_SEMI)) {
            skip_separators(p);
        } else if (!at(p, TOK_RBRACE)) {
            parser_error_expected(p, "a newline, ';', or '}'");
            while (!at(p, TOK_EOF) && !at(p, TOK_NEWLINE) &&
                   !at(p, TOK_SEMI) && !at(p, TOK_RBRACE)) {
                advance(p);
            }
            skip_separators(p);
        }
    }

    expect(p, TOK_RBRACE, "'}'");

    p->no_struct_literal = saved_flag;

    e = new_expr(p, EXPR_BLOCK, start);
    e->as.block.items = head;
    return e;
}

/* ------------------------------------------------------------------ */
/* Declarations                                                        */
/* ------------------------------------------------------------------ */

static AstDecl *parse_type_alias(Parser *p)
{
    AstDecl *d;
    Token name;
    AstType *type;

    expect(p, TOK_KW_TYPE, "'type'");
    if (!at(p, TOK_IDENT)) {
        parser_error_expected(p, "a type name");
        return new_decl(p, DECL_TYPE_ALIAS, p->current.span);
    }
    name = p->current;
    advance(p);
    skip_newlines(p);
    expect(p, TOK_ASSIGN, "'='");
    skip_newlines(p);
    type = parse_type(p);

    d = new_decl(p, DECL_TYPE_ALIAS, name.span);
    d->as.alias.name = name.as.ident.text;
    d->as.alias.name_len = name.as.ident.len;
    d->as.alias.type = type;
    return d;
}

static AstDecl *parse_struct_decl(Parser *p)
{
    AstDecl *d;
    Token name;
    AstField *head;
    AstField *tail;

    expect(p, TOK_KW_STRUCT, "'struct'");
    if (!at(p, TOK_IDENT)) {
        parser_error_expected(p, "a struct name");
        return new_decl(p, DECL_STRUCT, p->current.span);
    }
    name = p->current;
    advance(p);
    skip_newlines(p);
    expect(p, TOK_LBRACE, "'{'");
    skip_newlines(p);

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RBRACE)) {
        for (;;) {
            Token field;
            AstField *f;

            if (!at(p, TOK_IDENT)) {
                parser_error_expected(p, "a field name");
                break;
            }
            field = p->current;
            advance(p);
            skip_newlines(p);
            expect(p, TOK_COLON, "':'");
            skip_newlines(p);

            f = new_field(p);
            f->span = field.span;
            f->name = field.as.ident.text;
            f->name_len = field.as.ident.len;
            f->type = parse_type(p);

            if (tail != NULL) {
                tail->next = f;
            } else {
                head = f;
            }
            tail = f;

            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACE)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RBRACE, "'}'");

    d = new_decl(p, DECL_STRUCT, name.span);
    d->as.struct_.name = name.as.ident.text;
    d->as.struct_.name_len = name.as.ident.len;
    d->as.struct_.fields = head;
    return d;
}

static AstDecl *parse_enum_decl(Parser *p)
{
    AstDecl *d;
    Token name;
    AstVariant *head;
    AstVariant *tail;

    expect(p, TOK_KW_ENUM, "'enum'");
    if (!at(p, TOK_IDENT)) {
        parser_error_expected(p, "an enum name");
        return new_decl(p, DECL_ENUM, p->current.span);
    }
    name = p->current;
    advance(p);
    skip_newlines(p);
    expect(p, TOK_LBRACE, "'{'");
    skip_newlines(p);

    head = NULL;
    tail = NULL;

    if (!at(p, TOK_RBRACE)) {
        for (;;) {
            Token variant;
            AstVariant *v;

            if (!at(p, TOK_IDENT)) {
                parser_error_expected(p, "a variant name");
                break;
            }
            variant = p->current;
            advance(p);

            v = new_variant(p);
            v->span = variant.span;
            v->name = variant.as.ident.text;
            v->name_len = variant.as.ident.len;
            v->payload = NULL;

            skip_newlines(p);
            if (match_token(p, TOK_LPAREN)) {
                AstTypeList *payload_head;
                AstTypeList *payload_tail;

                skip_newlines(p);
                payload_head = NULL;
                payload_tail = NULL;

                if (at(p, TOK_RPAREN)) {
                    parser_error_expected(p, "a payload type");
                }

                if (!at(p, TOK_RPAREN)) {
                    for (;;) {
                        AstType *payload;
                        payload = parse_type(p);
                        type_list_add(p, &payload_head, &payload_tail,
                                      payload);
                        skip_newlines(p);
                        if (match_token(p, TOK_COMMA)) {
                            skip_newlines(p);
                            if (at(p, TOK_RPAREN)) {
                                break;
                            }
                            continue;
                        }
                        break;
                    }
                }

                expect(p, TOK_RPAREN, "')'");
                v->payload = payload_head;
            }

            if (tail != NULL) {
                tail->next = v;
            } else {
                head = v;
            }
            tail = v;

            skip_newlines(p);
            if (match_token(p, TOK_COMMA)) {
                skip_newlines(p);
                if (at(p, TOK_RBRACE)) {
                    break;
                }
                continue;
            }
            break;
        }
    }

    expect(p, TOK_RBRACE, "'}'");

    d = new_decl(p, DECL_ENUM, name.span);
    d->as.enum_.name = name.as.ident.text;
    d->as.enum_.name_len = name.as.ident.len;
    d->as.enum_.variants = head;
    return d;
}

static AstDecl *parse_fn_decl(Parser *p)
{
    AstDecl *d;
    Token name;
    AstParam *params;
    AstType *result;
    AstExpr *body;

    expect(p, TOK_KW_FN, "'fn'");
    if (!at(p, TOK_IDENT)) {
        parser_error_expected(p, "a function name");
        return new_decl(p, DECL_FN, p->current.span);
    }
    name = p->current;
    advance(p);
    params = parse_param_list(p);
    skip_newlines(p);
    expect(p, TOK_ARROW, "'->'");
    skip_newlines(p);
    result = parse_type(p);
    skip_newlines(p);
    expect(p, TOK_ASSIGN, "'='");
    skip_newlines(p);
    body = parse_expr(p);

    d = new_decl(p, DECL_FN, name.span);
    d->as.fn.name = name.as.ident.text;
    d->as.fn.name_len = name.as.ident.len;
    d->as.fn.params = params;
    d->as.fn.result = result;
    d->as.fn.body = body;
    return d;
}

static AstImport *parse_import(Parser *p)
{
    AstImport *import;
    Token path;

    expect(p, TOK_KW_IMPORT, "'import'");
    if (!at(p, TOK_STRING)) {
        parser_error_expected(p, "a string path");
        return new_import(p);
    }
    path = p->current;
    advance(p);

    import = new_import(p);
    import->span = path.span;
    import->path = path.as.str_lit.data;
    import->path_len = path.as.str_lit.len;
    return import;
}

static int is_decl_start(TokenKind kind)
{
    return kind == TOK_KW_TYPE || kind == TOK_KW_STRUCT ||
           kind == TOK_KW_ENUM || kind == TOK_KW_FN ||
           kind == TOK_KW_IMPORT;
}

static void synchronize_top_level(Parser *p)
{
    while (!at(p, TOK_EOF) && !is_decl_start(p->current.kind)) {
        advance(p);
    }
}

static AstDecl *parse_decl(Parser *p)
{
    switch (p->current.kind) {
    case TOK_KW_TYPE:
        return parse_type_alias(p);
    case TOK_KW_STRUCT:
        return parse_struct_decl(p);
    case TOK_KW_ENUM:
        return parse_enum_decl(p);
    case TOK_KW_FN:
        return parse_fn_decl(p);
    default:
        parser_error_expected(p, "a declaration");
        return new_decl(p, DECL_FN, p->current.span);
    }
}

void parser_init(Parser *p, Lexer *lx, Arena *arena)
{
    memset(p, 0, sizeof(Parser));
    p->lexer = lx;
    p->arena = arena;
    parser_load_next(p);
}

AstProgram *parser_parse_program(Parser *p)
{
    AstProgram *program;
    AstImport *import_head;
    AstImport *import_tail;
    AstDecl *decl_head;
    AstDecl *decl_tail;

    program = (AstProgram *)parser_alloc(p, sizeof(AstProgram));
    import_head = NULL;
    import_tail = NULL;
    decl_head = NULL;
    decl_tail = NULL;

    skip_separators(p);

    while (!at(p, TOK_EOF)) {
        if (at(p, TOK_KW_IMPORT)) {
            AstImport *import;
            import = parse_import(p);
            if (import_tail != NULL) {
                import_tail->next = import;
            } else {
                import_head = import;
            }
            import_tail = import;
        } else if (is_decl_start(p->current.kind)) {
            AstDecl *decl;
            decl = parse_decl(p);
            if (decl_tail != NULL) {
                decl_tail->next = decl;
            } else {
                decl_head = decl;
            }
            decl_tail = decl;
        } else {
            parser_error_expected(p, "a declaration or import");
            synchronize_top_level(p);
        }

        skip_separators(p);
    }

    program->imports = import_head;
    program->decls = decl_head;
    return program;
}

int parser_had_error(const Parser *p)
{
    return p->had_error;
}

Span parser_error_span(const Parser *p)
{
    return p->error_span;
}

const char *parser_error_message(const Parser *p)
{
    return p->error_message;
}
