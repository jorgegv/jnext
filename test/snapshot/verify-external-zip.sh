#!/usr/bin/env bash
#
# The second half of the GH #27 stage-S1 exit gate: every archive the `.jns`
# writer produces must be accepted by a ZIP implementation THAT IS NOT OURS.
#
# WHY THIS EXISTS. jnext's `.szx` saver wrote RAM pages 0-111; its own loader
# accepted any page with no upper bound, so save -> load -> compare passed
# byte-exact with discriminative, mutation-tested assertions — all green, all
# worthless, because libspectrum rejects any page > 63 and every `.szx` jnext
# could produce failed to load in real FUSE. Saver and loader shared the blind
# spot, which made the defect structurally invisible to the suite as written
# (doc/design/NEXT-SNAPSHOT-FORMAT.md §13.1).
#
# `snapshot_test` is that same shape: our writer checked by our reader. It
# cannot, by construction, find a framing error both sides agree on. These two
# tools can, and they are the reason ZIP was chosen over a first-party chunked
# binary in the first place (§5.1, §13.2(2)):
#
#   * info-zip `unzip -t`  — verifies the central directory, every local
#                            header, the sizes and every per-member CRC-32
#   * CPython `zipfile`    — an entirely separate implementation; `testzip()`
#                            re-checks every CRC, and `namelist()` /
#                            `infolist()` expose the framing fields so this
#                            script can assert the compression method too
#
# POSTURE: both readers are REQUIRED, locally and in CI alike; a missing one
# FAILS (owner, 2026-10-06). A check that cannot run is not a pass.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${REPO_ROOT}/build/test/snapshot_test"
OUT_DIR="${REPO_ROOT}/build/test/jns-corpus"

have_unzip=0
have_python=0
command -v unzip   >/dev/null 2>&1 && have_unzip=1
command -v python3 >/dev/null 2>&1 && python3 -c 'import zipfile' >/dev/null 2>&1 \
    && have_python=1

# BOTH independent readers are required: a missing one is a reader that did not
# check the container, and a check that cannot run is not a pass.
if [ "$have_unzip" -eq 0 ] || [ "$have_python" -eq 0 ]; then
    [ "$have_unzip" -eq 0 ] && echo "FAIL  unzip is not installed"
    [ "$have_python" -eq 0 ] && echo "FAIL  python3 zipfile is not importable"
    echo "      An independent ZIP reader did not check the container, so the"
    echo "      result would not be evidence. Install unzip and python3."
    exit 1
fi

if [ ! -x "$BIN" ]; then
    echo "FAIL  $BIN is not built; run 'make unit-test-build' first"
    exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# --emit-corpus writes every archive the suite's rows built. Deliberately the
# rows' own output rather than a separate sample: a hand-picked corpus drifts
# away from what the writer actually emits, and then this gate checks files
# nothing produces.
if ! "$BIN" --emit-corpus "$OUT_DIR" >"$OUT_DIR/emit.log" 2>&1; then
    echo "FAIL  snapshot_test could not emit its corpus:"
    sed 's/^/      /' "$OUT_DIR/emit.log"
    exit 1
fi

count=$(find "$OUT_DIR" \( -name '*.jns' -o -name '*.zip' \) | wc -l)
snaps=$(find "$OUT_DIR" -name '*.jns' | wc -l)
if [ "$count" -lt 1 ] || [ "$snaps" -lt 1 ]; then
    # A corpus that came out empty would make every check below pass over
    # nothing — the vacuous-pass failure mode, which turns the strongest
    # evidence here into the most confident lie.
    echo "FAIL  the corpus is empty; there is nothing for an independent"
    echo "      reader to accept, so a pass here would be vacuous"
    exit 1
fi

rc=0

# (both readers are present: checked above)
if [ "$have_unzip" -eq 1 ]; then
    for f in "$OUT_DIR"/*.jns "$OUT_DIR"/*.zip; do
        if ! unzip -t "$f" >"$OUT_DIR/unzip.log" 2>&1; then
            echo "FAIL  unzip -t rejected $(basename "$f"):"
            sed 's/^/      /' "$OUT_DIR/unzip.log"
            rc=1
        fi
    done
    [ "$rc" -eq 0 ] && echo "  unzip -t      accepted $count archives ($snaps snapshots)"
fi

if [ "$have_python" -eq 1 ]; then
    if ! python3 "${REPO_ROOT}/test/snapshot/verify_zipfile.py" "$OUT_DIR"; then
        rc=1
    fi
fi

if [ "$rc" -ne 0 ]; then
    echo "FAIL  an independent ZIP reader rejected an archive the .jns writer"
    echo "      produced. That is the GH #27 S1 exit gate; see"
    echo "      doc/design/NEXT-SNAPSHOT-FORMAT.md §13.2(2)."
    exit 1
fi

rm -rf "$OUT_DIR"
echo "PASS  every archive the .jns writer produces is accepted by an"
echo "      independent ZIP reader ($count archives)"
