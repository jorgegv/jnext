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
# AND THE SUITE COULD DIRTY IT TOO (GH #65). The warning above covers only
# *interactive* sessions, so the care everyone was taking could never have been
# sufficient on its own: a regression run boots NextZXOS as well, and any row
# that does could in principle write back. Measured honestly, it does not — four
# full runs, plus a run instrumented to hash the booted image at cleanup, all
# left it byte-identical — so this is a RISK REMOVED, not an observed bug fixed.
#
# What WAS observed, directly: while GH #65 was under review, another worktree
# on this machine was running its own pre-#65 regression.sh against a different
# branch, visible in `ps aux`. On a shared box, non-isolated runs from other
# branches are live writers to the master, which accounts for the drift without
# needing the isolated suite to be one — and fits the timing (the drift's mtime
# landed well outside any one run's own window) better than an in-run write.
# Isolation removes the whole class regardless of which writer it was.
#
# Hence TWO mechanisms, doing different jobs:
#
#   1. THE HASH GATE (below) protects the MASTER at $HOME/.jnext/sdcard/test-suite from
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
#
# THE RECIPE (GH #284). The witness is written when the image is derived, so a
# hash gate alone can never notice that jnext now derives a DIFFERENT image:
# a master made by the old recipe still matches its own witness, and every row
# that needs the new content would fail on every existing dev box and CI cache.
# jnext therefore records the recipe that made the image in "<image>.recipe"
# (sdcard::kFixedImageRecipe), and the gate re-derives when that number is not
# the one this tree builds (sd_expected_recipe) — including when the sidecar is
# missing, as it is on every image made before recipes existed. After a
# re-derive the sidecar must carry the expected number, or the binary is not
# the one this tree describes and the row FAILS rather than bless its output.
#
# This is the TEST master only. jnext never rebuilds a user's image for being
# old: that image holds their own saved files (PROV-RECIPE-03).
echo -e "${BOLD}[sdcard-provision] Ensuring a pristine NextZXOS SD image...${RESET}"
# The gate itself (hash, recipe, distribution, repair, flock) is sd_master_gate in
# test-functions.inc, shared with tools/sd-master-ensure.sh, which `make gui-release` and the
# unit-test targets use. It sets sd_state and its sd_old_*/sd_want_*/sd_seam_why companions.
sd_master_gate

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
    pristine)    sd_verdict=pass; sd_text=": unchanged since it was derived"
                 [[ -z "${JNEXT_REGRESSION_SD_STATE_DIR:-}" ]] || sd_text=": candidate card unchanged since it was witnessed" ;;
    provisioned) sd_verdict=pass; sd_text=": derived $FALLBACK_SD_IMAGE" ;;
    repaired)    sd_verdict=pass; sd_text=": WAS MODIFIED — re-derived from the verified distribution image" ;;
    distro)      sd_verdict=pass; sd_text=": made from distribution $sd_old_distro - re-derived from the default $sd_want_distro" ;;
    recipe)      sd_verdict=pass; sd_text=": made by image recipe ${sd_old_recipe:-(none recorded)} — re-derived with recipe $sd_want_recipe" ;;
    adopted)     sd_verdict=pass; sd_text=": candidate card used as it is and witnessed (not re-derived: it has no witness yet, so it is taken as it is)" ;;
    seamfail)    sd_verdict=fail; sd_text=": candidate card: $sd_seam_why" ;;
    wrongrecipe) sd_verdict=fail; sd_text=": jnext derived an image of recipe '${sd_got_recipe:-}', expected '$sd_want_recipe' — $JNEXT is not built from this tree" ;;
    nodistro)    sd_verdict=fail; sd_text=": cannot read kDistroUrl from src/core/sdcard_provisioner.cpp" ;;
    norecipe)    sd_verdict=fail; sd_text=": cannot read kFixedImageRecipe from src/core/sdcard_provisioner.cpp" ;;
    *)           sd_verdict=fail; sd_text=": provisioning failed - $FALLBACK_SD_IMAGE was not produced: $(printf '%s' "${sd_derive_why:-no reason recorded}" | tr '\n' ';')" ;;
esac

# The clone is what the run actually boots, so a missing one is as fatal as a
# missing master — and it must FAIL the row rather than let every downstream row
# quietly fall back to the shared image.
case "$SD_CLONE_MODE" in
    reflink) sd_text+="; run image reflinked to $RUN_DIR/sdcard" ;;
    copy)    sd_text+="; run image COPIED (no reflink on this filesystem) to $RUN_DIR/sdcard" ;;
    *)       sd_verdict=fail; sd_text+="; per-run clone $SD_CLONE_MODE — this run would share the master" ;;
esac
# A clone without its distribution sidecars looks like a legacy card to jnext, which offers it the
# upgrade; a GUI run blocks on that box. Never present such a clone as usable (GH #476).
if [[ "${SD_CLONE_SIDECARS:-}" == missing ]]; then
    sd_verdict=fail
    sd_text+="; the run's card carries no distribution sidecars, so every Qt GUI run will stop at the offer of the default distribution"
fi

# A run on a candidate card (JNEXT_REGRESSION_SD_STATE_DIR) says so in its row.
[[ -z "${JNEXT_REGRESSION_SD_STATE_DIR:-}" ]] || sd_text+="; master in use $FALLBACK_SD_IMAGE (JNEXT_REGRESSION_SD_STATE_DIR)"

# The boot anchor (test-functions.inc, "The boot-ready helper"): the frame at which this card's
# firmware commits a machine type, measured once here on the run's clone and exported, so every
# row's bf shift is the same number. The DEFAULT master must give BOOT_DEFAULT_COMMIT: a change
# that moves the boot is a red row, never a shift the helper absorbs. A candidate card (the
# seam) may commit anywhere, and the row reports where. JNEXT_REGRESSION_BOOT_ANCHOR=off skips
# the measurement (the row sdcard-provision-func drives this script with a stub jnext whose
# every call it counts).
#
# The WebAssembly build has no debugger, so it cannot run the probe script. On the DEFAULT card it
# runs the same core as the native build, whose commit frame the Linux run of this row has already
# pinned at BOOT_DEFAULT_COMMIT, so a WebAssembly run takes that number instead of measuring it.
# A candidate card (the seam) may commit anywhere and cannot be measured here: that run FAILS.
if [[ "$sd_verdict" == pass && "${JNEXT_REGRESSION_BOOT_ANCHOR:-on}" != off && "${JNEXT_TARGET_OS:-linux}" == emscripten ]]; then
    if [[ -n "${JNEXT_REGRESSION_SD_STATE_DIR:-}" ]]; then
        sd_verdict=fail
        sd_text+="; a candidate card's commit frame cannot be measured without a debugger (the WebAssembly build has none): run the candidate natively"
    else
        export BOOT_COMMIT_MEASURED=$BOOT_DEFAULT_COMMIT
        sd_text+="; no debugger in the WebAssembly build: the boot anchor is the default card's pinned commit frame $BOOT_DEFAULT_COMMIT (boot-relative frames shift by $((BOOT_COMMIT_MEASURED - BOOT_REF_COMMIT)))"
    fi
elif [[ "$sd_verdict" == pass && "${JNEXT_REGRESSION_BOOT_ANCHOR:-on}" != off ]]; then
    if boot_c=$(boot_commit_measure); then
        export BOOT_COMMIT_MEASURED=$boot_c
        sd_text+="; firmware commits the machine type at frame $boot_c (reference $BOOT_REF_COMMIT): boot-relative frames shift by $((boot_c - BOOT_REF_COMMIT))"
        if [[ -z "${JNEXT_REGRESSION_SD_STATE_DIR:-}" && "$boot_c" != "$BOOT_DEFAULT_COMMIT" ]]; then
            sd_verdict=fail
            sd_text+=" - the default card must commit at frame $BOOT_DEFAULT_COMMIT: an emulator change moved the boot, or the master is not the default card"
        fi
    else
        sd_verdict=fail
        sd_text+="; the firmware never committed a machine type within 1500 frames"
    fi
fi

CURRENT_ROW=sdcard-provision
printf "  "
if [[ "$sd_verdict" == pass ]]; then pass_row "$sd_text"; else fail_row "$sd_text"; fi
echo ""

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
