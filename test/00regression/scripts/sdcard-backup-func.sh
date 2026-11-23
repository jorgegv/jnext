#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-backup-func (GH #555 S6): the backup an upgrade leaves, and the verbs that act on it. The real binary,
# headless, two synthetic file:// distributions (99.98 then 99.99), mtools as the foreign reader.
#   1. 99.98 installed, the user's file added, a writes file filled, upgraded to 99.99 (as sdcard-upgrade-carry-func);
#   2. --sdcard-go-back: exit 0; the card's .distro is 99.98 again, it holds the user's file and not the 99.99 marker,
#      the writes file is the old card's again and binds on the next start (exit 0), the 99.99 card is the backup and
#      the distribution just left is recorded as declined;
#   3. going back again restores the 99.99 card with the user's file carried over;
#   4. --sdcard-delete-backup: exit 0, previous/ is gone and the card is untouched; again: exit 2;
#   5. with another jnext holding the card both verbs exit 4 and change nothing; with --sdcard they exit 1.
# The scratch directory is under $RUN_DIR/private (not the RAM-backed /tmp: it holds ~3 GB).
if want sdcard-backup-func; then
    begin_func sdcard-backup-func

    bk_dir="$RUN_DIR/private/sdcard-backup"
    rm -rf "$bk_dir"; mkdir -p "$bk_dir/cfg/sdcard"
    bk_faults=()
    bk_missing=()
    for tool in mformat mcopy mdir mtype mmd sha256sum python3; do
        command -v "$tool" &>/dev/null || bk_missing+=("$tool")
    done

    if [[ ${#bk_missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${bk_missing[*]})"
    else
        export MTOOLS_SKIP_CHECK=1
        bk_card="$bk_dir/cfg/sdcard/cspect-next-1gb-fixed.img"
        bk_prev="$bk_dir/cfg/sdcard/previous/cspect-next-1gb-fixed.img"
        bk_off=32256
        bk_sectors=1228800
        # bk_mkdist <name>: dist-<name>.zip holding an MBR + FAT32 image with MARKER-<name>.TXT and a 16 KB 48.rom
        bk_mkdist() {
            local n=$1 img="$bk_dir/dist-$1.img"
            truncate -s $(( bk_off + bk_sectors * 512 )) "$img"
            printf '\x0c' | dd of="$img" bs=1 seek=$((0x1BE + 4)) conv=notrunc status=none
            printf '\x3f\x00\x00\x00' | dd of="$img" bs=1 seek=$((0x1BE + 8)) conv=notrunc status=none
            printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((bk_sectors & 255)) $(((bk_sectors >> 8) & 255)) $(((bk_sectors >> 16) & 255)) $(((bk_sectors >> 24) & 255)))" \
                | dd of="$img" bs=1 seek=$((0x1BE + 12)) conv=notrunc status=none
            printf '\x55\xaa' | dd of="$img" bs=1 seek=510 conv=notrunc status=none
            LANG=C mformat -i "$img@@$bk_off" -F -T "$bk_sectors" -h 255 -s 63 -c 1 :: </dev/null >/dev/null 2>&1 \
                || bk_faults+=("harness: mformat failed for $n")
            printf 'marker %s\n' "$n" > "$bk_dir/MARKER-$n.TXT"
            head -c 16384 /dev/zero | tr '\0' 'R' > "$bk_dir/48.rom"
            LANG=C mmd -i "$img@@$bk_off" ::/MACHINES ::/MACHINES/NEXT </dev/null >/dev/null 2>&1 || true
            LANG=C mcopy -i "$img@@$bk_off" "$bk_dir/MARKER-$n.TXT" ::/MARKER-$n.TXT </dev/null >/dev/null 2>&1 \
                && LANG=C mcopy -i "$img@@$bk_off" "$bk_dir/48.rom" ::/MACHINES/NEXT/48.rom </dev/null >/dev/null 2>&1 \
                || bk_faults+=("harness: mcopy failed for $n")
            python3 -I - "$img" "$bk_dir/sn-emulator-$n.zip" <<'PY' || bk_faults+=("harness: could not zip $n")
import sys, zipfile
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as z:
    z.write(sys.argv[1], "cspect-next-1gb.img")
PY
            rm -f "$img"
        }
        bk_mkdist 99.98
        bk_mkdist 99.99
        # bk_run <name> <zip version> <jnext args...>: headless run on the private config dir; prints the exit status
        bk_run() {
            local name=$1 ver=$2 rc=0; shift 2
            LANG=C JNEXT_CONFIG_DIR="$bk_dir/cfg" JNEXT_SDCARD_DISTRO_URL="file://$bk_dir/sn-emulator-$ver.zip" \
                timeout --foreground --kill-after=5s 300s "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                "$@" </dev/null > "$bk_dir/$name.out" 2>&1 || rc=$?
            tr -d '\r' < "$bk_dir/$name.out" > "$bk_dir/$name.txt"
            echo "$rc"
        }
        bk_has() {   # bk_has <card> <path>: the file is on that card
            LANG=C mdir -i "$1@@$bk_off" "::$2" </dev/null >/dev/null 2>&1
        }
        bk_distro() { tr -d '\n' < "$bk_card.distro" 2>/dev/null; }

        if [[ ${#bk_faults[@]} -eq 0 ]]; then
            rc=$(bk_run install 99.98 --sdcard-download-confirm --delayed-automatic-exit-frames 2)
            [[ $rc == 0 && -f "$bk_card" ]] || bk_faults+=("1: the 99.98 card was not installed (exit $rc)")
            printf 'user file\n' > "$bk_dir/mine.txt"; printf 'overlay file\n' > "$bk_dir/ovl.txt"
            rc=$(bk_run add1 99.98 --sdcard-file-add "$bk_dir/mine.txt" --sdcard-file-dest /MINE.TXT)
            [[ $rc == 0 ]] || bk_faults+=("1: adding the user's file failed (exit $rc)")
            bk_ov="$bk_dir/user.jsdw"
            rc=$(bk_run add2 99.98 --sdcard-writes "$bk_ov" --sdcard-file-add "$bk_dir/ovl.txt" --sdcard-file-dest /OVL.TXT)
            [[ $rc == 0 && -f "$bk_ov" ]] || bk_faults+=("1: adding the overlay's file failed (exit $rc)")
            rc=$(bk_run up 99.99 --sdcard-upgrade yes --sdcard-writes "$bk_ov" --delayed-automatic-exit-frames 2)
            [[ $rc == 0 && "$(bk_distro)" == "sn-emulator-99.99.zip" && -f "$bk_prev" ]] || bk_faults+=("1: the upgrade did not leave a 99.99 card and a backup (exit $rc)")
        fi
        if [[ ${#bk_faults[@]} -eq 0 ]]; then
            new_sha=$(sha256sum < "$bk_card" | cut -d' ' -f1)
            old_sha=$(sha256sum < "$bk_prev" | cut -d' ' -f1)
            # 2: go back
            rc=$(bk_run back1 99.99 --sdcard-go-back)
            [[ $rc == 0 ]] || bk_faults+=("2: --sdcard-go-back exited $rc: $(tail -n 2 "$bk_dir/back1.txt" | tr '\n' ' ')")
            [[ "$(bk_distro)" == "sn-emulator-99.98.zip" ]] || bk_faults+=("2: the card's .distro is '$(bk_distro)', not 99.98")
            [[ "$(sha256sum < "$bk_card" | cut -d' ' -f1)" == "$old_sha" ]] || bk_faults+=("2: the card is not the 99.98 card byte for byte")
            [[ "$(sha256sum < "$bk_prev" | cut -d' ' -f1)" == "$new_sha" ]] || bk_faults+=("2: the backup is not the 99.99 card byte for byte")
            bk_has "$bk_card" /MINE.TXT || bk_faults+=("2: the user's file is not on the 99.98 card")
            ! bk_has "$bk_card" /MARKER-99.99.TXT || bk_faults+=("2: the 99.98 card holds the 99.99 marker")
            [[ "$(tr -d '\n' < "$bk_card.distro-declined" 2>/dev/null)" == "sn-emulator-99.99.zip" ]] || bk_faults+=("2: the distribution left is not recorded as declined")
            rc=$(bk_run boot1 99.99 --sdcard-writes "$bk_ov" --delayed-automatic-exit-frames 2)
            [[ $rc == 0 ]] || bk_faults+=("2: the old card's writes file is refused after going back (exit $rc): $(tail -n 2 "$bk_dir/boot1.txt" | tr '\n' ' ')")
            # 3: and forward again
            rc=$(bk_run back2 99.99 --sdcard-go-back)
            [[ $rc == 0 && "$(bk_distro)" == "sn-emulator-99.99.zip" && "$(sha256sum < "$bk_card" | cut -d' ' -f1)" == "$new_sha" ]] \
                || bk_faults+=("3: the second go back did not restore the 99.99 card (exit $rc)")
            bk_has "$bk_card" /MINE.TXT || bk_faults+=("3: the user's file is not on the 99.99 card")
            # 5a: another jnext holds the card: both verbs exit 4, nothing changes
            LANG=C JNEXT_CONFIG_DIR="$bk_dir/cfg" JNEXT_SDCARD_DISTRO_URL="file://$bk_dir/sn-emulator-99.99.zip" \
                "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" --delayed-automatic-exit 900 </dev/null > "$bk_dir/holder.out" 2>&1 &
            bk_holder=$!
            bk_up=0
            for bk_i in $(seq 600); do   # up to 3 minutes: the holder boots slowly on a loaded host
                if [[ -s "$bk_dir/holder.out" ]] && grep -qF "SD card image mounted" "$bk_dir/holder.out"; then bk_up=1; break; fi
                sleep 0.3
            done
            [[ $bk_up == 1 ]] || bk_faults+=("5: the holding jnext never reported its card mounted")
            rc=$(bk_run locked1 99.99 --sdcard-go-back); bk_l1=$rc
            rc=$(bk_run locked2 99.99 --sdcard-delete-backup); bk_l2=$rc
            # 5c: the upgrade and its crash recovery run under the card's lock too: with the card held, --sdcard-upgrade
            # yes gets the lock message and the whole set (card, raw, sidecars, previous/) stays byte-identical, and the
            # scratch of an "interrupted" upgrade is not touched (a start that does not hold the card must not clean it)
            mkdir -p "$bk_dir/cfg/sdcard/upgrade-work"; printf 'x' > "$bk_dir/cfg/sdcard/upgrade-work/card.new"
            bk_snap() { ( cd "$bk_dir/cfg/sdcard" && find . -type f ! -name '*.lock' -print0 | sort -z | xargs -0 sha256sum ) ; }
            bk_before=$(bk_snap)
            rc=$(bk_run lockedup 99.98 --sdcard-upgrade yes)
            bk_after=$(bk_snap)
            [[ $rc == 1 ]] && grep -qF "is in use by another jnext" "$bk_dir/lockedup.txt" || bk_faults+=("5: --sdcard-upgrade yes on a held card exited $rc without the lock message")
            [[ "$bk_before" == "$bk_after" ]] || bk_faults+=("5: --sdcard-upgrade yes on a held card changed the set or its scratch")
            rm -rf "$bk_dir/cfg/sdcard/upgrade-work"
            # the holder must have lived through every held-card check above (headless runs at full speed, and it
            # only stops when killed here): a dead one means those checks tested nothing, a fault of the row itself
            kill -0 "$bk_holder" 2>/dev/null || bk_faults+=("harness: the holding jnext exited before the held-card checks finished; their results mean nothing")
            kill "$bk_holder" 2>/dev/null || true
            wait "$bk_holder" 2>/dev/null || true
            [[ "$bk_l1" == 4 && "$bk_l2" == 4 ]] || bk_faults+=("5: with the card in use the verbs exited $bk_l1 and $bk_l2, not 4 and 4")
            [[ "$(sha256sum < "$bk_prev" | cut -d' ' -f1)" == "$old_sha" && "$(sha256sum < "$bk_card" | cut -d' ' -f1)" == "$new_sha" ]] \
                || bk_faults+=("5: a refused verb changed the set")
            # 5b: with --sdcard
            rc=$(bk_run explicit 99.99 --sdcard "$bk_card" --sdcard-go-back)
            [[ $rc == 1 ]] && grep -qF "cannot be used with --sdcard" "$bk_dir/explicit.txt" || bk_faults+=("5: --sdcard-go-back with --sdcard was not refused (exit $rc)")
            # 4: delete
            rc=$(bk_run del1 99.99 --sdcard-delete-backup)
            [[ $rc == 0 && ! -e "$bk_dir/cfg/sdcard/previous" && "$(sha256sum < "$bk_card" | cut -d' ' -f1)" == "$new_sha" ]] \
                || bk_faults+=("4: --sdcard-delete-backup exited $rc or left previous/ or touched the card")
            rc=$(bk_run del2 99.99 --sdcard-delete-backup)
            [[ $rc == 2 ]] || bk_faults+=("4: a second --sdcard-delete-backup exited $rc, not 2")
            rc=$(bk_run back3 99.99 --sdcard-go-back)
            [[ $rc == 2 ]] || bk_faults+=("4: --sdcard-go-back with no backup exited $rc, not 2")
        fi
        rm -rf "$bk_dir/cfg/sdcard"/*.img "$bk_dir/cfg/sdcard/previous"
        if [[ ${#bk_faults[@]} -eq 0 ]]; then
            pass_row " (go back swaps the cards byte for byte with the writes file, the user's file and the declined record; going back again restores; delete removes only previous/; exits 0, 2, 4 and 1 as documented)"
        else
            fail_row " (${#bk_faults[@]} fault(s) in the card backup verbs)"
            printf '      %s\n' "${bk_faults[@]}"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
