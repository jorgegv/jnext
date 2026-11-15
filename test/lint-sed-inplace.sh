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
# SECOND HAZARD: a sed block closed straight after a command (`{n;p}`,
# `s/a/b/p}`, `{d}`). GNU sed accepts it; BSD sed reads the `}` as one more flag
# of the command (`bad flag in substitute command: '}'`, `extra characters at the
# end of p command`) and the script dies. That is how main's macos-regression went
# red (CI run 43653903261, GH #476): sd_expected_distro printed nothing, 44 rows
# failed. The portable spelling is `{n;p;}` (or a newline before the `}`).
# FLAGGED: a `}` whose previous command is not ended by `;` or a newline, in the
# script of a `sed` command (each -e word, BSD-style joined by newlines, else the
# first non-option word; the word after -f is skipped). Double-quoted scripts have
# their shell expansions read as the digit 1. NOT FLAGGED: a brace inside an
# address RE, an s/y part or an awk program; `}` straight after `{` or `}`.
# The same `# lint-sed-i: allow (<why>)` opts a line out of both hazards.
#
# SCOPE — stated, not implied. Tracked `*.sh` and `*.inc` under test/, except:
#   * test/packaging/ : run by `make package-test`, which is Linux only;
#   * test/harness-selftest.sh : its only run is the regression row
#     harness-selftest-func, which is tagged os=linux.
# Everything else (every row script, the harness, the unit-test runner) is run
# by macOS's `make regression-macos` or `make unit-test-sdl`. Each in-scope file
# is scanned to its last line; the lint refuses (exit 2) if it was not.
#
# CANNOT CATCH (EXAMPLES, not exhaustive), second hazard: a script spanning
# physical lines; a sed reached through a variable; every OTHER GNU-only sed
# spelling (`\x1b`, `\t`, `\S`, `\L`, the I/M flags, a `b`/`t` label followed by
# `;`): a different class, not covered here. First hazard: a quoted string spanning physical
# lines (quote state is reset at each line end, so the closing line of a
# multi-line awk/python program is read with its quotes inverted, and a
# `'; sed -i ...` there is missed). Carrying the state across lines was
# rejected on purpose: one lone apostrophe in a heredoc body would then hide
# the rest of the file, a worse failure than this rare miss. This lint's scope
# is narrower than lint-traps' on purpose: it targets the accidental bare
# `sed -i`, not deliberate or exotic spellings. Also not caught: a sed
# reached through a variable, eval or alias; an option spelled via a here-doc body (scanned like code, so
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
    awk -v MK="$(printf '\001')" '
        # SKEL = the line with every quoted string collapsed to the inert word
        # X and any comment removed (three-state scanner, as lint-traps.sh).
        function analyse(l,   i, n, c, nxt) {
            SKEL = ""; SKEL2 = ""; NQ = 0; qst = 0; qcontent = ""
            n = length(l)
            for (i = 1; i <= n; i++) {
                c = substr(l, i, 1)
                if (qst == 0) {
                    if (c == "\\") {
                        nxt = substr(l, i + 1, 1)
                        if (nxt != "") { SKEL = SKEL (nxt ~ /[A-Za-z0-9_]/ ? nxt : "X"); SKEL2 = SKEL2 (nxt ~ /[A-Za-z0-9_]/ ? nxt : "X") }
                        i++; continue
                    }
                    if (c == "\047") { qst = 1; qcontent = ""; continue }
                    if (c == "\"")   { qst = 2; qcontent = ""; continue }
                    if (c == "#" && (i == 1 || substr(l, i-1, 1) ~ /[ \t]/)) return
                    SKEL = SKEL c; SKEL2 = SKEL2 c; continue
                }
                if (qst == 1) {
                    if (c == "\047") { qst = 0; close_quote(); continue }
                    qcontent = qcontent c; continue
                }
                if (c == "\\") {
                    nxt = substr(l, i + 1, 1)
                    if (nxt ~ /["\\$`]/) { qcontent = qcontent nxt; i++ } else qcontent = qcontent c
                    continue
                }
                if (c == "\"") { close_quote(); qst = 0; continue }
                qcontent = qcontent c
            }
            if (qst != 0 && length(qcontent) > 0) { SKEL = SKEL "X"; SKEL2 = SKEL2 "X" }
        }
        # close_quote() — a quoted string just ended: SKEL gets the inert X, SKEL2 a
        # marker token that names the kept content (QS) and its quote kind (QD).
        function close_quote() {
            if (length(qcontent) > 0) {
                NQ++; QS[NQ] = qcontent; QD[NQ] = (qst == 2)
                SKEL = SKEL "X"; SKEL2 = SKEL2 MK NQ MK
            }
            qcontent = ""
        }
        # skip_to(sc, i, d) — index just after the next unescaped d at or after i.
        function skip_to(sc, i, d,   n, ch) {
            n = length(sc)
            while (i <= n) {
                ch = substr(sc, i, 1)
                if (ch == "\\") { i += 2; continue }
                if (ch == d) return i + 1
                i++
            }
            return n + 1
        }
        # sed_unclosed(sc) — 1 if the sed script sc closes a block with "}" straight
        # after a command (no ";" or newline between them). A small tokenizer for the
        # POSIX subset: addresses, { }, s and y (parts skipped), rest-of-line commands.
        function sed_unclosed(sc,   i, n, c, d, prev, rest) {
            n = length(sc); i = 1; prev = 0
            while (i <= n) {
                c = substr(sc, i, 1)
                if (c == " " || c == "\t") { i++; continue }
                if (c == ";" || c == "\n") { prev = 0; i++; continue }
                if (c == "/" || c == "\\") {
                    if (c == "\\") { d = substr(sc, i + 1, 1); i += 2 } else { d = "/"; i++ }
                    i = skip_to(sc, i, d); continue
                }
                if (c ~ /[0-9$,!~+]/) { i++; continue }
                if (c == "{") { prev = 0; i++; continue }
                if (c == "}") { if (prev) return 1; prev = 0; i++; continue }
                if (c == "s" || c == "y") {
                    d = substr(sc, i + 1, 1)
                    i = skip_to(sc, i + 2, d); i = skip_to(sc, i, d)
                    if (c == "s") {
                        while (i <= n) {
                            c = substr(sc, i, 1)
                            if (c == ";" || c == "\n" || c == "}") break
                            if (c == "w") { rest = index(substr(sc, i), "\n"); i = rest ? i + rest - 1 : n + 1; break }
                            i++
                        }
                    }
                    prev = 1; continue
                }
                if (c ~ /[aicrwbtT:#RW]/) {
                    rest = index(substr(sc, i), "\n"); i = rest ? i + rest : n + 1
                    prev = 0; continue
                }
                prev = 1; i++
            }
            return 0
        }
        # piece_text(tok) — the text of one script word: each marker token replaced by
        # its quoted content (a double-quoted one with shell expansions turned to 1).
        function piece_text(tok,   out, p, q, k, v) {
            out = ""
            while ((p = index(tok, MK)) > 0) {
                out = out substr(tok, 1, p - 1)
                tok = substr(tok, p + 1)
                q = index(tok, MK)
                k = substr(tok, 1, q - 1) + 0
                tok = substr(tok, q + 1)
                v = QS[k]
                if (QD[k]) {
                    gsub(/\$\{[^}]*\}/, "1", v)
                    gsub(/\$\(\([^)]*\)\)/, "1", v)
                    gsub(/\$\([^)]*\)/, "1", v)
                    gsub(/\$[A-Za-z_][A-Za-z0-9_]*/, "1", v)
                    gsub(/\$[0-9#?@*]/, "1", v)
                }
                out = out v
            }
            return out tok
        }
        # sed_block_unclosed(s) — 1 if a `sed` command in skeleton s has a script that
        # closes a block without ";". Script words: each -e word (BSD joins them with
        # newlines), else the first non-option word. The word after -f is skipped.
        function sed_block_unclosed(s,   t, toks, nt, j, k, insed, have_e, got, expect_e, skipn, script) {
            t = s
            gsub(/[;&|()`]/, " SEP ", t)
            gsub(/\$\{?/, " ", t)
            nt = split(t, toks, /[[:space:]]+/)
            insed = 0; script = ""
            for (j = 1; j <= nt + 1; j++) {
                k = (j <= nt) ? toks[j] : "SEP"
                if (k == "") continue
                if (k == "SEP") {
                    if (insed && script != "" && sed_unclosed(script)) return 1
                    insed = 0; script = ""; continue
                }
                if (!insed) {
                    if (k ~ /(^|\/)sed$/) { insed = 1; have_e = 0; got = 0; expect_e = 0; skipn = 0; script = "" }
                    continue
                }
                if (expect_e) { script = script (script == "" ? "" : "\n") piece_text(k); expect_e = 0; continue }
                if (skipn) { skipn = 0; continue }
                if (k ~ /^-[A-Za-z]*e$/) { expect_e = 1; have_e = 1; continue }
                if (k ~ /^-e./) { script = script (script == "" ? "" : "\n") piece_text(substr(k, 3)); have_e = 1; continue }
                if (k ~ /^-[A-Za-z]*f$/) { skipn = 1; continue }
                if (k ~ /^-/) continue
                if (!have_e && !got) { script = piece_text(k); got = 1 }
            }
            return 0
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
            } else if (sed_block_unclosed(SKEL2)) {
                t = $0; gsub(/^[ \t]+/, "", t)
                print "OFFENDER\t" path ":" FNR ": [sed block closed without \047;\047] " substr(t, 1, 160)
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
        1 'empty dquote after -i'     'sed -i"" "s/a/b/" f'
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
        1 'block close: kDistroUrl line' "    n=\$(sed -n '/^const char\* const kDistroUrl =\$/{n;s#.*/\\([^/\"]*\\)\";\$#\\1#p}' \"\$src\" 2>/dev/null)"
        1 'block close: ghidra line'  "sed -n '/A/,/B/{s/^L //p}' f"
        1 'block close: p}'           "sed -n '/x/{p}' f"
        1 'block close: d}'           "sed '/x/{d}' f"
        1 'block close: after -e'     "sed -n -e '/x/{n;p}' f"
        1 'block close: dquotes'      'sed -n "/x/{p}" f'
        1 'block close: -E, a{2} RE'  "sed -nE '/x/{s/a{2}/b/p}' f"
        1 'block close: inside $( )'  "x=\$(sed -n '/x/{p}' f)"
        0 'block close: fixed kDistroUrl line' "    n=\$(sed -n '/^const char\* const kDistroUrl =\$/{n;s#.*/\\([^/\"]*\\)\";\$#\\1#p;}' \"\$src\" 2>/dev/null)"
        0 'block close: {p;}'         "sed -n '/x/{p;}' f"
        0 'brace in s RE (escaped)'   "sed 's/a\\{2\\}/b/' f"
        0 'brace in s RE (-E)'        "sed -E 's/^(P[0-9]{2}):/\\1/' f"
        0 'dquote ${var}p'            'sed -n "${first}p" f'
        0 'dquote $(( ))p'            'sed -n "$(( a + 1 ))p" f'
        0 'brace in an address RE'    "sed -n '/^menu_index() {/,/^}/p' f"
        0 'braces in y'               "sed 'y/{}/()/' f"
        0 '-e pieces are newline-joined' "sed -n -e '/x/{p' -e '}' f"
        0 'awk program with a block'  "awk '/x/{print}' f"
        0 'sed block in a comment'    "# sed -n '/x/{p}' f"
        0 'block close, opted out'    "sed -n '/x/{p}' f  # lint-sed-i: allow (fixture)"
        0 'block close in an echo'    "echo '/x/{p}'"
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
    echo "[lint-sed-i] a bare in-place sed (no attached suffix), or a sed block closed without ';', in a script macOS runs:"
    sed "s#^$PROJECT_DIR/##;s/^/  /" <<<"$OFFENDERS"
    echo ""
    echo "BSD sed (macOS) takes the next word after -i as the suffix, so the script is"
    echo "lost and it reads stdin. Do not edit in place:"
    echo ""
    echo "    sed 's/a/b/' f > f.new && mv f.new f"
    echo ""
    echo "(or -i.bak with an attached suffix, then rm the .bak)."
    echo ""
    echo "BSD sed (macOS) needs a ';' or a newline before the '}' that closes a block:"
    echo "{n;p;}, not {n;p}."
    echo "Mark fixture text with a trailing  # lint-sed-i: allow (<why>)"
} >&2
exit 1
