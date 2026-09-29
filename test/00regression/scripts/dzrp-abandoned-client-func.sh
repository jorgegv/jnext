#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-6; SES-01) — dezogif_ng's abandoned-send
# client, adapted (test/dzrp/abandoned-send-client.py): client 1 asks for 64 KB
# and closes without reading any of it, so the reply is written to a peer that
# has gone. The next client's CMD_INIT is answered, and client 1's own pause
# (its CMD_INIT's) did not outlive it: the machine ran in between.
if want dzrp-abandoned-client-func; then
    dzrp_client_row dzrp-abandoned-client-func abandoned-send-client.py
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
