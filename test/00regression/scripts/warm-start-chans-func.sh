#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable - the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# warm-start-chans-func: a warm-started program can print through the ROM
# (`RST $10`), and meets NextZXOS as a typed command leaves it. Before the
# fix the warm start recorded NextZXOS at its welcome program, where channels
# K and S point at the RAM stub $5B4D (`nextreg $8E,0 : ret`, it pages ROM 0),
# so the program's first RST $10 hung. The recording is now taken at the
# Command Line executing `PRINT ;: PAUSE 0`, which is what a dot command's
# program entry looks like.
#
# The probe NEX is generated below at run time (V1.1, bank 2 only; nothing
# derived from a NextZXOS image is ever committed). It dumps bank 5 to the
# magic port, prints "OK" with RST $10 and NO CHAN_OPEN (the dot-command
# convention, NextZXOS_and_esxDOS_APIs.pdf p.27), writes marker 'P' and spins.
#
#   A. WARM     a warm `--load` reaches 'P' (RST $10 returned) and the dump
#               has K/S output = $09F4, CURCHL = CHANS+STRMS[2]-1, TV_FLAG
#               bit 0 and FLAGS2 bit 4 clear, ERR_NR $FF, PROG == VARS.
#   B. REAL     the oracle, executed here: boot NextZXOS, Command Line,
#               `.nexload` of the SAME probe from the card. 'P' is present, the
#               dump's channel area, CURCHL, TV_FLAG, FLAGS2, ERR_NR, PPC, PROG,
#               VARS and S_POSN equal A's, and A's screenshot equals B's.
#   C. NO WELCOME  on a card where NextZXOS has disabled its own welcome (D at
#               the welcome), a warm load records and passes A's checks.
#   D. 60 HZ    on a card whose config.ini says 50_60hz=1 (edited in place with
#               mtools, never replaced by a foreign file), the same.
#   E. CLOCK   the recording does not depend on the clock: three recordings of the
#               same card, with --rtc 08:55, --rtc 23:10 (another day) and the host's own time,
#               are byte-identical files. (The NextZXOS menu's work depends on
#               the RTC minute, which moved R, the clock phase and a RAM byte, so
#               R-seeded programs such as celeste differed run to run; the
#               recorder now runs on a constant clock.)
#   F. MENU     a card whose start-up menu has Calculator second (a 7-line
#               ENMENUS.CFG written with mtools here, never committed) cannot be
#               brought to the Command Line: the load logs the error naming it
#               and "Not recording", writes no cache, and falls back.
#   G. USER CLOCK  a warm-loaded program reads the USER's clock over I2C (the
#               DS1307 at $68): --rtc 2031-12-24 13:46:57 gives 57 46 13, and with
#               no --rtc the host's hour and minute, not the recorder's constant.
if want warm-start-chans-func; then
    begin_func warm-start-chans-func

    wc_faults=()
    missing=()
    for tool in mdir mcopy; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (mtools absent: ${missing[*]})"
    else
    export MTOOLS_SKIP_CHECK=1
    wc_w="$RUN_DIR/private/warm-start-chans"
    rm -rf "$wc_w"; mkdir -p "$wc_w"
    wc_src="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"

    python3 - "$wc_w/probe.nex" <<'PY'
import struct
import sys

code = bytearray()
def e(*b):
    code.extend(b)

e(0xF3)                          # di
e(0x01, 0xFE, 0xCA)              # ld bc,$cafe
e(0xED, 0x5F, 0xED, 0x79)        # ld a,r : out (c),a  (first byte of the output)
for m in (0xA5, 0x5A, 0xA5, 0x5A):
    e(0x3E, m, 0xED, 0x79)       # leading marker
e(0x21, 0x00, 0x40)              # ld hl,$4000
loop = 0x8000 + len(code)
e(0x7E)                          # ld a,(hl)
e(0xED, 0x79)                    # out (c),a
e(0x23)                          # inc hl
e(0x7C)                          # ld a,h
e(0xFE, 0x80)                    # cp $80
e(0x20, (loop - (0x8000 + len(code) + 2)) & 0xFF)   # jr nz,loop
for m in (0x5A, 0xA5, 0x5A, 0xA5):
    e(0x3E, m, 0xED, 0x79)       # trailing marker
e(0xFD, 0x21, 0x3A, 0x5C)        # ld iy,$5c3a
e(0x3E, ord('O'), 0xD7)          # rst $10  (no CHAN_OPEN)
e(0x3E, ord('K'), 0xD7)          # rst $10
e(0x01, 0xFE, 0xCA, 0x3E, ord('P'), 0xED, 0x79)   # marker 'P': printing returned
e(0xF3, 0x18, 0xFE)              # di : jr $

h = bytearray(512)
h[0:4] = b'Next'
h[4:8] = b'V1.1'
h[9] = 1                         # num_banks
h[11] = 7
struct.pack_into('<H', h, 12, 0xBFF0)
struct.pack_into('<H', h, 14, 0x8000)
h[18 + 2] = 1                    # bank 2 only
open(sys.argv[1], 'wb').write(bytes(h) + bytes(code) + bytes(0x4000 - len(code)))
PY

    # The dump parser and the field checks. A log holds the magic port's hex
    # bytes one per line among the log lines.
    cat > "$wc_w/fields.py" <<'PY'
import sys

def dump(path):
    hx = []
    for l in open(path, errors="replace"):
        l = l.strip()
        if len(l) == 2 and all(c in "0123456789ABCDEFabcdef" for c in l):
            hx.append(int(l, 16))
    start = None
    for i in range(len(hx) - 3):
        if hx[i:i + 4] == [0xA5, 0x5A, 0xA5, 0x5A]:
            start = i + 4
            break
    if start is None or len(hx) < start + 16384 + 4:
        return None, False
    d = bytes(hx[start:start + 16384])
    tail = hx[start + 16384 + 4:]
    return d, 0x50 in tail

def w(d, a): return d[a - 0x4000] | d[a - 0x4000 + 1] << 8
def b(d, a): return d[a - 0x4000]

def fields(d):
    ch = w(d, 0x5C4F)
    return {
        "CHANS": ch,
        "channels": d[ch - 0x4000: ch - 0x4000 + 20],
        "CURCHL": w(d, 0x5C51), "TV_FLAG": b(d, 0x5C3C), "FLAGS2": b(d, 0x5C6A),
        "ERR_NR": b(d, 0x5C3A), "PPC": w(d, 0x5C45), "PROG": w(d, 0x5C53),
        "VARS": w(d, 0x5C4B), "S_POSN": w(d, 0x5C88),
    }

def check_a(name, path):
    d, p = dump(path)
    if d is None:
        return [name + ": no bank-5 dump"], None
    f = fields(d)
    out = []
    if not p: out.append(name + ": marker 'P' absent, RST $10 did not return")
    ch = f["CHANS"]; strms2 = w(d, 0x5C1A)
    if w(d, ch) != 0x09F4 or w(d, ch + 5) != 0x09F4:
        out.append("%s: K/S output %04x/%04x, want 09f4" % (name, w(d, ch), w(d, ch + 5)))
    if f["CURCHL"] != ch + strms2 - 1: out.append(name + ": CURCHL is not stream 2")
    if f["TV_FLAG"] & 1: out.append(name + ": TV_FLAG bit 0 set")
    if f["FLAGS2"] & 0x10: out.append(name + ": FLAGS2 bit 4 set")
    if f["ERR_NR"] != 0xFF: out.append("%s: ERR_NR %02x" % (name, f["ERR_NR"]))
    if f["PROG"] != f["VARS"]: out.append(name + ": a BASIC program is resident")
    return out, f

mode = sys.argv[1]
if mode == "a":
    out, _ = check_a(sys.argv[2], sys.argv[3])
    print("; ".join(out))
elif mode == "cmp":
    out, fa = check_a("real", sys.argv[3])
    _, fb = check_a("warm", sys.argv[2])
    if fa is not None and fb is not None:
        for k in fa:
            if fa[k] != fb[k]:
                out.append("%s differs: warm %s, real %s" % (k, fb[k], fa[k]))
    print("; ".join(out))
PY

    wc_args=(--headless --machine next --rtc "$NEXTZXOS_RTC"
             --magic-port 0xCAFE --magic-port-mode hex)
    # wc_card <name>: a private card (a reflink clone of this run's image)
    # with its own config directory, so its warm-start cache is its own.
    wc_card() {
        mkdir -p "$wc_w/$1/sdcard"
        cp --reflink=auto "$wc_src" "$wc_w/$1/sdcard/cspect-next-1gb-fixed.img"
    }
    # wc_warm <name> <out>: a warm --load of the probe on card <name>.
    wc_warm() {
        JNEXT_CONFIG_DIR="$wc_w/$1" timeout --foreground --kill-after=5s 200s "$JNEXT" \
            "${wc_args[@]}" --sdcard "$wc_w/$1/sdcard/cspect-next-1gb-fixed.img" \
            --load "$wc_w/probe.nex" --delayed-screenshot "$wc_w/$2.png" \
            --delayed-screenshot-frames 120 --delayed-automatic-exit-frames 130 \
            > "$wc_w/$2.log" 2>&1 || true
    }
    # wc_keys <frame> <key>... : --delayed-keypress-frames pairs
    wc_press() {   # wc_press <var> <frame> <key>
        local -n arr=$1
        arr+=(--delayed-keypress-frames "$2" "$3")
    }

    # ---- A. warm, and B. the real chain, on card "ab" ------------------------
    wc_card ab
    JNEXT_CONFIG_DIR="$wc_w/ab" timeout --foreground --kill-after=5s 120s "$JNEXT" \
        --sdcard-file-add "$wc_w/probe.nex" --sdcard-file-dest /PROBE.NEX \
        --sdcard "$wc_w/ab/sdcard/cspect-next-1gb-fixed.img" \
        > "$wc_w/add.log" 2>&1 \
        || wc_faults+=("harness: --sdcard-file-add of the probe failed")

    wc_warm ab a
    grep -q "recorded a NextZXOS-resident machine" "$wc_w/a.log" \
        || wc_faults+=("A: the warm start did not record")
    # The recipe's length on this card is a deterministic function of the card
    # and the recipe's idle/state rules (605 frames on the distribution card);
    # a change to any of them moves it, so it is pinned here, not only through
    # the pixels of an R-seeded program.
    wc_n=$(sed -n 's/.*recorded a NextZXOS-resident machine ([0-9]* KB, \([0-9]*\) frames).*/\1/p' "$wc_w/a.log" | head -1)
    [[ "$wc_n" == 605 ]] \
        || wc_faults+=("A: the recording took ${wc_n:-?} frames, not the 605 measured on the distribution card")
    wc_v=$(python3 -I "$wc_w/fields.py" a A "$wc_w/a.log")
    [[ -z "$wc_v" ]] || wc_faults+=("$wc_v")

    # ---- G. a warm-loaded program reads the USER's clock -----------------------
    python3 - "$wc_w/rtc.nex" <<'PY'
import struct
import sys

code = bytearray()
labels = {}
fix = []
def e(*b): code.extend(b)
def label(n): labels[n] = 0x8000 + len(code)
def call(n):
    e(0xCD); fix.append((len(code), n)); e(0, 0)
def jr(op, n):
    e(op); fix.append((len(code), n, 'r')); e(0)
def out(v): e(0x01, 0xFE, 0xCA, 0x3E, v, 0xED, 0x79)

e(0xF3)                                  # di
e(0x3E, 1); call('scl'); e(0x3E, 1); call('sda')
out(0xA5); out(0x5A)
call('start'); e(0x1E, 0xD0); call('wb'); e(0x1E, 0x00); call('wb')    # DS1307, register 0
call('start'); e(0x1E, 0xD1); call('wb')
for nack in (0, 0, 1):                   # seconds, minutes, hours
    e(0x16, nack); call('rb')
    e(0x01, 0xFE, 0xCA, 0x7B, 0xED, 0x79)
call('stop')
out(0x5A); out(0xA5)
e(0x18, 0xFE)                            # jr $
label('scl'); e(0x01, 0x3B, 0x10, 0xED, 0x79, 0xC9)     # port $103B bit 0 = SCL
label('sda'); e(0x01, 0x3B, 0x11, 0xED, 0x79, 0xC9)     # port $113B bit 0 = SDA
label('start'); e(0x3E, 1); call('sda'); e(0x3E, 1); call('scl'); e(0xAF); call('sda'); e(0xAF); call('scl'); e(0xC9)
label('stop'); e(0xAF); call('sda'); e(0x3E, 1); call('scl'); e(0x3E, 1); call('sda'); e(0xC9)
label('wb'); e(0x2E, 8)                  # send E, MSB first, then clock the ACK
label('wb1'); e(0x7B, 0x07, 0x5F, 0xE6, 0x01); call('sda'); e(0x3E, 1); call('scl'); e(0xAF); call('scl')
e(0x2D); jr(0x20, 'wb1')
e(0x3E, 1); call('sda'); e(0x3E, 1); call('scl'); e(0xAF); call('scl'); e(0xC9)
label('rb'); e(0x3E, 1); call('sda'); e(0x2E, 8); e(0x1E, 0)    # read into E, D = 0 ACK / 1 NACK
label('rb1'); e(0x3E, 1); call('scl')
e(0x01, 0x3B, 0x11, 0xED, 0x78, 0xE6, 0x01, 0x67, 0x7B, 0x87, 0xB4, 0x5F)
e(0xAF); call('scl'); e(0x2D); jr(0x20, 'rb1')
e(0x7A); call('sda'); e(0x3E, 1); call('scl'); e(0xAF); call('scl'); e(0xC9)
for f in fix:
    if len(f) == 2:
        pos, n = f; code[pos] = labels[n] & 255; code[pos + 1] = labels[n] >> 8
    else:
        pos, n, _ = f; rel = labels[n] - (0x8000 + pos + 1); code[pos] = rel & 255
h = bytearray(512)
h[0:4] = b'Next'; h[4:8] = b'V1.1'; h[9] = 1; h[11] = 7
struct.pack_into('<H', h, 12, 0xBFF0); struct.pack_into('<H', h, 14, 0x8000)
h[18 + 2] = 1
open(sys.argv[1], 'wb').write(bytes(h) + bytes(code) + bytes(0x4000 - len(code)))
PY
    # wc_rtc_read <out> [jnext args]: warm --load of the RTC probe on card "ab"'s cache
    wc_rtc_read() {
        local out=$1; shift
        JNEXT_CONFIG_DIR="$wc_w/ab" timeout --foreground --kill-after=5s 200s "$JNEXT" \
            --headless --machine next "$@" --magic-port 0xCAFE \
            --magic-port-mode hex --sdcard "$wc_w/ab/sdcard/cspect-next-1gb-fixed.img" \
            --load "$wc_w/rtc.nex" --delayed-automatic-exit-frames 40 > "$out" 2>&1 || true
    }
    wc_hex() {   # the three bytes between the markers, "SS MM HH"
        grep -E '^[0-9A-Fa-f]{2}$' "$1" | tr '\n' ' ' | sed -n 's/^.*A5 5A \([0-9A-Fa-f][0-9A-Fa-f] [0-9A-Fa-f][0-9A-Fa-f] [0-9A-Fa-f][0-9A-Fa-f]\) 5A A5.*$/\1/p'
    }
    wc_rtc_read "$wc_w/g1.log" --rtc 2031-12-24T13:46:57
    grep -q "restored a recorded NextZXOS machine" "$wc_w/g1.log" \
        || wc_faults+=("G: the RTC probe did not run on a restored recording")
    [[ "$(wc_hex "$wc_w/g1.log")" == "57 46 13" ]] \
        || wc_faults+=("G: with --rtc 2031-12-24 13:46:57 the program read '$(wc_hex "$wc_w/g1.log")', want '57 46 13'")
    wc_h0=$(date +%H); wc_m0=$(date +%M)
    wc_rtc_read "$wc_w/g2.log"
    wc_h1=$(date +%H); wc_m1=$(date +%M)
    wc_got=$(wc_hex "$wc_w/g2.log")
    wc_ok=$(WC_GOT="$wc_got" WC_T="$wc_h0:$wc_m0:$wc_h1:$wc_m1" python3 -I -c '
import os
g = os.environ["WC_GOT"].split()
if len(g) != 3:
    print("no"); raise SystemExit
mm = int(g[1], 16) - (int(g[1], 16) >> 4) * 6
hh = int(g[2], 16) - (int(g[2], 16) >> 4) * 6
h0, m0, h1, m1 = (int(x) for x in os.environ["WC_T"].split(":"))
t = hh * 60 + mm
print("yes" if any((t - (h * 60 + m)) % 1440 in (0, 1, 2) for h, m in ((h0, m0), (h1, m1))) else "no")
')
    [[ "$wc_ok" == yes ]] \
        || wc_faults+=("G: with no --rtc the program read '$wc_got' (SS MM HH), not the host clock $wc_h0:$wc_m0")

    # ---- F. a start-up menu NextZXOS cannot reach the Command Line from --------
    wc_card f
    wc_f_img="$wc_w/f/sdcard/cspect-next-1gb-fixed.img"
    wc_flba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$wc_f_img" | tr -d ' ')
    printf '=0\r\nNextZXOS\r\nB_rowser:i10\r\nC_alculator:i1\r\nCommand L_ine:i2\r\nN_extBASIC:i0\r\nG_uide:gNextZXOS\r\n' \
        > "$wc_w/enmenus.cfg"
    mcopy -o -i "$wc_f_img@@$(( ${wc_flba:-0} * 512 ))" "$wc_w/enmenus.cfg" ::/NEXTZXOS/ENMENUS.CFG \
        || wc_faults+=("harness: could not write ENMENUS.CFG onto the card")
    JNEXT_CONFIG_DIR="$wc_w/f" timeout --foreground --kill-after=5s 200s "$JNEXT" \
        "${wc_args[@]}" --sdcard "$wc_f_img" --load "$wc_w/probe.nex" \
        --delayed-automatic-exit-frames 40 > "$wc_w/f.log" 2>&1 || true
    grep -q "NextZXOS could not be brought to its Command Line" "$wc_w/f.log" \
        || wc_faults+=("F: the load did not report that NextZXOS could not be brought to its Command Line")
    grep -q "Not recording" "$wc_w/f.log" \
        || wc_faults+=("F: the refusal does not say 'Not recording'")
    grep -q "the recipe was waiting for the Command Line to be idle" "$wc_w/f.log" \
        || wc_faults+=("F: the refusal does not say what the recipe was waiting for")
    if compgen -G "$wc_w/f/warm-start/*" >/dev/null; then
        wc_faults+=("F: a recording was cached for a machine that never reached the Command Line")
    fi
    ! grep -q "recorded a NextZXOS-resident machine\|NextZXOS is resident; the program is applied" "$wc_w/f.log" \
        || wc_faults+=("F: the load went on with a recorded NextZXOS machine instead of falling back")

    wc_keys=()
    wc_press wc_keys 400 space; wc_press wc_keys 470 down; wc_press wc_keys 500 enter
    wc_f=600; wc_cmd=".nexload probe.nex"
    for (( i = 0; i < ${#wc_cmd}; i++ )); do
        wc_ch=${wc_cmd:i:1}; [[ "$wc_ch" == " " ]] && wc_ch=space
        wc_press wc_keys "$wc_f" "$wc_ch"; wc_f=$((wc_f + 15))
    done
    wc_press wc_keys "$wc_f" enter
    JNEXT_CONFIG_DIR="$wc_w/ab-real" timeout --foreground --kill-after=5s 300s "$JNEXT" \
        "${wc_args[@]}" --sdcard "$wc_w/ab/sdcard/cspect-next-1gb-fixed.img" \
        "${wc_keys[@]}" --delayed-screenshot "$wc_w/b.png" \
        --delayed-screenshot-frames $((wc_f + 300)) \
        --delayed-automatic-exit-frames $((wc_f + 320)) > "$wc_w/b.log" 2>&1 || true
    wc_v=$(python3 -I "$wc_w/fields.py" cmp "$wc_w/a.log" "$wc_w/b.log")
    [[ -z "$wc_v" ]] || wc_faults+=("B: $wc_v")
    if [[ -f "$wc_w/a.png" && -f "$wc_w/b.png" ]]; then
        wc_px=$(png_diff "$wc_w/a.png" "$wc_w/b.png")
        [[ "$wc_px" == "0" ]] \
            || wc_faults+=("B: the warm screenshot differs from the real chain's by $wc_px pixels")
    else
        wc_faults+=("B: a screenshot is missing")
    fi

    # ---- C. NextZXOS's own welcome disabled ----------------------------------
    wc_card c
    JNEXT_CONFIG_DIR="$wc_w/c" timeout --foreground --kill-after=5s 120s "$JNEXT" \
        --headless --machine next --rtc "$NEXTZXOS_RTC" \
        --sdcard "$wc_w/c/sdcard/cspect-next-1gb-fixed.img" \
        --delayed-keypress-frames 400 d --delayed-automatic-exit-frames 600 \
        > "$wc_w/c-welcome.log" 2>&1 || true
    wc_warm c c-run
    grep -q "recorded a NextZXOS-resident machine" "$wc_w/c-run.log" \
        || wc_faults+=("C: the warm start did not record on a welcome-disabled card")
    wc_v=$(python3 -I "$wc_w/fields.py" a C "$wc_w/c-run.log")
    [[ -z "$wc_v" ]] || wc_faults+=("$wc_v")

    # ---- D. a 60 Hz card -----------------------------------------------------
    wc_card d
    wc_d_img="$wc_w/d/sdcard/cspect-next-1gb-fixed.img"
    wc_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$wc_d_img" | tr -d ' ')
    wc_off=$(( ${wc_lba:-0} * 512 ))
    if mcopy -i "$wc_d_img@@$wc_off" ::/MACHINES/NEXT/CONFIG.INI "$wc_w/config.ini" 2>/dev/null \
        && grep -q '^50_60hz=' "$wc_w/config.ini"; then
        sed -i 's/^50_60hz=.*/50_60hz=1/' "$wc_w/config.ini"
        mcopy -o -i "$wc_d_img@@$wc_off" "$wc_w/config.ini" ::/MACHINES/NEXT/CONFIG.INI \
            || wc_faults+=("harness: could not write CONFIG.INI back")
        wc_warm d d-run
        grep -q "recorded a NextZXOS-resident machine" "$wc_w/d-run.log" \
            || wc_faults+=("D: the warm start did not record on a 60 Hz card")
        wc_v=$(python3 -I "$wc_w/fields.py" a D "$wc_w/d-run.log")
        [[ -z "$wc_v" ]] || wc_faults+=("$wc_v")
    else
        wc_faults+=("harness: no 50_60hz line in the card's CONFIG.INI")
    fi

    # ---- E. the recording does not depend on the clock -----------------------
    wc_i=0
    for wc_t in "--rtc 2026-07-10T08:55:00" "--rtc 2026-03-02T23:10:00" "--no-rtc"; do
        wc_i=$((wc_i + 1))
        rm -rf "$wc_w/e$wc_i"; mkdir -p "$wc_w/e$wc_i"
        wc_rtc=(); [[ "$wc_t" == "--no-rtc" ]] || read -ra wc_rtc <<<"$wc_t"
        JNEXT_CONFIG_DIR="$wc_w/e$wc_i" timeout --foreground --kill-after=5s 200s "$JNEXT" \
            --headless --machine next "${wc_rtc[@]}" \
            --sdcard "$wc_w/ab/sdcard/cspect-next-1gb-fixed.img" --load "$wc_w/probe.nex" \
            --delayed-automatic-exit-frames 5 > "$wc_w/e$wc_i.log" 2>&1 || true
    done
    for wc_i in 2 3; do
        if [[ -s "$wc_w/e1/warm-start/warm-start-m0.jwss" ]] \
            && cmp -s "$wc_w/e1/warm-start/warm-start-m0.jwss" "$wc_w/e$wc_i/warm-start/warm-start-m0.jwss"; then :; else
            wc_faults+=("E: the recording made under clock setting $wc_i differs from the first (or is missing)")
        fi
    done

    rm -rf "$wc_w/ab" "$wc_w/ab-real" "$wc_w/c" "$wc_w/d" "$wc_w/e1" "$wc_w/e2" "$wc_w/e3" "$wc_w/f"
    if [[ ${#wc_faults[@]} -eq 0 ]]; then
        pass_row " (a warm program prints through RST \$10; the state equals the real chain's; welcome-off and 60 Hz cards record; the recording is clock-independent; a custom menu is refused; the program reads the user's clock)"
    else
        fail_row " ($(IFS=';'; echo "${wc_faults[*]}"))"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
