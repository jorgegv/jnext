#!/usr/bin/env bash
#
# Contract tests for test/regression-package.sh and the Makefile recipe that
# feeds it (GH #487: the regression suite must run the binary each package
# ships). Hermetic: a throwaway copy of the script in a fake repo root, fake
# artifacts, a stub runner; no toolchain, no real package.
#   RP-01  an empty or unknown package name is refused (exit 2), naming the set
#   RP-02  no artifact, or only a previous version's, is refused (exit 2) naming
#          `make package-<p>`
#   RP-03  the screenshot rows come from the manifest and must equal its pin: a
#          disagreeing pin is refused, the real manifest passes the check
#   RP-04  `make regression-win` builds the PGO tree package-win zips and runs it
#          (text pin on the Makefile recipe: no non-PGO tree, no WIN_PGO=0)
# The rows that need a real artifact (the unpacked binary is the package's,
# the Windows scratch copy, the exit-status and count rules) are in
# packaging-test.sh's full mode, which has the packages.
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root
repo=$(pwd)

RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'
pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %s\n" "$1"; fail=$((fail+1)); }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fake=$tmp/root
mkdir -p "$fake/test/packaging" "$fake/test/00regression" "$fake/build"
cp "$repo/test/regression-package.sh" "$fake/test/"
cp "$repo/test/packaging/artifact-select.sh" "$fake/test/packaging/"
cp "$repo/test/00regression/regression_tests.conf" "$fake/test/00regression/"
printf 'version: 9.9.10\n' > "$fake/version.yaml"
run() { LANG=C bash "$fake/test/regression-package.sh" "$@" 2>&1; }

printf "${BOLD}=== regression-package contract tests ===${RESET}\n\n"

# RP-01
out=$(run); rc=$?
if [ "$rc" -eq 2 ] && grep -qF "rpm | deb | win | win-qt5 | win32-qt5" <<<"$out"; then
    ok "RP-01: no package name is refused, naming the set"
else
    bad "RP-01: no package name (rc=$rc): $out"
fi
out=$(run floppy); rc=$?
if [ "$rc" -eq 2 ] && grep -qF "rpm | deb | win | win-qt5 | win32-qt5" <<<"$out" && grep -qF floppy <<<"$out"; then
    ok "RP-01: an unknown package name is refused, naming it and the set"
else
    bad "RP-01: unknown package name (rc=$rc): $out"
fi

# RP-02
out=$(run rpm); rc=$?
if [ "$rc" -eq 2 ] && grep -qF "make package-rpm" <<<"$out"; then
    ok "RP-02: no artifact is refused, naming make package-rpm"
else
    bad "RP-02: no artifact (rc=$rc): $out"
fi
mkdir -p "$fake/build/win-release"
: > "$fake/build/win-release/jnext-9.9.9-windows-x64.zip"
out=$(run win); rc=$?
if [ "$rc" -eq 2 ] && grep -qF "make package-win" <<<"$out" && grep -qF "9.9.9" <<<"$out"; then
    ok "RP-02: a previous version's artifact alone is refused, naming make package-win"
else
    bad "RP-02: stale artifact (rc=$rc): $out"
fi

# RP-02 (continued): two versions in the build dir are refused naming BOTH (the reason, not
# "build it"), and --clear removes every version's artifact and nothing else.
mkdir -p "$fake/build/rpm-release"
: > "$fake/build/rpm-release/jnext-9.9.9-1.x86_64.rpm"; : > "$fake/build/rpm-release/jnext-9.9.10-1.x86_64.rpm"
: > "$fake/build/rpm-release/keep.me"
out=$(run rpm); rc=$?
if [ "$rc" -eq 2 ] && grep -qF "jnext-9.9.9-1.x86_64.rpm" <<<"$out" && grep -qF "2 candidates" <<<"$out"; then
    ok "RP-02: two versions of an artifact are refused, listing the candidates"
else
    bad "RP-02: two versions (rc=$rc): $out"
fi
run --clear rpm >/dev/null; rc=$?
left=$(ls "$fake/build/rpm-release")
if [ "$rc" -eq 0 ] && [ "$left" = "keep.me" ]; then
    ok "RP-02: --clear removes every version's artifact and nothing else"
else
    bad "RP-02: --clear (rc=$rc) left: $left"
fi
# Scope: the order of two lines of the recipe text (the clear must come BEFORE the package build, or the
# build leaves two versions behind for the selection to refuse); it does not run make.
rpk=$(awk '/^regression-package:/ { on = 1; next } on && /^$/ { exit } on { print }' "$repo/Makefile")
n_clear=$(grep -nF 'bash test/regression-package.sh --clear $(PACKAGE)' <<<"$rpk" | head -n1 | cut -d: -f1)
n_build=$(grep -nF 'package-$(PACKAGE) $$pre' <<<"$rpk" | head -n1 | cut -d: -f1)
if [ -n "$n_clear" ] && [ -n "$n_build" ] && [ "$n_clear" -lt "$n_build" ]; then
    ok "RP-02: the make regression-package recipe clears (line $n_clear) before it builds (line $n_build)"
else
    bad "RP-02: the recipe does not clear before it builds (clear line '${n_clear:-none}', build line '${n_build:-none}')"
fi

# RP-03: a pin that disagrees with the rows is refused; the real manifest is accepted.
mkdir -p "$fake/build/deb-release"
printf 'not a deb\n' > "$fake/build/deb-release/jnext_9.9.10_amd64.deb"
sed 's/^# expect: [0-9][0-9]*/# expect: 1/' "$repo/test/00regression/regression_tests.conf" > "$tmp/pin1.conf"
out=$(JNEXT_REGRESSION_CONF="$tmp/pin1.conf" run deb); rc=$?
if [ "$rc" -eq 2 ] && grep -qF "but pins 1" <<<"$out"; then
    ok "RP-03: a manifest whose pin disagrees with its rows is refused"
else
    bad "RP-03: wrong pin (rc=$rc): $out"
fi
pin=$(sed -n 's/^# *expect: *\([0-9][0-9]*\).*/\1/p' "$repo/test/00regression/regression_tests.conf" | head -n1)
out=$(run deb); rc=$?
if grep -qF "but pins" <<<"$out" || grep -qF "carries no" <<<"$out"; then
    bad "RP-03: the real manifest ($pin rows) was refused at the pin check: $out"
else
    ok "RP-03: the real manifest's derived screenshot rows equal its pin ($pin)"
fi

# RP-04: scope = the recipe text of regression-win; it does not prove the build ran.
recipe=$(awk '/^regression-win:/ { on = 1; next } on && /^$/ { exit } on { print }' "$repo/Makefile")
if [ -z "$recipe" ]; then
    bad "RP-04: no regression-win recipe found in the Makefile"
elif grep -qF 'win-release-non-pgo' <<<"$recipe" || grep -qF 'WIN_PGO=0' <<<"$recipe"; then
    bad "RP-04: regression-win builds or runs the non-PGO tree (it must test the tree package-win zips)"
elif grep -qF '$(MAKE) --no-print-directory win-release ' <<<"$recipe" \
     && grep -qF 'JNEXT_TARGET_BUILD=$(BUILD_DIR_WIN_RELEASE) ' <<<"$recipe"; then
    ok "RP-04: regression-win builds win-release and runs JNEXT_TARGET_BUILD=\$(BUILD_DIR_WIN_RELEASE)"
else
    bad "RP-04: regression-win no longer builds win-release / runs BUILD_DIR_WIN_RELEASE"
fi

printf "\n${BOLD}Total: %d  Pass: %d  Fail: %d${RESET}\n" $((pass+fail)) "$pass" "$fail"
[ "$fail" -eq 0 ]
