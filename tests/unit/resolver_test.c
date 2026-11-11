#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "resolver.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n",                         \
                    __FILE__, __LINE__, #cond);                         \
            failures += 1;                                              \
        }                                                               \
    } while (0)

static int write_file(const char *path, const char *text)
{
    FILE *f;

    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    if (fwrite(text, 1, strlen(text), f) != strlen(text)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static void report_error(Resolver *r)
{
    if (resolver_had_error(r)) {
        Span span;
        span = resolver_error_span(r);
        fprintf(stderr, "resolver error at %s:%lu:%lu: %s\n",
                span.file != NULL ? span.file : "<unknown>",
                (unsigned long)span.line,
                (unsigned long)span.column,
                resolver_error_message(r));
    }
}

static void test_ok_program(void)
{
    Arena arena;
    Resolver resolver;
    Module *root;
    Symbol *sym;
    TypeInfo *info;
    TypeField *field;
    TypeVariant *variant;
    TypeList *payload;
    Type *type;
    int ok;

    CHECK(write_file("build/_resolver_ok_lib.idem",
                     "type Number = Int | Float\n"
                     "struct Point { x: Int, y: Number }\n"
                     "enum Maybe { Nothing, Just(Int) }\n"
                     "fn helper(n: Int) -> Number = n\n"));
    CHECK(write_file("build/_resolver_ok_root.idem",
                     "import \"_resolver_ok_lib.idem\"\n"
                     "import \"./_resolver_ok_lib.idem\"\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"
                     "fn use(p: Point) -> Int = p.x\n"));

    arena_init(&arena);
    resolver_init(&resolver, &arena);
    ok = resolver_resolve(&resolver, "build/_resolver_ok_root.idem");
    CHECK(ok);
    CHECK(!resolver_had_error(&resolver));
    if (!ok) {
        report_error(&resolver);
        arena_free(&arena);
        remove("build/_resolver_ok_lib.idem");
        remove("build/_resolver_ok_root.idem");
        return;
    }

    root = resolver_root(&resolver);
    CHECK(root != NULL);

    sym = resolver_lookup(&resolver, root, "Point", 5);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_STRUCT);
    info = sym->as.nominal.info;
    CHECK(info != NULL);
    CHECK(info->kind == TY_STRUCT);
    CHECK(info->fields != NULL);
    field = info->fields;
    CHECK(field->name_len == 1 && field->name[0] == 'x');
    CHECK(field->type->kind == TY_INT);
    field = field->next;
    CHECK(field != NULL);
    CHECK(field->name_len == 1 && field->name[0] == 'y');
    CHECK(field->type->kind == TY_UNION);

    sym = resolver_lookup(&resolver, root, "Number", 6);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_ALIAS);
    CHECK(sym->as.alias.target != NULL);
    CHECK(sym->as.alias.target->kind == TY_UNION);

    sym = resolver_lookup(&resolver, root, "Maybe", 5);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_ENUM);
    info = sym->as.nominal.info;
    CHECK(info != NULL);
    CHECK(info->variants != NULL);
    CHECK(info->variants->payload == NULL);
    CHECK(info->variants->next != NULL);

    sym = resolver_lookup(&resolver, root, "Just", 4);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_VARIANT);
    variant = sym->as.variant.variant;
    CHECK(variant != NULL);
    payload = variant->payload;
    CHECK(payload != NULL);
    CHECK(payload->type->kind == TY_INT);
    CHECK(payload->next == NULL);

    sym = resolver_lookup(&resolver, root, "helper", 6);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_FN);
    type = sym->as.fn.type;
    CHECK(type != NULL);
    CHECK(type->kind == TY_FUNCTION);
    CHECK(type->as.fn.params != NULL);
    CHECK(type->as.fn.params->type->kind == TY_INT);
    CHECK(type->as.fn.result->kind == TY_UNION);

    sym = resolver_lookup(&resolver, root, "main", 4);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_FN);
    type = sym->as.fn.type;
    CHECK(type != NULL);
    CHECK(type->kind == TY_FUNCTION);
    CHECK(type->as.fn.params != NULL);
    CHECK(type->as.fn.params->next == NULL);
    CHECK(type->as.fn.params->type->kind == TY_ARRAY);
    CHECK(type->as.fn.params->type->as.array.element->kind == TY_STRING);
    CHECK(type->as.fn.result->kind == TY_UNIT);

    sym = resolver_lookup(&resolver, root, "Error", 5);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_ENUM);
    sym = resolver_lookup(&resolver, root, "NotFound", 8);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_VARIANT);
    CHECK(sym->as.variant.variant->payload == NULL);

    sym = resolver_lookup(&resolver, root, "use", 3);
    CHECK(sym != NULL);
    CHECK(sym->kind == SYM_FN);
    type = resolver_resolve_type(&resolver, root,
                                 sym->as.fn.decl->as.fn.params->type);
    CHECK(type != NULL);
    CHECK(type->kind == TY_STRUCT);

    arena_free(&arena);
    remove("build/_resolver_ok_lib.idem");
    remove("build/_resolver_ok_root.idem");
}

static void expect_error(const char *root, const char *needle)
{
    Arena arena;
    Resolver resolver;
    int ok;

    arena_init(&arena);
    resolver_init(&resolver, &arena);
    ok = resolver_resolve(&resolver, root);
    CHECK(!ok);
    CHECK(resolver_had_error(&resolver));
    if (resolver_had_error(&resolver)) {
        if (strstr(resolver_error_message(&resolver), needle) == NULL) {
            CHECK(strstr(resolver_error_message(&resolver), needle) != NULL);
            report_error(&resolver);
        }
    }
    arena_free(&arena);
}

static void test_errors(void)
{
    CHECK(write_file("build/_resolver_alias_cycle.idem",
                     "type A = B\n"
                     "type B = A\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_alias_cycle.idem", "alias cycle");

    CHECK(write_file("build/_resolver_cycle_a.idem",
                     "import \"_resolver_cycle_b.idem\"\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    CHECK(write_file("build/_resolver_cycle_b.idem",
                     "import \"_resolver_cycle_a.idem\"\n"
                     "fn b() -> Int = 1\n"));
    expect_error("build/_resolver_cycle_a.idem", "import cycle");

    CHECK(write_file("build/_resolver_missing.idem",
                     "import \"_resolver_missing_dep.idem\"\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_missing.idem", "cannot open");

    CHECK(write_file("build/_resolver_reserved.idem",
                     "fn print() -> Int = 1\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_reserved.idem", "reserved");

    CHECK(write_file("build/_resolver_duplicate.idem",
                     "fn f() -> Int = 1\n"
                     "fn f() -> Int = 2\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_duplicate.idem", "duplicate top-level");

    CHECK(write_file("build/_resolver_clash_a.idem",
                     "fn clash() -> Int = 1\n"));
    CHECK(write_file("build/_resolver_clash_b.idem",
                     "fn clash() -> Int = 2\n"));
    CHECK(write_file("build/_resolver_clash_root.idem",
                     "import \"_resolver_clash_a.idem\"\n"
                     "import \"_resolver_clash_b.idem\"\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_clash_root.idem", "collides");

    CHECK(write_file("build/_resolver_no_main.idem",
                     "fn f() -> Int = 1\n"));
    expect_error("build/_resolver_no_main.idem", "must declare 'main'");

    CHECK(write_file("build/_resolver_bad_main.idem",
                     "fn main() -> Int = 1\n"));
    expect_error("build/_resolver_bad_main.idem", "must have type");

    CHECK(write_file("build/_resolver_imported_main_lib.idem",
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    CHECK(write_file("build/_resolver_imported_main.idem",
                     "import \"_resolver_imported_main_lib.idem\"\n"
                     "fn f() -> Int = 1\n"));
    expect_error("build/_resolver_imported_main.idem",
                 "may only be declared");

    CHECK(write_file("build/_resolver_unknown.idem",
                     "struct P { x: Nope }\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_unknown.idem", "unknown type");

    CHECK(write_file("build/_resolver_dup_field.idem",
                     "struct P { x: Int, x: Int }\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_dup_field.idem", "duplicate field");

    CHECK(write_file("build/_resolver_dup_param.idem",
                     "fn f(x: Int, x: Int) -> Int = 1\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_dup_param.idem", "duplicate parameter");

    CHECK(write_file("build/_resolver_param_shadow.idem",
                     "struct P { x: Int }\n"
                     "fn f(P: Int) -> Int = 1\n"
                     "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_resolver_param_shadow.idem",
                 "shadows a type name");

    remove("build/_resolver_alias_cycle.idem");
    remove("build/_resolver_cycle_a.idem");
    remove("build/_resolver_cycle_b.idem");
    remove("build/_resolver_missing.idem");
    remove("build/_resolver_reserved.idem");
    remove("build/_resolver_duplicate.idem");
    remove("build/_resolver_clash_a.idem");
    remove("build/_resolver_clash_b.idem");
    remove("build/_resolver_clash_root.idem");
    remove("build/_resolver_no_main.idem");
    remove("build/_resolver_bad_main.idem");
    remove("build/_resolver_imported_main_lib.idem");
    remove("build/_resolver_imported_main.idem");
    remove("build/_resolver_unknown.idem");
    remove("build/_resolver_dup_field.idem");
    remove("build/_resolver_dup_param.idem");
    remove("build/_resolver_param_shadow.idem");
}

int resolver_test_run(void)
{
    test_ok_program();
    test_errors();

    if (failures != 0) {
        fprintf(stderr, "%d resolver test failure(s)\n", failures);
        return failures;
    }

    printf("resolver tests passed\n");
    return 0;
}
