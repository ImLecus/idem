#ifndef IDEM_IR_H
#define IDEM_IR_H

#include <stddef.h>

#include "arena.h"
#include "ast.h"
#include "resolver.h"
#include "types.h"

typedef struct IrExpr IrExpr;
typedef struct IrStmt IrStmt;
typedef struct IrFunction IrFunction;
typedef struct IrMatchArm IrMatchArm;
typedef struct IrCapture IrCapture;

typedef enum IrExprKind {
    IR_EXPR_UNIT,
    IR_EXPR_BOOL,
    IR_EXPR_INT,
    IR_EXPR_FLOAT,
    IR_EXPR_CHAR,
    IR_EXPR_STRING,
    IR_EXPR_BYTES,
    IR_EXPR_LOCAL,
    IR_EXPR_FUNCTION,
    IR_EXPR_CLOSURE,
    IR_EXPR_CALL,
    IR_EXPR_BUILTIN,
    IR_EXPR_STRUCT,
    IR_EXPR_ENUM,
    IR_EXPR_TUPLE,
    IR_EXPR_ARRAY,
    IR_EXPR_LIST,
    IR_EXPR_MAP,
    IR_EXPR_FIELD,
    IR_EXPR_PROJECT,
    IR_EXPR_INDEX,
    IR_EXPR_BINARY,
    IR_EXPR_UNARY
} IrExprKind;

typedef enum IrProjectKind {
    IR_PROJECT_TUPLE,
    IR_PROJECT_STRUCT,
    IR_PROJECT_ENUM
} IrProjectKind;

struct IrExpr {
    IrExprKind kind;
    Type *type;
    union {
        struct {
            int value;
        } bool_lit;
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
        struct {
            int slot;
        } local;
        struct {
            IrFunction *function;
        } function;
        struct {
            IrFunction *function;
            int *captures;
            size_t capture_count;
        } closure;
        struct {
            int direct;
            IrFunction *function;
            int callee;
            int *args;
            size_t argc;
        } call;
        struct {
            const char *name;
            size_t name_len;
            int *args;
            size_t argc;
            Type *arg_type;
        } builtin;
        struct {
            TypeInfo *info;
            int *fields;
            size_t count;
        } struct_lit;
        struct {
            TypeInfo *info;
            int variant;
            int *payload;
            size_t count;
        } enum_lit;
        struct {
            int *items;
            size_t count;
        } list_lit;
        struct {
            int *keys;
            int *values;
            size_t count;
        } map_lit;
        struct {
            int object;
            int index;
        } field;
        struct {
            IrProjectKind kind;
            int object;
            int index;
            TypeInfo *info;
        } project;
        struct {
            int object;
            int index;
        } index;
        struct {
            AstBinaryOp op;
            int left;
            int right;
            Type *operand_type;
        } binary;
        struct {
            AstUnaryOp op;
            int operand;
            Type *operand_type;
        } unary;
    } as;
};

typedef enum IrStmtKind {
    IR_STMT_LET,
    IR_STMT_IF,
    IR_STMT_MATCH,
    IR_STMT_BLOCK,
    IR_STMT_EXPR
} IrStmtKind;

struct IrStmt {
    IrStmtKind kind;
    IrStmt *next;
    union {
        struct {
            int dst;
            IrExpr *value;
        } let;
        struct {
            int cond;
            IrStmt *then_branch;
            IrStmt *else_branch;
        } if_;
        struct {
            int subject;
            IrMatchArm *arms;
        } match;
        struct {
            IrStmt *items;
        } block;
        struct {
            IrExpr *value;
        } expr;
    } as;
};

struct IrMatchArm {
    AstPattern *pattern;
    IrStmt *bindings;
    IrStmt *body;
    IrMatchArm *next;
};

struct IrCapture {
    void *key; /* IrBinding identity */
    int slot;
    int outer_slot;
    struct IrCapture *next;
};

struct IrFunction {
    int id;
    int is_lambda;
    const char *name;
    size_t name_len;
    Span span;
    Symbol *symbol; /* named functions only */
    IrFunction *parent;
    IrFunction *next;

    int param_count;
    int slot_count;
    int next_slot;
    int result_slot;
    Type *result_type;
    IrCapture *captures;
    IrCapture *captures_tail;

    IrStmt *body;
};

typedef struct IrFunctionMap {
    Symbol *symbol;
    IrFunction *function;
    struct IrFunctionMap *next;
} IrFunctionMap;

typedef struct IrProgram {
    Resolver *resolver;
    Arena *arena;
    IrFunction *functions;
    IrFunction *functions_tail;
    IrFunction *main_function;
    IrFunctionMap *function_map;
    int next_function_id;

    int had_error;
    Span error_span;
    char error_message[512];
} IrProgram;

void ir_init(IrProgram *program, Resolver *resolver, Arena *arena);
int ir_lower(IrProgram *program);

int ir_had_error(const IrProgram *program);
Span ir_error_span(const IrProgram *program);
const char *ir_error_message(const IrProgram *program);

#endif /* IDEM_IR_H */
