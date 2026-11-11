#ifndef IDEM_TYPES_H
#define IDEM_TYPES_H

#include <stddef.h>

#include "arena.h"
#include "platform.h"

typedef struct Type Type;
typedef struct TypeList TypeList;
typedef struct TypeField TypeField;
typedef struct TypeVariant TypeVariant;
typedef struct TypeInfo TypeInfo;

typedef enum TypeKind {
    TY_ANY,
    TY_NEVER,
    TY_UNIT,
    TY_BOOL,
    TY_INT,
    TY_FLOAT,
    TY_CHAR,
    TY_STRING,
    TY_BYTES,
    TY_ARRAY,
    TY_LIST,
    TY_MAP,
    TY_TUPLE,
    TY_FUNCTION,
    TY_UNION,
    TY_STRUCT,
    TY_ENUM,
    TY_ERROR
} TypeKind;

struct TypeList {
    Type *type;
    TypeList *next;
};

struct TypeField {
    const char *name;
    size_t name_len;
    Type *type;
    TypeField *next;
};

struct TypeVariant {
    const char *name;
    size_t name_len;
    TypeList *payload; /* NULL when the variant has no payload */
    TypeVariant *next;
};

struct TypeInfo {
    TypeKind kind; /* TY_STRUCT or TY_ENUM */
    u32 id;
    const char *name;
    size_t name_len;
    Type *type;
    TypeField *fields;     /* structs */
    TypeVariant *variants; /* enums */
};

struct Type {
    TypeKind kind;
    union {
        struct {
            Type *element;
        } array;
        struct {
            Type *element;
        } list;
        struct {
            Type *key;
            Type *value;
        } map;
        struct {
            TypeList *items;
        } tuple;
        struct {
            TypeList *params;
            Type *result;
        } fn;
        struct {
            TypeList *items;
        } union_;
        struct {
            TypeInfo *info;
        } nominal;
    } as;
};

Type *type_new(Arena *arena, TypeKind kind);
Type *type_primitive(Arena *arena, TypeKind kind);
TypeList *type_list_cons(Arena *arena, Type *type, TypeList *next);
Type *type_array(Arena *arena, Type *element);
Type *type_list_of(Arena *arena, Type *element);
Type *type_map(Arena *arena, Type *key, Type *value);
Type *type_tuple(Arena *arena, TypeList *items);
Type *type_function(Arena *arena, TypeList *params, Type *result);
Type *type_union(Arena *arena, TypeList *items);
TypeInfo *type_info_new(Arena *arena, TypeKind kind, u32 id,
                        const char *name, size_t name_len);
TypeField *type_field_new(Arena *arena, const char *name, size_t name_len,
                          Type *type);
TypeVariant *type_variant_new(Arena *arena, const char *name,
                              size_t name_len, TypeList *payload);

int type_equal(const Type *a, const Type *b);
const char *type_kind_name(TypeKind kind);

#endif /* IDEM_TYPES_H */
