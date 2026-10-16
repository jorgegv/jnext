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
# row (nextpi_test PI-27, nextpi::start_outcome). What only the real
# binary can show is that main.cpp applies it: that a --nextpi which cannot
# start NextPi makes jnext exit non-zero BEFORE the machine boots, and that
# declining the first-use download is not a failure. Neither needs QEMU, the
# network or a NextPi image: the first run has no QEMU on its PATH, the second
# answers the download prompt with end-of-input.
#
# EIGHT FACTS (the Pi's sound is in nextpi-audio-func, which needs a stand-in QEMU
# playing a tone into the audio pipe):
#   1. --nextpi with a ready NextPi directory and no QEMU installed exits 1 and
#      says QEMU is not found (the install hint), without booting.
#   2. --nextpi with nothing installed and the prompt declined (stdin closed)
#      starts jnext without NextPi: exit 0, the "declined" line logged, nothing
#      fetched (the mirror points nowhere, so a fetch would have failed). It
#      needs a QEMU on PATH (the stand-in, as in nextpi-link-func): since GH #357
#      a missing QEMU stops jnext BEFORE the question is asked (fact 5).
#   3. --no-nextpi overrides an ENABLED preference: a GUI session with
#      [nextpi] enabled=true and no QEMU installed does not even try (nothing
#      about QEMU is logged) and runs to its automatic exit.
#   4. Enabled only in Preferences, a NextPi that cannot start is NOT an error:
#      the same GUI session without the flag logs the failure and runs to its
#      automatic exit (0), never taking --nextpi's exit-1 path.
#   5. --nextpi with nothing installed and no QEMU exits 1 saying QEMU is not
#      found, WITHOUT asking to download NextPi and without fetching anything
#      (GH #357: nobody is offered 6 GB for a program that cannot run it).
#   6. The same failure with --log-file: the reason is in the LOG file too, not
#      only on stderr (GH #358: a GUI-subsystem Windows exe has no stderr).
#   7. A NextPi directory the provisioner cannot use (only an empty nextpi.img)
#      is reported in the log with the missing file (GH #358), then the
#      declined download starts jnext without NextPi (exit 0).
#   8. A manual unpack (NextPi-1_93D.tar and a NextPi-1_93D/ folder holding the
#      .img, GH #358's reporter's layout) is reported in the log with the
#      missing nextpi.img: the provisioner's own fetch files are the only
#      NextPi-* names that do not count as "a setup was tried".
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

    # The stand-in QEMU (nextpi-link-func stages it the same way), for fact 2.
    exe=""; [[ "${JNEXT_TARGET_OS:-linux}" == windows ]] && exe=.exe
    if [[ "$exe" == .exe ]]; then
        stub="$TEST_BIN_DIR/../../win-sdl-unit-test/test/nextpi_qemu_stub.exe"
    else
        stub="$TEST_BIN_DIR/nextpi_qemu_stub"
    fi
    bindir="$TMP_DIR/nextpi-func-bin"
    mkdir -p "$bindir"
    if [[ -x "$stub" ]]; then
        cp "$stub" "$bindir/qemu-system-arm$exe"
        cp "$stub" "$bindir/qemu-img$exe"
    else
        fails+=("$stub missing — run 'make unit-test-build' (and 'make unit-test-win-build' for the Windows run)")
    fi

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
        timeout --foreground --kill-after=5s 60s \
        env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 0 ]] || fails+=("declining the NextPi download exited $rc, want 0")
    grep -q 'NextPi download declined; starting without NextPi' <<<"$out" \
        || fails+=("declining the NextPi download was not logged as such")
    [[ ! -e "$np/NextPi-1_93D.tar.gz.md5" ]] \
        || fails+=("declining the NextPi download still fetched its checksum")

    # Fact 5 — nothing installed AND no QEMU: refused before anything is asked.
    rm -rf "$np"
    rc=0
    out=$(JNEXT_NEXTPI_MIRROR="file:///nonexistent-nextpi-mirror" \
        timeout --foreground --kill-after=5s 60s \
        env PATH="$no_qemu_path" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 1 ]] || fails+=("--nextpi with nothing installed and no QEMU exited $rc, want 1")
    grep -q 'qemu-system-arm not found' <<<"$out" \
        || fails+=("--nextpi with nothing installed and no QEMU did not say QEMU is not found")
    if grep -q 'Download NextPi now?' <<<"$out"; then
        fails+=("--nextpi with no QEMU still offered the NextPi download")
    fi
    [[ ! -e "$np/NextPi-1_93D.tar.gz.md5" ]] \
        || fails+=("--nextpi with no QEMU still fetched the NextPi checksum")

    # Fact 6 — the same refusal, with --log-file: the reason must reach the file.
    rm -rf "$np"
    nplog="$TMP_DIR/nextpi-func.log"
    rm -f "$nplog"
    rc=0
    JNEXT_NEXTPI_MIRROR="file:///nonexistent-nextpi-mirror" \
        timeout --foreground --kill-after=5s 60s \
        env PATH="$no_qemu_path" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --log-file "$nplog" --delayed-automatic-exit-frames 2 </dev/null >/dev/null 2>&1 || rc=$?
    grep -q 'NextPi: qemu-system-arm not found' "$nplog" 2>/dev/null \
        || fails+=("--nextpi without QEMU did not log why to --log-file")

    # Fact 7 — a directory holding only nextpi.img: the log says which file is missing.
    rm -rf "$np"
    mkdir -p "$np"
    : >"$np/nextpi.img"
    rc=0
    out=$(JNEXT_NEXTPI_MIRROR="file:///nonexistent-nextpi-mirror" \
        timeout --foreground --kill-after=5s 60s \
        env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 0 ]] || fails+=("--nextpi with an unusable NextPi directory (download declined) exited $rc, want 0")
    grep -Eq '\] \[uart\] \[warning\] NextPi: .*kernel\.img.* is missing' <<<"$out" \
        || fails+=("an unusable NextPi directory did not log which file is missing")

    # Fact 8 — the reporter's manual unpack: the reason must reach the log.
    rm -rf "$np"
    mkdir -p "$np/NextPi-1_93D"
    : >"$np/NextPi-1_93D.tar"
    : >"$np/NextPi-1_93D/NextPi2-20260924.img"
    rc=0
    out=$(JNEXT_NEXTPI_MIRROR="file:///nonexistent-nextpi-mirror" \
        timeout --foreground --kill-after=5s 60s \
        env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 0 ]] || fails+=("--nextpi with a manually unpacked NextPi directory (download declined) exited $rc, want 0")
    grep -Eq '\] \[uart\] \[warning\] NextPi: .*cannot be used: .*nextpi\.img is missing' <<<"$out" \
        || fails+=("a manually unpacked NextPi directory did not log that nextpi.img is missing")

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
        pass_row " (--nextpi without QEMU exits 1 with the install hint; a declined download starts jnext without NextPi; --no-nextpi overrides the preference; a Preferences-only failure is logged and jnext runs on; with no QEMU nothing is offered or fetched; failures reach --log-file; an unusable directory, a manual unpack included, names its missing file)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
