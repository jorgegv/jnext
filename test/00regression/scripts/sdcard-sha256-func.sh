#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-sha256-func (GH #464): --sdcard-sha256 keys the warm-start recording
# on a digest the CALLER checked instead of on the card's size and mtime, and a
# zipped card is refused on the desktop.
#
# The digest here is a fixed fake (64 x 'a'): the flag is a claim and jnext
# never hashes the card, so the row tests the KEYING, not hashing.
#   (a) a malformed digest exits 1, naming the argument;
#   (b) a digest without --sdcard exits 1: it describes that file;
#   (c) a recording is cached with the digest at header offset 40; after the
#       card's mtime is moved to 2001 a second run still restores it, does not
#       record again, and the tilemap-demo screenshot matches its reference;
#   (d) --sdcard of a ZIP archive exits 1, "is a ZIP archive".
# The card is the row's private copy: the mtime is moved on it.
if want sdcard-sha256-func; then
    begin_func sdcard-sha256-func

    sh_faults=()
    sh_hex=$(printf 'a%.0s' $(seq 1 64))
    sh_card="$JNEXT_CONFIG_DIR/sdcard/cspect-next-1gb-fixed.img"
    sh_cache_dir="$JNEXT_CONFIG_DIR/warm-start"
    sh_nex="test/00regression/nex/tilemap_demo.nex"
    sh_ref="test/00regression/img/tilemap-demo-reference.png"
    sh_shot="$TMP_DIR/sdcard-sha256-tilemap.png"
    sh_zip="$TMP_DIR/not-a-card.zip"

    # (a)
    sh_rc=0
    sh_a=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        --sdcard "$sh_card" --sdcard-sha256 xyz 2>&1) || sh_rc=$?
    [[ "$sh_rc" == 1 ]] || sh_faults+=("a: a malformed digest exited $sh_rc, not 1")
    grep -q "is not a SHA-256 (64 hex digits)" <<<"$sh_a" \
        || sh_faults+=("a: no 'is not a SHA-256' message")

    # (b)
    sh_rc=0
    sh_b=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        --sdcard-sha256 "$sh_hex" 2>&1) || sh_rc=$?
    [[ "$sh_rc" == 1 ]] || sh_faults+=("b: a digest without --sdcard exited $sh_rc, not 1")
    grep -q "needs --sdcard" <<<"$sh_b" || sh_faults+=("b: no 'needs --sdcard' message")

    # (c) record, then restore across an mtime change
    rm -rf "$sh_cache_dir"
    sh_c1=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        --sdcard "$sh_card" --sdcard-sha256 "$sh_hex" --rtc "$NEXTZXOS_RTC" --load "$sh_nex" \
        --delayed-automatic-exit-frames 10 2>&1 || true)
    grep -q "recorded a NextZXOS-resident machine" <<<"$sh_c1" \
        || sh_faults+=("c: the first run did not record")
    sh_file=$(ls "$sh_cache_dir"/*.jwss 2>/dev/null | head -1 || true)
    if [[ -s "$sh_file" ]]; then
        sh_got=$(od -An -tx1 -v -j40 -N32 "$sh_file" | tr -d ' \n')
        [[ "$sh_got" == "$sh_hex" ]] \
            || sh_faults+=("c: the digest at header offset 40 is '$sh_got'")
        touch -d '2001-01-01 00:00' "$sh_card"
        sh_c2=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            --sdcard "$sh_card" --sdcard-sha256 "$sh_hex" --rtc "$NEXTZXOS_RTC" --load "$sh_nex" \
            --delayed-screenshot "$sh_shot" --delayed-screenshot-frames 150 \
            --delayed-automatic-exit-frames 160 2>&1 || true)
        grep -q "restored a recorded NextZXOS machine" <<<"$sh_c2" \
            || sh_faults+=("c: after the mtime moved the recording was not restored")
        grep -q "recorded a NextZXOS-resident machine" <<<"$sh_c2" \
            && sh_faults+=("c: after the mtime moved the machine was recorded again")
        if [[ -f "$sh_shot" ]]; then
            sh_diff=$(png_diff "$sh_shot" "$sh_ref")
            [[ "$sh_diff" == "0" ]] \
                || sh_faults+=("c: tilemap-demo differs from its reference by $sh_diff pixels")
        else
            sh_faults+=("c: the restored run wrote no screenshot")
        fi
    else
        sh_faults+=("c: no cache file was written to $sh_cache_dir")
    fi

    # (d)
    python3 -I -c 'import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w") as z:
    z.writestr("card.img", b"x" * 1024)' "$sh_zip"
    sh_rc=0
    sh_d=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        --sdcard "$sh_zip" 2>&1) || sh_rc=$?
    [[ "$sh_rc" == 1 ]] || sh_faults+=("d: a zipped card exited $sh_rc, not 1")
    grep -q "is a ZIP archive" <<<"$sh_d" || sh_faults+=("d: no 'is a ZIP archive' message")

    if [[ ${#sh_faults[@]} -eq 0 ]]; then
        pass_row " (bad digest and digest-without-card refused; recording keyed on the digest survives an mtime change; zipped card refused)"
    else
        fail_row " ($(IFS=';'; echo "${sh_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
