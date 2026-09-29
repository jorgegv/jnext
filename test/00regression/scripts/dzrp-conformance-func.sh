#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-6) — the owner's dezogif_ng DZRP conformance
# suite (test/dzrp/conformance.py, copied unchanged) against a live jnext, as a
# 2.2.0 client (the version is dzrp.py's DZRP_VERSION parameter). Every check
# it runs must PASS — none unsupported, since each command is --require'd — and
# its own summary line is asserted, so a check that silently stopped running
# would show in the count.
#
# THE CHECKS NOT RUN, and why each is not jnext's to pass:
#   C19-C23  test the dezogif_ng STUB's own mechanism — breakpoints patched into
#            memory as RST 0 by CMD_SET_BREAKPOINTS / CMD_RESTORE_MEM, the stub's
#            trampoline and its swap window. jnext is a normal-mode remote with
#            native breakpoints and serves neither command (design §2), which
#            dzrp-unsupported-func pins.
#   C18      takes its verdict on a SECOND connection opened while the first is
#            still open; jnext serves one client at a time (design §2 row 1) and
#            closes it by design.
CONF_CHECKS=C1,C2,C3,C4,C5,C6,C7,C8,C9,C10,C11,C12,C13,C14,C16,C17,C24,C25,C15
CONF_REQUIRE=INIT,CLOSE,LOOPBACK,GET_REGISTERS,SET_REGISTER,READ_MEM,WRITE_MEM,WRITE_BANK,CONTINUE,PAUSE,GET_SPRITES,GET_SPRITE_PATTERNS,ADD_BREAKPOINT,REMOVE_BREAKPOINT
if want dzrp-conformance-func; then
    DZRP_EXPECT_LAST="DZRP conformance: 19 passed, 0 failed, 0 unsupported, of 19 checks"
    dzrp_client_row dzrp-conformance-func conformance.py \
        --remote "tcp:127.0.0.1:@PORT@" --start-byte none --expect-preamble none \
        --only "$CONF_CHECKS" --require "$CONF_REQUIRE" --timeout 5
    DZRP_EXPECT_LAST=""
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
