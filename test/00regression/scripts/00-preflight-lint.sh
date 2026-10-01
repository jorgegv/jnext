#!/usr/bin/env bash
# Group rows: the six static preflight lints, always rows 1-6 of the suite.
# Sourced by regression.sh (the driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --- Tautological-assertion lint (fast fail on new offenders) ---
echo -e "${BOLD}[lint-assertions] Scanning test/ for tautological assertions...${RESET}"
CURRENT_ROW=lint-assertions
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
CURRENT_ROW=lint-traps
if bash "$SCRIPT_DIR/lint-traps.sh"; then
    printf "  "; pass_row ": no row script installs its own trap"
else
    printf "  "; fail_row ": a row script installs its own trap (see above)"
fi
echo ""

# --- unescalated-timeout lint ---
# `timeout N cmd` sends SIGTERM and nothing after it, so a command that does
# not act on SIGTERM runs unbounded while timeout reports 124 as if it had been
# stopped. Two jnext processes were found alive 9289 s after a `timeout 120`,
# reparented to systemd, burning a core apiece underneath the suite's
# real-time-pacing-bound rows — the class of runaway that fails nothing itself
# and makes OTHER rows lie. Commit d56ad276 swept two files by hand and fixed
# five call sites; three more were already in the tree elsewhere, one of them
# seven weeks old, and only a gate that looks at the WHOLE test tree finds
# those. Hence the scope: every tracked test/*.sh, wider than lint-traps,
# because a runaway started outside a row costs exactly the same.
echo -e "${BOLD}[lint-timeouts] Scanning test shell scripts for unescalated timeouts...${RESET}"
CURRENT_ROW=lint-timeouts
if bash "$PROJECT_DIR/test/lint-timeouts.sh"; then
    printf "  "; pass_row ": every 'timeout' escalates to SIGKILL"
else
    printf "  "; fail_row ": a test script runs 'timeout' with no escalation (see above)"
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
CURRENT_ROW=lint-paths
if bash "$PROJECT_DIR/test/lint-hardcoded-paths.sh"; then
    printf "  "; pass_row ": no owner-absolute paths in tracked code/config"
else
    printf "  "; fail_row ": a tracked file names one machine's home directory (see above)"
fi
echo ""

# --- published-debug-header include-graph lint (GH #276 B0) ---
# Epic #276's defining property is that a debugger frontend stops holding an
# `Emulator*`: src/debug/{debugger,events,inspect,result}.h are the whole API and
# nothing behind them may reach the emulator core, src/platform/, a toolkit, SDL
# or the debug internals design §3.1 keeps unpublished. Nothing checked it. The
# B0 review found the headers citing design §9's
# `grep -l 'core/emulator.h' src/debugger/*.cpp` as their gate — that greps the
# QT PANELS and says nothing about these headers; adding
# `#include "core/emulator.h"` to inspect.h compiled clean and no step noticed.
# Five downstream packages code against the property, so it gets a row.
echo -e "${BOLD}[lint-debug-headers] Checking the published debug headers' include graph...${RESET}"
CURRENT_ROW=lint-debug-headers
if bash "$PROJECT_DIR/test/lint-debug-headers.sh"; then
    printf "  "; pass_row ": no published debug header reaches a forbidden dependency"
else
    printf "  "; fail_row ": a published debug header reaches a forbidden dependency (see above)"
fi
echo ""

# --- quiet-grep-in-a-pipe lint ---
# Under pipefail, `producer | grep -q` reports NO MATCH for a line that is
# there whenever grep exits on its match while the producer is still writing:
# the producer dies of SIGPIPE and 141 becomes the pipeline's status. That is
# what made warm-start-func fail under load and pass solo; 47 lines across
# 23 test scripts carried the same pattern. Scope: every tracked test script
# that runs under pipefail, which includes every row script here.
echo -e "${BOLD}[lint-pipe-grepq] Scanning pipefail test scripts for a quiet grep at the end of a pipe...${RESET}"
CURRENT_ROW=lint-pipe-grepq
if bash "$PROJECT_DIR/test/lint-pipe-grepq.sh"; then
    printf "  "; pass_row ": no pipeline ends in a quiet grep"
else
    printf "  "; fail_row ": a pipeline ends in a quiet grep under pipefail (see above)"
fi
echo ""

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
