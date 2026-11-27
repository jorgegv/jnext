#!/usr/bin/env bash
#
# Contract test: every SD-card cache key in .github/workflows names the NextZXOS
# distribution and the fixed-image recipe that jnext itself derives (GH #494).
#
# The key is `sdcard-image-distro-<distro>-recipe<N>-<platform>`. <distro> and <N> were
# hand-kept copies of sdcard::kDistroUrl and sdcard::kFixedImageRecipe in
# src/core/sdcard_provisioner.cpp, and they drifted once: the Intel macOS jobs kept
# 24.11 / recipe 2 after the merge that moved every other key to 26.11 / recipe 3, so
# every Intel job missed the cache and downloaded the card, and would have saved a new
# card under an old key. The source is the oracle; this reads it the way the
# regression harness does (sd_expected_distro / sd_expected_recipe), with sed and
# grep only. It runs under `make package-contract-test` (Linux only), but the sed is
# spelled the portable way anyway (a block closed with `;}` after the `p` flag, which
# BSD sed requires).
#
#   CIKEY-01  every key names <distro>-recipe<N> read from the source
#   CIKEY-02  the macOS keys are ONE key (arm64 and Intel jobs share the cache entry)
#   CIKEY-03  the parser fails loud: it reads a fixture source and fixture workflows
#             correctly, and refuses (never passes) a source with no definition and a
#             directory with no keys
#   CIKEY-04  a distribution name with a trailing letter ("26.11a") parses, and the key
#             must carry the letter: 99.12-recipe7 is refused for a 99.12a source, and
#             99.12a-recipe7 for a 99.12 source
#   CIKEY-06  every fedora container job that uses actions/cache runs `dnf upgrade -y` (or
#             test/ci-deps.sh, which does) before its first cache step (GH #580)
#
set -u
cd "$(dirname "$0")/../.." || exit 2   # repo root

RED='\033[0;31m'; GREEN='\033[0;32m'; RESET='\033[0m'
pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %s\n" "$1"; fail=$((fail+1)); }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# read_expected <source.cpp> - print "<distro> <recipe>", e.g. "26.11 3"; fail printing nothing.
read_expected() {
    local src=$1 zip distro recipe pat='^const int kFixedImageRecipe = [0-9]+;$'
    zip=$(sed -n '/^const char\* const kDistroUrl =$/{n;s#.*/\([^/"]*\)";$#\1#p;}' "$src" 2>/dev/null)
    [[ -n "$zip" && "$zip" != *$'\n'* ]] || return 1
    distro=${zip#sn-emulator-}; distro=${distro%.zip}
    [[ "$distro" != "$zip" && "$distro" =~ ^[0-9][0-9.]*[a-z]?$ ]] || return 1
    [[ "$(grep -cE "$pat" "$src" 2>/dev/null)" == 1 ]] || return 1
    recipe=$(grep -E "$pat" "$src" | grep -oE '[0-9]+')
    printf '%s %s\n' "$distro" "$recipe"
}

# keys <workflow dir> - every `sdcard-image-distro-*` cache key, one per line
keys() { grep -rhoE 'sdcard-image-distro-[A-Za-z0-9.-]+' "$1"/*.yml 2>/dev/null | sort; }

# check_keys <workflow dir> <source.cpp> - print the offending keys; fail loud when it
# cannot read the source or finds no key at all (a check that finds nothing proves nothing).
check_keys() {
    local dir=$1 src=$2 exp want k bad="" n=0
    exp=$(read_expected "$src") || { echo "cannot read kDistroUrl / kFixedImageRecipe from $src"; return 2; }
    want="sdcard-image-distro-${exp% *}-recipe${exp#* }-"
    while IFS= read -r k; do
        [ -n "$k" ] || continue
        n=$((n + 1))
        [[ "$k" == "$want"* ]] || bad="$bad $k"
    done < <(keys "$dir")
    [ "$n" -gt 0 ] || { echo "no sdcard-image-distro key found under $dir"; return 2; }
    [ -z "$bad" ] || { echo "keys not $want*:$bad"; return 1; }
    return 0
}

# upgrade_order <workflow dir> - GH #580: in a fedora container, installing a -devel from `updates` upgrades its
# runtime library but not the base image's CLI of the same package (libzstd 1.6.0 beside zstd 1.5.7, whose CLI
# refuses it), and actions/cache runs the container's tar --use-compress-program unzstd: every restore failed. So a
# job whose container is fedora:* and that uses actions/cache must run `dnf upgrade -y` or `bash test/ci-deps.sh `
# (which upgrades) on a line BEFORE its first cache step. Prints "<file>:<job>" per offender; fails loud (2) when it
# finds no fedora job with a cache (a check that finds nothing proves nothing).
upgrade_order() {
    local out
    out=$(awk '
        function flush() {
            if (fed && cache) { n++; if (!upg || upg > cache) print FILENAME ":" job }
            fed = 0; cache = 0; upg = 0
        }
        FNR == 1 { flush(); job = "" }
        /^  [A-Za-z0-9_-]+:[ \t]*$/ { flush(); job = $1; sub(/:$/, "", job); next }
        /^[ \t]*#/ { next }
        /^[ \t]+(container|image):[ \t]*fedora:/ { fed = 1 }
        /uses:[ \t]*actions\/cache/ { if (!cache) cache = FNR }
        /dnf upgrade -y|bash test\/ci-deps\.sh / { if (!upg) upg = FNR }
        END { flush(); print "N=" n + 0 }' "$1"/*.yml)
    grep -q '^N=[1-9]' <<<"$out" || { echo "no fedora job with an actions/cache step found under $1"; return 2; }
    out=$(grep -v '^N=' <<<"$out" || true)
    [ -z "$out" ] || { echo "$out"; return 1; }
    return 0
}

SRC=src/core/sdcard_provisioner.cpp
WF=.github/workflows

out=$(check_keys "$WF" "$SRC"); rc=$?
if [ "$rc" -eq 0 ]; then ok "CIKEY-01 every sdcard-image-distro key names $(read_expected "$SRC" | tr ' ' '/') (distro/recipe of $SRC)"
else bad "CIKEY-01 $out"; fi

macos=$(keys "$WF" | grep -E -- '-macos$' | sort -u)
if [ "$(wc -l <<<"$macos")" -eq 1 ] && [ -n "$macos" ]; then ok "CIKEY-02 the macOS jobs share one cache key ($macos)"
else bad "CIKEY-02 the macOS keys are not one key: $(tr '\n' ' ' <<<"$macos")"; fi

# CIKEY-03: fixtures. A source and workflows that agree parse and pass; a stale key, a
# source with no definition, and a directory with no keys are each refused.
mkdir -p "$tmp/wf-ok" "$tmp/wf-stale" "$tmp/wf-none" "$tmp/wf-digit"
cat > "$tmp/src-ok.cpp" <<'CPP'
const char* const kDistroUrl =
    "https://www.specnext.com/distro/99.12/sn-emulator-99.12.zip";
const int kFixedImageRecipe = 7;
CPP
printf 'x: sdcard-image-distro-99.12-recipe7-macos\ny: sdcard-image-distro-99.12-recipe7-fedora44\n' > "$tmp/wf-ok/a.yml"
printf 'x: sdcard-image-distro-99.12-recipe7-macos\ny: sdcard-image-distro-99.11-recipe7-macos\n' > "$tmp/wf-stale/a.yml"
printf 'nothing: here\n' > "$tmp/wf-none/a.yml"
printf 'x: sdcard-image-distro-99.12-recipe70-macos\n' > "$tmp/wf-digit/a.yml"   # recipe 7 must not accept recipe 70
printf 'const int kFixedImageRecipe = 7;\n' > "$tmp/src-nodef.cpp"
msg=""
[ "$(read_expected "$tmp/src-ok.cpp")" = "99.12 7" ] || msg="$msg reader-wrong;"
check_keys "$tmp/wf-ok" "$tmp/src-ok.cpp" >/dev/null || msg="$msg agreeing-fixture-refused;"
check_keys "$tmp/wf-stale" "$tmp/src-ok.cpp" >/dev/null && msg="$msg stale-key-accepted;"
check_keys "$tmp/wf-ok" "$tmp/src-nodef.cpp" >/dev/null && msg="$msg missing-definition-accepted;"
check_keys "$tmp/wf-none" "$tmp/src-ok.cpp" >/dev/null && msg="$msg no-keys-accepted;"
check_keys "$tmp/wf-digit" "$tmp/src-ok.cpp" >/dev/null && msg="$msg recipe70-accepted-for-recipe7;"
if [ -z "$msg" ]; then ok "CIKEY-03 the parser reads a fixture source, and refuses a stale key, a missing definition and a directory with no keys"
else bad "CIKEY-03 $msg"; fi

# CIKEY-04: SpecNext's point releases carry a letter (sn-emulator-26.11a.zip).
mkdir -p "$tmp/wf-a" "$tmp/wf-noletter" "$tmp/wf-letter"
cat > "$tmp/src-a.cpp" <<'CPP'
const char* const kDistroUrl =
    "https://www.specnext.com/distro/99.12a/sn-emulator-99.12a.zip";
const int kFixedImageRecipe = 7;
CPP
printf 'x: sdcard-image-distro-99.12a-recipe7-macos\ny: sdcard-image-distro-99.12a-recipe7-fedora45\n' > "$tmp/wf-a/a.yml"
printf 'x: sdcard-image-distro-99.12-recipe7-macos\n' > "$tmp/wf-noletter/a.yml"
printf 'x: sdcard-image-distro-99.12a-recipe7-macos\n' > "$tmp/wf-letter/a.yml"
msg=""
[ "$(read_expected "$tmp/src-a.cpp")" = "99.12a 7" ] || msg="$msg letter-reader-wrong;"
check_keys "$tmp/wf-a" "$tmp/src-a.cpp" >/dev/null || msg="$msg agreeing-letter-fixture-refused;"
check_keys "$tmp/wf-noletter" "$tmp/src-a.cpp" >/dev/null && msg="$msg letterless-key-accepted-for-letter-source;"
check_keys "$tmp/wf-letter" "$tmp/src-ok.cpp" >/dev/null && msg="$msg letter-key-accepted-for-letterless-source;"
if [ -z "$msg" ]; then ok "CIKEY-04 a trailing letter in the distribution name parses, and the key must match it exactly"
else bad "CIKEY-04 $msg"; fi

# CIKEY-05 (GH #555): the cache holds the SD directory in its layout since the suites' master moved to
# ~/.jnext/sdcard/test-suite/; every real key carries the `-ts-` layout marker after the recipe, or an
# entry saved in the old layout would restore with the test master missing (and never be re-saved).
tsbad=""
while IFS= read -r k; do
    [ -n "$k" ] || continue
    [[ "$k" == *-recipe[0-9]*-ts-* ]] || tsbad="$tsbad $k"
done < <(keys "$WF")
printf 'x: sdcard-image-distro-99.12-recipe7-macos\n' > "$tmp/wf-ok/old.yml"
oldlayout=""
while IFS= read -r k; do [[ "$k" == *-recipe[0-9]*-ts-* ]] || oldlayout="$oldlayout $k"; done < <(keys "$tmp/wf-ok")
if [ -z "$tsbad" ] && [ -n "$oldlayout" ]; then ok "CIKEY-05 every sdcard cache key carries the -ts- layout marker (and a key without it is detected)"
else bad "CIKEY-05 keys without -ts-:$tsbad (detector saw old-layout keys: ${oldlayout:+yes}${oldlayout:-NO})"; fi

# CIKEY-06 (GH #580): the real workflows pass; fixtures pin the rule both ways.
mkdir -p "$tmp/up-comment" "$tmp/up-bad" "$tmp/up-ok" "$tmp/up-late" "$tmp/up-ubuntu" "$tmp/up-deps" "$tmp/up-none"
job() {   # job <container> <steps...>: a one-job workflow on stdout
    printf 'jobs:\n  build:\n    runs-on: ubuntu-latest\n    container: %s\n    steps:\n' "$1"; shift
    printf '%s\n' "$@"
}
job fedora:45 '      - run: dnf install -y git' '      - uses: actions/cache@v5' > "$tmp/up-bad/a.yml"
job fedora:45 '      - run: |' '          dnf upgrade -y' '          dnf install -y git' '      - uses: actions/cache/restore@v5' > "$tmp/up-ok/a.yml"
job fedora:45 '      - uses: actions/cache@v5' '      - run: dnf upgrade -y' > "$tmp/up-late/a.yml"
job ubuntu:24.04 '      - run: apt-get install -y git' '      - uses: actions/cache@v5' > "$tmp/up-ubuntu/a.yml"
job fedora:45 '      # dnf upgrade -y (commented out: does not count)' '      - run: dnf install -y git' '      - uses: actions/cache@v5' > "$tmp/up-comment/a.yml"
job fedora:45 '      # dnf upgrade -y (a comment does not count)' '      - run: bash test/ci-deps.sh test' '      - uses: actions/cache@v5' > "$tmp/up-deps/a.yml"
job fedora:45 '      - run: dnf install -y git' > "$tmp/up-none/a.yml"
out=$(upgrade_order "$WF"); rc=$?
msg=""
[ "$rc" -eq 0 ] || msg="$msg real-workflows: $out;"
upgrade_order "$tmp/up-bad" >/dev/null && msg="$msg no-upgrade-accepted;"
upgrade_order "$tmp/up-ok" >/dev/null || msg="$msg upgrade-before-cache-refused;"
upgrade_order "$tmp/up-late" >/dev/null && msg="$msg upgrade-after-cache-accepted;"
upgrade_order "$tmp/up-ubuntu" >/dev/null 2>&1 && msg="$msg ubuntu-only-treated-as-found;"
upgrade_order "$tmp/up-deps" >/dev/null || msg="$msg ci-deps-before-cache-refused;"
upgrade_order "$tmp/up-comment" >/dev/null && msg="$msg commented-upgrade-accepted;"
upgrade_order "$tmp/up-none" >/dev/null && msg="$msg no-cache-job-accepted;"
if [ -z "$msg" ]; then ok "CIKEY-06 every fedora container job that uses actions/cache upgrades the image before its first cache step (and a job without it, or with it after the cache, is refused)"
else bad "CIKEY-06 $msg"; fi

printf '\nTotal: %d  Pass: %d  Fail: %d\n' $((pass+fail)) "$pass" "$fail"
[ "$fail" -eq 0 ]
