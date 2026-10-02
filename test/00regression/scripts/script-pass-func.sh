#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP4 (dsl-frontend.md §6.6) — a debugger script through the CLI,
# headless, on a real program: --map feeds `@symbol`, the rule fires on the
# program's own write, and the script's `exit 0` is the verdict.
#
#   8000 3E 5A     LD A,0x5A
#   8002 32 00 90  LD (0x9000),A   <- `@sentinel`
#   8005 18 FE     JR $
if want script-pass-func; then
    begin_func script-pass-func
    prog="$TMP_DIR/script-pass.bin"
    printf '\x3e\x5a\x32\x00\x90\x18\xfe' > "$prog"
    printf 'sentinel                        = $9000 ; addr, local, , prog, , prog.c:1\n' \
        > "$TMP_DIR/script-pass.map"
    cat > "$TMP_DIR/script-pass.jds" <<'JDS'
on write @sentinel when VALUE == 0x5A do
    log "PASS write ${VALUE:x2} to ${ADDR:x4} from PC ${PC:x4}"
    exit 0
end
JDS
    out=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --inject "$prog" --inject-org 0x8000 \
        --map "$TMP_DIR/script-pass.map" --script "$TMP_DIR/script-pass.jds" \
        --delayed-automatic-exit-frames 600 2>&1) && rc=0 || rc=$?
    if [[ $rc -eq 0 ]] && grep -q "PASS write 5A to 9000 from PC 8002" <<<"$out" &&
       grep -q "script requested exit 0" <<<"$out"; then
        pass_row " (exit 0 from the script, on the program's own write, @symbol from --map)"
    else
        fail_row " (exit $rc: $(grep -E 'SCRIPT|PASS|error' <<<"$out" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
