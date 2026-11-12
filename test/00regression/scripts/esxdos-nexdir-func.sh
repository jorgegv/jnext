#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# esxdos-nexdir-func (GH #490): --esxdos-stub with no --esxdos-stub-root serves
# the directory of the loaded NEX. It reuses the esxdos-hostfs-func probe NEX
# (byte-identical, one generator), which opens "c:/data.txt", seeks 6, reads 6
# bytes and prints them, then tries "../escape.txt".
#
# Layout: d/hostfs.nex, d/data.txt = 0123456789ABCDEF, escape.txt one level
# above d, x/data.txt = ZYXWVUTSRQPONMLK, d/hostfs.zip holding the NEX.
#   A  --esxdos-stub --load d/hostfs.nex        -> 6789AB, SANDBOX-OK
#   B  the same as a bare relative name from d  -> 6789AB (the NEX's directory
#      is the absolute path's parent, not the empty parent of "hostfs.nex")
#   C  --esxdos-stub-root x overrides it        -> TSRQPO
#   D  the NEX loaded from a .zip has no directory -> ERR, and the log says so
#      (its scratch directory holds no data.txt, so the data alone cannot tell)
#   E  no --esxdos-stub: no root                -> ERR
if want esxdos-nexdir-func; then
    begin_func esxdos-nexdir-func

    nd_dir="$TMP_DIR/d"
    mkdir -p "$nd_dir" "$TMP_DIR/x"
    printf '0123456789ABCDEF' > "$nd_dir/data.txt"
    printf 'SECRET' > "$TMP_DIR/escape.txt"
    printf 'ZYXWVUTSRQPONMLK' > "$TMP_DIR/x/data.txt"
    python3 "$SCRIPT_DIR/esxdos-hostfs-probe.py" "$nd_dir/hostfs.nex"
    python3 - "$nd_dir/hostfs.nex" "$nd_dir/hostfs.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as z:
    z.write(sys.argv[1], "hostfs.nex")
PY

    nd_run() {  # args: extra jnext args; prints the output, then "RC=<exit status>"
        local rc=0
        timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --machine next \
            --magic-port 0xCAFE --magic-port-mode line \
            "$@" --delayed-automatic-exit-frames 120 2>&1 || rc=$?
        echo "RC=$rc"
    }

    nd_a=$(nd_run --esxdos-stub --load "$nd_dir/hostfs.nex")
    nd_b=$(cd "$nd_dir" && nd_run --esxdos-stub hostfs.nex)
    nd_c=$(nd_run --esxdos-stub-root "$TMP_DIR/x" --load "$nd_dir/hostfs.nex")
    nd_d=$(nd_run --esxdos-stub --load "$nd_dir/hostfs.zip")
    nd_e=$(nd_run --load "$nd_dir/hostfs.nex")

    cnt() { grep -cxF -- "$1" <<<"$2" || true; }
    a_rc=$(cnt RC=0 "$nd_a")
    a_slice=$(cnt 6789AB "$nd_a"); a_ok=$(cnt SANDBOX-OK "$nd_a")
    a_err=$(cnt ERR "$nd_a");      a_esc=$(cnt SANDBOX-ESCAPED "$nd_a")
    b_slice=$(cnt 6789AB "$nd_b")
    c_x=$(cnt TSRQPO "$nd_c");     c_d=$(cnt 6789AB "$nd_c")
    d_err=$(cnt ERR "$nd_d");      d_d=$(cnt 6789AB "$nd_d")
    # The member's scratch directory holds no data.txt either way, so the data
    # alone cannot tell "no directory" from "the scratch directory": the log can.
    d_none=$(grep -cF "was extracted from an archive" <<<"$nd_d" || true)
    d_dir=$(grep -cF "esxdos host directory: /" <<<"$nd_d" || true)
    e_err=$(cnt ERR "$nd_e");      e_d=$(cnt 6789AB "$nd_e")

    if [[ "$a_rc" -eq 1 && "$a_slice" -ge 1 && "$a_ok" -ge 1 && "$a_err" -eq 0 \
          && "$a_esc" -eq 0 && "$b_slice" -ge 1 && "$c_x" -ge 1 && "$c_d" -eq 0 \
          && "$d_err" -ge 1 && "$d_d" -eq 0 && "$d_none" -ge 1 && "$d_dir" -eq 0 && "$e_err" -ge 1 && "$e_d" -eq 0 ]]; then
        pass_row " (NEX directory served by default, absolute and bare relative; explicit root overrides; archive member and no stub get none)"
    else
        fail_row " (A clean-exit=$a_rc want1 slice=$a_slice ok=$a_ok err=$a_err esc=$a_esc; B slice=$b_slice want>=1; C x=$c_x want>=1 d=$c_d want0; D err=$d_err want>=1 data=$d_d want0 no-dir-log=$d_none want>=1 dir-log=$d_dir want0; E err=$e_err want>=1 data=$e_d want0)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
