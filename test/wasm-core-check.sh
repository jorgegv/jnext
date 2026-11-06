#!/usr/bin/env bash
#
# wasm-core-check.sh - run the WebAssembly core build under node (`make wasm-core`).
#
#   bash test/wasm-core-check.sh <build-dir>      e.g. build/wasm-core
#
# Rows WCORE-01..08, printed in the suite's uniform format; exits non-zero on any
# FAIL or SKIP. The oracle is the native binary's behaviour for the same command
# where one exists (the CLI refusals, the log-file error), else the SDK's own
# contract (no shared memory). The node is the SDK's, resolved through
# test/provision-emsdk.sh (never one on PATH).
set -uo pipefail

export LC_ALL=C LANG=C

EXPECTED_TOTAL=8

BUILD=${1:?usage: wasm-core-check.sh <build-dir>}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

EMSDK=$(bash "$HERE/provision-emsdk.sh" --print-path) || { echo "wasm-core-check: no provisioned emsdk (run make emsdk)" >&2; exit 2; }
NODE=$(ls -d "$EMSDK"/node/*/bin/node)
JS="$BUILD/jnext.js"
WASM="$BUILD/jnext.wasm"
SHA_JS="$BUILD/test/sha256_test.js"
for f in "$JS" "$WASM" "$SHA_JS"; do
    [[ -f "$f" ]] || { echo "wasm-core-check: $f is not built" >&2; exit 2; }
done

VERSION=$(sed -n 's/^version: *//p' "$ROOT/version.yaml")
[[ -n "$VERSION" ]] || { echo "wasm-core-check: no version in version.yaml" >&2; exit 2; }

SHA_ROWS=$(awk '$1 == "sha256_test" { print $2 }' "$ROOT/test/unit-tests.conf")
[[ "$SHA_ROWS" =~ ^[0-9]+$ ]] || { echo "wasm-core-check: sha256_test is not declared in test/unit-tests.conf" >&2; exit 2; }

PASS=0; FAIL=0; SKIP=0; TOTAL=0
row() { # id desc ok(0/1) detail
    TOTAL=$((TOTAL + 1))
    if [[ "$3" == 1 ]]; then
        PASS=$((PASS + 1)); printf 'PASS %-9s %s\n' "$1" "$2"
    else
        FAIL=$((FAIL + 1)); printf 'FAIL %-9s %s [%s]\n' "$1" "$2" "$4"
    fi
}

SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/wasm-core-check.XXXXXX")
cleanup() { rm -rf "$SCRATCH"; }
trap cleanup EXIT

# Run jnext.js under node with the given arguments: OUT, ERR, RC. Bounded, and
# escalated to SIGKILL.
run_jnext() {
    RC=0
    timeout --kill-after=5s 60s "$NODE" "$JS" "$@" >"$SCRATCH/out" 2>"$SCRATCH/err" || RC=$?
    OUT=$(<"$SCRATCH/out")
    ERR=$(<"$SCRATCH/err")
}

# WCORE-01: --version prints exactly "jnext <version.yaml version>".
run_jnext --version
row WCORE-01 "node jnext.js --version prints exactly 'jnext $VERSION' and exits 0" \
    $([[ $RC -eq 0 && "$OUT" == "jnext $VERSION" ]] && echo 1 || echo 0) "rc=$RC out=[$OUT] err=[$ERR]"

# WCORE-02: the module defines its memory: no pthreads, no SharedArrayBuffer.
MEMIMPORTS=$(timeout --kill-after=5s 60s "$NODE" -e '
const m = new WebAssembly.Module(require("fs").readFileSync(process.argv[1]));
const imp = WebAssembly.Module.imports(m);
console.log(imp.length + " " + imp.filter(i => i.kind === "memory").length);
' "$WASM" 2>&1)
row WCORE-02 "jnext.wasm imports no memory (not shared: no pthreads)" \
    $([[ "$MEMIMPORTS" =~ ^[0-9]+\ 0$ ]] && echo 1 || echo 0) "imports/memory=[$MEMIMPORTS]"

# WCORE-03: a thrown-and-caught exception works (Wasm EH): spdlog cannot open the
# log file (its parent is a file), Log::apply_sink_policy catches it. Without
# -fwasm-exceptions the module prints "Aborted(...)" instead.
run_jnext --log-file /dev/null/x.log --version
row WCORE-03 "a caught exception reaches its handler: --log-file under a file exits 1 'cannot open log file'" \
    $([[ $RC -eq 1 && "$ERR" == *"cannot open log file '/dev/null/x.log'"* && "$ERR" != *Aborted* ]] && echo 1 || echo 0) "rc=$RC err=[$ERR]"

# WCORE-04: the refused flags, BEFORE --version (which returns at parse).
bad=""
for spec in "--nextpi" "--esp" "--record x.mp4" "--zrcp-port 10000" "--dzrp-port 10001" \
            "--gdb-port 10002" "--debug-listen-address 127.0.0.1" "--script x.jds" "--map x.map"; do
    flag=${spec%% *}
    # shellcheck disable=SC2086
    run_jnext $spec --version
    if ! [[ $RC -eq 1 && "$ERR" == *"$flag"* && "$ERR" == *"browser build"* && "$OUT" != jnext* ]]; then
        bad+="$flag(rc=$RC err=[$ERR]) "
    fi
done
row WCORE-04 "each of 9 refused flags exits 1 naming the flag and 'browser build', before --version runs" \
    $([[ -z "$bad" ]] && echo 1 || echo 0) "$bad"

# WCORE-05: --set of a refused key.
bad=""
for kv in "esp.enabled=true" "nextpi.enabled=true"; do
    run_jnext --set "$kv" --version
    key=${kv%%=*}
    if ! [[ $RC -eq 1 && "$ERR" == *"$key"* && "$ERR" == *"browser build"* && "$OUT" != jnext* ]]; then
        bad+="$key(rc=$RC err=[$ERR]) "
    fi
done
row WCORE-05 "--set esp.enabled=true and --set nextpi.enabled=true exit 1 naming the key and 'browser build'" \
    $([[ -z "$bad" ]] && echo 1 || echo 0) "$bad"

# WCORE-06: no over-refusal: the flags that ask for the only state there is.
run_jnext --no-esp --no-nextpi --machine 48k --version
row WCORE-06 "--no-esp --no-nextpi --machine 48k --version is accepted and prints the version" \
    $([[ $RC -eq 0 && "$OUT" == "jnext $VERSION" ]] && echo 1 || echo 0) "rc=$RC out=[$OUT] err=[$ERR]"

# WCORE-07: the SHA-256 suite on the portable backend.
SHA_RC=0
SHA_OUT=$(timeout --kill-after=5s 300s "$NODE" "$SHA_JS" 2>&1) || SHA_RC=$?
SHA_TOTAL_LINE=$(grep '^Total:' <<<"$SHA_OUT" || true)
want_re="^Total: +$SHA_ROWS +Passed: +$SHA_ROWS +Failed: +0 +Skipped: +0\$"
row WCORE-07 "sha256_test under node runs $SHA_ROWS rows (the count pinned in unit-tests.conf), all passed, none skipped" \
    $([[ $SHA_RC -eq 0 && "$SHA_TOTAL_LINE" =~ $want_re ]] && echo 1 || echo 0) "rc=$SHA_RC total=[$SHA_TOTAL_LINE]"

# WCORE-08: parse_hex16 refuses a bare "0x" (musl's strtoul reads it as 0).
run_jnext --inject-org 0x --version
row WCORE-08 "--inject-org 0x is refused (exit 1), as natively" \
    $([[ $RC -eq 1 && "$OUT" != jnext* ]] && echo 1 || echo 0) "rc=$RC out=[$OUT] err=[$ERR]"

RAW=$(stat -c %s "$WASM")
GZ=$(gzip -9 -c "$WASM" | wc -c)
echo "INFO jnext.wasm: $RAW bytes raw, $GZ bytes gzip -9"

if [[ $TOTAL -ne $EXPECTED_TOTAL ]]; then
    echo "wasm-core-check: ran $TOTAL rows, expected $EXPECTED_TOTAL" >&2
    FAIL=$((FAIL + 1))
fi
printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$TOTAL" "$PASS" "$FAIL" "$SKIP"
[[ $FAIL -eq 0 && $SKIP -eq 0 ]]
