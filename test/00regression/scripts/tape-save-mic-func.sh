#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# tape-save-mic-func: --tape-save to TZX and WAV captures every way a program
# saves, and the tapes load back (GH #89).
#
# THE FIXTURE: bin/tape_save_demo.bin (source and build:
# demo/tape_save_demo/tape_save_demo.asm), a 48K program that saves, with a
# second's pause after each block:
#   1-2  a BASIC loader and its CODE through the ROM's SA-BYTES (the trap),
#   3    CODE through a RAM copy of SA-BYTES: the ROM's own pulses, which only
#        the MIC capture sees,
#   4    CODE through a RAM copy with other timings (a turbo saver),
#   5    24 MIC edges with no pilot,
# then prints DONE on the magic port $BEEF. Loading the tape runs the BASIC
# loader, which loads a verifier, which loads blocks 3 and 4 through the ROM's
# LD-BYTES and checks every byte: it prints OK, or F and the stage.
#
# THE MEASUREMENT:
#   tzx    the TZX's block IDs are exactly 10 x6 (the four trapped blocks and
#          the ROM-timed MIC pair, recognised as the ROM's), 11 x2 (turbo),
#          13 (the pulses) and 20 (their pause);
#   wav    the WAV header is PCM mono 44100 Hz 8-bit and its data size is the
#          file's;
#   load   each tape loads in real time on a 48K and the verifier prints OK;
#   control a copy of the TZX with one turbo payload byte flipped prints F, so
#          OK is not what the verifier prints whatever it loads.
# The same TZX and WAV load in FUSE 1.6.0 with its tape traps off (the GH #89
# report); this row cannot assume FUSE on the host.
if want tape-save-mic-func; then
    begin_func tape-save-mic-func

    ts_dir="$TMP_DIR/tape-save-mic"
    rm -rf "$ts_dir"; mkdir -p "$ts_dir"
    ts_bin="$SCRIPT_DIR/bin/tape_save_demo.bin"
    ts_faults=()

    # ts_run <tag> <jnext args...>: a headless 48K run; prints its exit status.
    ts_run() {
        local tag=$1 rc=0; shift
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --magic-port 0xBEEF --magic-port-mode line "$@" \
            >"$ts_dir/$tag.log" 2>&1 || rc=$?
        echo "$rc"
    }
    # ts_said <tag> <line>: the run's magic-port output holds <line> on its own.
    ts_said() { grep -q -x -- "$2" "$ts_dir/$1.log"; }

    for fmt in tzx wav; do
        rc=$(ts_run "save-$fmt" --inject "$ts_bin" --tape-save "$ts_dir/out.$fmt" \
                 --delayed-automatic-exit-frames 1500)
        { [[ "$rc" == 0 ]] && ts_said "save-$fmt" DONE; } \
            || ts_faults+=("save $fmt: rc=$rc or no DONE")
    done

    ids=$(perl -e '
        local $/; open my $f, "<:raw", $ARGV[0] or exit 1; my $d = <$f>;
        substr($d, 0, 10) eq "ZXTape!\x1A\x01\x14" or exit 1;
        my ($i, @ids) = (10);
        while ($i < length $d) {
            my $id = ord substr($d, $i, 1); push @ids, sprintf "%02x", $id;
            if    ($id == 0x10) { $i += 5 + unpack "v", substr($d, $i + 3, 2) }
            elsif ($id == 0x11) { $i += 19 + (unpack("V", substr($d, $i + 16, 3) . "\0")) }
            elsif ($id == 0x13) { $i += 2 + 2 * ord substr($d, $i + 1, 1) }
            elsif ($id == 0x20) { $i += 3 }
            else  { last }
        }
        print "@ids";' "$ts_dir/out.tzx" 2>/dev/null || true)
    [[ "$ids" == "10 10 10 10 10 10 11 11 13 20" ]] \
        || ts_faults+=("tzx blocks: '$ids', expected '10 10 10 10 10 10 11 11 13 20'")

    wav_ok=$(perl -e '
        open my $f, "<:raw", $ARGV[0] or exit 1; read $f, my $h, 44;
        my @v = unpack "a4 V a4 a4 V v v V V v v a4 V", $h;
        my $size = -s $ARGV[0];
        print( ($v[0] eq "RIFF" && $v[1] == $size - 8 && $v[2] eq "WAVE" && $v[3] eq "fmt "
                && $v[4] == 16 && $v[5] == 1 && $v[6] == 1 && $v[7] == 44100 && $v[8] == 44100
                && $v[9] == 1 && $v[10] == 8 && $v[11] eq "data" && $v[12] == $size - 44
                && $v[12] > 0) ? "yes" : "no");' "$ts_dir/out.wav" 2>/dev/null || true)
    [[ "$wav_ok" == yes ]] || ts_faults+=("wav header: not PCM mono 44100 Hz 8-bit with matching sizes")

    perl -e '
        local $/; open my $f, "<:raw", $ARGV[0] or exit 1; my $d = <$f>;
        my $at = rindex $d, "\x11\x68\x07";     # the turbo data block (pilot 1896 T)
        exit 1 if $at < 0;
        substr($d, $at + 19 + 50, 1) ^= "\x01";
        open my $o, ">:raw", $ARGV[1] or exit 1; print $o $d;' \
        "$ts_dir/out.tzx" "$ts_dir/bad.tzx" 2>/dev/null \
        || ts_faults+=("control: could not build the corrupted copy")

    for tape in out.tzx out.wav bad.tzx; do
        rc=$(ts_run "load-$tape" --load "$ts_dir/$tape" --tape-realtime \
                 --delayed-automatic-exit-frames 3500)
        if [[ "$tape" == bad.tzx ]]; then
            ts_said "load-$tape" F4 || ts_faults+=("control: the corrupted tape did not print F4")
        elif ! { [[ "$rc" == 0 ]] && ts_said "load-$tape" OK; }; then
            said=$(grep -x -E 'OK|F[0-9]' "$ts_dir/load-$tape.log" || true)
            ts_faults+=("load $tape: rc=$rc, verifier said '${said%%$'\n'*}'")
        fi
    done

    if [[ ${#ts_faults[@]} -eq 0 ]]; then
        pass_row " (TZX: 10 x6, 11 x2, 13, 20; WAV header ok; both load back OK; corrupted copy F4)"
    else
        fail_row " ($(IFS=';'; echo "${ts_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
