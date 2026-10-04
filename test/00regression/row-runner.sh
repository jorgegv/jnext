#!/usr/bin/env bash
# One functional row of a regression run, in a process of its own (GH #295).
#
# Usage (driver only): row-runner.sh <row> <row-script>
#
# The driver (run_func_phase, parallel-rows.inc) starts one of these per row,
# with the row's own TMP_DIR / RUN_DIR / result file passed in the
# JNEXT_REGRESSION_ROW_* environment, which the suite library consumes
# (regression_lib_init, row-process mode). The row is SOURCED here, exactly as
# it used to be sourced into the driver: it still shares one shell with the
# library's trap and counters — this one — which is why lint-traps and the
# counter guard still apply. What it no longer shares is anything with the
# OTHER rows.
#
# Exit status: 0 with the result file written, 2 (and "<result>.fault") for a
# harness fault, anything else is the row's own death and the driver reports
# it as a FAIL of that row.
set -euo pipefail
row=$1
row_script=$2
# shellcheck source=test/00regression/test-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/test-functions.inc"
[[ -n "${ROW_RESULT_FILE:-}" ]] \
    || harness_fault "row-runner.sh was started without a result file — it is run by regression.sh, not by hand" \
                     "To run one row alone: ${BOLD}bash test/00regression/regression.sh $row${RESET}"

row_counters_snapshot "$row"
# shellcheck source=/dev/null
source "$row_script"
row_counters_check
write_row_result
