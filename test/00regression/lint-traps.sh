#!/usr/bin/env bash
# EXIT-trap lint for the regression harness row scripts (GH #153).
#
# WHY THIS EXISTS
#
# regression.sh SOURCES every row script into its own shell. Before the first
# one is sourced, test-functions.inc has already installed the harness's ONE
# documented trap:
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
# Any `trap` command in test/00regression/scripts/*.sh — not merely
# `trap ... EXIT`, and not merely at top level.
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
#     already uses. A trap inside an explicitly-executed child cannot reach the
#     harness's shell, and this lint does not see heredoc bodies (below).
#
# WHAT IT DOES NOT SEE
#
#   * Full-line comments.
#   * Heredoc bodies — that is the child-script shape above, deliberately.
#   * `trap` appearing as a word inside a string or prose (`fail_row "... SAVE
#     trap fired ..."`), because the match requires command position.
#
# Usage:
#   bash test/00regression/lint-traps.sh          # scan the real row scripts
#
# Exit 0 clean, 1 offenders found, 2 the lint itself is broken.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPTS_DIR="$SCRIPT_DIR/scripts"

# scan_dir <dir> — print "file:line:text" for every trap command found in
# <dir>/*.sh. Prints nothing (and succeeds) when the directory is clean.
#
# The awk does three things, in order: drop heredoc bodies, drop full-line
# comments, then match `trap` in COMMAND position — at the start of a line, or
# after a separator (`;` `&` `|` `(` `)` `{` `}`), or after then/do/else.
scan_dir() {
    local dir="$1" f
    for f in "$dir"/*.sh; do
        [[ -e "$f" ]] || continue
        awk -v FNAME="$f" '
            # --- heredoc bodies are another shells source, not ours ---
            in_hd {
                if ($0 ~ hd_term) in_hd = 0
                next
            }
            {
                line = $0

                # Note the heredoc BEFORE anything else on the line can hide
                # it. "<<<" is a herestring and must not arm the skip.
                if (line !~ /<<</ && match(line, /<<-?[[:space:]]*['"'"'"]?[A-Za-z_][A-Za-z0-9_]*/)) {
                    w = substr(line, RSTART, RLENGTH)
                    sub(/^<<-?[[:space:]]*/, "", w)
                    gsub(/['"'"'"]/, "", w)
                    hd_term = "^[[:space:]]*" w "[[:space:]]*$"
                    in_hd = 1
                }

                if (line ~ /^[[:space:]]*#/) next

                if (line ~ /^[[:space:]]*trap[[:space:]]/ \
                    || line ~ /[;&|(){}][[:space:]]*trap[[:space:]]/ \
                    || line ~ /[[:space:]](then|do|else)[[:space:]]+trap[[:space:]]/) {
                    sub(/^[[:space:]]+/, "", line)
                    printf "%s:%d:%s\n", FNAME, FNR, line
                }
            }
        ' "$f"
    done
}

# Self-test: prove the matcher still fires on a stray trap AND still ignores
# the three legitimate shapes, before its verdict on the real tree is trusted.
# A guard that has silently become a no-op reports success, which is worse than
# no guard; a guard that flags everything gets disabled, which is the same
# outcome. Both halves therefore run on every invocation.
self_test() {
    local dir bad good out failed=0
    dir=$(mktemp -d)
    bad="$dir/bad"; good="$dir/good"
    mkdir -p "$bad" "$good"

    cat >"$bad/offender.sh" <<'FIXTURE'
#!/usr/bin/env bash
trap 'rm -rf "$W"' EXIT
W=$(mktemp -d) && trap 'rm -rf "$W"' EXIT
if true; then trap 'rm -rf "$W"' EXIT; fi
FIXTURE

    cat >"$good/innocent.sh" <<'FIXTURE'
#!/usr/bin/env bash
# Installing a trap here would clobber the harness one; we do not.
fail_row " (SAVE trap fired during boot?)"
cat > "$W/child.sh" <<CHILD
#!/usr/bin/env bash
trap 'rm -rf "$D"' EXIT
CHILD
FIXTURE

    out=$(scan_dir "$bad")
    if [[ $(printf '%s\n' "$out" | grep -c . || true) -ne 3 ]]; then
        echo "ERROR: self-test: expected 3 offenders in the bad fixture, got:" >&2
        printf '%s\n' "$out" >&2
        failed=1
    fi
    out=$(scan_dir "$good")
    if [[ -n "$out" ]]; then
        echo "ERROR: self-test: the clean fixture was flagged:" >&2
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
    echo "[lint-traps] a row script installs its own trap:"
    printf '%s\n' "$OFFENDERS" | sed 's/^/  /'
    echo ""
    echo "regression.sh SOURCES these scripts into the harness shell, which already"
    echo "holds 'trap regression_cleanup EXIT' (test-functions.inc). Bash keeps ONE"
    echo "handler per signal, so yours REPLACES it and every successful run then"
    echo "leaks its 1-2 GB \$RUN_DIR while the count stays green (GH #153)."
    echo ""
    echo "Do this instead:"
    echo "  * scratch files -> create them under \$TMP_DIR; the harness trap already"
    echo "    removes that tree on every exit path, signals included."
    echo "  * a shell of your own that must clean up on a signal -> write it to a file"
    echo "    and run it with 'bash' (see sdcard-isolation-func.sh's child.sh). A trap"
    echo "    inside an executed child cannot reach the harness shell, and heredoc"
    echo "    bodies are invisible to this lint."
    echo ""
    echo "Re-check with: bash test/00regression/lint-traps.sh"
} >&2
exit 1
