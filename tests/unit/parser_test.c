#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n",                         \
                    __FILE__, __LINE__, #cond);                         \
            failures += 1;                                              \
        }                                                               \
    } while (0)

static int name_eq(const char *s, size_t n, const char *lit)
{
    size_t m;
    m = strlen(lit);
    return n == m && memcmp(s, lit, n) == 0;
}

static AstProgram *parse_text(const char *src, Parser *p, Arena *a,
                              Lexer *lx)
{
    arena_init(a);
    lexer_init(lx, "<test>", (const unsigned char *)src, strlen(src), a);
    parser_init(p, lx, a);
    return parser_parse_program(p);
}

static void test_declarations(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstDecl *decl;
    AstField *field;
    AstVariant *variant;

    src =
        "type Number = Int | Float\n"
        "struct Point { x: Int, y: Int }\n"
        "enum Shape { Circle(Float), Rectangle(Float, Float) }\n"
        "fn add(a: Int, b: Int) -> Int = a + b\n";

    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));

    decl = program->decls;
    CHECK(decl != NULL);
    CHECK(decl->kind == DECL_TYPE_ALIAS);
    CHECK(name_eq(decl->as.alias.name, decl->as.alias.name_len, "Number"));
    CHECK(decl->as.alias.type->kind == TYPE_UNION);
    CHECK(decl->as.alias.type->as.union_.items != NULL);
    CHECK(decl->as.alias.type->as.union_.items->type->kind == TYPE_NAMED);
    CHECK(name_eq(decl->as.alias.type->as.union_.items->type->as.named.name,
                  decl->as.alias.type->as.union_.items->type->as.named.name_len,
                  "Int"));
    CHECK(decl->as.alias.type->as.union_.items->next != NULL);

    decl = decl->next;
    CHECK(decl != NULL);
    CHECK(decl->kind == DECL_STRUCT);
    CHECK(name_eq(decl->as.struct_.name, decl->as.struct_.name_len, "Point"));
    field = decl->as.struct_.fields;
    CHECK(field != NULL);
    CHECK(name_eq(field->name, field->name_len, "x"));
    CHECK(field->next != NULL);
    CHECK(name_eq(field->next->name, field->next->name_len, "y"));

    decl = decl->next;
    CHECK(decl != NULL);
    CHECK(decl->kind == DECL_ENUM);
    CHECK(name_eq(decl->as.enum_.name, decl->as.enum_.name_len, "Shape"));
    variant = decl->as.enum_.variants;
    CHECK(variant != NULL);
    CHECK(name_eq(variant->name, variant->name_len, "Circle"));
    CHECK(variant->payload != NULL);
    CHECK(variant->payload->next == NULL);
    variant = variant->next;
    CHECK(variant != NULL);
    CHECK(name_eq(variant->name, variant->name_len, "Rectangle"));
    CHECK(variant->payload != NULL);
    CHECK(variant->payload->next != NULL);

    decl = decl->next;
    CHECK(decl != NULL);
    CHECK(decl->kind == DECL_FN);
    CHECK(name_eq(decl->as.fn.name, decl->as.fn.name_len, "add"));
    CHECK(decl->as.fn.params != NULL);
    CHECK(decl->as.fn.params->next != NULL);
    CHECK(decl->as.fn.result->kind == TYPE_NAMED);
    CHECK(decl->as.fn.body->kind == EXPR_BINARY);
    CHECK(decl->as.fn.body->as.binary.op == BIN_ADD);

    CHECK(decl->next == NULL);
    arena_free(&arena);
}

static void test_expression_precedence(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstExpr *body;
    AstBlockItem *item;
    AstExpr *value;

    src =
        "fn main(args: Array[String]) -> Unit = {\n"
        "  let x = 1 + 2 * 3\n"
        "  let y = 1 < 2 && 3 >= 4\n"
        "  let z = f(a, b)[0].field\n"
        "}\n";

    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));

    body = program->decls->as.fn.body;
    CHECK(body->kind == EXPR_BLOCK);
    item = body->as.block.items;
    CHECK(item != NULL);
    CHECK(item->kind == BLOCK_LET);
    value = item->as.let.value;
    CHECK(value->kind == EXPR_BINARY);
    CHECK(value->as.binary.op == BIN_ADD);
    CHECK(value->as.binary.right->kind == EXPR_BINARY);
    CHECK(value->as.binary.right->as.binary.op == BIN_MUL);

    item = item->next;
    CHECK(item != NULL);
    value = item->as.let.value;
    CHECK(value->kind == EXPR_BINARY);
    CHECK(value->as.binary.op == BIN_AND);
    CHECK(value->as.binary.left->as.binary.op == BIN_LT);
    CHECK(value->as.binary.right->as.binary.op == BIN_GE);

    item = item->next;
    CHECK(item != NULL);
    value = item->as.let.value;
    CHECK(value->kind == EXPR_FIELD);
    CHECK(value->as.field.object->kind == EXPR_INDEX);
    CHECK(value->as.field.object->as.index.object->kind == EXPR_CALL);
    CHECK(value->as.field.object->as.index.object->as.call.args != NULL);
    CHECK(value->as.field.object->as.index.object->as.call.args->next != NULL);
    CHECK(value->as.field.object->as.index.object->as.call.args->next->next == NULL);

    arena_free(&arena);
}

static void test_if_match_patterns(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstDecl *decl;
    AstExpr *body;
    AstMatchArm *arm;
    AstPattern *pat;

    src =
        "struct Point { x: Int, y: Int }\n"
        "enum Maybe { None, Some(Int) }\n"
        "fn f(p: Point, m: Maybe) -> Int = match (p, m) {\n"
        "  (Point { x, y }, Some(v)) => if x > y then x else y,\n"
        "  (_, None) => 0,\n"
        "}\n";

    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));

    decl = program->decls;
    CHECK(decl != NULL);
    decl = decl->next;
    CHECK(decl != NULL);
    decl = decl->next;
    CHECK(decl != NULL);
    CHECK(decl->kind == DECL_FN);

    body = decl->as.fn.body;
    CHECK(body->kind == EXPR_MATCH);
    CHECK(body->as.match.scrutinee->kind == EXPR_TUPLE);

    arm = body->as.match.arms;
    CHECK(arm != NULL);
    pat = arm->pattern;
    CHECK(pat->kind == PAT_TUPLE);
    CHECK(pat->as.tuple.items != NULL);
    CHECK(pat->as.tuple.items->pattern->kind == PAT_STRUCT);
    CHECK(pat->as.tuple.items->next != NULL);
    CHECK(pat->as.tuple.items->next->pattern->kind == PAT_ENUM);
    CHECK(arm->body->kind == EXPR_IF);

    arm = arm->next;
    CHECK(arm != NULL);
    pat = arm->pattern;
    CHECK(pat->kind == PAT_TUPLE);
    CHECK(pat->as.tuple.items->pattern->kind == PAT_WILDCARD);
    CHECK(pat->as.tuple.items->next->pattern->kind == PAT_VAR);
    CHECK(arm->body->kind == EXPR_INT);

    arena_free(&arena);
}

static void test_type_patterns(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstExpr *body;
    AstMatchArm *arm;

    src =
        "fn describe(n: Int | Float) -> String = match n {\n"
        "  i: Int => \"int\",\n"
        "  f: Float => \"float\",\n"
        "}\n";

    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));

    body = program->decls->as.fn.body;
    CHECK(body->kind == EXPR_MATCH);
    arm = body->as.match.arms;
    CHECK(arm != NULL);
    CHECK(arm->pattern->kind == PAT_TYPE);
    CHECK(name_eq(arm->pattern->as.type.name,
                  arm->pattern->as.type.name_len, "i"));
    CHECK(arm->pattern->as.type.type->kind == TYPE_NAMED);
    CHECK(arm->next != NULL);
    CHECK(arm->next->pattern->kind == PAT_TYPE);

    arena_free(&arena);
}

static void test_match_struct_scrutinee(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstExpr *body;

    src =
        "struct P { x: Int }\n"
        "fn f() -> Int = match (P { x: 1 }) { P { x } => x }\n";

    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));

    body = program->decls->next->as.fn.body;
    CHECK(body->kind == EXPR_MATCH);
    CHECK(body->as.match.scrutinee->kind == EXPR_STRUCT);
    CHECK(body->as.match.arms != NULL);
    CHECK(body->as.match.arms->pattern->kind == PAT_STRUCT);

    arena_free(&arena);

    src =
        "fn g(m: Map[String, Int]) -> Int = 1\n"
        "fn f() -> Int = match g(Map{ \"a\": 1 }) { _ => 0 }\n";
    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));
    arena_free(&arena);
}

static void test_collections(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstExpr *body;
    AstBlockItem *item;

    src =
        "fn main(args: Array[String]) -> Unit = {\n"
        "  let a = [1, 2, 3]\n"
        "  let l = List[1, 2]\n"
        "  let m = Map{ \"a\": 1, \"b\": 2 }\n"
        "  let s = \"hi\\n\"\n"
        "  let b = b\"A\\x42\"\n"
        "}\n";

    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));

    body = program->decls->as.fn.body;
    CHECK(body->kind == EXPR_BLOCK);
    item = body->as.block.items;
    CHECK(item != NULL);
    CHECK(item->as.let.value->kind == EXPR_ARRAY);
    CHECK(item->as.let.value->as.array.items != NULL);
    CHECK(item->as.let.value->as.array.items->next != NULL);

    item = item->next;
    CHECK(item != NULL);
    CHECK(item->as.let.value->kind == EXPR_LIST);
    CHECK(item->as.let.value->as.list.items != NULL);

    item = item->next;
    CHECK(item != NULL);
    CHECK(item->as.let.value->kind == EXPR_MAP);
    CHECK(item->as.let.value->as.map.entries != NULL);
    CHECK(item->as.let.value->as.map.entries->next != NULL);

    item = item->next;
    CHECK(item != NULL);
    CHECK(item->as.let.value->kind == EXPR_STRING);
    CHECK(item->as.let.value->as.str_lit.len == 3);

    item = item->next;
    CHECK(item != NULL);
    CHECK(item->as.let.value->kind == EXPR_BYTES);
    CHECK(item->as.let.value->as.str_lit.len == 2);

    arena_free(&arena);
}

static void test_imports(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstImport *import;

    src =
        "import \"math.idem\"\n"
        "import \"geometry/shapes.idem\"\n"
        "fn main(args: Array[String]) -> Unit = ()\n";

    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));

    import = program->imports;
    CHECK(import != NULL);
    CHECK(name_eq(import->path, import->path_len, "math.idem"));
    import = import->next;
    CHECK(import != NULL);
    CHECK(name_eq(import->path, import->path_len, "geometry/shapes.idem"));
    CHECK(import->next == NULL);

    arena_free(&arena);
}

static void test_precedence_and_errors(void)
{
    const char *src;
    Arena arena;
    Lexer lx;
    Parser parser;
    AstProgram *program;
    AstExpr *body;

    src =
        "fn f() -> Int = 1 + 2 * 3 - 4\n"
        "fn g() -> Bool = a || b && c\n";
    program = parse_text(src, &parser, &arena, &lx);
    CHECK(!parser_had_error(&parser));
    body = program->decls->as.fn.body;
    CHECK(body->kind == EXPR_BINARY);
    CHECK(body->as.binary.op == BIN_SUB);
    CHECK(body->as.binary.left->as.binary.op == BIN_ADD);
    CHECK(body->as.binary.left->as.binary.right->as.binary.op == BIN_MUL);

    body = program->decls->next->as.fn.body;
    CHECK(body->kind == EXPR_BINARY);
    CHECK(body->as.binary.op == BIN_OR);
    CHECK(body->as.binary.right->as.binary.op == BIN_AND);
    arena_free(&arena);

    src = "fn f() -> Bool = a < b < c\n";
    program = parse_text(src, &parser, &arena, &lx);
    CHECK(parser_had_error(&parser));
    arena_free(&arena);

    src = "fn f() -> Int = if true then 1\n";
    program = parse_text(src, &parser, &arena, &lx);
    CHECK(parser_had_error(&parser));
    arena_free(&arena);

    src = "fn f() -> Int = { let 0 = 1 }\n";
    program = parse_text(src, &parser, &arena, &lx);
    CHECK(parser_had_error(&parser));
    arena_free(&arena);

    src = "fn f() -> Int = { let x = 1 let y = 2 }\n";
    program = parse_text(src, &parser, &arena, &lx);
    CHECK(parser_had_error(&parser));
    arena_free(&arena);

    src = "fn f() -> Int = {\n";
    program = parse_text(src, &parser, &arena, &lx);
    CHECK(parser_had_error(&parser));
    arena_free(&arena);
}

static char *read_text_file(const char *path)
{
    FILE *f;
    long size;
    char *buf;
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static void test_examples(void)
{
    const char *paths[] = {
        "examples/hello.idem",
        "examples/factorial.idem",
        "examples/fibonacci.idem",
        "examples/shapes.idem",
        "examples/collections.idem",
        "examples/errors.idem"
    };
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        char *src;
        Arena arena;
        Lexer lx;
        Parser parser;
        AstProgram *program;

        src = read_text_file(paths[i]);
        if (src == NULL) {
            fprintf(stderr, "FAIL: cannot read %s\n", paths[i]);
            failures += 1;
            continue;
        }

        arena_init(&arena);
        lexer_init(&lx, paths[i], (const unsigned char *)src, strlen(src),
                   &arena);
        parser_init(&parser, &lx, &arena);
        program = parser_parse_program(&parser);
        CHECK(program != NULL);
        if (parser_had_error(&parser)) {
            Span span;
            span = parser_error_span(&parser);
            fprintf(stderr, "FAIL: %s:%lu:%lu: %s\n",
                    paths[i],
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    parser_error_message(&parser));
            failures += 1;
        }
        arena_free(&arena);
        free(src);
    }
}

int parser_test_run(void)
{
    test_declarations();
    test_expression_precedence();
    test_if_match_patterns();
    test_type_patterns();
    test_match_struct_scrutinee();
    test_collections();
    test_imports();
    test_precedence_and_errors();
    test_examples();

    if (failures != 0) {
        fprintf(stderr, "%d parser test failure(s)\n", failures);
        return failures;
    }

    printf("parser tests passed\n");
    return 0;
}
