set -u

BIN=${1:-}
ROOT="$(cd "$(dirname "$0")" && pwd)/.."
DIR="$ROOT/tests/mh"
cd "$DIR" || exit 1

if [ -z "$BIN" ]; then
    BIN="$ROOT/dist/atomc"
elif [ "${BIN#/}" = "$BIN" ]; then
    BIN="$(cd "$(dirname "$BIN")" 2>/dev/null && pwd)/$(basename "$BIN")"
fi

pass=0
fail=0

for mh in "$DIR"/*.mh; do
    name=$(basename "$mh" .mh)
    expect="$DIR/$name.expect"
    if [ ! -f "$expect" ]; then
        echo "SKIP $name (no .expect)"
        continue
    fi

    if [ -f "$DIR/$name.setup" ]; then
        bash "$DIR/$name.setup"
    fi

    out=$(mktemp)

    if [ -f "$DIR/$name.input" ]; then
        "$BIN" "$mh" < "$DIR/$name.input" > "$out" 2>&1
    else
        "$BIN" "$mh" > "$out" 2>&1
    fi
    rc=$?

    ok=1
    while IFS= read -r pattern || [ -n "$pattern" ]; do
        [ -z "$pattern" ] && continue
        if [[ "$pattern" == "~"* ]]; then
            if ! grep -qF -- "${pattern:1}" "$out"; then
                ok=0
                echo "    missing substring: ${pattern:1}"
            fi
        else
            if ! grep -qxF -- "$pattern" "$out"; then
                ok=0
                echo "    missing line: $pattern"
            fi
        fi
    done < "$expect"

    if [ "$ok" -eq 1 ] && [ "$rc" -eq 0 ]; then
        pass=$((pass + 1))
        echo "PASS $name"
    else
        fail=$((fail + 1))
        echo "FAIL $name (exit=$rc)"
    fi

    rm -f "$out"
done

echo
echo "mh tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
