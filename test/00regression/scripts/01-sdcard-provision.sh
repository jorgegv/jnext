#!/usr/bin/env bash
# Group row: NextZXOS SD-image self-provisioning.
# Sourced by regression.sh (the driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --- SD-card image: gate the master, then clone it for THIS run ---
# Every screenshot test and several functional tests boot a NextZXOS SD image
# (the fallback, see SD_CARD_ARGS in test-functions.inc). This row runs once,
# before the screenshot rows launch in parallel — all of them would race
# otherwise.
#
# THE PROBLEM. The patched image is machine-wide MUTABLE state, and NextZXOS
# writes back to the card, so any jnext session that boots it changes what the
# whole suite tests against. On 2026-07-21, after an evening of manual NextZXOS
# booting, the image had 11012 bytes changed and its first-boot welcome screen
# disabled; a full run then reported 91 pass / 5 FAIL on a branch that was green
# — boot-nextzxos-welcome/-menu/-dotls, tape-save-boot-func and
# reset-to-nextzxos-func failing with screenshot diffs indistinguishable from a
# rendering regression. Re-deriving gave 96/0/0 on the same tree. A result that
# does not correspond to the thing under test is not a result; it is worse than
# none, because it is believed. Checking only that the file EXISTED could not
# see any of this.
#
# AND THE SUITE ITSELF IS A WRITER (GH #65). The warning above covers only
# *interactive* sessions, so the care everyone was taking could never have been
# sufficient: a regression run boots NextZXOS too, and dirties the image for the
# next run. Hence TWO mechanisms, doing different jobs:
#
#   1. THE HASH GATE (below) protects the MASTER at $HOME/.jnext/sdcard from
#      whatever happened between runs — an interactive session, a crash, a
#      half-written file. It re-derives only on drift. The image is a derived
#      artifact, not user content: jnext rebuilds it from the raw distribution
#      download in 1.9 s, and the result is byte-identical every time (fatfs is
#      built with FF_FS_NORTC=1, so its timestamps are a compile-time constant
#      and nothing in the output varies with the clock).
#
#      WHY A GATE, rather than simply re-deriving every run: rewriting a 1 GB
#      file every time would truncate it under a concurrent run's screenshot
#      rows — trading a visible problem for a rarer, harder one. The common path
#      does no writes at all: hash (0.75 s), and if it matches, touch nothing.
#      Repair is serialised with flock so two concurrent runs cannot both
#      rebuild the file. A missing witness re-derives rather than adopting
#      whatever is on disk — adopting would silently bless an already-corrupt
#      image and look like protection while providing none.
#
#   2. THE PER-RUN CLONE (sd_clone_for_run, test-functions.inc) makes THIS run
#      incapable of dirtying the master in the first place: it boots a private
#      reflink copy under $JNEXT_CONFIG_DIR, removed by the EXIT trap. That is
#      what makes concurrent runs independent, and it needs no lock.
#
# Deleting the patched copy is safe by construction, and the safety is jnext's
# rather than ours: per src/core/sdcard_provisioner.h, when the patched image is
# absent but the raw is present, jnext re-patches from the raw ONLY if the raw's
# SHA256 matches its .sha256 sidecar, and forces a full re-download otherwise.
echo -e "${BOLD}[sdcard-provision] Ensuring a pristine NextZXOS SD image...${RESET}"
SD_DIR="$SD_MASTER_DIR"
FALLBACK_SD_IMAGE="$SD_MASTER_IMAGE"
SD_WITNESS="$SD_MASTER_WITNESS"
mkdir -p "$SD_DIR"

sd_hash() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }

sd_rederive() {
    # The witness rm is belt-and-braces: sd_rederive rewrites it on every
    # success anyway. It matters only on the FAILURE path, where it stops a
    # witness describing an image that no longer exists from surviving.
    rm -f "$FALLBACK_SD_IMAGE" "$SD_WITNESS"
    # JNEXT_CONFIG_DIR is pinned to the MASTER's directory for this one
    # invocation. It points at the per-run clone dir everywhere else, and the
    # provisioner honours it (src/core/sdcard_provisioner.cpp) — without the
    # override, the repair would build a fresh image inside the run directory
    # and delete it again on exit, leaving the master broken forever.
    JNEXT_CONFIG_DIR="$HOME/.jnext" \
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

# Re-clone unconditionally: the gate may have just rewritten the master, and
# regression_lib_init's clone (made before the gate ran) would then be a copy of
# the drifted image. ~60 ms when reflinked, so there is no reason to be clever
# about when to skip it.
sd_clone_for_run || true

# Re-stamp the master AFTER the gate: a repair legitimately rewrites it, and
# sdcard-isolation-func's "nothing touched the master" assertion must measure
# from this point on, not from before the one write the suite is allowed to do.
# shellcheck disable=SC2034  # read by scripts/sdcard-isolation-func.sh
SD_MASTER_STAMP=$(stat -c '%i:%Y:%s' "$FALLBACK_SD_IMAGE" 2>/dev/null || echo "absent")

case "$sd_state" in
    pristine)    sd_verdict=pass; sd_text=": unchanged since it was derived" ;;
    provisioned) sd_verdict=pass; sd_text=": derived $FALLBACK_SD_IMAGE" ;;
    repaired)    sd_verdict=pass; sd_text=": WAS MODIFIED — re-derived from the verified distribution image" ;;
    *)           sd_verdict=fail; sd_text=": provisioning failed — $FALLBACK_SD_IMAGE was not produced" ;;
esac

# The clone is what the run actually boots, so a missing one is as fatal as a
# missing master — and it must FAIL the row rather than let every downstream row
# quietly fall back to the shared image.
case "$SD_CLONE_MODE" in
    reflink) sd_text+="; run image reflinked to $RUN_DIR/sdcard" ;;
    copy)    sd_text+="; run image COPIED (no reflink on this filesystem) to $RUN_DIR/sdcard" ;;
    *)       sd_verdict=fail; sd_text+="; per-run clone $SD_CLONE_MODE — this run would share the master" ;;
esac

printf "  "
if [[ "$sd_verdict" == pass ]]; then pass_row "$sd_text"; else fail_row "$sd_text"; fi
echo ""

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
