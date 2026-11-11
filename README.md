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

The language design is closed for version 0.1. The C89 **lexer is implemented** and covered by unit tests. The parser, type checker, and code generator are still pending. The current `idem` binary dumps the token stream of a source file.

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
src/        C89 compiler sources (lexer implemented)
runtime/    C89 runtime and garbage collector (skeleton)
tests/      unit tests, golden tests, and error tests
examples/   example Idem programs
docs/       language specification and compiler guide
private/    design notes and decision log
Makefile    C89 build system
LICENSE     MIT
```

## Build

```
make            # builds ./idem
make test       # builds and runs the unit tests
make help       # lists all targets
```

The current interface dumps tokens:

```
./idem examples/hello.idem
```

The eventual interface will compile and run Idem programs with the same
command.

## Documentation

- [Language specification](docs/language.md)
- [Compiler implementation guide](docs/compiler.md)

## License

MIT. See [LICENSE](LICENSE).
