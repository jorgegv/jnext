#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/zrcp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../zrcp-functions.inc"

# GH #323 — a .sna / .szx / .z80 runs on the machine it was SAVED on, through
# the real binary: the startup route (main.cpp + emulator_startup_machine()),
# the core loader, and a later hard reset (the frontend must have kept the
# machine). The oracle is the right machine's own run, never the code: a
# snapshot loaded under the WRONG --machine must show the same screen as the
# same snapshot loaded under the machine it was made on.
#
#   (a) `--machine 48k --load x128.szx` (explicit, owner: the snapshot wins):
#       the warning is logged, every machine init is the 128K, and the screen
#       at frame N equals the control `--machine 128k --load x128.szx`.
#   (b) the same with no --machine (the Next default) and a 48K .sna.
#   (c) (a) plus a hard reset after the load: the reset's init is the 128K too,
#       i.e. the frontend kept the machine.
#   (d) a TS2068 .z80, a machine jnext cannot run: exit status 1, the error
#       names TS2068, and no machine init is other than the configured 48K.
#   (e) the in-place route (a debugger load never reconstructs): a 48K jnext
#       serving ZRCP is told `smartload x128.szx` then `hard-reset-cpu`; the
#       reset's init must be the 128K, i.e. driver.load told the frontend.
if want snapshot-machine-func; then
    begin_func snapshot-machine-func
    d="$TMP_DIR/snm"; mkdir -p "$d"
    x128="$d/x128.szx"; x48="$d/x48.sna"; ts="$d/ts2068.z80"
    rm -f "$d"/*.png "$d"/*.log
    timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 128k \
        --delayed-snapshot "$x128" --delayed-snapshot-frames 150 \
        --delayed-automatic-exit-frames 200 >"$d/make128.log" 2>&1 || true
    timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
        --delayed-snapshot "$x48" --delayed-snapshot-frames 150 \
        --delayed-automatic-exit-frames 200 >"$d/make48.log" 2>&1 || true
    python3 - "$ts" <<'PY'
import sys
f = bytearray(32 + 54)
f[30] = 54                      # v3
f[32] = 0x00; f[33] = 0x80      # PC
f[34] = 128                     # hardware mode 128: Timex TS2068
f += bytes([0xFF, 0xFF, 4]) + bytes(16384)   # one uncompressed page, so it parses
open(sys.argv[1], "wb").write(f)
PY
    # run <tag> <env-assignments-or-empty> <machine-args...> --load FILE: one headless run with a
    # screenshot at frame 120; sets rc and leaves $d/<tag>.log and $d/<tag>.png.
    run_load() {
        local tag=$1; shift
        rc=0
        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --rtc "2026-01-01 00:00:00" \
            "$@" --delayed-screenshot "$d/$tag.png" --delayed-screenshot-frames 120 \
            --delayed-automatic-exit-frames 160 >"$d/$tag.log" 2>&1 || rc=$?
    }
    inits() { grep -c 'Initializing emulator: machine_type=' "$d/$1.log" || true; }
    inits_of() { grep -c "Initializing emulator: machine_type=$2" "$d/$1.log" || true; }

    if [[ ! -s "$x128" || ! -s "$x48" ]]; then
        fail_row " (fixtures not written: $(grep -m1 -E '\[(error|critical)\]' "$d/make128.log" "$d/make48.log" || echo 'no error logged'))"
    else
        run_load a      --machine 48k  --load "$x128"
        run_load a_ctl  --machine 128k --load "$x128"
        run_load b      --load "$x48"
        run_load b_ctl  --machine 48k  --load "$x48"
        rc=0
        JNEXT_DELAYED_RESET_FRAMES=130 JNEXT_DELAYED_RESET_TYPE=hard \
        run_load c      --machine 48k  --load "$x128"
        run_load d      --machine 48k  --load "$ts"
        d_rc=$rc

        why=""
        # (a)
        [[ "$(inits a)" -ge 1 && "$(inits_of a 2)" -eq "$(inits a)" ]] ||
            why+=" (a: not all inits are the 128K: $(inits_of a 2) of $(inits a))"
        grep -q -- '--machine 48k.*saved on the 128K' "$d/a.log" || why+=" (a: no --machine warning)"
        cmp -s "$d/a.png" "$d/a_ctl.png" && [[ -s "$d/a.png" ]] || why+=" (a: screen differs from the 128K control)"
        # (b)
        [[ "$(inits b)" -ge 1 && "$(inits_of b 1)" -eq "$(inits b)" ]] ||
            why+=" (b: not all inits are the 48K: $(inits_of b 1) of $(inits b))"
        cmp -s "$d/b.png" "$d/b_ctl.png" && [[ -s "$d/b.png" ]] || why+=" (b: screen differs from the 48K control)"
        # (c)
        [[ "$(inits c)" -ge 2 && "$(inits_of c 2)" -eq "$(inits c)" ]] ||
            why+=" (c: the reset did not boot the 128K: $(inits_of c 2) of $(inits c) inits)"
        # (d)
        [[ "$d_rc" -eq 1 ]] || why+=" (d: exit status $d_rc, wanted 1)"
        grep -q 'TS2068' "$d/d.log" || why+=" (d: the error does not name TS2068)"
        [[ "$(inits d)" -ge 1 && "$(inits_of d 1)" -eq "$(inits d)" ]] ||
            why+=" (d: a machine other than the configured 48K was built)"

        # (e)
        elog="$d/e.log"
        if ! command -v python3 &>/dev/null; then
            why+=" (e: no python3 for the ZRCP client)"
        elif ! zrcp_launch_cmd "$elog" "$JNEXT" --headless --machine 48k --zrcp-port 0; then
            zrcp_stop
            why+=" (e: no 'zrcp: listening on' line: $(tail -n 3 "$elog" | tr '\n' ' '))"
        elif zrcp_peer snm "$x128"; then
            zrcp_stop
            n48=$(grep -c 'Initializing emulator: machine_type=1' "$elog" || true)
            n128=$(grep -c 'Initializing emulator: machine_type=2' "$elog" || true)
            last=$(grep 'Initializing emulator: machine_type=' "$elog" | tail -n 1 || true)
            [[ "$n48" -eq 1 && "$n128" -ge 2 && "$last" == *'machine_type=2'* ]] ||
                why+=" (e: after smartload + hard-reset-cpu: $n48 48K and $n128 128K inits, last: $last)"
        else
            zrcp_stop
            why+=" (e: ${ZRCP_OUT#FAIL })"
        fi

        if [[ -z "$why" ]]; then
            pass_row " (a 128K .szx under --machine 48k, a 48K .sna under the Next default: the saved machine runs, a reset keeps it, a TS2068 .z80 is refused, an in-place debugger load is followed by the reset)"
        else
            fail_row "$why"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
