#!/usr/bin/env bash
#
# Contract tests for packaging/add-release.sh — the helper the bump-minor /
# bump-major targets use to add a tag to releases.yaml. Runs on throwaway
# copies, never touches the real tree.
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root
repo=$(pwd)

RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'
pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %s\n" "$1"; fail=$((fail+1)); }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/packaging"
cp "$repo/packaging/add-release.sh" "$tmp/packaging/"
add="$tmp/packaging/add-release.sh"
rel="$tmp/releases.yaml"

# This suite validates real YAML, so it needs PyYAML. Check ONCE, up front,
# and fail loudly — the alternative is what shipped: three Python tracebacks
# interleaved with the results, read as "add-release is broken" when the truth
# was "this host has no PyYAML". It cost a CI-only red that stood for two
# pushes because the failure was undiagnosable from the log.
#
# NOT a skip: this suite gates the releases.yaml allowlist, which decides
# whether a tag becomes a public GitHub Release. A silent skip there is exactly
# the class of hole test/unit-tests.conf exists to forbid.
if ! python3 -c 'import yaml' 2>/dev/null; then
    printf "${BOLD}=== add-release.sh contract tests ===${RESET}\n\n" >&2
    printf "  FATAL: PyYAML is not installed, so YAML validity cannot be checked.\n" >&2
    printf "  This suite will NOT pretend to pass without it.\n" >&2
    printf "  Fedora: sudo dnf install python3-pyyaml    (CI: see .github/workflows/ci.yml)\n\n" >&2
    exit 1
fi

# a well-formed YAML list with exactly these versions?
haslist() { python3 -c "import yaml,sys; d=yaml.safe_load(open('$rel')); sys.exit(0 if d.get('releases')==$1 else 1)"; }

printf "${BOLD}=== add-release.sh contract tests ===${RESET}\n\n"

# 1 — empty inline list becomes a block list with the new tag
printf 'releases: []\n' > "$rel"
bash "$add" v0.99.0 >/dev/null 2>&1
if haslist "['v0.99.0']"; then ok "empty list -> block list with the new tag"; else bad "did not start the list correctly"; fi

# 2 — a second tag is appended, first preserved, YAML still valid
bash "$add" v1.0.0 >/dev/null 2>&1
if haslist "['v1.0.0', 'v0.99.0']" || haslist "['v0.99.0', 'v1.0.0']"; then
    ok "second tag added, first preserved, valid YAML"
else bad "append lost an entry or broke the YAML"; fi

# 3 — idempotent (re-adding an existing tag changes nothing)
before=$(cat "$rel")
bash "$add" v0.99.0 >/dev/null 2>&1
if [ "$before" = "$(cat "$rel")" ]; then ok "re-adding an existing tag is a no-op"; else bad "re-add duplicated an entry"; fi

# 4 — missing 'releases:' key -> fail loud, file untouched
printf 'other: 1\n' > "$rel"; b4=$(cat "$rel")
if bash "$add" v2.0.0 >/dev/null 2>&1; then bad "succeeded despite no 'releases:' key"
elif [ "$b4" = "$(cat "$rel")" ]; then ok "no 'releases:' key -> fail loud, no write"
else bad "failed but still modified the file"; fi

# 5 — missing file -> fail loud
rm -f "$rel"
if bash "$add" v2.0.0 >/dev/null 2>&1; then bad "succeeded despite missing releases.yaml"; else ok "missing file -> fail loud"; fi

printf "\n${BOLD}=== Results ===${RESET}\n"
printf "  ${GREEN}Pass: %d${RESET}  ${RED}Fail: %d${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
