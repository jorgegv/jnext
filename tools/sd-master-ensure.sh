#!/usr/bin/env bash
# Make the shared SD master (~/.jnext/sdcard/test-suite/sdcard/cspect-next-1gb-fixed.img, never the user's own card) the card THIS tree
# defaults to, before something reads it (GH #476).
#
# usage: sd-master-ensure.sh --probe          exit 0 when the master needs nothing, 10 when
#                                             it would be derived; writes nothing
#        sd-master-ensure.sh <jnext-binary>   run the gate; the binary derives the card when needed
#
# The decision and the repair are sd_master_gate in test/00regression/test-functions.inc, the
# code the regression row sdcard-provision runs (a master of another distribution or recipe, a
# drifted or witness-less one, or none, is re-derived; a current one is not touched), under the
# same provisioning lock. `make gui-release` (tools/pgo-train.sh) and the unit-test targets
# (the sdcard-image target) call it: a host left with the previous distribution's card used to
# fail their training and sd_rom_extractor_test until the regression suite ran.
# With JNEXT_REGRESSION_SD_STATE_DIR set (a candidate card) it does nothing. Needs bash 4.2+.
set -uo pipefail

[[ $# -eq 1 ]] || { echo "usage: $0 --probe | <jnext-binary>" >&2; exit 2; }
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ -z "${JNEXT_REGRESSION_SD_STATE_DIR:-}" ]] || exit 0
export JNEXT_REGRESSION_LIB_NO_INIT=1
# shellcheck source=test/00regression/test-functions.inc
source "$PROJECT_DIR/test/00regression/test-functions.inc"

if [[ "$1" == --probe ]]; then
    # The same gate with the derive replaced by a flag: nothing is removed or written.
    sd_rederive() { sd_would_derive=1; return 1; }
    sd_would_derive=0
    sd_master_gate
    [[ "$sd_would_derive" == 0 ]] || exit 10
    case "$sd_state" in nodistro|norecipe) echo "sd-master-ensure: $sd_state" >&2; exit 2 ;; esac
    exit 0
fi

JNEXT=$1
[[ -x "$JNEXT" ]] || { echo "sd-master-ensure: no executable $JNEXT" >&2; exit 2; }
sd_master_gate
case "$sd_state" in
    pristine)    echo "sd-master-ensure: SD master is current: $SD_MASTER_IMAGE" ;;
    provisioned) echo "sd-master-ensure: derived $SD_MASTER_IMAGE" ;;
    repaired)    echo "sd-master-ensure: SD master had drifted: re-derived $SD_MASTER_IMAGE" ;;
    distro)      echo "sd-master-ensure: SD master was made from $sd_old_distro: re-derived from $sd_want_distro" ;;
    recipe)      echo "sd-master-ensure: SD master was made by recipe ${sd_old_recipe:-(none)}: re-derived with recipe $sd_want_recipe" ;;
    wrongrecipe) echo "sd-master-ensure: ERROR: $JNEXT derived recipe '${sd_got_recipe:-}', this tree builds '$sd_want_recipe': it is not built from this tree" >&2; exit 1 ;;
    *)           echo "sd-master-ensure: ERROR: SD master not provisioned ($sd_state) at $SD_MASTER_IMAGE (network?)" >&2; exit 1 ;;
esac
