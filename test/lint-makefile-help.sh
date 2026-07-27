#!/usr/bin/env bash
# Structural lint for the Makefile's `make` (no target) help listing.
#
# The help awk in the `default` target OVERWRITES its pending description on
# every `^# ` line, so only the LAST one before a target survives. A target
# whose rationale sits below its description therefore advertises a sentence
# fragment: GH #140 found eight, including `regression` and `unit-test`, the
# two most-used targets in the project.
#
# The rule enforced here is the project's documented one (STRICT, 2026-04-22):
# the comment block immediately above a target carries AT MOST ONE `# ` line.
# Rationale goes INSIDE the recipe as tab-indented `@#` shell comments (see
# win-release, unit-test, harness-selftest) or in a design doc.
#
# This is structural, not a heuristic on prose: it fires on discarded content,
# not on how a description reads. Rationale-first blocks (description last)
# print correctly today but are one appended line away from the same bug, so
# they are violations too.
#
# Usage:  bash test/lint-makefile-help.sh [MAKEFILE]
# Exit:   0 clean · 1 violations found · 2 self-test failed / bad usage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MAKEFILE="${1:-$PROJECT_DIR/Makefile}"

# scan <makefile> -> one line per violating target:  <line>:<target>:<count>
#
# Mirrors the help awk's own notion of a target (^[a-zA-Z0-9_-]+:) and of a
# description (^# , hash-space). Counts only the comment block DIRECTLY above
# the target: any non-comment line ends the block, exactly like a blank line
# visually separates a banner from a target.
scan() {
    awk '
        /^# /                  { n++; if (n == 1) first = NR; next }
        /^#/                   { next }                       # bare "#" spacer
        /^[a-zA-Z0-9_-]+:/     { if (n > 1) { sub(/:.*/, "", $0); print first ":" $0 ":" n } n = 0; next }
        { n = 0 }
    ' "$1"
}

# --- self-test: prove the scanner discriminates, on every run (~5 ms) --------
# A checker that has never been shown to fail is not evidence of anything.
selftest() {
    local dir good bad rc=0
    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' RETURN
    good="$dir/good.mk"
    bad="$dir/bad.mk"

    printf '%s\n' \
        '# One-line description' \
        'clean-target:' \
        '	@# rationale lives here' \
        '	@echo hi' \
        '' \
        'undocumented-target:' \
        '	@echo hi' > "$good"

    printf '%s\n' \
        '# One-line description' \
        'stacked-target:' \
        '	@echo hi' \
        '' \
        '# One-line description' \
        '# rationale stacked below it — this is the GH #140 shape' \
        'broken-target:' \
        '	@echo hi' > "$bad"

    [ -z "$(scan "$good")" ] || { echo "SELFTEST FAIL: flagged a clean fixture" >&2; rc=1; }
    [ "$(scan "$bad")" = "5:broken-target:2" ] \
        || { echo "SELFTEST FAIL: did not flag the stacked fixture (got '$(scan "$bad")')" >&2; rc=1; }

    return $rc
}

selftest || exit 2

# --- the real scan ----------------------------------------------------------
[ -f "$MAKEFILE" ] || { echo "ERROR: no such Makefile: $MAKEFILE" >&2; exit 2; }

violations="$(scan "$MAKEFILE")"

if [ -n "$violations" ]; then
    echo "[lint-makefile-help] target-preceding comment blocks must be ONE '# ' line:" >&2
    while IFS=: read -r line target count; do
        printf "  %s:%s: target '%s' has %s '# ' lines above it — %s would be discarded by the help listing\n" \
            "${MAKEFILE#"$PROJECT_DIR"/}" "$line" "$target" "$count" "$((count - 1))" >&2
    done <<< "$violations"
    echo "  Fix: keep one '# ' description line above the target; move the rest into the recipe as '@#' lines." >&2
    exit 1
fi

echo "[lint-makefile-help] ok: every documented target has exactly one description line"
