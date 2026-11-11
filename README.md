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

The language design is closed for version 0.1. The reference compiler is a work in progress. The normative specification is in [docs/language.md](docs/language.md), and the compiler implementation guide is in [docs/compiler.md](docs/compiler.md).

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
src/        C89 compiler sources (to be implemented)
runtime/    C89 runtime and garbage collector (to be implemented)
tests/      unit tests and golden tests
examples/   example Idem programs
docs/       language specification and compiler guide
Makefile    build and test entry points (to be added with the compiler)
LICENSE     MIT
```

## Build

The compiler is not implemented yet. Once it is, the intended interface is:

```
make
make test
./idem examples/hello.idem
```

## Documentation

- [Language specification](docs/language.md)
- [Compiler implementation guide](docs/compiler.md)

## License

MIT. See [LICENSE](LICENSE).
