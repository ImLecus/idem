#!/bin/sh

# Command-line interface tests for the idem driver.

root=$(cd "$(dirname "$0")/.." && pwd) || exit 1
idem=${IDEM_BIN:-$root/idem}
tmpdir=$(mktemp -d) || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

failures=0

pass() {
    echo "ok $1"
}

fail() {
    echo "FAIL $1: $2" >&2
    failures=$((failures + 1))
}

version_define() {
    sed -n "s/^#define $1 \"\\(.*\\)\"/\\1/p" "$root/src/version.h"
}

expected_version="idem $(version_define IDEM_VERSION) ($(version_define IDEM_LANGUAGE_VERSION))"
actual_version=$("$idem" --version 2>"$tmpdir/err")
if [ "$actual_version" = "$expected_version" ]; then
    pass "--version"
else
    fail "--version" "got '$actual_version', expected '$expected_version'"
fi

"$idem" --help >"$tmpdir/help" 2>&1
if [ $? -eq 0 ] && grep -q 'usage: idem' "$tmpdir/help" &&
   grep -q -e '--version' "$tmpdir/help" &&
   grep -q -e '--emit-c' "$tmpdir/help"; then
    pass "--help"
else
    fail "--help" "missing usage text"
fi

check_status() {
    name=$1
    expected=$2
    shift 2
    "$@" >"$tmpdir/out" 2>"$tmpdir/err"
    actual=$?
    if [ "$actual" -eq "$expected" ]; then
        pass "$name"
    else
        fail "$name" "exit $actual, expected $expected"
    fi
}

check_status "no arguments" 2 "$idem"
check_status "--tokens without a file" 2 "$idem" --tokens
check_status "--emit-c without a file" 2 "$idem" --emit-c
check_status "missing input file" 1 "$idem" "$tmpdir/does-not-exist.idem"

"$idem" "$root/examples/hello.idem" >"$tmpdir/hello.out" 2>"$tmpdir/err"
if [ $? -eq 0 ] && [ "$(cat "$tmpdir/hello.out")" = "Hello, Idem!" ]; then
    pass "run hello"
else
    fail "run hello" "unexpected output"
fi

"$idem" --tokens "$root/examples/hello.idem" >"$tmpdir/tokens" 2>"$tmpdir/err"
if [ $? -eq 0 ] && [ -s "$tmpdir/tokens" ]; then
    pass "--tokens"
else
    fail "--tokens" "no token output"
fi

"$idem" --emit-c "$root/examples/hello.idem" >"$tmpdir/hello.c" 2>"$tmpdir/err"
if [ $? -eq 0 ] && grep -q '#include "value.h"' "$tmpdir/hello.c"; then
    pass "--emit-c"
else
    fail "--emit-c" "unexpected generated C"
fi

cat >"$tmpdir/args.idem" <<'EOF'
fn main(args: Array[String]) -> Unit = println(get(args, 1))
EOF
"$idem" "$tmpdir/args.idem" "hello world" >"$tmpdir/args.out" 2>"$tmpdir/err"
if [ $? -eq 0 ] && [ "$(cat "$tmpdir/args.out")" = "hello world" ]; then
    pass "program arguments"
else
    fail "program arguments" "argument forwarding failed"
fi

IDEM_RUNTIME_DIR="$tmpdir/missing-runtime" "$idem" "$root/examples/hello.idem" \
    >"$tmpdir/out" 2>"$tmpdir/runtime.err"
if [ $? -eq 1 ] && grep -q 'runtime directory not found' "$tmpdir/runtime.err"; then
    pass "missing runtime directory"
else
    fail "missing runtime directory" "expected a clear error"
fi

IDEM_KEEP_C=1 "$idem" "$root/examples/hello.idem" >"$tmpdir/out" 2>"$tmpdir/keep.err"
if [ $? -eq 0 ] &&
   grep -q 'generated C at ' "$tmpdir/keep.err" &&
   grep -q 'executable at ' "$tmpdir/keep.err"; then
    c_path=$(sed -n 's/^idem: generated C at //p' "$tmpdir/keep.err")
    exe_path=$(sed -n 's/^idem: executable at //p' "$tmpdir/keep.err")
    if [ -n "$c_path" ] && [ -n "$exe_path" ]; then
        keep_dir=$(dirname "$c_path")
        case "$keep_dir" in
        */idem-*)
            rm -rf "$keep_dir"
            ;;
        esac
        pass "IDEM_KEEP_C"
    else
        fail "IDEM_KEEP_C" "missing paths"
    fi
else
    fail "IDEM_KEEP_C" "expected kept artifacts"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures CLI test(s) failed"
    exit 1
fi

echo "all CLI tests passed"
