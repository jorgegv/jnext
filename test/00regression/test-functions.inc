#!/usr/bin/env bash
# shellcheck disable=SC2034  # variables set here are consumed by the sourcing scripts
# Shared helpers, constants and one-time environment setup for the regression
# suite. Sourced by test/00regression/regression.sh (the driver) and by
# standalone test scripts.
#
# Include guard: sourcing this file twice in one shell is a no-op. All
# initialization lives in regression_lib_init() — never in top-level side
# effects — and the function itself is idempotent, so an already-initialized
# shell keeps its state (same TMP_DIR, one EXIT trap, counters untouched).
# The guard variables are NOT exported: a child bash (e.g. the self-test
# re-invoking regression.sh) must initialize itself from scratch.

if [[ -n "${JNEXT_REGRESSION_LIB_SOURCED:-}" ]]; then
    return 0
fi
JNEXT_REGRESSION_LIB_SOURCED=1

# Colour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
RESET='\033[0m'

# pass_row/fail_row/skip_row <text> — print the coloured verdict followed by
# <text> and bump the one matching counter. Keeps verdict text and row
# accounting in lockstep at every reporting site.
#
# fail_row also records WHICH row failed and the host load at that moment, and
# flags the failure on the spot when the host was loaded (GH #245). The verdict
# and the counter are untouched: a FAIL under load is still a FAIL.
pass_row() { echo -e "${GREEN}PASS${RESET}${1-}"; pass=$((pass + 1)); }
fail_row() {
    echo -e "${RED}FAIL${RESET}${1-}"; fail=$((fail + 1))
    local load
    load=$(host_load1)
    FAILED_ROWS+=("${CURRENT_ROW:-unnamed row} (1-min load ${load:-unknown})")
    # The same fact in machine form, for the stamp (GH #295): only a FAIL on a
    # loaded host can ever become a solo-confirmed one.
    local fail_class=idle
    host_load_high "$load" && fail_class=loaded
    FAIL_RECORDS+=("${CURRENT_ROW:-unnamed-row} $fail_class")
    if host_load_high "$load"; then
        LOADED_FAILS=$((LOADED_FAILS + 1))
        echo -e "        ${YELLOW}^ host load ${load} on ${HOST_CPUS} CPUs when this row failed — re-run it SOLO before calling it real or dismissing it (GH #245)${RESET}"
    fi
}
skip_row() { echo -e "${YELLOW}SKIP${RESET}${1-}"; skip=$((skip + 1)); SKIPPED_ROWS+=("${CURRENT_ROW:-unnamed-row}"); }

# row_counters_snapshot <row> / row_counters_check — the guard the driver puts
# around every SOURCED functional row. The row's name is taken BEFORE it runs,
# so a row that clobbers the driver's loop variable is still named right.
#
# Rows are sourced into this shell, so a row that assigns `pass`, `fail` or
# `skip` for its own purposes clobbers the harness's counters. That happened:
# script-replay-edge-func did `skip=""`, which zeroed an earlier SKIP, so CI
# (where gdb-z88dk-func SKIPs) reported 214 of 215 rows and an EMPTY `Skip:`,
# while a local run (nothing skipped) stayed green. The check is on the EFFECT,
# after the row, so it does not care how the row did it (`skip=`, `local`,
# `read`, `printf -v`, `declare`, `(( skip-- ))`, a helper it called): all
# three counters must still be non-negative integers, none may go down, and
# together they must grow by exactly the row's results — 1 for a row that ran,
# 0 for one a name filter left out. A violation is a harness fault NAMING the
# row.
#
# What it cannot catch: a row that changes a counter and puts it back, or
# that trades one result for another in a way that keeps the sum (a FAIL turned
# into a PASS); clobbering of any OTHER harness global (TMP_DIR, FAILED_ROWS,
# the snapshot below itself); and a row run on its own with `bash row.sh`,
# which no driver wraps. Keep harness names out of rows: a row's own variables
# get a prefix of its own.
row_counters_snapshot() {
    ROW_GUARD_NAME=$1
    ROW_GUARD_PASS=$pass
    ROW_GUARD_FAIL=$fail
    ROW_GUARD_SKIP=$skip
}
row_counters_check() {
    local row=$ROW_GUARD_NAME results=1 v
    want "$row" || results=0
    for v in pass fail skip; do
        [[ "${!v-}" =~ ^[0-9]+$ ]] \
            || harness_fault "row ${BOLD}$row${RESET} left the harness counter ${BOLD}\$$v${RESET} as '${!v-<unset>}' (it was ${ROW_GUARD_PASS}/${ROW_GUARD_FAIL}/${ROW_GUARD_SKIP} pass/fail/skip before the row)" \
                             "Rows are SOURCED into the harness shell: a row must never assign pass, fail or skip. Give the row's variable a name of its own."
    done
    if (( pass < ROW_GUARD_PASS || fail < ROW_GUARD_FAIL || skip < ROW_GUARD_SKIP )); then
        harness_fault "row ${BOLD}$row${RESET} moved a harness counter BACK: pass/fail/skip ${ROW_GUARD_PASS}/${ROW_GUARD_FAIL}/${ROW_GUARD_SKIP} -> $pass/$fail/$skip" \
                      "Rows are SOURCED into the harness shell: a row must never assign pass, fail or skip."
    fi
    local grew=$(( pass + fail + skip - ROW_GUARD_PASS - ROW_GUARD_FAIL - ROW_GUARD_SKIP ))
    [[ $grew -eq $results ]] \
        || harness_fault "row ${BOLD}$row${RESET} reported ${BOLD}$grew${RESET} results; a row reports exactly $results (pass/fail/skip ${ROW_GUARD_PASS}/${ROW_GUARD_FAIL}/${ROW_GUARD_SKIP} -> $pass/$fail/$skip)"
}

# show_output <label> <text> [ERE] — call after a fail_row: print what a run
# said, indented under the verdict, so a failure can be diagnosed from the log
# alone ($TMP_DIR, where a row might have kept it, is gone by then). With an
# ERE, the matching lines (at most 20); without, the last 15. The size is
# printed too: whether a line is "missing" can depend on how much came before
# it. Writes to stdout, like the verdict, and never fails.
show_output() {
    local label=$1 text=$2 sel
    if [[ -n "${3-}" ]]; then
        sel=$(grep -E -- "$3" <<<"$text" || true)
        sel=$(head -n 20 <<<"$sel")
    else
        sel=$(tail -n 15 <<<"$text")
    fi
    echo "        --- $label (${#text} bytes) ---"
    [[ -n "$sel" ]] && sed -E 's/^/        | /' <<<"$sel"
    return 0
}

# ── Host load (GH #245) ──────────────────────────────────────────────────
#
# Under concurrent load some rows fail spuriously, and the class is wider than
# the real-time-paced rows: rows that merely spawn short-lived processes have
# failed and not reproduced solo. Which rows are sensitive used to be left to
# the reader's memory of a list in CLAUDE.md, so the harness now says it
# itself: it prints the load at the start and the end of the run, flags each
# FAIL that happened with the 1-minute load at or above the CPU count, and
# lists every failed row by name at the end.
#
# JNEXT_REGRESSION_LOADAVG_FILE / JNEXT_REGRESSION_NPROC override the two
# inputs, for the self-test (test/harness-selftest.sh HS-50..55).

# host_load1 — the 1-minute load average, or nothing if it cannot be read.
host_load1() {
    local l _rest
    if [[ ! -r "$LOADAVG_FILE" && "$(uname -s)" == Darwin ]]; then
        # macOS has no /proc: `sysctl -n vm.loadavg` prints "{ 1.23 4.56 7.89 }".
        { read -r _ l _rest < <(sysctl -n vm.loadavg); } 2>/dev/null || return 0
    else
        { read -r l _rest < "$LOADAVG_FILE"; } 2>/dev/null || return 0
    fi
    [[ "$l" =~ ^[0-9]+([.][0-9]+)?$ ]] && echo "$l"
    return 0
}

# host_load_high <load> — true when <load> is at least one runnable task per CPU.
host_load_high() {
    [[ -n "${1:-}" ]] || return 1
    awk -v l="$1" -v n="$HOST_CPUS" 'BEGIN { exit !(l + 0 >= n + 0) }'
}

# load_report <start|end> — print (and remember) the load at that point.
load_report() {
    local load
    load=$(host_load1)
    if [[ "$1" == start ]]; then LOAD_START=$load; else LOAD_END=$load; fi
    echo -e "  host load at $1: ${load:-unknown} (1-min) on ${HOST_CPUS} CPUs"
}

# load_summary — after the results line: every failed row by name, and a
# warning when any of them failed on a loaded host. Prints nothing on a clean
# run; never changes the verdict.
load_summary() {
    [[ "$fail" -gt 0 ]] || return 0
    echo -e "  ${BOLD}Failed rows:${RESET}"
    local row
    for row in "${FAILED_ROWS[@]+"${FAILED_ROWS[@]}"}"; do echo "    $row"; done
    if [[ "$LOADED_FAILS" -gt 0 ]] || host_load_high "${LOAD_START:-}" \
       || host_load_high "${LOAD_END:-}"; then
        echo -e "  ${YELLOW}${BOLD}WARNING: this run was on a loaded host${RESET}${YELLOW} (load ${LOAD_START:-?} at start, ${LOAD_END:-?} at end, ${HOST_CPUS} CPUs; ${LOADED_FAILS} FAIL(s) at or above that)."
        echo -e "  Rows that pace against real time or spawn short-lived processes can fail"
        echo -e "  spuriously under contention. Re-run each failed row SOLO, on a quieter host"
        echo -e "  (make regression-rows ROWS=<row> ROWS_PGO=1, the PGO binary a full make"
        echo -e "  regression tests; after a full make regression whose only"
        echo -e "  FAILs were flagged like this, make regression-confirm does it and stamps the"
        echo -e "  run if they pass), before treating it as a"
        echo -e "  regression — and before dismissing it: a row that also fails solo is real."
        echo -e "  The FAIL verdict above stands either way.${RESET}"
    fi
    return 0
}

# A harness fault is not a test failure: it means the suite cannot be trusted to
# have run what it claims. Exit 2, loudly, and run nothing further.
#
# In a row process of a parallel run (GH #295) the fault is also written to
# "<result>.fault", so the driver reports it as the harness fault it is rather
# than mistaking the exit status for a crashed row.
harness_fault() {
    echo ""
    echo -e "${RED}${BOLD}=== REGRESSION HARNESS FAULT ===${RESET}"
    for msg in "$@"; do echo -e "  $msg"; done
    echo ""
    if [[ -n "${ROW_RESULT_FILE:-}" ]]; then
        printf '%s\n' "$@" > "$ROW_RESULT_FILE.fault" 2>/dev/null || true
    fi
    exit 2
}

# write_row_result — the last act of a row process (row-runner.sh): hand the
# row's accounting to the driver. Written to a temp name and renamed, so the
# driver never reads half a file. One `key value` per line; `reported` and
# `failed` repeat.
write_row_result() {
    local f="$ROW_RESULT_FILE.tmp" name row
    {
        echo "pass $pass"
        echo "fail $fail"
        echo "skip $skip"
        echo "loaded $LOADED_FAILS"
        for name in "${REPORTED_FUNC[@]+"${REPORTED_FUNC[@]}"}"; do echo "reported $name"; done
        for row in "${FAILED_ROWS[@]+"${FAILED_ROWS[@]}"}"; do echo "failed $row"; done
        for row in "${FAIL_RECORDS[@]+"${FAIL_RECORDS[@]}"}"; do echo "failrec $row"; done
        for row in "${SKIPPED_ROWS[@]+"${SKIPPED_ROWS[@]}"}"; do echo "skipped $row"; done
    } > "$f"
    mv -f "$f" "$ROW_RESULT_FILE"
}

# regression_cleanup — the one EXIT handler. Removes the scratch dir and this
# run's private state directory (which holds the ~1 GB SD-image clone). Guarded
# against an empty variable so it can never become `rm -rf /` or `rm -rf ""`.
#
# A parallel run (GH #295) first stops the row processes still running:
# without that a Ctrl-C would delete the run's SD clone under them, and their
# jnext children would run on until their own bounds. Each row runs under
# `timeout`, which forwards the TERM to the row's whole process group and
# escalates to KILL after its --kill-after, so the wait is bounded.
regression_cleanup() {
    local p
    for p in ${ROW_RUNNING_PIDS:-} ${HEARTBEAT_PID:-}; do kill -TERM "$p" 2>/dev/null || true; done
    for p in ${ROW_RUNNING_PIDS:-} ${HEARTBEAT_PID:-}; do wait "$p" 2>/dev/null || true; done
    # A Windows platform run (regression.sh --platform) started a persistent
    # wineserver; stop it, which also reaps any wine child a row's timeout
    # could not. Set by the driver only, never exported to a row process.
    [[ -n "${REG_WINE_PREFIX:-}" ]] && WINEPREFIX="$REG_WINE_PREFIX" wineserver -k >/dev/null 2>&1
    [[ -n "${TMP_DIR:-}" ]] && rm -rf "$TMP_DIR"
    [[ -n "${RUN_DIR:-}" ]] && rm -rf "$RUN_DIR"
    # The shared parent $HOME/.jnext/runs is NEVER removed, by this or any other script: only a run's
    # own directory is. A run that pruned the parent when it ended raced every concurrent run's
    # `mkdir -p`/`mktemp -d`/QTemporaryDir under it (ENOENT, a lost clone), and a parent that is
    # never deleted cannot be lost at any site, present or future.
    return 0
}

# sd_clone_for_run — give THIS run a private copy of the master SD image at
# $RUN_DIR/sdcard/, the location jnext's provisioner resolves from
# $JNEXT_CONFIG_DIR. Idempotent: safe to call again after the master has been
# re-derived.
#
# reflink first, explicitly (`--reflink=always`), so the outcome is OBSERVABLE
# in $SD_CLONE_MODE rather than guessed: on a copy-on-write filesystem the
# clone is instant and free, and anywhere else it degrades to a plain copy that
# still lands on the same filesystem as the master (never in RAM).
# Sets SD_CLONE_MODE to reflink | copy | absent | failed.
# Sets SD_CLONE_SIDECARS to none | missing | ok: missing = the image was copied but the
# distribution sidecars were not written (the default distribution could not be read), so a GUI
# run on this card meets the upgrade offer and blocks.
sd_clone_for_run() {
    SD_CLONE_MODE=absent
    SD_CLONE_SIDECARS=none
    [[ -f "$SD_MASTER_IMAGE" ]] || return 1
    local dst="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    mkdir -p "$RUN_DIR/sdcard" || { SD_CLONE_MODE=failed; return 1; }
    rm -f "$dst"
    if cp --reflink=always "$SD_MASTER_IMAGE" "$dst" 2>/dev/null; then
        SD_CLONE_MODE=reflink
    elif cp --reflink=auto "$SD_MASTER_IMAGE" "$dst" 2>/dev/null; then
        SD_CLONE_MODE=copy
    else
        SD_CLONE_MODE=failed
        return 1
    fi
    # The clone is a card jnext opens by its default path, so jnext would OFFER it the upgrade to
    # the default distribution when it is another one (a GUI run blocks on that box). Rows never
    # answer it: the clone carries the master's distribution sidecar, and when that is not the
    # default distribution it also carries "do not ask again" for the default one (GH #476).
    local want_dist have_dist="sn-emulator-24.11.zip"
    want_dist=$(sd_expected_distro) || { SD_CLONE_SIDECARS=missing; return 1; }
    if [[ -f "$SD_MASTER_IMAGE.distro" ]]; then
        have_dist=$(head -n1 "$SD_MASTER_IMAGE.distro" | tr -d '[:space:]')
        cp "$SD_MASTER_IMAGE.distro" "$dst.distro"
    fi
    rm -f "$dst.distro-declined"
    [[ "$have_dist" == "$want_dist" ]] || printf '%s\n' "$want_dist" > "$dst.distro-declined"
    SD_CLONE_SIDECARS=ok
    return 0
}

# sd_card_sidecars <src.img> <dst.img>: a row that makes a config directory of its own and copies
# the card into it copies the card's distribution sidecars too (see sd_clone_for_run), or a GUI run
# in that directory is blocked on the offer of a newer distribution.
sd_card_sidecars() {
    local sc
    for sc in distro distro-declined; do
        [[ ! -f "$1.$sc" ]] || cp "$1.$sc" "$2.$sc" || return 1
    done
}

# sd_expected_distro: the zip name of the default distribution, from its one definition
# (sdcard::kDistroUrl in src/core/sdcard_provisioner.cpp: the line after the declaration).
# Fails, printing nothing, unless exactly one name comes out.
sd_expected_distro() {
    local src="$PROJECT_DIR/src/core/sdcard_provisioner.cpp" n
    n=$(sed -n '/^const char\* const kDistroUrl =$/{n;s#.*/\([^/"]*\)";$#\1#p;}' "$src" 2>/dev/null)
    [[ -n "$n" && "$n" != *$'\n'* ]] || return 1
    printf '%s\n' "$n"
}

# sd_expected_recipe — print the fixed-image recipe number the jnext under test
# derives (GH #284). Read from its one definition, sdcard::kFixedImageRecipe
# in src/core/sdcard_provisioner.cpp: the binary is built from this same tree,
# so no second copy of the number exists to drift. Fails (prints nothing) unless
# exactly one line matches, so a reworded definition is loud, never a guess.
sd_expected_recipe() {
    local src="$PROJECT_DIR/src/core/sdcard_provisioner.cpp" pat='^const int kFixedImageRecipe = [0-9]+;$'
    [[ "$(grep -cE "$pat" "$src" 2>/dev/null)" == 1 ]] || return 1
    grep -E "$pat" "$src" | grep -oE '[0-9]+'
}

# ---- The SD master gate: shared by 01-sdcard-provision.sh and tools/sd-master-ensure.sh ----
# sd_master_paths: where the master card and its sidecars live (the candidate-card seam,
# JNEXT_REGRESSION_SD_STATE_DIR, moves them). Sets the SD_MASTER_* names and the short aliases the gate uses.
sd_master_paths() {
    SD_MASTER_DIR="${JNEXT_REGRESSION_SD_STATE_DIR:-$HOME/.jnext}/sdcard"
    SD_MASTER_IMAGE="$SD_MASTER_DIR/cspect-next-1gb-fixed.img"
    SD_MASTER_WITNESS="$SD_MASTER_IMAGE.sha256"
    # Written by jnext itself when it derives the image: the number of the
    # recipe that made it (sdcard::kFixedImageRecipe, GH #284). Absent on any
    # image derived before recipes existed.
    SD_MASTER_RECIPE="$SD_MASTER_IMAGE.recipe"
    SD_DIR="$SD_MASTER_DIR"
    FALLBACK_SD_IMAGE="$SD_MASTER_IMAGE"
    SD_WITNESS="$SD_MASTER_WITNESS"
    SD_RECIPE="$SD_MASTER_RECIPE"
}

sd_hash() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }
# The recipe number jnext recorded for the master, "" when it recorded none.
# Always succeeds: it is used in assignments, where a failing substitution
# would trip `set -e` on exactly the images this exists to catch.
sd_recipe() { { head -n1 "$SD_RECIPE" 2>/dev/null || true; } | tr -d '[:space:]'; }

# sd_seam_raw_distro: the zip name the seam dir's raw came from: its .distro sidecar, else the
# legacy 24.11 name (what jnext itself reads a raw with no sidecar as).
sd_seam_raw_distro() {
    local side="$SD_DIR/cspect-next-1gb.img.distro" d=""
    [[ ! -f "$side" ]] || d=$(head -n1 "$side" | tr -d '[:space:]')
    echo "${d:-sn-emulator-24.11.zip}"
}

# sd_master_distro: the zip name the master was made from: its .distro sidecar, else the legacy
# 24.11 name (a master made before distributions were recorded).
sd_master_distro() {
    local d=""
    [[ ! -f "$FALLBACK_SD_IMAGE.distro" ]] || d=$(head -n1 "$FALLBACK_SD_IMAGE.distro" | tr -d '[:space:]')
    echo "${d:-sn-emulator-24.11.zip}"
}

sd_rederive() {
    # The witness rm is belt-and-braces: sd_rederive rewrites it on every
    # success anyway. It matters only on the FAILURE path, where it stops a
    # witness describing an image that no longer exists from surviving.
    rm -f "$FALLBACK_SD_IMAGE" "$SD_WITNESS" "$SD_RECIPE" "$FALLBACK_SD_IMAGE.distro"
    # JNEXT_CONFIG_DIR is pinned to the MASTER's directory for this one
    # invocation. It points at the per-run clone dir everywhere else, and the
    # provisioner honours it (src/core/sdcard_provisioner.cpp) — without the
    # override, the repair would build a fresh image inside the run directory
    # and delete it again on exit, leaving the master broken forever.
    # JNEXT_NATIVE (GH #319): a platform run for Windows derives the image with a
    # jnext of the HOST (the download is a network job, not worth running through
    # wine); unset, the binary under test does it, as before.
    # With the candidate-card seam the derive is offline by construction: it only runs
    # from a raw the sidecar verifies (sd_seam_raw_ok), and a download address that
    # cannot be fetched makes any other path fail instead of fetching the default
    # distribution into the candidate's directory. The address ENDS IN THE RAW'S OWN
    # DISTRIBUTION NAME (its .distro sidecar, else sn-emulator-24.11.zip): jnext reuses a raw
    # only when the distribution it was downloaded from is the one the address names (GH #476),
    # so any other name makes it refuse the verified raw and try the download.
    local sd_env=("JNEXT_CONFIG_DIR=${JNEXT_REGRESSION_SD_STATE_DIR:-$HOME/.jnext}")
    [[ -z "${JNEXT_REGRESSION_SD_STATE_DIR:-}" ]] \
        || sd_env+=("JNEXT_SDCARD_DISTRO_URL=file:///nonexistent-candidate-card-never-download/$(sd_seam_raw_distro)")
    # A download that hangs must not hang the caller (make gui-release, make unit-test): bound it.
    local sd_bound=()
    ! command -v timeout >/dev/null 2>&1 || sd_bound=(timeout --kill-after=5s 1200s)
    env "${sd_env[@]}" ${sd_bound[@]+"${sd_bound[@]}"} \
        "${JNEXT_NATIVE:-$JNEXT}" --headless --sdcard-download-confirm --delayed-automatic-exit 2 >/dev/null 2>&1 || true
    [[ -f "$FALLBACK_SD_IMAGE" ]] || return 1
    # No witness for an image of the wrong recipe: the next run re-derives it
    # instead of adopting it.
    sd_got_recipe=$(sd_recipe)
    [[ "$sd_got_recipe" == "$sd_want_recipe" ]] || return 2
    printf '%s  %s\n' "$(sd_hash "$FALLBACK_SD_IMAGE")" "$(basename "$FALLBACK_SD_IMAGE")" > "$SD_WITNESS"
}

# sd_derive_as <state> — re-derive, and name the outcome: <state> on success,
# "wrongrecipe" when jnext derived an image of another recipe, else "failed".
sd_derive_as() {
    local rc=0
    sd_rederive || rc=$?
    case $rc in 0) sd_state=$1 ;; 2) sd_state=wrongrecipe ;; *) sd_state=failed ;; esac
}
# sd_seam_raw_ok: 0 when the seam dir holds the raw distribution image and its sidecar
# names that image's sha256 (what jnext itself checks before it reuses a raw).
sd_seam_raw_ok() {
    local raw="$SD_DIR/cspect-next-1gb.img" side="$SD_DIR/cspect-next-1gb.img.sha256" want
    [[ -f "$raw" && -f "$side" ]] || return 1
    want=$(awk '{print $1; exit}' "$side")
    [[ -n "$want" && "$(sd_hash "$raw")" == "$want" ]] || return 1
    # The offline derive names the raw's distribution in its address: only a plain file name can be one.
    [[ "$(sd_seam_raw_distro)" =~ ^[A-Za-z0-9._-]+$ ]]
}
# The candidate-card gate (JNEXT_REGRESSION_SD_STATE_DIR set). The candidate is the thing
# under evaluation, so unlike the default master it is never deleted unless a verified raw
# can rebuild it, and nothing is downloaded. Without a verified raw: a card with no
# witness is adopted as it is (and witnessed); a missing card, or one that drifted from
# its witness, FAILS the row and says why.
sd_seam_gate() {
    local have_raw=1
    sd_seam_raw_ok || have_raw=0
    if [[ ! -f "$FALLBACK_SD_IMAGE" ]]; then
        if [[ $have_raw == 1 ]]; then sd_derive_as provisioned
        else sd_state=seamfail; sd_seam_why="no card in the seam dir and no raw whose sidecar verifies, so it cannot be derived (nothing is downloaded)"; fi
    elif [[ ! -f "$SD_WITNESS" ]]; then
        printf '%s  %s\n' "$(sd_hash "$FALLBACK_SD_IMAGE")" "$(basename "$FALLBACK_SD_IMAGE")" > "$SD_WITNESS"
        sd_state=adopted
    elif [[ "$(sd_hash "$FALLBACK_SD_IMAGE")" != "$(awk '{print $1}' "$SD_WITNESS")" ]]; then
        if [[ $have_raw == 1 ]]; then sd_derive_as repaired
        else sd_state=seamfail; sd_seam_why="the card drifted from its witness and there is no raw whose sidecar verifies to rebuild it from (the card is left as it is)"; fi
    elif [[ "$(sd_recipe)" != "$sd_want_recipe" && $have_raw == 1 ]]; then
        sd_old_recipe=$(sd_recipe); sd_derive_as recipe
    else
        sd_state=pristine
    fi
}

# sd_master_gate: decide what the master needs and do it, under the provisioning lock. Sets
# sd_state (pristine, provisioned, repaired, distro, recipe, adopted, or a failure: seamfail,
# wrongrecipe, nodistro, norecipe, failed) and sd_old_recipe, sd_old_distro, sd_want_recipe,
# sd_want_distro, sd_got_recipe, sd_seam_why. Needs $JNEXT (or $JNEXT_NATIVE) only when it derives.
sd_master_gate() {
    sd_master_paths
    sd_want_recipe=$(sd_expected_recipe) || sd_want_recipe=""
    mkdir -p "$SD_DIR"
    sd_state=""
    sd_old_recipe=""
    sd_old_distro=""
    sd_want_distro=$(sd_expected_distro) || sd_want_distro=""
    sd_seam_why=""
{
    flock 9 2>/dev/null || true
    if [[ -z "$sd_want_recipe" ]]; then
        sd_state="norecipe"
    elif [[ -z "$sd_want_distro" ]]; then
        sd_state="nodistro"
    elif [[ -n "${JNEXT_REGRESSION_SD_STATE_DIR:-}" ]]; then
        sd_seam_gate
    elif [[ ! -f "$FALLBACK_SD_IMAGE" || ! -f "$SD_WITNESS" ]]; then
        sd_derive_as provisioned
    elif [[ "$(sd_master_distro)" != "$sd_want_distro" ]]; then
        # The default distribution moved (GH #476): a master of another one is replaced, however
        # healthy its witness and recipe, or every boot row runs on the wrong card.
        sd_old_distro=$(sd_master_distro)
        sd_derive_as distro
    elif [[ "$(sd_recipe)" != "$sd_want_recipe" ]]; then
        sd_old_recipe=$(sd_recipe)
        sd_derive_as recipe
    elif [[ "$(sd_hash "$FALLBACK_SD_IMAGE")" == "$(awk '{print $1}' "$SD_WITNESS")" ]]; then
        sd_state="pristine"
    else
        sd_derive_as repaired
    fi
} 9>"$SD_DIR/.provision.lock"
}

# ---- The boot-ready helper: boot-timeline frames anchored on the machine-type commit ----
# A row that presses a key, takes a screenshot or resets "at frame N of a cold boot" must mean
# the same moment of the boot on every card, and a new NextZXOS distribution loads its firmware
# for a different number of frames before it reaches the same state. The anchor is the FIRMWARE'S
# MACHINE-TYPE COMMIT: the first NR 0x03 write whose bits 2:0 are 001..100, which by zxnext.vhd
# :5708-5712 is the write that clears config mode. It happens on every firmware boot whatever
# personality config.ini selects, and boot-commit-probe.jds reports its frame in 2 s.
#   BOOT_REF_COMMIT      the commit frame of the boot the row literals were written against.
#   BOOT_DEFAULT_COMMIT  the frame the DEFAULT master must give (01-sdcard-provision.sh fails on
#                        any other, so an emulator change that moves the boot is a red row and
#                        never a shift the helper quietly absorbs). A candidate card reports its own.
#                        476 is the 26.11a firmware's; the row literals were written against the
#                        26.11 boot (472), so every @bf literal shifts by +4 on the default card
#                        and no literal changed when 26.11a became the default.
#   bf N [K]             N + K * (measured - BOOT_REF_COMMIT). K = 2 for a frame after a hard reset
#                        that runs the firmware again. With the reference card the shift is 0 and
#                        every adopted row's command line is byte-identical to the literal one.
BOOT_REF_COMMIT=472
BOOT_DEFAULT_COMMIT=476

# boot_commit_measure: run the probe on this run's card clone; print the commit frame, or fail.
boot_commit_measure() {
    local out
    out=$(LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
              ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --rtc 2026-07-10T08:55:00 \
              --script "$SCRIPT_DIR/boot-commit-probe.jds" --delayed-automatic-exit-frames 1500 </dev/null 2>&1 || true)
    out=$(sed -n 's/.*BOOT-COMMIT FRAME=\([0-9][0-9]*\).*/\1/p' <<< "$out" | head -1)
    [[ -n "$out" ]] || return 1
    echo "$out"
}
# boot_commit: the measured commit frame: $BOOT_COMMIT_MEASURED (exported by the provision row),
# else the cache in $RUN_DIR, else a fresh measurement. Unmeasurable = harness fault.
boot_commit() {
    local f="${RUN_DIR:-$TMP_DIR}/boot-commit" m
    if [[ -n "${BOOT_COMMIT_MEASURED:-}" ]]; then echo "$BOOT_COMMIT_MEASURED"; return 0; fi
    if [[ -s "$f" ]]; then cat "$f"; return 0; fi
    m=$(boot_commit_measure) || { harness_fault "the firmware never committed a machine type within 1500 frames (boot-commit-probe.jds)" \
                                              "Every boot-relative frame (bf) is anchored on it." >&2; }
    echo "$m" > "$f"
    echo "$m"
}
boot_shift() { local c; c=$(boot_commit) || exit 2; echo $(( c - BOOT_REF_COMMIT )); }
# bf N [K]: see above. A bad N or K is a harness fault, never a pass-through.
bf() {
    local n=${1:-} k=${2:-1} sh v
    [[ "$n" =~ ^[0-9]+$ && "$k" =~ ^[0-9]+$ ]] \
        || { harness_fault "bf: '${n}' '${k}' are not non-negative integers" >&2; }
    sh=$(boot_shift) || exit 2
    v=$(( n + k * sh ))
    (( v >= 0 )) || { harness_fault "bf $n $k: shift $sh gives a negative frame" >&2; }
    echo "$v"
}
# bf_expand <word>: expand a conf token @bf:N or @bf:N:K; any other word passes unchanged.
bf_expand() {
    local w=$1
    if [[ "$w" == @bf:* ]]; then
        local spec=${w#@bf:} n k
        n=${spec%%:*}; k=1
        [[ "$spec" == *:* ]] && k=${spec#*:}
        bf "$n" "$k"
    else
        echo "$w"
    fi
}
# bf_keys <frame key>...: set BF_KEYS to the --delayed-keypress-frames arguments of those pairs, every
# frame a boot-timeline frame (bf). Not a subshell: the array is the caller's.
bf_keys() {
    local v
    BF_KEYS=()
    while (( $# >= 2 )); do
        v=$(bf "$1") || exit 2
        BF_KEYS+=(--delayed-keypress-frames "$v" "$2")
        shift 2
    done
}
# bf_jds <template> <out>: expand {{bf N}} and {{bf N K}} in a .jds.in template; a placeholder
# left over is a harness fault.
bf_jds() {
    local in=$1 out=$2 line pre rest spec n k v
    : > "$out"
    while IFS= read -r line || [[ -n "$line" ]]; do
        while [[ "$line" == *"{{bf "*"}}"* ]]; do
            pre=${line%%\{\{bf *}; rest=${line#*\{\{bf }
            spec=${rest%%\}\}*}; rest=${rest#*\}\}}
            n=${spec%% *}; k=1
            [[ "$spec" == *" "* ]] && k=${spec#* }
            v=$(bf "$n" "$k") || exit 2
            line="$pre$v$rest"
        done
        printf '%s\n' "$line" >> "$out"
    done < "$in"
    if grep -q '{{' "$out"; then
        harness_fault "bf_jds: an unexpanded placeholder is left in $out (from $in)" >&2
    fi
}

# menu_index <image> <mode> <rom>: the `default=` value of the card's menu line with that mode (2nd
# field) and ROM (4th). The firmware (TBBlue config.c:345-405) reads machines/next/menu.ini, else
# menu.def, counts only `menu=` lines whose ROM field is non-empty, and `default=N` names the N-th of
# those. A distribution can insert lines, so a row asks the card's own file instead of carrying a
# number. Prints the index; fails (no output) when the file is missing, nothing matches, or the
# (mode, ROM) key is not unique. `sdfile_tool` is $TEST_BIN_DIR/sdfile_tool.
menu_index() {
    local img=$1 mode=$2 rom=$3 f="$TMP_DIR/menu_index.def" n=0 hit="" line title m tm r
    "$TEST_BIN_DIR/sdfile_tool" get "$img" machines/next/menu.ini "$f" >/dev/null 2>&1 ||
        "$TEST_BIN_DIR/sdfile_tool" get "$img" machines/next/menu.def "$f" >/dev/null 2>&1 || return 1
    while IFS= read -r line || [[ -n "$line" ]]; do
        line=${line%$'\r'}
        [[ "$line" == menu=* ]] || continue
        IFS=, read -r title m tm r _ <<<"${line#menu=}"
        [[ -n "$r" ]] || continue
        if [[ "$m" == "$mode" && "$r" == "$rom" ]]; then [[ -z "$hit" ]] || return 1; hit=$n; fi
        n=$((n + 1))
    done <"$f"
    [[ -n "$hit" ]] && echo "$hit"
}

declared_count() {   # declared_count <conf>  — non-comment, non-blank lines
    grep -cvE '^[[:space:]]*(#|$)' "$1" || true
}
pinned_count() {     # pinned_count <conf>  — the `# expect: N` line
    # `|| true` is load-bearing: grep exits 1 when there is no pin line, and
    # under `set -e` + pipefail that would kill the script AT THE ASSIGNMENT,
    # before the driver's "No '# expect: N' pin" fault could print.
    grep -oP '^#\s*expect:\s*\K[0-9]+' "$1" 2>/dev/null | head -1 || true
}

# want <name> — should this test run? (no filter given, or explicitly named)
want() {
    [[ ${#FILTER_TESTS[@]} -eq 0 ]] && return 0
    [[ -n "${IS_FILTERED[$1]:-}" ]]
}

# begin_func <name> — register the row and print its label.
begin_func() {
    REPORTED_FUNC+=("$1")
    CURRENT_ROW=$1
    printf "  %-25s " "[$1]"
}

# standalone_summary — totals line for a directly-executed test script; exits
# non-zero if any row failed or was SKIPPED. (The driver prints its own results block instead.)
standalone_summary() {
    echo ""
    echo -e "  ${GREEN}Pass: $pass${RESET}  ${RED}Fail: $fail${RESET}  ${YELLOW}Skip: $skip${RESET}"
    load_report end
    load_summary
    if [[ "$skip" -gt 0 ]]; then
        echo -e "  ${RED}${BOLD}$skip row(s) SKIPPED, i.e. not tested — a SKIP is not a pass${RESET}"
        exit 1
    fi
    [[ "$fail" -eq 0 ]] || exit 1
}

# shot_wall_bound <exit_delay_s> -- the wall-clock bound, in seconds, of one screenshot
# row: its emulated exit delay plus a 5 s buffer, times 4, times SHOT_WALL_FACTOR
# (1 unless regression.sh widens it for a slower runtime, the WebAssembly run).
shot_wall_bound() { echo $(( ($1 + 5) * 4 * ${SHOT_WALL_FACTOR:-1} )); }

# png_diff <a> <b> [sentinel] — count pixels whose channels differ exactly.
#
# Do not use `compare -metric AE` here. ImageMagick 7.1.2 Q16-HDRI reports
# a quantum-scaled absolute-error sum for that metric (one black -> white
# pixel is 65535), not a pixel count. Build an any-channel binary difference
# mask instead, then its mean times its area is the literal number of changed
# pixels. A command or parse failure yields <sentinel> (default 999999, i.e.
# "treat as different"), as does a pixel-dimension mismatch between the two
# images (GH #85): -composite silently crops to the overlap, so two images of
# different sizes could otherwise compare as identical — and a dimension
# mismatch can only mean harness/renderer breakage.
png_diff() {
    local raw dims a_dim b_dim sentinel=${3:-999999}
    local -a image_cmd
    if command -v magick &>/dev/null; then
        image_cmd=(magick)
    elif command -v convert &>/dev/null; then
        image_cmd=(convert)
    else
        echo "$sentinel"
        return
    fi

    # IHDR dimension check first: "<w>x<h>;<w>x<h>;" for the two images.
    # Single-frame PNGs assumed (every producer here is jnext's own
    # --delayed-screenshot); a multi-frame input would emit extra
    # ';'-separated fields and misalign b_dim (GH #85 review note).
    dims=$("${image_cmd[@]}" "$1" "$2" -format '%wx%h;' info: 2>/dev/null) || {
        echo "$sentinel"
        return
    }
    a_dim=${dims%%;*}
    b_dim=${dims#*;}; b_dim=${b_dim%%;*}
    if [[ ! "$a_dim" =~ ^[0-9]+x[0-9]+$ || "$a_dim" != "$b_dim" ]]; then
        echo "$sentinel"
        return
    fi

    raw=$("${image_cmd[@]}" "$1" "$2" \
        -compose difference -composite -threshold 0 \
        -separate -evaluate-sequence max \
        -format '%[fx:mean*w*h]' info: 2>/dev/null) || {
        echo "$sentinel"
        return
    }
    if [[ "$raw" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        printf '%.0f\n' "$raw"
    else
        echo "$sentinel"
    fi
}

# count_streams <file> <codec_type> — number of streams of that type in the
# container (one ffprobe execution per call).
count_streams() {
    ffprobe -show_streams "$1" 2>/dev/null | grep -c "codec_type=$2" || true
}

# regression_lib_init — one-time environment setup shared by the driver and by
# standalone test scripts. Idempotent: a second call returns immediately.
regression_lib_init() {
    [[ -n "${JNEXT_REGRESSION_LIB_INITIALIZED:-}" ]] && return 0
    JNEXT_REGRESSION_LIB_INITIALIZED=1

    # Pin the locale for the whole suite: assertions grep C-locale strerror()
    # text ("No such file or directory"), Qt's QApplication constructor calls
    # setlocale(LC_ALL, "") which would localise it, and ImageMagick's
    # `compare` must emit a C-locale decimal point for the pixel-diff parse.
    # The suite's result must not depend on the user's LANG.
    export LC_ALL=C

    # Repo root is resolved from THIS file's own location (test/00regression),
    # so a standalone test script sourcing the lib needs no path knowledge.
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

    # Locate the jnext executable. Prefer gui-release (fastest), then
    # gui-debug, then the canonical build/. The first existing executable
    # wins. Override with JNEXT=... in the environment to bypass the search.
    #
    # MUTATION-TESTING A ROW? READ THIS FIRST. The preference above means a
    # stale build/gui-release/jnext SHADOWS the build/jnext you just rebuilt:
    # `cmake --build build` does not touch gui-release, so a mutation compiled
    # into build/jnext is never executed and the row passes — which reads
    # exactly like "this row does not discriminate". It cost a reviewer of
    # GH #18/#19 one wrong conclusion before the re-run with an explicit
    # JNEXT=... failed correctly. Either rebuild the binary this resolves to
    # (`make gui-release`) or pin the one you mutated:
    #
    #     JNEXT=$PWD/build/jnext bash test/00regression/regression.sh <row>
    #
    # This is intended behaviour, not a defect: the suite wants the fastest
    # binary, and a run must not silently depend on which build dirs happen to
    # exist. Only the mutation workflow has to know.
    if [[ -z "${JNEXT:-}" ]]; then
        local candidate
        for candidate in "$PROJECT_DIR/build/gui-release/jnext" \
                         "$PROJECT_DIR/build/gui-debug/jnext" \
                         "$PROJECT_DIR/build/jnext"; do
            if [[ -x "$candidate" ]]; then
                JNEXT="$candidate"
                break
            fi
        done
        JNEXT="${JNEXT:-$PROJECT_DIR/build/jnext}"
    fi

    # SD-card image: the suite does NOT pass --sdcard at all — SD_CARD_ARGS
    # stays empty, a deliberate seam. jnext falls back to the self-provisioned
    # image under its state directory (sdcard::provision_sd_card,
    # src/core/sdcard_provisioner.*), the same FAT32-patched canonical distro
    # image an end user gets. That state directory is $JNEXT_CONFIG_DIR, which
    # sd_clone_for_run below points at a PRIVATE per-run clone (see there).
    # When the boot-ROM auto-load gate is active (Next + sd_card non-empty +
    # load_file empty), `BOOT` rows exercise the firmware path; rows with
    # --load NEX skip the boot ROM via the cfg.load_file gate (Emulator::init).
    SD_CARD_ARGS=()

    # rewind_test is a unit-test binary (only built when ENABLE_TESTS=ON, i.e.
    # via `make unit-test-build`, which `make regression` depends on). If it is
    # missing, the rewind functional test FAILS LOUDLY in the preflight —
    # never a silently absent row.
    # The SDL-only jnext (sdl-keypress and the rows that run both frontends) and
    # the directory of native helper binaries (rewind_test, sdfile_tool, the
    # *_probe tools): `regression.sh --platform` points them at another build
    # tree. Exported so a row process inherits them.
    JNEXT_SDL="${JNEXT_SDL:-$PROJECT_DIR/build/sdl-release/jnext}"
    TEST_BIN_DIR="${TEST_BIN_DIR:-$PROJECT_DIR/build/test}"
    # The minimal-card builder (GH #458, tools/make-minimal-card), built in the same tree as the
    # native helpers above: the card the rows that boot a minimal NextZXOS card are made with.
    MAKE_MINIMAL_CARD="${MAKE_MINIMAL_CARD:-$TEST_BIN_DIR/../tools/make-minimal-card/make-minimal-card}"
    export JNEXT_SDL TEST_BIN_DIR MAKE_MINIMAL_CARD
    REWIND_TEST="$TEST_BIN_DIR/rewind_test"
    CONF="${JNEXT_REGRESSION_CONF:-$SCRIPT_DIR/regression_tests.conf}"
    FUNC_CONF="${JNEXT_REGRESSION_FUNC_CONF:-$SCRIPT_DIR/functional_tests.conf}"
    # img/ lives next to this file under test/00regression/img.
    IMG_DIR="$SCRIPT_DIR/img"
    # NextZXOS welcome-screen reference, diffed against by several boot rows.
    WELCOME_REF="$IMG_DIR/boot-nextzxos-welcome-reference.png"

    # ROW-PROCESS MODE (GH #295). In a parallel run each functional row runs
    # in a process of its own (row-runner.sh), and the driver has already
    # made that row's scratch and state directories and put its SD image in
    # place (run_row_launch, parallel-rows.inc). The handoff is these
    # variables, and they are UNSET as soon as they are read: a row that
    # starts a suite shell of its own (sdcard-isolation-func does, on
    # purpose) must get a fresh initialization, never this row's directories
    # — its trap would delete them.
    local row_tmp=${JNEXT_REGRESSION_ROW_TMP:-} row_run=${JNEXT_REGRESSION_ROW_RUN:-}
    ROW_RESULT_FILE=${JNEXT_REGRESSION_ROW_RESULT:-}
    local row_stamp=${JNEXT_REGRESSION_ROW_MASTER_STAMP:-} row_mode=${JNEXT_REGRESSION_ROW_CLONE_MODE:-}
    unset JNEXT_REGRESSION_ROW_TMP JNEXT_REGRESSION_ROW_RUN JNEXT_REGRESSION_ROW_RESULT \
          JNEXT_REGRESSION_ROW_MASTER_STAMP JNEXT_REGRESSION_ROW_CLONE_MODE
    if [[ -n "$row_tmp" ]]; then
        TMP_DIR=$row_tmp
    else
        TMP_DIR=$(mktemp -d)
    fi

    # The MASTER SD image — machine-wide, shared, and the only copy the suite
    # ever writes. 01-sdcard-provision.sh gates it on a hash and re-derives it
    # if a stray interactive session drifted it.
    # JNEXT_REGRESSION_SD_STATE_DIR (regression.sh validates it) points the
    # master at another jnext state dir, to evaluate a candidate card without
    # touching the shared one; unset, the master is exactly what it always was.
    sd_master_paths

    # This run's PRIVATE state directory (GH #65). Two jobs at once:
    #
    #  1. GUI preferences. Every NON-headless $JNEXT invocation constructs the
    #     real MainWindow, which loads AppConfig unconditionally for fields
    #     with no CLI flag (CPU speed, window scale, CRT filter, tape
    #     fast-load); a stray real config could silently change CPU speed and
    #     perturb timing-sensitive pixel comparisons. Pointing every load() at
    #     a fresh directory yields clean AppConfigData defaults.
    #
    #  2. The SD-card image. src/core/sdcard_provisioner.cpp resolves the
    #     fallback image under this same directory, so the run boots
    #     $RUN_DIR/sdcard/cspect-next-1gb-fixed.img — a private clone — and
    #     never the master. NextZXOS WRITES BACK to the card it boots, so
    #     without this the suite mutates the very artefact its reference
    #     screenshots were generated against. That is not hypothetical: a drift
    #     turned a green branch into 91 pass / 5 FAIL, indistinguishable from a
    #     rendering regression.
    #
    # WHY UNDER $HOME AND NOT $TMP_DIR. The clone is made with reflink, which
    # is a filesystem-internal operation: it works only WITHIN one filesystem.
    # On the dev host $HOME is btrfs and /tmp is tmpfs, and `cp --reflink` from
    # one to the other fails with "Invalid cross-device link" — `--reflink=auto`
    # then silently degrades to a real 1 GB copy INTO RAM, per concurrent run.
    # Same-filesystem, the clone takes ~60 ms and costs no space until written.
    if [[ -n "$row_run" ]]; then
        RUN_DIR=$row_run
    else
        RUN_DIR="$HOME/.jnext/runs/$$-$(date +%s)-$RANDOM"
    fi
    export JNEXT_CONFIG_DIR="$RUN_DIR"
    # (XDG_CONFIG_HOME is kept for any other Qt state.)
    #
    # ...but NOT for git. git resolves its global configuration and its default
    # global excludes file through XDG_CONFIG_HOME ($XDG_CONFIG_HOME/git/config,
    # $XDG_CONFIG_HOME/git/ignore), so under the isolation every git the
    # harness runs lost the user's own setup: on the main checkout a file
    # ignored only by ~/.config/git/ignore (.claude/settings.local.json) read
    # as an untracked change, and a 216/216 run was refused its stamp as
    # "dirty" (GH #295). The ORIGINAL value is captured once, by the outermost
    # run (rows and nested runs inherit it; empty = unset, which is how git
    # treats an empty one), and every `git` a harness bash runs — this shell,
    # the lints, the rows, the stamp script — goes through the exported
    # function below, which hands git that value. Qt still gets the scratch dir.
    if [[ -z "${JNEXT_REGRESSION_GIT_XDG_CONFIG_HOME+x}" ]]; then
        export JNEXT_REGRESSION_GIT_XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-}"
    fi
    git() { XDG_CONFIG_HOME="$JNEXT_REGRESSION_GIT_XDG_CONFIG_HOME" command git "$@"; }
    export -f git
    export XDG_CONFIG_HOME="$TMP_DIR/xdg-config-home"

    # ONE trap, installed before either directory has anything in it. Cleanup
    # must be a trap and never a final step: a killed or timed-out run would
    # otherwise leak a gigabyte, and `set -e` has already killed three scripts
    # in this suite.
    # INT/TERM as well as EXIT: an EXIT-only trap does not fire when the
    # shell dies from an untrapped signal, so a Ctrl-C during a multi-minute
    # regression run leaked the ~1 GB clone. This dates from GH #65 and was
    # found by the GH #75 review; same one-line shape as the fix in
    # test/run-unit-tests.sh and test/bench/bench.sh, and HS-40 now scans all
    # three so it cannot come back.
    #
    # THIS script is why the INT/TERM handlers exit explicitly. Registered as
    # `EXIT INT TERM` sharing one cleanup-only handler, a Ctrl-C here removed
    # the SD clone and then RESUMED the suite: measured 58 FAIL / 4 PASS
    # against an image that no longer existed, every row reading like a real
    # emulator regression. The per-test loop tolerates its own errors, so
    # nothing downstream trips `set -e` to stop it. A wrong result that is
    # believed is worse than no result.
    trap 'regression_cleanup' EXIT
    trap 'regression_cleanup; exit 130' INT
    trap 'regression_cleanup; exit 143' TERM

    # Clone now, so a DIRECTLY-EXECUTED test script (every scripts/*.sh is also
    # standalone-runnable) has an image before it launches jnext. A full run
    # re-clones in the "[sdcard-provision]" row after the master's hash gate,
    # which may have re-derived it; the re-clone costs ~60 ms. A failure here is
    # not fatal on its own — the row is what reports and provisions.
    # A row process boots the image the driver put in its RUN_DIR (a hard
    # link to the run's clone, or a private copy) and inherits the clone's
    # mode from the driver; cloning the master here would undo exactly that.
    if [[ -n "$row_run" ]]; then
        SD_CLONE_MODE=${row_mode:-absent}
    else
        sd_clone_for_run || true
    fi

    # The master's identity at the start of the run. sdcard-isolation-func
    # compares against it at the END, after every screenshot row has booted
    # NextZXOS — so the assertion covers the whole suite, not one contrived
    # boot. 01-sdcard-provision.sh re-stamps if its hash gate re-derived it.
    # A row process takes the driver's stamp, made after the provisioning
    # gate — its own stat would start the measurement too late.
    if [[ -n "$row_run" ]]; then
        SD_MASTER_STAMP=${row_stamp:-absent}
    else
        SD_MASTER_STAMP=$(stat -c '%i:%Y:%s' "$SD_MASTER_IMAGE" 2>/dev/null || echo "absent")
    fi

    # Pixel difference tolerance (0 = exact match)
    TOLERANCE=${JNEXT_TEST_TOLERANCE:-0}

    # Pinned RTC for deterministic NextZXOS boot screenshots (must match the
    # checked-in boot-nextzxos-* references).
    NEXTZXOS_RTC="2026-07-10T08:55:00"

    pass=0
    fail=0
    skip=0

    # Host-load bookkeeping (GH #245) — see host_load1 above.
    LOADAVG_FILE="${JNEXT_REGRESSION_LOADAVG_FILE:-/proc/loadavg}"
    HOST_CPUS="${JNEXT_REGRESSION_NPROC:-$(nproc 2>/dev/null || echo 1)}"
    LOAD_START=""
    LOAD_END=""
    LOADED_FAILS=0
    FAILED_ROWS=()
    FAIL_RECORDS=()
    SKIPPED_ROWS=()
    CURRENT_ROW=""

    # ImageMagick availability. Detected here (silently) so every consumer can
    # test $HAS_COMPARE; the driver prints the user-facing warning when false.
    if command -v compare &>/dev/null; then
        HAS_COMPARE=true
    else
        HAS_COMPARE=false
    fi

    # Filter and row-reporting state. Standalone defaults: no filter (every
    # want() succeeds), compare mode (not --update), nothing reported yet. The
    # driver overwrites these from its command line and its functional-tests
    # manifest.
    FILTER_TESTS=()
    declare -gA IS_FILTERED=()
    REPORTED_FUNC=()
    UPDATE_MODE=false
}

# tools/sd-master-ensure.sh sources this file for the SD master gate alone and sets
# JNEXT_REGRESSION_LIB_NO_INIT: the initialisation makes a run directory, a clone and a trap.
[[ -n "${JNEXT_REGRESSION_LIB_NO_INIT:-}" ]] || regression_lib_init
