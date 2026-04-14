#!/usr/bin/env bash
# Tautological-assertion lint for the JNEXT test suite.
#
# The Task 4 subsystem test-plan audit (2026-04-14) found that 100%-pass
# results across Layer2, Sprites, Copper, Compositor, I/O Dispatch and Input
# were partly "coverage theatre": some checks are tautologies that pass
# regardless of the code under test. This script bans three such patterns
# from landing in new code:
#
#   1. check(id, desc, true, ...)    -- literal true as the condition.
#                                       The check() signature is
#                                       check(const char*, const char*, bool, ...)
#                                       so the condition is always the 3rd arg.
#   2. "... || true"                 -- forces the assertion to always pass.
#   3. "a == b || a != b"            -- trivial tautology.
#
# Baseline: current offenders are recorded in test/lint-assertions.baseline
# so legacy code does not block CI. The script fails only on NEW violations
# (anything in the current scan that is not in the baseline).
#
# Usage:
#   bash test/lint-assertions.sh                  # check against baseline
#   bash test/lint-assertions.sh --update-baseline  # regenerate baseline
#
# Run from the repo root. Requires ripgrep (rg).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BASELINE="$SCRIPT_DIR/lint-assertions.baseline"

cd "$PROJECT_DIR"

if ! command -v rg &>/dev/null; then
    echo "ERROR: ripgrep (rg) is required" >&2
    exit 2
fi

GLOBS=(
    --glob 'test/**/*.cpp'
    --glob 'test/**/*.cc'
    --glob 'test/**/*.h'
    --glob 'test/**/*.hpp'
    --glob '!test/fuse/**'
)

scan() {
    # Pattern 1: literal true as the 3rd argument to check(...)
    rg -n --no-heading "${GLOBS[@]}" \
        'check\s*\([^,]+,[^,]+,\s*true\s*[,)]' || true
    # Pattern 2: "|| true" at end of condition
    rg -n --no-heading "${GLOBS[@]}" \
        '\|\|\s*true\b' || true
    # Pattern 3: "a == b || a != b" (backrefs -> PCRE2)
    rg -nP --no-heading "${GLOBS[@]}" \
        '(\w+)\s*==\s*(\w+)\s*\|\|\s*\1\s*!=\s*\2' || true
}

TMP=$(mktemp)
trap 'rm -f "$TMP" "$TMP.cur" "$TMP.base"' EXIT
scan | LC_ALL=C sort -u > "$TMP"

if [[ "${1:-}" == "--update-baseline" ]]; then
    cp "$TMP" "$BASELINE"
    echo "Updated baseline: $BASELINE ($(wc -l <"$BASELINE") entries)"
    exit 0
fi

if [[ ! -f "$BASELINE" ]]; then
    echo "ERROR: baseline not found at $BASELINE" >&2
    echo "Run: bash test/lint-assertions.sh --update-baseline" >&2
    exit 2
fi

LC_ALL=C sort -u "$BASELINE" > "$TMP.base"
cp "$TMP" "$TMP.cur"

NEW=$(comm -23 "$TMP.cur" "$TMP.base" || true)
BASELINE_N=$(wc -l <"$TMP.base" | tr -d ' ')
NEW_N=0
if [[ -n "$NEW" ]]; then
    NEW_N=$(printf '%s\n' "$NEW" | wc -l | tr -d ' ')
fi

echo "[lint-assertions] baseline: $BASELINE_N  new: $NEW_N"

if [[ "$NEW_N" -gt 0 ]]; then
    echo "[lint-assertions] NEW tautological assertions (not in baseline):" >&2
    printf '%s\n' "$NEW" >&2
    echo "" >&2
    echo "Fix them, or if legitimately unavoidable, update the baseline with:" >&2
    echo "  bash test/lint-assertions.sh --update-baseline" >&2
    exit 1
fi

exit 0
