#!/usr/bin/env bash
# Trap lint for the regression harness row scripts (GH #153).
#
# WHY THIS EXISTS
#
# regression.sh SOURCES every row script into its own shell. Before the first
# one is sourced, test-functions.inc:277-279 has already installed the harness's
# ONE documented set of handlers:
#
#     trap 'regression_cleanup' EXIT
#     trap 'regression_cleanup; exit 130' INT
#     trap 'regression_cleanup; exit 143' TERM
#
# whose job is deleting $RUN_DIR — the per-run SD-card clone, 1-2 GB. Bash
# keeps exactly one handler per signal, so a second `trap ... EXIT` anywhere in
# a sourced row silently REPLACES it. INT and TERM are untouched, so an
# interrupted run still cleans up: it is specifically the SUCCESSFUL run that
# then leaks its whole run directory, invisibly, while the count stays green.
# That is not hypothetical — a host reached 18 GB / 93% full with 112/112
# passing before a reviewer noticed ~/.jnext/runs/ growing.
#
# The GH #65 isolation rows cannot catch it: they spawn an isolated CHILD
# shell, and the failure is a later-sourced SIBLING clobbering the PARENT's
# trap. They stayed green throughout.
#
# WHAT IT BANS, AND WHY THAT IS BROADER THAN THE BUG
#
# Any `trap` command in test/00regression/scripts/*.sh — every signal, not just
# EXIT (a row-installed INT handler would clobber the harness's Ctrl-C path
# too), and at any nesting depth.
#
# Distinguishing a clobbering top-level trap from a harmless one inside a
# `( ... )` subshell needs a real bash parser, which is far more machinery than
# this class of defect warrants. It is also machinery with nothing to do: as of
# GH #153 NONE of the 50+ row scripts contains a `trap` command at all, at any
# nesting depth. So the rule is the simple, total one, and the escape hatch is
# structural rather than syntactic — see below.
#
# WHAT A ROW SHOULD DO INSTEAD
#
#   * Scratch files: create them under $TMP_DIR (or a mktemp -d beneath it).
#     The harness's own trap already removes that tree on every exit path,
#     including signals. cmake-guards-func.sh documents exactly this.
#   * Cleanup that must survive a signal in a shell of its OWN: write the
#     child as a file and run it with `bash`, the shape sdcard-isolation-func.sh
#     already uses (:116, :127). A trap inside an explicitly-executed child
#     cannot reach the harness's shell.
#
# ------------------------------------------------------------------------
# WHAT IT CATCHES, AND — READ THIS — WHAT IT DOES NOT
#
# It catches the naive and accidental case, plus the five evasions demonstrated
# against earlier versions of this lint in review. It is NOT a security boundary
# and does NOT prevent the whole class: a determined author can still reach
# `trap` through indirection this script cannot decide.
#
# Every claim below is a pinned self-test case, both directions — the CAUGHT
# list is asserted to flag and the NOT CAUGHT list has been verified to be
# accurate rather than merely modest (a `trap` inside a function body IS caught,
# for instance, so it is deliberately absent from the second list).
#
# CAUGHT (see CASES below for the case IDs):
#   * `trap`, `builtin trap`, `command trap` in command position — line start,
#     after `; & | ( ) { }`, or after then/do/else, including the `( ... )` and
#     `$( ... )` subshell forms, which are actually harmless but are flagged in
#     the safe direction.
#   * any `eval` whose argument text contains the word `trap`, on that line.
#   * a heredoc consumed by `source`, `.` or `eval` — `source /dev/stdin <<P`
#     runs the body IN THIS SHELL, so the heredoc exemption below would
#     otherwise be a hole big enough to drive the whole bug through. Flagged
#     unconditionally: a row script has no reason to source a heredoc at all.
#   * a live `trap` hidden behind bash quoting the comment stripper has to get
#     exactly right — `echo "\" #" ; trap ... EXIT`, the `'it'\''s'` idiom, a
#     `\\` before a closing quote, an escaped `\#`, or a string opened on the
#     previous line. See decomment() and cases P21-P25.
#
# NOT CAUGHT (known, accepted, each verified to be genuinely uncaught; all are
# undecidable without executing the script):
#   * the command name held in a variable — `t=trap; $t 'c' EXIT`.
#   * an `eval` whose argument is built across several lines, or assembled from
#     variables so the word `trap` never appears literally.
#   * a script written to a file by other means and then `source`d by path.
#     (`source` of a path cannot simply be banned: every row script legitimately
#     sources test-functions.inc. Only the heredoc form is decidable here.)
# Closing those needs a bash parser or an interpreter, and this guard exists to
# stop a tired author at 2 a.m., not an adversary. If you are reaching for one
# of them, you already know you are defeating a guard — don't.
#
# ALSO NOT SEEN (deliberate, these are the legitimate shapes):
#   * comments, whole-line and trailing.
#   * heredoc bodies whose consumer is NOT source/./eval — that is the
#     sanctioned child-script pattern (`cat > child.sh <<'X'`, `bash file <<X`,
#     `python3 - <<'PY'`).
#   * `trap` as a word inside a string (`fail_row "... SAVE trap fired ..."`)
#     or as an argument (`grep -n trap "$f"`), because a match needs command
#     position.
# ------------------------------------------------------------------------
#
# Usage:
#   bash test/00regression/lint-traps.sh          # scan the real row scripts
#
# Env (TEST ONLY — set by harness-selftest HS-49a/HS-49b to prove this lint is
# still wired into scripts/00-preflight-lint.sh; never set in a real run):
#   JNEXT_LINT_TRAPS_DIR   scan this directory instead of ./scripts
#
# Exit 0 clean, 1 offenders found, 2 the lint itself is broken.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPTS_DIR="${JNEXT_LINT_TRAPS_DIR:-$SCRIPT_DIR/scripts}"

# scan_dir <dir> — print "file:line: [reason] text" for every offender found in
# <dir>/*.sh. Prints nothing (and succeeds) when the directory is clean.
#
# The awk, in order: skip heredoc bodies with a safe consumer; strip comments
# (whole-line and trailing) with a bash-exact quote scanner; then apply the
# three rules documented above.
scan_dir() {
    local dir="$1" f
    for f in "$dir"/*.sh; do
        [[ -e "$f" ]] || continue
        awk -v FNAME="$f" '
            # decomment(l) — drop a trailing (or whole-line) comment, using
            # bash quoting rules exactly rather than a parity heuristic.
            #
            # A quote COUNTER cannot express those rules, and the difference is
            # a live bypass, not a nicety: in `echo "\" #" ; trap ... EXIT` the
            # \" is a literal quote INSIDE the string, so bash keeps the string
            # open and the `;` is a real command separator — but a counter sees
            # an even number of `"` one character early, treats ` #` as a
            # comment, truncates, and never sees the trap. Found in review.
            #
            # Left-to-right, three states (this comment cannot contain an
            # apostrophe: the whole awk program is a single-quoted shell word):
            #   OUTSIDE  backslash escapes anything; either quote opens a
            #            string; a # that starts a word begins the comment.
            #   SINGLE   nothing escapes; only the apostrophe closes. The
            #            standard escaped-apostrophe idiom is really close +
            #            escaped-quote-OUTSIDE + reopen, which a state machine
            #            gets right for free and a backslash-stripping pre-pass
            #            does not. Case P22.
            #   DOUBLE   backslash escapes only " \ $ and the backtick; the
            #            double quote closes. Cases P21, P23.
            #
            # qst carries across lines, so a string opened on one line and
            # closed on the next cannot hide a # (case P25). A desync — an
            # apostrophe in code that is not a string — errs toward NOT
            # stripping, i.e. toward flagging: the safe direction.
            function decomment(l,   i, n, c) {
                n = length(l)
                for (i = 1; i <= n; i++) {
                    c = substr(l, i, 1)
                    if (qst == 0) {
                        if (c == "\\") { i++; continue }
                        if (c == "\047") { qst = 1; continue }
                        if (c == "\"")   { qst = 2; continue }
                        if (c == "#" && (i == 1 || substr(l, i-1, 1) ~ /[ \t]/))
                            return substr(l, 1, i-1)
                    } else if (qst == 1) {
                        if (c == "\047") qst = 0
                    } else {
                        if (c == "\\") {
                            if (substr(l, i+1, 1) ~ /["\\$`]/) i++
                            continue
                        }
                        if (c == "\"") qst = 0
                    }
                }
                return l
            }

            # is a word present as a standalone token?
            function has_word(l, w) {
                return l ~ ("(^|[^A-Za-z0-9_])" w "([^A-Za-z0-9_]|$)")
            }

            function reason(l,   v) {
                # `trap`, optionally behind the `builtin`/`command` builtins,
                # in command position.
                v = "((builtin|command)[[:space:]]+)*trap([[:space:]]|$)"
                if (l ~ ("^[[:space:]]*" v) \
                    || l ~ ("[;&|(){}][[:space:]]*" v) \
                    || l ~ ("[[:space:]](then|do|else)[[:space:]]+" v))
                    return "trap command"
                if (has_word(l, "eval") && has_word(l, "trap"))
                    return "eval with trap in its argument"
                return ""
            }

            in_hd {
                if ($0 ~ hd_term) in_hd = 0
                next
            }
            {
                line = decomment($0)
                if (line ~ /^[[:space:]]*$/) next

                # Arm the heredoc skip BEFORE matching, so a `trap` inside a
                # child-script body is not read as ours. "<<<" is a herestring
                # and must not arm it.
                hd_bad = 0
                if (line !~ /<<</ && match(line, /<<-?[[:space:]]*['"'"'"]?[A-Za-z_][A-Za-z0-9_]*/)) {
                    head = substr(line, 1, RSTART - 1)
                    w = substr(line, RSTART, RLENGTH)
                    sub(/^<<-?[[:space:]]*/, "", w)
                    gsub(/['"'"'"]/, "", w)
                    hd_term = "^[[:space:]]*" w "[[:space:]]*$"
                    in_hd = 1
                    # A heredoc fed to source/./eval executes IN THIS SHELL.
                    if (has_word(head, "source") || has_word(head, "eval") \
                        || head ~ /(^|[;&|(){}])[[:space:]]*\.[[:space:]]/)
                        hd_bad = 1
                }

                why = hd_bad ? "heredoc sourced into this shell" : reason(line)
                if (why != "") {
                    sub(/^[[:space:]]+/, "", line)
                    printf "%s:%d: [%s] %s\n", FNAME, FNR, why, line
                }
            }
        ' "$f"
    done
}

# ---------------------------------------------------------------- self-test
#
# Runs on EVERY invocation, both directions. A guard that has silently become a
# no-op reports success, which is worse than no guard; a guard that flags
# everything gets disabled, which ends the same way. The case table below is
# the lint's specification: every construct the two GH #153 reviews
# demonstrated — including the five that defeated an earlier version — is
# pinned here, so the next author extends the table rather than re-deriving it.
#
# ONE FILE PER CASE, deliberately. The first version of this self-test put all
# the offenders in a single file and asserted the total row count, which is
# exactly the silent-truncation shape this project bans elsewhere: a mutation
# that LOSES one case and GAINS another keeps the total and reports green. That
# is not hypothetical — reverting the DOUBLE-state backslash rule did precisely
# that and the combined-fixture self-test passed. Per case, a lost case is named.
#
# CASES — 28 must flag, 13 must not.
#
#   P01 bare `trap`                      P15 `eval "trap ..."`
#   P02 indented                         P16 `eval 'trap - EXIT'`
#   P03 INT, not EXIT                    P17 `source /dev/stdin <<P`
#   P04 `trap - EXIT` (removal)          P18 `. /dev/stdin <<P`
#   P05 after `&&`                       P19 heredoc into `eval "$(cat <<P)"`
#   P06 after `;`                        P20 line-continuation `trap \`
#   P07 after `||`                       P21 `\"` inside "..." then a live trap
#   P08 after `then`                     P22 the escaped-apostrophe idiom
#   P09 after `do`                       P23 `\\` before a closing quote
#   P10 `{ ...; }` brace group           P24 escaped `\#` is not a comment
#   P11 `( ... )`  (safe; flagged)       P25 string opened on the PREVIOUS line
#   P12 `$( ... )` (safe; flagged)       P26 `command source /dev/stdin <<X`
#   P13 `builtin trap`                   P27 `builtin eval "trap ..."`
#   P14 `command trap`                   P28 indented `source ... <<-'X'`
#
#   N01 whole-line comment               N08 `echo "trap"`
#   N02 trailing comment (eval + trap)   N09 `grep -n trap "$f"`
#   N03 word inside a string             N10 escaped-apostrophe + REAL comment
#   N04 `cat > file <<X` body            N11 `#` inside a quoted string
#   N05 `bash /dev/stdin <<X` body       N12 `$#` and `${#v}` then a comment
#   N06 `trapped=` / `entrapment=`       N13 `bash -c "$(cat <<X)"` body
#   N07 `mytrap 'c' EXIT`                    (a real child process)
#
# P21-P25 are the quoting cases. They exist because an earlier decomment() here
# counted quote characters, and a counter cannot express bash escaping.
self_test() {
    local dir bad good out id failed=0
    local -a missing=() spurious=()
    dir=$(mktemp -d)
    bad="$dir/bad"; good="$dir/good"
    mkdir -p "$bad" "$good"

    # --- 28 cases that MUST be flagged, one file each -----------------------
    printf '%s\n' "trap 'rm -rf \"\$W\"' EXIT"                        >"$bad/P01.sh"
    printf '%s\n' "    trap 'rm -rf \"\$W\"' EXIT"                    >"$bad/P02.sh"
    printf '%s\n' "trap 'rm -rf \"\$W\"' INT"                         >"$bad/P03.sh"
    printf '%s\n' "trap - EXIT"                                        >"$bad/P04.sh"
    printf '%s\n' "W=\$(mktemp -d) && trap 'rm -rf \"\$W\"' EXIT"      >"$bad/P05.sh"
    printf '%s\n' "mkdir \"\$W\"; trap 'rm -rf \"\$W\"' EXIT"         >"$bad/P06.sh"
    printf '%s\n' "mkdir \"\$W\" || trap 'rm -rf \"\$W\"' EXIT"       >"$bad/P07.sh"
    printf '%s\n' "if true; then trap 'rm -rf \"\$W\"' EXIT; fi"      >"$bad/P08.sh"
    printf '%s\n' "for f in a b; do trap 'rm -rf \"\$W\"' EXIT; done"  >"$bad/P09.sh"
    printf '%s\n' "{ trap 'rm -rf \"\$W\"' EXIT; }"                   >"$bad/P10.sh"
    printf '%s\n' "( trap 'rm -rf \"\$W\"' EXIT )"                    >"$bad/P11.sh"
    printf '%s\n' "x=\$( trap 'rm -rf \"\$W\"' EXIT )"                 >"$bad/P12.sh"
    printf '%s\n' "builtin trap 'rm -rf \"\$W\"' EXIT"                >"$bad/P13.sh"
    printf '%s\n' "command trap 'rm -rf \"\$W\"' EXIT"                >"$bad/P14.sh"
    printf '%s\n' "eval \"trap 'rm -rf' EXIT\""                        >"$bad/P15.sh"
    printf '%s\n' "eval 'trap - EXIT'"                                 >"$bad/P16.sh"
    printf '%s\n' "source /dev/stdin <<'PAY'" "trap 'x' EXIT" "PAY"    >"$bad/P17.sh"
    printf '%s\n' ". /dev/stdin <<PAY" "trap 'x' EXIT" "PAY"           >"$bad/P18.sh"
    printf '%s\n' "eval \"\$(cat <<'PAY'" "trap 'x' EXIT" "PAY" ")\""   >"$bad/P19.sh"
    printf '%s\n' "trap \\" "    'rm -rf' EXIT"                        >"$bad/P20.sh"
    printf '%s\n' "echo \"\\\" #\" ; trap 'rm -rf \"\$W\"' EXIT"       >"$bad/P21.sh"
    printf '%s\n' "x='it'\\''s'; trap 'rm -rf \"\$W\"' EXIT"          >"$bad/P22.sh"
    printf '%s\n' "echo \"a\\\\\" ; trap 'rm -rf \"\$W\"' EXIT"       >"$bad/P23.sh"
    printf '%s\n' "echo a\\#b; trap 'rm -rf \"\$W\"' EXIT"            >"$bad/P24.sh"
    printf '%s\n' "msg=\"opened here" " # \" ; trap 'rm -rf' EXIT"     >"$bad/P25.sh"
    printf '%s\n' "command source /dev/stdin <<'PAY'" "trap 'x' EXIT" "PAY" >"$bad/P26.sh"
    printf '%s\n' "builtin eval \"trap 'rm -rf' EXIT\""                >"$bad/P27.sh"
    printf '%s\n' "    source /dev/stdin <<-'PAY'" "	PAY"             >"$bad/P28.sh"

    # --- 13 legitimate shapes that MUST NOT be flagged ----------------------
    # A hit here is a false positive that would block a row an author may write.
    printf '%s\n' "# Installing a trap here would clobber the harness one."  >"$good/N01.sh"
    printf '%s\n' "seen=1   # do not eval a trap here, see lint-traps.sh"   >"$good/N02.sh"
    printf '%s\n' "fail_row \" (SAVE trap fired during NextZXOS boot?)\""   >"$good/N03.sh"
    printf '%s\n' "cat > \"\$W/child.sh\" <<'CHILD'" "trap 'x' EXIT" "CHILD" >"$good/N04.sh"
    printf '%s\n' "bash /dev/stdin <<'CHILD'" "trap 'x' EXIT" "CHILD"      >"$good/N05.sh"
    printf '%s\n' "trapped=0" "entrapment=1"                              >"$good/N06.sh"
    printf '%s\n' "mytrap 'rm -rf' EXIT"                                  >"$good/N07.sh"
    printf '%s\n' "echo \"trap\""                                         >"$good/N08.sh"
    printf '%s\n' "grep -n trap \"\$f\""                                   >"$good/N09.sh"
    printf '%s\n' "x='it'\\''s'   # do not eval a trap here either"        >"$good/N10.sh"
    printf '%s\n' "echo \"value # trap\""                                 >"$good/N11.sh"
    printf '%s\n' "echo \"\${#v} \$#\"   # a note mentioning eval and trap" >"$good/N12.sh"
    printf '%s\n' "bash -c \"\$(cat <<'CHILD'" "trap 'x' EXIT" "CHILD" ")\"" >"$good/N13.sh"

    # Every must-flag case must produce at least one row against ITS OWN file.
    out=$(scan_dir "$bad")
    for id in P01 P02 P03 P04 P05 P06 P07 P08 P09 P10 P11 P12 P13 P14 \
              P15 P16 P17 P18 P19 P20 P21 P22 P23 P24 P25 P26 P27 P28; do
        grep -q "/$id\.sh:" <<<"$out" || missing+=("$id")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "ERROR: self-test: these must-flag cases were NOT caught: ${missing[*]}" >&2
        failed=1
    fi

    # ... and every must-not case must produce none.
    out=$(scan_dir "$good")
    if [[ -n "$out" ]]; then
        for id in $(grep -o '/N[0-9]*\.sh:' <<<"$out" | tr -d '/:' | sed 's/\.sh//' | sort -u); do
            spurious+=("$id")
        done
        echo "ERROR: self-test: legitimate code was flagged (false positive) in: ${spurious[*]}" >&2
        printf '%s\n' "$out" >&2
        failed=1
    fi

    rm -rf "$dir"
    if [[ "$failed" -ne 0 ]]; then
        echo "[lint-traps] SELF-TEST FAILED: the lint no longer matches what it" >&2
        echo "claims to match, so a clean report would be meaningless. Refusing." >&2
        exit 2
    fi
}

self_test

if [[ ! -d "$SCRIPTS_DIR" ]]; then
    echo "ERROR: row-script directory not found at $SCRIPTS_DIR" >&2
    exit 2
fi

OFFENDERS=$(scan_dir "$SCRIPTS_DIR")
N_FILES=$(find "$SCRIPTS_DIR" -maxdepth 1 -name '*.sh' | wc -l | tr -d ' ')

if [[ -z "$OFFENDERS" ]]; then
    echo "[lint-traps] scanned: $N_FILES row scripts  offenders: 0"
    exit 0
fi

echo "[lint-traps] scanned: $N_FILES row scripts  offenders: $(printf '%s\n' "$OFFENDERS" | wc -l | tr -d ' ')"
{
    echo "[lint-traps] a row script reaches the 'trap' builtin:"
    printf '%s\n' "$OFFENDERS" | sed 's/^/  /'
    echo ""
    echo "regression.sh SOURCES these scripts into the harness shell, which already"
    echo "holds 'trap regression_cleanup EXIT/INT/TERM' (test-functions.inc:277-279)."
    echo "Bash keeps ONE handler per signal, so yours REPLACES it and every successful"
    echo "run then leaks its 1-2 GB \$RUN_DIR while the count stays green (GH #153)."
    echo ""
    echo "Do this instead:"
    echo "  * scratch files -> create them under \$TMP_DIR; the harness trap already"
    echo "    removes that tree on every exit path, signals included."
    echo "  * a shell of your own that must clean up on a signal -> write it to a file"
    echo "    and run it with 'bash' (see sdcard-isolation-func.sh:116). A trap inside"
    echo "    an executed child cannot reach the harness shell, and such heredoc bodies"
    echo "    are invisible to this lint — but a heredoc fed to source/./eval runs HERE"
    echo "    and is reported for exactly that reason."
    echo ""
    echo "This lint catches the naive and accidental case, not every possible evasion"
    echo "(see the WHAT IT DOES NOT CATCH block in test/00regression/lint-traps.sh)."
    echo "Re-check with: bash test/00regression/lint-traps.sh"
} >&2
exit 1
