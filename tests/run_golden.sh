#!/bin/sh

# Golden tests: compile and run each tests/golden/<name>/main.idem and
# compare stdout, stderr, and the exit status with the expected files.

root=$(cd "$(dirname "$0")/.." && pwd) || exit 1
tmpdir=$(mktemp -d) || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

failures=0
total=0

for dir in "$root"/tests/golden/*/; do
    [ -f "$dir/main.idem" ] || continue
    total=$((total + 1))
    name=$(basename "$dir")
    out="$tmpdir/stdout"
    err="$tmpdir/stderr"

    (
        cd "$root" && ./idem "$dir/main.idem"
    ) >"$out" 2>"$err"
    status=$?

    expected_status=0
    if [ -f "$dir/exit.txt" ]; then
        expected_status=$(cat "$dir/exit.txt")
    fi

    failed=0
    if [ "$status" -ne "$expected_status" ]; then
        echo "FAIL $name: exit $status, expected $expected_status"
        failed=1
    fi
    if ! cmp -s "$out" "$dir/stdout.txt"; then
        echo "FAIL $name: stdout differs"
        diff -u "$dir/stdout.txt" "$out" 2>/dev/null || true
        failed=1
    fi
    if ! cmp -s "$err" "$dir/stderr.txt"; then
        echo "FAIL $name: stderr differs"
        diff -u "$dir/stderr.txt" "$err" 2>/dev/null || true
        failed=1
    fi

    if [ "$failed" -eq 0 ]; then
        echo "ok $name"
    else
        failures=$((failures + 1))
    fi
done

if [ "$total" -eq 0 ]; then
    echo "golden: no tests found"
    exit 1
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures of $total golden tests failed"
    exit 1
fi

echo "all $total golden tests passed"
