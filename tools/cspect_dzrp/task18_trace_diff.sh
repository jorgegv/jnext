#!/usr/bin/env bash
# Task 18 — symmetric trace diff for jnext-bypass vs CSpect.
#
# Both traces are in the same format (TraceLog::export_to_file). The
# "cycle" column (first 12 chars) differs by definition: jnext counts
# T-states, CSpect plugin counts instructions. Strip it before diffing.
#
# Usage:
#   ./task18_trace_diff.sh <jnext.txt> <cspect.txt> [head_lines]

set -uo pipefail

JN="${1:-/tmp/task18-symmetric-trace/jnext_full_trace.txt}"
CS="${2:-/tmp/task18-symmetric-trace/cspect_full_trace.txt}"
HEAD="${3:-50}"

if [[ ! -f "$JN" || ! -f "$CS" ]]; then
    echo "ERROR: trace file missing"
    [[ ! -f "$JN" ]] && echo "  missing: $JN"
    [[ ! -f "$CS" ]] && echo "  missing: $CS"
    exit 1
fi

OUTDIR="$(dirname "$JN")"
JN_STRIP="$OUTDIR/jnext_full_trace.stripped.txt"
CS_STRIP="$OUTDIR/cspect_full_trace.stripped.txt"

echo "=== sizes ==="
wc -l "$JN" "$CS"

echo
echo "=== first 5 lines each ==="
echo "--- jnext ---"
head -5 "$JN"
echo "--- cspect ---"
head -5 "$CS"

echo
echo "=== stripping cycle column ==="
# Format: "012345678901  $PCXX  ..." — strip first 14 chars (12 digits + 2 spaces).
sed 's/^[0-9]\{12\}  //' "$JN" > "$JN_STRIP"
sed 's/^[0-9]\{12\}  //' "$CS" > "$CS_STRIP"
wc -l "$JN_STRIP" "$CS_STRIP"

echo
echo "=== first 5 stripped lines each ==="
echo "--- jnext ---"
head -5 "$JN_STRIP"
echo "--- cspect ---"
head -5 "$CS_STRIP"

echo
echo "=== first divergent line (line-by-line diff) ==="
diff "$JN_STRIP" "$CS_STRIP" | head -30 || true

echo
echo "=== first matching prefix length (lines that match from line 1) ==="
# Walk forward until a line differs.
awk -v cs="$CS_STRIP" '
BEGIN {
    n=0
    while ((getline cline < cs) > 0) {
        getline jline
        n++
        if (cline != jline) {
            print "FIRST DIVERGENCE at line " n
            print "  jnext: " jline
            print "  cspect: " cline
            exit
        }
    }
    print "no divergence in first " n " lines"
}
' "$JN_STRIP"
