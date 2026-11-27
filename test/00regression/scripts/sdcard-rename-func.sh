#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #579: the default card is jnext-sdcard.img; a card an earlier jnext named cspect-next-1gb-fixed.img is
# renamed to it, with its sidecars, the first time jnext starts without --sdcard. Measured with the real Release
# binary on a config directory of its own whose "legacy card" is a HARD LINK of this row's SD clone (same inode,
# so nothing is copied and the shared clone is never changed: a rename moves one link). The download address is
# a file:// one that does not exist, so a download or an offer would fail the boot instead of hiding.
#   a  a legacy-only directory: the boot reaches the machine-type commit, the log says the card was renamed and
#      that the new path is used, the inode is unchanged (a rename, not a copy), the legacy name is gone and
#      the .distro sidecar moved with it
#   b  a second start is a no-op: no "renamed" line, the same inode
#   c  both names present: neither is touched, the new one is used, the warning names both
#   d  the window (Qt, offscreen) with the legacy default card's path saved in Preferences mounts the new path
#   e  --sdcard with the legacy path once it is gone is refused (non-zero) and says where the card is now
# Oracle: the migration rule in src/core/sdcard_provisioner.h and rename(2), which keeps the inode.
if want sdcard-rename-func; then
    begin_func sdcard-rename-func

    faults=()
    W="$RUN_DIR/extra/legacy"
    D="$W/sdcard"
    LEG="$D/cspect-next-1gb-fixed.img"
    NEW="$D/${SD_CARD_NAME}"
    rm -rf "$W"; mkdir -p "$D"
    SRC="$RUN_DIR/sdcard/${SD_CARD_NAME}"
    want_distro=$(sd_expected_distro) || want_distro=""
    [[ -n "$want_distro" && -f "$SRC" ]] || faults+=("no SD clone or no default distribution name to work from")
    ln "$SRC" "$LEG"
    [[ ! -f "$SRC.distro" ]] || cp "$SRC.distro" "$LEG.distro"
    ino=$(stat -c %i "$LEG")
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
    grep -qF "sdcard: renamed the SD card $LEG to $NEW" "$W/a.log" || faults+=("a: the log does not say the card was renamed")
    grep -qF "sdcard: using default image $NEW" "$W/a.log" || faults+=("a: the new path was not used")
    [[ -f "$NEW" && "$(stat -c %i "$NEW" 2>/dev/null)" == "$ino" ]] || faults+=("a: the new name is not the same inode (a copy, or no rename)")
    [[ ! -e "$LEG" && ! -e "$LEG.distro" ]] || faults+=("a: a legacy-named file is left behind")
    [[ -f "$NEW.distro" || ! -f "$SRC.distro" ]] || faults+=("a: the .distro sidecar did not move")

    # b
    rc=$(run_boot "$W/b.log")
    [[ "$rc" == 0 ]] || faults+=("b: the second boot exited $rc")
    ! grep -qF "renamed the SD card" "$W/b.log" || faults+=("b: the second start renamed again")
    [[ "$(stat -c %i "$NEW" 2>/dev/null)" == "$ino" ]] || faults+=("b: the card's inode changed")

    # c
    ln "$NEW" "$LEG"
    printf 'sn-emulator-24.11.zip\n' > "$LEG.distro"
    rc=$(run_boot "$W/c.log")
    [[ "$rc" == 0 ]] || faults+=("c: the boot with both names exited $rc")
    grep -qF "sdcard: using default image $NEW" "$W/c.log" || faults+=("c: the new card was not the one used")
    grep -F "is left as it is" "$W/c.log" | grep -qF "$LEG" || faults+=("c: the warning does not name the legacy card")
    grep -F "is left as it is" "$W/c.log" | grep -qF "$NEW" || faults+=("c: the warning does not name the new card")
    [[ -f "$LEG" && "$(stat -c %i "$LEG")" == "$ino" && "$(stat -c %i "$NEW")" == "$ino" ]] || faults+=("c: a card was touched")
    [[ "$(cat "$LEG.distro")" == "sn-emulator-24.11.zip" ]] || faults+=("c: the legacy sidecar was touched")
    rm -f "$LEG" "$LEG.distro"

    # d
    printf '[paths]\nsd_card_path=%s\n' "$LEG" > "$W/jnext.conf"
    d_rc=0
    env -u DISPLAY -u WAYLAND_DISPLAY LANG=C QT_QPA_PLATFORM=offscreen JNEXT_CONFIG_DIR="$W" JNEXT_SDCARD_DISTRO_URL="$url" \
        timeout --foreground --kill-after=5s 120s "$JNEXT" --machine next --rtc 2026-07-10T08:55:00 \
        --delayed-automatic-exit-frames 120 </dev/null >"$W/d.log" 2>&1 || d_rc=$?
    [[ "$d_rc" == 0 ]] || faults+=("d: the window exited $d_rc")
    grep -qF "SD card image mounted: '$NEW'" "$W/d.log" || faults+=("d: the window did not mount the renamed card")
    grep -qF "Preferences: the SD card is now $NEW" "$W/d.log" || faults+=("d: the saved path was not followed to the new name")
    rm -f "$W/jnext.conf"

    # e
    e_rc=0
    env -u DISPLAY -u WAYLAND_DISPLAY LANG=C JNEXT_CONFIG_DIR="$W" timeout --foreground --kill-after=5s 60s "$JNEXT" \
        --headless --machine next --sdcard "$LEG" --delayed-automatic-exit-frames 10 </dev/null >"$W/e.log" 2>&1 || e_rc=$?
    [[ "$e_rc" != 0 ]] || faults+=("e: --sdcard with the vanished legacy name was accepted")
    grep -qF "jnext now keeps this card as $NEW" "$W/e.log" || faults+=("e: the refusal does not say where the card is now")

    if [[ ${#faults[@]} -eq 0 ]]; then
        rm -rf "$W"
        pass_row " (a legacy card is renamed in place with its sidecars, once; both names: untouched; Preferences follow it; --sdcard stays verbatim with a hint)"
    else
        fail_row " (${#faults[@]} fault(s) in the legacy-name migration)"
        printf '      %s\n' "${faults[@]}"
        rm -rf "$W"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
