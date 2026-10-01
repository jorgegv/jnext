#!/usr/bin/env bash
# `producer | grep -q` lint for test scripts that run under `pipefail`.
#
# WHY THIS EXISTS
#
# `grep -q` exits the moment it finds a match. Whatever is still writing into
# the pipe then gets SIGPIPE and dies with status 141, and `set -o pipefail`
# makes 141 the status of the whole pipeline. So under pipefail
#
#     echo "$out" | grep -q "expected line"
#
# reports NO MATCH for a line that is there, whenever the writer has more left
# to write after grep has read the match: always, for output larger than the
# pipe's capacity; and only sometimes, depending on load and per-user pipe
# pressure, for output a little smaller. It is a row that flips its verdict
# with nobody having changed anything. A negated check turns it the other way:
# `echo "$out" | grep -q "must not appear" && fail` can PASS a defect.
#
# NOT HYPOTHETICAL. warm-start-func failed under load with "C: a cache with
# the wrong SD digest was NOT refused", then passed 5 of 5. A traced run of
# the same row caught it red-handed: grep exited 0 (it found the line) and the
# `echo "$ws_a"` subshell was "killed by SIGPIPE"; the row recorded "A: a cold
# cache did not boot the firmware" for output that said exactly that.
# regression.sh had already met this once, for its own `printf | grep -qx`
# membership test (the note above IS_FILTERED), and fixed that one call. The
# 50 rows' worth of the same pattern were fixed by hand in one pass; this file
# is the gate that keeps a 51st from arriving.
#
# The safe idioms ask grep alone for the verdict:
#
#     grep -q "expected line" <<<"$out"            # a string you already have
#     grep -q "expected line" <<<"$(producer)"     # a command: capture first
#
# A here-string has no writer process, so there is nothing to SIGPIPE.
#
# ------------------------------------------------------------------------
# SCOPE — stated, not implied.
#
# Files: tracked `*.sh` and `*.inc` under test/, recursively, that run under
# pipefail. "Under pipefail" means the file has a `set` command naming
# pipefail anywhere, or it lives under test/00regression/ — the harness sources
# every row there into its own `set -euo pipefail` shell. Decided per WHOLE
# file, and a file in scope is scanned from its first line to its last: the
# lint counts the lines that reached the matcher and REFUSES the run (exit 2)
# if that is not every line of every in-scope file. A file that does NOT run
# under pipefail is out of scope, because the hazard does not exist there:
# test/packaging/packaging-test.sh leaves pipefail off for exactly this reason
# and says so in its header.
#
# What is flagged: a pipe `|` or `|&` (not `||`) whose next command is grep
# with a quiet option — a short cluster containing q (`-q`, `-qE`, `-Fq`), or
# `--quiet` / `--silent`, before or after the pattern (GNU grep permutes) — on
# a logical line (backslash continuations joined). `|&` is covered because it
# is the same pipe with stderr added: the writer dies of the same SIGPIPE.
#
# CANNOT CATCH (EXAMPLES, not an exhaustive list — two "exhaustive" lists in
# this tree's lints have already been proved incomplete):
#   * Other early-exit consumers: `| head`, `| grep -m N`, `| sed q`. In this
#     tree they capture a VALUE (`x=$(cmd | head -1) || true`), where the
#     value head saw is right whatever the producer's status; the two that
#     were not masked were fixed by hand. A status-only consumer is what flips
#     a verdict, and grep -q is that consumer.
#   * A pipeline assembled at run time (eval, a command in a variable), or
#     split across lines in a way the continuation join does not see (a
#     newline straight after the `|`).
#   * A quiet grep reached by any other spelling (a function named grep, an
#     alias, `command grep`, `env grep`, `egrep`), or one the walk does not
#     reach as the pipe's next command: `cmd | (grep -q y)`,
#     `cmd | { grep -q y; }`, and any pipe inside a DOUBLE-QUOTED `"$( … )"`,
#     which the skeleton collapses to one inert word.
#
# MAY WRONGLY FLAG — the direction that COSTS, because a false positive blocks
# a correct row. Each is accepted as the safe direction:
#   * a quiet grep whose status is thrown away anyway (`… | grep -q x || true`)
#     or that runs after `set +o pipefail` in the same file: harmless, flagged,
#     because the lint does not follow status use or option changes. Rewrite it
#     with a here-string, or opt the line out.
#   * heredoc bodies, and any other text written out as a fixture: they are
#     scanned like code, because telling where a heredoc ends needs a bash
#     parser — an earlier version guessed, and a guess that never closed hid
#     555 lines of three files while reporting "0 offenders". Scanning them
#     fails the safe way: a fixture line that pipes into `grep -q` is flagged,
#     and is MARKED, never skipped by inference. No line in the tree needs it
#     today.
#   * an ANSI-C `$'...'` string holding an escaped apostrophe (`\'`): the
#     scanner reads it as a plain single-quoted string, which the `\'` closes,
#     so the rest of the line is read as code. Two self-test fixture lines in
#     this file are marked for exactly that.
#   * a quoted string that spans physical lines WITHOUT backslash
#     continuations (a multi-line message, an inline awk or python program):
#     the quote state is reset at every line end, so its later lines are read
#     as code, and a `| grep -q` written inside it is flagged. The reset is the
#     safe direction: carried across lines, one stray apostrophe ("Don't" in
#     heredoc text) opened a quote that hid every line after it — two files in
#     the tree ended inside such a quote — while the scan still reported them
#     whole. Mark the line.
# The escape for a deliberate case is a trailing
# `# lint-pipe-grepq: allow (<why>)` on the line (any physical line of a
# continued one) — used once, by harness-selftest.sh, whose membership probe
# builds the hazard on purpose to prove the in-shell lookup that replaced it.
#
# ALSO NOT SEEN (deliberate, these are the legitimate shapes): comments, and a
# `|` or `grep -q` inside a quoted string that opens and closes on the same
# logical line (one physical line, or several joined by backslash
# continuations). Quote state never carries past the end of a logical line.
#
# Env (TEST ONLY — set by harness-selftest HS-67a/HS-67b to prove this lint is
# wired into the regression preflight; regression.sh never sets it):
#   JNEXT_LINT_PIPE_GREPQ_DIR   scan <dir>/*.sh, all treated as in scope
#
# Exit: 0 clean, 1 offenders found, 2 the lint's own self-test failed, the
# file list could not be read, or an in-scope file was not scanned whole.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# in_scope <path> — succeeds if the file runs under pipefail: FORCE=1 (the
# fixture mode), a path under test/00regression/ (the harness sources those
# into its own `set -euo pipefail` shell), or a `set` command naming pipefail
# anywhere in the file. Decided per WHOLE file, before the scan, so a file in
# scope is scanned from its first line to its last.
in_scope() {
    [[ "${FORCE:-0}" == 1 ]] && return 0
    [[ "$1" == */test/00regression/* ]] && return 0
    grep -qE '^[[:space:]]*set[[:space:]][^#]*pipefail' "$1"
}

# scan_awk <path>... — every path given is in scope. Prints one
# "OFFENDER<TAB><path>:<line>: <text>" per offending logical line and one
# "CHECKED<TAB><path><TAB><n>" per file: the physical lines that reached the
# matcher. Nothing is skipped — a heredoc body is scanned like code (see MAY
# WRONGLY FLAG) — so n must equal the file's line count, and verify_coverage
# refuses the run if it does not.
scan_awk() {
    [[ $# -gt 0 ]] || return 0
    awk '
        # analyse(l) — the three-state quote scanner from lint-traps.sh /
        # lint-timeouts.sh (see lint-traps.sh for its derivation). SKEL is the
        # line with every quoted string collapsed to the inert word X and any
        # comment removed, so a | or a grep inside quotes is not syntax.
        function analyse(l,   i, n, c, nxt) {
            SKEL = ""
            n = length(l)
            for (i = 1; i <= n; i++) {
                c = substr(l, i, 1)
                if (qst == 0) {
                    if (c == "\\") {
                        nxt = substr(l, i + 1, 1)
                        if (nxt != "")
                            SKEL = SKEL (nxt ~ /[A-Za-z0-9_]/ ? nxt : "X")
                        i++
                        continue
                    }
                    if (c == "\047") { qst = 1; qcontent = ""; continue }
                    if (c == "\"")   { qst = 2; qcontent = ""; continue }
                    if (c == "#" && (i == 1 || substr(l, i-1, 1) ~ /[ \t]/)) return
                    SKEL = SKEL c
                    continue
                }
                if (qst == 1) {
                    if (c == "\047") {
                        qst = 0
                        if (length(qcontent) > 0) SKEL = SKEL "X"
                        qcontent = ""
                        continue
                    }
                    qcontent = qcontent c
                    continue
                }
                if (c == "\\") {
                    nxt = substr(l, i + 1, 1)
                    if (nxt ~ /["\\$`]/) { qcontent = qcontent nxt; i++ }
                    else qcontent = qcontent c
                    continue
                }
                if (c == "\"") {
                    qst = 0
                    if (length(qcontent) > 0) SKEL = SKEL "X"
                    qcontent = ""
                    continue
                }
                qcontent = qcontent c
            }
            if (qst != 0 && length(qcontent) > 0) { SKEL = SKEL "X"; qcontent = "" }
        }

        # quiet_grep_after_pipe(s) — 1 if a single | in skeleton s is followed
        # by a grep command carrying a quiet option. GNU grep permutes its
        # arguments, so an option AFTER the pattern counts too; the walk runs
        # to the end of that command (the next separator) or to `--`, and
        # skips the argument of an option that takes one (`-e -q` searches
        # for the text "-q").
        function quiet_grep_after_pipe(s,   rest, p, toks, nt, j, t) {
            gsub(/\|\|/, " OR ", s)            # || is not a pipe
            gsub(/\|&/, "|", s)                # |& is: stdout+stderr into grep
            rest = s
            while ((p = index(rest, "|")) > 0) {
                rest = substr(rest, p + 1)
                t = rest
                gsub(/[;&|()]/, " SEP ", t)
                nt = split(t, toks, /[[:space:]]+/)
                j = 1
                while (j <= nt && toks[j] == "") j++
                if (j > nt || toks[j] != "grep") continue
                for (j = j + 1; j <= nt; j++) {
                    t = toks[j]
                    if (t == "") continue
                    if (t == "SEP" || t == "OR" || t == "--") break
                    if (t == "--quiet" || t == "--silent") return 1
                    if (t ~ /^--/) continue
                    if (t ~ /^-[efmABCdD]$/) { j++; continue }   # option + its argument
                    if (t ~ /^-[A-Za-z0-9]*q/) return 1          # -q, -qE, -Fq, -iq
                }
            }
            return 0
        }

        # check(): the matcher on the logical line now complete, and the count
        # of physical lines it covered.
        function check(   t) {
            # Quote state is RESET for every logical line and never carries to
            # the next: one stray apostrophe (as in heredoc prose) would
            # otherwise open a quote that hides every line after it while the
            # scan still counts them as checked. A real string spanning lines
            # is then read as code on its later lines (see MAY WRONGLY FLAG).
            qst = 0; qcontent = ""
            analyse(logical)
            if (qst != 0) open_at_end++
            if (!allow && quiet_grep_after_pipe(SKEL)) {
                t = logical; gsub(/^[ \t]+/, "", t)
                print "OFFENDER\t" path ":" start ": " substr(t, 1, 160)
            }
            checked += FNR_end - start + 1
            logical = ""
        }
        # finish(): end of a file. A logical line still open (the file ends
        # in a backslash continuation) is checked too, never dropped.
        function finish() {
            if (logical != "") check()
            print "CHECKED\t" path "\t" checked
            print "OPENQUOTE\t" path "\t" open_at_end
        }
        FNR == 1 {
            if (NR > 1) finish()
            path = FILENAME; checked = 0; open_at_end = 0
            qst = 0; qcontent = ""; logical = ""; start = 0; allow = 0
        }
        {
            raw = $0
            FNR_end = FNR
            if (logical == "") { start = FNR; allow = 0 }
            if (raw ~ /#[ \t]*lint-pipe-grepq:[ \t]*allow/) allow = 1
            if (raw ~ /\\$/) { logical = logical substr(raw, 1, length(raw) - 1) " "; next }
            logical = logical raw
            check()
        }
        END { if (NR > 0) finish() }
    ' "$@"
}

# file_lines <path> — the number of lines awk reads from it: newlines, plus
# one for a last line with no newline.
file_lines() {
    local n
    n=$(wc -l < "$1")
    [[ -s "$1" && -n "$(tail -c 1 "$1")" ]] && n=$((n + 1))
    echo "$n"
}

# verify_coverage <awk-output> <path>... — fails loud, naming the file, if any
# in-scope file was not scanned to its last line. A lint that stops early on
# some construct and then reports "0 offenders" is the failure this guards:
# an earlier version skipped heredoc bodies and lost 555 lines that way.
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
            echo "[lint-pipe-grepq] COVERAGE LOSS: $f — $got of $want lines checked" >&2
            bad=1
        fi
    done
    return "$bad"
}

# scan_files <path>... — the in-scope files among <path>..., scanned whole and
# verified whole. Prints "<path>:<line>: <text>" per offender; exits 2 (via
# the caller) on any coverage loss.
scan_files() {
    local f out
    local -a mine=()
    for f in "$@"; do in_scope "$f" && mine+=("$f"); done
    [[ ${#mine[@]} -gt 0 ]] || return 0
    out=$(scan_awk "${mine[@]}")
    verify_coverage "$out" "${mine[@]}" || return 2
    sed -n 's/^OFFENDER\t//p' <<<"$out"
}

# ------------------------------------------------------------- self-test
# Pins the matcher in BOTH directions. want=1 must be reported, want=0 must
# not. Run on every invocation: a lint that cannot prove it still detects is
# not a gate.
selftest() {
    local d pass=0 fail=0 want desc text got
    d=$(mktemp -d)
    local -a cases=(
        1 'echo into grep -q'               'echo "$out" | grep -q "x"'
        1 'echo into grep -qE'              'echo "$out" | grep -qE "a|b"'
        1 'printf into grep -qx'            'if printf "%s\n" "$l" | grep -qx "$2"; then :; fi'
        1 'cluster with q last'             'echo "$o" | grep -Fiq "x"'
        1 '--quiet spelling'                'echo "$o" | grep --quiet "x"'
        1 '--silent spelling'               'echo "$o" | grep --silent "x"'
        1 'command producer'                'if ! run_thing --flag | grep -q "$banner"; then'
        1 'negated, as an && guard'         'echo "$o" | grep -q "bad" && fails+=(x)'
        1 'continuation join'               $'echo "$o" \\\n    | grep -q "x"'
        1 'quiet option after the pattern'  'echo "$o" | grep -e x -q'
        1 'quiet after a bare pattern'      'echo "$o" | grep x -q'
        1 'three-stage, quiet last'         'sed -n p "$f" | tr a b | grep -q x'
        1 '|& into grep -q'                 'run_thing |& grep -q "x"'
        1 'file ends inside a continuation' $'echo "$o" | grep -q y \\'
        0 'here-string'                     'grep -q "x" <<<"$out"'
        0 'here-string of a capture'        'grep -q "x" <<<"$(run_thing)"'
        0 'grep -c reads to EOF'            'n=$(echo "$o" | grep -c "x" || true)'
        0 'plain grep in a pipe'            'echo "$o" | grep "x" | tail -1'
        0 'or-list is not a pipe'           'test -f a || grep -q x "$f"'
        0 'pipe inside quotes'              'echo "a | grep -q b"'
        0 'pipe in single quotes'           "echo 'x | grep -q y'"
        0 'pipe in a comment'               'true  # echo "$o" | grep -q x'
        0 'q is the pattern, not an option' 'echo "$o" | grep -e -q'
        0 'quiet grep in the NEXT command'  'echo "$o" | sort; grep -q x "$f"'
        0 'opted out'                       'echo "$o" | grep -q x  # lint-pipe-grepq: allow (demo)'
        0 'opted out, continued line'       $'echo "$o" \\\n    | grep -q x  # lint-pipe-grepq: allow (demo)'
        0 'grep -q on a file'               'grep -q "x" "$log" || fails+=(y)'
        # Heredoc bodies are SCANNED (the safe direction): fixture text is
        # marked, never skipped by guessing where a heredoc ends.
        1 'heredoc body is scanned'         $'cat > child.sh <<X\necho "$o" | grep -q y\nX'
        0 'heredoc body line, marked'       $'cat > child.sh <<X\necho "$o" | grep -q y  # lint-pipe-grepq: allow (fixture)\nX'
        1 'offender after <<EOF-X'          $'cat <<EOF-X\nbody\nEOF-X\necho "$o" | grep -q y'
        1 'offender after $(( a << b ))'    $'x=$(( a << b ))\necho "$o" | grep -q y'
        1 'offender after a <<< line'       $'grep -q x <<<"$o"\necho "$o" | grep -q y'
        1 'offender after a quoted <<X'     $'echo "see <<X"\necho "$o" | grep -q y'
        1 'offender after an unclosed quote' $'msg=Don\'t stop here\necho "$o" | grep -q y'  # lint-pipe-grepq: allow ($'' escape)
        1 'offender after an apostrophe in a heredoc' $'cat <<X\nDon\'t do it\nX\necho "$o" | grep -q y'  # lint-pipe-grepq: allow ($'' escape)
    )
    local i=0 rc
    while (( i < ${#cases[@]} )); do
        want=${cases[i]}; desc=${cases[i+1]}; text=${cases[i+2]}
        i=$((i + 3))
        printf '%s\n' "$text" > "$d/case.sh"
        got=0; rc=0
        out=$(FORCE=1 scan_files "$d/case.sh") || rc=$?
        [[ -n "$out" ]] && got=1
        if [[ "$rc" == 0 && "$got" == "$want" ]]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            echo "[lint-pipe-grepq] SELFTEST FAIL: $desc (want $want, got $got, rc $rc): $text" >&2
        fi
    done
    # Scope, both rules. The same offending line in a file with no pipefail
    # and outside test/00regression/ is NOT reported; with `set -o pipefail`
    # it is; and with no pipefail line but under test/00regression/ (where
    # the harness sources it into its own pipefail shell) it is too.
    printf '%s\n' '#!/usr/bin/env bash' 'echo "$o" | grep -q x' > "$d/nopf.sh"
    printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' 'echo "$o" | grep -q x' > "$d/pf.sh"
    if [[ -z "$(FORCE=0 scan_files "$d/nopf.sh")" ]]; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: a file without pipefail was flagged" >&2; fi
    if [[ -n "$(FORCE=0 scan_files "$d/pf.sh")" ]]; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: a file with set -euo pipefail was not flagged" >&2; fi
    printf '%s\n' '#!/usr/bin/env bash' 'echo "$o" | grep -q x' '' 'f() { :; }' 'set -o pipefail' > "$d/latepf.sh"
    if [[ -n "$(FORCE=0 scan_files "$d/latepf.sh")" ]]; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: pipefail set late in the file did not bring the earlier line into scope" >&2; fi
    mkdir -p "$d/test/00regression"
    printf '%s\n' '#!/usr/bin/env bash' 'echo "$o" | grep -q x' > "$d/test/00regression/row.inc"
    if [[ -n "$(FORCE=0 scan_files "$d/test/00regression/row.inc")" ]]; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: a test/00regression/ file with no pipefail line was not flagged" >&2; fi
    # The coverage check itself: a scan that reports fewer lines than the
    # file has must be refused, and a full one accepted.
    printf '%s\n' 'a' 'b' 'c' > "$d/three.sh"
    if ! verify_coverage $'CHECKED\t'"$d/three.sh"$'\t2' "$d/three.sh" 2>/dev/null; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: a 2-of-3-lines scan was accepted" >&2; fi
    if verify_coverage $'CHECKED\t'"$d/three.sh"$'\t3' "$d/three.sh"; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: a full scan was refused" >&2; fi
    rm -rf "$d"
    if (( fail > 0 )); then
        echo "[lint-pipe-grepq] self-test: $pass passed, $fail FAILED" >&2
        return 1
    fi
    SELFTEST_PASSED=$pass
}

selftest || exit 2

FILES=()
if [[ -n "${JNEXT_LINT_PIPE_GREPQ_DIR:-}" ]]; then
    [[ -d "$JNEXT_LINT_PIPE_GREPQ_DIR" ]] || {
        echo "ERROR: JNEXT_LINT_PIPE_GREPQ_DIR is not a directory: $JNEXT_LINT_PIPE_GREPQ_DIR" >&2
        exit 2
    }
    for f in "$JNEXT_LINT_PIPE_GREPQ_DIR"/*.sh; do
        [[ -e "$f" ]] && FILES+=("$f")
    done
    export FORCE=1
else
    mapfile -t FILES < <(git -C "$PROJECT_DIR" ls-files -- 'test/*.sh' 'test/*.inc' \
                         | sed "s#^#$PROJECT_DIR/#")
    [[ ${#FILES[@]} -gt 0 ]] || {
        echo "ERROR: no tracked test/*.sh found — is this a git checkout?" >&2
        exit 2
    }
    export FORCE=0
fi
rc=0
OFFENDERS=$(scan_files "${FILES[@]+"${FILES[@]}"}") || rc=$?
if [[ "$rc" != 0 ]]; then
    echo "[lint-pipe-grepq] REFUSED: an in-scope file was not scanned to its last line (see above)" >&2
    exit 2
fi
# scan_files ran in a subshell; count the in-scope files again here.
INSCOPE=0
for f in "${FILES[@]+"${FILES[@]}"}"; do in_scope "$f" && INSCOPE=$((INSCOPE + 1)); done

if [[ -z "$OFFENDERS" ]]; then
    echo "[lint-pipe-grepq] scanned: ${#FILES[@]} shell scripts, ${INSCOPE} under pipefail, each to its last line  offenders: 0  (self-test ${SELFTEST_PASSED} cases)"
    exit 0
fi

echo "[lint-pipe-grepq] scanned: ${#FILES[@]} shell scripts, ${INSCOPE} under pipefail, each to its last line  offenders: $(wc -l <<<"$OFFENDERS" | tr -d ' ')"
{
    echo "[lint-pipe-grepq] a pipeline ends in a quiet grep under pipefail:"
    sed "s#^$PROJECT_DIR/##;s/^/  /" <<<"$OFFENDERS"
    echo ""
    echo "grep -q exits on its first match; the writer then dies of SIGPIPE (141) and"
    echo "pipefail makes that the pipeline's status, so a line that IS there reads as"
    echo "missing. Ask grep alone:"
    echo ""
    echo "    grep -q PATTERN <<<\"\$var\"          # a string you already have"
    echo "    grep -q PATTERN <<<\"\$(producer)\"   # a command: capture first"
    echo ""
    echo "Fixture TEXT (a heredoc body, a string written to a file) is scanned too;"
    echo "mark such a line with a trailing  # lint-pipe-grepq: allow (<why>)"
} >&2
exit 1
