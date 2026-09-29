#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-6) — dezogif_ng's queued-commands client,
# adapted (test/dzrp/queued-commands.py says how and why): commands that arrive
# while the server is busy answering others must each be answered. On jnext
# that is the drain-while-paused rule (SES-03): five commands in one send while
# paused plus a sixth sent the moment the first reply lands, then three in one
# send while running — every reply in order, its own seq and payload.
if want dzrp-queued-commands-func; then
    dzrp_client_row dzrp-queued-commands-func queued-commands.py
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
