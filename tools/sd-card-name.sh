#!/usr/bin/env bash
# Print the file name of the default SD card in <dir> (GH #579): the new name when that file exists, else the old
# name when that exists, else the new name. The shell side of sdcard::card_name_in; the rule lives in
# sd_card_name_in (test/00regression/test-functions.inc), which reads both names from src/core/sdcard_provisioner.cpp.
#
# usage: sd-card-name.sh <dir>
set -uo pipefail
[[ $# -eq 1 ]] || { echo "usage: $0 <dir>" >&2; exit 2; }
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export JNEXT_REGRESSION_LIB_NO_INIT=1
# shellcheck source=test/00regression/test-functions.inc
source "$PROJECT_DIR/test/00regression/test-functions.inc"
sd_card_name_in "$1"
