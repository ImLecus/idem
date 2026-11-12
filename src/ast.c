#include "ast.h"

#include <stdio.h>

#include "platform.h"

static void print_name(const char *s, size_t n)
{
    if (s != NULL && n > 0) {
        printf("%.*s", (int)n, s);
    }
}

static void dump_type(const AstType *type);

static void dump_type_list(const AstTypeList *list)
{
    const AstTypeList *it;
    int first;

    first = 1;
    for (it = list; it != NULL; it = it->next) {
        if (!first) {
            printf(", ");
        }
        dump_type(it->type);
        first = 0;
    }
}

static void dump_type(const AstType *type)
{
    if (type == NULL) {
        printf("<null>");
        return;
    }

    switch (type->kind) {
    case TYPE_NAMED:
        print_name(type->as.named.name, type->as.named.name_len);
        if (type->as.named.args != NULL) {
            printf("[");
            dump_type_list(type->as.named.args);
            printf("]");
        }
        break;
    case TYPE_TUPLE:
        printf("(");
        dump_type_list(type->as.tuple.items);
        printf(")");
        break;
    case TYPE_FUNCTION:
        printf("(");
        dump_type_list(type->as.fn.params);
        printf(") -> ");
        dump_type(type->as.fn.result);
        break;
    case TYPE_UNION: {
        const AstTypeList *it;
        int first;
        first = 1;
        for (it = type->as.union_.items; it != NULL; it = it->next) {
            if (!first) {
                printf(" | ");
            }
            dump_type(it->type);
            first = 0;
        }
        break;
    }
    case TYPE_UNIT:
        printf("()");
        break;
    case TYPE_ERROR:
        printf("<type-error>");
        break;
    }
}

static const char *binary_op_name(AstBinaryOp op)
{
    switch (op) {
    case BIN_ADD:     return "+";
    case BIN_SUB:     return "-";
    case BIN_MUL:     return "*";
    case BIN_DIV:     return "/";
    case BIN_MOD:     return "%";
    case BIN_EQ:      return "==";
    case BIN_NE:      return "!=";
    case BIN_LT:      return "<";
    case BIN_LE:      return "<=";
    case BIN_GT:      return ">";
    case BIN_GE:      return ">=";
    case BIN_AND:     return "&&";
    case BIN_OR:      return "||";
    case BIN_BIT_AND: return "&";
    case BIN_BIT_OR:  return "|";
    case BIN_BIT_XOR: return "^";
    case BIN_SHL:     return "<<";
    case BIN_SHR:     return ">>";
    }
    return "?";
}

static const char *unary_op_name(AstUnaryOp op)
{
    switch (op) {
    case UN_NEG:     return "-";
    case UN_NOT:     return "!";
    case UN_BIT_NOT: return "~";
    }
    return "?";
}

static void dump_expr(const AstExpr *expr);

static void dump_expr_list(const AstExprList *list)
{
    const AstExprList *it;
    int first;

    first = 1;
    for (it = list; it != NULL; it = it->next) {
        if (!first) {
            printf(", ");
        }
        dump_expr(it->expr);
        first = 0;
    }
}

static void dump_pattern(const AstPattern *pattern);

static void dump_pattern_list(const AstPatternList *list)
{
    const AstPatternList *it;
    int first;

    first = 1;
    for (it = list; it != NULL; it = it->next) {
        if (!first) {
            printf(", ");
        }
        dump_pattern(it->pattern);
        first = 0;
    }
}

static void dump_block_item(const AstBlockItem *item)
{
    if (item == NULL) {
        return;
    }

    switch (item->kind) {
    case BLOCK_LET:
        printf("(let ");
        dump_pattern(item->as.let.pattern);
        if (item->as.let.type != NULL) {
            printf(": ");
            dump_type(item->as.let.type);
        }
        printf(" = ");
        dump_expr(item->as.let.value);
        printf(")");
        break;
    case BLOCK_EXPR:
        dump_expr(item->as.expr);
        break;
    }
}

static void dump_match_arms(const AstMatchArm *arm)
{
    const AstMatchArm *it;
    for (it = arm; it != NULL; it = it->next) {
        printf("(arm ");
        dump_pattern(it->pattern);
        printf(" => ");
        dump_expr(it->body);
        printf(")");
        if (it->next != NULL) {
            printf(" ");
        }
    }
}

static void dump_expr(const AstExpr *expr)
{
    if (expr == NULL) {
        printf("<null>");
        return;
    }

    switch (expr->kind) {
    case EXPR_INT:
        printf("(int ");
        u64_print(expr->as.int_lit.value);
        printf(")");
        break;
    case EXPR_FLOAT:
        printf("(float %.17g)", expr->as.float_lit.value);
        break;
    case EXPR_BOOL:
        printf("(bool %s)", expr->as.bool_lit.value ? "true" : "false");
        break;
    case EXPR_CHAR:
        printf("(char U+%04lX)", (unsigned long)expr->as.char_lit.codepoint);
        break;
    case EXPR_STRING:
        printf("(string \"");
        print_name(expr->as.str_lit.data, expr->as.str_lit.len);
        printf("\")");
        break;
    case EXPR_BYTES:
        printf("(bytes \"");
        print_name(expr->as.str_lit.data, expr->as.str_lit.len);
        printf("\")");
        break;
    case EXPR_UNIT:
        printf("()");
        break;
    case EXPR_VAR:
        printf("(var ");
        print_name(expr->as.var.name, expr->as.var.name_len);
        printf(")");
        break;
    case EXPR_TUPLE:
        printf("(tuple ");
        dump_expr_list(expr->as.tuple.items);
        printf(")");
        break;
    case EXPR_ARRAY:
        printf("(array ");
        dump_expr_list(expr->as.array.items);
        printf(")");
        break;
    case EXPR_LIST:
        printf("(list ");
        dump_expr_list(expr->as.list.items);
        printf(")");
        break;
    case EXPR_MAP: {
        const AstMapEntry *it;
        printf("(map");
        for (it = expr->as.map.entries; it != NULL; it = it->next) {
            printf(" (entry ");
            dump_expr(it->key);
            printf(" ");
            dump_expr(it->value);
            printf(")");
        }
        printf(")");
        break;
    }
    case EXPR_STRUCT: {
        const AstFieldInit *it;
        printf("(struct ");
        print_name(expr->as.struct_.name, expr->as.struct_.name_len);
        for (it = expr->as.struct_.fields; it != NULL; it = it->next) {
            printf(" (field ");
            print_name(it->name, it->name_len);
            printf(" ");
            dump_expr(it->value);
            printf(")");
        }
        printf(")");
        break;
    }
    case EXPR_FIELD:
        printf("(field ");
        dump_expr(expr->as.field.object);
        printf(" . ");
        print_name(expr->as.field.field, expr->as.field.field_len);
        printf(")");
        break;
    case EXPR_INDEX:
        printf("(index ");
        dump_expr(expr->as.index.object);
        printf(" ");
        dump_expr(expr->as.index.index);
        printf(")");
        break;
    case EXPR_CALL:
        printf("(call ");
        dump_expr(expr->as.call.callee);
        if (expr->as.call.args != NULL) {
            printf(" ");
            dump_expr_list(expr->as.call.args);
        }
        printf(")");
        break;
    case EXPR_LAMBDA: {
        const AstParam *param;
        int first;
        printf("(lambda (");
        first = 1;
        for (param = expr->as.lambda.params; param != NULL;
             param = param->next) {
            if (!first) {
                printf(", ");
            }
            print_name(param->name, param->name_len);
            printf(": ");
            dump_type(param->type);
            first = 0;
        }
        printf(") -> ");
        dump_type(expr->as.lambda.result);
        printf(" ");
        dump_expr(expr->as.lambda.body);
        printf(")");
        break;
    }
    case EXPR_IF:
        printf("(if ");
        dump_expr(expr->as.if_.cond);
        printf(" then ");
        dump_expr(expr->as.if_.then_branch);
        printf(" else ");
        dump_expr(expr->as.if_.else_branch);
        printf(")");
        break;
    case EXPR_MATCH:
        printf("(match ");
        dump_expr(expr->as.match.scrutinee);
        printf(" ");
        dump_match_arms(expr->as.match.arms);
        printf(")");
        break;
    case EXPR_BLOCK: {
        const AstBlockItem *it;
        printf("(block");
        for (it = expr->as.block.items; it != NULL; it = it->next) {
            printf(" ");
            dump_block_item(it);
        }
        printf(")");
        break;
    }
    case EXPR_BINARY:
        printf("(%s ", binary_op_name(expr->as.binary.op));
        dump_expr(expr->as.binary.left);
        printf(" ");
        dump_expr(expr->as.binary.right);
        printf(")");
        break;
    case EXPR_UNARY:
        printf("(%s ", unary_op_name(expr->as.unary.op));
        dump_expr(expr->as.unary.operand);
        printf(")");
        break;
    case EXPR_ERROR:
        printf("<expr-error>");
        break;
    }
}

static void dump_pattern(const AstPattern *pattern)
{
    if (pattern == NULL) {
        printf("<null>");
        return;
    }

    switch (pattern->kind) {
    case PAT_WILDCARD:
        printf("_");
        break;
    case PAT_VAR:
        print_name(pattern->as.var.name, pattern->as.var.name_len);
        break;
    case PAT_LITERAL:
        printf("(lit ");
        dump_expr(pattern->as.literal.literal);
        printf(")");
        break;
    case PAT_UNIT:
        printf("()");
        break;
    case PAT_TUPLE:
        printf("(tuple ");
        dump_pattern_list(pattern->as.tuple.items);
        printf(")");
        break;
    case PAT_STRUCT: {
        const AstPatternField *it;
        printf("(struct ");
        print_name(pattern->as.struct_.name, pattern->as.struct_.name_len);
        for (it = pattern->as.struct_.fields; it != NULL; it = it->next) {
            printf(" (field ");
            print_name(it->name, it->name_len);
            printf(" ");
            dump_pattern(it->pattern);
            printf(")");
        }
        printf(")");
        break;
    }
    case PAT_ENUM:
        printf("(enum ");
        print_name(pattern->as.enum_.name, pattern->as.enum_.name_len);
        if (pattern->as.enum_.payload != NULL) {
            printf(" ");
            dump_pattern_list(pattern->as.enum_.payload);
        }
        printf(")");
        break;
    case PAT_TYPE:
        printf("(type ");
        print_name(pattern->as.type.name, pattern->as.type.name_len);
        printf(": ");
        dump_type(pattern->as.type.type);
        printf(")");
        break;
    case PAT_ERROR:
        printf("<pattern-error>");
        break;
    }
}

static void dump_decl(const AstDecl *decl)
{
    if (decl == NULL) {
        return;
    }

    switch (decl->kind) {
    case DECL_TYPE_ALIAS:
        printf("(type ");
        print_name(decl->as.alias.name, decl->as.alias.name_len);
        printf(" ");
        dump_type(decl->as.alias.type);
        printf(")\n");
        break;
    case DECL_STRUCT: {
        const AstField *it;
        printf("(struct ");
        print_name(decl->as.struct_.name, decl->as.struct_.name_len);
        for (it = decl->as.struct_.fields; it != NULL; it = it->next) {
            printf(" (field ");
            print_name(it->name, it->name_len);
            printf(": ");
            dump_type(it->type);
            printf(")");
        }
        printf(")\n");
        break;
    }
    case DECL_ENUM: {
        const AstVariant *it;
        printf("(enum ");
        print_name(decl->as.enum_.name, decl->as.enum_.name_len);
        for (it = decl->as.enum_.variants; it != NULL; it = it->next) {
            printf(" (variant ");
            print_name(it->name, it->name_len);
            if (it->payload != NULL) {
                printf(" ");
                dump_type_list(it->payload);
            }
            printf(")");
        }
        printf(")\n");
        break;
    }
    case DECL_FN: {
        const AstParam *param;
        int first;
        printf("(fn ");
        print_name(decl->as.fn.name, decl->as.fn.name_len);
        printf(" (");
        first = 1;
        for (param = decl->as.fn.params; param != NULL; param = param->next) {
            if (!first) {
                printf(", ");
            }
            print_name(param->name, param->name_len);
            printf(": ");
            dump_type(param->type);
            first = 0;
        }
        printf(") -> ");
        dump_type(decl->as.fn.result);
        printf(" ");
        dump_expr(decl->as.fn.body);
        printf(")\n");
        break;
    }
    }
}

void ast_dump_program(const AstProgram *program)
{
    const AstImport *import;
    const AstDecl *decl;

    if (program == NULL) {
        return;
    }

    for (import = program->imports; import != NULL; import = import->next) {
        printf("(import \"");
        print_name(import->path, import->path_len);
        printf("\")\n");
    }

    for (decl = program->decls; decl != NULL; decl = decl->next) {
        dump_decl(decl);
    }
}
