#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-6) — dezogif_ng's split-command client, adapted
# (test/dzrp/split-command.py): a command whose header and payload arrive
# apart, with a SECOND client connecting and speaking in the gap, is
# reassembled from its own bytes only — its reply carries its own seq and the
# register it asked for, not the one the other client's bytes would name — and
# the second client is closed with nothing sent. A frame dribbled one byte per
# send is reassembled too, and a fresh client is served afterwards.
if want dzrp-split-command-func; then
    dzrp_client_row dzrp-split-command-func split-command.py
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
