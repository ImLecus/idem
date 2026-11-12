#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "arena.h"
#include "check.h"
#include "codegen.h"
#include "ir.h"
#include "resolver.h"

typedef struct BenchTimes {
    double resolver;
    double checker;
    double lower;
    double codegen;
    double total;
} BenchTimes;

static void report_resolver(Resolver *resolver)
{
    Span span;
    span = resolver_error_span(resolver);
    fprintf(stderr, "resolver error at %s:%lu:%lu: %s\n",
            span.file != NULL ? span.file : "<unknown>",
            (unsigned long)span.line,
            (unsigned long)span.column,
            resolver_error_message(resolver));
}

static void report_checker(TypeChecker *checker)
{
    Span span;
    span = checker_error_span(checker);
    fprintf(stderr, "type error at %s:%lu:%lu: %s\n",
            span.file != NULL ? span.file : "<unknown>",
            (unsigned long)span.line,
            (unsigned long)span.column,
            checker_error_message(checker));
}

static void report_ir(IrProgram *program)
{
    Span span;
    span = ir_error_span(program);
    fprintf(stderr, "lowering error at %s:%lu:%lu: %s\n",
            span.file != NULL ? span.file : "<unknown>",
            (unsigned long)span.line,
            (unsigned long)span.column,
            ir_error_message(program));
}

static int bench_once(const char *path, BenchTimes *times)
{
    Arena arena;
    Resolver resolver;
    TypeChecker checker;
    IrProgram program;
    Codegen codegen;
    clock_t start;
    clock_t after_resolve;
    clock_t after_check;
    clock_t after_lower;
    clock_t after_codegen;

    memset(times, 0, sizeof(*times));
    arena_init(&arena);

    start = clock();
    resolver_init(&resolver, &arena);
    if (!resolver_resolve(&resolver, path)) {
        report_resolver(&resolver);
        arena_free(&arena);
        return 0;
    }
    after_resolve = clock();

    checker_init(&checker, &resolver, &arena);
    if (!checker_check_program(&checker)) {
        report_checker(&checker);
        arena_free(&arena);
        return 0;
    }
    after_check = clock();

    ir_init(&program, &resolver, &arena);
    if (!ir_lower(&program)) {
        report_ir(&program);
        arena_free(&arena);
        return 0;
    }
    after_lower = clock();

    codegen_init(&codegen, &program, &arena);
    if (!codegen_emit(&codegen, "/dev/null")) {
        fprintf(stderr, "codegen error: %s\n",
                codegen_error_message(&codegen));
        arena_free(&arena);
        return 0;
    }
    after_codegen = clock();

    times->resolver = (double)(after_resolve - start) /
                      (double)CLOCKS_PER_SEC;
    times->checker = (double)(after_check - after_resolve) /
                     (double)CLOCKS_PER_SEC;
    times->lower = (double)(after_lower - after_check) /
                   (double)CLOCKS_PER_SEC;
    times->codegen = (double)(after_codegen - after_lower) /
                     (double)CLOCKS_PER_SEC;
    times->total = (double)(after_codegen - start) /
                   (double)CLOCKS_PER_SEC;

    arena_free(&arena);
    return 1;
}

int main(int argc, char **argv)
{
    const char *path;
    int iterations;
    int i;
    BenchTimes times;
    BenchTimes total;

    if (argc < 2 || argc > 3) {
        fprintf(stderr,
                "usage: compiler-bench <file.idem> [iterations]\n");
        return 2;
    }

    path = argv[1];
    iterations = argc == 3 ? atoi(argv[2]) : 5;
    if (iterations < 1) {
        iterations = 1;
    }

    /* Warm-up: page cache, allocator, and branch predictors. */
    if (!bench_once(path, &times)) {
        return 1;
    }

    memset(&total, 0, sizeof(total));
    for (i = 0; i < iterations; i++) {
        if (!bench_once(path, &times)) {
            return 1;
        }
        total.resolver += times.resolver;
        total.checker += times.checker;
        total.lower += times.lower;
        total.codegen += times.codegen;
        total.total += times.total;
    }

    printf("compiler benchmark\n");
    printf("  input:      %s\n", path);
    printf("  iterations: %d\n", iterations);
    printf("  resolver:   %8.3f ms\n",
           total.resolver * 1000.0 / (double)iterations);
    printf("  typecheck:  %8.3f ms\n",
           total.checker * 1000.0 / (double)iterations);
    printf("  lower:      %8.3f ms\n",
           total.lower * 1000.0 / (double)iterations);
    printf("  codegen:    %8.3f ms\n",
           total.codegen * 1000.0 / (double)iterations);
    printf("  total:      %8.3f ms\n",
           total.total * 1000.0 / (double)iterations);

    return 0;
}
