#ifndef IDEM_RESOLVER_H
#define IDEM_RESOLVER_H

#include <stddef.h>

#include "arena.h"
#include "ast.h"
#include "token.h"
#include "types.h"

typedef struct Module Module;
typedef struct Symbol Symbol;

typedef enum SymbolKind {
    SYM_ALIAS,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_FN,
    SYM_VARIANT
} SymbolKind;

typedef enum AliasState {
    ALIAS_UNRESOLVED,
    ALIAS_RESOLVING,
    ALIAS_RESOLVED
} AliasState;

struct Symbol {
    SymbolKind kind;
    const char *name;
    size_t name_len;
    Span span;
    Module *module;
    union {
        struct {
            AstDecl *decl;
            Type *target;
            AliasState state;
        } alias;
        struct {
            AstDecl *decl;
            TypeInfo *info;
        } nominal;
        struct {
            AstDecl *decl;
            Type *type;
        } fn;
        struct {
            TypeInfo *owner;
            AstVariant *ast;
            TypeVariant *variant;
        } variant;
    } as;
    Symbol *next;
};

typedef struct ImportLink {
    Module *module;
    Span span;
    struct ImportLink *next;
} ImportLink;

typedef struct Visibility {
    Symbol *symbol;
    Span span;
    struct Visibility *next;
} Visibility;

struct Module {
    const char *path;
    const char *source;
    size_t source_len;
    AstProgram *ast;
    Symbol *symbols;
    Symbol *symbols_tail;
    Visibility *visible;
    Visibility *visible_tail;
    ImportLink *imports;
    ImportLink *imports_tail;
    Module *next;
    int loading;
    int loaded;
    int failed;
    int is_root;
};

typedef struct Resolver {
    Arena *arena;
    Module *modules;
    Module *modules_tail;
    Module *root;
    Symbol *prelude;

    Type *ty_any;
    Type *ty_never;
    Type *ty_unit;
    Type *ty_bool;
    Type *ty_int;
    Type *ty_float;
    Type *ty_char;
    Type *ty_string;
    Type *ty_bytes;
    Type *ty_error;

    TypeInfo *error_info;
    u32 next_type_id;

    int had_error;
    Span error_span;
    char error_message[512];
} Resolver;

void resolver_init(Resolver *r, Arena *arena);
int resolver_resolve(Resolver *r, const char *root_path);

Module *resolver_root(Resolver *r);
Symbol *resolver_lookup(Resolver *r, Module *module,
                        const char *name, size_t name_len);
Type *resolver_resolve_type(Resolver *r, Module *module, AstType *type);
int resolver_is_reserved_name(const char *name, size_t name_len);

int resolver_had_error(const Resolver *r);
Span resolver_error_span(const Resolver *r);
const char *resolver_error_message(const Resolver *r);

#endif /* IDEM_RESOLVER_H */
