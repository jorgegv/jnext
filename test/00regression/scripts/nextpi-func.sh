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
# FIVE FACTS:
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
#   5. The Pi's sound reaches jnext's own output: with a stand-in QEMU on PATH
#      that plays a square wave (left +-16384, right +-8192) into the audio
#      FIFO, and a 6-byte program that opens NR 0xA2 (0xC0), --wav-record
#      carries it, each channel at its own level (about 2048 and 1024 peak to
#      peak). This is main.cpp handing PiQemu's reader to the emulator
#      (cfg.pi_audio), which no unit row can reach.
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

    # Fact 5 — the Pi's sound in jnext's own output. A ready directory with its
    # overlay already made (so no qemu-img is needed) and a stand-in QEMU that
    # writes a WAV header and then loops the tone into the wav FIFO until jnext
    # closes it (cat then fails on the broken pipe, ending the loop); its UART
    # FIFOs are held open as the real one's are. The machine is a 48K, so
    # nothing has to boot, and the injected program is NEXTREG $A2,$C0 / JR $.
    rm -rf "$np"
    mkdir -p "$np/boot"
    : >"$np/nextpi.img"
    : >"$np/boot/kernel.img"
    : >"$np/boot/bcm2708-rpi-zero.dtb"
    : >"$np/overlay.qcow2"
    echo 1_93D >"$np/release"
    stand_in="$TMP_DIR/nextpi-qemu-bin"
    mkdir -p "$stand_in"
    printf '%044d' 0 >"$stand_in/header.bin"
    : >"$stand_in/tone.pcm"
    for _ in $(seq 50); do printf '\000\100\000\340' >>"$stand_in/tone.pcm"; done   # L +16384, R -8192
    for _ in $(seq 50); do printf '\000\300\000\040' >>"$stand_in/tone.pcm"; done   # L -16384, R +8192
    cat >"$stand_in/qemu-system-arm" <<'STANDIN'
#!/bin/sh
here=$(dirname "$0")
for a in "$@"; do
    case "$a" in
        pipe,*path=*) base="${a##*path=}" ;;
        wav,*path=*) au="${a#*path=}"; au="${au%%,*}" ;;
    esac
done
if [ -n "$au" ]; then
    ( cat "$here/header.bin"; while cat "$here/tone.pcm"; do :; done ) >"$au" 2>/dev/null &
fi
exec 3<>"$base.in" 4<>"$base.out"
exec cat <&3 >/dev/null
STANDIN
    chmod +x "$stand_in/qemu-system-arm"
    printf '\355\221\242\300\030\376' >"$TMP_DIR/nextpi-a2.bin"
    wav="$TMP_DIR/nextpi-audio.wav"
    rc=0
    out=$(timeout --foreground --kill-after=5s 60s \
        env PATH="$stand_in:$PATH" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine 48k \
        --nextpi --inject "$TMP_DIR/nextpi-a2.bin" --wav-record "$wav" \
        --delayed-automatic-exit-frames 150 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 0 ]] || fails+=("--nextpi with a stand-in QEMU and --wav-record exited $rc, want 0")
    swing=$(python3 - "$wav" <<'PY' 2>&1 || true
import struct, sys
data = open(sys.argv[1], "rb").read()
i = data.find(b"data")
pcm = data[i + 8:] if i >= 0 else b""
n = len(pcm) // 4
s = struct.unpack("<%dh" % (2 * n), pcm[:4 * n]) if n else ()
left, right = s[0::2], s[1::2]
print("%d %d" % (max(left) - min(left), max(right) - min(right)) if n else "0 0")
PY
)
    read -r swing_l swing_r <<<"$swing"
    if [[ ! "$swing_l" =~ ^[0-9]+$ || ! "$swing_r" =~ ^[0-9]+$ ]]; then
        fails+=("could not read the recorded WAV ($swing)")
    elif (( swing_l < 1900 || swing_l > 2200 || swing_r < 900 || swing_r > 1150 )); then
        fails+=("the Pi's tone in the recording swings L=$swing_l R=$swing_r, want L 1900..2200, R 900..1150")
    fi

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
        pass_row " (--nextpi without QEMU exits 1 with the install hint; a declined download starts jnext without NextPi; --no-nextpi overrides the preference; a Preferences-only failure is logged and jnext runs on; the Pi's sound is in --wav-record, each channel at its own level)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
