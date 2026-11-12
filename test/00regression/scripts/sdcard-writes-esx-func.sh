#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-writes-esx-func (GH #470): the file jnext's esxDOS stand-in keeps for a
# directly loaded NEX goes into the --sdcard-writes file, so it survives the run.
# The guest is test/fixtures/nex/esx_save_counter.nex (source: demo/esx_save_counter):
# a run counter in /JNHI.DAT through esxDOS, printed as "ESX <n> OK" on the magic
# port. esx_save_counter_b.nex is the same program built with another constant byte:
# a changed NEX. The stand-in keeps one file, keyed by the card and the name, never
# by the program (as on hardware, where a changed game reads its old high scores).
#
# One private minimal NextZXOS card (tools/make-minimal-sdcard-image.sh, built here,
# deleted at the end, never kept): B is the pristine card the journal J is used on.
#   (a) B + J, two runs: ESX 1 OK, ESX 2 OK; B's SHA-256 is the same after both;
#       the second run RESTORES the warm start (a stand-in save is not card state, so
#       it does not change the warm-start key);
#   (b) the FOREIGN reader test/00regression/sdcard-writes-files.py, written from the
#       format table: J holds no sector record and one kept-file record, /JNHI.DAT = 02;
#   (b2) sdcard-writes-apply.py skips the kept-file record: a copy of B with J laid over
#       it is byte-identical to B;
#   (c) no --sdcard-writes: ESX 1 OK twice (the file is session-only, as before);
#   (d) the changed NEX with the same J reads the old save (ESX 3 OK), then the
#       original reads the changed NEX's (ESX 4 OK);
#   (e) --sdcard-writes-reset: ESX 1 OK, and J is the file the first run of (a) left.
if want sdcard-writes-esx-func; then
    begin_func sdcard-writes-esx-func

    swe_faults=()
    swe_missing=()
    for tool in mformat mcopy mmd sfdisk truncate sha256sum python3 cmp; do
        command -v "$tool" &>/dev/null || swe_missing+=("$tool")
    done

    if [[ ${#swe_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${swe_missing[*]} - install mtools and util-linux)"
    elif [[ ! -f "$SD_MASTER_DIR/cspect-next-1gb.img" ]]; then
        fail_row " (the distribution image $SD_MASTER_DIR/cspect-next-1gb.img is absent)"
    else
        swe_dir="$RUN_DIR/private/sdcard-writes-esx"
        swe_b="$swe_dir/b.img"
        swe_j="$swe_dir/w.jsdw"
        swe_nex="test/fixtures/nex/esx_save_counter.nex"
        swe_nex_b="test/fixtures/nex/esx_save_counter_b.nex"
        rm -rf "$swe_dir"; mkdir -p "$swe_dir"

        swe_run() {     # swe_run <nex> [extra jnext args]: the guest's output and the log
            local nex=$1; shift
            timeout --foreground --kill-after=5s 180s "$JNEXT" --headless --machine next \
                --sdcard "$swe_b" --rtc "$NEXTZXOS_RTC" \
                --magic-port 0xCAFE --magic-port-mode line \
                --load "$nex" --delayed-automatic-exit-frames 400 "$@" 2>&1 || true
        }
        swe_say() { grep -a '^ESX ' <<<"$1" | head -1 || true; }

        swe_rc=0
        bash "$PROJECT_DIR/tools/make-minimal-sdcard-image.sh" \
            "$SD_MASTER_DIR/cspect-next-1gb.img" "$swe_b" >"$swe_dir/build.out" 2>"$swe_dir/build.err" \
            || swe_rc=$?
        [[ "$swe_rc" == 0 ]] || swe_faults+=("build: the script exited $swe_rc")

        if [[ "$swe_rc" == 0 ]]; then
            swe_b_sha=$(sha256sum "$swe_b" | cut -d' ' -f1)

            # (a)
            rm -rf "$JNEXT_CONFIG_DIR/warm-start"
            swe_a1=$(swe_run "$swe_nex" --sdcard-writes "$swe_j")
            cp "$swe_j" "$swe_dir/after1.jsdw" 2>/dev/null || swe_faults+=("a: run 1 made no journal")
            swe_a2=$(swe_run "$swe_nex" --sdcard-writes "$swe_j")
            [[ "$(swe_say "$swe_a1")" == "ESX 1 OK" ]] || swe_faults+=("a: run 1 said '$(swe_say "$swe_a1")'")
            [[ "$(swe_say "$swe_a2")" == "ESX 2 OK" ]] || swe_faults+=("a: run 2 said '$(swe_say "$swe_a2")'")
            [[ "$(sha256sum "$swe_b" | cut -d' ' -f1)" == "$swe_b_sha" ]] \
                || swe_faults+=("a: the image changed")
            grep -q "restored a recorded NextZXOS machine" <<<"$swe_a2" \
                || swe_faults+=("a: run 2 did not restore the warm start")
            grep -q "recorded a NextZXOS-resident machine" <<<"$swe_a2" \
                && swe_faults+=("a: run 2 recorded the warm start again (a stand-in save must not change the key)")

            # (b) the foreign reader
            swe_files=$(python3 -I "$PROJECT_DIR/test/00regression/sdcard-writes-files.py" "$swe_j" 2>&1) \
                || swe_faults+=("b: the foreign reader refused the journal: $swe_files")
            [[ "$swe_files" == $'sectors=0\nfile /JNHI.DAT 02' ]] \
                || swe_faults+=("b: the journal reads as '${swe_files//$'\n'/ | }', not 'sectors=0 | file /JNHI.DAT 02'")

            # (b2) the sector reader skips the kept file
            cp --reflink=auto "$swe_b" "$swe_dir/applied.img"
            if python3 -I "$PROJECT_DIR/test/00regression/sdcard-writes-apply.py" "$swe_dir/applied.img" "$swe_j"; then
                cmp -s "$swe_dir/applied.img" "$swe_b" \
                    || swe_faults+=("b2: laying a file-only journal over the card changed it")
            else
                swe_faults+=("b2: sdcard-writes-apply.py refused the journal")
            fi

            # (c) no journal: session-only
            swe_c1=$(swe_run "$swe_nex")
            swe_c2=$(swe_run "$swe_nex")
            [[ "$(swe_say "$swe_c1")" == "ESX 1 OK" && "$(swe_say "$swe_c2")" == "ESX 1 OK" ]] \
                || swe_faults+=("c: without --sdcard-writes the runs said '$(swe_say "$swe_c1")' and '$(swe_say "$swe_c2")', not ESX 1 OK twice")

            # (d) a changed NEX reads the old save
            swe_d1=$(swe_run "$swe_nex_b" --sdcard-writes "$swe_j")
            swe_d2=$(swe_run "$swe_nex" --sdcard-writes "$swe_j")
            [[ "$(swe_say "$swe_d1")" == "ESX 3 OK" ]] || swe_faults+=("d: the changed NEX said '$(swe_say "$swe_d1")', not ESX 3 OK")
            [[ "$(swe_say "$swe_d2")" == "ESX 4 OK" ]] || swe_faults+=("d: the original said '$(swe_say "$swe_d2")', not ESX 4 OK")
            swe_files=$(python3 -I "$PROJECT_DIR/test/00regression/sdcard-writes-files.py" "$swe_j" 2>&1 || true)
            [[ "$swe_files" == $'sectors=0\nfile /JNHI.DAT 04' ]] \
                || swe_faults+=("d: the journal reads as '${swe_files//$'\n'/ | }', not 'sectors=0 | file /JNHI.DAT 04'")

            # (e) reset
            swe_e=$(swe_run "$swe_nex" --sdcard-writes "$swe_j" --sdcard-writes-reset)
            [[ "$(swe_say "$swe_e")" == "ESX 1 OK" ]] || swe_faults+=("e: the reset run said '$(swe_say "$swe_e")'")
            cmp -s "$swe_j" "$swe_dir/after1.jsdw" \
                || swe_faults+=("e: after the reset the journal is not the one run 1 left")
        fi

        if [[ ${#swe_faults[@]} -eq 0 ]]; then
            pass_row " (the stand-in's file kept in the journal across runs, per card and name, reset by the flag; the card pristine)"
        else
            fail_row " ($(IFS=';'; echo "${swe_faults[*]}"))"
            show_output "a run 2" "${swe_a2:-}" 'ESX|warm start|\[error\]|\[warning\]'
            show_output "d run 1" "${swe_d1:-}" 'ESX|warm start|\[error\]|\[warning\]'
        fi
        rm -rf "$swe_dir"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
