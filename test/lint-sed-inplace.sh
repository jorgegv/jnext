#!/usr/bin/env bash
# Bare in-place `sed` lint for test scripts that macOS runs.
#
# WHY THIS EXISTS
#
# GNU sed's in-place option takes an OPTIONAL suffix attached to it (-i, -i.bak);
# BSD sed's (macOS) takes a MANDATORY suffix argument, so `sed -i 's/a/b/' f`
# reads the script as the suffix, finds no script, and falls back to stdin:
#     sed: -I or -i may not be used with stdin
# That is how warm-start-chans-func died on CI's macos-regression job (run
# 42508978745) while passing on every Linux run. The one spelling both seds
# accept is an ATTACHED suffix (`sed -i.bak ...; rm f.bak`); simplest of all is
# to not edit in place: `sed ... f > f.new && mv f.new f`.
#
# FLAGGED: a `sed` command with an option cluster ending in `i` (-i, -ni, -Ei,
# -i -e, `-i ''`), or ANY long option starting `--i` (--in-place, --in-place=SUF,
# --in-pl: BSD sed has no long options at all). The sed word may carry a path
# (`/usr/bin/sed -i`).
# NOT FLAGGED: -i.bak (attached suffix), no in-place at all.
#
# SCOPE — stated, not implied. Tracked `*.sh` and `*.inc` under test/, except:
#   * test/packaging/ : run by `make package-test`, which is Linux only;
#   * test/harness-selftest.sh : its only run is the regression row
#     harness-selftest-func, which is tagged os=linux.
# Everything else (every row script, the harness, the unit-test runner) is run
# by macOS's `make regression-macos` or `make unit-test-sdl`. Each in-scope file
# is scanned to its last line; the lint refuses (exit 2) if it was not.
#
# CANNOT CATCH (EXAMPLES, not exhaustive): a quoted string spanning physical
# lines (quote state is reset at each line end, so the closing line of a
# multi-line awk/python program is read with its quotes inverted, and a
# `'; sed -i ...` there is missed); a sed reached through a variable,
# eval or alias; an option spelled via a here-doc body (scanned like code, so
# it flags the safe way: mark fixture text); a sed split after a backslash
# continuation (lines are not joined; options sit on the sed line in practice).
# The escape for a deliberate case is a trailing
# `# lint-sed-i: allow (<why>)` on the line.
#
# Env (TEST ONLY, set by harness-selftest to prove the lint is wired into the
# preflight; regression.sh never sets it):
#   JNEXT_LINT_SED_I_DIR   scan <dir>/*.sh, all in scope
#
# Exit: 0 clean, 1 offenders, 2 self-test failed / file list unreadable /
# a file not scanned whole.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# scan_awk <path>... — prints "OFFENDER<TAB><path>:<line>: <text>" per offending
# line and "CHECKED<TAB><path><TAB><n>" per file (physical lines scanned).
scan_awk() {
    [[ $# -gt 0 ]] || return 0
    awk '
        # SKEL = the line with every quoted string collapsed to the inert word
        # X and any comment removed (three-state scanner, as lint-traps.sh).
        function analyse(l,   i, n, c, nxt) {
            SKEL = ""; qst = 0; qcontent = ""
            n = length(l)
            for (i = 1; i <= n; i++) {
                c = substr(l, i, 1)
                if (qst == 0) {
                    if (c == "\\") {
                        nxt = substr(l, i + 1, 1)
                        if (nxt != "") SKEL = SKEL (nxt ~ /[A-Za-z0-9_]/ ? nxt : "X")
                        i++; continue
                    }
                    if (c == "\047") { qst = 1; qcontent = ""; continue }
                    if (c == "\"")   { qst = 2; qcontent = ""; continue }
                    if (c == "#" && (i == 1 || substr(l, i-1, 1) ~ /[ \t]/)) return
                    SKEL = SKEL c; continue
                }
                if (qst == 1) {
                    if (c == "\047") { qst = 0; if (length(qcontent) > 0) SKEL = SKEL "X"; qcontent = ""; continue }
                    qcontent = qcontent c; continue
                }
                if (c == "\\") {
                    nxt = substr(l, i + 1, 1)
                    if (nxt ~ /["\\$`]/) { qcontent = qcontent nxt; i++ } else qcontent = qcontent c
                    continue
                }
                if (c == "\"") { qst = 0; if (length(qcontent) > 0) SKEL = SKEL "X"; qcontent = ""; continue }
                qcontent = qcontent c
            }
            if (qst != 0 && length(qcontent) > 0) SKEL = SKEL "X"
        }
        # bare_inplace(s) — 1 if a `sed` command in skeleton s carries an
        # in-place option with no attached suffix.
        function bare_inplace(s,   t, toks, nt, j, k, insed) {
            t = s
            gsub(/[;&|()`]/, " SEP ", t)
            gsub(/\$\{?/, " ", t)
            nt = split(t, toks, /[[:space:]]+/)
            insed = 0
            for (j = 1; j <= nt; j++) {
                k = toks[j]
                if (k == "") continue
                if (k == "SEP") { insed = 0; continue }
                if (k ~ /(^|\/)sed$/) { insed = 1; continue }
                if (!insed) continue
                if (k ~ /^--i/) return 1
                if (k ~ /^--/) continue
                if (k ~ /^-[A-Za-z0-9]*i$/) return 1
            }
            return 0
        }
        FNR == 1 { if (NR > 1) print "CHECKED\t" path "\t" checked; path = FILENAME; checked = 0 }
        {
            checked++
            if ($0 ~ /#[ \t]*lint-sed-i:[ \t]*allow/) next
            analyse($0)
            if (bare_inplace(SKEL)) {
                t = $0; gsub(/^[ \t]+/, "", t)
                print "OFFENDER\t" path ":" FNR ": " substr(t, 1, 160)
            }
        }
        END { if (NR > 0) print "CHECKED\t" path "\t" checked }
    ' "$@"
}

file_lines() {
    local n
    n=$(wc -l < "$1")
    [[ -s "$1" && -n "$(tail -c 1 "$1")" ]] && n=$((n + 1))
    echo "$n"
}

# verify_coverage <awk-output> <path>... — fails loud if a file was not scanned whole.
verify_coverage() {
    local out=$1 f want got bad=0
    shift
    declare -A seen=()
    while IFS=$'\t' read -r tag p n; do
        [[ "$tag" == CHECKED ]] && seen["$p"]=$n
    done <<<"$out"
    for f in "$@"; do
        want=$(file_lines "$f")
        got=${seen[$f]:-0}
        if [[ "$got" != "$want" ]]; then
            echo "[lint-sed-i] COVERAGE LOSS: $f — $got of $want lines checked" >&2
            bad=1
        fi
    done
    return "$bad"
}

scan_files() {
    local out
    [[ $# -gt 0 ]] || return 0
    out=$(scan_awk "$@")
    verify_coverage "$out" "$@" || return 2
    sed -n 's/^OFFENDER\t//p' <<<"$out"
}

# Pins the matcher both ways on every invocation: want=1 must be reported.
selftest() {
    local d pass=0 fail=0 want desc text got rc out i=0
    d=$(mktemp -d)
    local -a cases=(
        1 'bare -i'                   "sed -i 's/a/b/' f"
        1 'bare -i, double quotes'    'sed -i "s/a/b/" "$f"'
        1 '-i -e'                     "sed -i -e 's/a/b/' f"
        1 'cluster -ni'               "sed -ni 's/a/b/p' f"
        1 'cluster -Ei'               "sed -Ei 's/a/b/' f"
        1 'BSD empty suffix'          "sed -i '' 's/a/b/' f"
        1 'long --in-place'           "sed --in-place 's/a/b/' f"
        1 'in a pipeline'             "cat x | sed -i 's/a/b/' f"
        1 'after && '                 "cd d && sed -i 's/a/b/' f"
        1 'inside $( )'               "x=\$(sed -i 's/a/b/' f)"
        1 'option after the script'   "sed 's/a/b/' -i f"
        1 'the warm-start-chans line' "        sed -i 's/^50_60hz=.*/50_60hz=1/' \"\$wc_w/config.ini\""
        0 'attached suffix'           "sed -i.bak 's/a/b/' f"
        1 'long with suffix'          "sed --in-place=.bak 's/a/b/' f"
        1 'long, empty suffix'        "sed --in-place= 's/a/b/' f"
        1 'long, abbreviated'         "sed --in-pl 's/a/b/' f"
        1 'sed given by path'         "/usr/bin/sed -i 's/a/b/' f"
        1 'empty quote after -i'      "sed -i'' 's/a/b/' f"
        1 'hash inside a word'        "echo a#b; sed -i 's/a/b/' f"
        1 'escaped apostrophe'        "echo it\\'s; sed -i 's/a/b/' f"
        1 'escaped quote in dquotes'  'echo "a \" b"; sed -i "s/a/b/" f'
        0 'separator ends the sed'    "sed -n 's/a/b/p' f; grep -i x f"
        0 'temp file and mv'          "sed 's/a/b/' f >f.new && mv f.new f"
        0 'sed -n print'              "sed -n 's/a/b/p' f"
        0 'sed -E no in-place'        "sed -E 's/a/b/' f"
        0 'script text holds -i'      "sed 's/ -i /x/' f"
        0 '-i in a quoted string'     "echo \"run sed -i here\""
        0 'in a comment'              "true  # sed -i 's/a/b/' f"
        0 '-i is another tool'        "grep -i x f; awk -i inplace 1 f"
        0 'opted out'                 "sed -i 's/a/b/' f  # lint-sed-i: allow (demo)"
    )
    while (( i < ${#cases[@]} )); do
        want=${cases[i]}; desc=${cases[i+1]}; text=${cases[i+2]}
        i=$((i + 3))
        printf '%s\n' "$text" > "$d/case.sh"
        got=0; rc=0
        out=$(scan_files "$d/case.sh") || rc=$?
        [[ -n "$out" ]] && got=1
        if [[ "$rc" == 0 && "$got" == "$want" ]]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            echo "[lint-sed-i] SELFTEST FAIL: $desc (want $want, got $got, rc $rc): $text" >&2
        fi
    done
    printf '%s\n' 'a' 'b' 'c' > "$d/three.sh"
    if ! verify_coverage $'CHECKED\t'"$d/three.sh"$'\t2' "$d/three.sh" 2>/dev/null; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-sed-i] SELFTEST FAIL: a 2-of-3-lines scan was accepted" >&2; fi
    if verify_coverage $'CHECKED\t'"$d/three.sh"$'\t3' "$d/three.sh"; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-sed-i] SELFTEST FAIL: a full scan was refused" >&2; fi
    printf 'a\nb' > "$d/nonl.sh"
    if [[ "$(file_lines "$d/nonl.sh")" == 2 ]] && verify_coverage $'CHECKED\t'"$d/nonl.sh"$'\t2' "$d/nonl.sh"; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-sed-i] SELFTEST FAIL: a file with no final newline was not counted as 2 lines" >&2; fi
    rm -rf "$d"
    if (( fail > 0 )); then
        echo "[lint-sed-i] self-test: $pass passed, $fail FAILED" >&2
        return 1
    fi
    SELFTEST_PASSED=$pass
}

selftest || exit 2

FILES=()
if [[ -n "${JNEXT_LINT_SED_I_DIR:-}" ]]; then
    [[ -d "$JNEXT_LINT_SED_I_DIR" ]] || {
        echo "ERROR: JNEXT_LINT_SED_I_DIR is not a directory: $JNEXT_LINT_SED_I_DIR" >&2
        exit 2
    }
    for f in "$JNEXT_LINT_SED_I_DIR"/*.sh; do
        [[ -e "$f" ]] && FILES+=("$f")
    done
else
    mapfile -t FILES < <(git -C "$PROJECT_DIR" ls-files -- 'test/*.sh' 'test/*.inc' \
                         | grep -v -e '^test/packaging/' -e '^test/harness-selftest\.sh$' \
                         | sed "s#^#$PROJECT_DIR/#")
    [[ ${#FILES[@]} -gt 0 ]] || {
        echo "ERROR: no tracked test/*.sh found — is this a git checkout?" >&2
        exit 2
    }
fi
rc=0
OFFENDERS=$(scan_files "${FILES[@]}") || rc=$?
if [[ "$rc" != 0 ]]; then
    echo "[lint-sed-i] REFUSED: a file was not scanned to its last line (see above)" >&2
    exit 2
fi

if [[ -z "$OFFENDERS" ]]; then
    echo "[lint-sed-i] scanned: ${#FILES[@]} shell scripts, each to its last line  offenders: 0  (self-test ${SELFTEST_PASSED} cases)"
    exit 0
fi

echo "[lint-sed-i] scanned: ${#FILES[@]} shell scripts, each to its last line  offenders: $(wc -l <<<"$OFFENDERS" | tr -d ' ')"
{
    echo "[lint-sed-i] a bare in-place sed (no attached suffix) in a script macOS runs:"
    sed "s#^$PROJECT_DIR/##;s/^/  /" <<<"$OFFENDERS"
    echo ""
    echo "BSD sed (macOS) takes the next word after -i as the suffix, so the script is"
    echo "lost and it reads stdin. Do not edit in place:"
    echo ""
    echo "    sed 's/a/b/' f > f.new && mv f.new f"
    echo ""
    echo "(or -i.bak with an attached suffix, then rm the .bak)."
    echo "Mark fixture text with a trailing  # lint-sed-i: allow (<why>)"
} >&2
exit 1
