# The Idem Reference Compiler

**Version:** 0.1.0-draft  
**Date:** 2026-11-11

---

## 1. Overview

The Idem reference compiler is a strict C89 program that translates Idem source into portable C89. It is a **transpiler**: the emitted C89 is compiled by the system C compiler and linked with a small C89 runtime that provides values, collections, strings, I/O, and a mark-sweep garbage collector.

The compiler is deliberately modular:

```
source (.idem)
   │
   ▼
 lexer ──► parser ──► resolver ──► type checker ──► lowering ──► C89 codegen
   │          │           │              │              │
 tokens      AST       symbols        typed AST       IR
```

The driver then invokes the system C compiler and runs the resulting executable.

The language specification is in `docs/language.md`. This document explains how the reference implementation works, phase by phase.

---

## 2. Constraints and Conventions

### 2.1. Strict C89

The compiler and runtime are written in ISO C89:

- no `//` comments;
- no declarations after statements;
- no variable-length arrays;
- no `long long`, `stdint.h`, `inline`, designated initializers, compound literals, or `stdbool.h`;
- `size_t` comes from `stddef.h`;
- integer types are defined in a small `platform.h`:

```c
typedef unsigned char  u8;
typedef signed   char  i8;
typedef unsigned short u16;
typedef signed   short i16;
typedef unsigned long  u32;
typedef signed   long  i32;

#if defined(IDEM_HAS_LONG64)
typedef unsigned long      u64;
typedef signed   long      i64;
#else
typedef struct { u32 lo, hi; } u64;
typedef struct { u32 lo, hi; } i64;
#endif
```

On platforms where `long` is 64 bits, the compiler can use it directly for `Int`; otherwise it uses the two-word representation and the helper functions in `runtime/i64.c` for arithmetic, comparison, shifting, and conversion. The runtime must verify the assumption at startup or at build time.

### 2.2. Source locations

Every token and AST node carries a source span:

```c
typedef struct Span {
    const char *file;
    u32 line;
    u32 column;
    u32 offset;
} Span;
```

Lines and columns are 1-based. Columns count Unicode scalar values, not bytes. The lexer tracks line and column while decoding UTF-8.

### 2.3. Memory management in the compiler

The compiler itself does not need a garbage collector. It uses:

- an **arena allocator** for the AST, types, symbols, and IR;
- growable arrays for token lists, declarations, and diagnostics;
- a small string interner for identifiers and string literals.

Arena allocation is freed all at once when compilation finishes. This keeps the compiler simple and deterministic.

---

## 3. Repository Layout

```
src/
    main.c          driver and CLI
    platform.c/.h   integer typedefs, helpers, compiler abstraction
    arena.c/.h      arena allocator
    lexer.c/.h      UTF-8 lexer
    token.c/.h      token kinds, spans, and token names
    ast.c/.h        AST node definitions and dumper
    parser.c/.h     recursive-descent parser
    resolver.c/.h   imports, top-level names, aliases, signatures
    types.c/.h      type representation, unions, subtyping, joins
    check.c/.h      bidirectional type checker
    ir.c/.h         typed AST lowering to a slot-based IR
    codegen.c/.h    C89 emission
runtime/
    value.h         Value representation, tags, and runtime API
    internal.h      heap object layouts and internal helpers
    value.c         allocation, GC, constructors, equality, descriptors
    string.c        UTF-8, buffers, string helpers
    string_ops.c    string/bytes built-ins and conversions
    collection.c    arrays, lists, maps, higher-order collection built-ins
    print.c         canonical rendering
    io.c            console/file I/O, panic, exit
    math.c          integer/float arithmetic and math built-ins
tests/
    unit/           lexer, parser, resolver, checker tests
    golden/         .idem programs with expected stdout/stderr/exit
    errors/         .idem programs with expected diagnostics
    run_golden.sh   golden test runner
benchmarks/
    README.md       benchmark methodology and knobs
    compiler_bench.c in-process phase timing
    generate_inputs.sh synthetic compiler and runtime workloads
    run.sh          runtime benchmark driver
examples/
docs/
private/
Makefile
LICENSE
```

---

## 4. Driver and CLI

`src/main.c` implements the single command:

```
idem file.idem [program arguments...]
```

Steps:

1. Read `file.idem` as UTF-8.
2. Run the compiler pipeline.
3. If there are errors, print diagnostics to stderr and exit 1.
4. Write the generated C89 to a temporary file with a `.c` suffix.
5. Invoke the system C compiler:
   - use `$CC` if set, otherwise `cc`;
   - compile the generated C file together with the runtime sources in `runtime/`;
   - pass `-std=c89 -pedantic -Wall -Wextra -O2` for the reference build;
   - produce a temporary executable.
6. Run the executable with the remaining program arguments.
7. Forward the program's exit status.
8. Delete the temporary files unless `IDEM_KEEP_C` is set.

Usage errors exit with status 2. Compile errors exit with status 1. A program that calls `panic` exits with 101. A normal `main` exits with 0 unless the program calls `exit`.

The driver also supports two inspection modes:

- `idem --tokens file.idem` prints the token stream;
- `idem --emit-c file.idem` prints the generated C89.

The driver does not need a build system, a manifest, or a package manager in version 0.1.

---

## 5. Lexer

### 5.1. Input

The lexer receives a UTF-8 source buffer and produces a token array. It validates UTF-8 and reports invalid sequences with a source span.

### 5.2. Tokens

Token kinds include:

- identifiers and keywords;
- integer, float, boolean, char, string, raw-string, and byte literals;
- operators and punctuation;
- `newline`, used as a block-item separator;
- `eof`.

### 5.3. Whitespace and comments

Spaces and tabs are skipped. Newlines produce a `newline` token. Consecutive newlines can be collapsed into one. Comments are skipped:

- `#` skips to the end of the line.

There are no block comments.

### 5.4. Identifiers and keywords

The lexer scans `[A-Za-z_][A-Za-z0-9_]*` and interns the spelling. It then checks a keyword table. Built-in type names and built-in function names are ordinary identifiers at the lexer level; the resolver and type checker mark them as reserved.

### 5.5. Numbers

- `0x...` and `0b...` are parsed with an explicit base.
- Decimal integers are parsed into a 64-bit accumulator; overflow is a compile error.
- Float literals require a `.` or an exponent. They are parsed with `strtod` and checked for overflow to infinity (which is a compile error).
- Underscores are removed before conversion.

### 5.6. Characters, strings, and bytes

- `'...'` decodes exactly one Unicode scalar or escape.
- `"..."` decodes UTF-8 text and escapes.
- `r"..."` copies raw bytes until the closing quote.
- `b"..."` decodes ASCII and `\xNN` escapes into a `Bytes` literal.

String literals are stored in the AST as length-plus-bytes; they do not need to be NUL-terminated.

### 5.7. Newline handling

The parser treats `newline` as a separator inside blocks. The lexer can suppress newlines after tokens that cannot end an expression (`+`, `(`, `,`, `=>`, etc.) and before tokens that cannot begin one. A simple approach is to emit all newlines and let the parser skip them where they are not separators.

### 5.8. Errors

The lexer reports:

- invalid UTF-8;
- unterminated strings and chars;
- invalid escapes;
- invalid numeric literals;
- integer overflow.

The lexer continues after an error where possible so that the parser can report more than one problem.

---

## 6. Parser

### 6.1. Strategy

The parser is recursive descent with precedence climbing for binary expressions. It builds an AST in the compiler arena. The grammar is in `docs/language.md`, Appendix A.

### 6.2. AST

A simplified AST:

```c
typedef enum {
    EXPR_INT, EXPR_FLOAT, EXPR_BOOL, EXPR_CHAR, EXPR_STRING, EXPR_BYTES,
    EXPR_UNIT, EXPR_VAR, EXPR_TUPLE, EXPR_ARRAY, EXPR_LIST, EXPR_MAP,
    EXPR_STRUCT, EXPR_FIELD, EXPR_INDEX, EXPR_CALL, EXPR_LAMBDA,
    EXPR_IF, EXPR_MATCH, EXPR_BLOCK, EXPR_LET, EXPR_BINARY, EXPR_UNARY
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    Span span;
    union { ... } as;
} Expr;
```

Patterns, types, declarations, and imports have their own node kinds. Every node stores a `Span`.

### 6.3. Declarations

The parser reads top-level `import`, `type`, `struct`, `enum`, and `fn` declarations. It checks basic syntax but not name resolution or types.

### 6.4. Expressions

Precedence climbing handles binary operators according to Appendix B. `if`, `match`, `fn`, and blocks are parsed as primary expressions.

`if` requires `then` and `else`. `match` parses arms separated by commas. `let` is parsed only as a block item.

### 6.5. Patterns

Patterns are parsed by a dedicated function that handles:

- `_`, variables, and literals;
- unit and tuple patterns;
- struct patterns;
- enum variant patterns;
- type patterns (`x: T`).

The parser distinguishes a variable pattern from an enum variant pattern by context: an identifier followed by `(` or `{` is a constructor pattern; an identifier followed by `:` is a type pattern; otherwise it is a variable.

### 6.6. Error recovery

The parser reports an error and synchronizes at the next declaration or block-item boundary. It inserts an `EXPR_ERROR` node so that later phases can continue without crashing.

### 6.7. Newlines and separators

Inside a block, the parser consumes optional newlines and semicolons between items. It allows a trailing separator. A newline is not a separator inside parentheses, brackets, or braces of a struct/enum/match; the parser skips newlines there.

---

## 7. Resolver

### 7.1. Imports

The resolver loads imported files recursively:

- paths are relative to the importing file;
- a file is loaded once (canonical path);
- import cycles are reported as errors;
- the root module is the file passed on the command line;
- only the root may define `main`.

A module table maps canonical paths to module objects.

### 7.2. Top-level collection

For each module, the resolver collects all top-level declarations before checking bodies:

- type aliases;
- structs and enums;
- functions;
- imports.

It reports duplicate top-level names and reserved built-in names.

### 7.3. Alias expansion

Aliases are expanded lazily:

- `type Name = Type` records a definition;
- expansion follows aliases until a non-alias type is reached;
- a visiting set detects cycles;
- an alias cycle is a compile error.

Aliases are transparent; no nominal wrapper is created.

### 7.4. Nominal types

For each struct and enum, the resolver builds a type descriptor:

- a unique type ID;
- the declared name;
- fields (name, type, order) for structs;
- variants (name, payload types) for enums;
- a flag for recursion.

Recursive references are allowed. The resolver does not need to compute layouts yet.

### 7.5. Function signatures

For each function, the resolver builds a signature:

- parameter names and types;
- return type;
- a flag for top-level visibility (all are public);
- the module that declares it.

Function bodies are checked after all signatures are available, so mutual recursion works.

### 7.6. `main`

The resolver checks:

- the root module defines exactly one `main`;
- imported modules do not define `main`;
- the signature is exactly `(Array[String]) -> Unit`.

### 7.7. Name collisions

Because imports are unqualified, the resolver inserts imported top-level names into the importing module's top-level scope. A collision is a compile error. Version 0.1 has no qualified names or import aliases.

---

## 8. Type Checker

### 8.1. Type representation

Types are arena-allocated nodes:

```c
typedef enum {
    TY_ANY, TY_NEVER, TY_UNIT, TY_BOOL, TY_INT, TY_FLOAT,
    TY_CHAR, TY_STRING, TY_BYTES,
    TY_TUPLE, TY_FUNCTION, TY_UNION, TY_NOMINAL,
    TY_ARRAY, TY_LIST, TY_MAP, TY_VAR
} TypeKind;

typedef struct Type {
    TypeKind kind;
    union {
        struct { Type **items; u32 count; } tuple;
        struct { Type **params; u32 param_count; Type *result; } fn;
        struct { Type **items; u32 count; } union_;
        struct { u32 type_id; } nominal;
        struct { Type *element; } array;
        struct { Type *element; } list;
        struct { Type *key; Type *value; } map;
    } as;
} Type;
```

`TY_VAR` is used only during checking of built-in polymorphic functions; user code has no type variables.

### 8.2. Type equality and normalization

- Expand aliases before comparing.
- Normalize unions: flatten, deduplicate, remove `Never`, absorb `Any`, sort canonically.
- Nominal types compare by `type_id`.
- Tuples, functions, and collections compare structurally.

### 8.3. Subtyping

Implement `is_subtype(a, b)` according to §5.6 of the language spec:

- reflexive;
- `Never` ≤ any;
- any ≤ `Any`;
- union introduction and elimination;
- function contravariance/covariance;
- nominal identity;
- tuple exactness;
- collection invariance.

Use a small memo table to avoid exponential behavior on nested unions.

### 8.4. Joins

`join(a, b)` returns the least upper bound:

- if `a ≤ b`, return `b`;
- if `b ≤ a`, return `a`;
- otherwise return `normalize_union(a, b)`;
- if either is `Any`, return `Any`;
- if one is `Never`, return the other.

For `if` and `match`, fold `join` over the branch/arm types, ignoring `Never`.

### 8.5. Contexts

```c
typedef struct Binding {
    const char *name;
    Type *type;
    struct Binding *next;
} Binding;

typedef struct Context {
    Binding *locals;
    Module *module;
} Context;
```

Top-level functions and types are looked up in the module and its imports. Locals are a linked list; shadowing is handled by pushing a new binding.

### 8.6. Bidirectional checking

Two entry points:

```c
Type *infer(Context *ctx, Expr *expr);
void  check(Context *ctx, Expr *expr, Type *expected);
```

`check` can call `infer` and then `is_subtype`; `infer` can call `check` for subexpressions with known expected types. The checker inserts explicit `Coerce` nodes into the typed AST when a subtype is used where a supertype is expected.

Coercions:

- `Never` to any type: no runtime work;
- `T` to a union: add a tag;
- `T` to `Any`: add a tag.

### 8.7. Literals and expected types

- Integer literals are `Int`; float literals are `Float`.
- Empty array literals `[]` require an expected `Array[T]`.
- Empty list literals `List[]` require an expected `List[T]`.
- Empty map literals `Map{}` require an expected `Map[K,V]`.
- A non-empty array/list literal infers the join of its element types; if an expected type is available, each element is checked against the expected element type instead.
- A map literal infers the join of key types and value types; keys must be hashable.

### 8.8. Structs and enums

- Struct literal: look up the nominal type; check every field exactly once; field order is irrelevant.
- Field access: look up the field in the struct type.
- Enum constructor: look up the variant; check payload arity and types.
- Pattern matching: check patterns against the scrutinee type; bind variables.

### 8.9. Operators

- Arithmetic: both operands must be the same `Int` or `Float` type.
- Comparison: both operands must have the same comparable type. Equality is broader than ordering.
- Logical: `Bool`.
- Bitwise: `Int`.
- Unary `-`: `Int` or `Float`; `!`: `Bool`; `~`: `Int`.

### 8.10. Built-in overloads

For a call to an overloaded built-in:

1. Infer the argument types.
2. Expand aliases and normalize unions.
3. If any argument is a union, report an ambiguity error asking the user to `match` first.
4. Find the unique signature whose parameter types match the argument types.
5. If zero or more than one match, report an error.

For `map`, `filter`, and `fold`, infer the collection element type and the function type, then check the function argument against the expected function type. If the collection is a union, report an error.

### 8.11. Exhaustiveness

`exhaust.c` implements a usefulness check over pattern matrices:

- Decompose the scrutinee type into constructors:
  - `Bool`: `true`, `false`;
  - enum: variants;
  - union: members;
  - tuple/struct: a single constructor with component fields;
  - `Unit`: `()`;
  - `Int`, `Float`, `Char`, `String`, `Bytes`: infinite; only a variable/wildcard covers them;
  - `Any`: only a variable/wildcard;
  - `Never`: no constructors.
- A variable or `_` is a wildcard row.
- A match is exhaustive if the wildcard row is not useful after all arms.
- An arm is unreachable if it is not useful relative to previous arms.

Report the first missing case and the first unreachable arm with source spans.

### 8.12. Diagnostics

The checker reports:

- type mismatches, with expected and found types;
- undefined names and fields;
- wrong arity;
- non-exhaustive matches;
- unreachable arms;
- invalid coercions;
- non-hashable map keys;
- invalid `main`.

Each diagnostic includes a primary span and an optional note.

---

## 9. Lowering

### 9.1. Typed AST to IR

The typed AST still contains high-level constructs. Lowering produces a smaller, slot-based IR:

- every local, temporary, and parameter lives in a numbered `Value` slot;
- expressions lower to statements that write a slot;
- `if`, `match`, and blocks become statement nodes;
- patterns are kept in match arms with precomputed binding slots and projection statements;
- closures become separate IR functions plus explicit capture lists;
- collection and struct/enum literals become constructor expressions over argument slots;
- calls become direct calls to named functions or indirect calls through a closure value.

The IR is defined in `src/ir.h`:

```c
typedef struct IrStmt IrStmt;
typedef struct IrExpr IrExpr;
typedef struct IrFunction IrFunction;

struct IrFunction {
    int id;
    int is_lambda;
    const char *name;
    int param_count;
    int slot_count;
    int result_slot;
    IrCapture *captures;
    IrStmt *body;
};
```

Statements are `let` (slot assignment), `if`, `match`, and blocks. Expressions are constants, slot reads, function/closure values, direct or indirect calls, built-ins, struct/enum/collection constructors, projections, indexing, and primitive operators.

### 9.2. Coercions and tags

Every `Value` already carries its runtime tag. Union injection and `Any` widening therefore lower to **no code**: the value keeps its tag and payload. `Never` coercions also produce no code because those paths never execute.

Static types recorded by the checker on AST nodes are still used by codegen to choose:

- integer versus floating-point arithmetic;
- specialized `sum`, `min`, `max`, and `abs` helpers;
- nominal type descriptors for struct/enum construction and type patterns.

### 9.3. Closures

For each lambda:

1. Lowering records the enclosing function as its parent.
2. Each free variable is turned into a capture slot in the lambda and, transitively, in any enclosing lambdas.
3. The capture list records the slot in the parent function that supplies each value.
4. At runtime a closure value stores a C function pointer and a `Value` array of captured values.
5. The generated function loads its capture slots from the environment at entry.

Named functions have no captures and ignore the environment argument.

### 9.4. Pattern matching

`match` lowers to an ordered sequence of guarded arm blocks:

- the scrutinee is evaluated once into a slot;
- each arm gets a generated C boolean test built from runtime tag checks, nominal descriptor checks, variant checks, literal comparisons, and safe field projections;
- projections are short-circuited, so a payload is only read after its tag and descriptor match;
- binding statements run only after the whole pattern test succeeds;
- the selected arm writes the result slot and jumps to the end of the match.

Exhaustiveness is guaranteed by the checker. Codegen still emits a `panic("non-exhaustive match")` fallback as a safety net.

### 9.5. Collections

- Array/list/map literals become calls to runtime constructors.
- `map`, `filter`, and `fold` become runtime loops that call the function value.
- `sum` becomes a specialized loop for `Int` or `Float`.
- `len`, `get`, `set`, `push`, `concat`, `keys`, and `values` become runtime calls.

### 9.6. Blocks and `let`

- Each `let` becomes a local `Value` slot.
- Destructuring `let` becomes projections plus bindings.
- The block's value is the last expression's value, or `unit` if the block is empty or ends with a `let`.

---

## 10. C89 Code Generation

### 10.1. Value representation

The runtime defines:

```c
typedef enum {
    IDEM_TAG_UNIT, IDEM_TAG_BOOL, IDEM_TAG_INT, IDEM_TAG_FLOAT,
    IDEM_TAG_CHAR, IDEM_TAG_STRING, IDEM_TAG_BYTES, IDEM_TAG_ARRAY,
    IDEM_TAG_LIST, IDEM_TAG_MAP, IDEM_TAG_TUPLE, IDEM_TAG_STRUCT,
    IDEM_TAG_ENUM, IDEM_TAG_CLOSURE
} IdemTag;

typedef struct Value {
    IdemTag tag;
    union {
        long i;          /* Int; the runtime requires 64-bit long */
        double f;
        unsigned long c;
        void *obj;
    } as;
} Value;
```

The runtime assumes a C implementation whose `long` is 64 bits; this is checked at build time. A two-word fallback can be added later without changing the IR.

### 10.2. Heap objects

Every heap object starts with a header:

```c
typedef struct IdemObj {
    unsigned int mark;
    unsigned int kind;
    struct IdemObj *next;
} IdemObj;
```

Specific objects embed the header first:

```c
typedef struct IdemStringObj {
    IdemObj header;
    size_t length;
    char *data;
} IdemStringObj;
```

Arrays, lists, tuples, maps, structs, enums, and closures follow the same pattern.

### 10.3. Functions

Every generated function has the same C signature:

```c
Value idem_fn_name(Value *env, Value *args, size_t argc);
```

- Named functions ignore `env`.
- Closures receive their captured values in `env`.
- Parameters are copied from `args` into slots at entry.
- The return value is a `Value`.
- The type checker guarantees arity; generated code does not add an arity check.

### 10.4. Shadow stack

The runtime maintains a stack of rooted slot frames:

```c
void idem_gc_push_frame(Value *slots, size_t count);
void idem_gc_pop_frame(void);
```

Each generated function allocates one `Value slots[N]` array, initializes every slot to unit, and pushes the whole array as a GC root frame. Temporaries and locals all live in this array, so the collector traces them conservatively but precisely enough for correctness. Runtime functions that build intermediate `Value` arrays push additional temporary frames.

### 10.5. Allocation

`idem_obj_alloc(size, tag)`:

1. If the allocation count since the last collection exceeds the threshold, run a collection.
2. Request memory from the system allocator.
3. If that fails, collect and retry.
4. If it still fails, call `idem_panic_oom`.
5. Link the new object into the global object list.

### 10.6. Garbage collection

Mark-sweep:

- `idem_gc_collect()` marks from all rooted slot frames;
- objects never move;
- unmarked objects are freed together with their owned arrays/data;
- there are no finalizers.

The collector traces:

- array/list/tuple elements;
- map keys and values;
- struct fields;
- enum payloads;
- closure environments.

### 10.7. Control flow

- `if` becomes a C `if`.
- Blocks become C blocks.
- `match` becomes a sequence of tag checks, literal comparisons, and field projections, with generated `goto` labels for the selected arm.
- `&&` and `||` are lowered to `if` statements so short-circuiting is preserved.
- `Never` expressions call `panic` or `exit` and do not return.

### 10.8. Strings and collections

The runtime provides:

- `idem_string`, `idem_bytes`, `idem_concat`, `idem_slice`,
  `idem_compare`, `idem_byte_at`, `idem_char_at`, `idem_chars`,
  `idem_to_bytes`, `idem_from_bytes`;
- `idem_array`, `idem_list`, `idem_map_lit`, `idem_get`, `idem_set`,
  `idem_remove`, `idem_push`, `idem_concat`;
- `idem_map`, `idem_filter`, `idem_fold`, `idem_sum_int`,
  `idem_sum_float`, `idem_keys`, `idem_values`;
- `idem_print`, `idem_to_string`, `idem_panic`.

All collection operations preserve immutability in the observable API. The reference runtime copies on update. The GC handles reclamation.

### 10.9. Printing

`idem_print` and `idem_to_string` implement the canonical rendering from `docs/language.md` §9.4:

- integers in decimal;
- floats with the shortest round-trip algorithm;
- strings raw;
- bytes with `b"..."`;
- structs/enums/arrays/lists/maps in constructor syntax;
- functions as `<function>`.

The float formatter is the trickiest part. A practical approach is:

1. Try `%.1g`, `%.2g`, ..., `%.17g` until `strtod` round-trips to the same bits.
2. Normalize the exponent format.
3. Handle `NaN`, `inf`, `-inf`, and `-0.0`.

This is deterministic as long as `strtod` and `printf` are correctly rounded; the reference implementation can include its own decimal conversion if stricter portability is required.

### 10.10. I/O and errors

- `readFile` uses `fopen`/`fread`; it validates UTF-8 and returns an `Error` enum value on failure.
- `writeFile` uses `fopen`/`fwrite`.
- `readLine` reads until `\n` or EOF, strips `\r\n`, and returns `Unit` at EOF.
- `print`/`println` write to stdout and do not flush automatically.
- `flush` calls `fflush(stdout)`.
- `panic` flushes stdout, writes to stderr, and calls `exit(101)`.
- `exit` flushes stdout and calls `exit(code & 0xFF)`.

### 10.11. Program entry

The generated C file contains:

```c
int main(int argc, char **argv)
{
    Value args[1];
    Value result;

    idem_runtime_init();
    args[0] = idem_args_array(argc, argv, &idem_stype_0,
                              "program.idem");
    idem_gc_push_frame(args, 1);
    result = idem_fn_main(NULL, args, 1);
    (void)result;
    idem_gc_pop_frame();
    fflush(stdout);
    return 0;
}
```

`idem_args_array` creates an `Array[String]` whose first element is the source-path string passed to the compiler, followed by the program's command-line arguments.

---

## 11. Runtime Details

### 11.1. Strings

- `StringObj` stores a length and a byte buffer.
- Constructors validate UTF-8.
- Slicing checks UTF-8 boundaries.
- `charAt` decodes UTF-8 from the beginning to the requested index.
- `chars` allocates an `Array[Char]`.

### 11.2. Bytes

- `BytesObj` stores a length and a byte buffer.
- No UTF-8 validation.
- `fromBytes` validates and returns `String | Error`.

### 11.3. Arrays

- `ArrayObj` stores a length and a `Value` array.
- `set` copies the array and replaces one element.
- `slice` copies the requested range.
- `concat` copies both arrays.
- Indexing out of bounds panics for `set` and `slice`; `get` returns `Unit`.

### 11.4. Lists

- A list is stored as an immutable length-plus-`Value` array, like an array but with a distinct tag.
- `push` copies the elements into a new list with one extra element.
- `get` is O(1); `concat`, `map`, `filter`, and `fold` walk the array.
- A persistent linked or tree representation can replace this later without changing the language or IR.

### 11.5. Maps

- A map is stored as two parallel `Value` arrays (keys and values) in insertion order.
- `set` replaces an existing key in place or appends a new entry.
- `remove` copies all entries except the matching key.
- Lookup is a linear scan using structural equality. This is simple and deterministic; a hash index can replace it later.
- `keys` and `values` return arrays in insertion order.

### 11.6. Structs and enums

- `StructObj` stores a type descriptor pointer and a `Value` array of fields.
- `EnumObj` stores a type descriptor pointer, a variant index, and a `Value` array of payloads.
- Field access is an array index; the compiler knows the layout.
- Pattern matching checks the descriptor pointer and variant index.
- Arrays, lists, maps, tuples, and closures also store a structural type descriptor pointer so that type patterns on unions such as `Array[Int] | Array[String]` are distinguishable at runtime. The compiler interns one descriptor per structurally equal type.

### 11.7. Closures

- `ClosureObj` stores a function pointer, a structural function-type descriptor, and a `Value` array of captured values.
- Calling a closure passes the environment as an implicit argument.

---

## 12. Testing

### 12.1. Unit tests

- Lexer: tokens, spans, escapes, invalid UTF-8.
- Parser: precedence, declarations, patterns, error recovery.
- Types: normalization, equality, subtyping, joins.
- Checker: inference, coercions, exhaustiveness, overload resolution.

### 12.2. Golden tests

Each test is a directory:

```
tests/golden/factorial/
    main.idem
    stdout.txt
    stderr.txt
    exit.txt
```

The test runner (`tests/run_golden.sh`, invoked by `make golden`) compiles and runs `main.idem`, compares stdout, stderr, and exit status, and reports a diff on failure.

### 12.3. Example programs

`examples/` contains the programs used in the acceptance criteria:

- `hello.idem`
- `factorial.idem`
- `fibonacci.idem`
- `shapes.idem`
- `collections.idem`

### 12.4. Error tests

Error tests contain an `.idem` file and an `errors.txt` file with expected diagnostics. The runner compares file, line, column, and message.

### 12.5. Benchmarks

`benchmarks/` contains performance inputs and runners:

- `make bench-compiler` measures resolver, checker, lowering, and codegen CPU time on a generated module with hundreds of nominal types and functions;
- `make bench-runtime` compiles a set of workloads once and measures wall time for recursion, arrays, strings, maps, GC, recursive enums, and closures;
- `make bench` runs both.

The compiler benchmark runs the pipeline in-process. The runtime benchmark excludes C compilation by generating and building each workload once before timing repeated executions. See `benchmarks/README.md` for workload sizes and environment variables.

### 12.6. Future testing

- Property-based tests for union normalization and subtyping.
- Fuzzing for the lexer and parser.
- Differential tests against a reference interpreter.
- Stress tests for the GC.
- Automated performance regression tracking.

---

## 13. Implementation Plan

A practical order for building version 0.1:

1. **Skeleton:** arena, diagnostics, token definitions, driver.
2. **Lexer:** all tokens, UTF-8, comments, literals.
3. **Parser:** declarations, types, expressions, patterns.
4. **Resolver:** imports, top-level names, aliases, signatures.
5. **Type checker:** primitives, functions, `if`, `let`, structs, enums, `match`, unions.
6. **Lowering and codegen:** values, functions, blocks, `if`, structs, enums, `match`.
7. **Runtime:** allocation, strings, arrays, printing, panic/exit.
8. **GC:** shadow stack, mark-sweep.
9. **Collections:** lists and maps.
10. **Built-ins:** the full catalog.
11. **Modules:** imports and cycles.
12. **Tests and examples:** golden tests, unit tests, acceptance programs.
13. **Polish:** diagnostics, float formatting, documentation.

Version 0.1 deliberately omits:

- modules in the first executable milestone (they can be added after the core works);
- `List` and `Map` in the first milestone (arrays, strings, and structs/enums are enough to validate the model);
- non-deterministic built-ins (`random`, `now`) until the core is stable;
- self-hosting.

---

## 14. Bootstrap and Future Work

The C89 compiler is the reference implementation. Future work includes:

- self-hosting the compiler in Idem;
- a faster backend (bytecode or native);
- a package manager and manifests;
- qualified imports and import aliases;
- richer collection implementations (persistent vectors, HAMTs);
- more built-ins (environment, directories, time formatting);
- an LSP server and formatter;
- a formal semantics for the type system and runtime.

Self-hosting should wait until the language and the C89 compiler have stabilized, because a self-hosted compiler doubles the maintenance cost during rapid language changes.

---

## 15. Common Pitfalls

- **Boxing everything is simple but slow.** Do not optimize prematurely; correctness first. Desboxing locals is a later optimization.
- **GC roots are easy to miss.** Push every temporary that can survive an allocation. Use a debug mode that poisons freed memory.
- **Union tags must be consistent.** A value's runtime tag must match its static union member. Do not strip tags during coercions.
- **`Any` is opaque.** Do not expose `typeOf` or casts in user code. Built-ins may inspect tags, but the type checker must reject user pattern matching on `Any`.
- **Float formatting is subtle.** Test `0.1`, `1e300`, `NaN`, `-0.0`, and denormals.
- **Map iteration order matters.** Preserve insertion order so that `keys`, `values`, and printing are deterministic.
- **C89 portability matters.** Avoid `long long` in generated code; the runtime uses 64-bit `long` and checks the assumption at build time.
- **Diagnostics should point at the smallest useful span.** A good error message is part of the language experience.
