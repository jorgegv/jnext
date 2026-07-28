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
# the comment block attached to a target carries AT MOST ONE `# ` line.
# Rationale goes INSIDE the recipe as tab-indented `@#` shell comments (see
# win-release, unit-test, harness-selftest) or in a design doc.
#
# This is structural, not a heuristic on prose: it fires on discarded content,
# not on how a description reads. Rationale-first blocks (description last)
# print correctly today but are one appended line away from the same bug, so
# they are violations too.
#
# WHAT ENDS A BLOCK — the part that has to mirror the oracle.
#
# The help awk clears its pending description on ONE thing: a line matching
# ^[a-zA-Z0-9_-]+: . Everything else is inert and lets a stale `# ` line ride
# through — blank lines, bare `#`, `.PHONY:` (leading dot: no match), `ifeq`/
# `endif`, and a multi-target rule written `a b:` (space before the colon: no
# match). A first version of this lint ended the block on any non-comment line
# and so reported CLEAN on all four of those shapes while the real awk printed
# a fragment. A blank line between a short description and a longer rationale
# paragraph is ordinary prose convention, not a contrived case.
#
# Mirroring the oracle EXACTLY (reset on target lines only) is wrong in the
# other direction, and that was measured, not assumed: it flags `default`
# (38 lines — the whole file header) and `package-src` (14 — a section banner).
# Nothing between those comment blocks and their target is a target line,
# because a variable assignment writes `FOO := bar` with a space and does not
# match the oracle's regex either. So "every prior target line would already
# have reset state" does not hold for this Makefile.
#
# Hence: reset on a target line (the oracle's own rule) OR on substantive make
# content — a variable assignment or a file-level directive. Blank lines and
# .PHONY do not reset. Recipe lines deliberately do NOT reset: a comment cannot
# be attached to a target through one, and resetting there would re-open the
# `a b:` shape.
#
# That second reset is a TRADE-OFF, not a general truth, and it is the reason
# for the known gap below. A variable assignment usually means the comment above
# it documents THAT, not the target further down — but the oracle does not reset
# there, so anything the oracle discards across such a line is invisible here.
#
# One consequence is deliberate and worth stating: a banner comment separated
# from a description by only a blank line IS flagged, because it is structurally
# identical to the bug — same lines, same order, same discarded content, and no
# rule can tell them apart. Put a variable, or a bare `#` (which the oracle
# ignores), between them, or fold the banner into the one description line.
#
# KNOWN GAP — WONT (recorded 2026-07-28, argued below; do not re-derive it).
# This shape reproduces the #140 symptom and this lint reports CLEAN:
#
#     # the real description
#                                    <- blank
#     SOME_VAR := value              <- or `include foo.mk`
#                                    <- blank
#     # rationale that wrongly wins
#     some-target:
#
# The oracle prints the rationale and discards the real description. The
# assignment cleared the running count, and with it the discard history, so an
# orphaned comment landing after the boundary and before the target is invisible.
# Pinned by the `gap` fixture in selftest() so it cannot be silently re-derived.
#
# Why WONT rather than fixed — both alternatives were built and measured:
#   * Drop the substantive-line reset (a pure oracle mirror). Reinstates the
#     false positives it was added for: `default` (38 lines — the file header)
#     and `package-src` (14 — a section banner). Bare `make` prints a correct,
#     complete description for BOTH today, so those flags would be pure noise.
#   * Reset only when the comment block ABUTS the assignment (no blank between),
#     treating a blank-separated one as orphaned. This is clean on the current
#     Makefile and does catch both forms of the gap — but it converts the miss
#     into a FALSE POSITIVE on a legitimate style: a comment, a blank line, then
#     the variable it documents now flags the next target, whose help output was
#     correct all along. Verified, not assumed. A gate that fails on a blank line
#     costs more than the shape it catches.
# Re-open if the shape ever appears in the Makefile (it does not today — the
# lint is clean), or if someone finds a rule that closes it without either cost.
#
# Usage:  bash test/lint-makefile-help.sh [MAKEFILE]
# Exit:   0 clean · 1 violations found · 2 self-test failed / bad usage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MAKEFILE="${1:-$PROJECT_DIR/Makefile}"

# scan <makefile> -> one line per violating target:  <first-line>:<target>:<count>
scan() {
    awk '
        # A description line: the only thing the oracle ever stores.
        /^# /              { n++; if (n == 1) first = NR; next }

        # A target line: where the oracle prints, then clears. Same regex.
        /^[a-zA-Z0-9_-]+:/ { if (n > 1) { t = $0; sub(/:.*/, "", t); print first ":" t ":" n } n = 0; next }

        # Substantive make content detaches a comment block from what follows.
        # Ordered after the target rule so `foo:=bar` stays a target to us
        # exactly as it does to the oracle.
        /^[[:space:]]*(export|override|unexport)?[[:space:]]*[A-Za-z_][A-Za-z0-9_.]*[[:space:]]*[:+?!]?=/ { n = 0; next }
        /^[[:space:]]*(include|-include|sinclude|define|endef|vpath)[[:space:]]/                          { n = 0; next }

        # Everything else — blank, bare "#", .PHONY:, ifeq/endif, `a b:`,
        # recipe lines — is inert, exactly as it is to the oracle.
    ' "$1"
}

# --- self-test: prove the scanner discriminates, on every run (~10 ms) -------
# A checker that has never been shown to fail is not evidence of anything, and
# a checker whose fixtures share its blind spot is not self-testing: the first
# version of this file had ONE fixture, strictly-adjacent `# ` lines, which is
# exactly why the four shapes above went unnoticed. Every reset rule and every
# non-reset above now has a fixture.
selftest() {
    local dir good bad gap got want rc=0
    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' RETURN
    good="$dir/good.mk"
    bad="$dir/bad.mk"
    gap="$dir/gap.mk"

    # Must stay CLEAN. The SOME_VAR block is the false-positive guard: it is the
    # shape of this Makefile's own file header, which a strict oracle mirror
    # would flag.
    printf '%s\n' \
        '# One-line description'                                       \
        'clean-target:'                                                \
        '	@# rationale lives here, where it cannot be a description' \
        '	@echo hi'                                                  \
        ''                                                             \
        'undocumented-target:'                                         \
        '	@echo hi'                                                  \
        ''                                                             \
        '# A prose paragraph about the variable below,'                \
        '# running onto a second line.'                                \
        'SOME_VAR := value'                                            \
        ''                                                             \
        '# One-line description'                                       \
        'after-assignment:'                                            \
        '	@echo hi'                                                  \
        ''                                                             \
        '# One-line description'                                       \
        '#'                                                            \
        'after-bare-hash:'                                             \
        '	@echo hi'                                                  > "$good"

    # Must ALL be flagged. One target per shape the oracle treats as inert.
    printf '%s\n' \
        '# Description that the help listing will discard'          \
        '# trailing rationale line'                                 \
        'adjacent:'                                                 \
        '	@echo hi'                                               \
        ''                                                          \
        '# Description that the help listing will discard'          \
        ''                                                          \
        '# trailing rationale line'                                 \
        'across-blank:'                                             \
        '	@echo hi'                                               \
        ''                                                          \
        '# Description that the help listing will discard'          \
        '.PHONY: across-phony'                                      \
        '# trailing rationale line'                                 \
        'across-phony:'                                             \
        '	@echo hi'                                               \
        ''                                                          \
        '# Description that the help listing will discard'          \
        'multi-a multi-b:'                                          \
        '	@echo hi'                                               \
        '# trailing rationale line'                                 \
        'across-multi-target:'                                      \
        '	@echo hi'                                               \
        ''                                                          \
        '# Description that the help listing will discard'          \
        'ifeq ($(FOO),bar)'                                         \
        '# trailing rationale line'                                 \
        'across-ifeq:'                                              \
        '	@echo hi'                                               \
        'endif'                                                     > "$bad"

    # KNOWN GAP, pinned deliberately: these two DO reproduce the #140 symptom and
    # this scanner does NOT catch them. See the "KNOWN GAP — WONT" note in the
    # header for the two closures that were built and rejected. The fixture exists
    # so the hole is a recorded decision rather than folklore.
    printf '%s\n' \
        '# the real description'                    \
        ''                                          \
        'SOME_VAR := value'                         \
        ''                                          \
        '# rationale that wrongly wins'             \
        'across-assignment:'                        \
        '	@echo hi'                               \
        ''                                          \
        '# the real description'                    \
        ''                                          \
        'include some.mk'                           \
        ''                                          \
        '# rationale that wrongly wins'             \
        'across-include:'                           \
        '	@echo hi'                               > "$gap"

    got="$(scan "$good")"
    [ -z "$got" ] || { echo "SELFTEST FAIL: flagged a clean fixture: $got" >&2; rc=1; }

    got="$(scan "$gap")"
    [ -z "$got" ] || {
        echo "SELFTEST FAIL: the known gap now flags: $got" >&2
        echo "  This is NOT a bug — the gap fixture pins behaviour the scanner is" >&2
        echo "  documented as NOT catching (see 'KNOWN GAP — WONT' in the header)." >&2
        echo "  If you closed it deliberately: prove the 14/clean pair and the good" >&2
        echo "  fixture still hold, then delete this fixture and the WONT note." >&2
        rc=1
    }

    want='1:adjacent:2
6:across-blank:2
12:across-phony:2
18:across-multi-target:2
25:across-ifeq:2'
    got="$(scan "$bad")"
    [ "$got" = "$want" ] || {
        echo "SELFTEST FAIL: bad-fixture scan mismatch (want <, got >)" >&2
        diff <(echo "$want") <(echo "$got") >&2 || true
        rc=1
    }

    return $rc
}

selftest || exit 2

# --- the real scan ----------------------------------------------------------
[ -f "$MAKEFILE" ] || { echo "ERROR: no such Makefile: $MAKEFILE" >&2; exit 2; }

violations="$(scan "$MAKEFILE")"

if [ -n "$violations" ]; then
    echo "[lint-makefile-help] target-attached comment blocks must be ONE '# ' line:" >&2
    while IFS=: read -r line target count; do
        printf "  %s:%s: target '%s' has %s '# ' lines attached to it — %s would be discarded by the help listing\n" \
            "${MAKEFILE#"$PROJECT_DIR"/}" "$line" "$target" "$count" "$((count - 1))" >&2
    done <<< "$violations"
    echo "  (blank lines, bare '#', .PHONY: and ifeq do NOT separate them — the help awk does not reset on those)" >&2
    echo "  Fix: keep one '# ' description line above the target; move the rest into the recipe as '@#' lines." >&2
    exit 1
fi

echo "[lint-makefile-help] ok: every documented target has exactly one description line"
