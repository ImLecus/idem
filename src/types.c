#include "types.h"

#include <string.h>

Type *type_new(Arena *arena, TypeKind kind)
{
    Type *t;
    t = (Type *)arena_alloc(arena, sizeof(Type));
    if (t == NULL) {
        return NULL;
    }
    memset(t, 0, sizeof(Type));
    t->kind = kind;
    return t;
}

Type *type_primitive(Arena *arena, TypeKind kind)
{
    return type_new(arena, kind);
}

TypeList *type_list_cons(Arena *arena, Type *type, TypeList *next)
{
    TypeList *l;
    l = (TypeList *)arena_alloc(arena, sizeof(TypeList));
    if (l == NULL) {
        return NULL;
    }
    memset(l, 0, sizeof(TypeList));
    l->type = type;
    l->next = next;
    return l;
}

Type *type_array(Arena *arena, Type *element)
{
    Type *t;
    t = type_new(arena, TY_ARRAY);
    if (t != NULL) {
        t->as.array.element = element;
    }
    return t;
}

Type *type_list_of(Arena *arena, Type *element)
{
    Type *t;
    t = type_new(arena, TY_LIST);
    if (t != NULL) {
        t->as.list.element = element;
    }
    return t;
}

Type *type_map(Arena *arena, Type *key, Type *value)
{
    Type *t;
    t = type_new(arena, TY_MAP);
    if (t != NULL) {
        t->as.map.key = key;
        t->as.map.value = value;
    }
    return t;
}

Type *type_tuple(Arena *arena, TypeList *items)
{
    Type *t;
    t = type_new(arena, TY_TUPLE);
    if (t != NULL) {
        t->as.tuple.items = items;
    }
    return t;
}

Type *type_function(Arena *arena, TypeList *params, Type *result)
{
    Type *t;
    t = type_new(arena, TY_FUNCTION);
    if (t != NULL) {
        t->as.fn.params = params;
        t->as.fn.result = result;
    }
    return t;
}

Type *type_union(Arena *arena, TypeList *items)
{
    Type *t;
    t = type_new(arena, TY_UNION);
    if (t != NULL) {
        t->as.union_.items = items;
    }
    return t;
}

static void union_add_normalized(Arena *arena, Type *type,
                                 TypeList **head, TypeList **tail,
                                 int *has_any)
{
    TypeList *it;

    if (type == NULL) {
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
            union_add_normalized(arena, it->type, head, tail, has_any);
        }
        return;
    }

    for (it = *head; it != NULL; it = it->next) {
        if (type_equal(it->type, type)) {
            return;
        }
    }

    it = type_list_cons(arena, type, NULL);
    if (it == NULL) {
        return;
    }
    if (*tail != NULL) {
        (*tail)->next = it;
    } else {
        *head = it;
    }
    *tail = it;
}

Type *type_union_normalized(Arena *arena, TypeList *items)
{
    TypeList *head;
    TypeList *tail;
    int has_any;

    head = NULL;
    tail = NULL;
    has_any = 0;

    while (items != NULL) {
        union_add_normalized(arena, items->type, &head, &tail, &has_any);
        items = items->next;
    }

    if (has_any) {
        return type_new(arena, TY_ANY);
    }
    if (head == NULL) {
        return type_new(arena, TY_NEVER);
    }
    if (head->next == NULL) {
        return head->type;
    }
    return type_union(arena, head);
}

TypeInfo *type_info_new(Arena *arena, TypeKind kind, u32 id,
                        const char *name, size_t name_len)
{
    TypeInfo *info;
    Type *type;

    info = (TypeInfo *)arena_alloc(arena, sizeof(TypeInfo));
    if (info == NULL) {
        return NULL;
    }
    memset(info, 0, sizeof(TypeInfo));

    info->kind = kind;
    info->id = id;
    info->name_len = name_len;
    info->name = arena_strndup(arena, name, name_len);

    type = type_new(arena, kind);
    if (type == NULL) {
        return NULL;
    }
    type->as.nominal.info = info;
    info->type = type;
    return info;
}

TypeField *type_field_new(Arena *arena, const char *name, size_t name_len,
                          Type *type)
{
    TypeField *field;
    field = (TypeField *)arena_alloc(arena, sizeof(TypeField));
    if (field == NULL) {
        return NULL;
    }
    memset(field, 0, sizeof(TypeField));
    field->name_len = name_len;
    field->name = arena_strndup(arena, name, name_len);
    field->type = type;
    return field;
}

TypeVariant *type_variant_new(Arena *arena, const char *name,
                              size_t name_len, TypeList *payload)
{
    TypeVariant *variant;
    variant = (TypeVariant *)arena_alloc(arena, sizeof(TypeVariant));
    if (variant == NULL) {
        return NULL;
    }
    memset(variant, 0, sizeof(TypeVariant));
    variant->name_len = name_len;
    variant->name = arena_strndup(arena, name, name_len);
    variant->payload = payload;
    return variant;
}

static int type_list_equal(const TypeList *a, const TypeList *b)
{
    while (a != NULL && b != NULL) {
        if (!type_equal(a->type, b->type)) {
            return 0;
        }
        a = a->next;
        b = b->next;
    }
    return a == NULL && b == NULL;
}

int type_equal(const Type *a, const Type *b)
{
    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->kind != b->kind) {
        return 0;
    }

    switch (a->kind) {
    case TY_ARRAY:
        return type_equal(a->as.array.element, b->as.array.element);
    case TY_LIST:
        return type_equal(a->as.list.element, b->as.list.element);
    case TY_MAP:
        return type_equal(a->as.map.key, b->as.map.key) &&
               type_equal(a->as.map.value, b->as.map.value);
    case TY_TUPLE:
        return type_list_equal(a->as.tuple.items, b->as.tuple.items);
    case TY_FUNCTION:
        return type_list_equal(a->as.fn.params, b->as.fn.params) &&
               type_equal(a->as.fn.result, b->as.fn.result);
    case TY_UNION:
        return type_list_equal(a->as.union_.items, b->as.union_.items);
    case TY_STRUCT:
    case TY_ENUM:
        return a->as.nominal.info == b->as.nominal.info;
    default:
        return 1;
    }
}

const char *type_kind_name(TypeKind kind)
{
    switch (kind) {
    case TY_ANY:      return "Any";
    case TY_NEVER:    return "Never";
    case TY_UNIT:     return "Unit";
    case TY_BOOL:     return "Bool";
    case TY_INT:      return "Int";
    case TY_FLOAT:    return "Float";
    case TY_CHAR:     return "Char";
    case TY_STRING:   return "String";
    case TY_BYTES:    return "Bytes";
    case TY_ARRAY:    return "Array";
    case TY_LIST:     return "List";
    case TY_MAP:      return "Map";
    case TY_TUPLE:    return "Tuple";
    case TY_FUNCTION: return "Function";
    case TY_UNION:    return "Union";
    case TY_STRUCT:   return "Struct";
    case TY_ENUM:     return "Enum";
    case TY_ERROR:    return "<error>";
    }
    return "Unknown";
}

static void append_str(char **p, char *end, const char *s)
{
    while (s != NULL && *s != '\0' && *p < end) {
        **p = *s;
        *p += 1;
        s += 1;
    }
}

static void append_char(char **p, char *end, char c)
{
    if (*p < end) {
        **p = c;
        *p += 1;
    }
}

static void format_type(const Type *type, char **p, char *end);

static void format_type_list(const TypeList *list, char **p, char *end,
                             const char *separator)
{
    const TypeList *it;
    int first;

    first = 1;
    for (it = list; it != NULL; it = it->next) {
        if (!first) {
            append_str(p, end, separator);
        }
        format_type(it->type, p, end);
        first = 0;
    }
}

static void format_type(const Type *type, char **p, char *end)
{
    if (type == NULL) {
        append_str(p, end, "<null>");
        return;
    }

    switch (type->kind) {
    case TY_ARRAY:
        append_str(p, end, "Array[");
        format_type(type->as.array.element, p, end);
        append_char(p, end, ']');
        break;
    case TY_LIST:
        append_str(p, end, "List[");
        format_type(type->as.list.element, p, end);
        append_char(p, end, ']');
        break;
    case TY_MAP:
        append_str(p, end, "Map[");
        format_type(type->as.map.key, p, end);
        append_str(p, end, ", ");
        format_type(type->as.map.value, p, end);
        append_char(p, end, ']');
        break;
    case TY_TUPLE:
        append_char(p, end, '(');
        format_type_list(type->as.tuple.items, p, end, ", ");
        append_char(p, end, ')');
        break;
    case TY_FUNCTION:
        append_char(p, end, '(');
        format_type_list(type->as.fn.params, p, end, ", ");
        append_str(p, end, ") -> ");
        format_type(type->as.fn.result, p, end);
        break;
    case TY_UNION:
        format_type_list(type->as.union_.items, p, end, " | ");
        break;
    case TY_STRUCT:
    case TY_ENUM:
        append_str(p, end, type->as.nominal.info->name);
        break;
    default:
        append_str(p, end, type_kind_name(type->kind));
        break;
    }
}

void type_format(const Type *type, char *buf, size_t cap)
{
    char *p;
    char *end;

    if (cap == 0) {
        return;
    }
    p = buf;
    end = buf + cap - 1;
    format_type(type, &p, end);
    *p = '\0';
}
