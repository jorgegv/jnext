#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #27 S9 — THE FOREIGN READER (design §13.2(1)).
#
# This is the strongest evidence in §13.2 and the only row in the tree where an
# implementation jnext did not write adjudicates jnext's state.
#
# §13.1, which this exists to answer: jnext's `.szx` saver once wrote RAM pages
# 0-111 and its own loader accepted any `uint8_t` page with no upper bound, so
# save → load → compare passed byte-exact with discriminative, mutation-tested
# assertions — all green, all worthless. libspectrum's `read_ramp_chunk()`
# hard-rejects any page > 63, so EVERY `.szx` jnext could produce failed to
# load in real FUSE. Saver and loader shared the blind spot.
#
# The shape, per §13.2(1): a `.jns` of a 48K/128K machine expresses a strict
# SUPERSET of what a `.szx` expresses, so write both from the same machine
# state, have a foreign reader extract the `.szx`, and require jnext's `.jns`
# to agree.
#
# TWO DEVIATIONS FROM §13.2(1)'s LETTER, both deliberate and both recorded in
# the design doc's S9 append:
#
#  1. It links LIBSPECTRUM DIRECTLY (`test/snapshot/szx_probe.c`) instead of
#     scraping FUSE's GUI debugger. Same library — the one FUSE itself uses and
#     the one that rejected those pages — without Xvfb, a GTK UI, a breakpoint
#     that must be hit, or output scraped from a GUI process. §13.2(1) names
#     the failure that route invites: an invocation that produces NOTHING
#     compares an empty extraction against an empty expectation and passes
#     vacuously, "which converts the best evidence in §13.2 into the most
#     confident lie". Removing four ways to produce nothing is the point.
#  2. The pair is written by TWO RUNS to the same frame rather than one run
#     writing both, because `--delayed-snapshot` takes one path. The control
#     below proves that is equivalent: two independent runs to the same frame
#     produce BYTE-IDENTICAL `.szx`. Without it the comparison would rest on an
#     assumption of determinism rather than a measurement of it.
#
# WHAT IT DOES NOT PROVE (§13.3, stated rather than implied): the `.szx` in the
# comparison is written by jnext, so an error SHARED between `SzxSaver` and the
# JNS writer — an accessor that returns the wrong thing — produces two files
# that agree, and libspectrum confirms the agreement. This is a foreign reader
# for the FORMAT, not for the STATE. What it does catch, and what nothing else
# does, is JNS mis-encoding a value `SzxSaver` gets right. And it reaches only
# the CPU: nothing external can adjudicate Layer 2, sprites, the Copper, the
# tilemap, the NextREG file or the SD FSM, because no foreign implementation
# reads a Next snapshot.
if want snapshot-foreign-szx-func; then
    begin_func snapshot-foreign-szx-func
    PROBE="$PROJECT_DIR/build/test/szx_probe"
    READER="$PROJECT_DIR/test/snapshot/jns_reader.py"
    if [[ ! -x "$PROBE" ]]; then
        skip_row " (szx_probe not built — libspectrum-devel absent; this is the ONLY foreign adjudication in the tree, so its absence is worth seeing)"
    elif ! command -v python3 >/dev/null 2>&1; then
        skip_row " (no python3 — the .jns side needs the spec-written reader)"
    else
    szx="$TMP_DIR/foreign.szx"; szx2="$TMP_DIR/foreign2.szx"
    jns="$TMP_DIR/foreign.jns"
    rm -f "$szx" "$szx2" "$jns"

    # 128K: the richest machine a `.szx` can express, so the comparison covers
    # paging as well as the CPU.
    run_at_frame() {  # run_at_frame <outfile>
        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless \
            --machine 128k "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-snapshot "$1" --delayed-snapshot-frames 200 \
            --delayed-automatic-exit 20 >/dev/null 2>&1
    }
    a_rc=1; run_at_frame "$szx"  && a_rc=0
    b_rc=1; run_at_frame "$jns"  && b_rc=0
    c_rc=1; run_at_frame "$szx2" && c_rc=0

    # THE DETERMINISM CONTROL. Two independent runs to frame 200 must produce
    # byte-identical .szx, or "the same machine state" is an assumption.
    same_state=0
    [[ -s "$szx" && -s "$szx2" ]] && cmp -s "$szx" "$szx2" && same_state=1

    # THE FOREIGN EXTRACTION. §13.2(1): assert NON-EMPTY output BEFORE any
    # comparison — an empty-vs-empty comparison passes vacuously, and this is
    # the strongest substitute the design has.
    foreign_rc=1; foreign=""
    if [[ -s "$szx" ]]; then
        if foreign=$("$PROBE" "$szx" 2>/dev/null); then foreign_rc=0; fi
    fi
    got_output=0
    [[ "$foreign_rc" -eq 0 && ${#foreign} -gt 20 ]] \
        && grep -q '^pc=' <<<"$foreign" && got_output=1

    # THE jnext SIDE, read by the independent spec-written reader rather than
    # by jnext: two readers, neither of them the writer.
    mine=""
    mine_rc=1
    if [[ -s "$jns" ]]; then
        if mine=$(python3 "$READER" "$jns" --json 2>/dev/null); then mine_rc=0; fi
    fi

    agree=0
    detail=""
    if [[ "$got_output" -eq 1 && "$mine_rc" -eq 0 ]]; then
        # Both extractions go to FILES and the comparator reads them by path.
        # An earlier version passed one on argv and one on stdin with two
        # redirections on the same command, which is not valid: the last one
        # wins, the heredoc was discarded, and the row reported a traceback.
        printf '%s' "$foreign" > "$TMP_DIR/foreign.txt"
        printf '%s' "$mine"    > "$TMP_DIR/mine.json"
        detail=$(python3 "$PROJECT_DIR/test/snapshot/compare_szx_jns.py" \
                     "$TMP_DIR/foreign.txt" "$TMP_DIR/mine.json" 2>&1 || true)
        [[ "$detail" == "AGREE" ]] && agree=1
    fi

    if [[ "$a_rc" -eq 0 && "$b_rc" -eq 0 && "$c_rc" -eq 0 ]] \
       && [[ "$same_state" -eq 1 && "$got_output" -eq 1 && "$agree" -eq 1 ]]; then
        pass_row " (libspectrum ACCEPTS jnext's .szx and its CPU extraction agrees with the .jns read by the spec-written reader; two runs byte-identical, so the pair really is one machine state)"
    else
        fail_row " (a=$a_rc b=$b_rc c=$c_rc same_state=$same_state foreign_out=$got_output agree=$agree detail='$detail')"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
