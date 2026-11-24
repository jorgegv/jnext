#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-upgrade-carry-func: an upgrade carries the user's files to the new distribution's card
# (GH #555). The real binary, headless, on a private config directory, with two synthetic
# distributions (99.98 then 99.99: a marker file each, no NextZXOS byte) fetched over file://.
#   1. 99.98 is installed through the normal download path (raw, card, sidecars);
#   2. the user adds a file to the card, and another into a writes file (--sdcard-writes);
#   3. --sdcard-upgrade yes --sdcard-writes FILE with 99.99: exit 0; the new card holds the user's
#      file and the 99.99 marker (checked with mtools, a foreign reader); the writes file, rebuilt,
#      shows its file over the NEW card and the card itself does not hold it; the old card is in
#      previous/ byte-identical (and has no 99.99 marker); the sidecars name 99.99;
#   4. a second run with the same writes file binds it to the new card (nothing is refused);
#   5. the report is in previous/upgrade-report.txt and names the carried file.
# The scratch directory is under $RUN_DIR/private (not the RAM-backed /tmp: it holds ~3 GB).
if want sdcard-upgrade-carry-func; then
    begin_func sdcard-upgrade-carry-func

    uc_dir="$RUN_DIR/private/sdcard-upgrade-carry"
    rm -rf "$uc_dir"; mkdir -p "$uc_dir/cfg/sdcard"
    uc_faults=()
    uc_missing=()
    for tool in mformat mcopy mdir mtype mmd sha256sum python3; do
        command -v "$tool" &>/dev/null || uc_missing+=("$tool")
    done

    if [[ ${#uc_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${uc_missing[*]})"
    else
        export MTOOLS_SKIP_CHECK=1
        uc_card="$uc_dir/cfg/sdcard/cspect-next-1gb-fixed.img"
        uc_prev="$uc_dir/cfg/sdcard/previous/cspect-next-1gb-fixed.img"
        uc_off=32256
        uc_sectors=1228800
        # uc_mkdist <name>: dist-<name>.zip holding an MBR + FAT32 image with MARKER-<name>.TXT and a 16 KB 48.rom
        uc_mkdist() {
            local n=$1 img="$uc_dir/dist-$1.img"
            truncate -s $(( uc_off + uc_sectors * 512 )) "$img"
            printf '\x0c' | dd of="$img" bs=1 seek=$((0x1BE + 4)) conv=notrunc status=none
            printf '\x3f\x00\x00\x00' | dd of="$img" bs=1 seek=$((0x1BE + 8)) conv=notrunc status=none
            printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((uc_sectors & 255)) $(((uc_sectors >> 8) & 255)) $(((uc_sectors >> 16) & 255)) $(((uc_sectors >> 24) & 255)))" \
                | dd of="$img" bs=1 seek=$((0x1BE + 12)) conv=notrunc status=none
            printf '\x55\xaa' | dd of="$img" bs=1 seek=510 conv=notrunc status=none
            LANG=C mformat -i "$img@@$uc_off" -F -T "$uc_sectors" -h 255 -s 63 -c 1 :: </dev/null >/dev/null 2>&1 \
                || uc_faults+=("harness: mformat failed for $n")
            printf 'marker %s\n' "$n" > "$uc_dir/MARKER-$n.TXT"
            head -c 16384 /dev/zero | tr '\0' 'R' > "$uc_dir/48.rom"
            LANG=C mmd -i "$img@@$uc_off" ::/MACHINES ::/MACHINES/NEXT </dev/null >/dev/null 2>&1 || true
            LANG=C mcopy -i "$img@@$uc_off" "$uc_dir/MARKER-$n.TXT" ::/MARKER-$n.TXT </dev/null >/dev/null 2>&1 \
                && LANG=C mcopy -i "$img@@$uc_off" "$uc_dir/48.rom" ::/MACHINES/NEXT/48.rom </dev/null >/dev/null 2>&1 \
                || uc_faults+=("harness: mcopy failed for $n")
            python3 -I - "$img" "$uc_dir/sn-emulator-$n.zip" <<'PY' || uc_faults+=("harness: could not zip $n")
import sys, zipfile
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as z:
    z.write(sys.argv[1], "cspect-next-1gb.img")
PY
            rm -f "$img"
        }
        uc_mkdist 99.98
        uc_mkdist 99.99
        # uc_run <name> <zip version> <jnext args...>: headless run on the private config dir; prints the exit status
        uc_run() {
            local name=$1 ver=$2 rc=0; shift 2
            LANG=C JNEXT_CONFIG_DIR="$uc_dir/cfg" JNEXT_SDCARD_DISTRO_URL="file://$uc_dir/sn-emulator-$ver.zip" \
                timeout --foreground --kill-after=5s 300s "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                "$@" </dev/null > "$uc_dir/$name.out" 2>&1 || rc=$?
            tr -d '\r' < "$uc_dir/$name.out" > "$uc_dir/$name.txt"
            echo "$rc"
        }

        if [[ ${#uc_faults[@]} -eq 0 ]]; then
            rc=$(uc_run install 99.98 --sdcard-download-confirm --delayed-automatic-exit-frames 2)
            [[ $rc == 0 && -f "$uc_card" ]] || uc_faults+=("1: the 99.98 card was not installed (exit $rc): $(tail -n 2 "$uc_dir/install.txt" | tr '\n' ' ')")
            printf 'user file\n' > "$uc_dir/mine.txt"; printf 'overlay file\n' > "$uc_dir/ovl.txt"
            rc=$(uc_run add1 99.98 --sdcard-file-add "$uc_dir/mine.txt" --sdcard-file-dest /MINE.TXT)
            [[ $rc == 0 ]] || uc_faults+=("2: adding the user's file failed (exit $rc)")
            uc_ov="$uc_dir/user.jsdw"
            rc=$(uc_run add2 99.98 --sdcard-writes "$uc_ov" --sdcard-file-add "$uc_dir/ovl.txt" --sdcard-file-dest /OVL.TXT)
            [[ $rc == 0 && -f "$uc_ov" ]] || uc_faults+=("2: adding the overlay's file failed (exit $rc)")
            old_sha=$(sha256sum < "$uc_card" | cut -d' ' -f1)
            if [[ ${#uc_faults[@]} -eq 0 ]]; then
                rc=$(uc_run up 99.99 --sdcard-upgrade yes --sdcard-writes "$uc_ov" --delayed-automatic-exit-frames 2)
                [[ $rc == 0 ]] || uc_faults+=("3: the upgrade run exited $rc: $(tail -n 3 "$uc_dir/up.txt" | tr '\n' ' ')")
                LANG=C mcopy -n -i "$uc_card@@$uc_off" ::/MINE.TXT "$uc_dir/mine.out" </dev/null >/dev/null 2>&1 || true
                cmp -s "$uc_dir/mine.out" "$uc_dir/mine.txt" || uc_faults+=("3: the user's file is not on the new card")
                LANG=C mcopy -n -i "$uc_card@@$uc_off" ::/MARKER-99.99.TXT "$uc_dir/m99.out" </dev/null >/dev/null 2>&1 || true
                cmp -s "$uc_dir/m99.out" "$uc_dir/MARKER-99.99.TXT" || uc_faults+=("3: the new card lacks the 99.99 marker")
                [[ "$(sha256sum < "$uc_prev" 2>/dev/null | cut -d' ' -f1)" == "$old_sha" ]] || uc_faults+=("3: the old card is not in previous/ byte-identical")
                ! LANG=C mdir -i "$uc_prev@@$uc_off" ::/MARKER-99.99.TXT </dev/null >/dev/null 2>&1 || uc_faults+=("3: the backup holds the 99.99 marker")
                [[ "$(tr -d '\n' < "$uc_card.distro" 2>/dev/null)" == "sn-emulator-99.99.zip" ]] || uc_faults+=("3: .distro does not name 99.99")
                ! LANG=C mdir -i "$uc_card@@$uc_off" ::/OVL.TXT </dev/null >/dev/null 2>&1 || uc_faults+=("3: the overlay's file went into the card itself")
                rc=$(uc_run flat 99.99 --sdcard-writes "$uc_ov" --sdcard-writes-flatten "$uc_dir/flat.img")
                [[ $rc == 0 ]] || uc_faults+=("3: flattening the rebuilt writes file failed (exit $rc): $(tail -n 2 "$uc_dir/flat.txt" | tr '\n' ' ')")
                LANG=C mcopy -n -i "$uc_dir/flat.img@@$uc_off" ::/OVL.TXT "$uc_dir/ovl.out" </dev/null >/dev/null 2>&1 || true
                cmp -s "$uc_dir/ovl.out" "$uc_dir/ovl.txt" || uc_faults+=("3: the rebuilt writes file does not show the overlay's file over the new card")
                rm -f "$uc_dir/flat.img"
                rc=$(uc_run again 99.99 --sdcard-writes "$uc_ov" --delayed-automatic-exit-frames 2)
                [[ $rc == 0 ]] || uc_faults+=("4: the rebuilt writes file was refused on the new card (exit $rc): $(tail -n 2 "$uc_dir/again.txt" | tr '\n' ' ')")
                grep -qF "/MINE.TXT" "$uc_dir/cfg/sdcard/previous/upgrade-report.txt" 2>/dev/null \
                    || grep -qiF "mine.txt" "$uc_dir/cfg/sdcard/previous/upgrade-report.txt" 2>/dev/null \
                    || uc_faults+=("5: the report does not name the carried file")
            fi
        fi
        rm -rf "$uc_dir/cfg/sdcard"/*.img "$uc_dir/cfg/sdcard/previous"
        if [[ ${#uc_faults[@]} -eq 0 ]]; then
            pass_row " (99.98 -> 99.99 carries the user's file and a writes file over; the old card is in previous/ byte-identical; the rebuilt writes file binds to the new card; the report is kept)"
        else
            fail_row " (${#uc_faults[@]} fault(s) in the upgrade carry-over)"
            printf '      %s\n' "${uc_faults[@]}"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
