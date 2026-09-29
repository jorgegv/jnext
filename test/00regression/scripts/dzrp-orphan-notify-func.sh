#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-6; SES-01) — dezogif_ng's orphan-notify client,
# adapted (test/dzrp/orphan-notify.py): CMD_CONTINUE sent into a breakpoint and
# the socket closed in the same breath, so the stop is owed to a client that has
# gone. SES-01: the dropped socket is a CMD_CLOSE — its breakpoint goes and its
# stop is released — so a second client finds the loop run far past the
# breakpoint, and is served.
if want dzrp-orphan-notify-func; then
    dzrp_client_row dzrp-orphan-notify-func orphan-notify.py
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
