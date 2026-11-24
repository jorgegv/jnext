#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable - the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# .psg export (GH #538): --psg-record writes each AY chip's register stream in the layout of
# Fuse 1.6.0's AY log. The program is test/audio/fixtures/psg_fixture.bin (its .asm lists the
# design); the oracle is psg_fixture.fuse.psg, recorded from the packaged Fuse by
# tools/fuse-psg-reference.sh. Both files go through tools/psg-normalise.py, which cuts the lead-in
# before the program's sentinel and the trailing run (they depend on where a recording starts and
# stops) and keeps everything between byte for byte; the Fuse body is pinned at N = 4728 bytes, the
# same number audio_debug_test PSF-03 pins for the C++ twin of the normaliser.
#   arm 1  headless 128K run: the normalised body equals Fuse's; no AY#1 or AY#2 file;
#   arm 2  the same with --silent gives the same file (the recording does not need the mixer);
#   arm 3  an unwritable FILE is refused before the machine starts, naming the option and the path;
#   arm 4  a write error exits 1 and says so (/dev/full, Linux only: macOS has none).
if want psg-record-func; then
    begin_func psg-record-func
    fixture="$SCRIPT_DIR/../audio/fixtures/psg_fixture.bin"
    fuse_ref="$SCRIPT_DIR/../audio/fixtures/psg_fixture.fuse.psg"
    norm="$SCRIPT_DIR/../../tools/psg-normalise.py"
    mine="$TMP_DIR/fx.psg"
    mine_silent="$TMP_DIR/fx-silent.psg"
    rm -f "$mine" "$mine_silent" "$TMP_DIR/fx-ay1.psg" "$TMP_DIR/fx-ay2.psg"
    run_psg() {   # run_psg FILE [extra args]: the fixture on a 128K machine, the space key at frame 150
        local out=$1; shift
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine 128k "${SD_CARD_ARGS[@]}" \
            --inject "$fixture" --inject-org 8000 --inject-pc 8000 --inject-delay 100 \
            --delayed-keypress-frames 150 SPACE --delayed-automatic-exit-frames 1650 \
            --psg-record "$out" "$@"
    }
    # The fuse body is pinned to N bytes.
    n_fuse=4728
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for the .psg normaliser)"
    elif [[ ! -r "$fixture" || ! -r "$fuse_ref" || ! -r "$norm" ]]; then
        fail_row " (fixture, Fuse reference or normaliser missing)"
    elif ! run_psg "$mine" &>"$TMP_DIR/psg1.log"; then
        fail_row " (the --psg-record run failed)"
    elif ! python3 -I "$norm" "$mine" "$TMP_DIR/mine.body" 2>"$TMP_DIR/psg1.norm"; then
        fail_row " (the recording does not normalise: $(head -n 1 "$TMP_DIR/psg1.norm"))"
    elif ! python3 -I "$norm" "$fuse_ref" "$TMP_DIR/fuse.body" 2>"$TMP_DIR/psg1.fnorm"; then
        fail_row " (the Fuse reference does not normalise: $(head -n 1 "$TMP_DIR/psg1.fnorm"))"
    elif [[ $(( $(wc -c <"$TMP_DIR/fuse.body") )) != "$n_fuse" ]]; then
        fail_row " (the Fuse reference body is not $n_fuse bytes)"
    elif ! cmp -s "$TMP_DIR/mine.body" "$TMP_DIR/fuse.body"; then
        fail_row " (the normalised recording differs from the Fuse 1.6.0 recording)"
    elif ! { head -c 16 "$mine" >"$TMP_DIR/mine.hdr" && head -c 16 "$fuse_ref" >"$TMP_DIR/fuse.hdr" &&
             cmp -s "$TMP_DIR/mine.hdr" "$TMP_DIR/fuse.hdr"; }; then
        # head -c, not `cmp -n 16`: BSD (macOS) cmp has no -n, so the check read as a header difference there.
        fail_row " (the header differs from Fuse's)"
    elif [[ -e "$TMP_DIR/fx-ay1.psg" || -e "$TMP_DIR/fx-ay2.psg" ]]; then
        fail_row " (a chip file was created for a program that only uses AY#0)"
    elif ! run_psg "$mine_silent" --silent &>"$TMP_DIR/psg2.log"; then
        fail_row " (the --silent run failed)"
    elif ! cmp -s "$mine" "$mine_silent"; then
        fail_row " (--silent changed the recording)"
    else
        bad_err="$TMP_DIR/psg_bad.err"
        bad_rc=0
        run_psg "$TMP_DIR/no-such-dir/x.psg" &>"$bad_err" || bad_rc=$?
        ok=1
        [[ $bad_rc != 0 ]] || ok=0
        grep -q -- '--psg-record' "$bad_err" || ok=0
        grep -q 'no-such-dir/x.psg' "$bad_err" || ok=0
        [[ ! -e "$TMP_DIR/no-such-dir/x.psg" ]] || ok=0
        # /dev/full exists on Linux; macOS has none and Windows has no such device (see audio-log-func).
        if [[ "${JNEXT_TARGET_OS:-linux}" == linux && -w /dev/full ]]; then
            full_err="$TMP_DIR/psg_full.err"
            full_rc=0
            run_psg /dev/full &>"$full_err" || full_rc=$?
            [[ $full_rc == 1 ]] || ok=0
            grep -q 'write error on' "$full_err" || ok=0
        fi
        if [[ $ok == 1 ]]; then
            pass_row " (headless 128K recording equals Fuse 1.6.0's from the sentinel on, with and without --silent; no chip files; an unwritable FILE refused; a write error exits 1)"
        else
            fail_row " (an unwritable FILE was not refused, or a write error did not exit 1 with a message)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
