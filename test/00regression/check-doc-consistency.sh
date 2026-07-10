#!/usr/bin/env bash
# Verify that doc/CURRENT-REGRESSION-STATE.md documents exactly the
# screenshot tests defined in regression_tests.conf — no more, no less.
# Exits non-zero on any drift. Keeps the doc from silently diverging
# from the conf (Task 29).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
conf="$here/regression_tests.conf"
doc="$here/../../doc/CURRENT-REGRESSION-STATE.md"

# Non-screenshot tests bundled into regression.sh — documented in the
# doc but not present as screenshot rows in the conf. Excluded from the
# 1:1 cross-check.
non_screenshot="rewind-func fuse_z80_test z80n_test"

# Conf test names: first field of every non-comment, non-blank line.
conf_tests="$(grep -vE '^[[:space:]]*(#|$)' "$conf" | awk '{print $1}' | sort -u)"

# Doc test names: the backticked heading '### `name`'.
doc_all="$(grep -oE '^### `[^`]+`' "$doc" | sed -E 's/^### `([^`]+)`/\1/' | sort -u)"
doc_tests="$doc_all"
for n in $non_screenshot; do
    doc_tests="$(printf '%s\n' "$doc_tests" | grep -vx "$n" || true)"
done
doc_tests="$(printf '%s\n' "$doc_tests" | sort -u)"

missing="$(comm -23 <(printf '%s\n' "$conf_tests") <(printf '%s\n' "$doc_tests"))"
extra="$(comm -13 <(printf '%s\n' "$conf_tests") <(printf '%s\n' "$doc_tests"))"

rc=0
if [ -n "$missing" ]; then
    echo "DRIFT: conf tests with no doc entry:"
    printf '  %s\n' $missing
    rc=1
fi
if [ -n "$extra" ]; then
    echo "DRIFT: doc entries with no conf test:"
    printf '  %s\n' $extra
    rc=1
fi

# Cross-check the '**N screenshot tests**' count in the doc.
doc_count="$(grep -oE '\*\*[0-9]+ screenshot tests\*\*' "$doc" | grep -oE '[0-9]+' || true)"
conf_count="$(printf '%s\n' "$conf_tests" | grep -c .)"
if [ "$doc_count" != "$conf_count" ]; then
    echo "DRIFT: doc says '$doc_count screenshot tests' but conf has $conf_count"
    rc=1
fi

if [ "$rc" -eq 0 ]; then
    echo "OK: $conf_count screenshot tests, conf and doc in sync"
fi
exit "$rc"
