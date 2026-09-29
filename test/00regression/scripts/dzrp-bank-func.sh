#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2; rows 5, 25, 26, REQ-dzrp-5) — legacy
# CMD_WRITE_BANK 14 and CMD_WRITE_BANK_MEM 14 at offset 0x100, read back through
# the CPU after CMD_SET_SLOT 6,14 at 0xC000 (page 14 is bank 7's, the BRAM
# routing); CMD_READ_BANK_MEM across the page edge serves only the 16 bytes
# inside it; WRITE_BANK 250 answers an error string; READ_BANK_MEM bank 255 is
# the ROM the CPU sees at 0x0000.
if want dzrp-bank-func; then
    dzrp_row dzrp-bank-func bank
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
