#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# NextPi (--nextpi) — the launcher's link reaches the machine, through the real
# binary (GH #316).
#
# WHY A FUNCTIONAL ROW. nextpi-func covers main.cpp's start POLICY and needs no
# QEMU. What nothing else showed, on any OS, is that main() hands the launcher's
# link to the Emulator (PiQemu::link_config): the unit rows build the Emulator
# from link_config themselves. Here the real jnext starts a stand-in QEMU (the
# program test/uart/nextpi_qemu_stub.cpp, copied as qemu-system-arm / qemu-img
# onto a PATH of its own, a PE on a Windows run) and must
#   1. exit 0 and log that QEMU started;
#   2. log that the serial link attached to UART 1, naming the transport the
#      platform uses — the FIFO pair on POSIX, TCP 127.0.0.1:<port> on Windows,
#      where <port> must be the one QEMU was told to listen on (the stand-in
#      records its arguments), i.e. the Emulator really got the launcher's link;
#   3. have created the overlay through qemu-img;
#   4. stop QEMU the ORDERLY way on exit (the stand-in's `graceful` marker) and,
#      where the stand-in's pid is a host pid (not under wine), have no stand-in
#      left running.
#   5. (GH #407) The optional flags set what Preferences > NextPi sets, for one run, in a
#      headless session: --nextpi-dir (a directory of its own), --nextpi-release (1_100, the one installed there),
#      --nextpi-qemu (a QEMU under another name, found on PATH), --nextpi-audio none and
#      --nextpi-qemu-args reach the stand-in as its -kernel path, its -audiodev, its program
#      and its LAST four arguments, after jnext's own.
if want nextpi-link-func; then
    begin_func nextpi-link-func

    fails=()
    exe=""; [[ "${JNEXT_TARGET_OS:-linux}" == windows ]] && exe=.exe
    if [[ "$exe" == .exe ]]; then
        stub="$TEST_BIN_DIR/../../win-sdl-unit-test/test/nextpi_qemu_stub.exe"
    else
        stub="$TEST_BIN_DIR/nextpi_qemu_stub"
    fi
    if [[ ! -x "$stub" ]]; then
        fail_row " ($stub missing — run 'make unit-test-build' (and 'make unit-test-win-build' for the Windows run))"
    else
        bindir="$TMP_DIR/nextpi-link-bin"
        np="$JNEXT_CONFIG_DIR/nextpi"
        rm -rf "$bindir" "$np"
        mkdir -p "$bindir" "$np/boot"
        cp "$stub" "$bindir/qemu-system-arm$exe"
        cp "$stub" "$bindir/qemu-img$exe"
        : >"$np/nextpi.img"
        : >"$np/boot/kernel.img"
        : >"$np/boot/bcm2708-rpi-zero.dtb"
        echo 1_93D >"$np/release"

        rc=0
        out=$(timeout --foreground --kill-after=5s 90s \
            env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
            --nextpi --delayed-automatic-exit-frames 50 </dev/null 2>&1) || rc=$?
        [[ $rc -eq 0 ]] || fails+=("--nextpi with the stand-in QEMU exited $rc, want 0")
        grep -q 'NextPi: QEMU raspi0 started (pid ' <<<"$out" \
            || fails+=("the start of QEMU was not logged")

        if [[ "$exe" == .exe ]]; then
            port=$(grep -oP 'port=\K[0-9]+' "$bindir/args" 2>/dev/null | head -n1 || true)
            [[ -n "$port" ]] || fails+=("QEMU was not given a listening port (no port= in its arguments)")
            grep -q "Raspberry Pi serial link attached to UART 1 - TCP 127.0.0.1:$port;" <<<"$out" \
                || fails+=("the serial link was not attached over TCP 127.0.0.1:${port:-?}")
        else
            grep -q 'Raspberry Pi serial link attached to UART 1 - FIFO pair ' <<<"$out" \
                || fails+=("the serial link was not attached over the FIFO pair")
            grep -q '^pipe,id=pi,path=' "$bindir/args" 2>/dev/null \
                || fails+=("QEMU was not given the pipe chardev")
        fi

        [[ -f "$np/overlay.qcow2" ]] || fails+=("the overlay was not created")
        [[ "$(cat "$bindir/graceful" 2>/dev/null)" == 1 ]] \
            || fails+=("QEMU was not stopped the orderly way (no graceful marker)")
        if [[ "$exe" != .exe ]]; then
            spid=$(tr -d '\n' <"$bindir/pid" 2>/dev/null || true)
            if [[ -z "$spid" ]] || kill -0 "$spid" 2>/dev/null; then
                fails+=("the stand-in QEMU (pid ${spid:-?}) is still running after jnext exited")
                [[ -z "$spid" ]] || kill -KILL "$spid" 2>/dev/null || true
            fi
        fi
        rm -rf "$bindir" "$np"

        # Fact 5 - the optional flags, with a directory of their own and a QEMU under another name.
        cdir="$TMP_DIR/nextpi-cli-dir"
        bindir="$TMP_DIR/nextpi-cli-bin"
        rm -rf "$cdir" "$bindir"
        mkdir -p "$cdir/boot" "$bindir"
        cp "$stub" "$bindir/qemu-alt$exe"
        cp "$stub" "$bindir/qemu-img$exe"
        : >"$cdir/nextpi.img"
        : >"$cdir/boot/kernel.img"
        : >"$cdir/boot/bcm2708-rpi-zero.dtb"
        echo 1_100 >"$cdir/release"
        rc=0
        out=$(timeout --foreground --kill-after=5s 90s \
            env PATH="$bindir:$PATH" JNEXT_EXTRA_WINEPATH="$bindir" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
            --nextpi --nextpi-dir "$cdir" --nextpi-release 1_100 --nextpi-qemu qemu-alt \
            --nextpi-audio none --nextpi-qemu-args "-name 'cli pi' -serial telnet:127.0.0.1:2357,server,nowait" \
            --delayed-automatic-exit-frames 50 </dev/null 2>&1) || rc=$?
        [[ $rc -eq 0 ]] || fails+=("--nextpi with the --nextpi-* flags exited $rc, want 0")
        if [[ ! -f "$bindir/args" ]]; then
            fails+=("--nextpi-qemu qemu-alt did not start the stand-in under that name")
        else
            mapfile -t cl < <(tr -d '\r' <"$bindir/args")
            cn=${#cl[@]}
            (( cn >= 4 )) && [[ "${cl[cn-4]}|${cl[cn-3]}|${cl[cn-2]}|${cl[cn-1]}" == "-name|cli pi|-serial|telnet:127.0.0.1:2357,server,nowait" ]] \
                || fails+=("--nextpi-qemu-args did not reach QEMU as its last four arguments")
            cl_all=$(printf '%s\n' "${cl[@]}")
            grep -qx 'none,id=snd0' <<<"$cl_all" || fails+=("--nextpi-audio none did not reach QEMU")
            grep -q 'nextpi-cli-dir.*kernel\.img' <<<"$cl_all" || fails+=("--nextpi-dir did not reach QEMU (-kernel)")
        fi
        [[ -f "$cdir/overlay.qcow2" ]] || fails+=("--nextpi-dir: the overlay was not created in that directory")
        rm -rf "$bindir" "$cdir"

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (--nextpi starts the stand-in QEMU, attaches the launcher's serial link to UART 1 over the platform's transport, creates the overlay and stops QEMU the orderly way)"
        else
            fail_row " (${fails[*]})"
            echo "        jnext said (last lines): $(tail -n 6 <<<"$out" | tr '\n' '|')"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
