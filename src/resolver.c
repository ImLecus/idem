#include "resolver.h"

#include "lexer.h"
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Allocation, diagnostics, and small helpers                          */
/* ------------------------------------------------------------------ */

static void *resolver_alloc(Resolver *r, size_t size)
{
    void *p;
    p = arena_alloc(r->arena, size);
    if (p == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    memset(p, 0, size);
    return p;
}

static void resolver_error(Resolver *r, Span span, const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    size_t out;
    const char *p;

    if (r->had_error) {
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

    r->had_error = 1;
    r->error_span = span;
    strcpy(r->error_message, buf);
}

static const char *copy_name(Resolver *r, const char *s, size_t n)
{
    char *p;
    p = arena_strndup(r->arena, s, n);
    if (p == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    return p;
}

static int name_eq(const char *s, size_t n, const char *lit)
{
    size_t m;
    m = strlen(lit);
    return n == m && memcmp(s, lit, n) == 0;
}

static Span span_at_file(const char *file, u32 line, u32 column)
{
    Span s;
    s.file = file;
    s.line = line;
    s.column = column;
    s.offset = 0;
    return s;
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

static const char *path_normalize(Resolver *r, const char *path)
{
    size_t len;
    size_t p;
    size_t out_len;
    size_t count;
    size_t base;
    int absolute;
    char *out;
    size_t *starts;
    size_t *lens;

    len = strlen(path);
    out = (char *)resolver_alloc(r, len + 3);
    starts = (size_t *)resolver_alloc(r, (len / 2 + 2) * sizeof(size_t));
    lens = (size_t *)resolver_alloc(r, (len / 2 + 2) * sizeof(size_t));

    absolute = len > 0 && path[0] == '/';
    base = absolute ? 1 : 0;
    out_len = base;
    if (absolute) {
        out[0] = '/';
    }
    count = 0;
    p = 0;

    while (p < len) {
        size_t start;
        size_t seg_len;

        while (p < len && path[p] == '/') {
            p += 1;
        }
        start = p;
        while (p < len && path[p] != '/') {
            p += 1;
        }
        seg_len = p - start;
        if (seg_len == 0) {
            continue;
        }
        if (seg_len == 1 && path[start] == '.') {
            continue;
        }
        if (seg_len == 2 && path[start] == '.' && path[start + 1] == '.') {
            if (count > 0) {
                size_t prev_len;
                size_t prev_start;
                prev_len = lens[count - 1];
                prev_start = starts[count - 1];
                if (!(prev_len == 2 &&
                      out[prev_start] == '.' &&
                      out[prev_start + 1] == '.')) {
                    count -= 1;
                    out_len = count > 0 ? starts[count] - 1 : base;
                    continue;
                }
            }
            if (absolute) {
                continue;
            }
        }

        if (out_len > base) {
            out[out_len] = '/';
            out_len += 1;
        }
        starts[count] = out_len;
        lens[count] = seg_len;
        memcpy(out + out_len, path + start, seg_len);
        out_len += seg_len;
        count += 1;
    }

    out[out_len] = '\0';
    if (out_len == 0) {
        if (absolute) {
            out[0] = '/';
            out[1] = '\0';
        } else {
            out[0] = '.';
            out[1] = '\0';
        }
    }
    return out;
}

static const char *path_dirname(Resolver *r, const char *path)
{
    size_t len;
    size_t i;

    len = strlen(path);
    for (i = len; i > 0; i--) {
        if (path[i - 1] == '/') {
            if (i == 1) {
                return "/";
            }
            return copy_name(r, path, i - 1);
        }
    }
    return ".";
}

static const char *path_join(Resolver *r, const char *base_path,
                             const char *rel)
{
    const char *dir;
    size_t dlen;
    size_t rlen;
    char *tmp;

    if (rel[0] == '/') {
        return path_normalize(r, rel);
    }

    dir = path_dirname(r, base_path);
    dlen = strlen(dir);
    rlen = strlen(rel);
    tmp = (char *)resolver_alloc(r, dlen + rlen + 2);
    memcpy(tmp, dir, dlen);
    tmp[dlen] = '/';
    memcpy(tmp + dlen + 1, rel, rlen);
    tmp[dlen + 1 + rlen] = '\0';
    return path_normalize(r, tmp);
}

/* ------------------------------------------------------------------ */
/* Reserved names and built-in type names                              */
/* ------------------------------------------------------------------ */

static const char *RESERVED_TYPES[] = {
    "Any", "Never", "Unit", "Bool", "Int", "Float", "Char",
    "String", "Bytes", "Array", "List", "Map", "Error"
};

static const char *RESERVED_VALUES[] = {
    "print", "println", "readLine", "readFile", "writeFile", "flush",
    "exit", "panic",
    "len", "get", "set", "remove", "push", "concat", "map", "filter",
    "fold", "sum", "keys", "values",
    "abs", "min", "max", "sqrt", "floor", "ceil", "pow", "random", "now",
    "byteAt", "slice", "compare", "charAt", "chars", "toInt", "toFloat",
    "toString", "toBytes", "fromBytes",
    "NotFound", "PermissionDenied", "InvalidUtf8", "Io"
};

static int in_name_list(const char **list, size_t count,
                        const char *name, size_t len)
{
    size_t i;
    for (i = 0; i < count; i++) {
        if (name_eq(name, len, list[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_reserved_name(const char *name, size_t len)
{
    if (in_name_list(RESERVED_TYPES,
                     sizeof(RESERVED_TYPES) / sizeof(RESERVED_TYPES[0]),
                     name, len)) {
        return 1;
    }
    if (in_name_list(RESERVED_VALUES,
                     sizeof(RESERVED_VALUES) / sizeof(RESERVED_VALUES[0]),
                     name, len)) {
        return 1;
    }
    return 0;
}

int resolver_is_reserved_name(const char *name, size_t name_len)
{
    return is_reserved_name(name, name_len);
}

static int primitive_kind(const char *name, size_t len)
{
    if (name_eq(name, len, "Any")) {
        return (int)TY_ANY;
    }
    if (name_eq(name, len, "Never")) {
        return (int)TY_NEVER;
    }
    if (name_eq(name, len, "Unit")) {
        return (int)TY_UNIT;
    }
    if (name_eq(name, len, "Bool")) {
        return (int)TY_BOOL;
    }
    if (name_eq(name, len, "Int")) {
        return (int)TY_INT;
    }
    if (name_eq(name, len, "Float")) {
        return (int)TY_FLOAT;
    }
    if (name_eq(name, len, "Char")) {
        return (int)TY_CHAR;
    }
    if (name_eq(name, len, "String")) {
        return (int)TY_STRING;
    }
    if (name_eq(name, len, "Bytes")) {
        return (int)TY_BYTES;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Symbols and prelude                                                 */
/* ------------------------------------------------------------------ */

static Symbol *symbol_new(Resolver *r, SymbolKind kind,
                          const char *name, size_t name_len,
                          Span span, Module *module)
{
    Symbol *s;
    s = (Symbol *)resolver_alloc(r, sizeof(Symbol));
    s->kind = kind;
    s->name = copy_name(r, name, name_len);
    s->name_len = name_len;
    s->span = span;
    s->module = module;
    return s;
}

static void prelude_add(Resolver *r, Symbol *s)
{
    s->next = r->prelude;
    r->prelude = s;
}

static TypeVariant *append_variant(Resolver *r, TypeInfo *info,
                                   const char *name, size_t name_len,
                                   TypeList *payload)
{
    TypeVariant *v;
    TypeVariant *tail;

    v = type_variant_new(r->arena, name, name_len, payload);
    if (v == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }

    if (info->variants == NULL) {
        info->variants = v;
    } else {
        tail = info->variants;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = v;
    }
    return v;
}

static void add_prelude_variant(Resolver *r, TypeInfo *info,
                                const char *name, TypeList *payload)
{
    TypeVariant *v;
    Symbol *s;
    Span span;

    v = append_variant(r, info, name, strlen(name), payload);
    span = span_at_file("<prelude>", 1, 1);
    s = symbol_new(r, SYM_VARIANT, v->name, v->name_len, span, NULL);
    s->as.variant.owner = info;
    s->as.variant.ast = NULL;
    s->as.variant.variant = v;
    prelude_add(r, s);
}

static void create_prelude(Resolver *r)
{
    TypeInfo *info;
    Symbol *error_symbol;
    Span span;

    info = type_info_new(r->arena, TY_ENUM, r->next_type_id,
                         "Error", 5);
    if (info == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    r->next_type_id += 1;
    r->error_info = info;

    add_prelude_variant(r, info, "NotFound", NULL);
    add_prelude_variant(r, info, "PermissionDenied", NULL);
    add_prelude_variant(r, info, "InvalidUtf8", NULL);
    add_prelude_variant(r, info, "Io",
                        type_list_cons(r->arena, r->ty_string, NULL));

    span = span_at_file("<prelude>", 1, 1);
    error_symbol = symbol_new(r, SYM_ENUM, "Error", 5, span, NULL);
    error_symbol->as.nominal.decl = NULL;
    error_symbol->as.nominal.info = info;
    prelude_add(r, error_symbol);
}

/* ------------------------------------------------------------------ */
/* Modules and imports                                                 */
/* ------------------------------------------------------------------ */

static Module *module_new(Resolver *r, const char *path, int is_root)
{
    Module *m;
    m = (Module *)resolver_alloc(r, sizeof(Module));
    m->path = copy_name(r, path, strlen(path));
    m->is_root = is_root;
    m->loading = 1;
    if (r->modules_tail != NULL) {
        r->modules_tail->next = m;
    } else {
        r->modules = m;
    }
    r->modules_tail = m;
    return m;
}

static Module *module_find(Resolver *r, const char *path)
{
    Module *m;
    for (m = r->modules; m != NULL; m = m->next) {
        if (strcmp(m->path, path) == 0) {
            return m;
        }
    }
    return NULL;
}

static void module_add_import(Resolver *r, Module *m, Module *child,
                              Span span)
{
    ImportLink *link;
    ImportLink *it;

    for (it = m->imports; it != NULL; it = it->next) {
        if (it->module == child) {
            return;
        }
    }

    link = (ImportLink *)resolver_alloc(r, sizeof(ImportLink));
    link->module = child;
    link->span = span;

    if (m->imports_tail != NULL) {
        m->imports_tail->next = link;
    } else {
        m->imports = link;
    }
    m->imports_tail = link;
}

static int module_add_symbol(Resolver *r, Module *m, Symbol *s)
{
    Symbol *it;

    if (is_reserved_name(s->name, s->name_len)) {
        resolver_error(r, s->span, "'%s' is a reserved name", s->name);
        return 0;
    }

    for (it = m->symbols; it != NULL; it = it->next) {
        if (it->name_len == s->name_len &&
            memcmp(it->name, s->name, s->name_len) == 0) {
            resolver_error(r, s->span,
                           "duplicate top-level name '%s'", s->name);
            return 0;
        }
    }

    if (m->symbols_tail != NULL) {
        m->symbols_tail->next = s;
    } else {
        m->symbols = s;
    }
    m->symbols_tail = s;
    return 1;
}

static int visibility_add(Resolver *r, Module *m, Symbol *sym, Span span)
{
    Visibility *it;
    Visibility *v;

    for (it = m->visible; it != NULL; it = it->next) {
        if (it->symbol->name_len == sym->name_len &&
            memcmp(it->symbol->name, sym->name, sym->name_len) == 0) {
            resolver_error(r, span,
                           "top-level name '%s' collides with another declaration",
                           sym->name);
            return 0;
        }
    }

    v = (Visibility *)resolver_alloc(r, sizeof(Visibility));
    v->symbol = sym;
    v->span = span;

    if (m->visible_tail != NULL) {
        m->visible_tail->next = v;
    } else {
        m->visible = v;
    }
    m->visible_tail = v;
    return 1;
}

static const char *resolver_read_source(Resolver *r, const char *path,
                                         size_t *out_len, Span err_span)
{
    FILE *f;
    char *buf;
    char *out;
    size_t cap;
    size_t len;
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        resolver_error(r, err_span, "cannot open '%s'", path);
        return NULL;
    }

    cap = 4096;
    len = 0;
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        fclose(f);
        resolver_error(r, err_span, "out of memory");
        return NULL;
    }

    for (;;) {
        if (len + 1 >= cap) {
            size_t new_cap;
            char *new_buf;

            new_cap = cap * 2;
            new_buf = (char *)realloc(buf, new_cap);
            if (new_buf == NULL) {
                free(buf);
                fclose(f);
                resolver_error(r, err_span, "out of memory");
                return NULL;
            }
            buf = new_buf;
            cap = new_cap;
        }

        n = fread(buf + len, 1, cap - len - 1, f);
        len += n;
        if (n == 0) {
            break;
        }
    }

    if (ferror(f)) {
        free(buf);
        fclose(f);
        resolver_error(r, err_span, "cannot read '%s'", path);
        return NULL;
    }

    fclose(f);

    out = (char *)resolver_alloc(r, len + 1);
    if (len != 0) {
        memcpy(out, buf, len);
    }
    out[len] = '\0';
    free(buf);

    *out_len = len;
    return out;
}

static Module *load_module(Resolver *r, const char *path, Span span,
                           int is_root);

static int parse_module(Resolver *r, Module *m)
{
    Lexer lx;
    Parser parser;
    AstProgram *program;

    lexer_init(&lx, m->path, (const unsigned char *)m->source,
               m->source_len, r->arena);
    parser_init(&parser, &lx, r->arena);
    program = parser_parse_program(&parser);

    if (parser_had_error(&parser)) {
        resolver_error(r, parser_error_span(&parser), "%s",
                       parser_error_message(&parser));
        return 0;
    }

    m->ast = program;
    return 1;
}

static Module *load_module(Resolver *r, const char *path, Span span,
                           int is_root)
{
    Module *m;
    Module *existing;
    AstImport *import;

    existing = module_find(r, path);
    if (existing != NULL) {
        if (existing->loading) {
            resolver_error(r, span, "import cycle involving '%s'", path);
            return NULL;
        }
        if (existing->failed) {
            return NULL;
        }
        return existing;
    }

    m = module_new(r, path, is_root);
    m->source = resolver_read_source(r, path, &m->source_len, span);
    if (m->source == NULL) {
        m->failed = 1;
        m->loading = 0;
        return NULL;
    }

    if (!parse_module(r, m)) {
        m->failed = 1;
        m->loading = 0;
        return NULL;
    }

    for (import = m->ast->imports; import != NULL; import = import->next) {
        const char *rel;
        const char *canonical;
        Module *child;

        rel = copy_name(r, import->path, import->path_len);
        canonical = path_join(r, m->path, rel);
        child = load_module(r, canonical, import->span, 0);
        if (child != NULL) {
            module_add_import(r, m, child, import->span);
        }
    }

    m->loading = 0;
    m->loaded = 1;
    return m;
}

/* ------------------------------------------------------------------ */
/* Symbol collection and visibility                                    */
/* ------------------------------------------------------------------ */

static const char *decl_name(AstDecl *decl, size_t *name_len)
{
    switch (decl->kind) {
    case DECL_TYPE_ALIAS:
        *name_len = decl->as.alias.name_len;
        return decl->as.alias.name;
    case DECL_STRUCT:
        *name_len = decl->as.struct_.name_len;
        return decl->as.struct_.name;
    case DECL_ENUM:
        *name_len = decl->as.enum_.name_len;
        return decl->as.enum_.name;
    case DECL_FN:
        *name_len = decl->as.fn.name_len;
        return decl->as.fn.name;
    }
    *name_len = 0;
    return NULL;
}

static int collect_symbols(Resolver *r, Module *m)
{
    AstDecl *decl;
    AstVariant *variant;

    for (decl = m->ast->decls; decl != NULL; decl = decl->next) {
        const char *name;
        size_t name_len;
        Symbol *s;

        name = decl_name(decl, &name_len);
        if (name == NULL) {
            continue;
        }

        switch (decl->kind) {
        case DECL_TYPE_ALIAS:
            s = symbol_new(r, SYM_ALIAS, name, name_len, decl->span, m);
            s->as.alias.decl = decl;
            s->as.alias.target = NULL;
            s->as.alias.state = ALIAS_UNRESOLVED;
            module_add_symbol(r, m, s);
            break;

        case DECL_STRUCT: {
            TypeInfo *info;
            info = type_info_new(r->arena, TY_STRUCT, r->next_type_id,
                                 name, name_len);
            if (info == NULL) {
                fprintf(stderr, "idem: out of memory\n");
                exit(1);
            }
            r->next_type_id += 1;
            s = symbol_new(r, SYM_STRUCT, name, name_len, decl->span, m);
            s->as.nominal.decl = decl;
            s->as.nominal.info = info;
            module_add_symbol(r, m, s);
            break;
        }

        case DECL_ENUM: {
            TypeInfo *info;
            info = type_info_new(r->arena, TY_ENUM, r->next_type_id,
                                 name, name_len);
            if (info == NULL) {
                fprintf(stderr, "idem: out of memory\n");
                exit(1);
            }
            r->next_type_id += 1;
            s = symbol_new(r, SYM_ENUM, name, name_len, decl->span, m);
            s->as.nominal.decl = decl;
            s->as.nominal.info = info;
            module_add_symbol(r, m, s);

            for (variant = decl->as.enum_.variants; variant != NULL;
                 variant = variant->next) {
                Symbol *vs;
                vs = symbol_new(r, SYM_VARIANT, variant->name,
                                variant->name_len, variant->span, m);
                vs->as.variant.owner = info;
                vs->as.variant.ast = variant;
                vs->as.variant.variant = NULL;
                module_add_symbol(r, m, vs);
            }
            break;
        }

        case DECL_FN:
            s = symbol_new(r, SYM_FN, name, name_len, decl->span, m);
            s->as.fn.decl = decl;
            s->as.fn.type = NULL;
            module_add_symbol(r, m, s);
            break;
        }
    }

    return 1;
}

static int build_visible(Resolver *r, Module *m)
{
    Symbol *sym;
    ImportLink *import;

    for (sym = m->symbols; sym != NULL; sym = sym->next) {
        if (!visibility_add(r, m, sym, sym->span)) {
            return 0;
        }
    }

    for (import = m->imports; import != NULL; import = import->next) {
        for (sym = import->module->symbols; sym != NULL; sym = sym->next) {
            if (!visibility_add(r, m, sym, import->span)) {
                return 0;
            }
        }
    }

    return 1;
}

Symbol *resolver_lookup(Resolver *r, Module *module,
                        const char *name, size_t name_len)
{
    Visibility *v;
    Symbol *s;

    if (module != NULL) {
        for (v = module->visible; v != NULL; v = v->next) {
            if (v->symbol->name_len == name_len &&
                memcmp(v->symbol->name, name, name_len) == 0) {
                return v->symbol;
            }
        }
    }

    for (s = r->prelude; s != NULL; s = s->next) {
        if (s->name_len == name_len &&
            memcmp(s->name, name, name_len) == 0) {
            return s;
        }
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Type resolution and aliases                                         */
/* ------------------------------------------------------------------ */

static Type *resolve_type(Resolver *r, Module *m, AstType *type);
static Type *resolve_alias_symbol(Resolver *r, Symbol *sym, Span use_span);

static TypeList *resolve_type_list(Resolver *r, Module *m, AstTypeList *list)
{
    TypeList *head;
    TypeList *tail;

    head = NULL;
    tail = NULL;

    while (list != NULL) {
        Type *t;
        TypeList *n;

        t = resolve_type(r, m, list->type);
        n = type_list_cons(r->arena, t, NULL);
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
        list = list->next;
    }

    return head;
}

static Type *type_for_primitive(Resolver *r, int kind)
{
    switch ((TypeKind)kind) {
    case TY_ANY:    return r->ty_any;
    case TY_NEVER:  return r->ty_never;
    case TY_UNIT:   return r->ty_unit;
    case TY_BOOL:   return r->ty_bool;
    case TY_INT:    return r->ty_int;
    case TY_FLOAT:  return r->ty_float;
    case TY_CHAR:   return r->ty_char;
    case TY_STRING: return r->ty_string;
    case TY_BYTES:  return r->ty_bytes;
    default:        return r->ty_error;
    }
}

static Type *resolve_type(Resolver *r, Module *m, AstType *type)
{
    if (type == NULL) {
        return r->ty_error;
    }

    switch (type->kind) {
    case TYPE_UNIT:
        return r->ty_unit;

    case TYPE_ERROR:
        return r->ty_error;

    case TYPE_NAMED: {
        const char *name;
        size_t name_len;
        int pk;
        Symbol *sym;

        name = type->as.named.name;
        name_len = type->as.named.name_len;

        pk = primitive_kind(name, name_len);
        if (pk >= 0) {
            if (type->as.named.args != NULL) {
                resolver_error(r, type->span,
                               "type '%.*s' does not take type arguments",
                               (int)name_len, name);
                return r->ty_error;
            }
            return type_for_primitive(r, pk);
        }

        if (name_eq(name, name_len, "Array") ||
            name_eq(name, name_len, "List") ||
            name_eq(name, name_len, "Map")) {
            AstTypeList *args;
            args = type->as.named.args;

            if (name_eq(name, name_len, "Array")) {
                Type *element;
                if (args == NULL || args->next != NULL) {
                    resolver_error(r, type->span,
                                   "type 'Array' expects one type argument");
                    return r->ty_error;
                }
                element = resolve_type(r, m, args->type);
                return type_array(r->arena, element);
            }

            if (name_eq(name, name_len, "List")) {
                Type *element;
                if (args == NULL || args->next != NULL) {
                    resolver_error(r, type->span,
                                   "type 'List' expects one type argument");
                    return r->ty_error;
                }
                element = resolve_type(r, m, args->type);
                return type_list_of(r->arena, element);
            }

            if (args == NULL || args->next == NULL ||
                args->next->next != NULL) {
                resolver_error(r, type->span,
                               "type 'Map' expects two type arguments");
                return r->ty_error;
            }
            return type_map(r->arena,
                            resolve_type(r, m, args->type),
                            resolve_type(r, m, args->next->type));
        }

        if (name_eq(name, name_len, "Error")) {
            if (type->as.named.args != NULL) {
                resolver_error(r, type->span,
                               "type 'Error' does not take type arguments");
                return r->ty_error;
            }
            return r->error_info->type;
        }

        sym = resolver_lookup(r, m, name, name_len);
        if (sym == NULL) {
            resolver_error(r, type->span, "unknown type '%.*s'",
                           (int)name_len, name);
            return r->ty_error;
        }

        if (type->as.named.args != NULL) {
            resolver_error(r, type->span,
                           "type '%.*s' does not take type arguments",
                           (int)name_len, name);
            return r->ty_error;
        }

        switch (sym->kind) {
        case SYM_ALIAS:
            return resolve_alias_symbol(r, sym, type->span);
        case SYM_STRUCT:
        case SYM_ENUM:
            return sym->as.nominal.info->type;
        default:
            resolver_error(r, type->span, "'%.*s' is not a type",
                           (int)name_len, name);
            return r->ty_error;
        }
    }

    case TYPE_TUPLE:
        return type_tuple(r->arena,
                          resolve_type_list(r, m, type->as.tuple.items));

    case TYPE_FUNCTION:
        return type_function(r->arena,
                             resolve_type_list(r, m, type->as.fn.params),
                             resolve_type(r, m, type->as.fn.result));

    case TYPE_UNION:
        return type_union_normalized(r->arena,
                                     resolve_type_list(r, m,
                                                       type->as.union_.items));
    }

    return r->ty_error;
}

static Type *resolve_alias_symbol(Resolver *r, Symbol *sym, Span use_span)
{
    Type *target;

    if (sym == NULL || sym->kind != SYM_ALIAS) {
        return r->ty_error;
    }
    if (sym->as.alias.state == ALIAS_RESOLVED) {
        return sym->as.alias.target;
    }
    if (sym->as.alias.state == ALIAS_RESOLVING) {
        resolver_error(r, use_span, "type alias cycle involving '%s'",
                       sym->name);
        return r->ty_error;
    }

    sym->as.alias.state = ALIAS_RESOLVING;
    target = resolve_type(r, sym->module, sym->as.alias.decl->as.alias.type);
    sym->as.alias.target = target;
    sym->as.alias.state = ALIAS_RESOLVED;
    return target;
}

static void resolve_all_aliases(Resolver *r)
{
    Module *m;
    Symbol *s;

    for (m = r->modules; m != NULL; m = m->next) {
        if (m->failed) {
            continue;
        }
        for (s = m->symbols; s != NULL; s = s->next) {
            if (s->kind == SYM_ALIAS) {
                resolve_alias_symbol(r, s, s->span);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Declaration resolution                                              */
/* ------------------------------------------------------------------ */

static void resolve_struct_symbol(Resolver *r, Module *m, Symbol *sym)
{
    AstDecl *decl;
    AstField *field;
    TypeField *head;
    TypeField *tail;
    TypeInfo *info;

    decl = sym->as.nominal.decl;
    info = sym->as.nominal.info;
    head = NULL;
    tail = NULL;

    for (field = decl->as.struct_.fields; field != NULL;
         field = field->next) {
        AstField *prev;
        TypeField *tf;

        for (prev = decl->as.struct_.fields; prev != field;
             prev = prev->next) {
            if (prev->name_len == field->name_len &&
                memcmp(prev->name, field->name, field->name_len) == 0) {
                resolver_error(r, field->span,
                               "duplicate field '%.*s' in struct '%s'",
                               (int)field->name_len, field->name,
                               sym->name);
                break;
            }
        }

        tf = type_field_new(r->arena, field->name, field->name_len,
                            resolve_type(r, m, field->type));
        if (tf == NULL) {
            fprintf(stderr, "idem: out of memory\n");
            exit(1);
        }
        if (tail != NULL) {
            tail->next = tf;
        } else {
            head = tf;
        }
        tail = tf;
    }

    info->fields = head;
}

static void resolve_enum_symbol(Resolver *r, Module *m, Symbol *sym)
{
    Symbol *s;
    TypeInfo *info;

    info = sym->as.nominal.info;
    info->variants = NULL;

    for (s = m->symbols; s != NULL; s = s->next) {
        if (s->kind == SYM_VARIANT && s->as.variant.owner == info) {
            AstVariant *variant;
            TypeList *payload;
            TypeVariant *v;

            variant = s->as.variant.ast;
            payload = resolve_type_list(r, m,
                                        variant != NULL ? variant->payload
                                                        : NULL);
            v = append_variant(r, info, s->name, s->name_len, payload);
            s->as.variant.variant = v;
        }
    }
}

static void resolve_fn_symbol(Resolver *r, Module *m, Symbol *sym)
{
    AstDecl *decl;
    AstParam *param;
    TypeList *head;
    TypeList *tail;
    Type *result;

    decl = sym->as.fn.decl;
    head = NULL;
    tail = NULL;

    for (param = decl->as.fn.params; param != NULL; param = param->next) {
        AstParam *prev;
        TypeList *n;

        for (prev = decl->as.fn.params; prev != param; prev = prev->next) {
            if (prev->name_len == param->name_len &&
                memcmp(prev->name, param->name, param->name_len) == 0) {
                resolver_error(r, param->span,
                               "duplicate parameter '%.*s' in function '%s'",
                               (int)param->name_len, param->name,
                               sym->name);
                break;
            }
        }

        if (is_reserved_name(param->name, param->name_len)) {
            resolver_error(r, param->span,
                           "parameter '%.*s' shadows a reserved name",
                           (int)param->name_len, param->name);
        } else {
            Symbol *shadow;
            shadow = resolver_lookup(r, m, param->name, param->name_len);
            if (shadow != NULL &&
                (shadow->kind == SYM_ALIAS ||
                 shadow->kind == SYM_STRUCT ||
                 shadow->kind == SYM_ENUM)) {
                resolver_error(r, param->span,
                               "parameter '%.*s' shadows a type name",
                               (int)param->name_len, param->name);
            }
        }

        n = type_list_cons(r->arena,
                           resolve_type(r, m, param->type), NULL);
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
    }

    result = resolve_type(r, m, decl->as.fn.result);
    sym->as.fn.type = type_function(r->arena, head, result);
}

static void resolve_declarations(Resolver *r)
{
    Module *m;
    Symbol *s;

    for (m = r->modules; m != NULL; m = m->next) {
        if (m->failed) {
            continue;
        }
        for (s = m->symbols; s != NULL; s = s->next) {
            switch (s->kind) {
            case SYM_STRUCT:
                resolve_struct_symbol(r, m, s);
                break;
            case SYM_ENUM:
                resolve_enum_symbol(r, m, s);
                break;
            case SYM_FN:
                resolve_fn_symbol(r, m, s);
                break;
            default:
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static int is_array_of_string(Type *type)
{
    return type != NULL &&
           type->kind == TY_ARRAY &&
           type->as.array.element != NULL &&
           type->as.array.element->kind == TY_STRING;
}

static void check_main(Resolver *r)
{
    Module *m;
    Symbol *s;
    Symbol *root_main;
    Type *fn_type;
    TypeList *params;

    root_main = NULL;

    for (m = r->modules; m != NULL; m = m->next) {
        if (m->failed) {
            continue;
        }
        for (s = m->symbols; s != NULL; s = s->next) {
            if (s->name_len == 4 && memcmp(s->name, "main", 4) == 0) {
                if (m != r->root) {
                    resolver_error(r, s->span,
                                   "'main' may only be declared in the root module");
                    return;
                }
                root_main = s;
            }
        }
    }

    if (root_main == NULL) {
        Span span;
        span = span_at_file(r->root->path, 1, 1);
        resolver_error(r, span, "root module must declare 'main'");
        return;
    }

    if (root_main->kind != SYM_FN) {
        resolver_error(r, root_main->span, "'main' must be a function");
        return;
    }

    fn_type = root_main->as.fn.type;
    if (fn_type == NULL || fn_type->kind != TY_FUNCTION) {
        resolver_error(r, root_main->span, "'main' has an invalid type");
        return;
    }

    params = fn_type->as.fn.params;
    if (params == NULL || params->next != NULL ||
        !is_array_of_string(params->type) ||
        fn_type->as.fn.result == NULL ||
        fn_type->as.fn.result->kind != TY_UNIT) {
        resolver_error(r, root_main->span,
                       "'main' must have type (Array[String]) -> Unit");
    }
}

void resolver_init(Resolver *r, Arena *arena)
{
    memset(r, 0, sizeof(Resolver));
    r->arena = arena;
    r->next_type_id = 1;

    r->ty_any = type_primitive(arena, TY_ANY);
    r->ty_never = type_primitive(arena, TY_NEVER);
    r->ty_unit = type_primitive(arena, TY_UNIT);
    r->ty_bool = type_primitive(arena, TY_BOOL);
    r->ty_int = type_primitive(arena, TY_INT);
    r->ty_float = type_primitive(arena, TY_FLOAT);
    r->ty_char = type_primitive(arena, TY_CHAR);
    r->ty_string = type_primitive(arena, TY_STRING);
    r->ty_bytes = type_primitive(arena, TY_BYTES);
    r->ty_error = type_primitive(arena, TY_ERROR);

    create_prelude(r);
}

int resolver_resolve(Resolver *r, const char *root_path)
{
    const char *path;
    Span span;
    Module *m;

    if (root_path == NULL) {
        resolver_error(r, span_at_file("<unknown>", 1, 1),
                       "no root module");
        return 0;
    }

    path = path_normalize(r, root_path);
    span = span_at_file(path, 1, 1);
    r->root = load_module(r, path, span, 1);

    if (r->root == NULL || r->had_error) {
        return 0;
    }

    for (m = r->modules; m != NULL; m = m->next) {
        if (!m->failed) {
            collect_symbols(r, m);
        }
    }
    if (r->had_error) {
        return 0;
    }

    for (m = r->modules; m != NULL; m = m->next) {
        if (!m->failed) {
            if (!build_visible(r, m)) {
                return 0;
            }
        }
    }
    if (r->had_error) {
        return 0;
    }

    resolve_all_aliases(r);
    if (r->had_error) {
        return 0;
    }

    resolve_declarations(r);
    if (r->had_error) {
        return 0;
    }

    check_main(r);
    return r->had_error ? 0 : 1;
}

Module *resolver_root(Resolver *r)
{
    return r->root;
}

Type *resolver_resolve_type(Resolver *r, Module *module, AstType *type)
{
    return resolve_type(r, module, type);
}

int resolver_had_error(const Resolver *r)
{
    return r->had_error;
}

Span resolver_error_span(const Resolver *r)
{
    return r->error_span;
}

const char *resolver_error_message(const Resolver *r)
{
    return r->error_message;
}
