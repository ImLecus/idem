#ifndef IDEM_AST_H
#define IDEM_AST_H

#include <stddef.h>

#include "platform.h"
#include "token.h"

typedef struct AstType AstType;
typedef struct AstExpr AstExpr;
typedef struct AstPattern AstPattern;
typedef struct AstDecl AstDecl;
typedef struct AstParam AstParam;
struct Type;

/* ------------------------------------------------------------------ */
/* Generic lists                                                       */
/* ------------------------------------------------------------------ */

typedef struct AstTypeList {
    AstType *type;
    struct AstTypeList *next;
} AstTypeList;

typedef struct AstExprList {
    AstExpr *expr;
    struct AstExprList *next;
} AstExprList;

typedef struct AstPatternList {
    AstPattern *pattern;
    struct AstPatternList *next;
} AstPatternList;

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef enum AstTypeKind {
    TYPE_NAMED,
    TYPE_TUPLE,
    TYPE_FUNCTION,
    TYPE_UNION,
    TYPE_UNIT,
    TYPE_ERROR
} AstTypeKind;

struct AstType {
    AstTypeKind kind;
    Span span;
    union {
        struct {
            const char *name;
            size_t name_len;
            AstTypeList *args; /* NULL when not an application */
        } named;
        struct {
            AstTypeList *items;
        } tuple;
        struct {
            AstTypeList *params;
            AstType *result;
        } fn;
        struct {
            AstTypeList *items;
        } union_;
    } as;
};

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

typedef enum AstBinaryOp {
    BIN_ADD,
    BIN_SUB,
    BIN_MUL,
    BIN_DIV,
    BIN_MOD,
    BIN_EQ,
    BIN_NE,
    BIN_LT,
    BIN_LE,
    BIN_GT,
    BIN_GE,
    BIN_AND,
    BIN_OR,
    BIN_BIT_AND,
    BIN_BIT_OR,
    BIN_BIT_XOR,
    BIN_SHL,
    BIN_SHR
} AstBinaryOp;

typedef enum AstUnaryOp {
    UN_NEG,
    UN_NOT,
    UN_BIT_NOT
} AstUnaryOp;

typedef enum AstExprKind {
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_BOOL,
    EXPR_CHAR,
    EXPR_STRING,
    EXPR_BYTES,
    EXPR_UNIT,
    EXPR_VAR,
    EXPR_TUPLE,
    EXPR_ARRAY,
    EXPR_LIST,
    EXPR_MAP,
    EXPR_STRUCT,
    EXPR_FIELD,
    EXPR_INDEX,
    EXPR_CALL,
    EXPR_LAMBDA,
    EXPR_IF,
    EXPR_MATCH,
    EXPR_BLOCK,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_ERROR
} AstExprKind;

typedef struct AstFieldInit {
    Span span;
    const char *name;
    size_t name_len;
    AstExpr *value;
    struct AstFieldInit *next;
} AstFieldInit;

typedef struct AstMapEntry {
    Span span;
    AstExpr *key;
    AstExpr *value;
    struct AstMapEntry *next;
} AstMapEntry;

typedef struct AstMatchArm {
    Span span;
    AstPattern *pattern;
    AstExpr *body;
    struct AstMatchArm *next;
} AstMatchArm;

typedef enum AstBlockItemKind {
    BLOCK_LET,
    BLOCK_EXPR
} AstBlockItemKind;

typedef struct AstBlockItem {
    AstBlockItemKind kind;
    Span span;
    union {
        struct {
            AstPattern *pattern;
            AstType *type; /* NULL when not annotated */
            AstExpr *value;
        } let;
        AstExpr *expr;
    } as;
    struct AstBlockItem *next;
} AstBlockItem;

struct AstExpr {
    AstExprKind kind;
    Span span;
    struct Type *type;
    union {
        struct {
            u64 value;
        } int_lit;
        struct {
            double value;
        } float_lit;
        struct {
            int value;
        } bool_lit;
        struct {
            u32 codepoint;
        } char_lit;
        struct {
            const char *data;
            size_t len;
        } str_lit;
        struct {
            const char *name;
            size_t name_len;
        } var;
        struct {
            AstExprList *items;
        } tuple;
        struct {
            AstExprList *items;
        } array;
        struct {
            AstExprList *items;
        } list;
        struct {
            AstMapEntry *entries;
        } map;
        struct {
            const char *name;
            size_t name_len;
            AstFieldInit *fields;
        } struct_;
        struct {
            AstExpr *object;
            const char *field;
            size_t field_len;
        } field;
        struct {
            AstExpr *object;
            AstExpr *index;
        } index;
        struct {
            AstExpr *callee;
            AstExprList *args;
        } call;
        struct {
            AstParam *params;
            AstType *result;
            AstExpr *body;
        } lambda;
        struct {
            AstExpr *cond;
            AstExpr *then_branch;
            AstExpr *else_branch;
        } if_;
        struct {
            AstExpr *scrutinee;
            AstMatchArm *arms;
        } match;
        struct {
            AstBlockItem *items;
        } block;
        struct {
            AstBinaryOp op;
            AstExpr *left;
            AstExpr *right;
        } binary;
        struct {
            AstUnaryOp op;
            AstExpr *operand;
        } unary;
    } as;
};

/* ------------------------------------------------------------------ */
/* Patterns                                                            */
/* ------------------------------------------------------------------ */

typedef struct AstPatternField {
    Span span;
    const char *name;
    size_t name_len;
    AstPattern *pattern;
    struct AstPatternField *next;
} AstPatternField;

typedef enum AstPatternKind {
    PAT_WILDCARD,
    PAT_VAR,
    PAT_LITERAL,
    PAT_UNIT,
    PAT_TUPLE,
    PAT_STRUCT,
    PAT_ENUM,
    PAT_TYPE,
    PAT_ERROR
} AstPatternKind;

struct AstPattern {
    AstPatternKind kind;
    Span span;
    int is_binding; /* struct-pattern shorthand is always a binding */
    struct Type *sem_type;
    int slot;       /* lowering: local slot for bindings */
    union {
        struct {
            const char *name;
            size_t name_len;
        } var;
        struct {
            AstExpr *literal;
        } literal;
        struct {
            AstPatternList *items;
        } tuple;
        struct {
            const char *name;
            size_t name_len;
            AstPatternField *fields;
        } struct_;
        struct {
            const char *name;
            size_t name_len;
            AstPatternList *payload; /* non-NULL when parentheses were used */
        } enum_;
        struct {
            const char *name;
            size_t name_len;
            AstType *type;
        } type;
    } as;
};

/* ------------------------------------------------------------------ */
/* Declarations                                                        */
/* ------------------------------------------------------------------ */

struct AstParam {
    Span span;
    const char *name;
    size_t name_len;
    AstType *type;
    struct AstParam *next;
};

typedef struct AstField {
    Span span;
    const char *name;
    size_t name_len;
    AstType *type;
    struct AstField *next;
} AstField;

typedef struct AstVariant {
    Span span;
    const char *name;
    size_t name_len;
    AstTypeList *payload; /* NULL when no parentheses */
    struct AstVariant *next;
} AstVariant;

typedef enum AstDeclKind {
    DECL_TYPE_ALIAS,
    DECL_STRUCT,
    DECL_ENUM,
    DECL_FN
} AstDeclKind;

struct AstDecl {
    AstDeclKind kind;
    Span span;
    union {
        struct {
            const char *name;
            size_t name_len;
            AstType *type;
        } alias;
        struct {
            const char *name;
            size_t name_len;
            AstField *fields;
        } struct_;
        struct {
            const char *name;
            size_t name_len;
            AstVariant *variants;
        } enum_;
        struct {
            const char *name;
            size_t name_len;
            AstParam *params;
            AstType *result;
            AstExpr *body;
        } fn;
    } as;
    struct AstDecl *next;
};

typedef struct AstImport {
    Span span;
    const char *path;
    size_t path_len;
    struct AstImport *next;
} AstImport;

typedef struct AstProgram {
    AstImport *imports;
    AstDecl *decls;
} AstProgram;

void ast_dump_program(const AstProgram *program);

#endif /* IDEM_AST_H */
