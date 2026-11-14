#!/bin/bash
# tests/run.sh - run the Tracey end-to-end test cases.
#
# For each case_XX.trc:
#   1. parse // expect_exit: N, // expect_stdout: S and // expect_stdin: S
#      from the header comments
#   2. compile with build/traceyc to a temporary binary
#   3. run the binary (feeding expect_stdin when present), capturing stdout
#      and exit code
#   4. compare against the expectations and report PASS/FAIL
#
# Usage: ./tests/run.sh [traceyc_path]
#   traceyc_path defaults to ./build/traceyc

set -u

TRACYC="${1:-./build/traceyc}"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -x "$TRACYC" ]; then
    echo "error: traceyc not found or not executable: $TRACYC" >&2
    exit 1
fi

pass=0
fail=0
total=0

for trc in "$TESTS_DIR"/case_*.trc; do
    [ -e "$trc" ] || continue
    total=$((total + 1))
    name=$(basename "$trc" .trc)

    # Parse expectations from the header comments.
    expect_exit=$(grep -E '^// expect_exit:' "$trc" | head -n 1 | sed 's|^// expect_exit:[[:space:]]*||')
    expect_stdout=$(grep -E '^// expect_stdout:' "$trc" | head -n 1 | sed 's|^// expect_stdout:[[:space:]]*||')
    expect_stdin=$(grep -E '^// expect_stdin:' "$trc" | head -n 1 | sed 's|^// expect_stdin:[[:space:]]*||')
    expect_exit=${expect_exit:-0}

    bin="$TESTS_DIR/$name"
    rm -f "$bin"

    # Compile.
    if ! "$TRACYC" "$trc" -o "$bin" 2>"$TESTS_DIR/$name.err"; then
        echo "FAIL: $name  (compile error)"
        cat "$TESTS_DIR/$name.err" | sed 's/^/      /'
        fail=$((fail + 1))
        continue
    fi

    # Run. Feed the expected stdin (with a trailing newline) when one is
    # declared; otherwise run with an immediately-closed stdin (EOF).
    if [ -n "$expect_stdin" ]; then
        out=$(printf '%s\n' "$expect_stdin" | "$bin" 2>/dev/null)
    else
        out=$("$bin" 2>/dev/null)
    fi
    rc=$?

    ok=1
    if [ "$rc" -ne "$expect_exit" ]; then
        echo "FAIL: $name  (exit code $rc, expected $expect_exit)"
        ok=0
    fi
    if [ -n "$expect_stdout" ] && [ "$out" != "$expect_stdout" ]; then
        echo "FAIL: $name  (stdout '$out', expected '$expect_stdout')"
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo "PASS: $name  (exit $rc$( [ -n "$expect_stdout" ] && echo ", stdout '$out'"))"
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi

    rm -f "$bin" "$TESTS_DIR/$name.err"
done

echo "----------------------------------------"
echo "Results: $pass passed, $fail failed, $total total"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
