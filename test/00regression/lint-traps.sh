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
# WHAT THIS LINT IS FOR — READ THIS BEFORE EXTENDING IT
#
# It exists to catch an ACCIDENTAL or CARELESS `trap` in a row script. That is
# the failure that actually happened: a row written in one night installed an
# EXIT trap without its author realising the harness already had one, and it was
# found only when a host filled to 18 GB with a green 112/112 on screen.
#
# It does NOT attempt to stop DELIBERATE OBFUSCATION, and it cannot. A row
# author who wants to evade it can, trivially — `t=trap; $t 'c' EXIT` is eight
# characters and no static grep will ever see it. Three rounds of review found
# six evasions (all now closed, cases P13-P34); the last of them, `tr''ap`, was
# not something anyone writes by accident. That is the line: accidents are in
# scope, obfuscation is not, and pretending otherwise would make this header the
# kind of overclaim the lint's own history is a record of.
#
# So: a newly-discovered way to write `trap` on purpose is NOT a defect in this
# lint. A newly-discovered way to write one BY ACCIDENT is.
#
# Every entry in CAUGHT is a pinned self-test case asserted in both directions.
# NOT CAUGHT is a list of EXAMPLES, deliberately not claimed to be exhaustive —
# it has twice been proved incomplete by review, and an enumeration that claims
# a completeness it does not have is exactly the defect we keep finding.
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
#   * `trap` spelled so the literal word never appears — bash concatenates
#     adjacent word fragments, so `tr''ap`, `tr""ap`, `tr\ap` and `t\r\a\p` all
#     run the builtin. See dequote() and cases P29-P34.
#
# NOT CAUGHT — EXAMPLES, NOT AN EXHAUSTIVE LIST. Each of these was verified to
# be genuinely uncaught, and all are undecidable without executing the script:
#   * the command name held in a variable — `t=trap; $t 'c' EXIT`.
#   * an `eval` whose argument is built across several lines, or assembled from
#     variables so the word `trap` never appears literally.
#   * a script written to a file by other means and then `source`d by path.
#     (`source` of a path cannot simply be banned: every row script legitimately
#     sources test-functions.inc. Only the heredoc form is decidable here.)
# Closing those needs a bash parser or an interpreter. Per the scope statement
# above they are out of scope, not a backlog: if you are reaching for one, you
# already know you are defeating a guard — so don't, and no amount of lint will
# stop you if you do.
#
# MAY WRONGLY FLAG — the other direction, and it is the one that COSTS, because
# a false positive blocks a correct row where a false negative merely fails to
# help. Measured, not guessed; each is accepted as the safe direction:
#   * `( trap ... )` and `$( trap ... )` — a subshell trap cannot reach the
#     harness shell, so these are harmless, but they are flagged (P11, P12).
#     Deliberate: telling them apart needs a parser, and no row wants one.
#   * a line that genuinely runs `eval` AND mentions `trap` in an unrelated
#     string: `eval "$cmd"; echo "see the trap docs"`. Narrow — it needs a live
#     `eval` as a real command token — and flagging beside a live eval is
#     defensible. No row in the tree does it.
#   * a heredoc whose head text inertly mentions source/eval before the `<<`:
#     `python3 - "notes about eval" <<'PY'`. Same shape, same reasoning.
# What is NOT in this list, because the skeleton fixed it: any live string whose
# CONTENTS look like syntax. `fail_row "... eval ... trap ..."`,
# `msg="warning: dont;trap yourself"`, backtick doc mentions, and an inert
# string spanning a backslash-newline are all clean and pinned as N17-N21.
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
            # analyse(l) — one bash-faithful pass producing THREE views.
            #
            # Sets:
            #   CODE   the line with any trailing/whole-line comment removed.
            #   NLINE  the SYNTAX SKELETON: every quoted string, whatever it
            #          contains, collapses to the single inert word character X;
            #          an empty quote pair collapses to nothing; a backslash
            #          escape becomes its character (word chars) or X (anything
            #          else). Only characters that were REALLY syntax survive.
            #   DLINE  the concatenated string CONTENTS, unwrapped. Data, never
            #          matched as syntax.
            #
            # WHY THE SKELETON. The previous version dequoted the whole line and
            # matched on that, which also stripped the delimiters AROUND an
            # argument — and those are not zero-content: they were the only
            # thing keeping the contents from being read as syntax. Verified
            # false positives that cost, on a lint whose stated target user is a
            # tired author:
            #     fail_row "please eval trap manually if this ever happens"
            #     msg="warning: dont;trap yourself here"     (inert ; became a
            #                                                separator)
            # A test suite ABOUT catching eval/trap will contain messages naming
            # both words; rejecting such a row is a real cost, not a safe
            # direction. Cases N17-N21.
            #
            # The word-spelling trick still resolves, because in every one of
            # its forms the trick is INSIDE the token, not around it:
            #   tr[EMPTY-PAIR]ap -> trap    tr\ap -> trap    t\r\a\p -> trap
            #
            # Three states (no apostrophe may appear in this comment: the whole
            # awk program is a single-quoted shell word):
            #   OUTSIDE  backslash escapes anything; either quote opens a
            #            string; a # that starts a word begins the comment.
            #   SINGLE   nothing escapes; only the apostrophe closes. The
            #            standard escaped-apostrophe idiom is really close +
            #            escaped-quote-OUTSIDE + reopen, which a state machine
            #            gets right for free. Case P22.
            #   DOUBLE   backslash escapes only " \ $ and the backtick; the
            #            double quote closes. Cases P21, P23.
            #
            # qst and qcontent carry across lines, so a string opened on one
            # line and closed on the next is still one inert token (P25, N20).
            function analyse(l,   i, n, c, nxt) {
                NLINE = ""; DLINE = ""; CODE = l
                n = length(l)
                for (i = 1; i <= n; i++) {
                    c = substr(l, i, 1)
                    if (qst == 0) {
                        if (c == "\\") {
                            nxt = substr(l, i + 1, 1)
                            if (nxt != "") {
                                DLINE = DLINE nxt
                                NLINE = NLINE (nxt ~ /[A-Za-z0-9_]/ ? nxt : "X")
                            }
                            i++
                            continue
                        }
                        if (c == "\047") { qst = 1; qcontent = ""; continue }
                        if (c == "\"")   { qst = 2; qcontent = ""; continue }
                        if (c == "#" && (i == 1 || substr(l, i-1, 1) ~ /[ \t]/)) {
                            CODE = substr(l, 1, i - 1)
                            return
                        }
                        NLINE = NLINE c; DLINE = DLINE c
                        continue
                    }
                    if (qst == 1) {
                        if (c == "\047") {
                            qst = 0
                            if (length(qcontent) > 0) NLINE = NLINE "X"
                            DLINE = DLINE qcontent; qcontent = ""
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
                        if (length(qcontent) > 0) NLINE = NLINE "X"
                        DLINE = DLINE qcontent; qcontent = ""
                        continue
                    }
                    qcontent = qcontent c
                }
                if (qst != 0 && length(qcontent) > 0) {
                    NLINE = NLINE "X"; DLINE = DLINE qcontent; qcontent = ""
                }
            }

            # dequote_all(s) — drop every backslash and quote character.
            #
            # Deliberately NOT used on a whole line any more (see analyse). Only
            # on text already known to be a single token or an argument list
            # belonging to a confirmed command:
            #   * the head of a heredoc redirection, to resolve sou[EMPTY]rce.
            #   * the argument text of a confirmed `eval`, to resolve
            #     tr[EMPTY-PAIR]ap after eval re-parses it.
            function dequote_all(s,   t) {
                t = s
                gsub(/\\/, "", t)
                gsub("\047", "", t)
                gsub("\"", "", t)
                return t
            }

            # is a word present as a standalone token?
            function has_word(l, w) {
                return l ~ ("(^|[^A-Za-z0-9_])" w "([^A-Za-z0-9_]|$)")
            }

            function reason(   v, e) {
                # `trap`, optionally behind the `builtin`/`command` builtins, in
                # command position. Matched on the SKELETON, so string contents
                # can never supply the separator or the word.
                v = "((builtin|command)[[:space:]]+)*trap([[:space:]]|$)"
                if (NLINE ~ ("^[[:space:]]*" v) \
                    || NLINE ~ ("[;&|(){}][[:space:]]*" v) \
                    || NLINE ~ ("[[:space:]](then|do|else)[[:space:]]+" v))
                    return "trap command"
                # `eval` must be INVOKED, and only then is its argument DATA
                # searched for the word.
                #
                # Anchored exactly like the trap rule above, not a presence
                # test. has_word(NLINE, "eval") asks only whether the word
                # appears somewhere, which two unrelated assignments satisfy —
                # `mode=eval; msg="see the trap docs"` was flagged, as was
                # `run_mode eval "read the trap docs"` where eval is another
                # command_s argument (N22, N23). Found in review; the comment
                # here promised invocation while the code checked presence.
                e = "((builtin|command)[[:space:]]+)*eval([[:space:]]|$)"
                if ((NLINE ~ ("^[[:space:]]*" e) \
                     || NLINE ~ ("[;&|(){}][[:space:]]*" e) \
                     || NLINE ~ ("[[:space:]](then|do|else)[[:space:]]+" e)) \
                    && has_word(dequote_all(DLINE), "trap"))
                    return "eval with trap in its argument"
                return ""
            }

            in_hd {
                if ($0 ~ hd_term) in_hd = 0
                next
            }
            {
                analyse($0)
                if (CODE ~ /^[[:space:]]*$/) next

                # Arm the heredoc skip BEFORE matching, so a `trap` inside a
                # child-script body is not read as ours. "<<<" is a herestring
                # and must not arm it.
                #
                # Detected on CODE, not on the skeleton: the delimiter word is
                # DATA that must survive (it is the terminator to look for), and
                # a heredoc can sit inside a "$( ... )" that the skeleton
                # collapses to X — `bash -c "$(cat <<X)"` is exactly that shape,
                # and missing it would leave the body scanned as our code (N13).
                # Only the HEAD is dequoted, which is a single-token question:
                # is the command consuming this heredoc source/./eval?
                hd_bad = 0
                if (CODE !~ /<<</ && match(CODE, /<<-?[[:space:]]*['"'"'"]?[A-Za-z_][A-Za-z0-9_]*/)) {
                    head = dequote_all(substr(CODE, 1, RSTART - 1))
                    w = substr(CODE, RSTART, RLENGTH)
                    sub(/^<<-?[[:space:]]*/, "", w)
                    gsub(/['"'"'"]/, "", w)
                    hd_term = "^[[:space:]]*" w "[[:space:]]*$"
                    in_hd = 1
                    # A heredoc fed to source/./eval executes IN THIS SHELL.
                    if (has_word(head, "source") || has_word(head, "eval") \
                        || head ~ /(^|[;&|(){}])[[:space:]]*\.[[:space:]]/)
                        hd_bad = 1
                }

                why = hd_bad ? "heredoc sourced into this shell" : reason()
                if (why != "") {
                    line = CODE
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
# CASES — 34 must flag, 23 must not (57 total).
# CASES-TABLE-BEGIN — every ID below is cross-checked against the fixture
# files at the end of self_test(); the two cannot drift apart.
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
#   the SPELLING class — bash concatenates adjacent word fragments, so the
#   literal word `trap` need never appear (round-3 review):
#   P29 tr''ap                           P32 t\r\a\p
#   P30 tr""ap                           P33 eval "tr''ap ..."
#   P31 tr\ap                            P34 sou''rce /dev/stdin <<X
#
#   N01 whole-line comment               N08 `echo "trap"`
#   N02 trailing comment (eval + trap)   N09 `grep -n trap "$f"`
#   N03 word inside a string             N10 escaped-apostrophe + REAL comment
#   N04 `cat > file <<X` body            N11 `#` inside a quoted string
#   N05 `bash /dev/stdin <<X` body       N12 `$#` and `${#v}` then a comment
#   N06 `trapped=` / `entrapment=`       N13 `bash -c "$(cat <<X)"` body
#   N07 `mytrap 'c' EXIT`                    (a real child process)
#   the spelling normalisation must not invent false positives:
#   N14 `echo "tr''ap"`   (in a string)  N16 `mytr''ap 'c' EXIT`
#   N15 `# tr''ap 'c' EXIT`  (a comment)
#
#   live strings whose CONTENTS look like syntax — the class that a whole-line
#   dequote wrongly flagged (round-4 review), zero coverage before it:
#   N17 `fail_row "... eval ... trap ..."`   N20 inert string over a \-newline
#   N18 `msg="... don't;trap ..."`           N21 `"step 3: eval; trap; done"`
#   N19 backtick doc mention of both words
#
#   the eval gate must test INVOCATION, not the presence of the word
#   (round-5 review):
#   N22 `mode=eval; msg="see the trap docs"`
#   N23 `run_mode eval "read the trap docs for details"`
#
# CASES-TABLE-END
#
# P21-P25 are the quoting cases: an earlier decomment() counted quote
# characters, and a counter cannot express bash escaping. P29-P34 are the
# spelling cases, closed by dequote(). Both classes were found by review, not
# by this table — which is the argument for keeping the table growing.
self_test() {
    local dir bad good out id f failed=0
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
    # the spelling class: none of these contains the literal word `trap`
    printf '%s\n' "tr''ap 'rm -rf' EXIT"                                >"$bad/P29.sh"
    printf '%s\n' 'tr""ap '"'"'rm -rf'"'"' EXIT'                        >"$bad/P30.sh"
    printf '%s\n' "tr\\ap 'rm -rf' EXIT"                                >"$bad/P31.sh"
    printf '%s\n' "t\\r\\a\\p 'rm -rf' EXIT"                            >"$bad/P32.sh"
    printf '%s\n' "eval \"tr''ap 'rm -rf' EXIT\""                       >"$bad/P33.sh"
    printf '%s\n' "sou''rce /dev/stdin <<'PAY'" "trap 'x' EXIT" "PAY"   >"$bad/P34.sh"

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
    # dequote() must not turn these into command-position traps
    printf '%s\n' "echo \"tr''ap\""                                     >"$good/N14.sh"
    printf '%s\n' "# tr''ap 'rm -rf' EXIT -- describing the bypass"      >"$good/N15.sh"
    printf '%s\n' "mytr''ap 'rm -rf' EXIT"                              >"$good/N16.sh"
    # live, non-comment strings whose CONTENTS look like syntax. Before the
    # skeleton, dequoting the whole line let these read as real code (N17-N21).
    printf '%s\n' "fail_row \"please eval trap manually if this ever happens\"" >"$good/N17.sh"
    printf '%s\n' "msg=\"warning: don't;trap yourself here\""                >"$good/N18.sh"
    printf '%s\n' 'echo "run `eval` never `trap` directly"'                 >"$good/N19.sh"
    printf '%s\n' "msg=\"one \\" "two; trap three\""                         >"$good/N20.sh"
    printf '%s\n' "printf '%s\\n' \"step 3: eval; trap; done\""             >"$good/N21.sh"
    # `eval` present as a plain word, never invoked
    printf '%s\n' "mode=eval; msg=\"see the trap docs\""                   >"$good/N22.sh"
    printf '%s\n' "run_mode eval \"read the trap docs for details\""       >"$good/N23.sh"

    # Every must-flag case must produce at least one row against ITS OWN file.
    out=$(scan_dir "$bad")
    for f in "$bad"/*.sh; do
        id=$(basename "$f" .sh)
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

    # --- the prose CASES table must list exactly the fixtures that exist ------
    # Same drift class as every pinned count in this project: a table nothing
    # checks is a claim nothing checks. Adding a fixture without documenting it,
    # or documenting a case without a fixture, is now a hard failure rather than
    # something a reader has to notice. (This check was added after the previous
    # round's report CLAIMED the table was generated from the fixtures when
    # nothing connected them at all.)
    local -a doc_only=() fix_only=()
    local documented actual
    documented=$(sed -n '/# CASES-TABLE-BEGIN/,/# CASES-TABLE-END/p' "${BASH_SOURCE[0]}" \
                 | grep -oE '\b[PN][0-9]{2}\b' | sort -u)
    actual=$( { ls -1 "$bad" "$good"; } | sed -n 's/\.sh$//p' | sort -u)
    mapfile -t doc_only < <(comm -23 <(printf '%s\n' "$documented") <(printf '%s\n' "$actual"))
    mapfile -t fix_only < <(comm -13 <(printf '%s\n' "$documented") <(printf '%s\n' "$actual"))
    if [[ ${#doc_only[@]} -gt 0 ]]; then
        echo "ERROR: self-test: CASES table documents cases with no fixture: ${doc_only[*]}" >&2
        failed=1
    fi
    if [[ ${#fix_only[@]} -gt 0 ]]; then
        echo "ERROR: self-test: fixtures missing from the CASES table: ${fix_only[*]}" >&2
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
    echo "This lint is for the ACCIDENTAL trap, which is the failure that actually"
    echo "happened. It does not try to stop deliberate obfuscation and cannot. It also"
    echo "has a few known FALSE positives -- a subshell trap, and a live eval or heredoc"
    echo "head that merely mentions the word. If you have hit one of those, say so in"
    echo "the row and see the MAY WRONGLY FLAG block in test/00regression/lint-traps.sh."
    echo "Re-check with: bash test/00regression/lint-traps.sh"
} >&2
exit 1
