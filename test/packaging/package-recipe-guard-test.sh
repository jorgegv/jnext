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
# THE RULE. Look at the text following the invocation and find the FIRST shell
# separator that could take over the exit status make sees. The invocation is
# accepted only if that separator is one make cannot mask a failure through:
#
#   own-line   there is no separator at all and the line does not continue —
#              the invocation IS the recipe line, and make checks its status
#              (this is how win-release and its four siblings call it)
#   &&         the next command is short-circuited away by a failure
#   || exit    an explicit failure guard (`|| exit`, `|| { ...`)
#
# `;`, `|` and `&` are reported: each hands the recipe's exit status to the
# command after it, which is #148 exactly.
#
# The separator is looked for AFTER the invocation on its own physical line,
# not merely at the line's end — that distinction is the whole point, and the
# first version of this lint got it wrong. It decided "own-line" from the
# absence of a trailing `\` alone, so
#
#     @bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe $(D); echo "ZIP(s) produced"
#
# passed the lint while real `make` printed the bundle's error, printed the
# banner and exited 0 — the reported bug, on one line, through a checker
# written to catch it. The same hole covered the chained form
# (`bundle-dlls.sh ...; echo hi && \`), which the end-of-line test called safe
# because it only ever read the last two characters.
#
# LIMIT, stated rather than papered over: only the FIRST separator is
# classified. A `;` further down a chain (`bundle && a ; b`) would still mask a
# failure and is not reported, because deciding that needs real shell parsing of
# compound commands — the five recipes are full of `;` inside `for`/`if` bodies,
# where it is syntax and not a chain separator. The five are uniformly
# `&&`-chained today; this lint pins the step that was actually wrong.
# Separators are matched textually, so one inside a quoted argument would be
# reported: conservative in the safe direction (report, never miss).
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
# For each bundle-dlls.sh line, find the first shell separator following the
# invocation and classify it. The search walks forward across continuations,
# because the invocation itself may be split over several physical lines and the
# separator then lives on a later one.
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
                    # On the invocation line only the text AFTER the command can
                    # take the status; later lines are examined whole, because a
                    # long invocation may be split across continuations.
                    if (j == i) sub(/^.*bundle-dlls\.sh/, "", s)
                    cont = (s ~ /\\[ \t]*$/)
                    sub(/\\[ \t]*$/, "", s)
                    sub(/[ \t]+$/, "", s)
                    # A redirection `2>&1` / `>&2` is not a background `&`.
                    gsub(/[0-9]?[<>]&[0-9-]?/, "", s)
                    if (match(s, /&&|\|\||;|\||&/)) {
                        sep  = substr(s, RSTART, RLENGTH)
                        rest = substr(s, RSTART + RLENGTH)
                        if      (sep == "&&")                             verdict = "and"
                        else if (sep == "||" && rest ~ /^[ \t]*(exit|\{)/) verdict = "guarded"
                        else if (sep == "||")                             verdict = "or"
                        else if (sep == ";")                              verdict = "semi"
                        else if (sep == "|")                              verdict = "pipe"
                        else                                              verdict = "bg"
                        break
                    }
                    if (!cont) { verdict = "own-line"; break }
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
# A checker nobody has seen fail is not evidence, and a checker whose fixtures
# share its blind spot is not self-testing. Both of the shapes that defeated the
# end-of-line version are fixtures here — `own-line-evade` (the invocation and a
# second command on ONE line, no continuation at all) and `chained-evade` (the
# `;` mid-line with an `&&` terminator after it) — alongside the split-across-
# lines cases, where the terminator lives on a LATER line than the invocation.
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
        'own-line-with-redirect:'                                         \
        '	bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe $(D) >/dev/null 2>&1' \
        ''                                                                \
        'chained-and:'                                                    \
        '	@stage=x && \'                                            \
        '	 bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe "$$stage" && \' \
        '	 zip -rq out.zip "$$stage"'                               \
        ''                                                                \
        'and-then-more-on-the-same-line:'                                 \
        '	@stage=x && \'                                            \
        '	 bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe "$$stage" && echo staged && \' \
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
        '	 zip -rq out.zip "$$stage"'                               \
        ''                                                                \
        'own-line-evade:'                                                 \
        '	@bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe $(D); echo "ZIP(s) produced"' \
        ''                                                                \
        'chained-evade:'                                                  \
        '	@stage=x && \'                                            \
        '	 bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe "$$stage"; echo hi && \' \
        '	 zip -rq out.zip "$$stage"'                               \
        ''                                                                \
        'piped:'                                                          \
        '	bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe $(D) | tee bundle.log' \
        ''                                                                \
        'backgrounded:'                                                   \
        '	bash packaging/windows/bundle-dlls.sh $(D)/jnext.exe $(D) &' \
        > "$bad"

    got="$(scan "$good")"
    [ -z "$got" ] || { echo "SELFTEST FAIL: flagged a clean fixture: $got" >&2; rc=1; }

    want='3:semi
8:semi
13:semi
17:semi
21:pipe
24:bg'
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
        case "$verdict" in
            semi) why="followed by ';' — the next command's status is what make sees" ;;
            pipe) why="piped — the status is the LAST stage's, and there is no pipefail here" ;;
            bg)   why="backgrounded with '&' — make never sees its status at all" ;;
            or)   why="followed by an unguarded '||' — the fallback's status wins" ;;
            eof)  why="its command chain runs off the end of the file unterminated" ;;
            *)    why="classified '$verdict'" ;;
        esac
        printf "  %s:%s: bundle-dlls.sh invocation %s\n" \
            "${MAKEFILE#"$PROJECT_DIR"/}" "$lineno" "$why" >&2
    done <<< "$violations"
    echo "  A failed bundle would then leave the recipe exiting 0 and shipping the zip (GH #148)." >&2
    echo "  Fix: chain it with '&&' (see package-win and its four siblings), or give it '|| exit 1'." >&2
    exit 1
fi

echo "[package-recipe-guard] ok: all $(count "$MAKEFILE") bundle-dlls.sh invocations abort their recipe on failure"
