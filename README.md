# Idem

Idem is a deterministic-by-default, minimal, type-centric programming language. Every construct is an expression with a type; types denote sets of values; and `if` returns the least upper bound (join) of its branches.

```idem
fn factorial(n: Int) -> Int =
    if n <= 1 then 1 else n * factorial(n - 1)

fn main(args: Array[String]) -> Unit = {
    println(factorial(10))
}
```

## Status

The language design is closed for `Idem2026`, and `idem 1.0.0` is the first stable compiler implementing it. The C89 **lexer, parser, resolver, type checker, IR lowering, C89 code generator, and runtime are implemented** and covered by unit and golden tests. The `idem` binary compiles a source file with the system C compiler and runs the resulting executable; `--tokens` dumps the token stream and `--emit-c` prints the generated C89.

The normative specification is in [docs/language.md](docs/language.md), and the compiler implementation guide is in [docs/compiler.md](docs/compiler.md).

## Design highlights

- **Types are sets.** `Int | String` is a union; `if` computes joins.
- **Everything is an expression.** Blocks, `if`, `match`, and `let` produce values.
- **User data is immutable.** Mutation, I/O, and non-determinism live in built-ins.
- **Closed effects.** The compiler does not track effects, but the set of effectful operations is fixed by the prelude.
- **No user-defined generics.** `Any` is an opaque top type; the prelude provides `Array[T]`, `List[T]`, and `Map[K, V]`.
- **No undefined behavior.** Integer overflow, division by zero, and out-of-bounds access are deterministic panics.
- **Portable compiler.** The reference compiler is strict C89 and emits portable C89.

## Repository layout

```
src/        C89 compiler sources (lexer, parser, resolver, checker, IR, codegen)
runtime/    C89 runtime, collections, printing, I/O, and mark-sweep GC
benchmarks/ compiler-phase and generated-program performance suite
tests/      unit tests, golden tests, and error tests
examples/   example Idem programs
docs/       language specification and compiler guide
private/    design notes and decision log
Makefile    C89 build system
LICENSE     MIT
```

## Build

```
make            # builds ./idem and the C89 runtime library
make test       # builds and runs the unit tests
make golden     # compiles and runs the golden tests
make bench      # runs compiler-phase and runtime benchmarks
make help       # lists all targets
```

The interface compiles, links, and runs an Idem program:

```
./idem examples/hello.idem
./idem examples/hello.idem arg1 arg2
./idem --tokens examples/hello.idem
./idem --emit-c examples/hello.idem > hello.c
```

Set `IDEM_KEEP_C=1` to keep the generated C file and its temporary
executable for inspection. The driver uses `$CC` when set, otherwise
`cc`.

## Benchmarks

`make bench` measures the compiler phases (resolver, type checker, lowering,
and C89 emission) and runs precompiled workloads for recursion, arrays,
strings, maps, GC, recursive enums, and closures. See
[benchmarks/README.md](benchmarks/README.md) for the workload details and
environment knobs.

## Documentation

- [Language specification](docs/language.md)
- [Compiler implementation guide](docs/compiler.md)

## License

MIT. See [LICENSE](LICENSE).
