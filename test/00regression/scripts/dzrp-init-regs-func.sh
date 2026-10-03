#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2, rows 1, 3, 24; F1) — CMD_INIT as a 2.0.0 and as
# a 2.2.0 client (DeZog 3.7.4 and 3.8) both answer 2.2.0, ZXNEXT (4), "jnext
# v<version>" — two connections one after the other, so a reconnect is served
# too; CMD_GET_SUPPORTED_COMMANDS is exactly DE 8F BF 1F 80 0F 0C with bits
# 5/12/13/14/22 clear; CMD_GET_REGISTERS is 37 bytes with the 48K slot map
# FF FF 0A 0B 04 05 00 01; 270 commands cross the seq wrap, each reply carrying
# its own seq; a CMD_INIT without a version answers error 1.
if want dzrp-init-regs-func; then
    dzrp_row dzrp-init-regs-func init-regs
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
