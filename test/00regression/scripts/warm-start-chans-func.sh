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
#   E. CLOCK    the probe's first byte is R at entry. Two recordings with RTCs
#               a few minutes apart give the same R (the recorder pins R: the
#               NextZXOS menu's work depends on the RTC minute, which made
#               R-seeded programs such as celeste differ run to run).
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
    wc_v=$(python3 -I "$wc_w/fields.py" a A "$wc_w/a.log")
    [[ -z "$wc_v" ]] || wc_faults+=("$wc_v")

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

    # ---- E. R does not follow the wall clock ---------------------------------
    wc_r=()
    for wc_t in 2026-07-10T08:55:00 2026-07-10T09:07:00; do
        rm -rf "$wc_w/e"; mkdir -p "$wc_w/e"
        JNEXT_CONFIG_DIR="$wc_w/e" timeout --foreground --kill-after=5s 200s "$JNEXT" \
            --headless --machine next --rtc "$wc_t" --magic-port 0xCAFE --magic-port-mode hex \
            --sdcard "$wc_w/ab/sdcard/cspect-next-1gb-fixed.img" --load "$wc_w/probe.nex" \
            --delayed-automatic-exit-frames 60 > "$wc_w/e.log" 2>&1 || true
        wc_r+=("$(grep -m1 -E '^[0-9A-Fa-f]{2}$' "$wc_w/e.log" || echo none)")
    done
    [[ "${wc_r[0]}" != none && "${wc_r[0]}" == "${wc_r[1]}" ]] \
        || wc_faults+=("E: R at entry is ${wc_r[0]} at 08:55 and ${wc_r[1]} at 09:07 (it follows the RTC)")

    rm -rf "$wc_w/ab" "$wc_w/ab-real" "$wc_w/c" "$wc_w/d"
    if [[ ${#wc_faults[@]} -eq 0 ]]; then
        pass_row " (a warm program prints through RST \$10; the state equals the real chain's; welcome-off and 60 Hz cards record)"
    else
        fail_row " ($(IFS=';'; echo "${wc_faults[*]}"))"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
