#!/bin/sh

# Relocation test: the compiler must find its runtime relative to the
# executable, without relying on the build-tree IDEM_RUNTIME_DIR.

root=$(cd "$(dirname "$0")/.." && pwd) || exit 1
idem=${IDEM_RELOC_BIN:-}
if [ -z "$idem" ]; then
    echo "error: IDEM_RELOC_BIN is not set" >&2
    exit 1
fi

tmpdir=$(mktemp -d) || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

prefix="$tmpdir/prefix"
mkdir -p "$prefix/bin" "$prefix/lib/idem"
cp "$idem" "$prefix/bin/idem" || exit 1
cp -R "$root/runtime" "$prefix/lib/idem/runtime" || exit 1

cat >"$tmpdir/relocated.idem" <<'EOF'
fn main(args: Array[String]) -> Unit = println("relocated ok")
EOF

output=$(
    unset IDEM_RUNTIME_DIR
    cd "$tmpdir" || exit 1
    "$prefix/bin/idem" "$tmpdir/relocated.idem"
) 2>"$tmpdir/err"
status=$?

if [ "$status" -ne 0 ]; then
    echo "FAIL relocation: exit $status" >&2
    cat "$tmpdir/err" >&2
    exit 1
fi

if [ "$output" != "relocated ok" ]; then
    echo "FAIL relocation: got '$output'" >&2
    exit 1
fi

flat="$tmpdir/flat"
mkdir -p "$flat"
cp "$idem" "$flat/idem" || exit 1
cp -R "$root/runtime" "$flat/runtime" || exit 1

output=$(
    unset IDEM_RUNTIME_DIR
    cd "$flat" || exit 1
    "$flat/idem" "$tmpdir/relocated.idem"
) 2>"$tmpdir/err"
status=$?

if [ "$status" -ne 0 ]; then
    echo "FAIL flat relocation: exit $status" >&2
    cat "$tmpdir/err" >&2
    exit 1
fi

if [ "$output" != "relocated ok" ]; then
    echo "FAIL flat relocation: got '$output'" >&2
    exit 1
fi

echo "ok relocation"
