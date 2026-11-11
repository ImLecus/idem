#!/bin/sh

# Run the Idem benchmarks.
#
#   sh benchmarks/run.sh compiler
#   sh benchmarks/run.sh runtime
#   sh benchmarks/run.sh all
#
# Environment:
#   IDEM_BENCH_ITERATIONS          compiler iterations (default 5)
#   IDEM_BENCH_RUNTIME_ITERATIONS  runtime iterations (default 5)
#   CC                             C compiler for generated programs

set -u

root=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$root" || exit 1

mode=${1:-all}
iterations=${IDEM_BENCH_ITERATIONS:-5}
runtime_iterations=${IDEM_BENCH_RUNTIME_ITERATIONS:-5}

if [ "$iterations" -lt 1 ] 2>/dev/null; then
    iterations=1
fi
if [ "$runtime_iterations" -lt 1 ] 2>/dev/null; then
    runtime_iterations=1
fi

case "$mode" in
    compiler|runtime|all) ;;
    *)
        echo "usage: sh benchmarks/run.sh [compiler|runtime|all]" >&2
        exit 2
        ;;
esac

gen=build/bench
mkdir -p "$gen" || exit 1
sh benchmarks/generate_inputs.sh "$gen" || exit 1

now_ns() {
    date +%s%N 2>/dev/null
}

check_clock() {
    value=$(now_ns)
    case "$value" in
        *[!0-9]*|'')
            echo "benchmarks: 'date +%s%N' does not provide nanoseconds" >&2
            exit 1
            ;;
    esac
}

if [ "$mode" = "compiler" ] || [ "$mode" = "all" ]; then
    if [ ! -x build/compiler-bench ]; then
        echo "benchmarks: build/compiler-bench is missing" >&2
        echo "benchmarks: run 'make bench' or 'make bench-compiler'" >&2
        exit 1
    fi

    echo "== compiler =="
    build/compiler-bench "$gen/compiler_input.idem" "$iterations" || exit 1
fi

if [ "$mode" = "runtime" ] || [ "$mode" = "all" ]; then
    check_clock
    cc=${CC:-cc}

    echo
    echo "== runtime =="
    echo "cc: $cc"
    printf '%-18s %10s %10s %10s\n' \
        "benchmark" "min ms" "median ms" "mean ms"

    for name in fib arrays strings maps gc adt closures; do
        source="$gen/$name.idem"
        expected="$gen/$name.expected"
        cfile="$gen/$name.c"
        exe="$gen/$name"
        out="$gen/$name.out"
        times="$gen/$name.times"

        if [ ! -f "$source" ] || [ ! -f "$expected" ]; then
            echo "benchmarks: missing generated input for '$name'" >&2
            exit 1
        fi

        ./idem --emit-c "$source" > "$cfile" || {
            echo "benchmarks: code generation failed for '$name'" >&2
            exit 1
        }

        $cc -std=c89 -pedantic -Wall -Wextra -O2 \
            -I"$root/runtime" -o "$exe" "$cfile" \
            "$root"/runtime/*.c -lm || {
            echo "benchmarks: C compilation failed for '$name'" >&2
            exit 1
        }

        # Warm-up and correctness check.
        "$exe" > "$out" || {
            echo "benchmarks: '$name' exited with an error" >&2
            exit 1
        }
        if ! cmp -s "$out" "$expected"; then
            echo "benchmarks: '$name' produced unexpected output" >&2
            exit 1
        fi

        : > "$times" || exit 1
        i=0
        while [ "$i" -lt "$runtime_iterations" ]; do
            start=$(now_ns)
            "$exe" >/dev/null || {
                echo "benchmarks: '$name' failed during timing" >&2
                exit 1
            }
            end=$(now_ns)
            echo $((end - start)) >> "$times"
            i=$((i + 1))
        done

        sort -n "$times" > "$times.sorted" || exit 1
        stats=$(awk '
            { values[NR] = $1; sum += $1 }
            END {
                n = NR;
                if (n % 2 == 1) {
                    median = values[(n + 1) / 2];
                } else {
                    median = (values[n / 2] + values[n / 2 + 1]) / 2;
                }
                printf "%.3f %.3f %.3f\n",
                       values[1] / 1000000.0,
                       median / 1000000.0,
                       sum / n / 1000000.0;
            }
        ' "$times.sorted") || exit 1

        min=$(echo "$stats" | cut -d' ' -f1)
        median=$(echo "$stats" | cut -d' ' -f2)
        mean=$(echo "$stats" | cut -d' ' -f3)
        printf '%-18s %10s %10s %10s\n' \
            "$name" "$min" "$median" "$mean"
    done
fi
