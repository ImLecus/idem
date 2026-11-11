#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "check.h"
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

static void report_resolver(Resolver *r)
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

static void report_checker(TypeChecker *c)
{
    if (checker_had_error(c)) {
        Span span;
        span = checker_error_span(c);
        fprintf(stderr, "type error at %s:%lu:%lu: %s\n",
                span.file != NULL ? span.file : "<unknown>",
                (unsigned long)span.line,
                (unsigned long)span.column,
                checker_error_message(c));
    }
}

static void expect_ok(const char *root)
{
    Arena arena;
    Resolver resolver;
    TypeChecker checker;
    int ok;

    arena_init(&arena);
    resolver_init(&resolver, &arena);
    if (!resolver_resolve(&resolver, root)) {
        CHECK(0);
        report_resolver(&resolver);
        arena_free(&arena);
        return;
    }
    checker_init(&checker, &resolver, &arena);
    ok = checker_check_program(&checker);
    CHECK(ok);
    if (!ok) {
        report_checker(&checker);
    }
    arena_free(&arena);
}

static void expect_error(const char *root, const char *needle)
{
    Arena arena;
    Resolver resolver;
    TypeChecker checker;
    int ok;

    arena_init(&arena);
    resolver_init(&resolver, &arena);
    if (!resolver_resolve(&resolver, root)) {
        CHECK(0);
        report_resolver(&resolver);
        arena_free(&arena);
        return;
    }
    checker_init(&checker, &resolver, &arena);
    ok = checker_check_program(&checker);
    CHECK(!ok);
    if (!ok) {
        if (strstr(checker_error_message(&checker), needle) == NULL) {
            CHECK(strstr(checker_error_message(&checker), needle) != NULL);
            report_checker(&checker);
        }
    }
    arena_free(&arena);
}

static int write_valid_program(const char *path)
{
    FILE *f;

    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }

    fputs("type Number = Int | Float\n", f);
    fputs("struct Point { x: Int, y: Int }\n", f);
    fputs("enum Maybe { Nothing, Just(Int) }\n\n", f);
    fputs("fn add(a: Int, b: Int) -> Int = a + b\n\n", f);
    fputs("fn describe(n: Number) -> String = match n {\n", f);
    fputs("  i: Int => \"int\",\n", f);
    fputs("  f: Float => \"float\",\n", f);
    fputs("}\n\n", f);
    fputs("fn sum_point(p: Point) -> Int = match p {\n", f);
    fputs("  Point { x, y } => x + y,\n", f);
    fputs("}\n\n", f);
    fputs("fn bool_pair(p: (Bool, Bool)) -> Int = match p {\n", f);
    fputs("  (true, _) => 1,\n", f);
    fputs("  (false, _) => 0,\n", f);
    fputs("}\n\n", f);
    fputs("fn main(args: Array[String]) -> Unit = {\n", f);
    fputs("  let p = Point { x: 1, y: 2 }\n", f);
    fputs("  let n: Number = if true then 1 else 2.0\n", f);
    fputs("  let xs: Array[Int] = [1, 2, 3]\n", f);
    fputs("  let l: List[Int] = List[10, 20]\n", f);
    fputs("  let m: Map[String, Int] = Map{ \"one\": 1, \"two\": 2 }\n", f);
    fputs("  let f = fn(x: Int) -> Int = x + 1\n", f);
    fputs("  let s = \"hello\"\n", f);
    fputs("  let b = b\"A\\x42\"\n", f);
    fputs("  let maybe = Just(1)\n", f);
    fputs("  let y = match maybe { Nothing => 0, Just(v) => v }\n", f);
    fputs("  let z = describe(n)\n", f);
    fputs("  print(p)\n", f);
    fputs("  print(n)\n", f);
    fputs("  print(xs)\n", f);
    fputs("  print(l)\n", f);
    fputs("  print(m)\n", f);
    fputs("  println(len(xs))\n", f);
    fputs("  println(sum(xs))\n", f);
    fputs("  println(map(xs, f))\n", f);
    fputs("  println(filter(xs, fn(x: Int) -> Bool = x > 1))\n", f);
    fputs("  println(fold(xs, 0, fn(acc: Int, x: Int) -> Int = acc + x))\n", f);
    fputs("  println(get(m, \"one\"))\n", f);
    fputs("  println(keys(m))\n", f);
    fputs("  println(values(m))\n", f);
    fputs("  println(charAt(s, 0))\n", f);
    fputs("  println(chars(s))\n", f);
    fputs("  println(toString(p))\n", f);
    fputs("  println(b)\n", f);
    fputs("  println(y)\n", f);
    fputs("  println(z)\n", f);
    fputs("  println(sum_point(p))\n", f);
    fputs("  println(bool_pair((true, false)))\n", f);
    fputs("}\n", f);

    if (ferror(f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static void test_valid_program(void)
{
    CHECK(write_valid_program("build/_check_valid.idem"));
    expect_ok("build/_check_valid.idem");
    remove("build/_check_valid.idem");
}

static void test_errors(void)
{
    CHECK(write_file("build/_check_arith.idem",
        "fn main(args: Array[String]) -> Unit = {\n"
        "  let x = 1 + 2.0\n"
        "}\n"));
    expect_error("build/_check_arith.idem", "arithmetic");

    CHECK(write_file("build/_check_let.idem",
        "fn main(args: Array[String]) -> Unit = {\n"
        "  let x: Int = \"s\"\n"
        "}\n"));
    expect_error("build/_check_let.idem", "expected Int");

    CHECK(write_file("build/_check_arity.idem",
        "fn f(a: Int, b: Int) -> Int = a + b\n"
        "fn main(args: Array[String]) -> Unit = { let x = f(1) }\n"));
    expect_error("build/_check_arity.idem", "wrong number of arguments");

    CHECK(write_file("build/_check_unknown.idem",
        "fn main(args: Array[String]) -> Unit = { print(nope) }\n"));
    expect_error("build/_check_unknown.idem", "unknown value");

    CHECK(write_file("build/_check_struct.idem",
        "struct P { x: Int, y: Int }\n"
        "fn main(args: Array[String]) -> Unit = { let p = P { x: 1 } }\n"));
    expect_error("build/_check_struct.idem", "missing field");

    CHECK(write_file("build/_check_mapkey.idem",
        "fn main(args: Array[String]) -> Unit = {\n"
        "  let m: Map[Float, Int] = Map{}\n"
        "}\n"));
    expect_error("build/_check_mapkey.idem", "not hashable");

    CHECK(write_file("build/_check_any.idem",
        "fn f(x: Any) -> Bool = x == x\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_any.idem", "not comparable");

    CHECK(write_file("build/_check_anypat.idem",
        "fn f(x: Any) -> Int = match x { i: Int => i, _ => 0 }\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_anypat.idem",
                 "type patterns are not allowed on Any");

    CHECK(write_file("build/_check_letpat.idem",
        "enum E { A, B }\n"
        "fn main(args: Array[String]) -> Unit = { let A = B }\n"));
    expect_error("build/_check_letpat.idem", "refutable pattern");

    CHECK(write_file("build/_check_lambda_shadow.idem",
        "struct P { x: Int }\n"
        "fn main(args: Array[String]) -> Unit = {\n"
        "  let f = fn(P: Int) -> Int = 1\n"
        "}\n"));
    expect_error("build/_check_lambda_shadow.idem", "shadows a type name");

    CHECK(write_file("build/_check_return.idem",
        "fn f() -> Int = \"s\"\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_return.idem", "expected Int");

    CHECK(write_file("build/_check_join.idem",
        "fn main(args: Array[String]) -> Unit = {\n"
        "  let x: Int = if true then 1 else \"s\"\n"
        "}\n"));
    expect_error("build/_check_join.idem", "expected Int");

    CHECK(write_file("build/_check_nonex_bool.idem",
        "fn f(b: Bool) -> Int = match b { true => 1 }\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_nonex_bool.idem", "non-exhaustive match");

    CHECK(write_file("build/_check_nonex_enum.idem",
        "enum E { A, B }\n"
        "fn f(e: E) -> Int = match e { A => 1 }\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_nonex_enum.idem", "non-exhaustive match");

    CHECK(write_file("build/_check_nonex_union.idem",
        "fn f(x: Int | String) -> Int = match x { i: Int => i }\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_nonex_union.idem", "non-exhaustive match");

    CHECK(write_file("build/_check_nonex_tuple.idem",
        "fn f(p: (Bool, Bool)) -> Int = match p {\n"
        "  (true, true) => 1,\n"
        "  (false, false) => 0,\n"
        "}\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_nonex_tuple.idem", "non-exhaustive match");

    CHECK(write_file("build/_check_unreachable.idem",
        "fn f(b: Bool) -> Int = match b { _ => 0, true => 1 }\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_unreachable.idem", "unreachable pattern");

    CHECK(write_file("build/_check_import_lib.idem",
        "fn bad() -> Int = \"s\"\n"));
    CHECK(write_file("build/_check_import.idem",
        "import \"_check_import_lib.idem\"\n"
        "fn main(args: Array[String]) -> Unit = ()\n"));
    expect_error("build/_check_import.idem", "expected Int");

    remove("build/_check_arith.idem");
    remove("build/_check_let.idem");
    remove("build/_check_arity.idem");
    remove("build/_check_unknown.idem");
    remove("build/_check_struct.idem");
    remove("build/_check_mapkey.idem");
    remove("build/_check_any.idem");
    remove("build/_check_anypat.idem");
    remove("build/_check_letpat.idem");
    remove("build/_check_lambda_shadow.idem");
    remove("build/_check_return.idem");
    remove("build/_check_join.idem");
    remove("build/_check_nonex_bool.idem");
    remove("build/_check_nonex_enum.idem");
    remove("build/_check_nonex_union.idem");
    remove("build/_check_nonex_tuple.idem");
    remove("build/_check_unreachable.idem");
    remove("build/_check_import.idem");
    remove("build/_check_import_lib.idem");
}

int check_test_run(void)
{
    test_valid_program();
    test_errors();

    if (failures != 0) {
        fprintf(stderr, "%d type checker test failure(s)\n", failures);
        return failures;
    }

    printf("type checker tests passed\n");
    return 0;
}
