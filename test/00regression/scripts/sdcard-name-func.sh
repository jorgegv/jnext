#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #579: the default card is jnext-sdcard.img; a card an earlier jnext named cspect-next-1gb-fixed.img keeps its
# name and is used when the new name does not exist; nothing renames, moves or deletes it; only a download or install
# creates the new name. Measured with the real Release binary on a config directory of its own whose "old card" is a
# HARD LINK of this row's SD clone (same inode: nothing is copied and the shared clone is never changed). The download
# address is a file:// one that does not exist, so a download or an offer would fail the boot instead of hiding.
#   a  old name only: the boot reaches the machine-type commit on the OLD path, the old name and its .distro are still
#      there with the same inode, and no jnext-sdcard.img appears
#   b  both names: the NEW one is used, both files stay with their inodes, the old sidecar is untouched
#   c  the window (Qt, offscreen) on an old-named card mounts the old path
#   d  --sdcard-go-back on an old-named card with no backup: exit 2, nothing renamed
#   e  neither name and an unreachable download: the start is refused, and no old-named file is created
# Oracle: the resolver rule in src/core/sdcard_provisioner.h (card_name_in) and the owner's ruling recorded there.
if want sdcard-name-func; then
    begin_func sdcard-name-func

    faults=()
    W="$RUN_DIR/extra/names"
    D="$W/sdcard"
    OLD="$D/$SD_CARD_LEGACY_NAME"
    NEW="$D/${SD_CARD_NAME}"
    rm -rf "$W"; mkdir -p "$D"
    SRC="$RUN_DIR/sdcard/${SD_CARD_NAME}"
    want_distro=$(sd_expected_distro) || want_distro=""
    [[ -n "$want_distro" && -f "$SRC" ]] || faults+=("no SD clone or no default distribution name to work from")
    ln "$SRC" "$OLD"
    [[ ! -f "$SRC.distro" ]] || cp "$SRC.distro" "$OLD.distro"
    ino=$(stat -c %i "$OLD")
    url="file:///nonexistent-never-download/${want_distro}"

    run_boot() {   # run_boot <log>: a headless boot to the machine-type commit on the directory $W
        local rc=0
        env -u DISPLAY -u WAYLAND_DISPLAY LANG=C JNEXT_CONFIG_DIR="$W" JNEXT_SDCARD_DISTRO_URL="$url" \
            timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next --rtc 2026-07-10T08:55:00 \
            --script "$SCRIPT_DIR/boot-commit-probe.jds" --delayed-automatic-exit-frames 1500 </dev/null >"$1" 2>&1 || rc=$?
        echo "$rc"
    }

    # a
    rc=$(run_boot "$W/a.log")
    [[ "$rc" == 0 ]] || faults+=("a: the boot exited $rc")
    grep -qF "BOOT-COMMIT FRAME=" "$W/a.log" || faults+=("a: the boot did not reach the machine-type commit")
    grep -qF "sdcard: using default image $OLD" "$W/a.log" || faults+=("a: the old-named card was not the one used")
    [[ -f "$OLD" && "$(stat -c %i "$OLD" 2>/dev/null)" == "$ino" ]] || faults+=("a: the old name is gone or changed (renamed or copied)")
    [[ ! -e "$NEW" ]] || faults+=("a: a jnext-sdcard.img appeared beside an old-named card")
    [[ ! -f "$SRC.distro" || -f "$OLD.distro" ]] || faults+=("a: the .distro sidecar was removed")

    # b
    ln "$OLD" "$NEW"
    printf 'sn-emulator-24.11.zip\n' > "$OLD.distro"
    rc=$(run_boot "$W/b.log")
    [[ "$rc" == 0 ]] || faults+=("b: the boot with both names exited $rc")
    grep -qF "sdcard: using default image $NEW" "$W/b.log" || faults+=("b: the new card was not the one used")
    [[ -f "$OLD" && "$(stat -c %i "$OLD")" == "$ino" && "$(stat -c %i "$NEW")" == "$ino" ]] || faults+=("b: a card was removed or replaced")
    [[ "$(cat "$OLD.distro")" == "sn-emulator-24.11.zip" ]] || faults+=("b: the old sidecar was touched")
    rm -f "$NEW"; [[ ! -f "$SRC.distro" ]] || cp "$SRC.distro" "$OLD.distro"

    # c
    c_rc=0
    env -u DISPLAY -u WAYLAND_DISPLAY LANG=C QT_QPA_PLATFORM=offscreen JNEXT_CONFIG_DIR="$W" JNEXT_SDCARD_DISTRO_URL="$url" \
        timeout --foreground --kill-after=5s 120s "$JNEXT" --machine next --rtc 2026-07-10T08:55:00 \
        --delayed-automatic-exit-frames 120 </dev/null >"$W/c.log" 2>&1 || c_rc=$?
    [[ "$c_rc" == 0 ]] || faults+=("c: the window exited $c_rc")
    grep -qF "SD card image mounted: '$OLD'" "$W/c.log" || faults+=("c: the window did not mount the old-named card")
    [[ -f "$OLD" && ! -e "$NEW" && "$(stat -c %i "$OLD")" == "$ino" ]] || faults+=("c: the window changed the card's name")

    # d
    d_rc=0
    env -u DISPLAY -u WAYLAND_DISPLAY LANG=C JNEXT_CONFIG_DIR="$W" timeout --foreground --kill-after=5s 60s "$JNEXT" \
        --sdcard-go-back </dev/null >"$W/d.log" 2>&1 || d_rc=$?
    [[ "$d_rc" == 2 ]] || faults+=("d: --sdcard-go-back exited $d_rc (want 2: no backup)")
    [[ -f "$OLD" && ! -e "$NEW" && "$(stat -c %i "$OLD")" == "$ino" ]] || faults+=("d: --sdcard-go-back changed the old-named card")

    # e
    rm -f "$OLD" "$OLD.distro"
    e_rc=0
    env -u DISPLAY -u WAYLAND_DISPLAY LANG=C JNEXT_CONFIG_DIR="$W" JNEXT_SDCARD_DISTRO_URL="$url" \
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next --sdcard-download-confirm \
        --delayed-automatic-exit-frames 10 </dev/null >"$W/e.log" 2>&1 || e_rc=$?
    [[ "$e_rc" != 0 ]] || faults+=("e: a start with no card and no reachable download succeeded")
    [[ ! -e "$OLD" ]] || faults+=("e: an old-named card was created")

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (an old-named card is used in place, never renamed; with both names the new one is used and nothing is removed; the window and --sdcard-go-back agree; no download creates the old name)"
    else
        fail_row " (${#faults[@]} fault(s) in the card-name resolution)"
        printf '      %s\n' "${faults[@]}"
    fi
    rm -rf "$W"
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
