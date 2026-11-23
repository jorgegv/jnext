#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-upgrade-real-func (GH #555, tier 3 of the plan): an upgrade with REAL content. Real: the NextZXOS
# files (the suites' own master raw, provisioned by jnext under a file:// address with its real distribution
# name, recipe 3), a guest file written by NextZXOS itself (the kept write-probe NEX, run from the NextZXOS
# command line, creates and removes files through NextZXOS's own esxDOS onto the card: a foreign writer for
# our reader), and the boot. Synthetic: only the second "release", made at run time from that raw by
# distro_variant (one file changed, one added, one removed) and zipped as sn-emulator-99.99.zip.
#   1. jnext provisions the old card from the real raw's zip;
#   2. the guest writes onto it (whatever the probe leaves is the guest's delta, listed with mdir);
#   3. --sdcard-upgrade yes with the 99.99 zip: exit 0; the guest's delta is on the new card byte for byte
#      (mtype), the changed file is the variant's, the added file is there, the removed one is gone, the old
#      card is in previous/ with the guest's delta, and nothing the user did not touch differs;
#   4. the new card boots to the same machine-type commit frame as the old.
# Everything lives under $RUN_DIR/private (a real image and three copies of it: ~4 GB of sparse files) and
# goes with the run; nothing is committed or uploaded (the SD-image rule).
if want sdcard-upgrade-real-func; then
    begin_func sdcard-upgrade-real-func

    ur_dir="$RUN_DIR/private/sdcard-upgrade-real"
    rm -rf "$ur_dir"; mkdir -p "$ur_dir/cfg/sdcard"
    ur_faults=()
    ur_missing=()
    for tool in mdir mtype python3 sha256sum; do
        command -v "$tool" &>/dev/null || ur_missing+=("$tool")
    done
    ur_variant="$PROJECT_DIR/build/test/distro_variant"
    sd_master_paths
    ur_raw_src="$SD_MASTER_DIR/cspect-next-1gb.img"
    ur_card="$ur_dir/cfg/sdcard/cspect-next-1gb-fixed.img"
    ur_prev="$ur_dir/cfg/sdcard/previous/cspect-next-1gb-fixed.img"

    if [[ ${#ur_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${ur_missing[*]})"
    elif [[ ! -x "$ur_variant" ]]; then
        fail_row " (build/test/distro_variant is not built: run 'make unit-test-build')"
    elif [[ ! -f "$ur_raw_src" ]]; then
        skip_row " (the suites' master raw image is not there: $ur_raw_src)"
    else
        export MTOOLS_SKIP_CHECK=1
        ur_off=32256
        ur_dist=$(head -n1 "$ur_raw_src.distro" 2>/dev/null | tr -d '[:space:]')
        [[ -n "$ur_dist" ]] || ur_dist="sn-emulator-24.11.zip"
        ur_zip() {   # ur_zip <image> <zip>: the image as cspect-next-1gb.img in a deflated zip
            python3 -I - "$1" "$2" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED, compresslevel=1) as z:
    z.write(sys.argv[1], "cspect-next-1gb.img")
PY
        }
        ur_run() {   # ur_run <name> <zip name> <jnext args...>: headless run on the private config dir; prints the exit status
            local name=$1 zip=$2 rc=0; shift 2
            LANG=C JNEXT_CONFIG_DIR="$ur_dir/cfg" JNEXT_SDCARD_DISTRO_URL="file://$ur_dir/$zip" \
                timeout --foreground --kill-after=5s 600s "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                "$@" </dev/null > "$ur_dir/$name.out" 2>&1 || rc=$?
            tr -d '\r' < "$ur_dir/$name.out" > "$ur_dir/$name.txt"
            echo "$rc"
        }
        ur_ls() { LANG=C mdir -/ -b -i "$1@@$ur_off" :: 2>/dev/null | sort || true; }

        ur_zip "$ur_raw_src" "$ur_dir/$ur_dist" || ur_faults+=("harness: could not zip the real raw")
        "$ur_variant" "$ur_raw_src" "$ur_dir/variant.img" > "$ur_dir/variant.txt" 2>&1 || ur_faults+=("harness: distro_variant failed: $(tail -n 2 "$ur_dir/variant.txt" | tr '\n' ' ')")
        [[ ${#ur_faults[@]} -ne 0 ]] || ur_zip "$ur_dir/variant.img" "$ur_dir/sn-emulator-99.99.zip" || ur_faults+=("harness: could not zip the variant")
        rm -f "$ur_dir/variant.img"

        if [[ ${#ur_faults[@]} -eq 0 ]]; then
            rc=$(ur_run install "$ur_dist" --sdcard-download-confirm --delayed-automatic-exit-frames 2)
            [[ $rc == 0 && -f "$ur_card" ]] || ur_faults+=("1: the real card was not provisioned (exit $rc): $(tail -n 2 "$ur_dir/install.txt" | tr '\n' ' ')")
        fi
        if [[ ${#ur_faults[@]} -eq 0 ]]; then
            # 2: the guest writes. The probe NEX goes onto the card, NextZXOS is driven to its command line and
            # runs `.nexload w457rw.nex` (esxdos-write-probe-measure.sh's route).
            python3 -I "$SCRIPT_DIR/esxdos-write-probe.py" nex --mode rw "$ur_dir/W457RW.NEX"
            rc=$(ur_run addnex "$ur_dist" --sdcard-file-add "$ur_dir/W457RW.NEX" --sdcard-file-dest /W457RW.NEX)
            [[ $rc == 0 ]] || ur_faults+=("2: the probe NEX could not be added to the card (exit $rc)")
            ur_before=$(ur_ls "$ur_card")
            ur_shift=$(boot_shift)
            ur_keys=(--delayed-keypress-frames $((577 + ur_shift)) space
                     --delayed-keypress-frames $((647 + ur_shift)) down
                     --delayed-keypress-frames $((677 + ur_shift)) enter)
            ur_f=$((737 + ur_shift)); ur_cmd=".nexload w457rw.nex"
            for ((ur_i = 0; ur_i < ${#ur_cmd}; ur_i++)); do
                ur_c=${ur_cmd:ur_i:1}; [[ $ur_c == " " ]] && ur_c=space
                ur_keys+=(--delayed-keypress-frames "$ur_f" "$ur_c"); ur_f=$((ur_f + 15))
            done
            ur_keys+=(--delayed-keypress-frames "$ur_f" enter)
            rc=$(ur_run guest "$ur_dist" --magic-port 0xCAFE --magic-port-mode line "${ur_keys[@]}" --delayed-automatic-exit-frames $((6177 + ur_shift)))
            ur_after=$(ur_ls "$ur_card")
            ur_delta=$(comm -13 <(echo "$ur_before") <(echo "$ur_after") || true)
            grep -qE '^END' "$ur_dir/guest.txt" || ur_faults+=("2: the guest probe did not run to its END line (exit $rc)")
            [[ -n "$ur_delta" ]] || ur_faults+=("2: the guest left nothing new on the card, so the row would prove nothing")
            old_sha=$(sha256sum < "$ur_card" | cut -d' ' -f1)
        fi
        if [[ ${#ur_faults[@]} -eq 0 ]]; then
            rc=$(ur_run up sn-emulator-99.99.zip --sdcard-upgrade yes --delayed-automatic-exit-frames 2)
            [[ $rc == 0 ]] || ur_faults+=("3: the upgrade run exited $rc: $(tail -n 3 "$ur_dir/up.txt" | tr '\n' ' ')")
            [[ "$(tr -d '\n' < "$ur_card.distro" 2>/dev/null)" == "sn-emulator-99.99.zip" ]] || ur_faults+=("3: .distro does not name 99.99: $(grep -iE '^warning|^error|carrying' "$ur_dir/up.txt" | tail -n 4 | cut -c1-700 | tr '\n' '|')")
            [[ "$(sha256sum < "$ur_prev" 2>/dev/null | cut -d' ' -f1)" == "$old_sha" ]] || ur_faults+=("3: the old card is not in previous/ byte-identical")
            ur_new=$(ur_ls "$ur_card")
            while IFS= read -r p; do
                [[ -n "$p" ]] || continue
                case "$p" in *.NEX|*.nex|W457RW*) continue ;; esac
                # a name ending in a space or a dot cannot be written by FatFs: the upgrade lists it as not carried
                case "$p" in *" /"*|*"./"*|*" "|*".") continue ;; esac
                grep -qxF -- "$p" <<<"$ur_new" || ur_faults+=("3: the guest's $p is not on the new card")
            done <<<"$ur_delta"
            # the files the guest wrote are the same bytes on both cards
            ur_checked=0
            while IFS= read -r p; do
                [[ -n "$p" && "$p" != */ ]] || continue
                case "$p" in *" /"*|*"./"*|*" "|*".") continue ;; esac
                LANG=C mtype -i "$ur_prev@@$ur_off" "$p" > "$ur_dir/a.out" 2>/dev/null || continue
                LANG=C mtype -i "$ur_card@@$ur_off" "$p" > "$ur_dir/b.out" 2>/dev/null || { ur_faults+=("3: $p cannot be read on the new card"); continue; }
                cmp -s "$ur_dir/a.out" "$ur_dir/b.out" || ur_faults+=("3: the guest's $p differs on the new card")
                ur_checked=$((ur_checked + 1))
            done <<<"$ur_delta"
            ur_t=$(LANG=C mtype -i "$ur_card@@$ur_off" ::README.md 2>/dev/null || true)
            [[ "$ur_t" == "variant release" ]] || ur_faults+=("3: README.md is not the variant's")
            ur_t=$(LANG=C mtype -i "$ur_card@@$ur_off" ::VARIANT-ADDED.TXT 2>/dev/null || true)
            [[ "$ur_t" == "added by the variant release" ]] || ur_faults+=("3: the variant's added file is missing")
            ! LANG=C mdir -i "$ur_card@@$ur_off" ::CONTRIBUTING.md &>/dev/null || ur_faults+=("3: the file the variant removed is still on the new card")
            # 4: the new card boots like the old one
            ur_boot=$(LANG=C JNEXT_CONFIG_DIR="$ur_dir/cfg" timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
                          --rtc 2026-07-10T08:55:00 --script "$SCRIPT_DIR/boot-commit-probe.jds" --delayed-automatic-exit-frames 1500 </dev/null 2>&1 || true)
            ur_boot=$(sed -n 's/.*BOOT-COMMIT FRAME=\([0-9][0-9]*\).*/\1/p' <<<"$ur_boot" | head -n 1)
            [[ -n "$ur_boot" && "$ur_boot" == "$(boot_commit)" ]] || ur_faults+=("4: the upgraded card commits its machine type at frame '${ur_boot:-none}', the old one at $(boot_commit)")
        fi
        rm -rf "$ur_dir"
        if [[ ${#ur_faults[@]} -eq 0 ]]; then
            pass_row " (a real card upgraded to a variant release keeps what NextZXOS itself wrote ($ur_checked guest file(s) byte for byte), takes the variant's changed, added and removed files, keeps the old card in previous/, and boots to the same frame)"
        else
            fail_row " (${#ur_faults[@]} fault(s) in the real-content upgrade)"
            printf '      %s\n' "${ur_faults[@]}"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
