#!/usr/bin/env bash
# Published-debug-header include-graph lint (GH #276, work package B0).
#
# THE ONE DURABLE STATEMENT OF WHY B0 EXISTS. Epic #276's defining property is
# that a debugger frontend stops holding an `Emulator*`: the four published
# headers `src/debug/{debugger,events,inspect,result}.h` are the whole API, and
# nothing behind them may drag in the emulator core, the platform layer, a
# toolkit, or the debug internals §3.1 keeps unpublished. Every one of the five
# downstream packages (Qt, DZRP, ZRCP, GDB, the DSL) codes against that.
#
# NOTHING IN THE TREE CHECKED IT. The B0 review found the headers' own banner
# citing design §9's `grep -l 'core/emulator.h' src/debugger/*.cpp` as the gate
# — which greps the QT PANELS' sources and says nothing whatever about these
# headers. Adding `#include "core/emulator.h"` to `inspect.h` compiled clean and
# no test, lint or CI step noticed. A comment is not a gate; this is.
#
# HOW. For each published header, preprocess a one-line translation unit that
# includes only it, with `-M` (list dependencies, do not compile), and assert
# that no dependency matches the forbidden set. A dependency list is the right
# instrument because it reports the TRANSITIVE graph: a forbidden header pulled
# in three levels down is caught exactly like a direct include, which a grep of
# the four files cannot do.
#
# `-M`, NOT `-MM`, and the difference is load-bearing. `-MM` OMITS system
# headers, and SDL3 IS on a default include path here (it preprocesses with no
# `-I` at all), so under `-MM` the SDL pattern below could never match and would
# be permanently dead. The first cut of this lint used `-MM` and its own mutation
# run proved it: four of five forbidden includes were caught, and
# `#include "input/keyboard.h"` — whose whole hazard is that it reaches
# `<SDL3/SDL.h>` — passed clean. `-M` lists the standard library too, which is
# noise the specific patterns below ignore.
#
# Qt is NOT the same case, and the pattern below says so rather than implying it:
# Qt6 lives in `/usr/include/qt6`, which is not a default include path here or in
# the Fedora 44 CI container, so a Qt include never reaches the pattern — it fails
# the "could not be preprocessed with -I<src> alone" arm above it instead. The
# property is enforced either way; which ARM enforces it differs, and an earlier
# version of this comment claimed SDL and Qt were "both" on a system path, which
# is what made the ten patterns read as nine exercisable ones.
#
# TWO POSITIVE CONTROLS, because a lint that reports absence must first be shown
# capable of reporting presence (a `-MM` that silently produced nothing would
# "pass" every header while checking nothing):
#
#   * every published header must EXIST, so a rename cannot make this vacuous;
#   * each one's dependency list must contain itself AND `src/debug/result.h`,
#     which every one of them reaches.
#
# It also asserts the four headers preprocess with `-Isrc` ALONE — no SDL, Qt,
# spdlog or third-party include path. That is a property worth having in its own
# right: it is what lets a frontend include the facade without inheriting a
# build configuration.
#
# WHAT IT CANNOT CATCH, stated so a pass is not mistaken for a proof: a
# forbidden TYPE reached without including its header (a forward declaration of
# something from `core/`), a dependency introduced only under a preprocessor
# condition this TU does not take, and anything in a header not listed in
# HEADERS below. It catches the mistake that actually happened — a forbidden
# `#include`, direct or transitive — and does not attempt more.

# ENVIRONMENT
#   JNEXT_LINT_DEBUG_HEADERS_SRC   use this directory as the include root and the
#                                  header source instead of the repository's
#                                  `src/`. It exists for the two
#                                  `make harness-selftest` rows that prove this
#                                  lint is still REACHED from the regression
#                                  preflight and that its verdict still turns
#                                  that row red; regression.sh never sets it.
#   CXX                            compiler to ask for the dependency list.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="${JNEXT_LINT_DEBUG_HEADERS_SRC:-$PROJECT_DIR/src}"
CXX="${CXX:-c++}"

# The published surface (design §3.1 / backend.md §6). Paths relative to src/.
HEADERS=(
    debug/result.h
    debug/events.h
    debug/inspect.h
    debug/debugger.h
)

# Forbidden dependencies, as extended regexes matched against each path `-M`
# prints. Each line says which layer it keeps out.
FORBIDDEN=(
    'core/emulator\.h'          # the emulator core — the whole point of the epic
    '(^|/)src/platform/'        # the layer ABOVE the backend (§4 CTL-15)
    # BELT, NOT BRACES, and deliberately kept: Qt6 is not on a default include
    # path here or in CI, so a Qt include fails the preprocess arm before it can
    # reach this pattern. It stays for the platform or container where Qt IS on
    # the default path — and because a pattern that costs one grep is cheaper
    # than the argument about whether it will ever be needed. Do not read a pass
    # of this pattern as evidence Qt was absent; the preprocess arm is what
    # proves that here.
    '[Qq]t[0-9]?/|QtCore|QtGui|QtWidgets|QtOpenGL'   # any toolkit header
    'SDL[0-9]?/|(^|/)SDL\.h$'   # SDL, which input/keyboard.h would bring
    'memory/mmu\.h'             # heavy core header; INS-02/03 use accessors
    'video/renderer\.h'         # pulls video/ula.h + video/lores.h
    'video/palette\.h'          # dbg::PaletteId is a superset, not a re-export
    'video/timing\.h'           # MachineInfo carries ints, not RasterPos
    'debug/debug_state\.h'      # INTERNAL (§3.1): dbg::StepMode mirrors it
    'debug/breakpoints\.h'      # INTERNAL (§3.1): superseded by Subscription
)

WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

failed=0

for header in "${HEADERS[@]}"; do
    src="$SRC_DIR/$header"
    if [[ ! -f "$src" ]]; then
        echo "  $header: MISSING — this lint checks a header that no longer exists" >&2
        failed=1
        continue
    fi

    tu="$WORK/tu.cpp"
    printf '#include "%s"\n' "$header" > "$tu"

    # -M: transitive dependency list INCLUDING system headers (see the header
    # note — -MM would make the SDL and Qt patterns dead), no compilation.
    # -Isrc ALONE, deliberately.
    if ! deps="$(timeout --kill-after=5s 60s "$CXX" -std=c++17 -I"$SRC_DIR" \
                     -M -MF - "$tu" 2>"$WORK/err")"; then
        echo "  $header: could not be preprocessed with -I\"$SRC_DIR\" alone:" >&2
        sed 's/^/      /' "$WORK/err" >&2
        failed=1
        continue
    fi

    # One path per line in a FILE, dropping the make-rule target and the line
    # continuations. A file, not a `printf '%s\n' "${paths[@]}" | grep -q`
    # pipeline, and that is not a style preference: under `set -o pipefail` a
    # `grep -q` that finds its match exits immediately, `printf` takes SIGPIPE,
    # and the PIPELINE reports 141 even though the grep succeeded — so the check
    # inverts itself, at random, more often the longer the list. It cost ~1 run
    # in 10 here and only on `debugger.h`, whose dependency list is the longest,
    # until `make harness-selftest`'s HS-57 rows caught it. The repository
    # already forbids this construct for membership (HS-30, "no suite source
    # reintroduces 'printf ... | grep -q'"); it is just as wrong here.
    deps_file="$WORK/deps.txt"
    tr ' \\' '\n\n' <<<"$deps" | sed '/^$/d;/:$/d;/\.o$/d' | sort -u > "$deps_file"

    # Positive controls first: a lint that reports absence has to be shown able
    # to report presence. Matched RELATIVE to the include root, never against a
    # literal `src/` — the `make harness-selftest` rows aim this lint at a
    # symlink farm under $TMPDIR, and a control anchored on `src/` is blind
    # there, which is how the first cut of these two lines failed its own
    # self-test.
    if ! grep -q -- "/$header\$" "$deps_file"; then
        echo "  $header: its own dependency list does not contain it — the lint is blind" >&2
        failed=1
        continue
    fi
    if ! grep -q -- '/debug/result\.h$' "$deps_file"; then
        echo "  $header: does not reach debug/result.h, which every published" >&2
        echo "      header does — either the surface changed or -M produced nothing" >&2
        failed=1
        continue
    fi

    for pattern in "${FORBIDDEN[@]}"; do
        hits="$(grep -E -- "$pattern" "$deps_file" || true)"
        if [[ -n "$hits" ]]; then
            echo "  $header reaches a FORBIDDEN dependency (/$pattern/):" >&2
            sed 's/^/      /' <<<"$hits" >&2
            failed=1
        fi
    done
done

if [[ "$failed" -eq 0 ]]; then
    echo "  ${#HEADERS[@]} published header(s) checked; none reaches a forbidden dependency."
    exit 0
fi

cat >&2 <<'EOF'

A published debug header reaches something it must not. The four headers
src/debug/{debugger,events,inspect,result}.h are epic #276's whole API surface:
a frontend includes them and must NOT thereby acquire the emulator core, the
platform layer, a GUI toolkit, SDL, or the debug internals design §3.1 keeps
unpublished.

Fix the header, not this lint:

  * a core/video type needed by value  -> carry the fields (MachineInfo does),
                                          or mirror the enum and pin it in
                                          src/debug/debug_types_check.cpp
  * a core class needed by reference   -> forward-declare it (class Emulator;)
  * an internal debug type            -> mirror it, or move the value type to a
                                          published header deliberately

If a dependency genuinely belongs on the published surface, remove its pattern
from FORBIDDEN above and say in the commit message why the epic's property still
holds without it.
EOF
exit 1
