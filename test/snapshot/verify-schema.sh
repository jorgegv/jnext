#!/usr/bin/env bash
#
# The second half of `make schema-check`: hand the committed `.jns` JSON Schema
# to an implementation THAT IS NOT OURS.
#
# WHY THIS EXISTS. The first half regenerates the schema and byte-diffs it
# against the committed copy. That proves the committed file is current; it
# proves nothing about whether the file is a valid schema, or whether it
# accepts and rejects the right documents. A schema with `additionalProperties`
# everywhere and no constraints would pass the byte-diff forever.
#
# It is also the answer to `feedback_self_consistent_generated_data`: a schema
# generated from a declaration, validated against JSON produced from the same
# declaration, agrees with itself by construction. Bringing in a validator we
# did not write does not close that — §9.3 is explicit that only a human
# reading the diff does — but it removes the ENCODING class: wrong type, a hex
# string of the wrong length, a value outside a register's width, a `u64`
# emitted as a number.
#
# POSTURE: a missing validator FAILS, locally and in CI alike. A check that
# cannot run is not a pass (owner, 2026-10-06; this used to skip locally).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCHEMA="${REPO_ROOT}/doc/formats/jns-snapshot.schema.json"

if ! command -v python3 >/dev/null 2>&1 ||
   ! python3 -c 'import jsonschema' >/dev/null 2>&1; then
    echo "FAIL  python3 jsonschema is not available. This check cannot run,"
    echo "      so the .jns schema is not verified against an implementation"
    echo "      that is not ours; install it (python3-jsonschema)."
    exit 1
fi

if [ ! -f "$SCHEMA" ]; then
    echo "FAIL  $SCHEMA does not exist; run 'make docs-schema'"
    exit 1
fi

exec python3 "${REPO_ROOT}/test/snapshot/verify_schema.py" "$SCHEMA"
