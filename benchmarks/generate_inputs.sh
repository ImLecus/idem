#!/bin/sh

# Generate synthetic benchmark inputs.  All generated files live under
# build/bench/ and are recreated on every run.

set -u

out=${1:-build/bench}
mkdir -p "$out" || exit 1

# ------------------------------------------------------------------
# Compiler benchmark: many nominal types, unions, matches, functions
# ------------------------------------------------------------------

count=${IDEM_BENCH_COMPILER_UNITS:-200}
compiler_input="$out/compiler_input.idem"

{
    i=0
    while [ "$i" -lt "$count" ]; do
        printf 'struct S%d { a: Int, b: Int, c: String }\n' "$i"
        printf 'enum E%d { A%d, B%d(Int), C%d(String) }\n' \
            "$i" "$i" "$i" "$i"
        printf 'type U%d = Int | String | E%d\n' "$i" "$i"
        i=$((i + 1))
    done

    i=0
    while [ "$i" -lt "$count" ]; do
        printf 'fn f%d(x: Int, s: S%d) -> Int = ' "$i" "$i"
        printf 'if x <= 0 then s.a + %d else f%d(x - 1, s) + s.b\n' \
            "$i" "$i"
        printf 'fn g%d(e: E%d) -> Int = match e { ' "$i" "$i"
        printf 'A%d => 0, B%d(x) => x + %d, C%d(s) => len(s) }\n' \
            "$i" "$i" "$i" "$i"
        printf 'fn u%d(v: U%d) -> Int = match v { ' "$i" "$i"
        printf 'i: Int => i, s: String => len(s), e: E%d => g%d(e) }\n' \
            "$i" "$i"
        i=$((i + 1))
    done

    printf 'fn main(args: Array[String]) -> Unit = {\n'
    printf '    let s = S0 { a: 1, b: 2, c: "x" }\n'
    printf '    println(f0(10, s))\n'
    printf '    println(g0(B0(5)))\n'
    printf '    println(u0(C0("hello")))\n'
    printf '}\n'
} > "$compiler_input" || exit 1

# ------------------------------------------------------------------
# Runtime: array/map/string/allocation workloads
# ------------------------------------------------------------------

n=${IDEM_BENCH_ARRAY_SIZE:-20000}
rounds=${IDEM_BENCH_ARRAY_ROUNDS:-20}
array_source="$out/arrays.idem"
array_expected="$out/arrays.expected"

{
    printf 'fn work(rounds: Int, xs: Array[Int]) -> Int =\n'
    printf '    if rounds <= 0 then 0 else {\n'
    printf '        let a = sum(map(xs, fn(x: Int) -> Int = x * 2))\n'
    printf '        let b = len(filter(xs, fn(x: Int) -> Bool = x %% 2 == 0))\n'
    printf '        let c = fold(xs, 0, fn(acc: Int, x: Int) -> Int = acc + x)\n'
    printf '        a + b + c + work(rounds - 1, xs)\n'
    printf '    }\n\n'
    printf 'fn main(args: Array[String]) -> Unit = {\n'
    printf '    let xs: Array[Int] = ['
    i=1
    while [ "$i" -le "$n" ]; do
        if [ "$i" -gt 1 ]; then
            printf ', '
        fi
        printf '%d' "$i"
        i=$((i + 1))
    done
    printf ']\n'
    printf '    println(work(%d, xs))\n' "$rounds"
    printf '}\n'
} > "$array_source" || exit 1

per_round=$((n * (n + 1) + n / 2 + n * (n + 1) / 2))
printf '%d\n' "$((rounds * per_round))" > "$array_expected" || exit 1

reps=${IDEM_BENCH_STRING_REPS:-4000}
string_source="$out/strings.idem"
string_expected="$out/strings.expected"

cat > "$string_source" <<'EOF'
fn repeat(n: Int, acc: String) -> String =
    if n <= 0 then acc else repeat(n - 1, concat(acc, "abcdefghij"))

fn main(args: Array[String]) -> Unit = {
    let s = repeat(REPS, "")
    println(len(s))
    println(compare(s, s))
    println(byteAt(s, 123))
    println(len(chars(s)))
}
EOF
sed "s/REPS/$reps/" "$string_source" > "$string_source.tmp" || exit 1
mv "$string_source.tmp" "$string_source" || exit 1
printf '%d\n0\n100\n%d\n' "$((reps * 10))" "$((reps * 10))" \
    > "$string_expected" || exit 1

map_n=${IDEM_BENCH_MAP_SIZE:-800}
map_source="$out/maps.idem"
map_expected="$out/maps.expected"

{
    printf 'fn build(n: Int, m: Map[Int, Int]) -> Map[Int, Int] =\n'
    printf '    if n <= 0 then m else build(n - 1, set(m, n, n * 2))\n\n'
    printf 'fn main(args: Array[String]) -> Unit = {\n'
    printf '    let empty: Map[Int, Int] = Map{}\n'
    printf '    let m = build(%d, empty)\n' "$map_n"
    printf '    println(len(m))\n'
    printf '    println(get(m, %d))\n' "$((map_n / 2))"
    printf '    println(len(remove(m, %d)))\n' "$((map_n / 2))"
    printf '}\n'
} > "$map_source" || exit 1
printf '%d\n%d\n%d\n' "$map_n" "$map_n" "$((map_n - 1))" \
    > "$map_expected" || exit 1

gc_n=${IDEM_BENCH_GC_SIZE:-8000}
gc_rounds=${IDEM_BENCH_GC_ROUNDS:-10}
gc_source="$out/gc.idem"
gc_expected="$out/gc.expected"

cat > "$gc_source" <<'EOF'
fn build(n: Int) -> String =
    if n <= 0 then "done" else {
        let s = concat("x", toString(n))
        if len(s) > 0 then build(n - 1) else s
    }

fn run(rounds: Int, n: Int) -> String =
    if rounds <= 0 then "done" else
        if len(build(n)) > 0 then run(rounds - 1, n) else "bad"

fn main(args: Array[String]) -> Unit = {
    println(run(ROUNDS, SIZE))
}
EOF
sed "s/ROUNDS/$gc_rounds/; s/SIZE/$gc_n/" "$gc_source" \
    > "$gc_source.tmp" || exit 1
mv "$gc_source.tmp" "$gc_source" || exit 1
printf 'done\n' > "$gc_expected" || exit 1

adt_n=${IDEM_BENCH_ADT_SIZE:-10000}
adt_rounds=${IDEM_BENCH_ADT_ROUNDS:-10}
adt_source="$out/adt.idem"
adt_expected="$out/adt.expected"

cat > "$adt_source" <<'EOF'
enum Nat { Z, S(Nat) }

fn build(n: Int) -> Nat = if n <= 0 then Z else S(build(n - 1))
fn total(n: Nat) -> Int = match n { Z => 0, S(p) => 1 + total(p) }

fn run(rounds: Int, n: Int) -> Int =
    if rounds <= 0 then 0 else total(build(n)) + run(rounds - 1, n)

fn main(args: Array[String]) -> Unit = {
    println(run(ROUNDS, SIZE))
}
EOF
sed "s/ROUNDS/$adt_rounds/; s/SIZE/$adt_n/" "$adt_source" \
    > "$adt_source.tmp" || exit 1
mv "$adt_source.tmp" "$adt_source" || exit 1
printf '%d\n' "$((adt_rounds * adt_n))" > "$adt_expected" || exit 1

closure_n=${IDEM_BENCH_CLOSURE_SIZE:-8000}
closure_calls=${IDEM_BENCH_CLOSURE_CALLS:-50}
closure_source="$out/closures.idem"
closure_expected="$out/closures.expected"

cat > "$closure_source" <<'EOF'
fn build(n: Int) -> Int -> Int =
    if n <= 0 then fn(x: Int) -> Int = x
    else {
        let f = build(n - 1)
        fn(x: Int) -> Int = f(x) + 1
    }

fn calls(k: Int, f: Int -> Int, x: Int) -> Int =
    if k <= 0 then x else calls(k - 1, f, f(x))

fn main(args: Array[String]) -> Unit = {
    let f = build(SIZE)
    println(calls(CALLS, f, 0))
}
EOF
sed "s/SIZE/$closure_n/; s/CALLS/$closure_calls/" "$closure_source" \
    > "$closure_source.tmp" || exit 1
mv "$closure_source.tmp" "$closure_source" || exit 1
printf '%d\n' "$((closure_n * closure_calls))" > "$closure_expected" || exit 1

fib_source="$out/fib.idem"
fib_expected="$out/fib.expected"

cat > "$fib_source" <<'EOF'
fn fib(n: Int) -> Int = if n < 2 then n else fib(n - 1) + fib(n - 2)
fn main(args: Array[String]) -> Unit = {
    println(fib(32))
}
EOF
printf '2178309\n' > "$fib_expected" || exit 1
