#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Benchmark harness line-format test: --benchmark N must print
# exactly one machine-parseable BENCH line to stdout, with every field present
# and the deterministic ones exact: a 48K machine at 3.5 MHz has
# (447+1)x(311+1)x4 / 8 = 69888 T-states/frame (timing.h HC/VC_MAX + Clock
# divisor 8). This validates the line contract, not performance — it runs on
# whatever binary/build the regression uses.
if want benchmark-func; then
    begin_func benchmark-func
    bench_out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --benchmark 20 2>/dev/null) || true
    bench_count=$(echo "$bench_out" | grep -c '^BENCH ' || true)
    if [[ "$bench_count" -eq 1 ]] && echo "$bench_out" | grep -qE \
        '^BENCH workload=boot-48k frames=20 wall=[0-9]+\.[0-9]+ fps=[0-9]+\.[0-9]+ tstates_per_sec=[0-9]+ tstates_per_frame=69888 cpu=3\.5MHz core=[0-9]+@[0-9]+kHz build=.+$'; then
        pass_row " (one well-formed BENCH line, 69888 T-states/frame @ 3.5MHz)"
    else
        fail_row " (BENCH line missing or malformed; got: $(echo "$bench_out" | grep '^BENCH ' || echo '<none>'))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
