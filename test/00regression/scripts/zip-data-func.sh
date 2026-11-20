#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# zip-data-func (GH #541): a .zip holding a NEX and the files beside it serves
# them to the NEX through the esxDOS stand-in, with no --esxdos-stub. The probe
# NEX is the one esxdos-hostfs-func and esxdos-nexdir-func run (one generator);
# it opens "c:/data.txt", seeks 6, reads 6 bytes and prints them, then tries
# "../escape.txt". The archives are written by CPython's zipfile (a foreign
# writer) at run time.
#   A  game.zip: game/hostfs.nex + game/data.txt (0123456789ABCDEF), no flag
#                                              -> 6789AB, SANDBOX-OK, no ERR, log names archive-load
#   B  the member is game/DATA.TXT              -> 6789AB (case-insensitive lookup)
#   C  A plus --esxdos-stub-root x              -> TSRQPO (explicit root wins), "are not served" logged
#   D  A plus a member ../evil.txt              -> exit 1, "unsafe", nothing written anywhere
#   E  game/a.nex + game/b.nex + data           -> exit 1 naming both
#   F  after all runs archive-load holds no file (the scratch directory is removed at exit)
if want zip-data-func; then
    begin_func zip-data-func

    zd="$TMP_DIR/zd"
    mkdir -p "$zd" "$TMP_DIR/x"
    printf 'ZYXWVUTSRQPONMLK' > "$TMP_DIR/x/data.txt"
    python3 "$SCRIPT_DIR/esxdos-hostfs-probe.py" "$zd/hostfs.nex"
    python3 - "$zd" <<'PY'
import sys, zipfile
d = sys.argv[1]
nex = open(d + "/hostfs.nex", "rb").read()
data = b"0123456789ABCDEF"
def make(name, members):
    with zipfile.ZipFile(d + "/" + name, "w", zipfile.ZIP_DEFLATED) as z:
        for n, b in members:
            z.writestr(zipfile.ZipInfo(n), b, zipfile.ZIP_DEFLATED)
make("a.zip", [("game/hostfs.nex", nex), ("game/data.txt", data)])
make("b.zip", [("game/hostfs.nex", nex), ("game/DATA.TXT", data)])
make("d.zip", [("game/hostfs.nex", nex), ("game/data.txt", data), ("../evil.txt", b"EVIL")])
make("e.zip", [("game/a.nex", nex), ("game/b.nex", nex), ("game/data.txt", data)])
PY

    zd_run() {  # args: extra jnext args; prints the output, then "RC=<exit status>"
        local rc=0
        timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --machine next \
            --magic-port 0xCAFE --magic-port-mode line \
            "$@" --delayed-automatic-exit-frames 120 2>&1 || rc=$?
        echo "RC=$rc"
    }

    zd_a=$(zd_run --load "$zd/a.zip")
    zd_b=$(zd_run --load "$zd/b.zip")
    zd_c=$(zd_run --esxdos-stub-root "$TMP_DIR/x" --load "$zd/a.zip")
    zd_d=$(zd_run --load "$zd/d.zip")
    zd_e=$(zd_run --load "$zd/e.zip")

    cnt() { grep -cxF -- "$1" <<<"$2" || true; }
    a_rc=$(cnt RC=0 "$zd_a");  a_slice=$(cnt 6789AB "$zd_a"); a_ok=$(cnt SANDBOX-OK "$zd_a")
    a_err=$(cnt ERR "$zd_a");  a_log=$(grep -F "esxdos host directory: " <<<"$zd_a" | grep -cF "archive-load" || true)
    b_slice=$(cnt 6789AB "$zd_b")
    c_x=$(cnt TSRQPO "$zd_c"); c_d=$(cnt 6789AB "$zd_c"); c_log=$(grep -cF "are not served" <<<"$zd_c" || true)
    d_rc=$(cnt RC=1 "$zd_d");  d_why=$(grep -cF "unsafe" <<<"$zd_d" || true)
    d_evil=$(find "$TMP_DIR" "$JNEXT_CONFIG_DIR" -name evil.txt 2>/dev/null | wc -l)
    e_rc=$(cnt RC=1 "$zd_e");  e_why=$(grep -F "holds 2 programs" <<<"$zd_e" | grep -F "a.nex" | grep -cF "b.nex" || true)
    f_left=0
    [[ -d "$JNEXT_CONFIG_DIR/archive-load" ]] && f_left=$(find "$JNEXT_CONFIG_DIR/archive-load" -type f | wc -l)

    if [[ "$a_rc" -eq 1 && "$a_slice" -ge 1 && "$a_ok" -ge 1 && "$a_err" -eq 0 && "$a_log" -ge 1 \
          && "$b_slice" -ge 1 && "$c_x" -ge 1 && "$c_d" -eq 0 && "$c_log" -ge 1 \
          && "$d_rc" -eq 1 && "$d_why" -ge 1 && "$d_evil" -eq 0 && "$e_rc" -eq 1 && "$e_why" -ge 1 \
          && "$f_left" -eq 0 ]]; then
        pass_row " (a zipped NEX is served its archive directory with no flag, any case; explicit root wins; unsafe and ambiguous archives refused; scratch removed)"
    else
        fail_row " (A clean-exit=$a_rc want1 slice=$a_slice ok=$a_ok err=$a_err log=$a_log; B slice=$b_slice want>=1; C x=$c_x want>=1 d=$c_d want0 log=$c_log; D rc1=$d_rc why=$d_why evil=$d_evil want0; E rc1=$e_rc why=$e_why; F left=$f_left want0)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
