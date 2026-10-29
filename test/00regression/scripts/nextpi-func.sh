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
# SIXTEEN FACTS (the Pi's sound is in nextpi-audio-func, which needs a stand-in QEMU
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
#   9. (GH #385) An archive in the folder that cannot be checked, because the
#      mirror's .md5 is unreachable, says so in the question AND in the log:
#      the reason in plain words and the address of the checksum.
#  10. (GH #385) In the Qt GUI, --nextpi that cannot start NextPi reports it in
#      the dialog too (not only on stderr), and still exits 1.
#  11. (GH #385) In the Qt GUI, "latest" that cannot read the mirror's list of
#      releases tells the user in a dialog (it was only logged), with the
#      address, and carries on with the release it has (exit 0).
#  12. (GH #385) The same, with a mirror that answers after 3 s: the slow list
#      shows the progress dialog (the log says so), then fails with the HTTP
#      status and the address.
#  13. (GH #385) Headless, where there is no dialog: a mirror that answers the
#      checksum of an archive in the folder only after 8 s makes the console and
#      the log say what the dialog would, in order: the line naming what is
#      fetched and from where (after 1 s), then the stalled note (after 5 s),
#      then the question that carries on.
#  14. (GH #407) Enabled in Preferences with an unterminated quote in the saved extra
#      QEMU arguments, NextPi is not started and the log says why with the position
#      (exit 0, the Preferences-only path). The check runs BEFORE the QEMU lookup:
#      "qemu-system-arm not found" is NOT logged, so no download would be offered.
#  15. (GH #407) The same from the command line, headless: `--nextpi --nextpi-qemu-args`
#      with an unterminated quote exits 1 with the same message and position, and does not
#      get as far as looking for QEMU.
#  16. (GH #407) A --nextpi-* setting flag with NextPi not enabled (no --nextpi, or
#      --no-nextpi) is warned about by name and the run goes on: exit 0.
# Facts 3, 4, 10-12 and 14 run the GUI on Qt's offscreen platform, where the warning
# dialogs are not shown (nobody could dismiss them); SdcardGuiProvisioner writes
# the title and text it would have shown to the platform log at debug level
# (--log-level platform=debug), and that is what the row reads.
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
        printf '[nextpi]\nenabled=true\n%s' "${gui_extra_conf:-}" >"$conf"
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

    # Fact 14 - a malformed extra-arguments value refuses the start before QEMU is looked up.
    gui_extra_conf='qemu_args="-name '"'"'my pi"'$'\n'
    gui_run
    gui_extra_conf=""
    [[ $rc -eq 0 ]] || fails+=("a Preferences-only NextPi with an unterminated quote in its extra QEMU arguments exited $rc, want 0")
    grep -qF "NextPi: extra QEMU arguments (--nextpi-qemu-args or Settings > Preferences > NextPi): unterminated ' quote (it opens at character 7)" <<<"$out" \
        || fails+=("an unterminated quote in the extra QEMU arguments was not reported with its position")
    if grep -q 'qemu-system-arm not found' <<<"$out"; then
        fails+=("the extra QEMU arguments were checked AFTER the QEMU lookup (an install hint or a download would be offered)")
    fi

    # Fact 15 - the same refusal from --nextpi-qemu-args, in a headless session (exit 1, the --nextpi path).
    rc=0
    out=$(timeout --foreground --kill-after=5s 60s \
        env PATH="$no_qemu_path" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --nextpi-qemu-args "-name 'my pi" --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 1 ]] || fails+=("--nextpi-qemu-args with an unterminated quote exited $rc, want 1")
    grep -qF "NextPi: extra QEMU arguments (--nextpi-qemu-args or Settings > Preferences > NextPi): unterminated ' quote (it opens at character 7)" <<<"$out" \
        || fails+=("--nextpi-qemu-args with an unterminated quote was not reported with its position")
    if grep -q 'qemu-system-arm not found' <<<"$out"; then
        fails+=("--nextpi-qemu-args was checked AFTER the QEMU lookup")
    fi

    # Fact 16 - a setting flag with NextPi off is reported, not dropped quietly.
    for nf16 in "" "--no-nextpi"; do
        rc=0
        out=$(timeout --foreground --kill-after=5s 60s \
            env PATH="$no_qemu_path" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
            ${nf16:+"$nf16"} --nextpi-qemu-args "-name x" --nextpi-dir "$TMP_DIR/nf16" \
            --delayed-automatic-exit-frames 2 </dev/null 2>&1) || rc=$?
        [[ $rc -eq 0 ]] || fails+=("a --nextpi-* flag with NextPi not enabled (${nf16:-no flag}) exited $rc, want 0")
        for f in --nextpi-qemu-args --nextpi-dir; do
            grep -qF "$f is ignored: NextPi is not enabled (use --nextpi, or enable it in Settings > Preferences > NextPi)" <<<"$out" \
                || fails+=("$f with NextPi not enabled (${nf16:-no flag}) was not warned about")
        done
    done

    # nf_text <fixed text> <file> <what>: the text is in that file (CR removed: Windows writes CRLF).
    nf_text() {
        local lf
        lf=$(tr -d '\r' <"$2" 2>/dev/null || true)
        grep -qF -- "$1" <<<"$lf" || fails+=("$3 does not carry '$1'")
    }

    # Fact 9 - an archive in the folder (junk is enough: the check never reads it) with no
    # .md5 beside it and a mirror that refuses the connection: the question and the log say
    # what could not be checked, why in plain words, and the address of the checksum.
    rm -rf "$np"
    mkdir -p "$np"
    echo junk >"$np/NextPi-1_93D.tar.gz"
    nplog9="$TMP_DIR/nextpi-func-9.log"
    nout9="$TMP_DIR/nextpi-func-9.out"
    rm -f "$nplog9" "$nout9"
    rc=0
    JNEXT_NEXTPI_MIRROR="http://127.0.0.1:1" no_proxy=127.0.0.1,localhost NO_PROXY=127.0.0.1,localhost \
        timeout --foreground --kill-after=5s 60s \
        env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --nextpi --log-file "$nplog9" --delayed-automatic-exit-frames 2 </dev/null >"$nout9" 2>&1 || rc=$?
    [[ $rc -eq 0 ]] || fails+=("--nextpi with an archive that cannot be checked (download declined) exited $rc, want 0")
    for nf_t in "jnext cannot check" \
                "the mirror's could not be used: could not connect to the server (" \
                "  URL: http://127.0.0.1:1/NextPi-1_93D.tar.gz.md5"; do
        nf_text "$nf_t" "$nout9" "the question (stderr)"
        nf_text "$nf_t" "$nplog9" "--log-file"
    done

    # Fact 10 - --nextpi in the Qt GUI with no QEMU: exit 1, the reason on stderr, and the dialog
    # (its text, since the offscreen platform shows none) says jnext exits.
    nplog10="$TMP_DIR/nextpi-func-10.log"
    rm -f "$nplog10"
    gui_run --nextpi --log-level platform=debug --log-file "$nplog10"
    [[ $rc -eq 1 ]] || fails+=("--nextpi in the Qt GUI without QEMU exited $rc, want 1")
    grep -qF 'error: NextPi: qemu-system-arm not found' <<<"$out" \
        || fails+=("--nextpi in the Qt GUI without QEMU did not say on stderr that QEMU is not found")
    nf_text "dialog not shown on the offscreen platform: jnext - NextPi: NextPi could not be started:" "$nplog10" "the Qt GUI's log (--nextpi)"
    nf_text "jnext exits, because --nextpi asked for it." "$nplog10" "the Qt GUI's log (--nextpi)"
    # ...and the same failure from Preferences alone says jnext starts without NextPi (exit 0, fact 4).
    nplog10b="$TMP_DIR/nextpi-func-10b.log"
    rm -f "$nplog10b"
    gui_run --log-level platform=debug --log-file "$nplog10b"
    [[ $rc -eq 0 ]] || fails+=("a Preferences-only NextPi that cannot start exited $rc, want 0 (dialog run)")
    nf_text "dialog not shown on the offscreen platform: jnext - NextPi: NextPi could not be started:" "$nplog10b" "the Qt GUI's log (Preferences)"
    nf_text "jnext starts without it." "$nplog10b" "the Qt GUI's log (Preferences)"

    # Facts 11 and 12 - "latest" in the Qt GUI with the release already installed and a mirror whose
    # list cannot be read. gui_latest <mirror> <log>: the stand-in QEMU on the PATH, so that NextPi
    # is tried at all.
    gui_latest() {
        printf '[nextpi]\nenabled=true\nrelease=latest\n' >"$conf"
        rm -rf "$np"
        mkdir -p "$np/boot"
        : >"$np/nextpi.img"
        : >"$np/boot/kernel.img"
        : >"$np/boot/bcm2708-rpi-zero.dtb"
        echo 1_93D >"$np/release"
        rm -f "$2"
        rc=0
        out=$(JNEXT_NEXTPI_MIRROR="$1" no_proxy=127.0.0.1,localhost NO_PROXY=127.0.0.1,localhost \
            QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            timeout --foreground --kill-after=5s 60s \
            env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" "${SD_CARD_ARGS[@]}" \
            --log-level platform=debug --log-file "$2" --delayed-automatic-exit 3 </dev/null 2>&1) || rc=$?
    }

    # Fact 11 - the mirror refuses at once.
    nplog11="$TMP_DIR/nextpi-func-11.log"
    gui_latest "http://127.0.0.1:1" "$nplog11"
    [[ $rc -eq 0 ]] || fails+=("'latest' with an unreachable mirror and NextPi installed exited $rc, want 0")
    nf_text "[warning] NextPi: cannot read the list of NextPi releases: could not connect to the server (" "$nplog11" "the Qt GUI's log ('latest', refused)"
    nf_text "  URL: http://127.0.0.1:1/" "$nplog11" "the Qt GUI's log ('latest', refused)"
    nf_text "  jnext uses the installed NextPi 1_93D" "$nplog11" "the Qt GUI's log ('latest', refused)"
    nf_text "dialog not shown on the offscreen platform: jnext - NextPi: NextPi:" "$nplog11" "the Qt GUI's log ('latest', refused)"

    # Fact 12 - the mirror is slow: a peer that answers every request with a 404 after 3 s.
    nplog12="$TMP_DIR/nextpi-func-12.log"
    nport12="$TMP_DIR/nextpi-func-12.port"
    npeer12="$TMP_DIR/nextpi-func-12.peer.log"
    rm -f "$nport12" "$npeer12"
    python3 "$SCRIPT_DIR/slow-http-peer.py" "$nport12" 3 404 >"$npeer12" 2>&1 &
    npeer_pid=$!
    for _ in $(seq 1 100); do
        [[ -s "$nport12" ]] && break
        kill -0 "$npeer_pid" 2>/dev/null || break
        sleep 0.1
    done
    if [[ ! -s "$nport12" ]]; then
        fails+=("the slow mirror never became ready: $(tr -d '\n' <"$npeer12")")
    else
        nport=$(tr -d '\n' <"$nport12")
        gui_latest "http://127.0.0.1:$nport" "$nplog12"
        [[ $rc -eq 0 ]] || fails+=("'latest' with a slow mirror and NextPi installed exited $rc, want 0")
        nf_text "progress dialog shown for a slow fetch: Reading the list of NextPi releases..." "$nplog12" "the Qt GUI's log ('latest', slow)"
        nf_text "[warning] NextPi: cannot read the list of NextPi releases: the server does not have this file (HTTP 404)" "$nplog12" "the Qt GUI's log ('latest', slow)"
        nf_text "  URL: http://127.0.0.1:$nport/" "$nplog12" "the Qt GUI's log ('latest', slow)"
        # The progress dialog comes before the failure it ends in.
        nf_lf=$(tr -d '\r' <"$nplog12" 2>/dev/null || true)
        nf_shown=$(grep -nF 'progress dialog shown for a slow fetch' <<<"$nf_lf" | head -n 1 | cut -d: -f1 || true)
        nf_failed=$(grep -nF '[warning] NextPi: cannot read the list' <<<"$nf_lf" | head -n 1 | cut -d: -f1 || true)
        [[ -n "$nf_shown" && -n "$nf_failed" && "$nf_shown" -lt "$nf_failed" ]] \
            || fails+=("the progress dialog was not shown before the list failed (lines '${nf_shown:-none}' and '${nf_failed:-none}')")
    fi
    # The peer is bounded (it ends by itself); no trap: the suite library owns the one EXIT handler.
    kill "$npeer_pid" 2>/dev/null || true
    wait "$npeer_pid" 2>/dev/null || true

    # Fact 13 - no dialog (headless): the archive of fact 9 again, and a peer that answers the
    # checksum's request with a 404 only after 8 s (the backend gives up after 30 s, so the answer
    # comes first). The checksum is a short fetch that passes a second and then stalls: stderr
    # and the log carry the line naming what is fetched and from where, then the stalled note,
    # then the question; the run carries on as in fact 9 (declined, exit 0).
    rm -rf "$np"
    mkdir -p "$np"
    echo junk >"$np/NextPi-1_93D.tar.gz"
    nplog13="$TMP_DIR/nextpi-func-13.log"
    nout13="$TMP_DIR/nextpi-func-13.out"
    nport13="$TMP_DIR/nextpi-func-13.port"
    npeer13="$TMP_DIR/nextpi-func-13.peer.log"
    rm -f "$nplog13" "$nout13" "$nport13" "$npeer13"
    python3 "$SCRIPT_DIR/slow-http-peer.py" "$nport13" 8 404 >"$npeer13" 2>&1 &
    npeer_pid=$!
    for _ in $(seq 1 100); do
        [[ -s "$nport13" ]] && break
        kill -0 "$npeer_pid" 2>/dev/null || break
        sleep 0.1
    done
    if [[ ! -s "$nport13" ]]; then
        fails+=("the slow mirror (fact 13) never became ready: $(tr -d '\n' <"$npeer13")")
    else
        nport=$(tr -d '\n' <"$nport13")
        rc=0
        JNEXT_NEXTPI_MIRROR="http://127.0.0.1:$nport" no_proxy=127.0.0.1,localhost NO_PROXY=127.0.0.1,localhost \
            timeout --foreground --kill-after=5s 60s \
            env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
            --nextpi --log-file "$nplog13" --delayed-automatic-exit-frames 2 </dev/null >"$nout13" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || fails+=("--nextpi with a slow mirror (download declined) exited $rc, want 0")
        nf_slow="Fetching the NextPi checksum... http://127.0.0.1:$nport/NextPi-1_93D.tar.gz.md5"
        nf_stalled='Stalled: no data received for [0-9]+ s\. The network or the server may be down; jnext gives up after 30 s\.'
        nf_text "$nf_slow" "$nout13" "the console (stderr)"
        nf_text "NextPi: $nf_slow" "$nplog13" "--log-file"
        nf_out13=$(tr -d '\r' <"$nout13" 2>/dev/null || true)
        nf_log13=$(tr -d '\r' <"$nplog13" 2>/dev/null || true)
        grep -qE -- "^$nf_stalled\$" <<<"$nf_out13" || fails+=("the console (stderr) does not carry the stalled note on a line of its own")
        grep -qE -- "\[warning\] NextPi: $nf_stalled" <<<"$nf_log13" || fails+=("--log-file does not carry the stalled note as a warning")
        # In order: the line, then the note, then the question.
        nf_l1=$(grep -nF -- "$nf_slow" <<<"$nf_out13" | head -n 1 | cut -d: -f1 || true)
        nf_l2=$(grep -nE -- "^$nf_stalled\$" <<<"$nf_out13" | head -n 1 | cut -d: -f1 || true)
        nf_l3=$(grep -nF -- "jnext cannot check" <<<"$nf_out13" | head -n 1 | cut -d: -f1 || true)
        [[ -n "$nf_l1" && -n "$nf_l2" && -n "$nf_l3" && "$nf_l1" -lt "$nf_l2" && "$nf_l2" -lt "$nf_l3" ]] \
            || fails+=("the console did not say the line, the stalled note and the question in that order (lines '${nf_l1:-none}', '${nf_l2:-none}', '${nf_l3:-none}')")
    fi
    kill "$npeer_pid" 2>/dev/null || true
    wait "$npeer_pid" 2>/dev/null || true

    if [[ -n "$conf_saved" ]]; then mv "$conf_saved" "$conf"; else rm -f "$conf"; fi

    rm -rf "$np"

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (--nextpi without QEMU exits 1 with the install hint; a declined download starts jnext without NextPi; --no-nextpi overrides the preference; a Preferences-only failure is logged and jnext runs on; with no QEMU nothing is offered or fetched; failures reach --log-file; an unusable directory, a manual unpack included, names its missing file; a failed fetch names its address and why, in the question, the log and the Qt GUI's dialogs, a slow list shows the progress dialog, and without a dialog the console and the log say a slow checksum and its stall)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
