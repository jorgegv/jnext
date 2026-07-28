#!/usr/bin/env bash
# Contract: a failing bundle step must ABORT its package recipe (GH #148).
#
# The five package-win* recipes are each written as ONE backslash-continued
# shell command, so make sees a single exit status: the LAST command's. A step
# terminated with `;` therefore cannot fail the recipe. `package-win` ran
#
#     bash packaging/windows/bundle-dlls.sh <exe> "$stage"; \
#
# with nothing after it that read the bundle, so a failed bundle still zipped
# the docs, printed "ZIP(s) produced:" and exited 0 — a published artifact with
# no executable in it. Reproduced before the fix: 2 MB zip, 165 files, zero
# .exe and zero .dll, `make` exit 0.
#
# THE RULE. Every bundle-dlls.sh invocation must be in one of these shapes:
#
#   own-line   it is a complete recipe line — make checks that line's status
#              itself (this is how win-release and its four siblings call it)
#   &&         it is inside a continued chain and terminated with `&&`
#   || exit    it carries an explicit failure guard
#
# A `;` terminator inside a continued chain is the defect, and it is the only
# thing this lint reports.
#
# WHAT THIS DELIBERATELY DOES NOT CHECK. The other half of the #148 fix is the
# structural post-bundle check (`for f in jnext.exe Qt6Core.dll …`) that the
# siblings already had. "Has a structural check" cannot be stated structurally:
# any grep for a guard would pass on an `exit 1` that tests something else
# entirely, and a lint that reads as coverage while proving nothing is worse
# than no lint (same reasoning as the traceability extractor's rejected
# banner-comment tier). The abort rule above IS checkable, and it is what turns
# a failed bundle into a failed build; the content assertions are proven
# instead by the package-win* rows of packaging-test.sh, against real zips.
#
# Usage:  bash test/packaging/package-recipe-guard-test.sh [MAKEFILE]
# Exit:   0 clean · 1 violations found · 2 self-test failed / bad usage

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MAKEFILE="${1:-$PROJECT_DIR/Makefile}"

# scan <makefile> -> one line per violation:  <line-no>:<verdict>
#
# For each bundle-dlls.sh line, walk forward to the end of the LOGICAL shell
# command (a command may itself be split across continuations) and classify how
# that command is terminated.
scan() {
    awk -v COUNT="${2:-0}" '
        { line[NR] = $0 }
        END {
            for (i = 1; i <= NR; i++) {
                if (line[i] !~ /bundle-dlls\.sh/) continue
                # A comment naming the script is not an invocation. Recipe
                # rationale is written as tab-indented `@#`, file-level as `#`.
                if (line[i] ~ /^[ \t]*@?#/) continue
                if (COUNT) { n++; continue }
                verdict = "eof"
                for (j = i; j <= NR; j++) {
                    s = line[j]
                    cont = (s ~ /\\[ \t]*$/)
                    sub(/\\[ \t]*$/, "", s)
                    sub(/[ \t]+$/, "", s)
                    if (s ~ /\|\|[ \t]*(exit|\{)/) { verdict = "guarded";  break }
                    if (!cont)                     { verdict = "own-line"; break }
                    if (s ~ /&&$/)                 { verdict = "and";      break }
                    if (s ~ /;$/)                  { verdict = "semi";     break }
                    if (s ~ /\|\|$/)               { verdict = "or";       break }
                    # else: the command itself continues onto the next line
                }
                if (verdict != "own-line" && verdict != "and" && verdict != "guarded")
                    print i ":" verdict
            }
            if (COUNT) print n + 0
        }
    ' "$1"
}

# count <makefile> -> how many real invocations the scan classified
count() { scan "$1" 1; }

# --- self-test: prove the scanner discriminates, on every run (~10 ms) -------
# A checker nobody has seen fail is not evidence. Every accepted shape and every
# rejected shape below has a fixture, including the one that made the naive
# version wrong: a bundle-dlls.sh call split across two continuation lines, where
# the terminator lives on a LATER line than the invocation.
selftest() {
    local dir good bad got want rc=0
    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' RETURN
    good="$dir/good.mk"
    bad="$dir/bad.mk"

    printf '%s\n' \
        'own-line:'                                                       \
        '	bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe $(D)' \
        '	@printf "done\n"'                                         \
        ''                                                                \
        'chained-and:'                                                    \
        '	@stage=x && \'                                            \
        '	 bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe "$$stage" && \' \
        '	 zip -rq out.zip "$$stage"'                               \
        ''                                                                \
        'explicitly-guarded:'                                             \
        '	@stage=x; \'                                              \
        '	 bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe "$$stage" || exit 1; \' \
        '	 zip -rq out.zip "$$stage"'                               \
        ''                                                                \
        'split-across-lines-and:'                                         \
        '	@stage=x && \'                                            \
        '	 bash packaging/windows/bundle-dlls.sh \'                 \
        '	     $(D)/jnext.exe "$$stage" && \'                       \
        '	 zip -rq out.zip "$$stage"'                               > "$good"

    # Every shape here leaves a failing bundle unable to fail the recipe.
    printf '%s\n' \
        'chained-semicolon:'                                              \
        '	@stage=x; \'                                              \
        '	 bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe "$$stage"; \' \
        '	 zip -rq out.zip "$$stage"'                               \
        ''                                                                \
        'split-across-lines-semicolon:'                                   \
        '	@stage=x; \'                                              \
        '	 bash packaging/windows/bundle-dlls.sh \'                 \
        '	     $(D)/jnext.exe "$$stage"; \'                         \
        '	 zip -rq out.zip "$$stage"'                               > "$bad"

    got="$(scan "$good")"
    [ -z "$got" ] || { echo "SELFTEST FAIL: flagged a clean fixture: $got" >&2; rc=1; }

    want='3:semi
8:semi'
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
    echo "[package-recipe-guard] a failing bundle step would NOT abort its recipe:" >&2
    while IFS=: read -r lineno verdict; do
        printf "  %s:%s: bundle-dlls.sh invocation terminated with '%s' inside a continued shell chain\n" \
            "${MAKEFILE#"$PROJECT_DIR"/}" "$lineno" \
            "$([ "$verdict" = semi ] && echo ';' || echo "$verdict")" >&2
    done <<< "$violations"
    echo "  make sees only the LAST command's status, so the recipe would still exit 0 and ship the zip (GH #148)." >&2
    echo "  Fix: terminate it with '&&' (see package-win and its four siblings)." >&2
    exit 1
fi

echo "[package-recipe-guard] ok: all $(count "$MAKEFILE") bundle-dlls.sh invocations abort their recipe on failure"
