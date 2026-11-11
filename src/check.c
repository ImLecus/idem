#include "check.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Allocation and diagnostics                                         */
/* ------------------------------------------------------------------ */

struct Binding {
    const char *name;
    size_t name_len;
    Type *type;
    Binding *next;
};

static void *checker_alloc(TypeChecker *c, size_t size)
{
    void *p;
    p = arena_alloc(c->arena, size);
    if (p == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    memset(p, 0, size);
    return p;
}

static void checker_error(TypeChecker *c, Span span, const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    size_t out;
    const char *p;

    if (c->had_error) {
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

    c->had_error = 1;
    c->error_span = span;
    strcpy(c->error_message, buf);
}

static int is_subtype(Type *a, Type *b);
static int type_equivalent(Type *a, Type *b);
static int name_eq(const char *s, size_t n, const char *lit);
static int is_builtin_function(const char *name, size_t len);
static void check_match_exhaustive(TypeChecker *c, AstExpr *expr,
                                   Type *scrutinee);
static void check_match_reachable(TypeChecker *c, AstExpr *expr,
                                  Type *scrutinee);

static void format_buffer(Type *type, char *buf, size_t cap)
{
    type_format(type, buf, cap);
}

static Type *checker_resolve_type(TypeChecker *c, AstType *type,
                                  Span span)
{
    Type *result;
    result = resolver_resolve_type(c->resolver, c->module, type);
    if (result->kind == TY_ERROR &&
        resolver_had_error(c->resolver) &&
        !c->had_error) {
        Span error_span;
        error_span = resolver_error_span(c->resolver);
        checker_error(c, error_span, "%s",
                      resolver_error_message(c->resolver));
    }
    (void)span;
    return result;
}

static void check_expected(TypeChecker *c, Span span,
                           Type *actual, Type *expected)
{
    char a[256];
    char e[256];

    if (expected == NULL || actual == NULL ||
        expected->kind == TY_ERROR || actual->kind == TY_ERROR) {
        return;
    }
    if (is_subtype(actual, expected)) {
        return;
    }

    format_buffer(actual, a, sizeof(a));
    format_buffer(expected, e, sizeof(e));
    checker_error(c, span, "expected %s, found %s", e, a);
}

/* ------------------------------------------------------------------ */
/* Type relations                                                      */
/* ------------------------------------------------------------------ */

static int type_equivalent(Type *a, Type *b);

static int is_subtype(Type *a, Type *b)
{
    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->kind == TY_ERROR || b->kind == TY_ERROR) {
        return 1;
    }
    if (a->kind == TY_NEVER) {
        return 1;
    }
    if (b->kind == TY_ANY) {
        return 1;
    }

    if (a->kind == TY_UNION) {
        TypeList *it;
        for (it = a->as.union_.items; it != NULL; it = it->next) {
            if (!is_subtype(it->type, b)) {
                return 0;
            }
        }
        return 1;
    }

    if (b->kind == TY_UNION) {
        TypeList *it;
        for (it = b->as.union_.items; it != NULL; it = it->next) {
            if (is_subtype(a, it->type)) {
                return 1;
            }
        }
        return 0;
    }

    if (a->kind != b->kind) {
        return 0;
    }

    switch (a->kind) {
    case TY_ARRAY:
        return type_equivalent(a->as.array.element, b->as.array.element);
    case TY_LIST:
        return type_equivalent(a->as.list.element, b->as.list.element);
    case TY_MAP:
        return type_equivalent(a->as.map.key, b->as.map.key) &&
               type_equivalent(a->as.map.value, b->as.map.value);
    case TY_TUPLE: {
        TypeList *x;
        TypeList *y;
        x = a->as.tuple.items;
        y = b->as.tuple.items;
        while (x != NULL && y != NULL) {
            if (!type_equivalent(x->type, y->type)) {
                return 0;
            }
            x = x->next;
            y = y->next;
        }
        return x == NULL && y == NULL;
    }
    case TY_FUNCTION: {
        TypeList *x;
        TypeList *y;
        x = a->as.fn.params;
        y = b->as.fn.params;
        while (x != NULL && y != NULL) {
            if (!is_subtype(y->type, x->type)) {
                return 0;
            }
            x = x->next;
            y = y->next;
        }
        if (x != NULL || y != NULL) {
            return 0;
        }
        return is_subtype(a->as.fn.result, b->as.fn.result);
    }
    case TY_STRUCT:
    case TY_ENUM:
        return a->as.nominal.info == b->as.nominal.info;
    default:
        return a->kind == b->kind;
    }
}

static int type_equivalent(Type *a, Type *b)
{
    return is_subtype(a, b) && is_subtype(b, a);
}

static void union_add(TypeChecker *c, Type *type,
                      TypeList **head, TypeList **tail, int *has_any)
{
    TypeList *it;

    if (type == NULL || type->kind == TY_ERROR) {
        return;
    }
    if (type->kind == TY_NEVER) {
        return;
    }
    if (type->kind == TY_ANY) {
        *has_any = 1;
        return;
    }
    if (type->kind == TY_UNION) {
        for (it = type->as.union_.items; it != NULL; it = it->next) {
            union_add(c, it->type, head, tail, has_any);
        }
        return;
    }

    for (it = *head; it != NULL; it = it->next) {
        if (type_equivalent(it->type, type)) {
            return;
        }
    }

    it = type_list_cons(c->arena, type, NULL);
    if (it == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    if (*tail != NULL) {
        (*tail)->next = it;
    } else {
        *head = it;
    }
    *tail = it;
}

static Type *join_types(TypeChecker *c, Type *a, Type *b)
{
    TypeList *head;
    TypeList *tail;
    int has_any;

    if (a == NULL || b == NULL) {
        return c->resolver->ty_error;
    }
    if (a->kind == TY_ERROR || b->kind == TY_ERROR) {
        return c->resolver->ty_error;
    }
    if (a->kind == TY_NEVER) {
        return b;
    }
    if (b->kind == TY_NEVER) {
        return a;
    }
    if (a->kind == TY_ANY || b->kind == TY_ANY) {
        return c->resolver->ty_any;
    }
    if (is_subtype(a, b)) {
        return b;
    }
    if (is_subtype(b, a)) {
        return a;
    }

    head = NULL;
    tail = NULL;
    has_any = 0;
    union_add(c, a, &head, &tail, &has_any);
    union_add(c, b, &head, &tail, &has_any);

    if (has_any) {
        return c->resolver->ty_any;
    }
    if (head == NULL) {
        return c->resolver->ty_never;
    }
    if (head->next == NULL) {
        return head->type;
    }
    return type_union(c->arena, head);
}

/* ------------------------------------------------------------------ */
/* Comparability, ordering, and hashability                            */
/* ------------------------------------------------------------------ */

typedef struct SeenInfo {
    TypeInfo *info;
    struct SeenInfo *next;
} SeenInfo;

static int seen_has(SeenInfo *seen, TypeInfo *info)
{
    while (seen != NULL) {
        if (seen->info == info) {
            return 1;
        }
        seen = seen->next;
    }
    return 0;
}

static int comparable_rec(TypeChecker *c, Type *type, SeenInfo *seen)
{
    TypeList *it;
    TypeField *field;
    TypeVariant *variant;

    if (type == NULL) {
        return 0;
    }

    switch (type->kind) {
    case TY_BOOL:
    case TY_INT:
    case TY_FLOAT:
    case TY_CHAR:
    case TY_STRING:
    case TY_BYTES:
    case TY_UNIT:
    case TY_NEVER:
    case TY_ERROR:
        return 1;
    case TY_ANY:
    case TY_FUNCTION:
        return 0;
    case TY_ARRAY:
        return comparable_rec(c, type->as.array.element, seen);
    case TY_LIST:
        return comparable_rec(c, type->as.list.element, seen);
    case TY_MAP:
        return comparable_rec(c, type->as.map.key, seen) &&
               comparable_rec(c, type->as.map.value, seen);
    case TY_TUPLE:
        for (it = type->as.tuple.items; it != NULL; it = it->next) {
            if (!comparable_rec(c, it->type, seen)) {
                return 0;
            }
        }
        return 1;
    case TY_UNION:
        for (it = type->as.union_.items; it != NULL; it = it->next) {
            if (!comparable_rec(c, it->type, seen)) {
                return 0;
            }
        }
        return 1;
    case TY_STRUCT:
    case TY_ENUM: {
        TypeInfo *info;
        SeenInfo *node;
        info = type->as.nominal.info;
        if (seen_has(seen, info)) {
            return 1;
        }
        node = (SeenInfo *)checker_alloc(c, sizeof(SeenInfo));
        node->info = info;
        node->next = seen;

        if (type->kind == TY_STRUCT) {
            for (field = info->fields; field != NULL; field = field->next) {
                if (!comparable_rec(c, field->type, node)) {
                    return 0;
                }
            }
        } else {
            for (variant = info->variants; variant != NULL;
                 variant = variant->next) {
                for (it = variant->payload; it != NULL; it = it->next) {
                    if (!comparable_rec(c, it->type, node)) {
                        return 0;
                    }
                }
            }
        }
        return 1;
    }
    }
    return 0;
}

static int is_comparable(TypeChecker *c, Type *type)
{
    return comparable_rec(c, type, NULL);
}

static int orderable_rec(Type *type)
{
    TypeList *it;

    if (type == NULL) {
        return 0;
    }
    switch (type->kind) {
    case TY_INT:
    case TY_FLOAT:
    case TY_CHAR:
    case TY_STRING:
    case TY_BYTES:
    case TY_NEVER:
        return 1;
    case TY_TUPLE:
        for (it = type->as.tuple.items; it != NULL; it = it->next) {
            if (!orderable_rec(it->type)) {
                return 0;
            }
        }
        return 1;
    default:
        return 0;
    }
}

static int hashable_rec(TypeChecker *c, Type *type, SeenInfo *seen)
{
    TypeList *it;
    TypeField *field;
    TypeVariant *variant;

    if (type == NULL) {
        return 0;
    }

    switch (type->kind) {
    case TY_BOOL:
    case TY_INT:
    case TY_CHAR:
    case TY_STRING:
    case TY_BYTES:
    case TY_ERROR:
        return 1;
    case TY_TUPLE:
        for (it = type->as.tuple.items; it != NULL; it = it->next) {
            if (!hashable_rec(c, it->type, seen)) {
                return 0;
            }
        }
        return 1;
    case TY_STRUCT:
    case TY_ENUM: {
        TypeInfo *info;
        SeenInfo *node;
        info = type->as.nominal.info;
        if (seen_has(seen, info)) {
            return 1;
        }
        node = (SeenInfo *)checker_alloc(c, sizeof(SeenInfo));
        node->info = info;
        node->next = seen;

        if (type->kind == TY_STRUCT) {
            for (field = info->fields; field != NULL; field = field->next) {
                if (!hashable_rec(c, field->type, node)) {
                    return 0;
                }
            }
        } else {
            for (variant = info->variants; variant != NULL;
                 variant = variant->next) {
                for (it = variant->payload; it != NULL; it = it->next) {
                    if (!hashable_rec(c, it->type, node)) {
                        return 0;
                    }
                }
            }
        }
        return 1;
    }
    default:
        return 0;
    }
}

static int is_hashable(TypeChecker *c, Type *type)
{
    return hashable_rec(c, type, NULL);
}

static void validate_type(TypeChecker *c, Type *type, Span span,
                          SeenInfo *seen)
{
    TypeList *it;
    TypeField *field;
    TypeVariant *variant;

    if (type == NULL || type->kind == TY_ERROR) {
        return;
    }

    switch (type->kind) {
    case TY_MAP:
        if (!is_hashable(c, type->as.map.key)) {
            char buf[256];
            format_buffer(type->as.map.key, buf, sizeof(buf));
            checker_error(c, span, "Map key type '%s' is not hashable", buf);
        }
        validate_type(c, type->as.map.key, span, seen);
        validate_type(c, type->as.map.value, span, seen);
        break;
    case TY_ARRAY:
        validate_type(c, type->as.array.element, span, seen);
        break;
    case TY_LIST:
        validate_type(c, type->as.list.element, span, seen);
        break;
    case TY_TUPLE:
        for (it = type->as.tuple.items; it != NULL; it = it->next) {
            validate_type(c, it->type, span, seen);
        }
        break;
    case TY_FUNCTION:
        for (it = type->as.fn.params; it != NULL; it = it->next) {
            validate_type(c, it->type, span, seen);
        }
        validate_type(c, type->as.fn.result, span, seen);
        break;
    case TY_UNION:
        for (it = type->as.union_.items; it != NULL; it = it->next) {
            validate_type(c, it->type, span, seen);
        }
        break;
    case TY_STRUCT:
    case TY_ENUM: {
        TypeInfo *info;
        SeenInfo *node;
        info = type->as.nominal.info;
        if (seen_has(seen, info)) {
            return;
        }
        node = (SeenInfo *)checker_alloc(c, sizeof(SeenInfo));
        node->info = info;
        node->next = seen;

        if (type->kind == TY_STRUCT) {
            for (field = info->fields; field != NULL; field = field->next) {
                validate_type(c, field->type, span, node);
            }
        } else {
            for (variant = info->variants; variant != NULL;
                 variant = variant->next) {
                for (it = variant->payload; it != NULL; it = it->next) {
                    validate_type(c, it->type, span, node);
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Local bindings                                                      */
/* ------------------------------------------------------------------ */

static Binding *binding_lookup(TypeChecker *c, const char *name, size_t len)
{
    Binding *b;
    for (b = c->locals; b != NULL; b = b->next) {
        if (b->name_len == len && memcmp(b->name, name, len) == 0) {
            return b;
        }
    }
    return NULL;
}

static int binding_name_ok(TypeChecker *c, const char *name, size_t len,
                           Span span, Binding *new_head)
{
    Symbol *sym;
    Binding *b;

    if (resolver_is_reserved_name(name, len)) {
        checker_error(c, span, "'%.*s' is a reserved name",
                      (int)len, name);
        return 0;
    }

    sym = resolver_lookup(c->resolver, c->module, name, len);
    if (sym != NULL &&
        (sym->kind == SYM_ALIAS ||
         sym->kind == SYM_STRUCT ||
         sym->kind == SYM_ENUM)) {
        checker_error(c, span, "'%.*s' shadows a type name",
                      (int)len, name);
        return 0;
    }

    for (b = new_head; b != NULL; b = b->next) {
        if (b->name_len == len && memcmp(b->name, name, len) == 0) {
            checker_error(c, span, "duplicate binding '%.*s'",
                          (int)len, name);
            return 0;
        }
    }

    return 1;
}

static void binding_add(TypeChecker *c, Binding **head, Binding **tail,
                        const char *name, size_t len, Type *type, Span span)
{
    Binding *b;

    if (!binding_name_ok(c, name, len, span, *head)) {
        return;
    }

    b = (Binding *)checker_alloc(c, sizeof(Binding));
    b->name = name;
    b->name_len = len;
    b->type = type;

    if (*tail != NULL) {
        (*tail)->next = b;
    } else {
        *head = b;
    }
    *tail = b;
}

static void bindings_push(TypeChecker *c, Binding *head, Binding *tail)
{
    if (head == NULL) {
        return;
    }
    tail->next = c->locals;
    c->locals = head;
}

/* ------------------------------------------------------------------ */
/* Expression checking                                                 */
/* ------------------------------------------------------------------ */

static Type *check_expr(TypeChecker *c, AstExpr *expr, Type *expected);
static Type *check_block(TypeChecker *c, AstExpr *expr, Type *expected);
static Type *check_match(TypeChecker *c, AstExpr *expr, Type *expected);
static Type *check_call(TypeChecker *c, AstExpr *expr, Type *expected);
static Type *check_lambda(TypeChecker *c, AstExpr *expr, Type *expected);
static Type *check_builtin(TypeChecker *c, AstExpr *expr,
                           const char *name,
                           AstExprList *args, Span span);
static void check_pattern(TypeChecker *c, AstPattern *pat, Type *scrutinee,
                          int allow_refutable, Binding **head,
                          Binding **tail);

static Type *type_of_literal(TypeChecker *c, AstExpr *expr)
{
    switch (expr->kind) {
    case EXPR_INT:    return c->resolver->ty_int;
    case EXPR_FLOAT:  return c->resolver->ty_float;
    case EXPR_BOOL:   return c->resolver->ty_bool;
    case EXPR_CHAR:   return c->resolver->ty_char;
    case EXPR_STRING: return c->resolver->ty_string;
    case EXPR_BYTES:  return c->resolver->ty_bytes;
    default:          return c->resolver->ty_error;
    }
}

static TypeList *check_type_list_from_exprs(TypeChecker *c,
                                            AstExprList *items,
                                            TypeList *expected_items)
{
    TypeList *head;
    TypeList *tail;

    head = NULL;
    tail = NULL;

    while (items != NULL) {
        Type *expected;
        Type *actual;
        TypeList *n;

        expected = expected_items != NULL ? expected_items->type : NULL;
        actual = check_expr(c, items->expr, expected);
        n = type_list_cons(c->arena, actual, NULL);
        if (n == NULL) {
            fprintf(stderr, "idem: out of memory\n");
            exit(1);
        }
        if (tail != NULL) {
            tail->next = n;
        } else {
            head = n;
        }
        tail = n;

        items = items->next;
        if (expected_items != NULL) {
            expected_items = expected_items->next;
        }
    }

    return head;
}

static Type *join_expr_item_types(TypeChecker *c, AstExprList *items)
{
    Type *result;
    result = c->resolver->ty_never;
    while (items != NULL) {
        Type *t;
        t = check_expr(c, items->expr, NULL);
        result = join_types(c, result, t);
        items = items->next;
    }
    return result;
}

static Type *check_tuple(TypeChecker *c, AstExpr *expr, Type *expected)
{
    TypeList *expected_items;
    TypeList *items;
    int expected_arity;
    int actual_arity;
    AstExprList *it;

    expected_items = NULL;
    expected_arity = -1;
    if (expected != NULL && expected->kind == TY_TUPLE) {
        expected_items = expected->as.tuple.items;
        expected_arity = 0;
        {
            TypeList *e;
            for (e = expected_items; e != NULL; e = e->next) {
                expected_arity += 1;
            }
        }
    }

    actual_arity = 0;
    for (it = expr->as.tuple.items; it != NULL; it = it->next) {
        actual_arity += 1;
    }

    if (expected_arity >= 0 && expected_arity != actual_arity) {
        checker_error(c, expr->span, "tuple arity mismatch");
        expected_items = NULL;
    }

    items = check_type_list_from_exprs(c, expr->as.tuple.items,
                                       expected_items);
    if (expected_arity == actual_arity && expected_items != NULL) {
        return expected;
    }
    return type_tuple(c->arena, items);
}

static Type *check_sequence(TypeChecker *c, AstExpr *expr, Type *expected,
                            int is_list)
{
    Type *element_expected;
    Type *element_type;
    AstExprList *items;
    AstExprList *it;

    items = is_list ? expr->as.list.items : expr->as.array.items;
    element_expected = NULL;
    if (expected != NULL) {
        if (is_list && expected->kind == TY_LIST) {
            element_expected = expected->as.list.element;
        } else if (!is_list && expected->kind == TY_ARRAY) {
            element_expected = expected->as.array.element;
        }
    }

    if (items == NULL) {
        if (expected != NULL &&
            ((is_list && expected->kind == TY_LIST) ||
             (!is_list && expected->kind == TY_ARRAY))) {
            return expected;
        }
        checker_error(c, expr->span,
                      is_list ? "empty List requires an expected type"
                              : "empty array requires an expected type");
        return c->resolver->ty_error;
    }

    if (element_expected != NULL) {
        for (it = items; it != NULL; it = it->next) {
            check_expr(c, it->expr, element_expected);
        }
        return expected;
    }

    element_type = join_expr_item_types(c, items);
    if (is_list) {
        return type_list_of(c->arena, element_type);
    }
    return type_array(c->arena, element_type);
}

static Type *check_map(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Type *key_expected;
    Type *value_expected;
    AstMapEntry *entry;
    Type *key_type;
    Type *value_type;
    int expected_map;

    expected_map = expected != NULL && expected->kind == TY_MAP;
    key_expected = expected_map ? expected->as.map.key : NULL;
    value_expected = expected_map ? expected->as.map.value : NULL;

    if (expr->as.map.entries == NULL) {
        if (expected_map) {
            return expected;
        }
        checker_error(c, expr->span,
                      "empty Map requires an expected type");
        return c->resolver->ty_error;
    }

    key_type = c->resolver->ty_never;
    value_type = c->resolver->ty_never;

    for (entry = expr->as.map.entries; entry != NULL; entry = entry->next) {
        Type *kt;
        Type *vt;

        kt = check_expr(c, entry->key, key_expected);
        vt = check_expr(c, entry->value, value_expected);
        key_type = join_types(c, key_type, kt);
        value_type = join_types(c, value_type, vt);
    }

    if (!is_hashable(c, key_type)) {
        char buf[256];
        format_buffer(key_type, buf, sizeof(buf));
        checker_error(c, expr->span,
                      "Map key type '%s' is not hashable", buf);
    }

    if (expected_map) {
        return expected;
    }
    return type_map(c->arena, key_type, value_type);
}

static TypeInfo *lookup_struct_info(TypeChecker *c, const char *name,
                                    size_t len, Span span)
{
    Symbol *sym;

    sym = resolver_lookup(c->resolver, c->module, name, len);
    if (sym == NULL) {
        checker_error(c, span, "unknown struct '%.*s'", (int)len, name);
        return NULL;
    }
    if (sym->kind == SYM_STRUCT) {
        return sym->as.nominal.info;
    }
    if (sym->kind == SYM_ALIAS && sym->as.alias.target != NULL &&
        sym->as.alias.target->kind == TY_STRUCT) {
        return sym->as.alias.target->as.nominal.info;
    }
    checker_error(c, span, "'%.*s' is not a struct", (int)len, name);
    return NULL;
}

static Type *check_struct_literal(TypeChecker *c, AstExpr *expr,
                                  Type *expected)
{
    TypeInfo *info;
    AstFieldInit *init;
    TypeField *field;
    Type *result;

    info = lookup_struct_info(c, expr->as.struct_.name,
                              expr->as.struct_.name_len, expr->span);
    if (info == NULL) {
        return c->resolver->ty_error;
    }

    for (init = expr->as.struct_.fields; init != NULL; init = init->next) {
        AstFieldInit *prev;
        int found;

        found = 0;
        for (field = info->fields; field != NULL; field = field->next) {
            if (field->name_len == init->name_len &&
                memcmp(field->name, init->name, init->name_len) == 0) {
                check_expr(c, init->value, field->type);
                found = 1;
                break;
            }
        }
        if (!found) {
            checker_error(c, init->span, "unknown field '%.*s'",
                          (int)init->name_len, init->name);
        }

        for (prev = expr->as.struct_.fields; prev != init; prev = prev->next) {
            if (prev->name_len == init->name_len &&
                memcmp(prev->name, init->name, init->name_len) == 0) {
                checker_error(c, init->span, "duplicate field '%.*s'",
                              (int)init->name_len, init->name);
                break;
            }
        }
    }

    for (field = info->fields; field != NULL; field = field->next) {
        int found;
        found = 0;
        for (init = expr->as.struct_.fields; init != NULL; init = init->next) {
            if (field->name_len == init->name_len &&
                memcmp(field->name, init->name, field->name_len) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            checker_error(c, expr->span, "missing field '%s'", field->name);
        }
    }

    result = info->type;
    check_expected(c, expr->span, result, expected);
    return result;
}

static Type *check_field_access(TypeChecker *c, AstExpr *expr,
                                Type *expected)
{
    Type *object_type;
    TypeField *field;

    object_type = check_expr(c, expr->as.field.object, NULL);
    if (object_type->kind == TY_ERROR) {
        return c->resolver->ty_error;
    }
    if (object_type->kind == TY_NEVER) {
        check_expected(c, expr->span, c->resolver->ty_never, expected);
        return c->resolver->ty_never;
    }
    if (object_type->kind != TY_STRUCT) {
        checker_error(c, expr->span, "field access requires a struct");
        return c->resolver->ty_error;
    }
    for (field = object_type->as.nominal.info->fields;
         field != NULL; field = field->next) {
        if (field->name_len == expr->as.field.field_len &&
            memcmp(field->name, expr->as.field.field,
                   field->name_len) == 0) {
            check_expected(c, expr->span, field->type, expected);
            return field->type;
        }
    }
    checker_error(c, expr->span, "unknown field '%.*s'",
                  (int)expr->as.field.field_len, expr->as.field.field);
    return c->resolver->ty_error;
}

static Type *check_index(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Type *object_type;
    Type *result;

    object_type = check_expr(c, expr->as.index.object, NULL);
    if (object_type->kind == TY_NEVER) {
        check_expected(c, expr->span, c->resolver->ty_never, expected);
        return c->resolver->ty_never;
    }
    check_expr(c, expr->as.index.index, c->resolver->ty_int);

    if (object_type->kind == TY_ARRAY) {
        result = join_types(c, object_type->as.array.element,
                            c->resolver->ty_unit);
    } else if (object_type->kind == TY_LIST) {
        result = join_types(c, object_type->as.list.element,
                            c->resolver->ty_unit);
    } else if (object_type->kind == TY_ERROR) {
        return c->resolver->ty_error;
    } else {
        checker_error(c, expr->span,
                      "indexing requires an Array or List");
        return c->resolver->ty_error;
    }

    check_expected(c, expr->span, result, expected);
    return result;
}

static Type *check_binary(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Type *left;
    Type *right;
    Type *result;
    AstBinaryOp op;

    op = expr->as.binary.op;
    left = check_expr(c, expr->as.binary.left, NULL);
    right = check_expr(c, expr->as.binary.right, NULL);

    if (left->kind == TY_ERROR || right->kind == TY_ERROR) {
        return c->resolver->ty_error;
    }
    if (left->kind == TY_NEVER || right->kind == TY_NEVER) {
        check_expected(c, expr->span, c->resolver->ty_never, expected);
        return c->resolver->ty_never;
    }

    if (op == BIN_AND || op == BIN_OR) {
        check_expected(c, expr->as.binary.left->span,
                       left, c->resolver->ty_bool);
        check_expected(c, expr->as.binary.right->span,
                       right, c->resolver->ty_bool);
        result = c->resolver->ty_bool;
    } else if (op == BIN_EQ || op == BIN_NE) {
        if (!type_equivalent(left, right)) {
            char a[256];
            char b[256];
            format_buffer(left, a, sizeof(a));
            format_buffer(right, b, sizeof(b));
            checker_error(c, expr->span,
                          "equality requires the same type, found %s and %s",
                          a, b);
            return c->resolver->ty_error;
        }
        if (!is_comparable(c, left)) {
            checker_error(c, expr->span,
                          "values of this type are not comparable");
            return c->resolver->ty_error;
        }
        result = c->resolver->ty_bool;
    } else if (op == BIN_LT || op == BIN_LE ||
               op == BIN_GT || op == BIN_GE) {
        if (!type_equivalent(left, right)) {
            char a[256];
            char b[256];
            format_buffer(left, a, sizeof(a));
            format_buffer(right, b, sizeof(b));
            checker_error(c, expr->span,
                          "ordering requires the same type, found %s and %s",
                          a, b);
            return c->resolver->ty_error;
        }
        if (!orderable_rec(left)) {
            checker_error(c, expr->span,
                          "values of this type are not orderable");
            return c->resolver->ty_error;
        }
        result = c->resolver->ty_bool;
    } else if (op == BIN_BIT_AND || op == BIN_BIT_OR ||
               op == BIN_BIT_XOR || op == BIN_SHL || op == BIN_SHR) {
        check_expected(c, expr->as.binary.left->span,
                       left, c->resolver->ty_int);
        check_expected(c, expr->as.binary.right->span,
                       right, c->resolver->ty_int);
        result = c->resolver->ty_int;
    } else {
        int ok_left;
        ok_left = left->kind == TY_INT || left->kind == TY_FLOAT;
        if (!ok_left || !type_equivalent(left, right)) {
            char a[256];
            char b[256];
            format_buffer(left, a, sizeof(a));
            format_buffer(right, b, sizeof(b));
            checker_error(c, expr->span,
                          "arithmetic requires matching Int or Float, found %s and %s",
                          a, b);
            return c->resolver->ty_error;
        }
        result = left;
    }

    check_expected(c, expr->span, result, expected);
    return result;
}

static Type *check_unary(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Type *operand;
    Type *result;

    operand = check_expr(c, expr->as.unary.operand, NULL);
    if (operand->kind == TY_ERROR) {
        return c->resolver->ty_error;
    }
    if (operand->kind == TY_NEVER) {
        check_expected(c, expr->span, c->resolver->ty_never, expected);
        return c->resolver->ty_never;
    }

    switch (expr->as.unary.op) {
    case UN_NOT:
        check_expected(c, expr->span, operand, c->resolver->ty_bool);
        result = c->resolver->ty_bool;
        break;
    case UN_NEG:
        if (operand->kind != TY_INT && operand->kind != TY_FLOAT) {
            checker_error(c, expr->span,
                          "unary '-' requires Int or Float");
            return c->resolver->ty_error;
        }
        result = operand;
        break;
    case UN_BIT_NOT:
        check_expected(c, expr->span, operand, c->resolver->ty_int);
        result = c->resolver->ty_int;
        break;
    default:
        result = c->resolver->ty_error;
        break;
    }

    check_expected(c, expr->span, result, expected);
    return result;
}

static Type *check_if(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Type *cond;
    Type *then_type;
    Type *else_type;
    Type *result;

    cond = check_expr(c, expr->as.if_.cond, c->resolver->ty_bool);
    check_expected(c, expr->as.if_.cond->span,
                   cond, c->resolver->ty_bool);

    then_type = check_expr(c, expr->as.if_.then_branch, expected);
    else_type = check_expr(c, expr->as.if_.else_branch, expected);
    result = join_types(c, then_type, else_type);

    check_expected(c, expr->span, result, expected);
    return result;
}

static Type *infer_var(TypeChecker *c, AstExpr *expr)
{
    Binding *b;
    Symbol *sym;
    const char *name;
    size_t len;

    name = expr->as.var.name;
    len = expr->as.var.name_len;

    b = binding_lookup(c, name, len);
    if (b != NULL) {
        return b->type;
    }

    sym = resolver_lookup(c->resolver, c->module, name, len);
    if (sym != NULL) {
        switch (sym->kind) {
        case SYM_FN:
            return sym->as.fn.type;
        case SYM_VARIANT:
            if (sym->as.variant.variant != NULL &&
                sym->as.variant.variant->payload != NULL) {
                checker_error(c, expr->span,
                              "constructor '%.*s' requires arguments",
                              (int)len, name);
                return c->resolver->ty_error;
            }
            return sym->as.variant.owner->type;
        default:
            checker_error(c, expr->span, "'%.*s' is not a value",
                          (int)len, name);
            return c->resolver->ty_error;
        }
    }

    if (resolver_is_reserved_name(name, len)) {
        checker_error(c, expr->span,
                      "built-in or reserved name '%.*s' cannot be used as a value",
                      (int)len, name);
        return c->resolver->ty_error;
    }

    checker_error(c, expr->span, "unknown value '%.*s'", (int)len, name);
    return c->resolver->ty_error;
}

static Type *check_function_call(TypeChecker *c, AstExpr *expr,
                                 Type *callee, Type *expected)
{
    TypeList *params;
    AstExprList *args;
    Type *result;

    if (callee->kind == TY_NEVER) {
        return c->resolver->ty_never;
    }
    if (callee->kind != TY_FUNCTION) {
        checker_error(c, expr->span, "value is not callable");
        return c->resolver->ty_error;
    }

    params = callee->as.fn.params;
    args = expr->as.call.args;
    while (params != NULL && args != NULL) {
        check_expr(c, args->expr, params->type);
        params = params->next;
        args = args->next;
    }
    if (params != NULL || args != NULL) {
        checker_error(c, expr->span, "wrong number of arguments");
        return c->resolver->ty_error;
    }

    result = callee->as.fn.result;
    check_expected(c, expr->span, result, expected);
    return result;
}

static Type *check_variant_call(TypeChecker *c, AstExpr *expr,
                                Symbol *sym, Type *expected)
{
    TypeVariant *variant;
    TypeList *payload;
    AstExprList *args;

    variant = sym->as.variant.variant;
    if (variant == NULL) {
        return c->resolver->ty_error;
    }

    payload = variant->payload;
    args = expr->as.call.args;
    while (payload != NULL && args != NULL) {
        check_expr(c, args->expr, payload->type);
        payload = payload->next;
        args = args->next;
    }
    if (payload != NULL || args != NULL) {
        checker_error(c, expr->span,
                      "wrong number of constructor arguments");
        return c->resolver->ty_error;
    }

    check_expected(c, expr->span, sym->as.variant.owner->type, expected);
    return sym->as.variant.owner->type;
}

static Type *check_call(TypeChecker *c, AstExpr *expr, Type *expected)
{
    if (expr->as.call.callee->kind == EXPR_VAR) {
        Binding *b;
        const char *name;
        size_t len;
        Symbol *sym;

        name = expr->as.call.callee->as.var.name;
        len = expr->as.call.callee->as.var.name_len;

        b = binding_lookup(c, name, len);
        if (b != NULL) {
            return check_function_call(c, expr, b->type, expected);
        }

        if (is_builtin_function(name, len)) {
            return check_builtin(c, expr, name,
                                 expr->as.call.args, expr->span);
        }

        sym = resolver_lookup(c->resolver, c->module, name, len);
        if (sym != NULL) {
            if (sym->kind == SYM_FN) {
                return check_function_call(c, expr, sym->as.fn.type,
                                           expected);
            }
            if (sym->kind == SYM_VARIANT) {
                return check_variant_call(c, expr, sym, expected);
            }
            checker_error(c, expr->span, "'%.*s' is not callable",
                          (int)len, name);
            return c->resolver->ty_error;
        }

        checker_error(c, expr->span, "unknown function '%.*s'",
                      (int)len, name);
        return c->resolver->ty_error;
    }

    {
        Type *callee;
        callee = check_expr(c, expr->as.call.callee, NULL);
        return check_function_call(c, expr, callee, expected);
    }
}

static Type *check_lambda(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Binding *saved;
    Binding *params_head;
    Binding *params_tail;
    TypeList *type_head;
    TypeList *type_tail;
    AstParam *param;
    Type *result_type;
    Type *body_type;
    Type *fn_type;

    saved = c->locals;
    params_head = NULL;
    params_tail = NULL;
    type_head = NULL;
    type_tail = NULL;

    for (param = expr->as.lambda.params; param != NULL; param = param->next) {
        Type *pt;
        TypeList *n;

        pt = checker_resolve_type(c, param->type, param->span);
        validate_type(c, pt, param->span, NULL);
        binding_add(c, &params_head, &params_tail,
                    param->name, param->name_len, pt, param->span);

        n = type_list_cons(c->arena, pt, NULL);
        if (n == NULL) {
            fprintf(stderr, "idem: out of memory\n");
            exit(1);
        }
        if (type_tail != NULL) {
            type_tail->next = n;
        } else {
            type_head = n;
        }
        type_tail = n;
    }

    result_type = checker_resolve_type(c, expr->as.lambda.result,
                                        expr->span);
    validate_type(c, result_type, expr->span, NULL);

    bindings_push(c, params_head, params_tail);
    body_type = check_expr(c, expr->as.lambda.body, result_type);
    check_expected(c, expr->as.lambda.body->span, body_type, result_type);
    c->locals = saved;

    fn_type = type_function(c->arena, type_head, result_type);
    check_expected(c, expr->span, fn_type, expected);
    return fn_type;
}

static Type *check_match(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Type *scrutinee;
    AstMatchArm *arm;
    Type *result;

    scrutinee = check_expr(c, expr->as.match.scrutinee, NULL);
    result = c->resolver->ty_never;

    for (arm = expr->as.match.arms; arm != NULL; arm = arm->next) {
        Binding *saved;
        Binding *head;
        Binding *tail;
        Type *body_type;

        saved = c->locals;
        head = NULL;
        tail = NULL;
        check_pattern(c, arm->pattern, scrutinee, 1, &head, &tail);
        bindings_push(c, head, tail);
        body_type = check_expr(c, arm->body, expected);
        c->locals = saved;
        result = join_types(c, result, body_type);
    }

    if (!c->had_error && scrutinee->kind != TY_ERROR) {
        check_match_reachable(c, expr, scrutinee);
    }
    if (!c->had_error && scrutinee->kind != TY_ERROR) {
        check_match_exhaustive(c, expr, scrutinee);
    }

    check_expected(c, expr->span, result, expected);
    return result;
}

static Type *check_expr(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Type *actual;

    if (expr == NULL) {
        return c->resolver->ty_error;
    }

    switch (expr->kind) {
    case EXPR_INT:
    case EXPR_FLOAT:
    case EXPR_BOOL:
    case EXPR_CHAR:
    case EXPR_STRING:
    case EXPR_BYTES:
        actual = type_of_literal(c, expr);
        break;
    case EXPR_UNIT:
        actual = c->resolver->ty_unit;
        break;
    case EXPR_VAR:
        actual = infer_var(c, expr);
        break;
    case EXPR_TUPLE:
        actual = check_tuple(c, expr, expected);
        break;
    case EXPR_ARRAY:
        actual = check_sequence(c, expr, expected, 0);
        break;
    case EXPR_LIST:
        actual = check_sequence(c, expr, expected, 1);
        break;
    case EXPR_MAP:
        actual = check_map(c, expr, expected);
        break;
    case EXPR_STRUCT:
        actual = check_struct_literal(c, expr, expected);
        break;
    case EXPR_FIELD:
        actual = check_field_access(c, expr, expected);
        break;
    case EXPR_INDEX:
        actual = check_index(c, expr, expected);
        break;
    case EXPR_CALL:
        actual = check_call(c, expr, expected);
        break;
    case EXPR_LAMBDA:
        actual = check_lambda(c, expr, expected);
        break;
    case EXPR_IF:
        actual = check_if(c, expr, expected);
        break;
    case EXPR_MATCH:
        actual = check_match(c, expr, expected);
        break;
    case EXPR_BLOCK:
        actual = check_block(c, expr, expected);
        break;
    case EXPR_BINARY:
        actual = check_binary(c, expr, expected);
        break;
    case EXPR_UNARY:
        actual = check_unary(c, expr, expected);
        break;
    case EXPR_ERROR:
        actual = c->resolver->ty_error;
        break;
    default:
        actual = c->resolver->ty_error;
        break;
    }

    check_expected(c, expr->span, actual, expected);
    return actual;
}

static Type *check_block(TypeChecker *c, AstExpr *expr, Type *expected)
{
    Binding *saved;
    AstBlockItem *item;
    Type *result;
    int has_result;

    saved = c->locals;
    result = c->resolver->ty_unit;
    has_result = 0;

    for (item = expr->as.block.items; item != NULL; item = item->next) {
        int last;
        last = item->next == NULL;

        if (item->kind == BLOCK_LET) {
            AstType *annotation;
            Type *value_type;
            Type *pattern_type;
            Binding *head;
            Binding *tail;

            annotation = item->as.let.type;
            if (annotation != NULL) {
                pattern_type = checker_resolve_type(c, annotation, item->span);
                validate_type(c, pattern_type, item->span, NULL);
                value_type = check_expr(c, item->as.let.value, pattern_type);
                check_expected(c, item->as.let.value->span,
                               value_type, pattern_type);
            } else {
                value_type = check_expr(c, item->as.let.value, NULL);
                pattern_type = value_type;
            }

            head = NULL;
            tail = NULL;
            check_pattern(c, item->as.let.pattern, pattern_type, 0,
                          &head, &tail);
            bindings_push(c, head, tail);

            if (last) {
                result = c->resolver->ty_unit;
                has_result = 1;
            }
        } else {
            Type *t;
            t = check_expr(c, item->as.expr, last ? expected : NULL);
            if (last) {
                result = t;
                has_result = 1;
            }
        }
    }

    c->locals = saved;

    if (!has_result) {
        result = c->resolver->ty_unit;
    }

    check_expected(c, expr->span, result, expected);
    return result;
}

/* ------------------------------------------------------------------ */
/* Patterns                                                            */
/* ------------------------------------------------------------------ */

static TypeVariant *find_variant(TypeInfo *info, const char *name, size_t len)
{
    TypeVariant *v;
    for (v = info->variants; v != NULL; v = v->next) {
        if (v->name_len == len && memcmp(v->name, name, len) == 0) {
            return v;
        }
    }
    return NULL;
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

static void check_pattern(TypeChecker *c, AstPattern *pat, Type *scrutinee,
                          int allow_refutable, Binding **head,
                          Binding **tail)
{
    if (pat == NULL || scrutinee == NULL) {
        return;
    }

    switch (pat->kind) {
    case PAT_WILDCARD:
        break;

    case PAT_VAR:
        if (scrutinee->kind == TY_ENUM) {
            TypeVariant *v;
            v = find_variant(scrutinee->as.nominal.info,
                             pat->as.var.name, pat->as.var.name_len);
            if (v != NULL) {
                if (!allow_refutable) {
                    checker_error(c, pat->span,
                                  "refutable pattern in let binding");
                    return;
                }
                if (v->payload != NULL) {
                    checker_error(c, pat->span,
                                  "variant '%.*s' requires arguments",
                                  (int)pat->as.var.name_len,
                                  pat->as.var.name);
                }
                return;
            }
        }
        binding_add(c, head, tail, pat->as.var.name,
                    pat->as.var.name_len, scrutinee, pat->span);
        break;

    case PAT_LITERAL:
        if (!allow_refutable) {
            checker_error(c, pat->span,
                          "refutable pattern in let binding");
        }
        {
            Type *literal_type;
            literal_type = check_expr(c, pat->as.literal.literal, NULL);
            if (!is_subtype(literal_type, scrutinee)) {
                char a[256];
                char b[256];
                format_buffer(literal_type, a, sizeof(a));
                format_buffer(scrutinee, b, sizeof(b));
                checker_error(c, pat->span,
                              "literal pattern of type %s cannot match %s",
                              a, b);
            }
        }
        break;

    case PAT_UNIT:
        if (scrutinee->kind != TY_UNIT) {
            checker_error(c, pat->span,
                          "unit pattern cannot match this type");
        }
        break;

    case PAT_TUPLE: {
        AstPatternList *item;
        TypeList *type;
        item = pat->as.tuple.items;
        type = scrutinee->kind == TY_TUPLE
                   ? scrutinee->as.tuple.items : NULL;
        if (scrutinee->kind != TY_TUPLE) {
            checker_error(c, pat->span,
                          "tuple pattern cannot match this type");
            return;
        }
        while (item != NULL && type != NULL) {
            check_pattern(c, item->pattern, type->type,
                          allow_refutable, head, tail);
            item = item->next;
            type = type->next;
        }
        if (item != NULL || type != NULL) {
            checker_error(c, pat->span, "tuple pattern arity mismatch");
        }
        break;
    }

    case PAT_STRUCT: {
        TypeInfo *info;
        TypeInfo *scrutinee_info;
        TypeField *field;
        AstPatternField *pf;

        if (scrutinee->kind != TY_STRUCT) {
            checker_error(c, pat->span,
                          "struct pattern cannot match this type");
            return;
        }
        scrutinee_info = scrutinee->as.nominal.info;
        info = lookup_struct_info(c, pat->as.struct_.name,
                                  pat->as.struct_.name_len, pat->span);
        if (info == NULL) {
            return;
        }
        if (info != scrutinee_info) {
            checker_error(c, pat->span,
                          "struct pattern type does not match scrutinee");
            return;
        }

        for (pf = pat->as.struct_.fields; pf != NULL; pf = pf->next) {
            AstPatternField *prev;
            int found;

            found = 0;
            for (field = info->fields; field != NULL; field = field->next) {
                if (field->name_len == pf->name_len &&
                    memcmp(field->name, pf->name, pf->name_len) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                checker_error(c, pf->span, "unknown struct field '%.*s'",
                              (int)pf->name_len, pf->name);
            }

            for (prev = pat->as.struct_.fields; prev != pf;
                 prev = prev->next) {
                if (prev->name_len == pf->name_len &&
                    memcmp(prev->name, pf->name, pf->name_len) == 0) {
                    checker_error(c, pf->span,
                                  "duplicate struct field '%.*s'",
                                  (int)pf->name_len, pf->name);
                    break;
                }
            }
        }

        for (field = info->fields; field != NULL; field = field->next) {
            AstPattern *sub;
            sub = find_pattern_field(pat->as.struct_.fields,
                                     field->name, field->name_len);
            if (sub == NULL) {
                checker_error(c, pat->span, "missing struct field '%s'",
                              field->name);
            } else {
                check_pattern(c, sub, field->type, allow_refutable,
                              head, tail);
            }
        }
        break;
    }

    case PAT_ENUM: {
        TypeVariant *v;
        AstPatternList *arg;
        TypeList *payload;

        if (!allow_refutable) {
            checker_error(c, pat->span,
                          "refutable pattern in let binding");
        }
        if (scrutinee->kind != TY_ENUM) {
            checker_error(c, pat->span,
                          "variant pattern cannot match this type");
            return;
        }
        v = find_variant(scrutinee->as.nominal.info,
                         pat->as.enum_.name, pat->as.enum_.name_len);
        if (v == NULL) {
            checker_error(c, pat->span, "unknown variant '%.*s'",
                          (int)pat->as.enum_.name_len,
                          pat->as.enum_.name);
            return;
        }

        arg = pat->as.enum_.payload;
        payload = v->payload;
        if (payload == NULL && arg != NULL) {
            checker_error(c, pat->span,
                          "variant '%.*s' does not take arguments",
                          (int)pat->as.enum_.name_len,
                          pat->as.enum_.name);
            return;
        }
        if (payload != NULL && arg == NULL) {
            checker_error(c, pat->span,
                          "variant '%.*s' requires arguments",
                          (int)pat->as.enum_.name_len,
                          pat->as.enum_.name);
            return;
        }

        while (arg != NULL && payload != NULL) {
            check_pattern(c, arg->pattern, payload->type,
                          allow_refutable, head, tail);
            arg = arg->next;
            payload = payload->next;
        }
        if (arg != NULL || payload != NULL) {
            checker_error(c, pat->span, "variant payload arity mismatch");
        }
        break;
    }

    case PAT_TYPE: {
        Type *member;

        if (!allow_refutable) {
            checker_error(c, pat->span,
                          "type pattern is not allowed in let binding");
            return;
        }
        if (scrutinee->kind == TY_ANY) {
            checker_error(c, pat->span,
                          "type patterns are not allowed on Any");
            return;
        }
        if (scrutinee->kind != TY_UNION) {
            checker_error(c, pat->span,
                          "type pattern requires a union scrutinee");
            return;
        }

        member = checker_resolve_type(c, pat->as.type.type, pat->span);
        validate_type(c, member, pat->span, NULL);
        if (!is_subtype(member, scrutinee)) {
            checker_error(c, pat->span,
                          "type pattern is not a member of the union");
            return;
        }
        binding_add(c, head, tail, pat->as.type.name,
                    pat->as.type.name_len, member, pat->span);
        break;
    }

    case PAT_ERROR:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Exhaustiveness                                                      */
/* ------------------------------------------------------------------ */

typedef struct CoverRow {
    AstPatternList *patterns;
    struct CoverRow *next;
} CoverRow;

static AstPattern *new_wildcard(TypeChecker *c, Span span)
{
    AstPattern *p;
    p = (AstPattern *)checker_alloc(c, sizeof(AstPattern));
    p->kind = PAT_WILDCARD;
    p->span = span;
    return p;
}

static AstPatternList *pattern_list_node(TypeChecker *c, AstPattern *pat,
                                         AstPatternList *next)
{
    AstPatternList *n;
    n = (AstPatternList *)checker_alloc(c, sizeof(AstPatternList));
    n->pattern = pat;
    n->next = next;
    return n;
}

static AstPatternList *copy_pattern_list(TypeChecker *c,
                                         AstPatternList *list)
{
    AstPatternList *head;
    AstPatternList *tail;
    AstPatternList *n;

    head = NULL;
    tail = NULL;
    while (list != NULL) {
        n = pattern_list_node(c, list->pattern, NULL);
        if (tail != NULL) {
            tail->next = n;
        } else {
            head = n;
        }
        tail = n;
        list = list->next;
    }
    return head;
}

static AstPatternList *append_pattern_list(TypeChecker *c,
                                           AstPatternList *head,
                                           AstPatternList *tail,
                                           AstPatternList *rest)
{
    AstPatternList *copy;
    copy = copy_pattern_list(c, rest);
    if (head == NULL) {
        return copy;
    }
    tail->next = copy;
    return head;
}

static TypeList *append_type_list(TypeChecker *c, TypeList *head,
                                  TypeList *rest)
{
    TypeList *copy_head;
    TypeList *copy_tail;
    TypeList *n;

    copy_head = NULL;
    copy_tail = NULL;
    while (head != NULL) {
        n = type_list_cons(c->arena, head->type, NULL);
        if (n == NULL) {
            fprintf(stderr, "idem: out of memory\n");
            exit(1);
        }
        if (copy_tail != NULL) {
            copy_tail->next = n;
        } else {
            copy_head = n;
        }
        copy_tail = n;
        head = head->next;
    }
    if (copy_tail != NULL) {
        copy_tail->next = rest;
        return copy_head;
    }
    return rest;
}

static void cover_row_add(TypeChecker *c, CoverRow **head, CoverRow **tail,
                          AstPatternList *patterns)
{
    CoverRow *row;
    row = (CoverRow *)checker_alloc(c, sizeof(CoverRow));
    row->patterns = patterns;
    if (*tail != NULL) {
        (*tail)->next = row;
    } else {
        *head = row;
    }
    *tail = row;
}

static int pattern_is_variable(Type *type, AstPattern *pat)
{
    TypeVariant *v;
    if (pat->kind == PAT_WILDCARD) {
        return 1;
    }
    if (pat->kind != PAT_VAR) {
        return 0;
    }
    if (type != NULL && type->kind == TY_ENUM) {
        v = find_variant(type->as.nominal.info,
                         pat->as.var.name, pat->as.var.name_len);
        if (v != NULL) {
            return 0;
        }
    }
    return 1;
}

static AstPatternList *wildcards_for_types(TypeChecker *c, TypeList *types,
                                           Span span)
{
    AstPatternList *head;
    AstPatternList *tail;
    AstPatternList *n;

    head = NULL;
    tail = NULL;
    while (types != NULL) {
        n = pattern_list_node(c, new_wildcard(c, span), NULL);
        if (tail != NULL) {
            tail->next = n;
        } else {
            head = n;
        }
        tail = n;
        types = types->next;
    }
    return head;
}

static AstPatternList *row_rest(CoverRow *row)
{
    return row->patterns->next;
}

static int pattern_is_irrefutable(TypeChecker *c, Type *type,
                                  AstPattern *pat)
{
    if (pat == NULL || type == NULL) {
        return 0;
    }

    switch (pat->kind) {
    case PAT_WILDCARD:
        return 1;
    case PAT_VAR:
        if (type->kind == TY_ENUM) {
            TypeVariant *v;
            v = find_variant(type->as.nominal.info,
                             pat->as.var.name, pat->as.var.name_len);
            if (v != NULL) {
                return 0;
            }
        }
        return 1;
    case PAT_UNIT:
        return type->kind == TY_UNIT;
    case PAT_TUPLE: {
        AstPatternList *item;
        TypeList *item_type;
        item = pat->as.tuple.items;
        item_type = type->kind == TY_TUPLE
                        ? type->as.tuple.items : NULL;
        if (type->kind != TY_TUPLE) {
            return 0;
        }
        while (item != NULL && item_type != NULL) {
            if (!pattern_is_irrefutable(c, item_type->type, item->pattern)) {
                return 0;
            }
            item = item->next;
            item_type = item_type->next;
        }
        return item == NULL && item_type == NULL;
    }
    case PAT_STRUCT: {
        TypeInfo *info;
        TypeInfo *pinfo;
        TypeField *field;

        if (type->kind != TY_STRUCT) {
            return 0;
        }
        info = type->as.nominal.info;
        pinfo = lookup_struct_info(c, pat->as.struct_.name,
                                   pat->as.struct_.name_len, pat->span);
        if (pinfo != info) {
            return 0;
        }
        for (field = info->fields; field != NULL; field = field->next) {
            AstPattern *sub;
            sub = find_pattern_field(pat->as.struct_.fields,
                                     field->name, field->name_len);
            if (sub == NULL ||
                !pattern_is_irrefutable(c, field->type, sub)) {
                return 0;
            }
        }
        return 1;
    }
    case PAT_ENUM: {
        TypeVariant *v;
        AstPatternList *item;
        TypeList *payload;

        if (type->kind != TY_ENUM) {
            return 0;
        }
        v = find_variant(type->as.nominal.info,
                         pat->as.enum_.name, pat->as.enum_.name_len);
        if (v == NULL || v->next != NULL || v != type->as.nominal.info->variants) {
            return 0;
        }
        item = pat->as.enum_.payload;
        payload = v->payload;
        while (item != NULL && payload != NULL) {
            if (!pattern_is_irrefutable(c, payload->type, item->pattern)) {
                return 0;
            }
            item = item->next;
            payload = payload->next;
        }
        return item == NULL && payload == NULL;
    }
    default:
        return 0;
    }
}

static int row_is_irrefutable(TypeChecker *c, TypeList *types,
                              AstPatternList *patterns)
{
    while (types != NULL && patterns != NULL) {
        if (!pattern_is_irrefutable(c, types->type, patterns->pattern)) {
            return 0;
        }
        types = types->next;
        patterns = patterns->next;
    }
    return types == NULL && patterns == NULL;
}

static int coverage_rows(TypeChecker *c, TypeList *types, CoverRow *rows);

static int coverage_infinite(TypeChecker *c, TypeList *types,
                             CoverRow *rows)
{
    CoverRow *row;
    CoverRow *head;
    CoverRow *tail;
    int any;

    any = 0;
    head = NULL;
    tail = NULL;

    for (row = rows; row != NULL; row = row->next) {
        if (pattern_is_variable(types->type, row->patterns->pattern)) {
            AstPatternList *rest;
            AstPatternList *patterns;
            AstPatternList *copy;
            rest = row_rest(row);
            copy = copy_pattern_list(c, rest);
            patterns = copy;
            cover_row_add(c, &head, &tail, patterns);
            any = 1;
        }
    }

    if (!any) {
        return 0;
    }
    return coverage_rows(c, types->next, head);
}

static int coverage_bool(TypeChecker *c, TypeList *types, CoverRow *rows)
{
    int value;

    for (value = 0; value <= 1; value++) {
        CoverRow *row;
        CoverRow *head;
        CoverRow *tail;
        int any;

        head = NULL;
        tail = NULL;
        any = 0;

        for (row = rows; row != NULL; row = row->next) {
            AstPattern *pat;
            pat = row->patterns->pattern;
            if (pattern_is_variable(types->type, pat)) {
                AstPatternList *patterns;
                patterns = copy_pattern_list(c, row_rest(row));
                cover_row_add(c, &head, &tail, patterns);
                any = 1;
            } else if (pat->kind == PAT_LITERAL &&
                       pat->as.literal.literal->kind == EXPR_BOOL &&
                       pat->as.literal.literal->as.bool_lit.value == value) {
                AstPatternList *patterns;
                patterns = copy_pattern_list(c, row_rest(row));
                cover_row_add(c, &head, &tail, patterns);
                any = 1;
            }
        }

        if (!any) {
            return 0;
        }
        if (!coverage_rows(c, types->next, head)) {
            return 0;
        }
    }
    return 1;
}

static int coverage_unit(TypeChecker *c, TypeList *types, CoverRow *rows)
{
    CoverRow *row;
    CoverRow *head;
    CoverRow *tail;
    int any;

    head = NULL;
    tail = NULL;
    any = 0;

    for (row = rows; row != NULL; row = row->next) {
        AstPattern *pat;
        pat = row->patterns->pattern;
        if (pattern_is_variable(types->type, pat) || pat->kind == PAT_UNIT) {
            AstPatternList *patterns;
            patterns = copy_pattern_list(c, row_rest(row));
            cover_row_add(c, &head, &tail, patterns);
            any = 1;
        }
    }

    if (!any) {
        return 0;
    }
    return coverage_rows(c, types->next, head);
}

static int coverage_enum(TypeChecker *c, TypeList *types, CoverRow *rows)
{
    TypeVariant *variant;
    TypeInfo *info;

    info = types->type->as.nominal.info;
    for (variant = info->variants; variant != NULL;
         variant = variant->next) {
        CoverRow *row;
        CoverRow *head;
        CoverRow *tail;
        int any;

        head = NULL;
        tail = NULL;
        any = 0;

        for (row = rows; row != NULL; row = row->next) {
            AstPattern *pat;
            AstPatternList *payload;
            AstPatternList *patterns;
            AstPatternList *rest;

            pat = row->patterns->pattern;
            payload = NULL;
            rest = row_rest(row);

            if (pattern_is_variable(types->type, pat)) {
                payload = wildcards_for_types(c, variant->payload,
                                              pat->span);
                any = 1;
            } else if (pat->kind == PAT_VAR &&
                       pat->as.var.name_len == variant->name_len &&
                       memcmp(pat->as.var.name, variant->name,
                              variant->name_len) == 0) {
                payload = NULL;
                any = 1;
            } else if (pat->kind == PAT_ENUM &&
                       pat->as.enum_.name_len == variant->name_len &&
                       memcmp(pat->as.enum_.name, variant->name,
                              variant->name_len) == 0) {
                payload = copy_pattern_list(c, pat->as.enum_.payload);
                any = 1;
            } else {
                continue;
            }

            if (payload != NULL) {
                AstPatternList *p;
                p = payload;
                while (p->next != NULL) {
                    p = p->next;
                }
                patterns = append_pattern_list(c, payload, p, rest);
            } else {
                patterns = copy_pattern_list(c, rest);
            }
            cover_row_add(c, &head, &tail, patterns);
        }

        if (!any) {
            return 0;
        }

        {
            TypeList *all;
            all = append_type_list(c, variant->payload, types->next);
            if (!coverage_rows(c, all, head)) {
                return 0;
            }
        }
    }
    return 1;
}

static int coverage_union(TypeChecker *c, TypeList *types, CoverRow *rows)
{
    TypeList *member;

    for (member = types->type->as.union_.items; member != NULL;
         member = member->next) {
        CoverRow *row;
        CoverRow *head;
        CoverRow *tail;
        int any;

        head = NULL;
        tail = NULL;
        any = 0;

        for (row = rows; row != NULL; row = row->next) {
            AstPattern *pat;
            pat = row->patterns->pattern;
            if (pattern_is_variable(types->type, pat)) {
                AstPatternList *patterns;
                patterns = copy_pattern_list(c, row_rest(row));
                cover_row_add(c, &head, &tail, patterns);
                any = 1;
            } else if (pat->kind == PAT_TYPE) {
                Type *pt;
                pt = resolver_resolve_type(c->resolver, c->module,
                                           pat->as.type.type);
                if (type_equivalent(pt, member->type)) {
                    AstPatternList *patterns;
                    patterns = copy_pattern_list(c, row_rest(row));
                    cover_row_add(c, &head, &tail, patterns);
                    any = 1;
                }
            }
        }

        if (!any) {
            return 0;
        }
        if (!coverage_rows(c, types->next, head)) {
            return 0;
        }
    }
    return 1;
}

static int coverage_tuple(TypeChecker *c, TypeList *types, CoverRow *rows)
{
    TypeList *items;
    TypeList *all;
    CoverRow *row;
    CoverRow *head;
    CoverRow *tail;
    int any;

    items = types->type->as.tuple.items;
    head = NULL;
    tail = NULL;
    any = 0;

    for (row = rows; row != NULL; row = row->next) {
        AstPattern *pat;
        AstPatternList *payload;
        AstPatternList *patterns;
        AstPatternList *rest;

        pat = row->patterns->pattern;
        rest = row_rest(row);

        if (pattern_is_variable(types->type, pat)) {
            payload = wildcards_for_types(c, items, pat->span);
            any = 1;
        } else if (pat->kind == PAT_TUPLE) {
            payload = copy_pattern_list(c, pat->as.tuple.items);
            any = 1;
        } else {
            continue;
        }

        if (payload != NULL) {
            AstPatternList *p;
            p = payload;
            while (p->next != NULL) {
                p = p->next;
            }
            patterns = append_pattern_list(c, payload, p, rest);
        } else {
            patterns = copy_pattern_list(c, rest);
        }
        cover_row_add(c, &head, &tail, patterns);
    }

    if (!any) {
        return 0;
    }
    all = append_type_list(c, items, types->next);
    return coverage_rows(c, all, head);
}

static int coverage_struct(TypeChecker *c, TypeList *types, CoverRow *rows)
{
    TypeInfo *info;
    TypeField *field;
    TypeList *field_types;
    TypeList *field_types_tail;
    TypeList *all;
    CoverRow *row;
    CoverRow *head;
    CoverRow *tail;
    int any;

    info = types->type->as.nominal.info;
    field_types = NULL;
    field_types_tail = NULL;

    for (field = info->fields; field != NULL; field = field->next) {
        TypeList *n;
        n = type_list_cons(c->arena, field->type, NULL);
        if (n == NULL) {
            fprintf(stderr, "idem: out of memory\n");
            exit(1);
        }
        if (field_types_tail != NULL) {
            field_types_tail->next = n;
        } else {
            field_types = n;
        }
        field_types_tail = n;
    }

    head = NULL;
    tail = NULL;
    any = 0;

    for (row = rows; row != NULL; row = row->next) {
        AstPattern *pat;
        AstPatternList *payload;
        AstPatternList *patterns;
        AstPatternList *rest;

        pat = row->patterns->pattern;
        rest = row_rest(row);

        if (pattern_is_variable(types->type, pat)) {
            payload = wildcards_for_types(c, field_types, pat->span);
            any = 1;
        } else if (pat->kind == PAT_STRUCT) {
            TypeInfo *pinfo;
            pinfo = lookup_struct_info(c, pat->as.struct_.name,
                                       pat->as.struct_.name_len,
                                       pat->span);
            if (pinfo != info) {
                continue;
            }
            payload = NULL;
            {
                AstPatternList *ptail;
                ptail = NULL;
                for (field = info->fields; field != NULL;
                     field = field->next) {
                    AstPattern *sub;
                    AstPatternList *n;
                    sub = find_pattern_field(pat->as.struct_.fields,
                                             field->name,
                                             field->name_len);
                    if (sub == NULL) {
                        sub = new_wildcard(c, pat->span);
                    }
                    n = pattern_list_node(c, sub, NULL);
                    if (ptail != NULL) {
                        ptail->next = n;
                    } else {
                        payload = n;
                    }
                    ptail = n;
                }
            }
            any = 1;
        } else {
            continue;
        }

        patterns = copy_pattern_list(c, rest);
        if (payload != NULL) {
            AstPatternList *p;
            p = payload;
            while (p->next != NULL) {
                p = p->next;
            }
            patterns = append_pattern_list(c, payload, p, rest);
        }
        cover_row_add(c, &head, &tail, patterns);
    }

    if (!any) {
        return 0;
    }
    all = append_type_list(c, field_types, types->next);
    return coverage_rows(c, all, head);
}

static int coverage_rows(TypeChecker *c, TypeList *types, CoverRow *rows)
{
    CoverRow *row;

    if (types == NULL) {
        return rows != NULL;
    }
    if (types->type == NULL || types->type->kind == TY_ERROR) {
        return 1;
    }
    if (types->type->kind == TY_NEVER) {
        return 1;
    }

    for (row = rows; row != NULL; row = row->next) {
        if (row_is_irrefutable(c, types, row->patterns)) {
            return 1;
        }
    }

    switch (types->type->kind) {
    case TY_BOOL:
        return coverage_bool(c, types, rows);
    case TY_UNIT:
        return coverage_unit(c, types, rows);
    case TY_ENUM:
        return coverage_enum(c, types, rows);
    case TY_UNION:
        return coverage_union(c, types, rows);
    case TY_TUPLE:
        return coverage_tuple(c, types, rows);
    case TY_STRUCT:
        return coverage_struct(c, types, rows);
    default:
        return coverage_infinite(c, types, rows);
    }
}

static void check_match_reachable(TypeChecker *c, AstExpr *expr,
                                  Type *scrutinee)
{
    CoverRow *head;
    CoverRow *tail;
    AstMatchArm *arm;
    TypeList *types;

    head = NULL;
    tail = NULL;
    types = type_list_cons(c->arena, scrutinee, NULL);
    if (types == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }

    for (arm = expr->as.match.arms; arm != NULL; arm = arm->next) {
        AstPatternList *patterns;

        if (head != NULL && coverage_rows(c, types, head)) {
            checker_error(c, arm->span, "unreachable pattern");
            return;
        }

        patterns = pattern_list_node(c, arm->pattern, NULL);
        cover_row_add(c, &head, &tail, patterns);
    }
}

static void check_match_exhaustive(TypeChecker *c, AstExpr *expr,
                                   Type *scrutinee)
{
    CoverRow *head;
    CoverRow *tail;
    AstMatchArm *arm;
    TypeList *types;

    head = NULL;
    tail = NULL;

    for (arm = expr->as.match.arms; arm != NULL; arm = arm->next) {
        AstPatternList *patterns;
        patterns = pattern_list_node(c, arm->pattern, NULL);
        cover_row_add(c, &head, &tail, patterns);
    }

    types = type_list_cons(c->arena, scrutinee, NULL);
    if (types == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    if (!coverage_rows(c, types, head)) {
        checker_error(c, expr->span, "non-exhaustive match");
    }
}

/* ------------------------------------------------------------------ */
/* Built-in functions                                                  */
/* ------------------------------------------------------------------ */

static const char *BUILTIN_FUNCTIONS[] = {
    "print", "println", "readLine", "readFile", "writeFile", "flush",
    "exit", "panic",
    "len", "get", "set", "remove", "push", "concat", "map", "filter",
    "fold", "sum", "keys", "values",
    "abs", "min", "max", "sqrt", "floor", "ceil", "pow", "random", "now",
    "byteAt", "slice", "compare", "charAt", "chars", "toInt", "toFloat",
    "toString", "toBytes", "fromBytes"
};

static int is_builtin_function(const char *name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof(BUILTIN_FUNCTIONS) / sizeof(BUILTIN_FUNCTIONS[0]);
         i++) {
        if (name_eq(name, len, BUILTIN_FUNCTIONS[i])) {
            return 1;
        }
    }
    return 0;
}

static int name_eq(const char *s, size_t n, const char *lit)
{
    size_t m;
    m = strlen(lit);
    return n == m && memcmp(s, lit, n) == 0;
}

static Type *union_pair(TypeChecker *c, Type *a, Type *b)
{
    return join_types(c, a, b);
}

static int builtin_arg_count(TypeChecker *c, Span span,
                             const char *name, size_t name_len,
                             int actual, int expected)
{
    if (actual != expected) {
        checker_error(c, span,
                      "built-in '%.*s' has the wrong number of arguments",
                      (int)name_len, name);
        return 0;
    }
    return 1;
}

static Type *check_builtin(TypeChecker *c, AstExpr *expr,
                           const char *name, AstExprList *args, Span span)
{
    Type *argv[4];
    AstExprList *it;
    int argc;
    Type *result;
    size_t name_len;

    name_len = expr->as.call.callee->as.var.name_len;
    argc = 0;
    for (it = args; it != NULL; it = it->next) {
        if (argc < 4) {
            argv[argc] = check_expr(c, it->expr, NULL);
        }
        argc += 1;
    }

    if (name_eq(name, name_len, "print") ||
        name_eq(name, name_len, "println")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        result = c->resolver->ty_unit;
    } else if (name_eq(name, name_len, "readLine")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 0)) return c->resolver->ty_error;
        result = union_pair(c, c->resolver->ty_string, c->resolver->ty_unit);
    } else if (name_eq(name, name_len, "readFile")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        result = union_pair(c, c->resolver->ty_string,
                            c->resolver->error_info->type);
    } else if (name_eq(name, name_len, "writeFile")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        check_expected(c, span, argv[1], c->resolver->ty_string);
        result = union_pair(c, c->resolver->ty_unit,
                            c->resolver->error_info->type);
    } else if (name_eq(name, name_len, "flush")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 0)) return c->resolver->ty_error;
        result = c->resolver->ty_unit;
    } else if (name_eq(name, name_len, "exit")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_int);
        result = c->resolver->ty_never;
    } else if (name_eq(name, name_len, "panic")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        result = c->resolver->ty_never;
    } else if (name_eq(name, name_len, "len")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_STRING && argv[0]->kind != TY_BYTES &&
            argv[0]->kind != TY_ARRAY && argv[0]->kind != TY_LIST &&
            argv[0]->kind != TY_MAP && argv[0]->kind != TY_ERROR) {
            checker_error(c, span, "'len' expects a collection or string");
            return c->resolver->ty_error;
        }
        result = c->resolver->ty_int;
    } else if (name_eq(name, name_len, "get")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind == TY_ARRAY || argv[0]->kind == TY_LIST) {
            check_expected(c, span, argv[1], c->resolver->ty_int);
            result = union_pair(c,
                                argv[0]->kind == TY_ARRAY
                                    ? argv[0]->as.array.element
                                    : argv[0]->as.list.element,
                                c->resolver->ty_unit);
        } else if (argv[0]->kind == TY_MAP) {
            check_expected(c, span, argv[1], argv[0]->as.map.key);
            result = union_pair(c, argv[0]->as.map.value,
                                c->resolver->ty_unit);
        } else {
            checker_error(c, span, "'get' expects an Array, List, or Map");
            return c->resolver->ty_error;
        }
    } else if (name_eq(name, name_len, "set")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 3)) return c->resolver->ty_error;
        if (argv[0]->kind == TY_ARRAY) {
            check_expected(c, span, argv[1], c->resolver->ty_int);
            check_expected(c, span, argv[2], argv[0]->as.array.element);
            result = argv[0];
        } else if (argv[0]->kind == TY_MAP) {
            check_expected(c, span, argv[1], argv[0]->as.map.key);
            check_expected(c, span, argv[2], argv[0]->as.map.value);
            result = argv[0];
        } else {
            checker_error(c, span, "'set' expects an Array or Map");
            return c->resolver->ty_error;
        }
    } else if (name_eq(name, name_len, "remove")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_MAP) {
            checker_error(c, span, "'remove' expects a Map");
            return c->resolver->ty_error;
        }
        check_expected(c, span, argv[1], argv[0]->as.map.key);
        result = argv[0];
    } else if (name_eq(name, name_len, "push")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_LIST) {
            checker_error(c, span, "'push' expects a List");
            return c->resolver->ty_error;
        }
        check_expected(c, span, argv[1], argv[0]->as.list.element);
        result = argv[0];
    } else if (name_eq(name, name_len, "concat")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind == TY_STRING) {
            check_expected(c, span, argv[1], c->resolver->ty_string);
            result = c->resolver->ty_string;
        } else if (argv[0]->kind == TY_BYTES) {
            check_expected(c, span, argv[1], c->resolver->ty_bytes);
            result = c->resolver->ty_bytes;
        } else if (argv[0]->kind == TY_ARRAY) {
            check_expected(c, span, argv[1], argv[0]);
            result = argv[0];
        } else if (argv[0]->kind == TY_LIST) {
            check_expected(c, span, argv[1], argv[0]);
            result = argv[0];
        } else {
            checker_error(c, span, "'concat' expects matching collections");
            return c->resolver->ty_error;
        }
    } else if (name_eq(name, name_len, "slice")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 3)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_STRING && argv[0]->kind != TY_BYTES) {
            checker_error(c, span, "'slice' expects String or Bytes");
            return c->resolver->ty_error;
        }
        check_expected(c, span, argv[1], c->resolver->ty_int);
        check_expected(c, span, argv[2], c->resolver->ty_int);
        result = argv[0];
    } else if (name_eq(name, name_len, "compare")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind == TY_STRING) {
            check_expected(c, span, argv[1], c->resolver->ty_string);
        } else if (argv[0]->kind == TY_BYTES) {
            check_expected(c, span, argv[1], c->resolver->ty_bytes);
        } else {
            checker_error(c, span, "'compare' expects String or Bytes");
            return c->resolver->ty_error;
        }
        result = c->resolver->ty_int;
    } else if (name_eq(name, name_len, "byteAt")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_STRING && argv[0]->kind != TY_BYTES) {
            checker_error(c, span, "'byteAt' expects String or Bytes");
            return c->resolver->ty_error;
        }
        check_expected(c, span, argv[1], c->resolver->ty_int);
        result = union_pair(c, c->resolver->ty_int, c->resolver->ty_unit);
    } else if (name_eq(name, name_len, "charAt")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        check_expected(c, span, argv[1], c->resolver->ty_int);
        result = union_pair(c, c->resolver->ty_char, c->resolver->ty_unit);
    } else if (name_eq(name, name_len, "chars")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        result = type_array(c->arena, c->resolver->ty_char);
    } else if (name_eq(name, name_len, "toInt")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        result = union_pair(c, c->resolver->ty_int, c->resolver->ty_unit);
    } else if (name_eq(name, name_len, "toFloat")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        result = union_pair(c, c->resolver->ty_float, c->resolver->ty_unit);
    } else if (name_eq(name, name_len, "toString")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        result = c->resolver->ty_string;
    } else if (name_eq(name, name_len, "toBytes")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_string);
        result = c->resolver->ty_bytes;
    } else if (name_eq(name, name_len, "fromBytes")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_bytes);
        result = union_pair(c, c->resolver->ty_string,
                            c->resolver->error_info->type);
    } else if (name_eq(name, name_len, "map")) {
        Type *element;
        Type *fn;
        Type *result_element;
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_ARRAY && argv[0]->kind != TY_LIST) {
            checker_error(c, span, "'map' expects an Array or List");
            return c->resolver->ty_error;
        }
        element = argv[0]->kind == TY_ARRAY
                      ? argv[0]->as.array.element
                      : argv[0]->as.list.element;
        fn = argv[1];
        if (fn->kind != TY_FUNCTION || fn->as.fn.params == NULL ||
            fn->as.fn.params->next != NULL ||
            !type_equivalent(fn->as.fn.params->type, element)) {
            checker_error(c, span,
                          "'map' expects a function from the element type");
            return c->resolver->ty_error;
        }
        result_element = fn->as.fn.result;
        result = argv[0]->kind == TY_ARRAY
                     ? type_array(c->arena, result_element)
                     : type_list_of(c->arena, result_element);
    } else if (name_eq(name, name_len, "filter")) {
        Type *element;
        Type *fn;
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_ARRAY && argv[0]->kind != TY_LIST) {
            checker_error(c, span, "'filter' expects an Array or List");
            return c->resolver->ty_error;
        }
        element = argv[0]->kind == TY_ARRAY
                      ? argv[0]->as.array.element
                      : argv[0]->as.list.element;
        fn = argv[1];
        if (fn->kind != TY_FUNCTION || fn->as.fn.params == NULL ||
            fn->as.fn.params->next != NULL ||
            !type_equivalent(fn->as.fn.params->type, element) ||
            fn->as.fn.result->kind != TY_BOOL) {
            checker_error(c, span,
                          "'filter' expects a predicate over the element type");
            return c->resolver->ty_error;
        }
        result = argv[0];
    } else if (name_eq(name, name_len, "fold")) {
        Type *element;
        Type *fn;
        Type *init;
        if (!builtin_arg_count(c, span, name, name_len, argc, 3)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_ARRAY && argv[0]->kind != TY_LIST) {
            checker_error(c, span, "'fold' expects an Array or List");
            return c->resolver->ty_error;
        }
        element = argv[0]->kind == TY_ARRAY
                      ? argv[0]->as.array.element
                      : argv[0]->as.list.element;
        init = argv[1];
        fn = argv[2];
        if (fn->kind != TY_FUNCTION || fn->as.fn.params == NULL ||
            fn->as.fn.params->next == NULL ||
            fn->as.fn.params->next->next != NULL ||
            !type_equivalent(fn->as.fn.params->type, init) ||
            !type_equivalent(fn->as.fn.params->next->type, element) ||
            !type_equivalent(fn->as.fn.result, init)) {
            checker_error(c, span,
                          "'fold' expects (U, T) -> U and an initial U");
            return c->resolver->ty_error;
        }
        result = init;
    } else if (name_eq(name, name_len, "sum")) {
        Type *element;
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_ARRAY && argv[0]->kind != TY_LIST) {
            checker_error(c, span, "'sum' expects an Array or List");
            return c->resolver->ty_error;
        }
        element = argv[0]->kind == TY_ARRAY
                      ? argv[0]->as.array.element
                      : argv[0]->as.list.element;
        if (element->kind != TY_INT && element->kind != TY_FLOAT) {
            checker_error(c, span, "'sum' expects Int or Float elements");
            return c->resolver->ty_error;
        }
        result = element;
    } else if (name_eq(name, name_len, "keys")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_MAP) {
            checker_error(c, span, "'keys' expects a Map");
            return c->resolver->ty_error;
        }
        result = type_array(c->arena, argv[0]->as.map.key);
    } else if (name_eq(name, name_len, "values")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_MAP) {
            checker_error(c, span, "'values' expects a Map");
            return c->resolver->ty_error;
        }
        result = type_array(c->arena, argv[0]->as.map.value);
    } else if (name_eq(name, name_len, "abs")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_INT && argv[0]->kind != TY_FLOAT) {
            checker_error(c, span, "'abs' expects Int or Float");
            return c->resolver->ty_error;
        }
        result = argv[0];
    } else if (name_eq(name, name_len, "min") ||
               name_eq(name, name_len, "max")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        if (argv[0]->kind != TY_INT && argv[0]->kind != TY_FLOAT) {
            checker_error(c, span, "'%.*s' expects Int or Float", (int)name_len, name);
            return c->resolver->ty_error;
        }
        if (!type_equivalent(argv[0], argv[1])) {
            checker_error(c, span, "'%.*s' expects two values of the same type",
                          (int)name_len, name);
            return c->resolver->ty_error;
        }
        result = argv[0];
    } else if (name_eq(name, name_len, "sqrt") ||
               name_eq(name, name_len, "floor") ||
               name_eq(name, name_len, "ceil")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 1)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_float);
        result = c->resolver->ty_float;
    } else if (name_eq(name, name_len, "pow")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 2)) return c->resolver->ty_error;
        check_expected(c, span, argv[0], c->resolver->ty_float);
        check_expected(c, span, argv[1], c->resolver->ty_float);
        result = c->resolver->ty_float;
    } else if (name_eq(name, name_len, "random") ||
               name_eq(name, name_len, "now")) {
        if (!builtin_arg_count(c, span, name, name_len, argc, 0)) return c->resolver->ty_error;
        result = c->resolver->ty_int;
    } else {
        checker_error(c, span, "unknown built-in '%.*s'", (int)name_len, name);
        result = c->resolver->ty_error;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* Program checking                                                    */
/* ------------------------------------------------------------------ */

static int validate_symbol_types(TypeChecker *c)
{
    Module *m;
    Symbol *s;

    for (m = c->resolver->modules; m != NULL; m = m->next) {
        if (m->failed) {
            continue;
        }
        for (s = m->symbols; s != NULL; s = s->next) {
            switch (s->kind) {
            case SYM_ALIAS:
                validate_type(c, s->as.alias.target, s->span, NULL);
                break;
            case SYM_STRUCT: {
                TypeField *f;
                for (f = s->as.nominal.info->fields; f != NULL;
                     f = f->next) {
                    validate_type(c, f->type, s->span, NULL);
                }
                break;
            }
            case SYM_ENUM: {
                TypeVariant *v;
                TypeList *p;
                for (v = s->as.nominal.info->variants; v != NULL;
                     v = v->next) {
                    for (p = v->payload; p != NULL; p = p->next) {
                        validate_type(c, p->type, s->span, NULL);
                    }
                }
                break;
            }
            case SYM_FN: {
                TypeList *p;
                for (p = s->as.fn.type->as.fn.params; p != NULL;
                     p = p->next) {
                    validate_type(c, p->type, s->span, NULL);
                }
                validate_type(c, s->as.fn.type->as.fn.result, s->span, NULL);
                break;
            }
            default:
                break;
            }
            if (c->had_error) {
                return 0;
            }
        }
    }
    return 1;
}

static int check_function(TypeChecker *c, Symbol *sym)
{
    AstDecl *decl;
    TypeList *param_type;
    AstParam *param;
    Binding *saved;
    Binding *head;
    Binding *tail;
    Type *body_type;
    Type *result_type;

    decl = sym->as.fn.decl;
    saved = c->locals;
    c->locals = NULL;
    c->module = sym->module;
    c->function = sym;

    param_type = sym->as.fn.type->as.fn.params;
    param = decl->as.fn.params;
    head = NULL;
    tail = NULL;

    while (param != NULL && param_type != NULL) {
        binding_add(c, &head, &tail, param->name, param->name_len,
                    param_type->type, param->span);
        param = param->next;
        param_type = param_type->next;
    }
    bindings_push(c, head, tail);

    result_type = sym->as.fn.type->as.fn.result;
    body_type = check_expr(c, decl->as.fn.body, result_type);
    check_expected(c, decl->as.fn.body->span, body_type, result_type);

    c->locals = saved;
    return !c->had_error;
}

void checker_init(TypeChecker *c, Resolver *r, Arena *arena)
{
    memset(c, 0, sizeof(TypeChecker));
    c->resolver = r;
    c->arena = arena;
}

int checker_check_program(TypeChecker *c)
{
    Module *m;
    Symbol *s;

    if (!validate_symbol_types(c)) {
        return 0;
    }

    for (m = c->resolver->modules; m != NULL; m = m->next) {
        if (m->failed) {
            continue;
        }
        for (s = m->symbols; s != NULL; s = s->next) {
            if (s->kind == SYM_FN) {
                if (!check_function(c, s)) {
                    return 0;
                }
            }
        }
    }

    return !c->had_error;
}

int checker_had_error(const TypeChecker *c)
{
    return c->had_error;
}

Span checker_error_span(const TypeChecker *c)
{
    return c->error_span;
}

const char *checker_error_message(const TypeChecker *c)
{
    return c->error_message;
}
