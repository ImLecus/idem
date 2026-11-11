#include "ir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Allocation and diagnostics                                          */
/* ------------------------------------------------------------------ */

typedef struct IrLower IrLower;
typedef struct IrBinding IrBinding;
typedef struct IrStmtList IrStmtList;

struct IrBinding {
    const char *name;
    size_t name_len;
    int slot;
    IrFunction *owner;
    IrBinding *next;
};

struct IrStmtList {
    IrStmt *head;
    IrStmt *tail;
};

struct IrLower {
    IrProgram *program;
    Resolver *resolver;
    Module *module;
    IrFunction *function;
    IrBinding *bindings;
};

static void *ir_alloc(IrProgram *program, size_t size)
{
    void *p;
    p = arena_alloc(program->arena, size);
    if (p == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    memset(p, 0, size);
    return p;
}

static void ir_error(IrProgram *program, Span span, const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    size_t out;
    const char *p;

    if (program->had_error) {
        return;
    }

    out = 0;
    p = fmt;
    va_start(ap, fmt);

    while (*p != '\0' && out + 1 < sizeof(buf)) {
        if (*p != '%') {
            buf[out++] = *p++;
            continue;
        }
        if (p[1] == 's') {
            const char *s;
            s = va_arg(ap, const char *);
            p += 2;
            while (s != NULL && *s != '\0' && out + 1 < sizeof(buf)) {
                buf[out++] = *s++;
            }
        } else if (p[1] == '.' && p[2] == '*' && p[3] == 's') {
            int n;
            const char *s;
            n = va_arg(ap, int);
            s = va_arg(ap, const char *);
            p += 4;
            while (n > 0 && s != NULL && *s != '\0' &&
                   out + 1 < sizeof(buf)) {
                buf[out++] = *s++;
                n -= 1;
            }
        } else if (p[1] == '%') {
            buf[out++] = '%';
            p += 2;
        } else {
            buf[out++] = '%';
            p += 1;
        }
    }

    va_end(ap);
    buf[out] = '\0';

    program->had_error = 1;
    program->error_span = span;
    strcpy(program->error_message, buf);
}

/* ------------------------------------------------------------------ */
/* Lists, slots, and bindings                                          */
/* ------------------------------------------------------------------ */

static size_t expr_list_count(AstExprList *list)
{
    size_t n;
    n = 0;
    while (list != NULL) {
        n += 1;
        list = list->next;
    }
    return n;
}

static int alloc_slot(IrFunction *function)
{
    int slot;
    slot = function->next_slot;
    function->next_slot += 1;
    if (function->next_slot > function->slot_count) {
        function->slot_count = function->next_slot;
    }
    return slot;
}

static int *alloc_slots(IrProgram *program, int count)
{
    int *slots;
    if (count <= 0) {
        return NULL;
    }
    slots = (int *)ir_alloc(program, sizeof(int) * (size_t)count);
    return slots;
}

static IrStmt *new_stmt(IrProgram *program, IrStmtKind kind)
{
    IrStmt *stmt;
    stmt = (IrStmt *)ir_alloc(program, sizeof(IrStmt));
    stmt->kind = kind;
    return stmt;
}

static IrExpr *new_expr(IrProgram *program, IrExprKind kind, Type *type)
{
    IrExpr *expr;
    expr = (IrExpr *)ir_alloc(program, sizeof(IrExpr));
    expr->kind = kind;
    expr->type = type;
    return expr;
}

static void stmt_add(IrStmtList *list, IrStmt *stmt)
{
    if (stmt == NULL) {
        return;
    }
    if (list->tail != NULL) {
        list->tail->next = stmt;
    } else {
        list->head = stmt;
    }
    list->tail = stmt;
}

static void stmt_let(IrLower *lower, IrStmtList *list, int dst, IrExpr *value)
{
    IrStmt *stmt;
    stmt = new_stmt(lower->program, IR_STMT_LET);
    stmt->as.let.dst = dst;
    stmt->as.let.value = value;
    stmt_add(list, stmt);
}

static void stmt_assign_unit(IrLower *lower, IrStmtList *list, int dst)
{
    stmt_let(lower, list, dst,
             new_expr(lower->program, IR_EXPR_UNIT,
                      lower->resolver->ty_unit));
}

static void stmt_assign_bool(IrLower *lower, IrStmtList *list, int dst,
                             int value)
{
    IrExpr *expr;
    expr = new_expr(lower->program, IR_EXPR_BOOL,
                    lower->resolver->ty_bool);
    expr->as.bool_lit.value = value;
    stmt_let(lower, list, dst, expr);
}

static IrBinding *binding_find(IrLower *lower, const char *name, size_t len)
{
    IrBinding *b;
    for (b = lower->bindings; b != NULL; b = b->next) {
        if (b->name_len == len && memcmp(b->name, name, len) == 0) {
            return b;
        }
    }
    return NULL;
}

static void binding_add(IrLower *lower, const char *name, size_t len,
                        int slot)
{
    IrBinding *b;
    b = (IrBinding *)ir_alloc(lower->program, sizeof(IrBinding));
    b->name = name;
    b->name_len = len;
    b->slot = slot;
    b->owner = lower->function;
    b->next = lower->bindings;
    lower->bindings = b;
}

static IrFunction *find_function(IrProgram *program, Symbol *symbol)
{
    IrFunctionMap *map;
    for (map = program->function_map; map != NULL; map = map->next) {
        if (map->symbol == symbol) {
            return map->function;
        }
    }
    return NULL;
}

static int ensure_capture(IrLower *lower, IrFunction *function,
                          IrBinding *binding)
{
    IrCapture *cap;
    int outer_slot;

    for (cap = function->captures; cap != NULL; cap = cap->next) {
        if (cap->key == (void *)binding) {
            return cap->slot;
        }
    }

    if (function->parent == NULL) {
        ir_error(lower->program, function->span,
                 "internal: capture in a named function");
        return 0;
    }

    if (binding->owner == function->parent) {
        outer_slot = binding->slot;
    } else {
        outer_slot = ensure_capture(lower, function->parent, binding);
    }

    cap = (IrCapture *)ir_alloc(lower->program, sizeof(IrCapture));
    cap->key = (void *)binding;
    cap->slot = alloc_slot(function);
    cap->outer_slot = outer_slot;

    if (function->captures_tail != NULL) {
        function->captures_tail->next = cap;
    } else {
        function->captures = cap;
    }
    function->captures_tail = cap;

    return cap->slot;
}

/* ------------------------------------------------------------------ */
/* Type helpers                                                        */
/* ------------------------------------------------------------------ */

static int variant_index(TypeInfo *info, const char *name, size_t len)
{
    TypeVariant *variant;
    int index;

    index = 0;
    for (variant = info->variants; variant != NULL;
         variant = variant->next) {
        if (variant->name_len == len &&
            memcmp(variant->name, name, len) == 0) {
            return index;
        }
        index += 1;
    }
    return -1;
}

static int field_index(TypeInfo *info, const char *name, size_t len)
{
    TypeField *field;
    int index;

    index = 0;
    for (field = info->fields; field != NULL; field = field->next) {
        if (field->name_len == len &&
            memcmp(field->name, name, len) == 0) {
            return index;
        }
        index += 1;
    }
    return -1;
}

static AstPattern *find_pattern_field(AstPatternField *fields,
                                      const char *name, size_t len)
{
    AstPatternField *f;
    for (f = fields; f != NULL; f = f->next) {
        if (f->name_len == len && memcmp(f->name, name, len) == 0) {
            return f->pattern;
        }
    }
    return NULL;
}

static const char *BUILTIN_NAMES[] = {
    "print", "println", "readLine", "readFile", "writeFile", "flush",
    "exit", "panic",
    "len", "get", "set", "remove", "push", "concat", "map", "filter",
    "fold", "sum", "keys", "values",
    "abs", "min", "max", "sqrt", "floor", "ceil", "pow", "random", "now",
    "byteAt", "slice", "compare", "charAt", "chars", "toInt", "toFloat",
    "toString", "toBytes", "fromBytes"
};

static int is_builtin_name(const char *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof(BUILTIN_NAMES) / sizeof(BUILTIN_NAMES[0]); i++) {
        size_t n;
        n = strlen(BUILTIN_NAMES[i]);
        if (n == len && memcmp(name, BUILTIN_NAMES[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Functions                                                           */
/* ------------------------------------------------------------------ */

static IrFunction *new_function(IrProgram *program,
                                const char *name, size_t name_len,
                                Span span, Symbol *symbol,
                                int is_lambda, IrFunction *parent)
{
    IrFunction *fn;
    IrFunctionMap *map;

    fn = (IrFunction *)ir_alloc(program, sizeof(IrFunction));
    fn->id = program->next_function_id;
    program->next_function_id += 1;
    fn->name = name;
    fn->name_len = name_len;
    fn->span = span;
    fn->symbol = symbol;
    fn->is_lambda = is_lambda;
    fn->parent = parent;

    if (program->functions_tail != NULL) {
        program->functions_tail->next = fn;
    } else {
        program->functions = fn;
    }
    program->functions_tail = fn;

    if (symbol != NULL) {
        map = (IrFunctionMap *)ir_alloc(program, sizeof(IrFunctionMap));
        map->symbol = symbol;
        map->function = fn;
        map->next = program->function_map;
        program->function_map = map;
    }

    return fn;
}

/* ------------------------------------------------------------------ */
/* Expression lowering                                                 */
/* ------------------------------------------------------------------ */

static void lower_expr(IrLower *lower, IrStmtList *out,
                       AstExpr *expr, int dst);

static void lower_pattern_bindings(IrLower *lower, IrStmtList *out,
                                   AstPattern *pattern, int value_slot,
                                   int register_names);

static int *lower_expr_list(IrLower *lower, IrStmtList *out,
                            AstExprList *items, size_t *count_out)
{
    size_t count;
    size_t i;
    int *slots;
    AstExprList *item;

    count = expr_list_count(items);
    slots = alloc_slots(lower->program, (int)count);
    i = 0;
    for (item = items; item != NULL; item = item->next) {
        slots[i] = alloc_slot(lower->function);
        lower_expr(lower, out, item->expr, slots[i]);
        i += 1;
    }
    *count_out = count;
    return slots;
}

static IrExpr *enum_constructor(IrProgram *program, TypeInfo *info,
                                int variant, int *payload, size_t count,
                                Type *type)
{
    IrExpr *expr;
    expr = new_expr(program, IR_EXPR_ENUM, type);
    expr->as.enum_lit.info = info;
    expr->as.enum_lit.variant = variant;
    expr->as.enum_lit.payload = payload;
    expr->as.enum_lit.count = count;
    return expr;
}

static void lower_map(IrLower *lower, IrStmtList *out,
                      AstExpr *expr, int dst)
{
    size_t count;
    size_t i;
    int *keys;
    int *values;
    AstMapEntry *entry;
    IrExpr *result;

    count = 0;
    for (entry = expr->as.map.entries; entry != NULL; entry = entry->next) {
        count += 1;
    }

    keys = alloc_slots(lower->program, (int)count);
    values = alloc_slots(lower->program, (int)count);

    i = 0;
    for (entry = expr->as.map.entries; entry != NULL; entry = entry->next) {
        keys[i] = alloc_slot(lower->function);
        lower_expr(lower, out, entry->key, keys[i]);
        values[i] = alloc_slot(lower->function);
        lower_expr(lower, out, entry->value, values[i]);
        i += 1;
    }

    result = new_expr(lower->program, IR_EXPR_MAP, expr->type);
    result->as.map_lit.keys = keys;
    result->as.map_lit.values = values;
    result->as.map_lit.count = count;
    stmt_let(lower, out, dst, result);
}

static void lower_struct(IrLower *lower, IrStmtList *out,
                         AstExpr *expr, int dst)
{
    TypeInfo *info;
    AstFieldInit *init;
    TypeField *field;
    int *fields;
    int *slot_for_index;
    size_t count;
    size_t i;
    IrExpr *result;

    if (expr->type == NULL || expr->type->kind != TY_STRUCT) {
        ir_error(lower->program, expr->span,
                 "internal: struct literal without a struct type");
        return;
    }

    info = expr->type->as.nominal.info;
    count = 0;
    for (field = info->fields; field != NULL; field = field->next) {
        count += 1;
    }

    fields = alloc_slots(lower->program, (int)count);
    slot_for_index = alloc_slots(lower->program, (int)count);
    for (i = 0; i < count; i++) {
        slot_for_index[i] = -1;
    }

    for (init = expr->as.struct_.fields; init != NULL; init = init->next) {
        int index;
        int slot;
        index = field_index(info, init->name, init->name_len);
        if (index < 0) {
            continue;
        }
        slot = alloc_slot(lower->function);
        lower_expr(lower, out, init->value, slot);
        slot_for_index[index] = slot;
    }

    for (i = 0; i < count; i++) {
        fields[i] = slot_for_index[i];
    }

    result = new_expr(lower->program, IR_EXPR_STRUCT, expr->type);
    result->as.struct_lit.info = info;
    result->as.struct_lit.fields = fields;
    result->as.struct_lit.count = count;
    stmt_let(lower, out, dst, result);
}

static void lower_call(IrLower *lower, IrStmtList *out,
                       AstExpr *expr, int dst)
{
    AstExpr *callee;
    IrExpr *result;
    size_t argc;
    int *args;
    int direct;
    IrFunction *function;
    Symbol *symbol;

    callee = expr->as.call.callee;
    direct = 0;
    function = NULL;
    symbol = NULL;
    args = NULL;
    argc = 0;

    if (callee->kind == EXPR_VAR) {
        IrBinding *binding;
        binding = binding_find(lower, callee->as.var.name,
                               callee->as.var.name_len);
        if (binding == NULL) {
            if (is_builtin_name(callee->as.var.name,
                                callee->as.var.name_len)) {
                Type *arg_type;
                IrExpr *builtin;
                args = lower_expr_list(lower, out, expr->as.call.args,
                                       &argc);
                arg_type = NULL;
                if (expr->as.call.args != NULL) {
                    arg_type = expr->as.call.args->expr->type;
                }
                builtin = new_expr(lower->program, IR_EXPR_BUILTIN,
                                   expr->type);
                builtin->as.builtin.name = callee->as.var.name;
                builtin->as.builtin.name_len = callee->as.var.name_len;
                builtin->as.builtin.args = args;
                builtin->as.builtin.argc = argc;
                builtin->as.builtin.arg_type = arg_type;
                stmt_let(lower, out, dst, builtin);
                return;
            }

            symbol = resolver_lookup(lower->resolver, lower->module,
                                     callee->as.var.name,
                                     callee->as.var.name_len);
            if (symbol != NULL && symbol->kind == SYM_FN) {
                function = find_function(lower->program, symbol);
                direct = 1;
            } else if (symbol != NULL && symbol->kind == SYM_VARIANT) {
                TypeVariant *variant;
                int index;
                variant = symbol->as.variant.variant;
                index = variant_index(symbol->as.variant.owner,
                                      callee->as.var.name,
                                      callee->as.var.name_len);
                args = lower_expr_list(lower, out, expr->as.call.args,
                                       &argc);
                result = enum_constructor(lower->program,
                                          symbol->as.variant.owner,
                                          index, args, argc, expr->type);
                stmt_let(lower, out, dst, result);
                (void)variant;
                return;
            }
        }
    }

    if (direct && function != NULL) {
        args = lower_expr_list(lower, out, expr->as.call.args, &argc);
        result = new_expr(lower->program, IR_EXPR_CALL, expr->type);
        result->as.call.direct = 1;
        result->as.call.function = function;
        result->as.call.callee = -1;
        result->as.call.args = args;
        result->as.call.argc = argc;
        stmt_let(lower, out, dst, result);
        return;
    }

    {
        int callee_slot;
        callee_slot = alloc_slot(lower->function);
        lower_expr(lower, out, callee, callee_slot);
        args = lower_expr_list(lower, out, expr->as.call.args, &argc);
        result = new_expr(lower->program, IR_EXPR_CALL, expr->type);
        result->as.call.direct = 0;
        result->as.call.function = NULL;
        result->as.call.callee = callee_slot;
        result->as.call.args = args;
        result->as.call.argc = argc;
        stmt_let(lower, out, dst, result);
    }
}

static void lower_lambda(IrLower *lower, IrStmtList *out,
                         AstExpr *expr, int dst)
{
    IrFunction *saved_function;
    IrBinding *saved_bindings;
    IrFunction *lambda;
    AstParam *param;
    int index;
    IrStmtList body;
    IrExpr *closure;
    IrCapture *cap;
    int *captures;
    size_t capture_count;
    size_t i;

    saved_function = lower->function;
    saved_bindings = lower->bindings;

    lambda = new_function(lower->program, NULL, 0, expr->span, NULL, 1,
                          saved_function);

    lower->function = lambda;

    index = 0;
    for (param = expr->as.lambda.params; param != NULL; param = param->next) {
        int slot;
        slot = alloc_slot(lambda);
        binding_add(lower, param->name, param->name_len, slot);
        index += 1;
    }
    lambda->param_count = index;

    lambda->result_type = expr->type != NULL &&
                                  expr->type->kind == TY_FUNCTION
                              ? expr->type->as.fn.result
                              : lower->resolver->ty_unit;
    lambda->result_slot = alloc_slot(lambda);

    lower->function = lambda;
    memset(&body, 0, sizeof(body));
    lower_expr(lower, &body, expr->as.lambda.body, lambda->result_slot);
    lambda->body = body.head;

    lower->function = saved_function;
    lower->bindings = saved_bindings;

    capture_count = 0;
    for (cap = lambda->captures; cap != NULL; cap = cap->next) {
        capture_count += 1;
    }
    captures = alloc_slots(lower->program, (int)capture_count);
    i = 0;
    for (cap = lambda->captures; cap != NULL; cap = cap->next) {
        captures[i] = cap->outer_slot;
        i += 1;
    }

    closure = new_expr(lower->program, IR_EXPR_CLOSURE, expr->type);
    closure->as.closure.function = lambda;
    closure->as.closure.captures = captures;
    closure->as.closure.capture_count = capture_count;
    stmt_let(lower, out, dst, closure);
}

/* ------------------------------------------------------------------ */
/* Pattern bindings                                                    */
/* ------------------------------------------------------------------ */

static IrExpr *project_expr(IrProgram *program, IrProjectKind kind,
                            int object, int index, TypeInfo *info,
                            Type *type)
{
    IrExpr *expr;
    expr = new_expr(program, IR_EXPR_PROJECT, type);
    expr->as.project.kind = kind;
    expr->as.project.object = object;
    expr->as.project.index = index;
    expr->as.project.info = info;
    return expr;
}

static void lower_pattern_bindings(IrLower *lower, IrStmtList *out,
                                   AstPattern *pattern, int value_slot,
                                   int register_names)
{
    if (pattern == NULL) {
        return;
    }

    switch (pattern->kind) {
    case PAT_WILDCARD:
        break;

    case PAT_VAR:
        if (!pattern->is_binding &&
            pattern->sem_type != NULL &&
            pattern->sem_type->kind == TY_ENUM) {
            TypeVariant *variant;
            variant = NULL;
            {
                TypeInfo *info;
                TypeVariant *v;
                info = pattern->sem_type->as.nominal.info;
                for (v = info->variants; v != NULL; v = v->next) {
                    if (v->name_len == pattern->as.var.name_len &&
                        memcmp(v->name, pattern->as.var.name,
                               v->name_len) == 0) {
                        variant = v;
                        break;
                    }
                }
            }
            if (variant != NULL) {
                return;
            }
        }
        pattern->slot = value_slot;
        if (register_names) {
            binding_add(lower, pattern->as.var.name,
                        pattern->as.var.name_len, value_slot);
        }
        break;

    case PAT_TYPE:
        pattern->slot = value_slot;
        if (register_names) {
            binding_add(lower, pattern->as.type.name,
                        pattern->as.type.name_len, value_slot);
        }
        break;

    case PAT_TUPLE: {
        AstPatternList *item;
        int index;
        index = 0;
        for (item = pattern->as.tuple.items; item != NULL;
             item = item->next) {
            int slot;
            slot = alloc_slot(lower->function);
            stmt_let(lower, out, slot,
                     project_expr(lower->program, IR_PROJECT_TUPLE,
                                  value_slot, index, NULL,
                                  item->pattern->sem_type));
            lower_pattern_bindings(lower, out, item->pattern, slot,
                                   register_names);
            index += 1;
        }
        break;
    }

    case PAT_STRUCT: {
        TypeInfo *info;
        TypeField *field;
        int index;
        if (pattern->sem_type == NULL ||
            pattern->sem_type->kind != TY_STRUCT) {
            return;
        }
        info = pattern->sem_type->as.nominal.info;
        index = 0;
        for (field = info->fields; field != NULL; field = field->next) {
            AstPattern *sub;
            int slot;
            sub = find_pattern_field(pattern->as.struct_.fields,
                                     field->name, field->name_len);
            if (sub != NULL) {
                slot = alloc_slot(lower->function);
                stmt_let(lower, out, slot,
                         project_expr(lower->program, IR_PROJECT_STRUCT,
                                      value_slot, index, info,
                                      field->type));
                lower_pattern_bindings(lower, out, sub, slot,
                                       register_names);
            }
            index += 1;
        }
        break;
    }

    case PAT_ENUM: {
        TypeInfo *info;
        TypeList *payload_type;
        AstPatternList *item;
        int variant;
        int index;
        if (pattern->sem_type == NULL ||
            pattern->sem_type->kind != TY_ENUM) {
            return;
        }
        info = pattern->sem_type->as.nominal.info;
        variant = variant_index(info, pattern->as.enum_.name,
                                pattern->as.enum_.name_len);
        index = 0;
        payload_type = NULL;
        {
            TypeVariant *v;
            int i;
            i = 0;
            for (v = info->variants; v != NULL; v = v->next) {
                if (i == variant) {
                    payload_type = v->payload;
                    break;
                }
                i += 1;
            }
        }
        for (item = pattern->as.enum_.payload; item != NULL;
             item = item->next) {
            int slot;
            slot = alloc_slot(lower->function);
            stmt_let(lower, out, slot,
                     project_expr(lower->program, IR_PROJECT_ENUM,
                                  value_slot, index, info,
                                  payload_type != NULL
                                      ? payload_type->type : NULL));
            lower_pattern_bindings(lower, out, item->pattern, slot,
                                   register_names);
            index += 1;
            if (payload_type != NULL) {
                payload_type = payload_type->next;
            }
        }
        break;
    }

    case PAT_LITERAL:
    case PAT_UNIT:
    case PAT_ERROR:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

static void lower_expr(IrLower *lower, IrStmtList *out,
                       AstExpr *expr, int dst)
{
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
    case EXPR_INT: {
        IrExpr *e;
        e = new_expr(lower->program, IR_EXPR_INT, expr->type);
        e->as.int_lit.value = expr->as.int_lit.value;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_FLOAT: {
        IrExpr *e;
        e = new_expr(lower->program, IR_EXPR_FLOAT, expr->type);
        e->as.float_lit.value = expr->as.float_lit.value;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_BOOL: {
        IrExpr *e;
        e = new_expr(lower->program, IR_EXPR_BOOL, expr->type);
        e->as.bool_lit.value = expr->as.bool_lit.value;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_CHAR: {
        IrExpr *e;
        e = new_expr(lower->program, IR_EXPR_CHAR, expr->type);
        e->as.char_lit.codepoint = expr->as.char_lit.codepoint;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_STRING:
    case EXPR_BYTES: {
        IrExpr *e;
        e = new_expr(lower->program,
                     expr->kind == EXPR_STRING ? IR_EXPR_STRING
                                               : IR_EXPR_BYTES,
                     expr->type);
        e->as.str_lit.data = expr->as.str_lit.data;
        e->as.str_lit.len = expr->as.str_lit.len;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_UNIT: {
        IrExpr *e;
        e = new_expr(lower->program, IR_EXPR_UNIT, expr->type);
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_VAR: {
        IrBinding *binding;
        IrExpr *e;
        binding = binding_find(lower, expr->as.var.name,
                               expr->as.var.name_len);
        if (binding != NULL) {
            int slot;
            slot = binding->slot;
            if (binding->owner != lower->function) {
                slot = ensure_capture(lower, lower->function, binding);
            }
            e = new_expr(lower->program, IR_EXPR_LOCAL, expr->type);
            e->as.local.slot = slot;
            stmt_let(lower, out, dst, e);
            return;
        }

        {
            Symbol *symbol;
            symbol = resolver_lookup(lower->resolver, lower->module,
                                     expr->as.var.name,
                                     expr->as.var.name_len);
            if (symbol != NULL && symbol->kind == SYM_FN) {
                IrFunction *fn;
                fn = find_function(lower->program, symbol);
                if (fn == NULL) {
                    ir_error(lower->program, expr->span,
                             "internal: missing function");
                    return;
                }
                e = new_expr(lower->program, IR_EXPR_FUNCTION,
                             expr->type);
                e->as.function.function = fn;
                stmt_let(lower, out, dst, e);
                return;
            }
            if (symbol != NULL && symbol->kind == SYM_VARIANT) {
                int index;
                index = variant_index(symbol->as.variant.owner,
                                      expr->as.var.name,
                                      expr->as.var.name_len);
                if (symbol->as.variant.variant != NULL &&
                    symbol->as.variant.variant->payload != NULL) {
                    ir_error(lower->program, expr->span,
                             "internal: variant requires arguments");
                    return;
                }
                stmt_let(lower, out, dst,
                         enum_constructor(lower->program,
                                          symbol->as.variant.owner,
                                          index, NULL, 0, expr->type));
                return;
            }
        }

        ir_error(lower->program, expr->span,
                 "internal: unresolved value in lowering");
        break;
    }
    case EXPR_TUPLE: {
        size_t count;
        int *items;
        IrExpr *e;
        items = lower_expr_list(lower, out, expr->as.tuple.items, &count);
        e = new_expr(lower->program, IR_EXPR_TUPLE, expr->type);
        e->as.list_lit.items = items;
        e->as.list_lit.count = count;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_ARRAY:
    case EXPR_LIST: {
        size_t count;
        AstExprList *items_ast;
        int *items;
        IrExpr *e;
        items_ast = expr->kind == EXPR_ARRAY
                        ? expr->as.array.items
                        : expr->as.list.items;
        items = lower_expr_list(lower, out, items_ast, &count);
        e = new_expr(lower->program,
                     expr->kind == EXPR_ARRAY ? IR_EXPR_ARRAY
                                              : IR_EXPR_LIST,
                     expr->type);
        e->as.list_lit.items = items;
        e->as.list_lit.count = count;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_MAP:
        lower_map(lower, out, expr, dst);
        break;
    case EXPR_STRUCT:
        lower_struct(lower, out, expr, dst);
        break;
    case EXPR_FIELD: {
        TypeInfo *info;
        int index;
        int object;
        IrExpr *e;
        if (expr->as.field.object->type != NULL &&
            expr->as.field.object->type->kind == TY_NEVER) {
            object = alloc_slot(lower->function);
            lower_expr(lower, out, expr->as.field.object, object);
            stmt_assign_unit(lower, out, dst);
            break;
        }
        if (expr->as.field.object->type == NULL ||
            expr->as.field.object->type->kind != TY_STRUCT) {
            ir_error(lower->program, expr->span,
                     "internal: field access on non-struct");
            return;
        }
        info = expr->as.field.object->type->as.nominal.info;
        index = field_index(info, expr->as.field.field,
                            expr->as.field.field_len);
        object = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.field.object, object);
        e = new_expr(lower->program, IR_EXPR_FIELD, expr->type);
        e->as.field.object = object;
        e->as.field.index = index;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_INDEX: {
        int object;
        int index;
        IrExpr *e;
        object = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.index.object, object);
        index = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.index.index, index);
        if (expr->as.index.object->type != NULL &&
            expr->as.index.object->type->kind == TY_NEVER) {
            stmt_assign_unit(lower, out, dst);
            break;
        }
        e = new_expr(lower->program, IR_EXPR_INDEX, expr->type);
        e->as.index.object = object;
        e->as.index.index = index;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_CALL:
        lower_call(lower, out, expr, dst);
        break;
    case EXPR_LAMBDA:
        lower_lambda(lower, out, expr, dst);
        break;
    case EXPR_IF: {
        int cond;
        IrStmt *stmt;
        IrStmtList then_list;
        IrStmtList else_list;
        cond = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.if_.cond, cond);
        memset(&then_list, 0, sizeof(then_list));
        memset(&else_list, 0, sizeof(else_list));
        lower_expr(lower, &then_list, expr->as.if_.then_branch, dst);
        lower_expr(lower, &else_list, expr->as.if_.else_branch, dst);
        stmt = new_stmt(lower->program, IR_STMT_IF);
        stmt->as.if_.cond = cond;
        stmt->as.if_.then_branch = then_list.head;
        stmt->as.if_.else_branch = else_list.head;
        stmt_add(out, stmt);
        break;
    }
    case EXPR_MATCH: {
        int subject;
        AstMatchArm *arm;
        IrMatchArm *arm_head;
        IrMatchArm *arm_tail;
        IrStmt *stmt;
        IrBinding *saved;
        subject = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.match.scrutinee, subject);
        arm_head = NULL;
        arm_tail = NULL;
        saved = lower->bindings;
        for (arm = expr->as.match.arms; arm != NULL; arm = arm->next) {
            IrMatchArm *ir_arm;
            IrStmtList bindings;
            IrStmtList body;
            memset(&bindings, 0, sizeof(bindings));
            memset(&body, 0, sizeof(body));
            lower->bindings = saved;
            lower_pattern_bindings(lower, &bindings, arm->pattern,
                                   subject, 1);
            lower_expr(lower, &body, arm->body, dst);
            ir_arm = (IrMatchArm *)ir_alloc(lower->program,
                                            sizeof(IrMatchArm));
            ir_arm->pattern = arm->pattern;
            ir_arm->bindings = bindings.head;
            ir_arm->body = body.head;
            if (arm_tail != NULL) {
                arm_tail->next = ir_arm;
            } else {
                arm_head = ir_arm;
            }
            arm_tail = ir_arm;
        }
        lower->bindings = saved;
        stmt = new_stmt(lower->program, IR_STMT_MATCH);
        stmt->as.match.subject = subject;
        stmt->as.match.arms = arm_head;
        stmt_add(out, stmt);
        break;
    }
    case EXPR_BLOCK: {
        IrBinding *saved;
        AstBlockItem *item;
        IrStmtList items;
        IrStmt *stmt;
        saved = lower->bindings;
        memset(&items, 0, sizeof(items));
        for (item = expr->as.block.items; item != NULL;
             item = item->next) {
            int last;
            last = item->next == NULL;
            if (item->kind == BLOCK_LET) {
                int value_slot;
                value_slot = alloc_slot(lower->function);
                lower_expr(lower, &items, item->as.let.value, value_slot);
                lower_pattern_bindings(lower, &items,
                                       item->as.let.pattern,
                                       value_slot, 1);
                if (last) {
                    stmt_assign_unit(lower, &items, dst);
                }
            } else {
                if (last) {
                    lower_expr(lower, &items, item->as.expr, dst);
                } else {
                    int tmp;
                    tmp = alloc_slot(lower->function);
                    lower_expr(lower, &items, item->as.expr, tmp);
                }
            }
        }
        if (expr->as.block.items == NULL) {
            stmt_assign_unit(lower, &items, dst);
        }
        lower->bindings = saved;
        stmt = new_stmt(lower->program, IR_STMT_BLOCK);
        stmt->as.block.items = items.head;
        stmt_add(out, stmt);
        break;
    }
    case EXPR_BINARY: {
        AstBinaryOp op;
        int left;
        int right;
        IrExpr *e;
        if ((expr->as.binary.left->type != NULL &&
             expr->as.binary.left->type->kind == TY_NEVER) ||
            (expr->as.binary.right->type != NULL &&
             expr->as.binary.right->type->kind == TY_NEVER)) {
            left = alloc_slot(lower->function);
            lower_expr(lower, out, expr->as.binary.left, left);
            right = alloc_slot(lower->function);
            lower_expr(lower, out, expr->as.binary.right, right);
            stmt_assign_unit(lower, out, dst);
            break;
        }
        op = expr->as.binary.op;
        if (op == BIN_AND || op == BIN_OR) {
            IrStmt *stmt;
            IrStmtList then_list;
            IrStmtList else_list;
            left = alloc_slot(lower->function);
            lower_expr(lower, out, expr->as.binary.left, left);
            memset(&then_list, 0, sizeof(then_list));
            memset(&else_list, 0, sizeof(else_list));
            if (op == BIN_AND) {
                lower_expr(lower, &then_list, expr->as.binary.right, dst);
                stmt_assign_bool(lower, &else_list, dst, 0);
            } else {
                stmt_assign_bool(lower, &then_list, dst, 1);
                lower_expr(lower, &else_list, expr->as.binary.right, dst);
            }
            stmt = new_stmt(lower->program, IR_STMT_IF);
            stmt->as.if_.cond = left;
            stmt->as.if_.then_branch = then_list.head;
            stmt->as.if_.else_branch = else_list.head;
            stmt_add(out, stmt);
            break;
        }
        left = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.binary.left, left);
        right = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.binary.right, right);
        e = new_expr(lower->program, IR_EXPR_BINARY, expr->type);
        e->as.binary.op = op;
        e->as.binary.left = left;
        e->as.binary.right = right;
        e->as.binary.operand_type = expr->as.binary.left->type;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_UNARY: {
        int operand;
        IrExpr *e;
        operand = alloc_slot(lower->function);
        lower_expr(lower, out, expr->as.unary.operand, operand);
        if (expr->as.unary.operand->type != NULL &&
            expr->as.unary.operand->type->kind == TY_NEVER) {
            stmt_assign_unit(lower, out, dst);
            break;
        }
        e = new_expr(lower->program, IR_EXPR_UNARY, expr->type);
        e->as.unary.op = expr->as.unary.op;
        e->as.unary.operand = operand;
        e->as.unary.operand_type = expr->as.unary.operand->type;
        stmt_let(lower, out, dst, e);
        break;
    }
    case EXPR_ERROR:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Functions and program                                               */
/* ------------------------------------------------------------------ */

static void lower_named_function(IrLower *lower, IrFunction *function)
{
    Symbol *symbol;
    AstDecl *decl;
    AstParam *param;
    TypeList *param_type;
    IrBinding *saved_bindings;
    IrFunction *saved_function;
    Module *saved_module;
    IrStmtList body;
    int index;

    symbol = function->symbol;
    if (symbol == NULL) {
        return;
    }

    decl = symbol->as.fn.decl;
    param = decl->as.fn.params;
    param_type = symbol->as.fn.type->as.fn.params;

    saved_bindings = lower->bindings;
    saved_function = lower->function;
    saved_module = lower->module;

    lower->bindings = NULL;
    lower->function = function;
    lower->module = symbol->module;

    index = 0;
    while (param != NULL && param_type != NULL) {
        int slot;
        slot = alloc_slot(function);
        binding_add(lower, param->name, param->name_len, slot);
        index += 1;
        param = param->next;
        param_type = param_type->next;
    }
    function->param_count = index;

    function->result_type = symbol->as.fn.type->as.fn.result;
    function->result_slot = alloc_slot(function);

    memset(&body, 0, sizeof(body));
    lower_expr(lower, &body, decl->as.fn.body, function->result_slot);
    function->body = body.head;

    lower->bindings = saved_bindings;
    lower->function = saved_function;
    lower->module = saved_module;
}

void ir_init(IrProgram *program, Resolver *resolver, Arena *arena)
{
    memset(program, 0, sizeof(IrProgram));
    program->resolver = resolver;
    program->arena = arena;
    program->next_function_id = 1;
}

int ir_lower(IrProgram *program)
{
    Resolver *resolver;
    Module *module;
    Symbol *symbol;
    IrLower lower;

    resolver = program->resolver;
    memset(&lower, 0, sizeof(lower));
    lower.program = program;
    lower.resolver = resolver;

    for (module = resolver->modules; module != NULL; module = module->next) {
        if (module->failed) {
            continue;
        }
        for (symbol = module->symbols; symbol != NULL;
             symbol = symbol->next) {
            if (symbol->kind == SYM_FN) {
                new_function(program, symbol->name, symbol->name_len,
                             symbol->span, symbol, 0, NULL);
            }
        }
    }

    module = resolver_root(resolver);
    if (module != NULL) {
        for (symbol = module->symbols; symbol != NULL;
             symbol = symbol->next) {
            if (symbol->kind == SYM_FN && symbol->name_len == 4 &&
                memcmp(symbol->name, "main", 4) == 0) {
                program->main_function = find_function(program, symbol);
                break;
            }
        }
    }

    for (module = resolver->modules; module != NULL; module = module->next) {
        if (module->failed) {
            continue;
        }
        for (symbol = module->symbols; symbol != NULL;
             symbol = symbol->next) {
            if (symbol->kind == SYM_FN) {
                IrFunction *function;
                function = find_function(program, symbol);
                if (function != NULL) {
                    lower_named_function(&lower, function);
                }
                if (program->had_error) {
                    return 0;
                }
            }
        }
    }

    if (program->main_function == NULL) {
        Span span;
        memset(&span, 0, sizeof(span));
        ir_error(program, span, "internal: missing main function");
        return 0;
    }

    return 1;
}

int ir_had_error(const IrProgram *program)
{
    return program->had_error;
}

Span ir_error_span(const IrProgram *program)
{
    return program->error_span;
}

const char *ir_error_message(const IrProgram *program)
{
    return program->error_message;
}
