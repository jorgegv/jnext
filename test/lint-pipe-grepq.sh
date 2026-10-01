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
# pipefail, or it lives under test/00regression/ — the harness sources every
# row there into its own `set -euo pipefail` shell. A file that does NOT run
# under pipefail is out of scope, because the hazard does not exist there:
# test/packaging/packaging-test.sh leaves pipefail off for exactly this reason
# and says so in its header.
#
# What is flagged: a pipe `|` (not `||`, not `|&`) whose next command is grep
# with a quiet option — a short cluster containing q (`-q`, `-qE`, `-Fq`), or
# `--quiet` / `--silent` — on a logical line (backslash continuations joined).
#
# NOT flagged, deliberately (EXAMPLES, not an exhaustive list):
#   * Other early-exit consumers: `| head`, `| grep -m N`, `| sed q`. In this
#     tree they capture a VALUE (`x=$(cmd | head -1) || true`), where the
#     value head saw is right whatever the producer's status; the two that
#     were not masked were fixed by hand. A status-only consumer is what flips
#     a verdict, and grep -q is that consumer.
#   * A pipeline assembled at run time (eval, a command in a variable), or
#     split across a line in a way the continuation join does not see.
#   * A quiet grep reached by any other spelling (a function named grep, an
#     alias, `command grep`).
# A line can opt out with a trailing `# lint-pipe-grepq: allow (<why>)` — used
# once, by harness-selftest.sh, whose membership probe builds the hazard on
# purpose to prove the in-shell lookup that replaced it.
#
# Env (TEST ONLY — set by harness-selftest HS-66a/HS-66b to prove this lint is
# wired into the regression preflight; regression.sh never sets it):
#   JNEXT_LINT_PIPE_GREPQ_DIR   scan <dir>/*.sh, all treated as in scope
#
# Exit: 0 clean, 1 offenders found, 2 the lint's own self-test failed or the
# file list could not be read.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# scan_files <path>... — prints "<path>:<line>: <text>" per offender. With
# FORCE=1 in the environment every file is in scope (the fixture mode).
scan_files() {
    [[ $# -gt 0 ]] || return 0
    FORCE="${FORCE:-0}" awk '
        BEGIN { force = ENVIRON["FORCE"] + 0 }

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
            gsub(/\|&/, " PIPEAMP ", s)        # neither is |& for this lint
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
                    if (t == "SEP" || t == "OR" || t == "PIPEAMP" || t == "--") break
                    if (t == "--quiet" || t == "--silent") return 1
                    if (t ~ /^--/) continue
                    if (t ~ /^-[efmABCdD]$/) { j++; continue }   # option + its argument
                    if (t ~ /^-[A-Za-z0-9]*q/) return 1          # -q, -qE, -Fq, -iq
                }
            }
            return 0
        }

        FNR == 1 {
            inscope = force
            qst = 0; qcontent = ""; logical = ""; start = 0; allow = 0
            path = FILENAME
            if (!force && path ~ /\/test\/00regression\//) inscope = 1
            # Otherwise in scope from its `set ... pipefail` line on: the
            # files that set it do so in their first lines.
        }
        {
            raw = $0
            if (!inscope && raw ~ /^[ \t]*set[ \t][^#]*pipefail/) inscope = 1
            if (logical == "") { start = FNR; allow = 0 }
            if (raw ~ /#[ \t]*lint-pipe-grepq:[ \t]*allow/) allow = 1
            if (raw ~ /\\$/) { logical = logical substr(raw, 1, length(raw) - 1) " "; next }
            logical = logical raw
            if (inscope && !allow) {
                analyse(logical)
                if (quiet_grep_after_pipe(SKEL)) {
                    t = logical; gsub(/^[ \t]+/, "", t)
                    print FILENAME ":" start ": " substr(t, 1, 160)
                }
            }
            logical = ""
        }
    ' "$@"
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
        0 'grep -q on a file'               'grep -q "x" "$log" || fails+=(y)'
    )
    local i=0
    while (( i < ${#cases[@]} )); do
        want=${cases[i]}; desc=${cases[i+1]}; text=${cases[i+2]}
        i=$((i + 3))
        printf '%s\n' "$text" > "$d/case.sh"
        got=0
        [[ -n "$(FORCE=1 scan_files "$d/case.sh")" ]] && got=1
        if [[ "$got" == "$want" ]]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            echo "[lint-pipe-grepq] SELFTEST FAIL: $desc (want $want, got $got): $text" >&2
        fi
    done
    # Scope: the same offending line in a file with no pipefail and outside
    # test/00regression/ is NOT reported; with `set -o pipefail` it is.
    printf '%s\n' '#!/usr/bin/env bash' 'echo "$o" | grep -q x' > "$d/nopf.sh"
    printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' 'echo "$o" | grep -q x' > "$d/pf.sh"
    if [[ -z "$(FORCE=0 scan_files "$d/nopf.sh")" ]]; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: a file without pipefail was flagged" >&2; fi
    if [[ -n "$(FORCE=0 scan_files "$d/pf.sh")" ]]; then pass=$((pass + 1))
    else fail=$((fail + 1)); echo "[lint-pipe-grepq] SELFTEST FAIL: a file with set -euo pipefail was not flagged" >&2; fi
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
    OFFENDERS=$(FORCE=1 scan_files "${FILES[@]+"${FILES[@]}"}")
else
    mapfile -t FILES < <(git -C "$PROJECT_DIR" ls-files -- 'test/*.sh' 'test/*.inc' \
                         | sed "s#^#$PROJECT_DIR/#")
    [[ ${#FILES[@]} -gt 0 ]] || {
        echo "ERROR: no tracked test/*.sh found — is this a git checkout?" >&2
        exit 2
    }
    OFFENDERS=$(FORCE=0 scan_files "${FILES[@]}")
fi

if [[ -z "$OFFENDERS" ]]; then
    echo "[lint-pipe-grepq] scanned: ${#FILES[@]} shell scripts  offenders: 0  (self-test ${SELFTEST_PASSED} cases)"
    exit 0
fi

echo "[lint-pipe-grepq] scanned: ${#FILES[@]} shell scripts  offenders: $(printf '%s\n' "$OFFENDERS" | wc -l | tr -d ' ')"
{
    echo "[lint-pipe-grepq] a pipeline ends in a quiet grep under pipefail:"
    printf '%s\n' "$OFFENDERS" | sed "s#^$PROJECT_DIR/##;s/^/  /"
    echo ""
    echo "grep -q exits on its first match; the writer then dies of SIGPIPE (141) and"
    echo "pipefail makes that the pipeline's status, so a line that IS there reads as"
    echo "missing. Ask grep alone:"
    echo ""
    echo "    grep -q PATTERN <<<\"\$var\"          # a string you already have"
    echo "    grep -q PATTERN <<<\"\$(producer)\"   # a command: capture first"
} >&2
exit 1
