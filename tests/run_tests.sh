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

ATOMC_FLAGS=${ATOMC_FLAGS:-}
pass=0
fail=0
skip=0

for mh in "$DIR"/*.mh; do
    name=$(basename "$mh" .mh)
    if [ "$name" = "ramlib" ]; then continue; fi
    expect="$DIR/$name.expect"

    if [ -f "$DIR/$name.setup" ]; then
        bash "$DIR/$name.setup" || echo "    setup failed for $name"
    fi

    if [ -f "$DIR/$name.guard" ]; then
        reason=$("$DIR/$name.guard" 2>/dev/null) || {
            skip=$((skip + 1))
            echo "SKIP $name (${reason:-guard failed})"
            continue
        }
    fi

    if [ ! -f "$expect" ]; then
        skip=$((skip + 1))
        echo "SKIP $name (no .expect)"
        continue
    fi

    out=$(mktemp)

    if [ -f "$DIR/$name.input" ]; then
        # shellcheck disable=SC2086
        "$BIN" $ATOMC_FLAGS "$mh" < "$DIR/$name.input" > "$out" 2>&1
    else
        # shellcheck disable=SC2086
        "$BIN" $ATOMC_FLAGS "$mh" > "$out" 2>&1
    fi
    rc=$?

    ok=1
    while IFS= read -r pattern || [ -n "$pattern" ]; do
        case "$pattern" in
            ''|'#'*) continue ;;
        esac

        negate=0
        if [ "${pattern:0:1}" = "!" ]; then
            negate=1
            pattern="${pattern:1}"
        fi

        case "$pattern" in
            '~/'*)
                body="${pattern:2}"
                body="${body%/}"
                if [ "$negate" -eq 1 ]; then
                    grep -qE -- "$body" "$out" && { ok=0; echo "    unexpected match: /$body"; }
                else
                    grep -qE -- "$body" "$out" || { ok=0; echo "    missing match: /$body"; }
                fi
                ;;
            '~'*)
                body="${pattern:1}"
                if [ "$negate" -eq 1 ]; then
                    grep -qF -- "$body" "$out" && { ok=0; echo "    unexpected substring: $body"; }
                else
                    grep -qF -- "$body" "$out" || { ok=0; echo "    missing substring: $body"; }
                fi
                ;;
            *)
                if [ "$negate" -eq 1 ]; then
                    grep -qxF -- "$pattern" "$out" && { ok=0; echo "    unexpected line: $pattern"; }
                else
                    grep -qxF -- "$pattern" "$out" || { ok=0; echo "    missing line: $pattern"; }
                fi
                ;;
        esac
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
echo "mh tests: $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
