#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-writes-func (GH #470): --sdcard-writes keeps the guest's writes in a
# side file and never writes the image. The guest is test/fixtures/nex/
# save_counter.nex (source: demo/save_counter): it keeps a one-byte run counter
# in /JNSAVE.DAT on the card and prints "SAVE <n> OK" on the magic port.
#
# Two private minimal NextZXOS cards (tools/make-minimal-sdcard-image.sh, built
# here, deleted at the end, never kept): R takes the writes itself (the
# reference), B is the pristine card the journal is used on.
#   (a) refusals, each exit 1 with its message: --sdcard-writes-reset alone;
#       --sdcard-writes with --sdcard-readonly; with --sdcard-file-add; a FILE
#       that is not a journal (left untouched); a journal made for another
#       digest (built here from the format table), naming --sdcard-writes-reset;
#   (b) R, two runs: SAVE 1 OK, SAVE 2 OK, and mtools reads JNSAVE.DAT = 2;
#   (c) B + a journal, two runs: SAVE 1 OK, SAVE 2 OK; B's SHA-256 is the same
#       after both runs; the second run records the warm start again (the key
#       follows the journal);
#   (d) the FOREIGN reader: test/00regression/sdcard-writes-apply.py, written
#       from the format table, lays the journal over a copy of B; the copy is
#       byte-identical to R, and mtools reads JNSAVE.DAT = 2 from it;
#   (e) --sdcard-writes-reset with the journal: SAVE 1 OK, and the journal
#       afterwards equals the one the first run of (c) left.
# The cards are 40 MiB; mtools reads them at the partition offset 32256.
if want sdcard-writes-func; then
    begin_func sdcard-writes-func

    sw_faults=()
    sw_missing=()
    for tool in mformat mcopy mtype mdir minfo mmd sfdisk truncate sha256sum realpath python3 cmp od; do
        command -v "$tool" &>/dev/null || sw_missing+=("$tool")
    done

    if [[ ${#sw_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${sw_missing[*]} - install mtools and util-linux)"
    elif [[ ! -f "$SD_MASTER_DIR/cspect-next-1gb.img" ]]; then
        fail_row " (the distribution image $SD_MASTER_DIR/cspect-next-1gb.img is absent)"
    else
        sw_dir="$RUN_DIR/private/sdcard-writes"
        sw_r="$sw_dir/r.img"          # takes the writes itself
        sw_b="$sw_dir/b.img"          # pristine; the journal is used on it
        sw_j="$sw_dir/w.jsdw"
        sw_nex="test/fixtures/nex/save_counter.nex"
        sw_hex=$(printf 'a%.0s' $(seq 1 64))
        sw_off=32256
        rm -rf "$sw_dir"; mkdir -p "$sw_dir"

        sw_run() {      # sw_run <card> [extra jnext args]: the guest's magic-port lines
            local card=$1; shift
            timeout --foreground --kill-after=5s 180s "$JNEXT" --headless --machine next \
                --sdcard "$card" --rtc "$NEXTZXOS_RTC" \
                --magic-port 0xCAFE --magic-port-mode line \
                --load "$sw_nex" --delayed-automatic-exit-frames 400 "$@" 2>&1 || true
        }
        sw_save() { grep -a '^SAVE ' <<<"$1" | head -1 || true; }
        sw_file_byte() {   # the first byte of /JNSAVE.DAT on a card, in hex
            mtype -i "$1@@$sw_off" ::JNSAVE.DAT 2>/dev/null | od -An -tx1 -N1 | tr -d ' \n' || true
        }

        sw_rc=0
        bash "$PROJECT_DIR/tools/make-minimal-sdcard-image.sh" \
            "$SD_MASTER_DIR/cspect-next-1gb.img" "$sw_r" >"$sw_dir/build.out" 2>"$sw_dir/build.err" \
            || sw_rc=$?
        [[ "$sw_rc" == 0 ]] || sw_faults+=("build: the script exited $sw_rc")

        if [[ "$sw_rc" == 0 ]]; then
            cp --reflink=auto "$sw_r" "$sw_b"
            sw_b_sha=$(sha256sum "$sw_b" | cut -d' ' -f1)

            # (a) refusals
            sw_refuse() {   # sw_refuse <label> <message> <jnext args...>
                local label=$1 msg=$2 rc=0 out; shift 2
                out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
                    --sdcard "$sw_b" "$@" 2>&1) || rc=$?
                [[ "$rc" == 1 ]] || sw_faults+=("a($label): exited $rc, not 1")
                grep -qF -- "$msg" <<<"$out" || sw_faults+=("a($label): no '$msg' message")
            }
            sw_refuse reset-alone "--sdcard-writes-reset needs --sdcard-writes" --sdcard-writes-reset
            sw_refuse readonly "cannot be combined with --sdcard-readonly" \
                --sdcard-writes "$sw_dir/x.jsdw" --sdcard-readonly
            sw_refuse file-add "cannot be combined with --sdcard-writes" \
                --sdcard-writes "$sw_dir/x.jsdw" --sdcard-file-add "$sw_nex"
            printf 'this is not a journal\n' >"$sw_dir/notj.txt"
            sw_notj_sha=$(sha256sum "$sw_dir/notj.txt" | cut -d' ' -f1)
            sw_refuse not-a-journal "is not a jnext SD-writes file" --sdcard-writes "$sw_dir/notj.txt"
            sw_refuse not-a-journal-reset "is not a jnext SD-writes file" \
                --sdcard-writes "$sw_dir/notj.txt" --sdcard-writes-reset
            [[ "$(sha256sum "$sw_dir/notj.txt" | cut -d' ' -f1)" == "$sw_notj_sha" ]] \
                || sw_faults+=("a: the non-journal file was changed")
            # A journal for another digest, built from the format table alone.
            python3 -I -c '
import struct, sys
size = int(sys.argv[2])
hdr = b"JNEXTSDW" + struct.pack("<IIQQ", 1, 512, size, 0) + bytes.fromhex("aa" * 32)
open(sys.argv[1], "wb").write(hdr + struct.pack("<I", 7) + bytes(512))' \
                "$sw_dir/other.jsdw" "$(stat -c %s "$sw_b")"
            sw_other_sha=$(sha256sum "$sw_dir/other.jsdw" | cut -d' ' -f1)
            sw_refuse other-digest "--sdcard-writes-reset" \
                --sdcard-sha256 "$(printf 'b%.0s' $(seq 1 64))" --sdcard-writes "$sw_dir/other.jsdw"
            [[ "$(sha256sum "$sw_dir/other.jsdw" | cut -d' ' -f1)" == "$sw_other_sha" ]] \
                || sw_faults+=("a: the journal of another image was changed")

            # (b) the read-write reference
            rm -rf "$JNEXT_CONFIG_DIR/warm-start"
            sw_b1=$(sw_run "$sw_r")
            sw_b2=$(sw_run "$sw_r")
            [[ "$(sw_save "$sw_b1")" == "SAVE 1 OK" ]] || sw_faults+=("b: run 1 said '$(sw_save "$sw_b1")'")
            [[ "$(sw_save "$sw_b2")" == "SAVE 2 OK" ]] || sw_faults+=("b: run 2 said '$(sw_save "$sw_b2")'")
            [[ "$(sw_file_byte "$sw_r")" == "02" ]] || sw_faults+=("b: mtools reads JNSAVE.DAT = '$(sw_file_byte "$sw_r")', not 02")

            # (c) the journal
            rm -rf "$JNEXT_CONFIG_DIR/warm-start"
            sw_c1=$(sw_run "$sw_b" --sdcard-writes "$sw_j")
            cp "$sw_j" "$sw_dir/after1.jsdw" 2>/dev/null || sw_faults+=("c: run 1 made no journal")
            sw_c2=$(sw_run "$sw_b" --sdcard-writes "$sw_j")
            [[ "$(sw_save "$sw_c1")" == "SAVE 1 OK" ]] || sw_faults+=("c: run 1 said '$(sw_save "$sw_c1")'")
            [[ "$(sw_save "$sw_c2")" == "SAVE 2 OK" ]] || sw_faults+=("c: run 2 said '$(sw_save "$sw_c2")'")
            [[ "$(sha256sum "$sw_b" | cut -d' ' -f1)" == "$sw_b_sha" ]] \
                || sw_faults+=("c: the image changed (the journal must keep it pristine)")
            grep -q "recorded a NextZXOS-resident machine" <<<"$sw_c2" \
                || sw_faults+=("c: run 2 did not record the warm start again (the key must follow the journal)")

            # (d) the foreign reader
            cp --reflink=auto "$sw_b" "$sw_dir/applied.img"
            if python3 -I "$PROJECT_DIR/test/00regression/sdcard-writes-apply.py" "$sw_dir/applied.img" "$sw_j"; then
                cmp -s "$sw_dir/applied.img" "$sw_r" \
                    || sw_faults+=("d: the journal laid over the pristine card differs from the read-write card")
                [[ "$(sw_file_byte "$sw_dir/applied.img")" == "02" ]] \
                    || sw_faults+=("d: mtools reads JNSAVE.DAT = '$(sw_file_byte "$sw_dir/applied.img")' from the applied card")
            else
                sw_faults+=("d: the foreign reader refused the journal")
            fi

            # (e) reset
            sw_e=$(sw_run "$sw_b" --sdcard-writes "$sw_j" --sdcard-writes-reset)
            [[ "$(sw_save "$sw_e")" == "SAVE 1 OK" ]] || sw_faults+=("e: the reset run said '$(sw_save "$sw_e")'")
            cmp -s "$sw_j" "$sw_dir/after1.jsdw" \
                || sw_faults+=("e: after the reset the journal is not the one run 1 left")
        fi

        if [[ ${#sw_faults[@]} -eq 0 ]]; then
            pass_row " (refusals; a pristine card plus its journal equals the read-write card, kept pristine, reset by the flag)"
        else
            fail_row " ($(IFS=';'; echo "${sw_faults[*]}"))"
            show_output "b run 2" "${sw_b2:-}" 'SAVE|warm start|\[error\]|\[warning\]'
            show_output "c run 1" "${sw_c1:-}" 'SAVE|warm start|\[error\]|\[warning\]'
            show_output "c run 2" "${sw_c2:-}" 'SAVE|warm start|\[error\]|\[warning\]'
        fi
        rm -rf "$sw_dir"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
