# The Idem Language Specification

**Version:** 0.1.0-draft  
**Date:** 2026-11-11

---

## 1. Introduction

Idem is a deterministic-by-default, minimal, type-centric programming language. Every construct is an expression with a type; there is no separate category of statements. Types denote sets of values, and the type of `if c then a else b` is the least upper bound (join) of the types of `a` and `b`.

User data is immutable. Input/output, non-determinism, and internal mutation are confined to compiler-provided **built-ins**. The compiler does not perform static effect tracking: purity is a programming discipline, but the set of effects is closed in the kernel. The prelude provides a small, fixed catalog of built-ins, including non-deterministic ones such as `random` and `now`.

Idem has no user-defined generics. The type `Any` is an opaque top type used to transport heterogeneous values to built-ins. The prelude provides a closed set of generic collections: `Array[T]`, `List[T]`, and `Map[K, V]`. For "one of several types", use a union (`Int | String`) or a monomorphic `enum`.

The reference implementation is a compiler written in strict C89 that emits portable C89. The language specification is independent of the implementation, but the reference compiler is the normative implementation for version 0.1.

### 1.1. Design principles

1. **Determinism by default, explicit exceptions.** Pure evaluation is deterministic and left-to-right. Only built-ins can produce I/O, non-determinism, or internal mutation. Non-deterministic built-ins are opt-in and documented.
2. **Everything is a typed expression.** Blocks produce values, and `if`, `match`, `let`, and functions are expressions.
3. **Types are the unit of composition.** Types describe sets of values. `if` computes the join of its branches.
4. **Minimalism and orthogonality.** Few constructs, each with a clear semantics.
5. **User immutability.** Values and bindings defined by the programmer are immutable. Mutation exists only inside built-ins.
6. **No undefined behavior.** Every well-typed program has defined semantics. Errors are values or deterministic aborts.
7. **Effects in the kernel.** I/O, non-determinism, and internal mutation live in privileged built-ins. The effect set is closed.
8. **Portable compiler.** The reference compiler is strict C89 with no mandatory external dependencies and a minimal C89 runtime.
9. **The specification is normative.** If the implementation and this document disagree, this document wins until it is explicitly revised.

### 1.2. Non-goals

Idem does **not** aim to provide:

- classes, inheritance, or implementation reuse through subtyping;
- implicit `null`;
- unchecked exceptions;
- global mutable state;
- user-defined generics or type parameters;
- an `IO` monad or static effect tracking;
- downcasting of `Any` in user code;
- concurrency in the core;
- arbitrary macros or metaprogramming;
- user-defined operator overloading;
- runtime reflection;
- platform-dependent language semantics.

---

## 2. Notation and Conventions

### 2.1. Grammar notation

The grammar uses EBNF:

- `x?` means zero or one occurrence of `x`.
- `x*` means zero or more occurrences.
- `x+` means one or more occurrences.
- `x (',' x)*` means a comma-separated list.
- `|` separates alternatives.
- Literal terminals are written in `'single quotes'`.

### 2.2. Typing judgments

A typing judgment has the form:

```
Γ ⊢ e : τ
```

read as "in context `Γ`, expression `e` has type `τ`". A context maps names to types.

### 2.3. Subtyping and joins

- `τ₁ ≤ τ₂` means every value of `τ₁` is also a value of `τ₂`.
- `τ₁ ⊔ τ₂` is the least upper bound (join) of `τ₁` and `τ₂`.
- `τ₁ ≡ τ₂` means the two types are equal.

### 2.4. Normative language

The words **must**, **must not**, **required**, and **shall** are normative. **May** and **should** are informative.

---

## 3. Lexical Structure

### 3.1. Source encoding

Source files are UTF-8. A leading UTF-8 byte-order mark (U+FEFF) is ignored. Invalid UTF-8 in source code is a compile-time error.

### 3.2. Whitespace and newlines

Space, tab, carriage return, and line feed are whitespace. Newlines separate expressions inside blocks (see §6.2). A newline is ignored in an explicitly incomplete context: after a binary operator, `=`, `,`, `(`, `[`, `then`, `else`, `=>`, `:`, `->`, or `|`. A newline before a binary operator or postfix operator is a separator, not a continuation; place the operator at the end of the previous line instead.

### 3.3. Comments

- A line comment begins with `--` and extends to the end of the line.
- A block comment begins with `{-` and ends with `-}`. Block comments nest.

Comments are whitespace.

### 3.4. Identifiers

An identifier begins with an ASCII letter or underscore and continues with ASCII letters, digits, or underscores:

```
ident ::= [A-Za-z_][A-Za-z0-9_]*
```

Identifiers are case-sensitive. Non-ASCII identifiers are not supported in version 0.1.

### 3.5. Keywords

The following words are reserved:

```
type struct enum fn let if then else match import true false
```

The following names denote built-in types and are reserved as type names:

```
Any Never Unit Bool Int Float Char String Bytes Array List Map Error
```

The following names denote built-in functions and are reserved as value names:

```
print println readLine readFile writeFile flush exit panic
len get set remove push concat map filter fold sum keys values
abs min max sqrt floor ceil pow random now
byteAt slice compare charAt chars toInt toFloat toString toBytes fromBytes
```

The prelude also defines the enum `Error` with constructors `NotFound`, `PermissionDenied`, `InvalidUtf8`, and `Io`. The name `Error` and its constructors are reserved as well.

No reserved word, reserved type name, or built-in name may be used as a user-defined identifier. Collection type names (`Array`, `List`, `Map`) are reserved and cannot be used as variable names or user-defined type names.

Collection literals use the reserved type names: `List[e1, e2, ...]` is a list literal and `Map{ key: value, ... }` is a map literal. In expression position, `List[...]` and `Map{...}` are literals; in type position, `List[T]` and `Map[K, V]` are type applications. The lower-case `map(...)` is the built-in function. A local binding may not shadow a reserved built-in or prelude name.

### 3.6. Literals

**Integer literals**

```
int-lit ::= '0' | [1-9][0-9_]* | ('0x' | '0X') [0-9A-Fa-f_]+ | ('0b' | '0B') [01_]+
```

Underscores are ignored. The value must fit in a signed 64-bit integer; otherwise it is a compile-time error. There are no suffixes.

**Float literals**

```
float-lit ::= [0-9][0-9_]* '.' [0-9][0-9_]* ([eE] [+-]? [0-9]+)?
            | [0-9][0-9_]* [eE] [+-]? [0-9]+
```

A float literal must contain a decimal point or an exponent. There are no hexadecimal float literals and no suffixes. The literal is rounded to the nearest IEEE-754 binary64 value.

**Boolean literals**

`true` and `false`, of type `Bool`.

**Character literals**

A character literal is a single Unicode scalar value or an escape, enclosed in single quotes: `'a'`, `'\n'`, `'\u{1F600}'`.

**String literals**

A string literal is a sequence of characters and escapes enclosed in double quotes: `"hello"`, `"línea\n"`. Strings are UTF-8. A raw string literal `r"..."` has no escape processing and ends at the next `"`.

**Byte literals**

A byte literal is `b"..."`, containing ASCII characters and escapes. It denotes a `Bytes` value. Non-ASCII characters must be written as `\xNN` escapes.

**Escapes**

The following escapes are valid in character, string, and byte literals:

```
\n  \r  \t  \\  \'  \"  \0  \xNN  \u{XXXXXX}
```

`\xNN` denotes one byte. In a string literal, the bytes produced by `\xNN` escapes must form valid UTF-8; otherwise the literal is a compile-time error. `\u{...}` denotes one Unicode scalar and is not allowed in byte literals. A `\u{...}` value must be a valid Unicode scalar (not a surrogate, at most U+10FFFF).

### 3.7. Operators and punctuation

```
+  -  *  /  %  ==  !=  <  <=  >  >=  &&  ||  !
&  |  ^  ~  <<  >>
=  ->  =>  :  ,  ;  .  (  )  [  ]  {  }  |
_
```

`|` is used in type unions and bitwise OR. Context disambiguates. `->` appears in function types and return annotations. `=>` separates match patterns from expressions.

### 3.8. Optional semicolons and trailing commas

Inside a block, expressions may be separated by newlines, by `;`, or by both. A trailing `;` is allowed and does not change the value of the block. Trailing commas are allowed in struct fields, enum variants, function parameters, function arguments, tuple expressions, array/list/map literals, and match arms.

---

## 4. Grammar

### 4.1. Programs and declarations

```
program     ::= (import | decl)*

import      ::= 'import' string-lit

decl        ::= type-decl | struct-decl | enum-decl | fn-decl

type-decl   ::= 'type' ident '=' type

struct-decl ::= 'struct' ident '{' field (',' field)* ','? '}'
field       ::= ident ':' type

enum-decl   ::= 'enum' ident '{' variant (',' variant)* ','? '}'
variant     ::= ident ('(' type (',' type)* ')')?

fn-decl     ::= 'fn' ident '(' param-list? ')' '->' type '=' expr
param-list  ::= param (',' param)*
param       ::= ident ':' type
```

A program must contain exactly one `main` declaration in its root module (see §8.5).

### 4.2. Types

```
type        ::= union-type

union-type  ::= arrow-type ('|' arrow-type)*

arrow-type  ::= tuple-type ('->' type)?

tuple-type  ::= '()'
              | '(' type (',' type)+ ')'
              | primary-type

primary-type ::= 'Any' | 'Never' | 'Unit' | 'Bool' | 'Int' | 'Float'
               | 'Char' | 'String' | 'Bytes'
               | ident ('[' type (',' type)* ']')?
```

`()` is the unit type and is equivalent to `Unit`. `(T)` is grouping, not a one-element tuple. There are no one-element tuple types. A type application `Name[...]` is valid only when `Name` is one of the built-in type constructors `Array`, `List`, or `Map`, with the appropriate number of arguments.

### 4.3. Expressions

```
expr        ::= if-expr | match-expr | fn-expr | block | binary-expr

if-expr     ::= 'if' expr 'then' expr 'else' expr

match-expr  ::= 'match' expr '{' (match-arm (',' match-arm)* ','?)? '}'
match-arm   ::= pattern '=>' expr

fn-expr     ::= 'fn' '(' param-list? ')' '->' type '=' expr

block       ::= '{' block-item* '}'
block-item  ::= let-binding | expr

let-binding ::= 'let' pattern (':' type)? '=' expr

binary-expr ::= unary-expr (bin-op unary-expr)*
bin-op      ::= '+' | '-' | '*' | '/' | '%'
              | '==' | '!=' | '<' | '<=' | '>' | '>='
              | '&&' | '||'
              | '&' | '|' | '^' | '<<' | '>>'

unary-expr  ::= ('!' | '-' | '~') unary-expr | postfix-expr

postfix-expr ::= primary-expr postfix*
postfix      ::= '.' ident
              | '[' expr ']'
              | '(' arg-list? ')'

arg-list    ::= expr (',' expr)*

primary-expr ::= literal
              | ident
              | '(' ')'
              | '(' expr ')'
              | '(' expr ',' expr (',' expr)* ')'
              | struct-lit
              | array-lit
              | list-lit
              | map-lit
              | if-expr
              | match-expr
              | fn-expr
              | block

struct-lit  ::= ident '{' field-init (',' field-init)* ','? '}'
field-init  ::= ident ':' expr

array-lit   ::= '[' (expr (',' expr)*)? ']'
list-lit    ::= 'List' '[' (expr (',' expr)*)? ']'
map-lit     ::= 'Map' '{' (map-entry (',' map-entry)*)? ','? '}'
map-entry   ::= expr ':' expr

literal     ::= int-lit | float-lit | 'true' | 'false'
              | char-lit | string-lit | raw-string-lit | byte-lit
```

The grammar above is ambiguous with respect to binary precedence. §4.5 defines the precedence and associativity that disambiguate it.

### 4.4. Patterns

```
pattern     ::= '_'
              | ident
              | literal
              | '()'
              | '(' pattern (',' pattern)+ ')'
              | ident '{' pattern-field (',' pattern-field)* ','? '}'
              | ident ('(' pattern (',' pattern)* ')')?
              | ident ':' type

pattern-field ::= ident ':' pattern
                | ident
```

The form `ident ':' type` is a **type pattern**. It is valid only when the scrutinee type is a union and `type` is one of its members. Type patterns are forbidden on `Any`.

### 4.5. Operator precedence and associativity

From lowest to highest precedence:

| Level | Operators                    | Associativity   |
| ----- | ---------------------------- | --------------- |
| 1     | `\|\|`                       | left            |
| 2     | `&&`                         | left            |
| 3     | `==` `!=`                    | non-associative |
| 4     | `<` `<=` `>` `>=`            | non-associative |
| 5     | `\|`                         | left            |
| 6     | `^`                          | left            |
| 7     | `&`                          | left            |
| 8     | `<<` `>>`                    | left            |
| 9     | `+` `-`                      | left            |
| 10    | `*` `/` `%`                  | left            |
| 11    | unary `!` `-` `~`            | prefix          |
| 12    | call, field access, indexing | left            |

Comparison operators are non-associative: `a < b < c` is a syntax error and must be written `(a < b) && (b < c)`.

`if`, `match`, `fn`, and blocks are primary expressions. When used as an operand of a binary operator, they extend as far to the right as possible; parentheses are recommended for clarity.

---

## 5. Types

### 5.1. Type expressions

A type is one of:

- `Any`, `Never`, `Unit`, `Bool`, `Int`, `Float`, `Char`, `String`, `Bytes`;
- a tuple type `(T₁, ..., Tₙ)` with `n ≥ 2`, or `()`;
- a function type `(T₁, ..., Tₙ) -> R`, where `n ≥ 0`;
- a union `T₁ | ... | Tₙ`;
- a nominal type name (struct or enum);
- a type alias name;
- a built-in collection type `Array[T]`, `List[T]`, or `Map[K, V]`.

Function types are right-associative: `A -> B | C` means `A -> (B | C)`. To form a union of function types, write `(A -> B) | C`. In a type, `|` is union; in an expression, `|` is bitwise OR. The context disambiguates.

### 5.2. Built-in types

| Type          | Meaning                                                       |
| ------------- | ------------------------------------------------------------- |
| `Any`         | The universal type. Opaque to user code.                      |
| `Never`       | The empty type. No values.                                    |
| `Unit` / `()` | The singleton type whose only value is `()`.                  |
| `Bool`        | `true` and `false`.                                           |
| `Int`         | Signed 64-bit two's-complement integer.                       |
| `Float`       | IEEE-754 binary64 floating-point value.                       |
| `Char`        | A Unicode scalar value (0 to 0x10FFFF, excluding surrogates). |
| `String`      | An immutable, valid UTF-8 byte sequence.                      |
| `Bytes`       | An immutable, arbitrary byte sequence.                        |
| `Array[T]`    | An immutable, fixed-size sequence of `T`.                     |
| `List[T]`     | An immutable, persistent sequence of `T`.                     |
| `Map[K, V]`   | An immutable, persistent map from hashable `K` to `V`.        |

`Array`, `List`, and `Map` are the only generic type constructors. Users cannot define new generic types.

### 5.3. Nominal types

`struct` and `enum` declarations introduce **nominal** types:

- Two struct types are equal only if they have the same declared name.
- Two enum types are equal only if they have the same declared name.
- There is no width subtyping: a struct with more fields is not a subtype of a struct with fewer fields.
- Nominal types are opaque outside their module? No: all top-level declarations are public (§8), so fields and variants are visible everywhere the type is visible.

### 5.4. Type aliases

```
type Name = Type
```

introduces a transparent alias. `Name` and `Type` are interchangeable everywhere. Aliases:

- cannot have type parameters;
- cannot be recursive without passing through a struct or enum;
- may refer to declarations that appear later in the module;
- are expanded before subtyping and equality checks.

The compiler builds an alias dependency graph and rejects cycles.

### 5.5. Recursive types

Recursion is allowed only through nominal types:

```
struct Node {
    value: Int,
    next: Node | Unit,
}

enum IntList {
    Nil,
    Cons(Int, IntList),
}
```

Mutually recursive structs and enums are allowed. An alias cycle that does not pass through a struct or enum is rejected with a diagnostic. For example, `type MaybeNode = Node | Unit` is allowed when `Node` is a struct that refers back to `MaybeNode`, because the recursion passes through the nominal type `Node`.

### 5.6. Subtyping

The subtyping relation is the smallest relation satisfying:

```
τ ≤ τ
Never ≤ τ
τ ≤ Any
τ₁ ≤ τ₁ | τ₂
τ₂ ≤ τ₁ | τ₂
(A | B) ≤ C        if A ≤ C and B ≤ C
(A₁, ..., Aₙ) -> R ≤ (B₁, ..., Bₙ) -> S
                    if Bᵢ ≤ Aᵢ for all i and R ≤ S
```

Additional rules:

- **Nominal types:** `N ≤ N`, `N ≤ Any`, and `N ≤ U` if `U` is a union with a member `M` such that `N ≤ M`. There is no other nominal subtyping.
- **Tuples:** `(A₁, ..., Aₙ) ≤ (B₁, ..., Bₙ)` if and only if `Aᵢ ≡ Bᵢ` for all `i`. No width subtyping.
- **Collections are invariant:** `Array[A] ≤ Array[B]` iff `A ≡ B`; likewise for `List` and for both parameters of `Map`.
- **Aliases** are expanded before applying these rules.
- **No implicit numeric conversions.** `Int` and `Float` are unrelated.

### 5.7. Union normalization

A union type is normalized as follows:

1. Flatten nested unions: `(A | B) | C` becomes `A | B | C`.
2. Remove duplicates.
3. Remove `Never`.
4. If any member is `Any`, the result is `Any`.
5. Sort the members by a canonical syntactic order.

Normalization makes type equality decidable and diagnostics stable.

### 5.8. Type equality

Two types are equal if each is a subtype of the other, after alias expansion and union normalization. In addition:

- nominal types are equal by name;
- tuples are equal structurally;
- function types are equal structurally, with equal parameter and result types;
- collections are equal when their arguments are equal.

### 5.9. Hashable types

A type is **hashable** if it is one of:

- `Bool`, `Int`, `Char`, `String`, `Bytes`;
- a tuple of hashable types;
- a struct whose fields are all hashable;
- an enum whose payload types are all hashable.

`Float`, `Any`, functions, `Array`, `List`, and `Map` are not hashable. `Map` keys must be hashable. `Float` is excluded because IEEE-754 equality is not reflexive for `NaN` and distinguishes `0.0` from `-0.0` in some contexts; using it as a map key would make the map's behavior surprising.

### 5.10. Numeric semantics

**Int**

- `Int` is a signed 64-bit two's-complement integer.
- `+`, `-`, and `*` panic on overflow.
- `/` truncates toward zero. Division by zero panics.
- `%` is the remainder with the sign of the dividend. Modulo by zero panics.
- Unary `-` panics on `Int` minimum value.
- `&`, `|`, `^`, and `~` operate on the 64-bit representation.
- `<<` and `>>` require a shift count in `0..63`; otherwise they panic. `>>` is an arithmetic shift (sign-extending). `<<` discards bits shifted out; it does not panic on overflow.
- Comparisons return `Bool`.

**Float**

- `Float` is IEEE-754 binary64.
- `+`, `-`, `*`, and `/` follow IEEE-754.
- `%` uses `fmod` semantics (truncated quotient).
- Comparisons follow IEEE-754: `NaN != NaN`, and `0.0 == -0.0`.
- `toFloat(i)` converts an `Int` to `Float`, rounding to nearest with ties to even.
- `toInt(f)` returns `Int | Unit`: it truncates toward zero, and returns `Unit` if `f` is `NaN`, infinite, or outside the representable `Int` range.
- `sqrt`, `pow`, `floor`, and `ceil` are built-ins. Basic arithmetic and comparison are bit-exact IEEE-754; transcendental functions may vary within 1 ulp across platforms (see §10.9).

### 5.11. Characters and strings

- `Char` is a Unicode scalar value. Values in the surrogate range (0xD800 to 0xDFFF) are invalid.
- `String` is an immutable sequence of bytes that is valid UTF-8.
- `String` length is measured in bytes.
- `byteAt(s, i)` returns the byte at index `i` as an `Int`, or `Unit` if out of range.
- `slice(s, start, end)` returns a substring by byte offsets. `start` and `end` must be valid UTF-8 boundaries and `0 ≤ start ≤ end ≤ len(s)`; otherwise the call panics.
- `charAt(s, i)` returns the `i`-th Unicode scalar as a `Char`, or `Unit` if out of range. It is O(n) in the byte length.
- `chars(s)` returns an `Array[Char]` of the code points.
- `compare(a, b)` compares two strings lexicographically by byte. UTF-8 preserves code-point order, so this is also lexicographic by code point.
- `toBytes(s)` returns the UTF-8 bytes. `fromBytes(b)` returns `String | Error`, validating UTF-8.
- String literals are valid UTF-8. `\u{...}` escapes encode the corresponding scalar.

---

## 6. Expressions and Semantics

### 6.1. Evaluation order

Evaluation is strict and left-to-right:

- function arguments are evaluated left-to-right;
- tuple, array, list, and map elements are evaluated left-to-right;
- struct fields are evaluated in source order;
- `if` evaluates the condition and then exactly one branch;
- `&&` and `||` short-circuit;
- `match` evaluates the scrutinee once and then exactly one arm;
- `let` evaluates its right-hand side once.

### 6.2. Blocks and `let`

A block is a sequence of items enclosed in `{ ... }`. Items are separated by newlines or `;`. The value of a block is the value of its last expression. If the block is empty, or its last item is a `let` binding, its value is `()`.

A `let` binding has the form:

```
let pattern: Type = expr
let pattern = expr
```

- The pattern must be **irrefutable**: a variable, `_`, a tuple of irrefutable patterns, or a struct pattern of irrefutable patterns.
- The binding is immutable.
- Its scope extends to the end of the enclosing block.
- Shadowing is allowed: a later binding with the same name shadows an earlier one in the same block, and inner blocks may shadow outer bindings.
- A type annotation applies to the whole pattern. For a simple variable, it is the variable's type. For a destructuring pattern, the annotation is the type of the right-hand side.
- The compiler checks that the pattern matches the right-hand side's type and binds the variables with their component types.

Examples:

```idem
let x: Int = 1
let (a, b) = (1, "two")
let Point { x, y } = p
let _ = sideEffectFreeComputation()
```

### 6.3. `if`

```
if condition then thenBranch else elseBranch
```

- `condition` must have type `Bool`.
- `thenBranch` and `elseBranch` must be expressions.
- The `else` branch is mandatory.
- The type of the expression is `τ_then ⊔ τ_else`.
- Only the selected branch is evaluated.

If one branch has type `Never`, the result type is the other branch's type.

### 6.4. `match`

```
match scrutinee {
    pattern => expression,
    ...
}
```

- The scrutinee is evaluated once.
- Arms are tried top to bottom; the first matching arm is evaluated.
- The match must be exhaustive (§7.4).
- The result type is the join of the arm expression types. Arms of type `Never` do not contribute.
- There are no guards.
- Unreachable arms are compile-time errors.
- A struct or map literal used directly as the scrutinee must be parenthesized, because `{` starts the match arms: `match (Point { x: 1, y: 2 }) { ... }`.

### 6.5. Functions

Named functions are declared at the top level:

```idem
fn add(a: Int, b: Int) -> Int = a + b
```

Anonymous functions are expressions:

```idem
fn(x: Int) -> Int = x + 1
```

- Parameter types and the return type are mandatory.
- Function arity is fixed.
- Functions are first-class values.
- Closures capture variables by value. Because all user bindings are immutable, there is no mutable capture.
- Named functions may be recursive and mutually recursive at the top level.
- Anonymous functions cannot refer to themselves; there is no `let rec`.
- There is no implicit currying. Partial application is written with an explicit lambda.
- Tail calls are not guaranteed to be optimized.

Function application:

```idem
f(arg1, arg2)
```

The callee is evaluated first, then the arguments left-to-right. The callee must have a function type with matching arity; each argument must be a subtype of the corresponding parameter type; the result type is the function's result type.

### 6.6. Structs

A struct declaration introduces a nominal product type:

```idem
struct Point {
    x: Int,
    y: Int,
}
```

Construction:

```idem
Point { x: 1, y: 2 }
```

- Every field must be provided exactly once.
- Field order in the literal may differ from the declaration order.
- Field access uses `.`:

```idem
p.x
```

- Structs are immutable. There is no field assignment and no `with` update syntax; construct a new struct instead.

### 6.7. Enums

An enum declaration introduces a nominal sum type:

```idem
enum Shape {
    Circle(Float),
    Rectangle(Float, Float),
}
```

Construction:

```idem
Circle(2.0)
Rectangle(1.0, 2.0)
```

- A variant with no payload is written without parentheses: `None`.
- A variant with multiple payload values is written with a parenthesized list.
- Variant constructors are unqualified top-level value names. Two enums in scope may not declare variants with the same name; use distinct variant names.
- Enum values are consumed with `match`. Patterns are resolved against the scrutinee's enum type, so they are unambiguous even when several enums are in scope.

### 6.8. Tuples

Tuple construction:

```idem
(1, "two")
()
```

- A tuple has arity at least 2. `()` is the unit value.
- Parentheses around a single expression are grouping, not a one-tuple.
- Tuples are destructured with `let` or `match`; there is no `.0` field access.

### 6.9. Arrays, lists, and maps

**Arrays**

```idem
let xs: Array[Int] = [1, 2, 3]
```

- `[e1, e2, ...]` constructs an `Array[T]`.
- All elements must have the same type `T`.
- `[]` requires an expected type.
- Arrays have fixed size.
- Indexing `xs[i]` is sugar for `get(xs, i)` and has type `T | Unit`.
- `set(xs, i, v)` returns a new array; it panics if `i` is out of bounds.
- `concat`, `map`, `filter`, `fold`, and `sum` are built-ins.

**Lists**

```idem
let xs: List[Int] = List[1, 2, 3]
```

- `List[e1, e2, ...]` constructs a `List[T]`.
- All elements must have the same type.
- `List[]` requires an expected type.
- Lists are persistent and immutable.
- `push(xs, v)` returns a new list.
- `get`, `concat`, `map`, `filter`, `fold`, and `sum` are built-ins.

**Maps**

```idem
let m: Map[String, Int] = Map{ "one": 1, "two": 2 }
```

- `Map{ k1: v1, ... }` constructs a `Map[K, V]`.
- All keys must have the same hashable type `K`; all values must have the same type `V`.
- `Map{}` requires an expected type.
- Entries are evaluated left-to-right and inserted in order. If a later entry has a key equal to an earlier one, the later value replaces it and the earlier position is preserved.
- Maps preserve insertion order. Replacing an existing key keeps its position; removing a key removes its entry.
- `set(m, k, v)` returns a new map; `remove(m, k)` returns a new map.
- `keys(m)` and `values(m)` return arrays in insertion order.
- `get(m, k)` returns `V | Unit`.

### 6.10. `Any`

`Any` is the universal type and is **opaque** to user code:

- Every value can be widened to `Any`. This is an implicit coercion.
- User code may store, pass, and return `Any` values.
- User code may not inspect the underlying type, compare `Any` values, cast them, or use type patterns on them.
- Built-ins may inspect the underlying representation. For example, `print`, `println`, and `toString` can render any value.

### 6.11. `Never`, `panic`, and `exit`

`Never` is the empty type. An expression of type `Never` never produces a value. It can be used at any type.

The built-ins `panic(message: String)` and `exit(code: Int)` have type `Never`:

- `panic` flushes standard output, writes `panic: <message>` to stderr, and terminates the process with status 101.
- `exit` flushes standard output and terminates the process with status `code & 0xFF`.

A `match` on a `Never` value may have zero arms.

### 6.12. Operators

Operators are built-in and cannot be overloaded by users. The semantics are:

- **Arithmetic** (`+`, `-`, `*`, `/`, `%`): both operands must have the same type, either `Int` or `Float`.
- **Comparison** (`==`, `!=`, `<`, `<=`, `>`, `>=`): see §6.13.
- **Logical** (`&&`, `||`, `!`): operands must be `Bool`. `&&` and `||` short-circuit.
- **Bitwise** (`&`, `|`, `^`, `~`, `<<`, `>>`): operands must be `Int`.
- **Unary minus** (`-`): `Int` or `Float`.

There is no `+` for strings; use `concat`.

### 6.13. Equality and ordering

**Equality** (`==`, `!=`) is structural and is defined for:

- `Bool`, `Int`, `Float`, `Char`, `String`, `Bytes`;
- tuples of comparable types;
- structs and enums all of whose fields/payloads are comparable;
- unions all of whose members are comparable;
- `Array[T]`, `List[T]`, and `Map[K, V]` when their element/key/value types are comparable.

Functions and `Any` are not comparable. For `Float`, `NaN != NaN` and `0.0 == -0.0`. For `Map`, equality is order-insensitive: two maps are equal if they have the same key-value pairs. For `Array` and `List`, equality is element-wise in order.

**Ordering** (`<`, `<=`, `>`, `>=`) is defined for:

- `Int`, `Float`, `Char`, `String`, `Bytes`;
- tuples of orderable types, compared lexicographically.

Structs, enums, collections, functions, and `Any` have no built-in ordering. Users can compare fields explicitly.

### 6.14. Typing rules

The type checker is bidirectional: it can **infer** the type of an expression or **check** an expression against an expected type.

Selected rules:

- **Variable:** `Γ(x) = τ` implies `Γ ⊢ x : τ`.
- **Literal:** integer literals have type `Int`; float literals have type `Float`; boolean, character, string, and byte literals have their obvious types.
- **`let`:** infer the right-hand side or check it against the annotation; check the pattern; extend the context.
- **`if`:** check the condition against `Bool`; infer both branches and take their join, or check both against the expected type.
- **`match`:** infer the scrutinee type; check each pattern against it; infer/check each arm; take the join; check exhaustiveness.
- **Application:** infer the callee; it must have a function type; check arity and arguments; result is the function's result type.
- **Lambda:** parameters and return type are explicit; check the body against the return type.
- **Struct literal:** the named struct must exist; every field must be present exactly once; each field expression is checked against the field type.
- **Field access:** the receiver must have a struct type with that field.
- **Enum constructor:** the variant must exist; payload arity and types must match.
- **Tuple:** infer/check each component; result is the tuple type.
- **Array/list literal:** infer a common element type or check against the expected element type; empty literals require an expected type.
- **Map literal:** infer/check keys and values; keys must be hashable; empty maps require an expected type.
- **Indexing:** `a[i]` requires `a` to be `Array[T]` or `List[T]`; result is `T | Unit`. Strings and bytes use `byteAt`.
- **Operators:** as described in §6.12 and §6.13.

**Coercions.** The only implicit coercions are:

- `Never` to any type;
- `T` to `T | U` (union injection);
- `T` to `Any` (opaque widening).

Union injection and `Any` widening are represented at runtime with tags. There are no implicit numeric conversions and no implicit conversions between named types.

**Inference.** Type inference is local and bidirectional. There is no Hindley-Milner generalization. Empty collection literals, lambda parameters, and some overloaded built-ins require an expected type. When the expected type is insufficient, the compiler reports an error asking for an annotation.

---

## 7. Patterns

### 7.1. Pattern forms

- `_` matches any value and binds nothing.
- `x` matches any value and binds it to `x`.
- A literal matches a value equal to that literal.
- `()` matches the unit value.
- `(p₁, ..., pₙ)` matches a tuple of arity `n`.
- `Name { field: p, ... }` matches a struct. Every field must be mentioned; use `_` to ignore a field. Shorthand `Name { x, y }` binds fields `x` and `y`.
- `Variant` matches an enum variant with no payload.
- `Variant(p₁, ..., pₙ)` matches an enum variant with payload.
- `x: T` matches a value of a union member `T` and binds it to `x` with type `T`. It is valid only when the scrutinee type is a union containing `T`, and never on `Any`.

There are no or-patterns, range patterns, or guards in version 0.1.

### 7.2. Irrefutable patterns

A pattern is **irrefutable** if it always matches a value of its type:

- `_` and variable patterns are irrefutable;
- `()` is irrefutable for `Unit`;
- a tuple of irrefutable patterns is irrefutable;
- a struct pattern is irrefutable if all its field patterns are irrefutable.

Enum, union type, and literal patterns are refutable. `let` accepts only irrefutable patterns; `match` accepts all patterns.

### 7.3. Typing patterns

A pattern is checked against a scrutinee type:

- `_` and variables accept any type; a variable binds the scrutinee type.
- A literal pattern requires the scrutinee type to be the literal's type.
- A tuple pattern requires a tuple type of the same arity.
- A struct pattern requires the named struct type; each field pattern is checked against the field type.
- An enum pattern requires the named enum type; payload patterns are checked against the variant's payload types.
- A type pattern `x: T` requires the scrutinee type to be a union containing `T`; `x` has type `T`.

### 7.4. Exhaustiveness

A `match` is exhaustive if every value of the scrutinee type is covered by at least one arm. The compiler checks:

- `Bool`: `true` and `false` must both be covered, or a variable/`_` must appear.
- Enum: every variant must be covered. For variants with payload, the payload patterns must themselves be exhaustive, or a variable/`_` must cover the payload.
- Union: every member must be covered by a type pattern or by a variable/`_`.
- Tuple/struct: the pattern is exhaustive if its component patterns are exhaustive.
- `Int`, `Float`, `Char`, `String`, `Bytes`: a variable/`_` is required, because literals do not cover the entire type.
- `Any`: only a variable/`_` is allowed; type patterns are forbidden. A variable/`_` is exhaustive.
- `Never`: a match with zero arms is exhaustive.

An arm is **unreachable** if every value it matches is already covered by previous arms. Unreachable arms are compile-time errors.

### 7.5. Matching semantics

Arms are tried in source order. The first matching arm is selected. Pattern variables are bound to the corresponding subvalues. The selected arm's expression is evaluated in the extended context. No other arm is evaluated.

---

## 8. Declarations and Modules

### 8.1. Program structure

A program consists of a **root module** and the modules it imports. Each `.idem` file is a module. Imports are written at the top level and may appear in any order relative to other top-level declarations:

```idem
import "math.idem"
import "geometry/shapes.idem"
```

- Paths are relative to the importing file.
- Import cycles are compile-time errors.
- Importing the same file more than once loads it once.
- All top-level declarations are public.
- Declaration order within a module does not matter; the resolver collects all top-level names before type checking.
- The root module must define exactly one `main`. Imported modules must not define `main`.
- Package management, versioning, and manifests are outside version 0.1.

### 8.2. Top-level declarations

The only top-level declarations are:

- `type` aliases;
- `struct` declarations;
- `enum` declarations;
- `fn` declarations;
- `import` directives.

There are no top-level `let` bindings and no top-level executable expressions.

### 8.3. Type declarations

See §5.3–§5.5. Type aliases are transparent and non-recursive. Structs and enums are nominal and may be recursive.

### 8.4. Function declarations

- User functions cannot be overloaded. Duplicate top-level names are errors.
- Built-in names are reserved and cannot be redefined.
- Top-level functions may be mutually recursive.
- A function's parameter and return types are mandatory.
- A function body is an expression.

### 8.5. Entry point

Every program must define:

```idem
fn main(args: Array[String]) -> Unit = ...
```

The runtime calls `main` with an `Array[String]` containing the program arguments. `args[0]` is the program name; subsequent elements are the command-line arguments. Normal completion exits with status 0.

### 8.6. Name resolution

- Top-level names are visible throughout their module and in importing modules.
- Local `let` bindings are lexically scoped from the point of binding to the end of the enclosing block.
- Inner scopes may shadow outer user-defined value names.
- A `let` binding may shadow an earlier user-defined binding in the same block.
- A local binding may not shadow a type name.
- Reserved built-in and prelude names cannot be shadowed by local bindings.
- There is no forward reference to local names.
- Top-level type names and value names must be distinct; a type and a value may not share a name.
- Undefined names, duplicate top-level names, and ambiguous imports are compile-time errors.

### 8.7. Imports and name collisions

Because all declarations are public and imports are unqualified, two imported modules that export the same top-level name cause a compile-time error at the import site. Version 0.1 does not provide qualified names or import aliases. Programs should use distinct names across modules.

---

## 9. Built-ins

### 9.1. Overview

Built-ins are privileged functions implemented by the compiler and runtime. They:

- use flat names and are reserved;
- may be overloaded by type;
- may use internal mutation, I/O, or non-determinism;
- are the only source of effects in the language;
- are documented in this section and in the prelude.

The compiler does not track effects. A user function that calls `print` is impure, but its type does not record that fact. Purity is a discipline, not a static guarantee.

Built-ins fall into four categories:

- **Pure:** semantically pure, even if implemented with internal mutation for performance (e.g., `sum`).
- **Effectful:** produce observable side effects (e.g., `print`).
- **Input-dependent:** their result depends on the outside world (e.g., `readLine`).
- **Non-deterministic:** their result is not a function of their arguments or declared inputs (e.g., `random`, `now`).

### 9.2. Catalog

#### I/O and process

| Built-in    | Signature                           | Semantics                                                                                             |
| ----------- | ----------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `print`     | `(Any) -> Unit`                     | Writes the canonical representation of the value to stdout, without a newline.                        |
| `println`   | `(Any) -> Unit`                     | Same as `print`, followed by `\n`.                                                                    |
| `readLine`  | `() -> String \| Unit`              | Reads one line from stdin, strips the trailing `\n` and optional `\r`. Returns `Unit` at end of file. |
| `readFile`  | `(String) -> String \| Error`       | Reads a file as UTF-8. Returns an `Error` on failure or invalid UTF-8.                                |
| `writeFile` | `(String, String) -> Unit \| Error` | Creates or truncates a file and writes the UTF-8 contents.                                            |
| `flush`     | `() -> Unit`                        | Flushes stdout.                                                                                       |
| `exit`      | `(Int) -> Never`                    | Flushes stdout and exits with `code & 0xFF`.                                                          |
| `panic`     | `(String) -> Never`                 | Writes `panic: <message>\n` to stderr and exits with status 101.                                      |

#### Strings and bytes

| Built-in    | Signature                                                     | Semantics                                                                                 |
| ----------- | ------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| `len`       | `(String) -> Int`, `(Bytes) -> Int`                           | Length in bytes.                                                                          |
| `byteAt`    | `(String, Int) -> Int \| Unit`, `(Bytes, Int) -> Int \| Unit` | Byte at the given index, or `Unit` if out of range.                                       |
| `slice`     | `(String, Int, Int) -> String`, `(Bytes, Int, Int) -> Bytes`  | Subsequence by byte offsets. Invalid ranges panic. String boundaries must be valid UTF-8. |
| `concat`    | `(String, String) -> String`, `(Bytes, Bytes) -> Bytes`       | Concatenation.                                                                            |
| `compare`   | `(String, String) -> Int`, `(Bytes, Bytes) -> Int`            | Lexicographic comparison by byte; negative, zero, or positive.                            |
| `charAt`    | `(String, Int) -> Char \| Unit`                               | The `i`-th Unicode scalar, or `Unit` if out of range.                                     |
| `chars`     | `(String) -> Array[Char]`                                     | The Unicode scalars of the string.                                                        |
| `toInt`     | `(String) -> Int \| Unit`                                     | Parses a decimal integer. Returns `Unit` on failure or overflow.                          |
| `toFloat`   | `(String) -> Float \| Unit`                                   | Parses a decimal float. Returns `Unit` on failure.                                        |
| `toString`  | `(Any) -> String`                                             | Canonical textual representation.                                                         |
| `toBytes`   | `(String) -> Bytes`                                           | UTF-8 bytes.                                                                              |
| `fromBytes` | `(Bytes) -> String \| Error`                                  | Validates UTF-8 and returns a string, or `InvalidUtf8`.                                   |

#### Collections

| Built-in | Signature                                                                                        | Semantics                                                   |
| -------- | ------------------------------------------------------------------------------------------------ | ----------------------------------------------------------- |
| `len`    | `(Array[T]) -> Int`, `(List[T]) -> Int`, `(Map[K, V]) -> Int`                                    | Number of elements/entries.                                 |
| `get`    | `(Array[T], Int) -> T \| Unit`, `(List[T], Int) -> T \| Unit`, `(Map[K, V], K) -> V \| Unit`     | Element at an index/key, or `Unit`.                         |
| `set`    | `(Array[T], Int, T) -> Array[T]`, `(Map[K, V], K, V) -> Map[K, V]`                               | Returns a new collection. Array index out of bounds panics. |
| `remove` | `(Map[K, V], K) -> Map[K, V]`                                                                    | Returns a new map without the key.                          |
| `push`   | `(List[T], T) -> List[T]`                                                                        | Returns a new list with the element appended.               |
| `concat` | `(Array[T], Array[T]) -> Array[T]`, `(List[T], List[T]) -> List[T]`                              | Concatenation.                                              |
| `map`    | `(Array[T], T -> U) -> Array[U]`, `(List[T], T -> U) -> List[U]`                                 | Applies the function to each element.                       |
| `filter` | `(Array[T], T -> Bool) -> Array[T]`, `(List[T], T -> Bool) -> List[T]`                           | Keeps elements for which the predicate is true.             |
| `fold`   | `(Array[T], U, (U, T) -> U) -> U`, `(List[T], U, (U, T) -> U) -> U`                              | Left fold.                                                  |
| `sum`    | `(Array[Int]) -> Int`, `(Array[Float]) -> Float`, `(List[Int]) -> Int`, `(List[Float]) -> Float` | Sum of elements.                                            |
| `keys`   | `(Map[K, V]) -> Array[K]`                                                                        | Keys in insertion order.                                    |
| `values` | `(Map[K, V]) -> Array[V]`                                                                        | Values in insertion order.                                  |

#### Mathematics

| Built-in | Signature                                      | Semantics                                                                                    |
| -------- | ---------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `abs`    | `(Int) -> Int`, `(Float) -> Float`             | Absolute value. `abs(Int_MIN)` panics.                                                       |
| `min`    | `(Int, Int) -> Int`, `(Float, Float) -> Float` | Minimum. For floats, if one operand is NaN, returns the other; if both are NaN, returns NaN. |
| `max`    | `(Int, Int) -> Int`, `(Float, Float) -> Float` | Maximum. For floats, if one operand is NaN, returns the other; if both are NaN, returns NaN. |
| `sqrt`   | `(Float) -> Float`                             | Square root. Negative inputs produce NaN.                                                    |
| `floor`  | `(Float) -> Float`                             | Largest integral value not greater than the input.                                           |
| `ceil`   | `(Float) -> Float`                             | Smallest integral value not less than the input.                                             |
| `pow`    | `(Float, Float) -> Float`                      | Exponentiation, following C `pow` semantics.                                                 |

#### Non-deterministic

| Built-in | Signature   | Semantics                                                  |
| -------- | ----------- | ---------------------------------------------------------- |
| `random` | `() -> Int` | A uniformly distributed 64-bit integer. Non-deterministic. |
| `now`    | `() -> Int` | Milliseconds since the Unix epoch. Non-deterministic.      |

### 9.3. Errors

The prelude defines a nominal enum:

```idem
enum Error {
    NotFound,
    PermissionDenied,
    InvalidUtf8,
    Io(String),
}
```

`Error` is an ordinary enum and can be used in unions:

```idem
type ReadResult = String | Error
```

The name `Error` and its constructors are prelude names. They cannot be redefined at the top level or shadowed by local bindings.

Operating-system errors are mapped to `NotFound`, `PermissionDenied`, or `Io(message)`. The exact mapping is implementation-defined but must be stable for a given platform.

### 9.4. Canonical rendering

`print`, `println`, and `toString` use the same canonical rendering:

- `Unit`: `()`
- `Bool`: `true` or `false`
- `Int`: decimal, with a leading `-` for negative values
- `Float`: the shortest decimal representation that round-trips to the same binary64 value, with a leading `-` for negative values; `NaN`, `inf`, and `-inf` for non-finite values; `-0.0` for negative zero
- `Char`: the character itself, encoded as UTF-8
- `String`: the raw string contents, without quotes
- `Bytes`: `b"..."` with printable ASCII as-is and `\xNN` for other bytes
- Tuple: `(v₁, v₂, ...)`
- Struct: `Name { field: value, ... }` in declaration order
- Enum: `Variant` or `Variant(payload, ...)`
- Array: `[v₁, v₂, ...]`
- List: `List[v₁, v₂, ...]`
- Map: `Map{ key: value, ... }` in insertion order
- Function: `<function>`

`Any` values are rendered according to their underlying value.

### 9.5. Overload resolution

Built-ins with multiple signatures are resolved using the static types of the arguments:

- The argument types must match exactly one overload, after alias expansion and union normalization.
- If an argument has a union type, the call is ambiguous unless the union is first eliminated with `match`.
- If no overload matches, the call is a type error.
- If more than one overload matches, the call is a type error (ambiguity).

The polymorphic built-ins `map`, `filter`, and `fold` are not overloaded. Their type parameters are inferred from the collection type and the function argument. If the collection type is a union, the call is a type error; eliminate the union first.

### 9.6. Reserved names

Built-in names are reserved. A top-level declaration or local binding may not redefine a built-in name. The compiler reports a diagnostic if it encounters such a definition.

---

## 10. Runtime and Memory

### 10.1. Value representation

The reference compiler uses a uniform, boxed representation:

```c
typedef struct Value {
    uint32_t tag;
    union {
        int64_t i;
        double f;
        uint32_t c;
        void *obj;
    } as;
} Value;
```

`tag` identifies the runtime type: unit, bool, int, float, char, string, bytes, array, list, map, struct, enum, closure, and so on. Heap objects carry a header with at least a mark bit, a type tag, and a size.

`Any` values use the underlying value's tag. Union values also use tags to distinguish their members. These tags are representation details, not user-visible type values.

### 10.2. Garbage collection

The runtime uses a precise **mark-sweep** collector:

- roots are the shadow stack, global runtime roots, and temporary roots held by the collector;
- the mark phase traces all reachable heap objects;
- the sweep phase returns unmarked objects to a free list;
- the collector never moves objects;
- there are no finalizers, weak references, or user-visible collection points;
- allocation failure aborts with a deterministic panic.

The compiler maintains a shadow stack of live `Value` roots so that the collector can find them without scanning the C stack.

### 10.3. Heap objects

- **String:** length in bytes plus a byte buffer; always valid UTF-8.
- **Bytes:** length plus a byte buffer.
- **Array:** length plus an array of `Value`.
- **List:** a persistent immutable sequence; the implementation may use a linked list, a vector, or a tree.
- **Map:** a persistent insertion-ordered map with a hash index; keys are hashable values.
- **Struct:** a type identifier plus fields in declaration order.
- **Enum:** a type identifier, a variant index, and payload values.
- **Closure:** a function pointer plus captured `Value`s.

### 10.4. Program startup and shutdown

The runtime:

1. initializes the heap and collector;
2. builds an `Array[String]` from `argv` (`args[0]` is the program name);
3. calls `main(args)`;
4. flushes stdout;
5. exits with status 0.

`exit(code)` flushes stdout and exits with `code & 0xFF`. `panic(message)` writes `panic: <message>\n` to stderr and exits with status 101.

### 10.5. Determinism caveats

Idem is deterministic by default, but the following are explicit exceptions:

- `random` and `now` are non-deterministic.
- File I/O depends on the filesystem and the environment.
- Basic IEEE-754 arithmetic and comparison are bit-exact on platforms where `double` is IEEE-754 binary64. Transcendental built-ins (`sqrt`, `pow`, `floor`, `ceil`) may vary within 1 ulp across platforms.
- Garbage collection timing and memory layout are not observable by the program.

Everything else—evaluation order, integer arithmetic, string operations, collection operations, pattern matching, and error behavior—is deterministic.

### 10.6. C89 constraints

The reference runtime and generated code are strict C89:

- no `//` comments;
- no declarations after statements;
- no variable-length arrays;
- no `long long`, `stdint.h`, `inline`, designated initializers, or compound literals;
- 64-bit integers are implemented with two 32-bit words or with a platform `long` verified to be 64 bits;
- `double` is assumed to be IEEE-754 binary64; platforms where it is not are unsupported for exact `Float` semantics;
- `size_t` comes from `stddef.h`;
- the runtime and compiler use only the C89 standard library plus the platform's C compiler.

---

## 11. Compiler

### 11.1. Reference implementation

The reference compiler is written in strict C89 and organized as a modular pipeline:

1. **Lexer:** converts UTF-8 source into tokens with source spans.
2. **Parser:** recursive-descent parser that builds an AST.
3. **Resolver:** collects top-level names, resolves imports, expands aliases, builds struct/enum/function signatures, and detects cycles.
4. **Type checker:** bidirectional inference/checking, union normalization, subtyping, overload resolution, and exhaustiveness checking.
5. **Lowering:** inserts coercions and tags, converts closures, compiles pattern matching into decision trees, and produces a typed IR.
6. **Code generator:** emits readable C89 with boxed values, GC calls, and a shadow stack.
7. **Driver:** invokes the system C compiler and runs the resulting program.

### 11.2. Command-line interface

Version 0.1 provides a single command:

```
idem file.idem
```

The compiler:

1. parses and type-checks `file.idem`;
2. emits a C89 translation unit;
3. invokes the system C compiler (the `CC` environment variable, or `cc` by default);
4. runs the resulting executable with the remaining command-line arguments;
5. forwards the program's exit status.

Compile errors are printed to stderr and cause exit status 1. Usage errors cause exit status 2. A program that calls `panic` exits with 101.

The driver may keep the generated C file for debugging when the `IDEM_KEEP_C` environment variable is set. Additional flags such as `--check` and `--emit-c` may be added in later versions; they are not part of the 0.1 interface.

### 11.3. Diagnostics

Diagnostics use the format:

```
path:line:column: error: message
path:line:column: warning: message
```

Line and column numbers are 1-based. Columns count Unicode scalar values, not bytes. The compiler reports the primary source location and, when useful, secondary notes.

Compilation stops after the first phase that produces errors. The compiler does not emit C code if there are errors.

### 11.4. Generated C89

The generated C89 is intended to be readable:

- one C function per Idem function, with mangled names;
- explicit `Value` construction and destruction-free GC allocation;
- a shadow-stack push/pop around live roots;
- readable control flow for `if`, `match`, and blocks;
- no compiler-specific extensions.

The generated file is not a stable interface and may change between compiler versions.

### 11.5. Build and tests

The repository uses a `Makefile`:

```
make        # builds the idem compiler
make test   # runs unit tests and golden tests
make clean
```

Golden tests are `.idem` programs with expected stdout, stderr, and exit status. Unit tests cover the lexer, parser, resolver, type checker, and selected runtime operations. The acceptance tests compile and run the programs in `examples/`.

### 11.6. Bootstrap and self-hosting

The C89 compiler is the reference implementation for version 0.1. Self-hosting is a future goal, to be evaluated after the language and compiler stabilize. The project does not depend on a bootstrap compiler written in another language.

### 11.7. Repository layout and license

```
src/        C89 compiler sources
runtime/    C89 runtime and GC
tests/      unit tests and golden tests
examples/   example Idem programs
docs/       language.md and compiler.md
private/    working notes and decision log
Makefile
LICENSE
```

The project is released under the MIT License.

---

## 12. Conformance

### 12.1. Conformance suite

A conforming implementation must pass the conformance suite, which includes:

- lexical and syntactic acceptance/rejection tests;
- type-checking tests, including joins, unions, subtyping, and exhaustiveness;
- runtime tests for integers, floats, strings, collections, structs, enums, and pattern matching;
- error tests for division by zero, overflow, out-of-bounds access, and panics;
- module tests for imports and cycles;
- built-in tests for I/O, collections, and non-deterministic functions (with controlled expectations).

### 12.2. Acceptance for version 0.1

Version 0.1 is accepted when:

- the compiler builds with a strict C89 compiler;
- `examples/hello.idem`, `examples/factorial.idem`, `examples/fibonacci.idem`, `examples/shapes.idem`, and `examples/collections.idem` compile and produce the expected output;
- type errors are reported with file, line, and column;
- `make test` passes.

### 12.3. Versioning

The language specification and the reference compiler are versioned independently:

- the language version follows semantic versioning (`MAJOR.MINOR.PATCH`);
- a major version may break source compatibility;
- a minor version adds backward-compatible features;
- a patch version fixes bugs without changing the language.

The current document is version `0.1.0-draft`.

---

## Appendix A. Grammar Summary

```
program     ::= (import | decl)*
import      ::= 'import' string-lit
decl        ::= type-decl | struct-decl | enum-decl | fn-decl

type-decl   ::= 'type' ident '=' type
struct-decl ::= 'struct' ident '{' field (',' field)* ','? '}'
field       ::= ident ':' type
enum-decl   ::= 'enum' ident '{' variant (',' variant)* ','? '}'
variant     ::= ident ('(' type (',' type)* ')')?
fn-decl     ::= 'fn' ident '(' param-list? ')' '->' type '=' expr
param-list  ::= param (',' param)*
param       ::= ident ':' type

type        ::= union-type
union-type  ::= arrow-type ('|' arrow-type)*
arrow-type  ::= tuple-type ('->' type)?
tuple-type  ::= '()' | '(' type (',' type)+ ')' | primary-type
primary-type ::= 'Any' | 'Never' | 'Unit' | 'Bool' | 'Int' | 'Float'
               | 'Char' | 'String' | 'Bytes'
               | ident ('[' type (',' type)* ']')?

expr        ::= if-expr | match-expr | fn-expr | block | binary-expr
if-expr     ::= 'if' expr 'then' expr 'else' expr
match-expr  ::= 'match' expr '{' (match-arm (',' match-arm)* ','?)? '}'
match-arm   ::= pattern '=>' expr
fn-expr     ::= 'fn' '(' param-list? ')' '->' type '=' expr
block       ::= '{' block-item* '}'
block-item  ::= let-binding | expr
let-binding ::= 'let' pattern (':' type)? '=' expr

binary-expr ::= unary-expr (bin-op unary-expr)*
bin-op      ::= '+' | '-' | '*' | '/' | '%'
              | '==' | '!=' | '<' | '<=' | '>' | '>='
              | '&&' | '||'
              | '&' | '|' | '^' | '<<' | '>>'
unary-expr  ::= ('!' | '-' | '~') unary-expr | postfix-expr
postfix-expr ::= primary-expr postfix*
postfix     ::= '.' ident | '[' expr ']' | '(' arg-list? ')'
arg-list    ::= expr (',' expr)*
primary-expr ::= literal | ident | '(' ')' | '(' expr ')'
               | '(' expr ',' expr (',' expr)* ')'
               | struct-lit | array-lit | list-lit | map-lit
               | if-expr | match-expr | fn-expr | block
struct-lit  ::= ident '{' field-init (',' field-init)* ','? '}'
field-init  ::= ident ':' expr
array-lit   ::= '[' (expr (',' expr)*)? ']'
list-lit    ::= 'List' '[' (expr (',' expr)*)? ']'
map-lit     ::= 'Map' '{' (map-entry (',' map-entry)*)? ','? '}'
map-entry   ::= expr ':' expr

pattern     ::= '_' | ident | literal | '()'
              | '(' pattern (',' pattern)+ ')'
              | ident '{' pattern-field (',' pattern-field)* ','? '}'
              | ident ('(' pattern (',' pattern)* ')')?
              | ident ':' type
pattern-field ::= ident ':' pattern | ident
```

## Appendix B. Operator Precedence

| Level | Operators          | Associativity   |
| ----- | ------------------ | --------------- |
| 1     | `\|\|`             | left            |
| 2     | `&&`               | left            |
| 3     | `==` `!=`          | non-associative |
| 4     | `<` `<=` `>` `>=`  | non-associative |
| 5     | `\|`               | left            |
| 6     | `^`                | left            |
| 7     | `&`                | left            |
| 8     | `<<` `>>`          | left            |
| 9     | `+` `-`            | left            |
| 10    | `*` `/` `%`        | left            |
| 11    | `!` `-` `~`        | prefix          |
| 12    | call, field, index | left            |

## Appendix C. Built-in Catalog Summary

This appendix is a quick reference. The normative overload sets and semantics are in §9.2.

```
print       (Any) -> Unit
println     (Any) -> Unit
readLine    () -> String | Unit
readFile    (String) -> String | Error
writeFile   (String, String) -> Unit | Error
flush       () -> Unit
exit        (Int) -> Never
panic       (String) -> Never

len         (String | Bytes | Array[T] | List[T] | Map[K,V]) -> Int
get         (Array[T] | List[T], Int) -> T | Unit
get         (Map[K,V], K) -> V | Unit
set         (Array[T], Int, T) -> Array[T]
set         (Map[K,V], K, V) -> Map[K,V]
remove      (Map[K,V], K) -> Map[K,V]
push        (List[T], T) -> List[T]
concat      (String, String) -> String
concat      (Bytes, Bytes) -> Bytes
concat      (Array[T], Array[T]) -> Array[T]
concat      (List[T], List[T]) -> List[T]
map         (Array[T] | List[T], T -> U) -> Array[U] | List[U]
filter      (Array[T] | List[T], T -> Bool) -> Array[T] | List[T]
fold        (Array[T] | List[T], U, (U, T) -> U) -> U
sum         (Array[Int] | Array[Float] | List[Int] | List[Float]) -> Int | Float
keys        (Map[K,V]) -> Array[K]
values      (Map[K,V]) -> Array[V]

byteAt      (String, Int) -> Int | Unit
byteAt      (Bytes, Int) -> Int | Unit
slice       (String, Int, Int) -> String
slice       (Bytes, Int, Int) -> Bytes
compare     (String, String) -> Int
compare     (Bytes, Bytes) -> Int
charAt      (String, Int) -> Char | Unit
chars       (String) -> Array[Char]
toInt       (String) -> Int | Unit
toFloat     (String) -> Float | Unit
toString    (Any) -> String
toBytes     (String) -> Bytes
fromBytes   (Bytes) -> String | Error

abs         (Int) -> Int
abs         (Float) -> Float
min         (Int, Int) -> Int
min         (Float, Float) -> Float
max         (Int, Int) -> Int
max         (Float, Float) -> Float
sqrt        (Float) -> Float
floor       (Float) -> Float
ceil        (Float) -> Float
pow         (Float, Float) -> Float

random      () -> Int
now         () -> Int
```

## Appendix D. Glossary

- **Alias:** `type Name = Type`; a transparent name for another type.
- **Array:** a fixed-size immutable sequence.
- **Built-in:** a privileged function provided by the compiler/runtime.
- **Char:** a Unicode scalar value.
- **Coercion:** an implicit type conversion inserted by the compiler (union injection, `Any` widening, `Never` elimination).
- **Enum:** a nominal sum type with tagged variants.
- **Float:** IEEE-754 binary64.
- **Hashable:** a type whose values can be used as `Map` keys.
- **Int:** signed 64-bit two's-complement integer.
- **Join:** the least upper bound of two types.
- **List:** a persistent immutable sequence.
- **Map:** a persistent insertion-ordered map.
- **Never:** the empty type.
- **Nominal:** a type whose identity is its declared name.
- **Struct:** a nominal product type with named fields.
- **Subtype:** a type whose values are included in another type.
- **Union:** a type `A | B` denoting the union of the sets `A` and `B`.
- **Unit:** the singleton type whose only value is `()`.
