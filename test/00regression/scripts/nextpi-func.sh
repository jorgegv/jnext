#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# NextPi (--nextpi) — main.cpp's start policy, through the real binary.
#
# WHY A FUNCTIONAL ROW. The policy itself is a pure function with its own unit
# row (uart_integration_test PI-27, nextpi::start_outcome). What only the real
# binary can show is that main.cpp applies it: that a --nextpi which cannot
# start NextPi makes jnext exit non-zero BEFORE the machine boots, and that
# declining the first-use download is not a failure. Neither needs QEMU, the
# network or a NextPi image: the first run has no QEMU on its PATH, the second
# answers the download prompt with end-of-input.
#
# FOUR FACTS:
#   1. --nextpi with a ready NextPi directory and no QEMU installed exits 1 and
#      says QEMU is not found (the install hint), without booting.
#   2. --nextpi with nothing installed and the prompt declined (stdin closed)
#      starts jnext without NextPi: exit 0, the "declined" line logged, nothing
#      fetched (the mirror points nowhere, so a fetch would have failed).
#   3. --no-nextpi overrides an ENABLED preference: a GUI session with
#      [nextpi] enabled=true and no QEMU installed does not even try (nothing
#      about QEMU is logged) and runs to its automatic exit.
#   4. Enabled only in Preferences, a NextPi that cannot start is NOT an error:
#      the same GUI session without the flag logs the failure and runs to its
#      automatic exit (0), never taking --nextpi's exit-1 path.
# Facts 3 and 4 run the GUI on Qt's offscreen platform, where the warning
# dialog of fact 4 is not shown (nobody could dismiss it); the log line that
# precedes it is what the row reads.
if want nextpi-func; then
    begin_func nextpi-func

    fails=()
    no_qemu_path="$TMP_DIR/nextpi-empty-path"
    mkdir -p "$no_qemu_path"

    # The NextPi directory is <config-dir>/nextpi, and this row's config dir is
    # its own (it also holds the row's SD-card clone), so it is staged there.
    np="$JNEXT_CONFIG_DIR/nextpi"

    # Fact 1 — a ready directory (the provisioner's marker and files), no QEMU.
    rm -rf "$np"
    mkdir -p "$np/boot"
    : >"$np/nextpi.img"
    : >"$np/boot/kernel.img"
    : >"$np/boot/bcm2708-rpi-zero.dtb"
    echo 1_93D >"$np/release"
    rc=0
    out=$(timeout --foreground --kill-after=5s 60s \
        env PATH="$no_qemu_path" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 1 ]] || fails+=("--nextpi without QEMU exited $rc, want 1")
    grep -q 'qemu-system-arm not found' <<<"$out" \
        || fails+=("--nextpi without QEMU did not say QEMU is not found")

    # Fact 2 — nothing installed, the prompt declined.
    rm -rf "$np"
    rc=0
    out=$(JNEXT_NEXTPI_MIRROR="file:///nonexistent-nextpi-mirror" \
        timeout --foreground --kill-after=5s 60s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 0 ]] || fails+=("declining the NextPi download exited $rc, want 0")
    grep -q 'NextPi download declined; starting without NextPi' <<<"$out" \
        || fails+=("declining the NextPi download was not logged as such")
    [[ ! -e "$np/NextPi-1_93D.tar.gz.md5" ]] \
        || fails+=("declining the NextPi download still fetched its checksum")

    # Facts 3 and 4 — a GUI session whose saved preference enables NextPi, with
    # the ready directory of fact 1 and still no QEMU on its PATH.
    conf="$JNEXT_CONFIG_DIR/jnext.conf"
    conf_saved=""
    [[ -e "$conf" ]] && conf_saved="$TMP_DIR/nextpi-func-jnext.conf" && cp "$conf" "$conf_saved"
    gui_run() {
        printf '[nextpi]\nenabled=true\n' >"$conf"
        rm -rf "$np"
        mkdir -p "$np/boot"
        : >"$np/nextpi.img"
        : >"$np/boot/kernel.img"
        : >"$np/boot/bcm2708-rpi-zero.dtb"
        echo 1_93D >"$np/release"
        rc=0
        out=$(QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            timeout --foreground --kill-after=5s 60s \
            env PATH="$no_qemu_path" "$JNEXT" "${SD_CARD_ARGS[@]}" "$@" \
            --delayed-automatic-exit 3 </dev/null 2>&1) || rc=$?
    }

    # Fact 3 — --no-nextpi overrides the preference.
    gui_run --no-nextpi
    [[ $rc -eq 0 ]] || fails+=("--no-nextpi with NextPi enabled in Preferences exited $rc, want 0")
    if grep -q 'qemu-system-arm' <<<"$out"; then
        fails+=("--no-nextpi still tried to start NextPi enabled in Preferences")
    fi

    # Fact 4 — from Preferences, a failure is reported and jnext carries on.
    gui_run
    [[ $rc -eq 0 ]] || fails+=("a Preferences-only NextPi that cannot start exited $rc, want 0")
    grep -q 'NextPi: qemu-system-arm not found' <<<"$out" \
        || fails+=("a Preferences-only NextPi that cannot start did not log why")
    if grep -q 'error: NextPi:' <<<"$out"; then
        fails+=("a Preferences-only NextPi failure was reported as a command-line error")
    fi

    if [[ -n "$conf_saved" ]]; then mv "$conf_saved" "$conf"; else rm -f "$conf"; fi
    rm -rf "$np"

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (--nextpi without QEMU exits 1 with the install hint; a declined download starts jnext without NextPi; --no-nextpi overrides the preference; a Preferences-only failure is logged and jnext runs on)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
