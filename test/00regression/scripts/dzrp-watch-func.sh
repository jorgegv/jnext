#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2; rows 42/43, REQ-dzrp-1) — a 256-byte WRITE
# watchpoint on 0x9000-0x90FF, both sides of both edges: the program writes
# 0x8FFF (one before), 0x90FF (the last byte), 0x9100 (one past) and reads
# 0x9000. Exactly one NTF_PAUSE, reason 4, at 0x90FF with its bank byte; the
# other writes run without a stop. Then a READ watch -> reason 3 at 0x9000.
if want dzrp-watch-func; then
    dzrp_row dzrp-watch-func watch
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
