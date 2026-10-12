#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# NextPi's SOUND (--nextpi), through the real binary. Split out of nextpi-func,
# whose four start-policy facts need no stand-in QEMU: these three do, and the
# path that brings the Pi's sound into the Next's mixer (a FIFO; on Windows a
# named pipe, GH #331). On POSIX the stand-in is a /bin/sh script; on Windows
# (a run under wine) it is the C++ stand-in test/uart/nextpi_qemu_stub.cpp, a PE
# copied as qemu-system-arm.exe, as nextpi-link-func does. Its `tone.loop` is
# the same 44-byte header and tone the shell script writes into the audio path,
# and the assertions are the same on every OS.
#
# THREE FACTS:
#   1. The Pi's sound reaches jnext's own output: with a stand-in QEMU on PATH
#      that plays a square wave (left +-16384, right +-8192) into the audio
#      FIFO, and a 6-byte program that opens NR 0xA2 (0xC0), --wav-record
#      carries it, each channel at its own level (about 2048 and 1024 peak to
#      peak). This is main.cpp handing PiQemu's reader to the emulator
#      (cfg.pi_audio), which no unit row can reach.
#   2. The saved Pi audio preference reaches QEMU: a GUI session with
#      [nextpi] audio=none and a stand-in QEMU on PATH starts it with
#      -audiodev none (not the mixer's wav FIFO). This is main.cpp copying
#      the preference into the start (spec.audio = saved.nextpi_audio).
#   3. NR 0xA2 bit 0 sends the Pi's audio to the EAR input: a program that
#      sets NR 0xA2 = 0xC3 and polls port 0xFE sees bit 6 toggle with a Pi
#      square wave, as a tape loader does when NextPi streams a tape.
# Fact 2 runs the GUI on Qt's offscreen platform.
# Windows arms (a run under wine): the PE stand-in replaces a directory's shell
# script. `win_standin DIR [loop]` copies it in as qemu-system-arm.exe (and
# qemu-img.exe); with `loop`, DIR's header.bin + tone.pcm become its tone.loop.
win=false
stub=""
wp=()
if [[ "${JNEXT_TARGET_OS:-linux}" == windows ]]; then
    win=true
    stub="$TEST_BIN_DIR/../../win-sdl-unit-test/test/nextpi_qemu_stub.exe"
fi
win_standin() {
    rm -f "$1/qemu-system-arm"
    cp "$stub" "$1/qemu-system-arm.exe"
    cp "$stub" "$1/qemu-img.exe"
    if [[ -n "${2:-}" ]]; then cat "$1/header.bin" "$1/tone.pcm" >"$1/tone.loop"; fi
}

if want nextpi-audio-func; then
    begin_func nextpi-audio-func

    fails=()
    if $win && [[ ! -x "$stub" ]]; then
        fails+=("$stub missing — run 'make unit-test-win-build'")
    fi
    np="$JNEXT_CONFIG_DIR/nextpi"

    # Fact 1 — the Pi's sound in jnext's own output. A ready directory with its
    # overlay already made (so no qemu-img is needed) and a stand-in QEMU that
    # writes a WAV header and then loops the tone into the wav FIFO until jnext
    # closes it (cat then fails on the broken pipe, ending the loop); its UART
    # FIFOs are held open as the real one's are. The machine is a 48K, so
    # nothing has to boot, and the injected program is NEXTREG $A2,$C0 / JR $.
    # 1500 frames, not fewer: the tone has to be in the ring (2205 frames of
    # prebuffer) before the headless run ends, and the stand-in starts as a
    # chain of processes. Measured: at 150 frames the fact failed 14 of 24
    # copies run 8 at once on a macOS host; on Linux (fedora:44, 8 CPUs) it
    # failed 0 of 180 across 8 and 12 concurrent copies at 150 and 1500 frames,
    # 24 of them beside 8 busy loops. 1500 frames is the margin, and the row
    # stays in the parallel phase.
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
    if $win; then win_standin "$stand_in" loop; wp=(JNEXT_EXTRA_WINEPATH="$stand_in"); fi
    printf '\355\221\242\300\030\376' >"$TMP_DIR/nextpi-a2.bin"
    wav="$TMP_DIR/nextpi-audio.wav"
    rc=0
    out=$(timeout --foreground --kill-after=5s 60s \
        env "${wp[@]}" PATH="$stand_in:$PATH" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine 48k \
        --nextpi --inject "$TMP_DIR/nextpi-a2.bin" --wav-record "$wav" \
        --delayed-automatic-exit-frames 1500 </dev/null 2>&1) || rc=$?
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

    conf="$JNEXT_CONFIG_DIR/jnext.conf"
    conf_saved=""
    [[ -e "$conf" ]] && conf_saved="$TMP_DIR/nextpi-audio-func-jnext.conf" && cp "$conf" "$conf_saved"

    # Fact 2 — the saved audio preference reaches QEMU. A ready directory with
    # its overlay (no qemu-img needed) and a stand-in QEMU that records its
    # arguments, then holds its UART FIFOs open as the real one does.
    printf '[nextpi]\nenabled=true\naudio=none\n' >"$conf"
    rm -rf "$np"
    mkdir -p "$np/boot"
    : >"$np/nextpi.img"
    : >"$np/boot/kernel.img"
    : >"$np/boot/bcm2708-rpi-zero.dtb"
    : >"$np/overlay.qcow2"
    echo 1_93D >"$np/release"
    args_bin="$TMP_DIR/nextpi-qemu-args"
    mkdir -p "$args_bin"
    cat >"$args_bin/qemu-system-arm" <<'STANDIN'
#!/bin/sh
here=$(dirname "$0")
printf '%s\n' "$@" >"$here/args"
for a in "$@"; do case "$a" in pipe,*path=*) base="${a##*path=}" ;; esac; done
exec 3<>"$base.in" 4<>"$base.out"
exec cat <&3 >/dev/null
STANDIN
    chmod +x "$args_bin/qemu-system-arm"
    wp=()
    if $win; then win_standin "$args_bin"; wp=(JNEXT_EXTRA_WINEPATH="$args_bin"); fi
    rc=0
    out=$(QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 60s \
        env "${wp[@]}" PATH="$args_bin:$PATH" "$JNEXT" "${SD_CARD_ARGS[@]}" \
        --delayed-automatic-exit 3 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 0 ]] || fails+=("a GUI session with the Pi's audio saved as 'none' exited $rc, want 0")
    qemu_args=""
    [[ -f "$args_bin/args" ]] && qemu_args=$(<"$args_bin/args")
    if [[ -z "$qemu_args" ]]; then
        fails+=("a GUI session with NextPi enabled in Preferences never started the stand-in QEMU")
    elif ! grep -qx 'none,id=snd0' <<<"$qemu_args" || grep -q 'wav,id=snd0,path=' <<<"$qemu_args"; then
        fails+=("the saved Pi audio preference 'none' did not reach QEMU's -audiodev")
    fi

    if [[ -n "$conf_saved" ]]; then mv "$conf_saved" "$conf"; else rm -f "$conf"; fi

    # Fact 3 — the Pi as a tape. The stand-in loops a square wave with both
    # channels in phase at +-24576 (10-bit 0x380/0x080: the EAR comparator's
    # thresholds 11 and 00), flipping every 20 frames. On a 48K machine the
    # injected program (org 0x8000) sets NR 0xA2 = 0xC3 (stereo + EAR; bit 1
    # reserved), counts changes of port 0xFE bit 6 over 60000 reads, then
    # writes 0xA5 and the count (high, low) to the magic port and loops:
    #   NEXTREG $A2,$C3 ; LD HL,0 ; LD DE,60000 ; LD C,0
    #   loop: LD A,$FF ; IN A,($FE) ; AND $40 ; CP C ; JR Z,same ; LD C,A ; INC HL
    #   same: DEC DE ; LD A,D ; OR E ; JR NZ,loop
    #   LD BC,$1234 ; LD A,$A5 ; OUT (C),A ; OUT (C),H ; OUT (C),L ; JR $
    rm -rf "$np"
    mkdir -p "$np/boot"
    : >"$np/nextpi.img"
    : >"$np/boot/kernel.img"
    : >"$np/boot/bcm2708-rpi-zero.dtb"
    : >"$np/overlay.qcow2"
    echo 1_93D >"$np/release"
    ear_bin="$TMP_DIR/nextpi-qemu-ear"
    mkdir -p "$ear_bin"
    printf '%044d' 0 >"$ear_bin/header.bin"
    : >"$ear_bin/tone.pcm"
    for _ in $(seq 20); do printf '\000\140\000\140' >>"$ear_bin/tone.pcm"; done   # +24576 both
    for _ in $(seq 20); do printf '\000\240\000\240' >>"$ear_bin/tone.pcm"; done   # -24576 both
    wp=()
    if $win; then
        win_standin "$ear_bin" loop
        wp=(JNEXT_EXTRA_WINEPATH="$ear_bin")
    else
        cp "$stand_in/qemu-system-arm" "$ear_bin/qemu-system-arm"   # fact 1's, reading its own dir
    fi
    printf '\355\221\242\303\041\000\000\021\140\352\016\000\076\377\333\376\346\100\271\050\002\117\043\033\172\263\040\360\001\064\022\076\245\355\171\355\141\355\151\030\376' \
        >"$TMP_DIR/nextpi-ear.bin"
    rc=0
    out=$(timeout --foreground --kill-after=5s 60s \
        env "${wp[@]}" PATH="$ear_bin:$PATH" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine 48k \
        --nextpi --inject "$TMP_DIR/nextpi-ear.bin" --magic-port 0x1234 --magic-port-mode dec \
        --delayed-automatic-exit-frames 150 </dev/null 2>&1) || rc=$?
    [[ $rc -eq 0 ]] || fails+=("the NR 0xA2 bit 0 EAR run exited $rc, want 0")
    toggles=$(awk '$0=="165" {getline h; getline l; print h * 256 + l; exit}' <<<"$out")
    if [[ ! "$toggles" =~ ^[0-9]+$ ]]; then
        fails+=("the EAR-polling program never reported its count")
    elif (( toggles < 20 )); then
        fails+=("port 0xFE bit 6 changed $toggles times with the Pi on EAR, want at least 20")
    fi
    rm -rf "$np"

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (the Pi's sound is in --wav-record, each channel at its own level; the saved Pi audio preference reaches QEMU; with NR 0xA2 bit 0 the Pi's square wave toggles port 0xFE bit 6)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
