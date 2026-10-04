#!/bin/bash
set -u

BIN=${1:-}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ -z "$BIN" ]; then
    BIN="$ROOT/dist/atomc"
elif [ "${BIN#/}" = "$BIN" ]; then
    BIN="$(cd "$(dirname "$BIN")" 2>/dev/null && pwd)/$(basename "$BIN")"
fi

if [ ! -x "$BIN" ]; then
    echo "native tests: $BIN is not executable"
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

check() {
    local name=$1 pattern=$2 negate=$3 file=$4
    if [ "$negate" -eq 1 ]; then
        if grep -qE -- "$pattern" "$file"; then
            echo "    unexpected match: /$pattern"
            return 1
        fi
    else
        if ! grep -qE -- "$pattern" "$file"; then
            echo "    missing match: /$pattern"
            return 1
        fi
    fi
    return 0
}

src="$ROOT/tests/mh/M_write.mh"
bin="$WORK/mwrite"

if ! "$BIN" -c "$src" -o "$bin" > "$WORK/build.log" 2>&1; then
    echo "FAIL native_build (the VM refused to pack)"
    cat "$WORK/build.log"
    exit 1
fi

ok=1
check native_build '^\[NATIVE\] Built: ' 0 "$WORK/build.log" || ok=0
check native_build '^\[NATIVE\] Platform: ' 0 "$WORK/build.log" || ok=0
check native_build '^\[NATIVE\] ABI: ' 0 "$WORK/build.log" || ok=0
[ -x "$bin" ] || { echo "    the output is not executable"; ok=0; }
if [ "$ok" -eq 1 ]; then
    pass=$((pass + 1))
    echo "PASS native_build"
else
    fail=$((fail + 1))
    echo "FAIL native_build"
fi

cp "$src" "$WORK/backup.mh"
rm -f "$WORK/mwrite.mh"
( cd "$WORK" && ATOM_LIB=/nonexistent ./mwrite ) > "$WORK/run.log" 2>&1
rc=$?

ok=1
[ "$rc" -eq 0 ] || { echo "    exit=$rc"; ok=0; }
check native_run '^\[NATIVE\] Platform linux-' 0 "$WORK/run.log" || ok=0
check native_run '^\[NATIVE\] Program .* is packed into this binary' 0 "$WORK/run.log" || ok=0
check native_run '^65$' 0 "$WORK/run.log" || ok=0
check native_run '^66$' 0 "$WORK/run.log" || ok=0
if [ "$ok" -eq 1 ]; then
    pass=$((pass + 1))
    echo "PASS native_run"
else
    fail=$((fail + 1))
    echo "FAIL native_run"
fi

"$BIN" --native-info > "$WORK/info_plain.log" 2>&1
"$BIN" --native-info "$bin" > "$WORK/info_packed.log" 2>&1

ok=1
check native_info '^platform : linux-' 0 "$WORK/info_plain.log" || ok=0
check native_info '^payload  : none \(plain VM\)$' 0 "$WORK/info_plain.log" || ok=0
check native_info '^payload  : .*M_write\.mh' 0 "$WORK/info_packed.log" || ok=0
if [ "$ok" -eq 1 ]; then
    pass=$((pass + 1))
    echo "PASS native_info"
else
    fail=$((fail + 1))
    echo "FAIL native_info"
fi

cp "$bin" "$WORK/damaged"
printf '\xff' | dd of="$WORK/damaged" bs=1 seek=$(( $(stat -c%s "$WORK/damaged") - 48 - 40 )) \
    count=1 conv=notrunc status=none
chmod +x "$WORK/damaged"
"$WORK/damaged" > "$WORK/damaged.log" 2>&1
rc=$?

ok=1
[ "$rc" -eq 1 ] || { echo "    a damaged payload must exit 1, got $rc"; ok=0; }
check native_guard 'checksum mismatch' 0 "$WORK/damaged.log" || ok=0
if [ "$ok" -eq 1 ]; then
    pass=$((pass + 1))
    echo "PASS native_guard"
else
    fail=$((fail + 1))
    echo "FAIL native_guard"
fi

echo
echo "native tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
