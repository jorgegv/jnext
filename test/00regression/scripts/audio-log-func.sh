#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable - the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"


# Audio write log (GH #531): an injected Z80N program writes the AY, the DAC and an
# NR mirror; --audio-log streams the decoded writes to a CSV. The fixture's rows are
# found by their PCs (everything that boots before it is not asserted). The program,
# at 0x8000:
#   8000 DI
#   8001 NEXTREG 0x08,0x0A      TurboSound and the DAC on
#   8005 LD BC,0xFFFD
#   8008 LD A,0xFF              800A OUT (C),A   chip select AY0: logged
#   800C LD A,7                 800E OUT (C),A   a plain register select: NOT logged
#   8010 LD B,0xBF
#   8012 LD A,0x38              8014 OUT (C),A   R7 write: logged
#   8016 LD A,0x11              8018 OUT (0x1F),A   (port 0x111F: OUT (n),A puts A on A15..A8)
#   801A LD A,0x22              801C OUT (0x1F),A   (port 0x221F)
#   801E NEXTREG 0x2D,0x33      the DAC mirror: channels A then D
#   8022 JR $
if want audio-log-func; then
    begin_func audio-log-func
    alog_bin="$TMP_DIR/audio_log.bin"
    alog_csv="$TMP_DIR/audio_log.csv"
    printf '\xF3\xED\x91\x08\x0A\x01\xFD\xFF\x3E\xFF\xED\x79\x3E\x07\xED\x79\x06\xBF\x3E\x38\xED\x79\x3E\x11\xD3\x1F\x3E\x22\xD3\x1F\xED\x91\x2D\x33\x18\xFE' > "$alog_bin"
    u8='0x[0-9A-F]{2}'
    num='[0-9]+'
    head_re='^frame,line,hc,tstate,cycle,pc,source,target,chip,reg,value,prev,flags,reason,port$'
    pre="^$num,$num,$num,$num,$num,"
    ok=1
    timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --inject "$alog_bin" --inject-org 8000 \
        --inject-pc 8000 --audio-log "$alog_csv" \
        --delayed-automatic-exit-frames 30 &>/dev/null || ok=0
    [[ $ok == 1 && $(head -n 1 "$alog_csv" 2>/dev/null) =~ $head_re ]] || ok=0
    sel_re="${pre}0x800A,cpu,ay_select,0,0,0xFF,$u8,0,none,0xFFFD\$"
    r7_re="${pre}0x8014,cpu,ay,0,7,0x38,$u8,0,none,0xBFFD\$"
    dac1_re="${pre}0x8018,cpu,dac,0,0,0x11,$u8,0,none,0x111F\$"
    dac2_re="${pre}0x801C,cpu,dac,0,0,0x22,0x11,0,none,0x221F\$"
    mir0_re="${pre}0x801E,cpu,dac_mirror,0,45,0x33,0x22,0,none,0x002D\$"
    mir3_re="${pre}0x801E,cpu,dac_mirror,3,45,0x33,$u8,0,none,0x002D\$"
    if [[ $ok == 1 ]]; then
        for re in "$sel_re" "$r7_re" "$dac1_re" "$dac2_re" "$mir0_re" "$mir3_re"; do
            [[ $(grep -cE "$re" "$alog_csv") == 1 ]] || ok=0
        done
        # The plain select's PC, 0x800E, appears in no row.
        ! grep -q ',0x800E,' "$alog_csv" || ok=0
        # The fixture's rows come in program order.
        order=""
        for re in "$sel_re" "$r7_re" "$dac1_re" "$dac2_re" "$mir0_re" "$mir3_re"; do
            order+="$(grep -nE "$re" "$alog_csv" | cut -d: -f1 || true) "
        done
        sorted="$(tr ' ' '\n' <<<"$order" | sed '/^$/d' | sort -n | tr '\n' ' ')"
        [[ "$order" == "$sorted" ]] || ok=0
    fi
    # An unwritable FILE is refused before the machine starts.
    bad_err="$TMP_DIR/audio_log_bad.err"
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --inject "$alog_bin" --inject-org 8000 \
        --inject-pc 8000 --audio-log "$TMP_DIR/no-such-dir/x.csv" \
        --delayed-automatic-exit-frames 30 &>"$bad_err"; then
        ok=0
    fi
    grep -q -- '--audio-log' "$bad_err" || ok=0
    # An early exit after the stream attached (a --load with an unsupported extension) tears the stream
    # down while the Debugger is alive: a clean exit 1 with the error, not a crash (glibc fills freed
    # memory with a pattern under MALLOC_PERTURB_, so a stale Debugger pointer faults at once).
    early_err="$TMP_DIR/audio_log_early.err"
    early_rc=0
    env MALLOC_PERTURB_=165 timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --audio-log "$TMP_DIR/audio_log_early.csv" --load "$TMP_DIR/not-a-program.xyz" \
        &>"$early_err" || early_rc=$?
    [[ $early_rc == 1 ]] || ok=0
    grep -q 'unsupported file extension' "$early_err" || ok=0
    # The guard detaches the stream's client while the Debugger is alive, so the backend logs the detach on
    # this path; with the guard not armed that call hits a freed Debugger, the line never appears and the
    # crash handler's exit 1 hides it (a "signal 11" line is the other sign).
    grep -q 'DETACH client' "$early_err" || ok=0
    ! grep -q 'signal 11' "$early_err" || ok=0
    # A write error makes the run exit non-zero and say so. /dev/full exists on Linux; macOS has none, and
    # Windows has no such device (under wine the host's /dev/full opens but takes the write: measured,
    # exit 0 and no error), so the one check has nothing to fail on those and runs on Linux only.
    if [[ "${JNEXT_TARGET_OS:-linux}" == linux && -w /dev/full ]]; then
        full_err="$TMP_DIR/audio_log_full.err"
        full_rc=0
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --inject "$alog_bin" --inject-org 8000 --inject-pc 8000 \
            --audio-log /dev/full --delayed-automatic-exit-frames 30 &>"$full_err" || full_rc=$?
        [[ $full_rc == 1 ]] || ok=0
        grep -q 'write error on' "$full_err" || ok=0
    fi
    if [[ $ok == 1 ]]; then
        pass_row " (decoded AY, DAC and NR mirror writes streamed to CSV; plain select not logged; an unwritable FILE refused; an early exit after attach is clean; a write error exits 1)"
    else
        fail_row " (missing or incorrect audio log rows, or the unwritable FILE was not refused)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
