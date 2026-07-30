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
# It catches the naive and accidental case, plus the four evasions that were
# demonstrated against the first version of this lint. It is NOT a security
# boundary and does NOT prevent the whole class: a determined author can still
# reach `trap` through indirection this script cannot decide.
#
# CAUGHT (each is a pinned self-test case; see CASES below):
#   * `trap`, `builtin trap`, `command trap` in command position — line start,
#     after `; & | ( ) { }`, or after then/do/else, including the `( ... )` and
#     `$( ... )` subshell forms, which are actually harmless but are flagged in
#     the safe direction.
#   * any `eval` whose argument text contains the word `trap`, on that line.
#   * a heredoc consumed by `source`, `.` or `eval` — `source /dev/stdin <<P`
#     runs the body IN THIS SHELL, so the heredoc exemption below would
#     otherwise be a hole big enough to drive the whole bug through. Flagged
#     unconditionally: a row script has no reason to source a heredoc at all.
#
# NOT CAUGHT (known, accepted, undecidable without executing the script):
#   * the command name held in a variable — `t=trap; $t 'c' EXIT`.
#   * an `eval` whose argument is built across several lines, or assembled from
#     variables so the word `trap` never appears literally.
#   * a script written to a temp file by some other means and then `source`d.
#   * `trap` inside a function defined here but invoked by the harness later.
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
# Env (TEST ONLY — set by harness-selftest HS-47 to prove this lint is still
# wired into scripts/00-preflight-lint.sh; never set in a real run):
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
# (whole-line and trailing, only where the text before `#` has balanced
# quotes); then apply the three rules documented above.
scan_dir() {
    local dir="$1" f
    for f in "$dir"/*.sh; do
        [[ -e "$f" ]] || continue
        awk -v FNAME="$f" '
            # count occurrences of a quote character without touching the input
            function nq(s, ch,   t) { t = s; return gsub(ch, "", t) }

            # drop a trailing (or whole-line) comment: the first "#" that
            # starts a word and has balanced quotes before it. Leaves "$#",
            # "${#v}" and a "#" inside a quoted string alone.
            function decomment(l,   i, n, before) {
                n = length(l)
                for (i = 1; i <= n; i++) {
                    if (substr(l, i, 1) != "#") continue
                    if (i > 1 && substr(l, i-1, 1) !~ /[ \t]/) continue
                    before = substr(l, 1, i-1)
                    if (nq(before, "\047") % 2 == 0 && nq(before, "\"") % 2 == 0)
                        return before
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
# the lint's specification: every construct the GH #153 review demonstrated
# (including the four that defeated the first version) is pinned here, so the
# next author extends the table rather than re-deriving it by hand.
#
# CASES — 20 must flag, 9 must not.
#
#   P01 bare `trap`                      P11 `( ... )`      (safe; flagged anyway)
#   P02 indented                         P12 `$( ... )`     (safe; flagged anyway)
#   P03 INT, not EXIT                    P13 `builtin trap`
#   P04 `trap - EXIT` (removal)          P14 `command trap`
#   P05 after `&&`                       P15 `eval "trap ..."`
#   P06 after `;`                        P16 `eval 'trap - EXIT'`
#   P07 after `||`                       P17 `source /dev/stdin <<P`
#   P08 after `then`                     P18 `. /dev/stdin <<P`
#   P09 after `do`                       P19 heredoc into `eval "$(cat <<P)"`
#   P10 `{ ...; }` brace group           P20 line-continuation `trap \`
#
#   N01 whole-line comment               N06 `trapped=` / `entrapment=`
#   N02 trailing comment (eval + trap)   N07 `mytrap 'c' EXIT`
#   N03 word inside a string             N08 `echo "trap"`
#   N04 `cat > file <<X` body            N09 `grep -n trap "$f"`
#   N05 `bash /dev/stdin <<X` body
self_test() {
    local dir bad good out n failed=0
    dir=$(mktemp -d)
    bad="$dir/bad"; good="$dir/good"
    mkdir -p "$bad" "$good"

    # 20 offending lines. Kept one per line so the count IS the assertion.
    cat >"$bad/offenders.sh" <<'FIXTURE'
#!/usr/bin/env bash
trap 'rm -rf "$W"' EXIT
    trap 'rm -rf "$W"' EXIT
trap 'rm -rf "$W"' INT
trap - EXIT
W=$(mktemp -d) && trap 'rm -rf "$W"' EXIT
mkdir "$W"; trap 'rm -rf "$W"' EXIT
mkdir "$W" || trap 'rm -rf "$W"' EXIT
if true; then trap 'rm -rf "$W"' EXIT; fi
for f in a b; do trap 'rm -rf "$W"' EXIT; done
{ trap 'rm -rf "$W"' EXIT; }
( trap 'rm -rf "$W"' EXIT )
x=$( trap 'rm -rf "$W"' EXIT )
builtin trap 'rm -rf "$W"' EXIT
command trap 'rm -rf "$W"' EXIT
eval "trap 'rm -rf \"$W\"' EXIT"
eval 'trap - EXIT'
source /dev/stdin <<'P1'
P1
. /dev/stdin <<P2
P2
eval "$(cat <<'P3'
P3
)"
trap \
    'rm -rf "$W"' EXIT
FIXTURE

    # 9 legitimate shapes. Any hit here is a false positive that would block a
    # row an author is entitled to write.
    cat >"$good/innocent.sh" <<'FIXTURE'
#!/usr/bin/env bash
# Installing a trap here would clobber the harness one; we do not.
seen=1   # do not eval a trap here, see lint-traps.sh
fail_row " (SAVE trap fired during NextZXOS boot?)"
cat > "$W/child.sh" <<'CHILD'
trap 'rm -rf "$D"' EXIT
CHILD
bash /dev/stdin <<'CHILD2'
trap 'rm -rf "$D"' EXIT
CHILD2
trapped=0
entrapment=1
mytrap 'rm -rf "$W"' EXIT
echo "trap"
grep -n trap "$f"
FIXTURE

    out=$(scan_dir "$bad")
    n=$(printf '%s' "$out" | grep -c . || true)
    if [[ "$n" -ne 20 ]]; then
        echo "ERROR: self-test: expected 20 offenders in the bad fixture, got $n:" >&2
        printf '%s\n' "$out" >&2
        failed=1
    fi
    out=$(scan_dir "$good")
    if [[ -n "$out" ]]; then
        echo "ERROR: self-test: legitimate code was flagged (false positive):" >&2
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
