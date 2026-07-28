#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# CMakeLists.txt's two "the spdlog submodule is missing" guards (GH #152).
#
# Nothing else can cover them: every green build has the submodule, so the
# entire triplet exercises only the happy path. Before the fix a failed
# `git submodule update` merely WARNed and configure died ~50 lines later at
# add_subdirectory(third_party/spdlog) with "does not contain a CMakeLists.txt
# file" — naming neither the submodule, the network, nor a remedy. A future
# edit demoting either guard back to a warning would pass every other test.
#
# Mechanism: a symlink farm of the REAL repo (every top-level entry except
# .git and third_party) is configured with cmake. Only third_party is
# fabricated, so there is NO hand-maintained mirror of what the top of
# CMakeLists.txt reads — a new find_package() there resolves through the farm.
# Hermetic: GIT_EXECUTABLE=/bin/false makes the init fail deterministically
# without needing network isolation, and no row ever reaches the network.
if want cmake-guards-func; then
    begin_func cmake-guards-func

    cg_t=$(mktemp -d); trap 'rm -rf "$cg_t"' EXIT
    cg_fail=""

    # build_sandbox <dir> <spdlog-mode: empty|sentinel> <git-mode: gitfile|archive>
    build_sandbox() {
        local sb="$1" spdlog_mode="$2" git_mode="$3"
        rm -rf "$sb"; mkdir -p "$sb/third_party/spdlog"
        local e
        for e in "$PROJECT_DIR"/* "$PROJECT_DIR"/.[!.]*; do
            case "$(basename "$e")" in
                .git|third_party|build|build-*) continue ;;
            esac
            ln -s "$e" "$sb/$(basename "$e")"
        done
        ln -s "$PROJECT_DIR/third_party/zot" "$sb/third_party/zot"
        # A populated submodule is stubbed by a CMakeLists that halts configure
        # with a sentinel: reaching it PROVES both guards let execution through,
        # and keeps the run at a few seconds instead of a full project configure.
        [[ "$spdlog_mode" == sentinel ]] &&
            echo 'message(FATAL_ERROR "CG-SENTINEL-REACHED-SPDLOG")' > "$sb/third_party/spdlog/CMakeLists.txt"
        # A git worktree's .git is a FILE, so EXISTS is true either way; the
        # `archive` mode (no .git at all) is the shape of a source tarball.
        [[ "$git_mode" == gitfile ]] && echo "gitdir: /nonexistent" > "$sb/.git"
        return 0
    }

    # cg_check <id> <desc> <expect-substring>... — configure the sandbox at
    # $cg_t/src, then assert every expected substring is in the output.
    cg_check() {
        local id="$1" desc="$2"; shift 2
        local out want_s
        out=$(LC_ALL=C timeout --foreground --kill-after=5s 180s \
                  cmake -B "$cg_t/build" -S "$cg_t/src" -DGIT_EXECUTABLE=/bin/false 2>&1) || true
        for want_s in "$@"; do
            if ! grep -qF -- "$want_s" <<<"$out"; then
                cg_fail="$cg_fail $id"
                echo -e "      $id ($desc): expected ${BOLD}$want_s${RESET} in the configure output" >&2
                echo "$out" | tail -12 | sed -E 's/^/        /' >&2
                return 0
            fi
        done
        return 0
    }
    # cg_check_absent <id> <desc> <forbidden-substring>
    cg_check_absent() {
        local id="$1" desc="$2" nope="$3"
        local out
        out=$(LC_ALL=C timeout --foreground --kill-after=5s 180s \
                  cmake -B "$cg_t/build" -S "$cg_t/src" -DGIT_EXECUTABLE=/bin/false 2>&1) || true
        if grep -qF -- "$nope" <<<"$out"; then
            cg_fail="$cg_fail $id"
            echo -e "      $id ($desc): ${BOLD}$nope${RESET} must NOT appear" >&2
        fi
        return 0
    }

    # CG-01 — the #152 case: init fails (offline) and the submodule is empty.
    # Fatal AT the failing step, naming the submodule and the exact command.
    rm -rf "$cg_t/build"; build_sandbox "$cg_t/src" empty gitfile
    cg_check CG-01 "failed init + empty submodule is fatal" \
        "third_party/spdlog submodule is" \
        "git submodule update --init --recursive" \
        "CMake Error"
    # ...and it stops THERE. Asserting the absence of the downstream messages is
    # what makes this row discriminative: demoting the init-site guard back to a
    # warning still yields a "CMake Error" (the add_subdirectory guard below
    # catches it), so only the fact that the LATER guard never spoke proves the
    # build stopped at the point of failure. Caught demoting M1 to a false green.
    cg_check_absent CG-01b "failed init does not fall through to add_subdirectory" \
        "does not contain a CMakeLists.txt file"
    cg_check_absent CG-01c "failed init does not fall through to the second guard" \
        "jnext needs this git submodule to build"

    # CG-02 — no .git at all (source archive / no git installed /
    # -DGIT_SUBMODULE=OFF): the init block never runs, so CG-01's guard cannot
    # fire and the add_subdirectory guard is the only thing standing there.
    rm -rf "$cg_t/build"; build_sandbox "$cg_t/src" empty archive
    cg_check CG-02 "empty submodule without .git is fatal" \
        "third_party/spdlog is empty" \
        "git submodule update --init --recursive" \
        "CMake Error"
    cg_check_absent CG-02b "archive case does not fall through" \
        "does not contain a CMakeLists.txt file"

    # CG-03 — a failed init with the content ALREADY present must NOT be fatal.
    # Real case: git's "dubious ownership" refusal (exit 128) on a workspace
    # owned by another uid, which CI works around with safe.directory. The
    # sentinel proves configure ran on past both guards.
    rm -rf "$cg_t/build"; build_sandbox "$cg_t/src" sentinel gitfile
    cg_check CG-03 "failed init + present submodule keeps building" \
        "third_party/spdlog is present" \
        "CG-SENTINEL-REACHED-SPDLOG"

    if [[ -z "$cg_fail" ]]; then
        pass_row " (CG-01/02 fatal at the failing step, CG-03 non-fatal)"
    else
        fail_row " (failed:$cg_fail)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
