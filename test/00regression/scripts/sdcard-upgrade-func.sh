#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-upgrade-func: a newer NextZXOS distribution is offered, never forced. The real
# binary, headless (no dialog: the Qt box and the SDL box are pinned by sdcard_dialog_test
# and sdcard_upgrade_box_test), on a private config directory whose default card is this
# run's own card (a distribution of 24.11 by definition: it has no .distro sidecar) and a
# distribution address ($JNEXT_SDCARD_DISTRO_URL, the provisioner's test seam) that names
# sn-emulator-99.99.zip in a local file: libcurl reads file://, so the row never touches
# the network, and the "distribution" is a synthetic FAT32 image made here (a marker file,
# no NextZXOS byte).
#   1. headless, no option: exit 0, the card is byte-identical (this clone has no original image, so the log says its files cannot be carried over and names --sdcard-upgrade fresh), and the log says the newer
#      distribution is available and how to upgrade (--sdcard-upgrade yes);
#   2. --sdcard-upgrade no: the card is untouched and nothing is recorded;
#   3. --sdcard-upgrade never: the card is untouched, <card>.distro-declined names 99.99,
#      and a later run says nothing about it; --sdcard-upgrade ask forgets it and says it again;
#   4. --sdcard-upgrade with --sdcard is refused (exit 1);
#   5. --sdcard-upgrade yes without an original image is refused (exit 1, naming fresh); --sdcard-upgrade fresh: the card is REPLACED by the distribution's own, the old one is in previous/ (the marker file
#      is on it, .distro names 99.99, .recipe is 3, its volume serial is the first 4 bytes of
#      the raw's SHA-256, the declined sidecar is gone) and the old raw/sha are the new ones.
#   6. the candidate-card seam's offline re-derive (01-sdcard-provision.sh with the REAL jnext, which
#      the stub of sdcard-provision-func cannot test): a seam dir holding a recipe-2 card, its witness and
#      the raw of a distribution jnext knows is re-derived at recipe 3 on the first run and kept on the
#      second, and the card is never lost (the raw is accepted only because the address the script
#      passes ends in the raw's own distribution name).
# The scratch directory is under $RUN_DIR/private (the same filesystem as the clone, and not the
# RAM-backed /tmp: it holds ~2 GB); the master is never touched.
if want sdcard-upgrade-func; then
    begin_func sdcard-upgrade-func

    su_dir="$RUN_DIR/private/sdcard-upgrade"
    rm -rf "$su_dir"; mkdir -p "$su_dir/cfg/sdcard"
    su_faults=()
    su_missing=()
    for tool in mformat mcopy mdir sha256sum python3 od; do
        command -v "$tool" &>/dev/null || su_missing+=("$tool")
    done

    if [[ ${#su_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${su_missing[*]})"
    elif [[ ! -f "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" ]]; then
        skip_row " (no SD-card image clone for this run)"
    else
        export MTOOLS_SKIP_CHECK=1
        su_card="$su_dir/cfg/sdcard/cspect-next-1gb-fixed.img"
        su_old="$su_dir/old.img"
        su_zip="$su_dir/sn-emulator-99.99.zip"
        su_off=32256   # partition at sector 63
        su_sectors=1228800
        # The synthetic distribution: an MBR, one FAT32 partition of ~600 MB (enough clusters for
        # the provisioner's re-format) holding MARKER.TXT, zipped under the official image name.
        truncate -s $(( su_off + su_sectors * 512 )) "$su_dir/dist.img"
        printf '\x0c' | dd of="$su_dir/dist.img" bs=1 seek=$((0x1BE + 4)) conv=notrunc status=none
        printf '\x3f\x00\x00\x00' | dd of="$su_dir/dist.img" bs=1 seek=$((0x1BE + 8)) conv=notrunc status=none
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((su_sectors & 255)) $(((su_sectors >> 8) & 255)) $(((su_sectors >> 16) & 255)) $(((su_sectors >> 24) & 255)))" \
            | dd of="$su_dir/dist.img" bs=1 seek=$((0x1BE + 12)) conv=notrunc status=none
        printf '\x55\xaa' | dd of="$su_dir/dist.img" bs=1 seek=510 conv=notrunc status=none
        LANG=C mformat -i "$su_dir/dist.img@@$su_off" -F -T "$su_sectors" -h 255 -s 63 -c 1 :: </dev/null >/dev/null 2>&1 \
            || su_faults+=("harness: mformat could not build the synthetic distribution")
        printf 'synthetic distribution marker\n' > "$su_dir/MARKER.TXT"
        LANG=C mcopy -i "$su_dir/dist.img@@$su_off" "$su_dir/MARKER.TXT" ::/MARKER.TXT </dev/null >/dev/null 2>&1 \
            || su_faults+=("harness: mcopy of the marker failed")
        python3 -I - "$su_dir/dist.img" "$su_zip" <<'PY' || su_faults+=("harness: could not zip the synthetic distribution")
import sys, zipfile
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as z:
    z.write(sys.argv[1], "cspect-next-1gb.img")
PY
        rm -f "$su_dir/dist.img"
        cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" "$su_old"
        su_old_sha=$(sha256sum < "$su_old" | cut -d' ' -f1)
        su_reset() { cp --reflink=auto "$su_old" "$su_card"; rm -f "$su_card".distro "$su_card".distro-declined "$su_card".recipe \
                       "$su_dir/cfg/sdcard/cspect-next-1gb.img" "$su_dir/cfg/sdcard/cspect-next-1gb.img".*; }
        su_card_sha() { sha256sum < "$su_card" | cut -d' ' -f1; }
        # su_run <name> <jnext args...>: a headless run on the private config dir, stdin closed; prints the exit status.
        su_run() {
            local name=$1 rc=0; shift
            LANG=C JNEXT_CONFIG_DIR="$su_dir/cfg" JNEXT_SDCARD_DISTRO_URL="file://$su_zip" \
                timeout --foreground --kill-after=5s 240s "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                "$@" --delayed-automatic-exit-frames 2 </dev/null > "$su_dir/$name.out" 2>&1 || rc=$?
            tr -d '\r' < "$su_dir/$name.out" > "$su_dir/$name.txt"
            echo "$rc"
        }
        line_new="NextZXOS 99.99 is available; this card is NextZXOS 24.11"

        if [[ ${#su_faults[@]} -eq 0 ]]; then
            # 1: no option
            su_reset
            rc=$(su_run one)
            [[ $rc == 0 ]] || su_faults+=("1: the run exited $rc: $(tail -n 2 "$su_dir/one.txt" | tr '\n' ' ')")
            grep -qF "$line_new. Your files cannot be carried over (the original image" "$su_dir/one.txt" && grep -qF "jnext --sdcard-upgrade fresh makes a new card and keeps this one as the backup." "$su_dir/one.txt" \
                || su_faults+=("1: the log does not say how to upgrade")
            [[ "$(su_card_sha)" == "$su_old_sha" ]] || su_faults+=("1: the card changed without a yes")
            [[ ! -e "$su_card.distro-declined" ]] || su_faults+=("1: a headless run recorded a decline")
            # 2: no
            rc=$(su_run two --sdcard-upgrade no)
            grep -qF "$line_new; keeping it this run" "$su_dir/two.txt" || su_faults+=("2: no 'keeping it this run' line")
            [[ "$(su_card_sha)" == "$su_old_sha" && ! -e "$su_card.distro-declined" ]] || su_faults+=("2: 'no' changed or recorded something")
            # 3: never, then silence, then ask
            rc=$(su_run three --sdcard-upgrade never)
            [[ "$(tr -d '\n' < "$su_card.distro-declined" 2>/dev/null)" == "sn-emulator-99.99.zip" ]] \
                || su_faults+=("3: never did not record 99.99 in .distro-declined")
            rc=$(su_run three_b)
            ! grep -qF "is available" "$su_dir/three_b.txt" || su_faults+=("3: a declined distribution is offered again")
            rc=$(su_run three_c --sdcard-upgrade ask)
            [[ ! -e "$su_card.distro-declined" ]] && grep -qF "$line_new" "$su_dir/three_c.txt" \
                || su_faults+=("3: ask did not forget the decline and say it again")
            [[ "$(su_card_sha)" == "$su_old_sha" ]] || su_faults+=("3: the card changed")
            # 4: --sdcard-upgrade with --sdcard
            rc=$(su_run four --sdcard "$su_old" --sdcard-upgrade yes)
            [[ $rc == 1 ]] && grep -qF "cannot be used with --sdcard" "$su_dir/four.txt" \
                || su_faults+=("4: --sdcard-upgrade with --sdcard was not refused (exit $rc)")
            # 5: yes
            # 5a: yes needs the card's original image, which this clone has none of: refused, nothing changed
            rc=$(su_run five_a --sdcard-upgrade yes)
            [[ $rc == 1 ]] && grep -qF "cannot be carried over" "$su_dir/five_a.txt" && grep -qF -- "--sdcard-upgrade fresh" "$su_dir/five_a.txt" \
                || su_faults+=("5a: yes without an original image was not refused with the fresh hint (exit $rc)")
            [[ "$(su_card_sha)" == "$su_old_sha" ]] || su_faults+=("5a: the refused upgrade changed the card")
            # 5b: fresh: a new card, the old one kept in previous/
            printf 'sn-emulator-99.99.zip\n' > "$su_card.distro-declined"
            rc=$(su_run five --sdcard-upgrade fresh)
            [[ "$(sha256sum < "$su_dir/cfg/sdcard/previous/cspect-next-1gb-fixed.img" 2>/dev/null | cut -d' ' -f1)" == "$su_old_sha" ]] \
                || su_faults+=("5: the old card is not in previous/ byte-identical")
            [[ -f "$su_card" && "$(su_card_sha)" != "$su_old_sha" ]] || su_faults+=("5: the card was not replaced")
            LANG=C mcopy -n -i "$su_card@@$su_off" ::/MARKER.TXT "$su_dir/marker.out" </dev/null >/dev/null 2>&1 || true
            cmp -s "$su_dir/marker.out" "$su_dir/MARKER.TXT" || su_faults+=("5: the new card does not hold the distribution's MARKER.TXT")
            [[ "$(tr -d '\n' < "$su_card.distro" 2>/dev/null)" == "sn-emulator-99.99.zip" ]] || su_faults+=("5: .distro is not 99.99")
            [[ "$(tr -d '\n' < "$su_card.recipe" 2>/dev/null)" == "3" ]] || su_faults+=("5: .recipe is not 3")
            [[ ! -e "$su_card.distro-declined" ]] || su_faults+=("5: the declined sidecar survived the replacement")
            raw="$su_dir/cfg/sdcard/cspect-next-1gb.img"
            want_serial=$(sha256sum < "$raw" 2>/dev/null | cut -c1-8)
            got_serial=$(od -An -tx4 -j $((su_off + 0x43)) -N 4 "$su_card" 2>/dev/null | tr -d ' ')
            [[ -n "$want_serial" && "$got_serial" == "$want_serial" ]] \
                || su_faults+=("5: the card's volume serial is '$got_serial', not the raw's SHA-256 prefix '$want_serial'")
            # 6: the seam's offline re-derive, with the real jnext and the real script. The raw and its
            # sidecars are those step 5 just made from the synthetic distribution (99.99).
            seam="$su_dir/seam"; sd6="$su_dir/seam/sdcard"; rm -rf "$seam"; mkdir -p "$sd6"
            if [[ -f "$raw" && -f "$raw.sha256" && -f "$raw.distro" ]]; then
                cp --reflink=auto "$raw" "$sd6/"; cp "$raw.sha256" "$raw.distro" "$sd6/"
                cp --reflink=auto "$su_card" "$sd6/cspect-next-1gb-fixed.img"
                printf '%s  cspect-next-1gb-fixed.img\n' "$(sha256sum < "$sd6/cspect-next-1gb-fixed.img" | cut -d' ' -f1)" > "$sd6/cspect-next-1gb-fixed.img.sha256"
                printf '2\n' > "$sd6/cspect-next-1gb-fixed.img.recipe"
                for run in 1 2; do
                    LANG=C JNEXT_REGRESSION_BOOT_ANCHOR=off JNEXT_REGRESSION_SD_STATE_DIR="$seam" HOME="$su_dir/seamhome" \
                        JNEXT="$JNEXT" bash "$SCRIPT_DIR/scripts/01-sdcard-provision.sh" > "$su_dir/seam$run.out" 2>&1 || true
                    sed 's/\x1b\[[0-9;]*m//g' "$su_dir/seam$run.out" | tr -d '\r' > "$su_dir/seam$run.txt"
                    [[ -f "$sd6/cspect-next-1gb-fixed.img" ]] || su_faults+=("6: run $run: the seam's card was lost")
                    [[ -f "$sd6/cspect-next-1gb-fixed.img.recipe" && "$(tr -d '[:space:]' < "$sd6/cspect-next-1gb-fixed.img.recipe")" == 3 ]] \
                        || su_faults+=("6: run $run: the card is not at recipe 3: $(grep 'sdcard-provision\|PASS\|FAIL' "$su_dir/seam$run.txt" | tail -n 1 | cut -c1-200)")
                done
                grep -q 're-derived with recipe 3' "$su_dir/seam1.txt" || su_faults+=("6: run 1 did not take the recipe path")
                grep -q 'unchanged since it was witnessed' "$su_dir/seam2.txt" || su_faults+=("6: run 2 did not find the card unchanged")
            else
                su_faults+=("6: step 5 left no raw with its sidecars to build the seam from")
            fi
        fi
    fi

    if [[ ${#su_missing[@]} -gt 0 ]]; then :; elif [[ ! -f "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" ]]; then :
    elif [[ ${#su_faults[@]} -eq 0 ]]; then
        pass_row " (a newer distribution is offered and never forced: headless keeps the card and says how to upgrade; no / never / ask behave; --sdcard refuses the option; yes replaces the card with the new distribution's, its own serial, atomically; the seam's offline re-derive keeps its card)"
    else
        fail_row " (${#su_faults[@]} fault(s) in the upgrade offer)"
        printf '      %s\n' "${su_faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
