#!/usr/bin/env bash
# Group row: NextZXOS SD-image self-provisioning.
# Sourced by regression.sh (the driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --- SD-card image self-provisioning (must run before ANY row that needs it) ---
# Every screenshot test and several functional tests need a NextZXOS SD image
# (the pristine fallback, see SD_CARD_ARGS in test-functions.inc). Ensure it
# is present, exactly once, right here — before the screenshot tests launch in
# parallel (all of them would race to provision it otherwise). If it is already
# present (the common case — provisioning is a one-time, machine-wide cost),
# this is instant.
echo -e "${BOLD}[sdcard-provision] Ensuring NextZXOS SD image is provisioned...${RESET}"
FALLBACK_SD_IMAGE="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"
if [[ -f "$FALLBACK_SD_IMAGE" ]]; then
    printf "  "; pass_row ": already present ($FALLBACK_SD_IMAGE)"
else
    echo "  not present — provisioning via jnext's own download+patch path (one-time)..."
    "$JNEXT" --headless --sdcard-download-confirm --delayed-automatic-exit 2 >/dev/null 2>&1 || true
    if [[ -f "$FALLBACK_SD_IMAGE" ]]; then
        printf "  "; pass_row ": provisioned $FALLBACK_SD_IMAGE"
    else
        printf "  "; fail_row ": provisioning failed — $FALLBACK_SD_IMAGE still missing"
    fi
fi
echo ""

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
