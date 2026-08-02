#!/usr/bin/env bash
# Group rows: the two static preflight lints, always rows 1-2 of the suite.
# Sourced by regression.sh (the driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --- Tautological-assertion lint (fast fail on new offenders) ---
echo -e "${BOLD}[lint-assertions] Scanning test/ for tautological assertions...${RESET}"
if bash "$PROJECT_DIR/test/lint-assertions.sh"; then
    printf "  "; pass_row ": no new tautological assertions"
else
    printf "  "; fail_row ": new tautological assertions detected (see above)"
fi
echo ""

# --- trap lint (GH #153) ---
# regression.sh SOURCES every row script into THIS shell, which already holds
# the harness's one `trap regression_cleanup EXIT/INT/TERM`. A second trap in
# any row silently replaces it and every SUCCESSFUL run then leaks its 1-2 GB
# $RUN_DIR while the count stays green. The GH #65 isolation rows cannot see
# it — they test a child shell, and this is a sibling clobbering the parent.
# A static grep costs well under a second and stops the naive and accidental
# case; it is NOT airtight (the lint header enumerates what it cannot decide).
echo -e "${BOLD}[lint-traps] Scanning regression row scripts for stray traps...${RESET}"
if bash "$SCRIPT_DIR/lint-traps.sh"; then
    printf "  "; pass_row ": no row script installs its own trap"
else
    printf "  "; fail_row ": a row script installs its own trap (see above)"
fi
echo ""

# --- owner-absolute-path lint (GH #204) ---
# The GH #204 sweep removed ~30 hardcoded home paths from tracked scripts and
# sources. Nothing in that fix was discriminative — the review proved it by
# re-running the pre-fix hook self-test, which passed, because the hardcoded
# literal names the maintainer's real checkout on the maintainer's machine.
# The bug class is invisible from the machine that has it, so a static grep is
# the only gate that can see it from here. It would have caught 29 lines across
# 26 files on the pre-fix tree.
echo -e "${BOLD}[lint-paths] Scanning tracked code/config for owner-absolute paths...${RESET}"
if bash "$PROJECT_DIR/test/lint-hardcoded-paths.sh"; then
    printf "  "; pass_row ": no owner-absolute paths in tracked code/config"
else
    printf "  "; fail_row ": a tracked file names one machine's home directory (see above)"
fi
echo ""

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
