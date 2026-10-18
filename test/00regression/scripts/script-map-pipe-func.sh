#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #390 -- --map reads its file ONCE, so a pipe works: `--map <(cmd)` is
# /dev/fd/N, which cannot be opened a second time without finding it drained
# (the format of the file is told by its first line, in the same read). Both
# dialects, from a process substitution, against the real binary: a one-line
# z88dk map, and a real sjasmplus --sym.
if want script-map-pipe-func; then
    begin_func script-map-pipe-func
    printf 'sentinel                        = $9000 ; addr, local, , prog, , prog.c:1\n' \
        > "$TMP_DIR/smp.map"
    cat > "$TMP_DIR/smp-z88dk.jds" <<'JDS'
on frame 1 do
    log "SMP z88dk=${@sentinel}"
    exit 0
end
JDS
    cat > "$TMP_DIR/smp-sjasmplus.jds" <<'JDS'
on frame 1 do
    log "SMP sjasmplus=${@main.loop} ${@pos.y}"
    exit 0
end
JDS
    jrun() {
        LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 50 "$@" 2>&1
    }
    outz=$(jrun --map <(cat "$TMP_DIR/smp.map") --script "$TMP_DIR/smp-z88dk.jds") && rcz=0 || rcz=$?
    outs=$(jrun --map <(cat "$PROJECT_DIR/test/fixtures/sjasmequ/equlabels.sym") \
                --script "$TMP_DIR/smp-sjasmplus.jds") && rcs=0 || rcs=$?
    if [[ $rcz -eq 0 && $rcs -eq 0 ]] &&
       grep -qE -- "--map /dev/fd/[0-9]+: 1 symbols" <<<"$outz" &&
       grep -q "SMP z88dk=36864" <<<"$outz" &&
       grep -qE -- "--map /dev/fd/[0-9]+: 16 symbols" <<<"$outs" &&
       grep -q "SMP sjasmplus=32774 32789" <<<"$outs"; then
        pass_row " (--map <(cmd): a z88dk map loads 1 symbol, an sjasmplus --sym loads 16, both exit 0)"
    else
        fail_row " (z88dk exit $rcz: $(grep -E -- '--map|SMP|error' <<<"$outz" | tail -n 2 | tr '\n' ' ') sjasmplus exit $rcs: $(grep -E -- '--map|SMP|error' <<<"$outs" | tail -n 2 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
