#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/zrcp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../zrcp-functions.inc"

# GH #280 (zrcp-frontend.md §6.2, items 1-4 — the rest land with WP-4/WP-5) — a
# live headless jnext serving ZRCP on `--zrcp-port 0`, driven by zrcp-peer.py m1
# and diffed byte-exactly against ZEsarUX 12.0's transcripts: the welcome and
# prompt, get-version coercing into [10.3, 12.1), enter-cpu-step's prompt, a
# cpu-step's reply shape (the mnemonic at column 7) and get-registers at DeZog's
# widths, `run` answering "Running until …" FIRST and then the stop with its
# `fired` line at the PC it stopped at (the magic breakpoint — slot
# breakpoints are WP-4's), and `run` + a bare newline as one plain stop with the
# newline not executed. The same process also serves DZRP (`--dzrp-port 0`),
# and a DZRP loopback is answered beside the ZRCP session.
if want zrcp-func; then
    begin_func zrcp-func
    log="$TMP_DIR/zrcp-func.log"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the ZRCP client)"
    elif ! zrcp_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" \
            --magic-breakpoint --zrcp-port 0 --dzrp-port 0; then
        zrcp_stop
        fail_row " (no 'zrcp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
    elif [[ -z "$DZRP_PORT_ALSO" ]]; then
        zrcp_stop
        fail_row " (--dzrp-port 0 beside --zrcp-port 0 logged no 'dzrp: listening on')"
    elif zrcp_peer m1 "$DZRP_PORT_ALSO"; then
        zrcp_stop
        pass_row " (${ZRCP_OUT#PASS })"
    else
        zrcp_stop
        fail_row " (${ZRCP_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
