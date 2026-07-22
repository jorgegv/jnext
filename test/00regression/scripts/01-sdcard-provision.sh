#!/usr/bin/env bash
# Group row: NextZXOS SD-image self-provisioning.
# Sourced by regression.sh (the driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --- SD-card image self-provisioning (must run before ANY row that needs it) ---
# Every screenshot test and several functional tests boot a NextZXOS SD image
# (the fallback, see SD_CARD_ARGS in test-functions.inc). It is provisioned
# exactly once here, before the screenshot rows launch in parallel — all of them
# would race to provision it otherwise.
#
# THE PROBLEM. The patched image is machine-wide MUTABLE state, and NextZXOS
# writes back to the card, so any interactive jnext session that boots it
# changes what the whole suite tests against. On 2026-07-21, after an evening of
# manual NextZXOS booting, the image had 11012 bytes changed and its first-boot
# welcome screen disabled; a full run then reported 91 pass / 5 FAIL on a branch
# that was green — boot-nextzxos-welcome/-menu/-dotls, tape-save-boot-func and
# reset-to-nextzxos-func failing with screenshot diffs indistinguishable from a
# rendering regression. Re-deriving gave 96/0/0 on the same tree. A result that
# does not correspond to the thing under test is not a result; it is worse than
# none, because it is believed. Checking only that the file EXISTED could not
# see any of this.
#
# THE FIX. Repair it automatically rather than asking a human to. The image is a
# derived artifact, not user content: jnext rebuilds it from the raw
# distribution download in 1.9 s, and the result is byte-identical every time
# (fatfs is built with FF_FS_NORTC=1, so its timestamps are a compile-time
# constant and nothing in the output varies with the clock).
#
# WHY THE HASH GATE, rather than simply re-deriving every run. CLAUDE.md tells
# reviewers to run regression cycles CONCURRENTLY, in separate worktrees. Those
# runs share $HOME, hence this one image, and today that is safe only because
# after provisioning the file is nothing but READ. Rewriting a 1 GB file on
# every run would truncate it under another run's parallel screenshot rows —
# trading a visible problem for a rarer, harder one. So the common path does no
# writes at all: hash (0.75 s), and if it matches, touch nothing.
#
# Repair is serialised with flock so two concurrent runs cannot both rebuild the
# file. A missing witness re-derives rather than adopting whatever is on disk —
# adopting would silently bless an already-corrupt image and look like
# protection while providing none.
#
# Deleting the patched copy is safe by construction, and the safety is jnext's
# rather than ours: per src/core/sdcard_provisioner.h, when the patched image is
# absent but the raw is present, jnext re-patches from the raw ONLY if the raw's
# SHA256 matches its .sha256 sidecar, and forces a full re-download otherwise.
echo -e "${BOLD}[sdcard-provision] Ensuring a pristine NextZXOS SD image...${RESET}"
SD_DIR="$HOME/.jnext/sdcard"
FALLBACK_SD_IMAGE="$SD_DIR/cspect-next-1gb-fixed.img"
SD_WITNESS="$FALLBACK_SD_IMAGE.sha256"
mkdir -p "$SD_DIR"

sd_hash() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }

sd_rederive() {
    # The witness rm is belt-and-braces: sd_rederive rewrites it on every
    # success anyway. It matters only on the FAILURE path, where it stops a
    # witness describing an image that no longer exists from surviving.
    rm -f "$FALLBACK_SD_IMAGE" "$SD_WITNESS"
    "$JNEXT" --headless --sdcard-download-confirm --delayed-automatic-exit 2 >/dev/null 2>&1 || true
    [[ -f "$FALLBACK_SD_IMAGE" ]] || return 1
    printf '%s  %s\n' "$(sd_hash "$FALLBACK_SD_IMAGE")" "$(basename "$FALLBACK_SD_IMAGE")" > "$SD_WITNESS"
}

# 200 exec-lock (9) held only across the decision + any repair, never across the
# suite. flock(1) is in util-linux, present on every Linux CI image and dev box;
# if it is somehow absent, fall through unlocked rather than refuse to test.
sd_state=""
{
    flock 9 2>/dev/null || true
    if [[ ! -f "$FALLBACK_SD_IMAGE" || ! -f "$SD_WITNESS" ]]; then
        sd_rederive && sd_state="provisioned" || sd_state="failed"
    elif [[ "$(sd_hash "$FALLBACK_SD_IMAGE")" == "$(awk '{print $1}' "$SD_WITNESS")" ]]; then
        sd_state="pristine"
    else
        sd_rederive && sd_state="repaired" || sd_state="failed"
    fi
} 9>"$SD_DIR/.provision.lock"

case "$sd_state" in
    pristine)    printf "  "; pass_row ": unchanged since it was derived" ;;
    provisioned) printf "  "; pass_row ": derived $FALLBACK_SD_IMAGE" ;;
    repaired)    printf "  "; pass_row ": WAS MODIFIED — re-derived from the verified distribution image" ;;
    *)           printf "  "; fail_row ": provisioning failed — $FALLBACK_SD_IMAGE was not produced" ;;
esac
echo ""

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
