#!/usr/bin/env bash
# Unescalated-`timeout` lint for the test tree.
#
# WHY THIS EXISTS
#
# `timeout N cmd` sends SIGTERM when N expires, and NOTHING AFTER THAT. A
# program that ignores, blocks or mishandles SIGTERM keeps running; `timeout`
# waits for it, then reports 124, and the caller reads 124 as "it was stopped".
# Measured on this host, coreutils 9.10:
#
#     $ time timeout 1 bash -c 'trap "" TERM; sleep 3'; echo $?
#     real 0m3.0s
#     124
#
# Three seconds, not one. The bound was decorative and the status lied.
#
# NOT HYPOTHETICAL. On 2026-09-22 two jnext processes were found alive 9289 s
# after a `timeout 120` in a regression row — reparented to systemd, the
# worktree that spawned them long deleted, ~18% CPU each. jnext does not act on
# SIGTERM; nothing escalated. They were burning a core apiece underneath the
# suite's real-time-pacing-bound rows (audio-underrun-func,
# screenshot-paused-func — see the JNEXT_TEST_JOBS note in CLAUDE.md), which
# are exactly the rows that report a FAIL when the box is loaded. A runaway of
# this class does not fail anything itself; it makes OTHER rows lie.
#
# The house form is
#
#     timeout --foreground --kill-after=5s Ns  CMD ...
#
# `--kill-after` is the whole point: SIGKILL cannot be ignored, so the bound is
# real. It costs nothing when the command is well behaved — it fires only if
# SIGTERM was already disregarded.
#
# A HAND SWEEP IS NOT A GATE, and the git history says so precisely. Commit
# d56ad276 looked at two files and fixed five call sites. Three more were
# already in the tree at that moment and it never looked at them:
#
#     timeout 110 cat     joy-uart-link-func.sh   39605ae9  09-23 01:16
#     timeout 15 sh -c    joy-uart-link-func.sh   d97bc2f1  09-23 01:59
#     timeout 600 python3 packaging-test.sh       699c376d  08-06 10:48
#     ------------------- the sweep -------------  d56ad276  09-23 02:25
#
# (An earlier draft of this header said the first two "arrived within hours"
# AFTER the sweep. They did not — they predate it by an hour and by 26
# minutes. Corrected in review; the timestamps above are the evidence.)
#
# The third is the one that matters for scope: seven weeks old, in a file the
# sweep had no reason to open, and invisible to any lint that looks only at
# regression row scripts. A sweep fixes what it is pointed at. That is what
# this file is instead: the gate, not the fix.
#
# ------------------------------------------------------------------------
# SCOPE — tracked `*.sh` under test/, recursively. Stated, not implied.
#
# BROADER than lint-traps.sh's `test/00regression/scripts/*.sh`, deliberately.
# A stray `trap` is only harmful in a script the harness SOURCES, so that
# lint's narrow scope is the hazard's real shape. This hazard has nothing to do
# with sourcing: a runaway process started by test/packaging/packaging-test.sh
# burns the same core on the same box as one started by a regression row. So
# the scope is the whole test tree — which is also where every one of the 162
# `timeout` call sites lives. `test/packaging/packaging-test.sh` had the third
# offender, and a row-script-only lint would have landed with it still in the
# tree, which is the "this one is fine" reasoning that let the first two in.
#
# NOT IN SCOPE, each for a reason rather than by omission:
#
#   * Makefile recipes. A recipe line is bash AFTER make expansion — a second
#     language layered on the one this scanner models exactly — and a false
#     positive there blocks every build, not one row. Measured before
#     excluding: the Makefile contains ZERO `timeout` invocations, so the
#     parser surface would buy nothing today. If a recipe ever launches a
#     long-running program under a timeout, extend the scan here rather than
#     trusting this comment.
#   * tools/, demo/, packaging/, src/. No `timeout` invocations in any tracked
#     shell there (checked). The hazard is about processes a TEST RUN leaves
#     behind, which is what makes a green suite untrustworthy.
#   * Untracked files. `git ls-files` decides membership, as lint-hardcoded-
#     paths.sh does: a scratch script nobody else runs is nobody else's problem.
#
# ------------------------------------------------------------------------
# WHAT COUNTS AS A VIOLATION — every bare `timeout`, with no exception list.
#
# A `timeout` invocation whose own leading option run contains no ESCALATION
# option. Escalation is `-k` / `--kill-after` in any spelling, or a signal
# option set to KILL (`-s KILL`, `--signal=SIGKILL`, `-s9`): SIGKILL cannot be
# caught, so it bounds the command by construction.
#
# The rule does NOT ask what program is being run, and that is the design
# rather than a limitation:
#
#   * It is not decidable. `timeout 110 cat` and `timeout 15 sh -c` — the two
#     offenders this lint landed with — both invoke programs that do die on
#     SIGTERM today. Whether they still do after the next rewrite, or under a
#     full pipe buffer, or when the command becomes `"$JNEXT"` because the row
#     grew, is not something a static scan can know.
#   * An exception list is the failure mode. "This one is fine" is precisely
#     the reasoning that produced a decorative timeout in the first place, and
#     a lint that accepts it needs the judgement re-made and re-reviewed at
#     every call site forever.
#   * Uniformity is what makes the rule cheap. One shape, copy it, done. The
#     escalation costs 16 characters and has NO downside — a command that exits
#     on SIGTERM never reaches the SIGKILL.
#
# `--foreground` is deliberately NOT required, although it is the other half of
# the house form. It addresses a different property (whether the command keeps
# the terminal's process group, so Ctrl-C and TTY reads reach it) and it is not
# universally correct: without it, `timeout` puts the command in its OWN
# process group and signals the whole group, which is what you want when the
# command spawns a process tree.
#
# That is not a theoretical preference — the sites that omit it are exactly the
# ones that bound a process TREE, and they cluster structurally rather than
# randomly: every `timeout` in `test/harness-selftest.sh` (which bounds whole
# harness and `make` invocations) and in `test/run-unit-tests.sh` (which bounds
# a suite) omits it, as does the wine console driver in
# `test/packaging/packaging-test.sh`. Requiring `--foreground` uniformly would
# leave the grandchildren of those runs unreachable by the KILL — reintroducing
# the very class this lint exists to stop — and would land the lint red on code
# that already bounds itself. A lint that flags correct code gets disabled.
#
# (An earlier draft quoted "158 of 162 escalate, 147 with --foreground". Those
# figures came from a grep that also counted comment lines and did not
# reproduce; review recounted with this lint own walker and got 149 invocation
# lines, 13 of them without `--foreground`, distributed exactly as above. The
# structural statement is both accurate and stable, so it replaces the counts.)
#
# ------------------------------------------------------------------------
# HOW THE MATCHING WORKS, AND WHY IT IS SHAPED LIKE THIS
#
# Everything is decided on a SYNTAX SKELETON in which every quoted string,
# whatever it contains, collapses to the single inert word character X. That is
# lint-traps.sh's analyse(), reused almost verbatim, and it is reused because
# that file earned the design the hard way: an earlier version of it dequoted
# whole lines and flagged correct rows whose STRING CONTENTS happened to look
# like syntax. A suite about timeouts is full of lines like
# `fail_row "... the timeout 120 never fired ..."`, and rejecting those would
# be a real cost (cases N01-N05 here).
#
# On the skeleton the line is then TOKENISED and walked as bash walks it, which
# is what lets each `timeout` be judged on ITS OWN option run:
#
#   * COMMAND POSITION is a state machine, not a regex. The position survives a
#     run of reserved words (`if ! timeout ...` — two of them, and `elif !` is
#     the commonest shape in this tree), a run of environment assignments
#     (`QT_QPA_PLATFORM=offscreen timeout ...` — 20-odd sites), and the wrapper
#     commands `command` / `builtin` / `env`. Anything else ends it, so
#     `echo timeout 5`, `grep -n timeout f`, `command -v timeout` and
#     `local timeout 5` are all inert.
#   * THE OPTION RUN is walked forward from the word until the first token that
#     does not begin with `-`; that token is the DURATION and the run is over.
#     This is exactly GNU timeout's own parse: its getopt string begins with
#     `+`, so permutation is off and options after the duration are passed to
#     the COMMAND. Measured: `timeout 2 -k 1 sleep 5` fails with
#     "failed to run command '-k'". So scanning only the leading run is
#     correct, not an approximation — and `timeout 5 -k 2 cmd`, which escalates
#     nothing, is correctly flagged.
#   * That per-invocation walk is what keeps `--kill-after` attached to a
#     DIFFERENT command on the same line from excusing this one:
#     `timeout -k 5s 5s a && timeout 10 b` flags the second (P11), and the
#     mirrored `timeout 10 a && timeout -k 5s 5s b` flags the first (P12). A
#     line-level `grep -q kill-after` passes both.
#   * BACKSLASH-CONTINUED LINES ARE JOINED first, bash-exactly (the backslash
#     and the newline both vanish, nothing is inserted), and an offence is
#     reported at the FIRST physical line. Without it,
#     `timeout --foreground \` + `--kill-after=5s 20s cmd` — a plausible way to
#     write a very long row — would be flagged although it is correct. That is
#     the expensive direction. N09/N10 pin it, and packaging-test.sh's real
#     offender is exactly this shape with the assignments on the line above.
#
# CAUGHT (case IDs in the CASES table below):
#   * `timeout` in command position with no escalation option, at line start,
#     after `; && || | ( ) { }`, after any bash reserved word a command may
#     follow, behind `command`/`builtin`/`env`, and behind any run of
#     environment assignments.
#   * an escalation option that belongs to a different `timeout` on the line.
#   * an option run split over continuation lines, in both directions.
#   * `timeout` spelled so the literal word never appears in ONE of its
#     variants — bash concatenates adjacent word fragments, so `tim''eout`,
#     `time\out` and `t\i\m\e\o\u\t` all run the program, and the EMPTY pair and
#     the backslash both resolve here. `"ti""meout"`, two NON-empty fragments,
#     does not: each collapses to its own X. That one is in NOT CAUGHT below,
#     squarely in the deliberate-obfuscation class.
#   * a signal option that is not KILL (`--signal=HUP`), which escalates
#     nothing.
#
# NOT CAUGHT — EXAMPLES, NOT AN EXHAUSTIVE LIST. Two lists in this project
# claimed exhaustiveness and were later proved incomplete; this one does not.
# Each was verified genuinely uncaught, and all are undecidable without running
# the script:
#   * the command name in a variable — `T=timeout; $T 60 "$JNEXT"`, or an
#     alias/function named `timeout`. Eight characters, no static grep sees it,
#     exactly as `t=trap; $t 'c' EXIT` defeats lint-traps.
#   * the option run in a variable — `timeout $TO_OPTS 60 cmd`. Undecidable,
#     and flagged rather than ignored: see MAY WRONGLY FLAG.
#   * a heredoc BODY. Bodies are skipped wholesale, because most of them here
#     are Python (`timeout = 5.0` is an assignment, not a command) and telling
#     the languages apart needs a consumer classifier. Measured before
#     deciding: ZERO `timeout` occurrences inside any heredoc body under test/.
#     A child shell script written through a heredoc and run later is therefore
#     a real hole — write it to a file and let the lint see it, or put the
#     escalation in by hand.
#   * a runaway that no `timeout` guards at all. This lint says an existing
#     bound is real; it cannot say a bound exists.
#   * any other wrapper command — `nohup timeout 5 x`, `stdbuf -o0 timeout ...`,
#     `xargs timeout ...`. Only `command`/`builtin`/`env` are modelled, being
#     the ones this tree uses.
#   * the word split into two NON-empty quoted fragments — `"ti""meout" 60 x`.
#     Each fragment collapses to its own inert X, so the word never forms. The
#     empty-pair and backslash variants DO resolve (P24, P25); this one is the
#     same deliberate-obfuscation class as a command name in a variable.
# These are OUT OF SCOPE, not a backlog. This lint is for the ACCIDENTAL bare
# `timeout` — the failure that actually happened, twice, in a suite whose other
# 158 call sites had the escalation. If you are assembling the command name
# from a variable you already know you are defeating a guard.
#
# MAY WRONGLY FLAG — the direction that COSTS, because a false positive blocks
# a correct row while a false negative merely fails to help. Measured, not
# guessed; each accepted as the safe direction, none present in the tree:
#   * an option run that is opaque — `timeout "$TO_OPTS" 60 cmd`, or a QUOTED
#     option `timeout "--kill-after=5s" 60 cmd`. The skeleton collapses the
#     quoted token to X, the walk sees a non-option, and the run ends with no
#     escalation found (P13). Safe direction on purpose: the escalation being
#     legible AT the call site is the property this lint exists to keep.
#   * `timeout` with a duration but no command (`timeout 5`), which runs
#     nothing. Flagged; nothing in the tree writes it. (`timeout --help` and
#     `timeout --version` ARE exempt — N11/N12 — because those are how a script
#     probes for the tool.)
# ------------------------------------------------------------------------
#
# Usage:
#   bash test/lint-timeouts.sh          # scan the tracked test-tree shell scripts
#
# Env (TEST ONLY — set by harness-selftest HS-56a/HS-56b to prove this lint is
# still wired into test/00regression/scripts/00-preflight-lint.sh; never set in
# a real run):
#   JNEXT_LINT_TIMEOUTS_DIR   scan <dir>/*.sh instead of the tracked test tree
#
# Exit 0 clean, 1 offenders found, 2 the lint itself is broken.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# scan_files <file...> — print "file:line: [reason] text" for every offender.
# Prints nothing (and succeeds) when every file is clean.
scan_files() {
    [[ $# -gt 0 ]] || return 0
    awk '
        BEGIN {
            # Bash reserved words a COMMAND may follow. The CLOSED set, taken
            # from bash(1) and filtered the same way lint-traps.sh filters it:
            # case/for/select/function are followed by a word or a name, `in`
            # is for/case syntax, done/esac/fi/}/]] are terminators, and [[
            # opens a conditional expression rather than a command.
            RW = "^(!|coproc|do|elif|else|if|then|time|until|while)$"
        }

        # ---------------------------------------------------------- analyse
        # analyse(l) — one bash-faithful pass producing TWO views. This is
        # lint-traps.sh analyse(), minus the DLINE view it needs for `eval`
        # and plus nothing; see that file for the full derivation of the
        # three-state scanner and why a quote COUNTER cannot replace it.
        #
        #   CODE   the line with any trailing/whole-line comment removed.
        #   SKEL   the SYNTAX SKELETON: every quoted string, whatever it
        #          contains, collapses to the single inert word character X;
        #          an empty quote pair collapses to nothing (so tim""eout is
        #          resolved); a backslash escape becomes its character for a
        #          word character and X otherwise. Only characters that were
        #          REALLY syntax survive.
        #
        # Three states (no apostrophe may appear in this comment: the whole awk
        # program is a single-quoted shell word):
        #   OUTSIDE  backslash escapes anything; either quote opens a string;
        #            a # that starts a word begins the comment.
        #   SINGLE   nothing escapes; only the apostrophe closes.
        #   DOUBLE   backslash escapes only " \ $ and the backtick.
        # qst and qcontent carry across lines, so a string opened on one line
        # and closed on the next is still one inert token.
        function analyse(l,   i, n, c, nxt) {
            SKEL = ""; CODE = l
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
                    if (c == "#" && (i == 1 || substr(l, i-1, 1) ~ /[ \t]/)) {
                        CODE = substr(l, 1, i - 1)
                        return
                    }
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

        # ------------------------------------------------------- option run
        # escalates(i) — walk the option run of the `timeout` whose word sits
        # at token i, and say whether it bounds its command.
        #
        # Returns 1 for escalates-or-exempt, 0 for a bare timeout. The walk
        # stops at the first token not beginning with `-`, which is GNU
        # timeouts DURATION; see the getopt `+` note in the header for why
        # that is the whole option run and not merely the start of it.
        function escalates(i,   j, t, want) {
            want = 0
            for (j = i + 1; j <= NT; j++) {
                t = TOK[j]
                if (t == "") continue
                if (want) {                       # argument of a split -s
                    if (t ~ /^(SIG)?(KILL|9)$/) return 1
                    want = 0
                    continue
                }
                if (t !~ /^-/) return 0           # the DURATION: run is over
                # --help / --version run no command at all.
                if (t == "--help" || t == "--version") return 1
                # --kill-after in either spelling.
                if (t ~ /^--kill-after=/) return 1
                if (t == "--kill-after") return 1       # split form: -k <dur>
                # --signal, only when it is KILL; anything else is catchable.
                if (t ~ /^--signal=(SIG)?(KILL|9)$/) return 1
                if (t == "--signal") { want = 1; continue }
                if (t ~ /^--/) continue           # --foreground, --preserve-status
                # A short cluster. -k takes an argument, attached or split;
                # bash-style clusters (-vk5s) are handled by looking for the
                # letter anywhere in the run.
                if (t ~ /^-[A-Za-z]*k$/)  return 1       # value is the next token
                # Attached value, and it has to LOOK like one: GNU takes the
                # rest of the token as the argument, so `-k=5s` and `-ks5s`
                # are "invalid time interval" (measured, exit 125) and bound
                # nothing at all. Flagged, not excused. X is a quoted token.
                if (t ~ /^-[A-Za-z]*k[0-9$X]/) return 1  # value attached
                if (t ~ /^-[A-Za-z]*s$/)  { want = 1; continue }
                if (t ~ /^-[A-Za-z]*s(SIG)?(KILL|9)$/) return 1
                continue                          # -v, -p, anything else
            }
            return 0                              # ran off the end still in options
        }

        # ---------------------------------------------------- command walk
        # offenders(s) — tokenise the skeleton and walk it the way bash does,
        # reporting every `timeout` invocation that does not escalate.
        #
        # `cmd` is "the next word would be a command name". It starts true,
        # is restored by every separator, and SURVIVES a reserved word, an
        # environment assignment and a wrapper command — the three prefixes
        # that really do precede a command in this tree. Anything else clears
        # it, which is what makes `echo timeout 5` and `grep -n timeout f`
        # inert without a special case for either.
        # Returns 1 on the FIRST offending invocation and stops: the report is
        # one row per line, so a second hit on the same line has nothing to add.
        function offenders(s,   i, t, cmd, aftertime) {
            gsub(/[;&|(){}]/, " & ", s)           # separators become tokens
            NT = split(s, TOK, /[[:space:]]+/)
            cmd = 1; aftertime = 0
            for (i = 1; i <= NT; i++) {
                t = TOK[i]
                if (t == "") continue
                if (t ~ /^[;&|(){}]$/) { cmd = 1; aftertime = 0; continue }
                if (cmd) {
                    # `time [-p] [--] pipeline` is the one reserved word that
                    # takes options; every other takes none (lint-traps.sh
                    # measured each of them).
                    if (aftertime && (t == "-p" || t == "--")) continue
                    if (t ~ RW) { aftertime = (t == "time"); continue }
                    aftertime = 0
                    if (t ~ /^[A-Za-z_][A-Za-z0-9_]*=/) continue        # VAR=val
                    if (t ~ /^(command|builtin|env)$/) continue         # wrappers
                    if (t == "timeout") {
                        if (!escalates(i)) return 1
                        # `timeout` is itself a command prefix: the word after
                        # its duration is another command name. Leaving cmd set
                        # is what catches `timeout 5 timeout 5 x` and costs
                        # nothing else, since the option/duration tokens are
                        # not command names anybody writes.
                        continue
                    }
                }
                cmd = 0; aftertime = 0
            }
            return 0
        }

        # ---------------------------------------------------------- emit
        # emit(f, n, code, skel) — one LOGICAL line: arm a heredoc skip if it
        # opens one, then report it if it carries an unescalated invocation.
        function emit(f, n, code, skel,   w, line) {
            if (code ~ /^[[:space:]]*$/) return

            # Arm the heredoc skip BEFORE matching, so a `timeout` in a child
            # script body is not read as ours. Detected on CODE, not on the
            # skeleton: the delimiter word is DATA that must survive, and a
            # heredoc can sit inside a "$( ... )" the skeleton collapses to X.
            # "<<<" is a herestring and must not arm it.
            if (code !~ /<<</ && match(code, /<<-?[[:space:]]*['"'"'"]?[A-Za-z_][A-Za-z0-9_]*/)) {
                w = substr(code, RSTART, RLENGTH)
                sub(/^<<-?[[:space:]]*/, "", w)
                gsub(/['"'"'"]/, "", w)
                hd_term = "^[[:space:]]*" w "[[:space:]]*$"
                in_hd = 1
            }

            if (offenders(skel)) {
                line = code
                sub(/^[[:space:]]+/, "", line)
                printf "%s:%d: [timeout with no --kill-after] %s\n", f, n, line
            }
        }

        # --------------------------------------------------------- driver
        #
        # ORDER MATTERS, AND IT IS THE OPPOSITE OF THE OBVIOUS ONE. analyse()
        # runs on each PHYSICAL line first and the continuation question is
        # then asked of CODE — the line with its comment already removed —
        # never of the raw text.
        #
        # A backslash has NO special meaning inside a `#` comment: bash ends
        # the comment at the newline, continuation or not. Measured:
        #
        #     $ bash -c $(: ...)echo before # a comment ending in backslash \
        #     echo second-line-executed-if-joined
        #     before
        #     second-line-executed-if-joined      <- a SEPARATE statement
        #
        # Deciding on the raw line therefore swallows the line after any
        # comment that happens to end in a backslash, and a bare `timeout`
        # there becomes invisible. That is not a hypothetical shape in this
        # tree: test/packaging/package-recipe-guard-test.sh:8 documents a
        # multi-line invocation inside a comment block and ends exactly that
        # way, harmless only because the line after it is another comment.
        # Found in review; cases P26-P28 and N29-N31 pin both orderings.
        #
        # The join itself is bash-exact: the backslash and the newline both
        # disappear and nothing is inserted, so `--kill\` + `-after=5s` is one
        # token. SKEL needs no trimming — analyse() already drops a trailing
        # unquoted backslash, having no next character to escape. An offence
        # is reported at the FIRST physical line of the logical line.
        FNR == 1 {
            # A file that ends mid-continuation still owes its last logical
            # line; flush it against the file it came from before resetting.
            if (joining) emit(prevfile, startln, accCODE, accSKEL)
            qst = 0; qcontent = ""; in_hd = 0
            joining = 0; accCODE = ""; accSKEL = ""; startln = 0
        }
        { prevfile = FILENAME }

        # Heredoc bodies are skipped wholesale — see NOT CAUGHT in the header.
        in_hd { if ($0 ~ hd_term) in_hd = 0; next }

        {
            analyse($0)

            nb = 0
            while (substr(CODE, length(CODE) - nb, 1) == "\\") nb++

            if (!joining) { startln = FNR; accCODE = ""; accSKEL = "" }
            if (nb % 2 == 1) {
                accCODE = accCODE substr(CODE, 1, length(CODE) - 1)
                accSKEL = accSKEL SKEL
                joining = 1
                next
            }
            accCODE = accCODE CODE
            accSKEL = accSKEL SKEL
            joining = 0

            emit(FILENAME, startln, accCODE, accSKEL)
        }

        END { if (joining) emit(prevfile, startln, accCODE, accSKEL) }
    ' "$@"
}

# ---------------------------------------------------------------- self-test
#
# Runs on EVERY invocation, both directions. A guard that has silently become a
# no-op reports success, which is worse than no guard; a guard that flags
# everything gets disabled, which ends the same way.
#
# ONE FILE PER CASE, deliberately — the same reasoning lint-traps.sh records:
# a combined fixture asserting a total row count goes green on a mutation that
# LOSES one case and GAINS another. Per case, a lost case is named.
#
# MUTATION-TESTED, and the table below is what the mutations shaped. Each rule
# in the scanner was reverted in turn and the self-test had to name the loss:
# 19 of 20 mutations killed. Two of them found real holes rather than merely
# confirming rules:
#
#   * reverting the comment stripper changed NOTHING, because every comment
#     fixture was inert for a second reason as well. N27/N28 (a comment
#     carrying a separator) exist because of that, not because anyone thought
#     of them first.
#   * reverting the join/analyse ORDER verbatim loses P26, P27 and P28 — the
#     comment-ending-in-a-backslash blocker that review found. Per-rule
#     mutation cannot discover it: it regresses rules that were written, and
#     this was an INTERACTION between two correct rules that nobody had put
#     together. Continuation had only ever been tested without a comment, and
#     comments only ever without a continuation.
#
# THE ONE SURVIVOR, stated rather than papered over: dropping the per-file
# `FNR == 1` state reset kills no case. It is hygiene, not a matching rule —
# it stops an unterminated quote or heredoc in one file from changing how the
# NEXT file is read. No fixture can exhibit it without an ordered pair of
# files contrived for the purpose, and a real .sh with an unterminated quote
# does not parse as bash, so the leak has no way into a working tree. It stays
# because file-order-dependent behaviour is worse than an unpinned line.
#
# CASES — 30 must flag, 31 must not (61 total).
# CASES-TABLE-BEGIN — every ID below is cross-checked against the fixture files
# at the end of self_test(); the two cannot drift apart.
#
#   the bare invocation, in every command position bash offers:
#   P01 bare `timeout 5 cmd`             P06 after `else`
#   P02 indented                         P07 after `do`
#   P03 after `;`                        P08 after `!`
#   P04 after `&&`                       P09 after `time -p`
#   P05 after `if`                       P10 inside `$( ... )`
#
#   the option run belongs to ITS OWN invocation:
#   P11 escalated FIRST, bare second     P12 bare first, escalated second
#
#   options that are present but do not bound the command:
#   P13 a QUOTED option run is opaque    P15 `--signal=HUP` is catchable
#   P14 `-s TERM` is catchable           P16 options AFTER the duration
#
#   the prefixes that keep command position — a miss here is a FALSE NEGATIVE
#   on the commonest shape in this tree (20-odd sites are `VAR=x timeout ...`):
#   P17 `QT_QPA_PLATFORM=offscreen timeout`   P20 `command timeout`
#   P18 two assignments in a row              P21 `elif ! timeout` (two RWs)
#   P19 `env timeout`
#
#   continuation lines, both directions:
#   P22 the option run is split, bare    P23 the whole invocation is on line 2
#
#   the SPELLING class — bash concatenates adjacent word fragments, so the
#   literal word need never appear. Only the EMPTY-pair and backslash variants,
#   which is the part lint-traps.sh pins too; see NOT CAUGHT for the rest:
#   P24 tim""eout                        P25 t\i\m\e\o\u\t
#
#   a comment ending in a BACKSLASH does not continue the line — bash ends the
#   comment at the newline. Deciding the join before stripping comments
#   swallowed the statement below it, and none of the cases above could see it:
#   continuation was only ever tested without a comment and comments only ever
#   without a continuation. Found in review, both orderings now pinned:
#   P26 trailing comment, then a bare timeout
#   P27 whole-line comment, then a bare timeout
#   P28 a REAL join first, then the comment trap, then a bare timeout
#
#   an option that LOOKS like escalation and is not — GNU takes the rest of a
#   short token as -k's argument, so both of these are "invalid time interval"
#   (measured: exit 125) and bound nothing:
#   P29 `-k=5s`                          P30 `-ks5s`
#
#   ---- and the other direction ----
#
#   the escalated spellings must all pass:
#   N01 the house form                   N05 `-k5s` attached
#   N02 `--kill-after=5s` alone          N06 `-s KILL` (uncatchable by construction)
#   N03 `--kill-after 5s` split          N07 `--signal=SIGKILL`
#   N04 `-k 5s` split                    N08 `-vk 5s` cluster
#
#   continuation lines must not manufacture an offence:
#   N09 escalation on the CONTINUATION   N10 escalation on the FIRST line
#
#   the two probes that run no command:
#   N11 `timeout --help`                 N12 `timeout --version`
#
#   `timeout` as data, a word, or an argument — the class an earlier
#   whole-line dequote would have flagged in lint-traps.sh:
#   N13 `fail_row "... the timeout 120 never fired"`
#   N14 a whole-line comment             N16 a live string whose CONTENTS
#   N15 a trailing comment                   look like syntax
#   N17 `msg="if timeout then"`          N18 backtick doc mention
#
#   `timeout` as something other than a command name:
#   N19 `echo timeout 5`                 N22 `timeout_pid=$!`
#   N20 `grep -n timeout "$f"`           N23 `wall_timeout=$(( x ))`
#   N21 `command -v timeout`             N24 `mytimeout 5 cmd`
#
#   a comment carrying a SEPARATOR, which would restore command position if the
#   comment were not stripped first. Added after mutation testing: without
#   these, reverting the comment stripper changed nothing, because every other
#   comment fixture was inert for a second reason as well:
#   N27 whole-line comment with a `;`    N28 trailing comment with a `(`
#
#   the heredoc exemption, and its limits:
#   N25 a python3 heredoc body           N26 a `cat > child.sh` body
#
#   the comment/continuation interaction in the direction that COSTS — the
#   blocker fix must not start flagging whatever follows a comment, nor break
#   a genuine multi-line invocation that carries one:
#   N29 comment ending in a backslash, then an ESCALATED timeout
#   N30 a genuine join whose LAST line carries a trailing comment
#   N31 a comment inside a genuine join terminates it, bash-exactly, and what
#       is left had already escalated
#
# CASES-TABLE-END
self_test() {
    local dir bad good out id f failed=0
    local -a missing=() spurious=()
    dir=$(mktemp -d)
    bad="$dir/bad"; good="$dir/good"
    mkdir -p "$bad" "$good"

    # --- 30 cases that MUST be flagged, one file each -----------------------
    printf '%s\n' 'timeout 60 "$JNEXT" --headless'                       >"$bad/P01.sh"
    printf '%s\n' '    timeout 60 "$JNEXT" --headless'                   >"$bad/P02.sh"
    printf '%s\n' 'rm -f "$log"; timeout 60 "$JNEXT"'                    >"$bad/P03.sh"
    printf '%s\n' 'mkdir "$W" && timeout 60 "$JNEXT"'                    >"$bad/P04.sh"
    printf '%s\n' 'if timeout 60 "$JNEXT"; then :; fi'                   >"$bad/P05.sh"
    printf '%s\n' 'if false; then :; else timeout 60 "$JNEXT"; fi'       >"$bad/P06.sh"
    printf '%s\n' 'for f in a b; do timeout 60 "$JNEXT"; done'           >"$bad/P07.sh"
    printf '%s\n' '! timeout 60 "$JNEXT"'                                >"$bad/P08.sh"
    printf '%s\n' 'time -p timeout 60 "$JNEXT"'                          >"$bad/P09.sh"
    printf '%s\n' 'out=$(timeout 60 "$JNEXT" 2>&1)'                      >"$bad/P10.sh"
    printf '%s\n' 'timeout -k 5s 5s a && timeout 10 b'                   >"$bad/P11.sh"
    printf '%s\n' 'timeout 10 a && timeout -k 5s 5s b'                   >"$bad/P12.sh"
    printf '%s\n' 'timeout "--kill-after=5s" 60 "$JNEXT"'                >"$bad/P13.sh"
    printf '%s\n' 'timeout -s TERM 60 "$JNEXT"'                          >"$bad/P14.sh"
    printf '%s\n' 'timeout --signal=HUP 60 "$JNEXT"'                     >"$bad/P15.sh"
    printf '%s\n' 'timeout 60 --kill-after=5s "$JNEXT"'                  >"$bad/P16.sh"
    printf '%s\n' 'QT_QPA_PLATFORM=offscreen timeout 60 "$JNEXT"'        >"$bad/P17.sh"
    printf '%s\n' 'MAKEFLAGS= HOME="$W/home" timeout 60 make'            >"$bad/P18.sh"
    printf '%s\n' 'env PATH=/nonexistent timeout 60 "$JNEXT"'            >"$bad/P19.sh"
    printf '%s\n' 'command timeout 60 "$JNEXT"'                          >"$bad/P20.sh"
    printf '%s\n' 'if false; then :; elif ! timeout 60 "$JNEXT"; then :; fi' >"$bad/P21.sh"
    printf '%s\n' 'timeout --foreground \' '    60 "$JNEXT" --headless'  >"$bad/P22.sh"
    printf '%s\n' 'WINEDEBUG=-all \' '   timeout 600 python3 drv.py'     >"$bad/P23.sh"
    printf '%s\n' 'tim""eout 60 "$JNEXT"'                                >"$bad/P24.sh"
    printf '%s\n' 't\i\m\e\o\u\t 60 "$JNEXT"'                            >"$bad/P25.sh"
    # a backslash is INERT inside a comment: bash ends the comment at the
    # newline, so the line below it is a statement of its own. Deciding the
    # join on the raw line swallowed it (found in review).
    printf '%s\n' 'echo before # a note ending in a backslash \' 'timeout 5 cmd' >"$bad/P26.sh"
    printf '%s\n' '# a whole-line note ending in a backslash \' 'timeout 5 cmd'  >"$bad/P27.sh"
    # the other ordering: a REAL continuation first, then the comment trap.
    printf '%s\n' 'echo a \' '   b   # trailing note ending in a backslash \' 'timeout 5 cmd' >"$bad/P28.sh"
    # GNU takes the rest of a short token as -k'"'"'s argument, so neither of
    # these is a valid interval and neither bounds anything.
    printf '%s\n' 'timeout -k=5s 60 "$JNEXT"'                           >"$bad/P29.sh"
    printf '%s\n' 'timeout -ks5s 60 "$JNEXT"'                           >"$bad/P30.sh"

    # --- 31 shapes that MUST NOT be flagged ---------------------------------
    # A hit here is a false positive that would block a row an author may write.
    printf '%s\n' 'timeout --foreground --kill-after=5s 60s "$JNEXT"'    >"$good/N01.sh"
    printf '%s\n' 'timeout --kill-after=5s 60s "$JNEXT"'                 >"$good/N02.sh"
    printf '%s\n' 'timeout --kill-after 5s 60s "$JNEXT"'                 >"$good/N03.sh"
    printf '%s\n' 'timeout -k 5s 60s "$JNEXT"'                           >"$good/N04.sh"
    printf '%s\n' 'timeout -k5s 60s "$JNEXT"'                            >"$good/N05.sh"
    printf '%s\n' 'timeout -s KILL 60s "$JNEXT"'                         >"$good/N06.sh"
    printf '%s\n' 'timeout --signal=SIGKILL 60s "$JNEXT"'                >"$good/N07.sh"
    printf '%s\n' 'timeout -vk 5s 60s "$JNEXT"'                          >"$good/N08.sh"
    printf '%s\n' 'timeout --foreground \' '    --kill-after=5s 60s "$JNEXT"' >"$good/N09.sh"
    printf '%s\n' 'timeout --kill-after=5s \' '    60s "$JNEXT" --headless'   >"$good/N10.sh"
    printf '%s\n' 'timeout --help >/dev/null'                            >"$good/N11.sh"
    printf '%s\n' 'timeout --version | head -1'                          >"$good/N12.sh"
    printf '%s\n' 'fail_row " (the timeout 120 never fired)"'            >"$good/N13.sh"
    printf '%s\n' '# a bare timeout 60 here would not escalate'          >"$good/N14.sh"
    printf '%s\n' 'seen=1   # timeout 60 would be wrong, see lint-timeouts.sh' >"$good/N15.sh"
    printf '%s\n' 'msg="step 3: timeout 60; then check"'                 >"$good/N16.sh"
    printf '%s\n' 'msg="if timeout then"'                                >"$good/N17.sh"
    printf '%s\n' 'echo "never run `timeout 5` unescalated"'             >"$good/N18.sh"
    printf '%s\n' 'echo timeout 5'                                       >"$good/N19.sh"
    printf '%s\n' 'grep -n timeout "$f"'                                 >"$good/N20.sh"
    printf '%s\n' 'command -v timeout >/dev/null || exit 2'              >"$good/N21.sh"
    printf '%s\n' 'timeout_pid=$!'                                       >"$good/N22.sh"
    printf '%s\n' 'wall_timeout=$(( (exit_delay + 5) * 4 ))'             >"$good/N23.sh"
    printf '%s\n' 'mytimeout 5 "$JNEXT"'                                 >"$good/N24.sh"
    printf '%s\n' '# never write: rm -f "$log"; timeout 60 "$JNEXT"'     >"$good/N27.sh"
    printf '%s\n' 'sleep 1   # (timeout 60 would not escalate)'          >"$good/N28.sh"
    printf '%s\n' "python3 - <<'PY'" 'timeout = 60' 'PY'                 >"$good/N25.sh"
    printf '%s\n' "cat > child.sh <<'CHILD'" 'timeout 60 jnext' 'CHILD'  >"$good/N26.sh"
    # the comment/continuation interaction must not flag CORRECT code either:
    # the line after the comment is judged on its own, and it escalates.
    printf '%s\n' 'echo before # a note ending in a backslash \' 'timeout --kill-after=5s 5s cmd' >"$good/N29.sh"
    # a genuine two-line invocation whose LAST line carries a comment.
    printf '%s\n' 'timeout --kill-after=5s \' '    60s "$JNEXT"   # bounded' >"$good/N30.sh"
    # a comment inside a genuine join TERMINATES the command, exactly as bash
    # does (measured); what is left had already escalated.
    printf '%s\n' 'timeout --kill-after=5s \' '# note \' '60s cmd'  >"$good/N31.sh"

    # Every must-flag case must produce at least one row against ITS OWN file.
    out=$(scan_files "$bad"/*.sh)
    for f in "$bad"/*.sh; do
        id=$(basename "$f" .sh)
        grep -q "/$id\.sh:" <<<"$out" || missing+=("$id")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "ERROR: self-test: these must-flag cases were NOT caught: ${missing[*]}" >&2
        failed=1
    fi

    # ... and every must-not case must produce none.
    out=$(scan_files "$good"/*.sh)
    if [[ -n "$out" ]]; then
        for id in $(grep -o '/N[0-9]*\.sh:' <<<"$out" | tr -d '/:' | sed 's/\.sh//' | sort -u); do
            spurious+=("$id")
        done
        echo "ERROR: self-test: legitimate code was flagged (false positive) in: ${spurious[*]}" >&2
        printf '%s\n' "$out" >&2
        failed=1
    fi

    # --- the prose CASES table must list exactly the fixtures that exist -----
    # Same drift class as every pinned count in this project: a table nothing
    # checks is a claim nothing checks. Adding a fixture without documenting
    # it, or documenting a case with no fixture, is a hard failure rather than
    # something a reader has to notice.
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
        echo "[lint-timeouts] SELF-TEST FAILED: the lint no longer matches what it" >&2
        echo "claims to match, so a clean report would be meaningless. Refusing." >&2
        exit 2
    fi
}

self_test

# --- the files to scan --------------------------------------------------
# Tracked *.sh under test/, recursively. `git ls-files` decides membership for
# the same reason lint-hardcoded-paths.sh uses it: an untracked scratch script
# is nobody else's problem, and a file that is not in the tree cannot make a
# shared suite lie.
declare -a FILES=()
if [[ -n "${JNEXT_LINT_TIMEOUTS_DIR:-}" ]]; then
    [[ -d "$JNEXT_LINT_TIMEOUTS_DIR" ]] || {
        echo "ERROR: JNEXT_LINT_TIMEOUTS_DIR is not a directory: $JNEXT_LINT_TIMEOUTS_DIR" >&2
        exit 2
    }
    for f in "$JNEXT_LINT_TIMEOUTS_DIR"/*.sh; do
        [[ -e "$f" ]] && FILES+=("$f")
    done
else
    mapfile -t FILES < <(git -C "$PROJECT_DIR" ls-files -- 'test/*.sh' \
                         | sed "s#^#$PROJECT_DIR/#")
    [[ ${#FILES[@]} -gt 0 ]] || {
        echo "ERROR: no tracked test/*.sh found — is this a git checkout?" >&2
        exit 2
    }
fi

OFFENDERS=$(scan_files "${FILES[@]+"${FILES[@]}"}")

if [[ -z "$OFFENDERS" ]]; then
    echo "[lint-timeouts] scanned: ${#FILES[@]} shell scripts  offenders: 0"
    exit 0
fi

echo "[lint-timeouts] scanned: ${#FILES[@]} shell scripts  offenders: $(printf '%s\n' "$OFFENDERS" | wc -l | tr -d ' ')"
{
    echo "[lint-timeouts] a test script runs 'timeout' with no escalation:"
    printf '%s\n' "$OFFENDERS" | sed "s#^$PROJECT_DIR/##;s/^/  /"
    echo ""
    echo "'timeout N cmd' sends SIGTERM and nothing more. A program that does not act"
    echo "on SIGTERM keeps running; timeout waits for it and then reports 124, so the"
    echo "bound is decorative and the status lies. Two jnext processes were found alive"
    echo "9289 s after a 'timeout 120', reparented to systemd, burning a core apiece"
    echo "underneath the suite's real-time-pacing-bound rows."
    echo ""
    echo "Use the house form:"
    echo "  timeout --foreground --kill-after=5s 60s \"\$JNEXT\" ..."
    echo ""
    echo "  * --kill-after is the requirement: SIGKILL cannot be ignored. It costs"
    echo "    nothing when the command is well behaved — it fires only if SIGTERM was"
    echo "    already disregarded. '--signal=KILL' satisfies the lint too."
    echo "  * --foreground is NOT required and is not always right: without it the"
    echo "    command gets its own process group and the signal reaches its CHILDREN,"
    echo "    which is what you want when it spawns a process tree."
    echo ""
    echo "The rule has no exception list, on purpose: whether a program handles SIGTERM"
    echo "is not statically decidable, and 'this one is fine' is the reasoning that put"
    echo "the bare timeout there in the first place. See the header of"
    echo "test/lint-timeouts.sh for what it cannot catch and for its known false"
    echo "positives (an opaque/quoted option run is flagged, deliberately)."
    echo "Re-check with: bash test/lint-timeouts.sh"
} >&2
exit 1
