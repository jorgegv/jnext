#!/usr/bin/env bash
#
# Contract test for packaging/web/package-web.sh (`make web-package-test`): the
# browser zip jnext-<ver>-web.zip. Hermetic: fake root, fake emsdk and fake build
# trees under a scratch directory, no emcc, no browser, no SD-card image. The
# scratch directory is removed on exit.
#
# Oracles: the package contract of this suite's own literal 21-name list (the
# issue's scope: js + wasm, example pages, README.txt with the source link,
# LICENSE, CREDITS.md, licences of the linked libraries; and NEVER a NextZXOS
# image, owner rule); version.yaml as the only source of the version; the
# WebAssembly module preamble (core spec); the zip format as CPython's zipfile
# reads it (date_time, external_attr, extra); GitHub Actions workflow syntax as
# PyYAML parses the real release.yml and ci.yml. There is no VHDL: this is packaging.
#
# Rows WEBPKG-01 .. WEBPKG-36. A missing tool FAILS WEBPKG-01 and every other row
# then reports FAIL "not run": there is no SKIP path.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EXPECTED_ROWS=36
EPOCH_FIX=1762600000

total=0; passed=0; failed=0
ids_seen=" "
row() {
    local id=$1 ok=$2 text=$3
    total=$((total + 1))
    if [[ "$ids_seen" == *" $id "* ]]; then
        echo "  FAIL $id duplicate row id"; failed=$((failed + 1)); return
    fi
    ids_seen+="$id "
    if [[ "$ok" == 1 ]]; then
        echo "  PASS $id $text"; passed=$((passed + 1))
    else
        echo "  FAIL $id $text"; failed=$((failed + 1))
    fi
}

finish() {
    printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
    if [[ $total -ne $EXPECTED_ROWS ]]; then
        echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1
    fi
    [[ $failed -eq 0 ]] || exit 1
    exit 0
}

# not_run <first-row-number>: every remaining row FAILS, never skips
not_run() {
    local i
    for ((i = $1; i <= EXPECTED_ROWS; i++)); do
        total=$((total + 1)); failed=$((failed + 1)); printf '  FAIL WEBPKG-%02d not run: WEBPKG-01 failed\n' "$i"
    done
    finish
}

T=$(mktemp -d "${TMPDIR:-$HOME/tmp}/webpkg-test.XXXXXX" 2>/dev/null || mktemp -d)
trap 'rm -rf "$T"' EXIT

# ---- the tools ----
missing=()
for t in zip python3 git sha256sum sed find sort cmp truncate; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
python3 -I -c 'import yaml' 2>/dev/null || missing+=("python3-pyyaml")

# ---- fixtures (made once, copied per scenario) ----
mkbase() {
    local b=$T/base
    mkdir -p "$b/root/packaging/web" "$b/root/third_party/spdlog" "$b/root/third_party/nlohmann-json" \
             "$b/root/third_party/zot" "$b/root/third_party/fatfs"
    cp "$ROOT/packaging/web/package-web.sh" "$ROOT/packaging/web/README.txt.in" "$b/root/packaging/web/"
    cp "$ROOT/LICENSE" "$ROOT/CREDITS.md" "$b/root/"
    cp "$ROOT/third_party/spdlog/LICENSE" "$b/root/third_party/spdlog/"
    cp "$ROOT/third_party/nlohmann-json/LICENSE.MIT" "$b/root/third_party/nlohmann-json/"
    cp "$ROOT/third_party/zot/LICENSE" "$b/root/third_party/zot/"
    cp "$ROOT/third_party/fatfs/LICENSE.txt" "$b/root/third_party/fatfs/"
    local em=$b/emsdk/upstream/emscripten
    local lib=$em/system/lib
    mkdir -p "$lib/libc/musl" "$lib/libcxx" "$lib/libcxxabi" "$lib/libunwind" "$lib/compiler-rt" \
             "$em/cache/ports/sdl3/SDL-release-9.9" "$em/cache/ports/zlib/zlib-9.9" "$em/cache/ports/libpng/libpng-9.9"
    echo "emscripten licence text" >"$em/LICENSE"
    echo "musl copyright text" >"$lib/libc/musl/COPYRIGHT"
    echo "libcxx licence text" >"$lib/libcxx/LICENSE.TXT"
    echo "libcxxabi licence text" >"$lib/libcxxabi/LICENSE.TXT"
    echo "libunwind licence text" >"$lib/libunwind/LICENSE.TXT"
    echo "compiler-rt licence text" >"$lib/compiler-rt/LICENSE.TXT"
    echo "sdl3 licence text" >"$em/cache/ports/sdl3/SDL-release-9.9/LICENSE.txt"
    echo "zlib licence text" >"$em/cache/ports/zlib/zlib-9.9/LICENSE"
    echo "libpng licence text" >"$em/cache/ports/libpng/libpng-9.9/LICENSE"
}

# mkjs <file> <bytes> <wasm-name-literal> <version-literal>: a fake jnext js of about <bytes>
mkjs() {
    { echo "var createJnextModule = 1;"; echo "// $3"; echo "var VERSION = '$4';"; } >"$1"
    local have; have=$(stat -c %s "$1")
    [[ $have -ge $2 ]] || head -c $(($2 - have)) /dev/zero | tr '\0' 'x' >>"$1"
}
# mkwasm <file> <bytes>: the preamble, then a sparse tail
mkwasm() { printf '\0asm\1\0\0\0' >"$1"; truncate -s "$2" "$1"; }
# mktree <dir> <version>: a fake Release wasm-core tree, with stale and stray files beside the build
mktree() {
    local d=$1 v=$2 p
    mkdir -p "$d"
    echo "CMAKE_BUILD_TYPE:STRING=Release" >"$d/CMakeCache.txt"
    mkjs "$d/jnext-$v.js" 20480 "jnext-$v.wasm" "$v"
    mkwasm "$d/jnext-$v.wasm" $((2 * 1024 * 1024))
    for p in plain itch-host itch-game; do
        sed "s/@PROJECT_VERSION@/$v/g" "$ROOT/web/examples/$p.html.in" >"$d/$p.html"
    done
    mkjs "$d/jnext-9.9.8.js" 20480 "jnext-9.9.8.wasm" "9.9.8"
    mkwasm "$d/jnext-9.9.8.wasm" $((2 * 1024 * 1024))
    echo "<html>stray</html>" >"$d/stray.html"
}
# scen <name> [version]: a fresh fake root + emsdk + build under $T/<name>; sets D
scen() {
    local v=${2:-9.9.9}
    D=$T/$1
    mkdir -p "$D"
    cp -a "$T/base/root" "$D/root"
    cp -a "$T/base/emsdk" "$D/emsdk"
    echo "version: $v" >"$D/root/version.yaml"
    mktree "$D/build" "$v"
}
EP="SOURCE_DATE_EPOCH=$EPOCH_FIX"
# run_pkg <dir> [env...]: build mode in scenario <dir>; sets RC, OUT; stderr in <dir>/stderr
run_pkg() {
    local d=$1; shift
    RC=0
    OUT=$(env -u SOURCE_DATE_EPOCH ${EP:+"$EP"} "$@" bash "$d/root/packaging/web/package-web.sh" "$d/build" "$d/emsdk" 2>"$d/stderr") || RC=$?
}
# run_verify <dir> <zip>: --verify mode; sets RC; stderr in <dir>/stderr
run_verify() {
    RC=0
    bash "$1/root/packaging/web/package-web.sh" --verify "$2" "$1/build" "$1/emsdk" >/dev/null 2>"$1/stderr" || RC=$?
}
nozip() { ! compgen -G "$1/build/*.zip" >/dev/null; }
errhas() { grep -qF -- "$2" "$1/stderr"; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

# mkzip.py <src.zip> <out.zip> op...: rewrite a zip with edits, by a foreign writer (zipfile)
cat >"$T/mkzip.py" <<'PYEOF'
import sys, zipfile
src, out = sys.argv[1:3]
ents = []
with zipfile.ZipFile(src) as z:
    for i in z.infolist():
        ents.append([i.filename, z.read(i.filename), i.date_time, i.external_attr])
def find(n):
    for e in ents:
        if e[0] == n: return e
    return None
for op in sys.argv[3:]:
    k, *a = op.split(":")
    if k == "drop":
        ents = [e for e in ents if e[0] != a[0]]
    elif k in ("add", "zeros"):
        e = find(a[0])
        if e: e[1] = bytes(int(a[1]))
        else: ents.append([a[0], bytes(int(a[1])), (2025, 1, 1, 0, 0, 0), 0o100644 << 16])
    elif k == "trunc":
        find(a[0])[1] = find(a[0])[1][:int(a[1])]
    elif k == "sym":
        e = find(a[0]); e[1] = a[1].encode(); e[3] = 0o120777 << 16
    elif k == "flip":
        e = find(a[0]); e[1] = bytes([e[1][0] ^ 1]) + e[1][1:]
    elif k == "nopre":
        e = find(a[0]); e[1] = b"\x01\x02\x03\x04" + e[1][4:]
    else:
        sys.exit("bad op " + op)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n, d, dt, ea in ents:
        zi = zipfile.ZipInfo(n, dt)
        zi.external_attr = ea
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, d)
PYEOF

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "  missing tools: ${missing[*]} (dnf install zip python3 python3-pyyaml git coreutils findutils)"
    row WEBPKG-01 0 "tools are present and the good tree builds"
    not_run 2
fi

mkbase

# ---- WEBPKG-01: the good tree builds ----
scen good
run_pkg "$D"
GOOD=$D
ZIP1=$GOOD/build/jnext-9.9.9-web.zip
last=$(tail -n1 <<<"$OUT")
nz=$(compgen -G "$GOOD/build/jnext-*-web.zip" | wc -l)
ok=0
[[ $RC -eq 0 && "$last" == "$ZIP1" && -f "$ZIP1" && $nz -eq 1 ]] && ok=1
row WEBPKG-01 "$ok" "tools present; build exits 0, one jnext-9.9.9-web.zip in the build dir, last stdout line is its path"
[[ $ok == 1 ]] || tail -n 5 "$GOOD/stderr"
[[ -f "$ZIP1" ]] || not_run 2
sha1=$(sha "$ZIP1")

# ---- WEBPKG-02: exactly the 21 names ----
cat >"$T/want-names" <<'NAMES'
jnext-9.9.9-web/CREDITS.md
jnext-9.9.9-web/LICENSE
jnext-9.9.9-web/README.txt
jnext-9.9.9-web/itch-game.html
jnext-9.9.9-web/itch-host.html
jnext-9.9.9-web/jnext-9.9.9.js
jnext-9.9.9-web/jnext-9.9.9.wasm
jnext-9.9.9-web/licenses/compiler-rt.txt
jnext-9.9.9-web/licenses/emscripten.txt
jnext-9.9.9-web/licenses/fatfs.txt
jnext-9.9.9-web/licenses/libcxx.txt
jnext-9.9.9-web/licenses/libcxxabi.txt
jnext-9.9.9-web/licenses/libpng.txt
jnext-9.9.9-web/licenses/libunwind.txt
jnext-9.9.9-web/licenses/musl.txt
jnext-9.9.9-web/licenses/nlohmann-json.txt
jnext-9.9.9-web/licenses/sdl3.txt
jnext-9.9.9-web/licenses/spdlog.txt
jnext-9.9.9-web/licenses/zlib.txt
jnext-9.9.9-web/licenses/zot.txt
jnext-9.9.9-web/plain.html
NAMES
python3 -I -c 'import sys, zipfile
print("\n".join(sorted(zipfile.ZipFile(sys.argv[1]).namelist())))' "$ZIP1" >"$T/got-names"
ok=0
[[ $(wc -l <"$T/want-names") -eq 21 ]] && cmp -s "$T/want-names" "$T/got-names" && ok=1
row WEBPKG-02 "$ok" "the zip holds exactly the literal 21 names, no directory entry, no stale jnext-9.9.8.*, no stray.html"

# ---- WEBPKG-03: every entry equals its fixture source ----
mkdir "$T/x1"
python3 -I -m zipfile -e "$ZIP1" "$T/x1"
X=$T/x1/jnext-9.9.9-web
B=$GOOD/build; R=$GOOD/root; E=$GOOD/emsdk/upstream/emscripten
sed 's/@VERSION@/9.9.9/g' "$R/packaging/web/README.txt.in" >"$T/readme-want"
ok=1
chk() { cmp -s "$X/$1" "$2" || { echo "    differs: $1"; ok=0; }; }
chk jnext-9.9.9.js "$B/jnext-9.9.9.js"; chk jnext-9.9.9.wasm "$B/jnext-9.9.9.wasm"
chk plain.html "$B/plain.html"; chk itch-host.html "$B/itch-host.html"; chk itch-game.html "$B/itch-game.html"
chk README.txt "$T/readme-want"; chk LICENSE "$R/LICENSE"; chk CREDITS.md "$R/CREDITS.md"
chk licenses/emscripten.txt "$E/LICENSE"; chk licenses/musl.txt "$E/system/lib/libc/musl/COPYRIGHT"
chk licenses/libcxx.txt "$E/system/lib/libcxx/LICENSE.TXT"; chk licenses/libcxxabi.txt "$E/system/lib/libcxxabi/LICENSE.TXT"
chk licenses/libunwind.txt "$E/system/lib/libunwind/LICENSE.TXT"; chk licenses/compiler-rt.txt "$E/system/lib/compiler-rt/LICENSE.TXT"
chk licenses/sdl3.txt "$E/cache/ports/sdl3/SDL-release-9.9/LICENSE.txt"; chk licenses/zlib.txt "$E/cache/ports/zlib/zlib-9.9/LICENSE"
chk licenses/libpng.txt "$E/cache/ports/libpng/libpng-9.9/LICENSE"
chk licenses/spdlog.txt "$R/third_party/spdlog/LICENSE"; chk licenses/nlohmann-json.txt "$R/third_party/nlohmann-json/LICENSE.MIT"
chk licenses/zot.txt "$R/third_party/zot/LICENSE"; chk licenses/fatfs.txt "$R/third_party/fatfs/LICENSE.txt"
if grep -qF '@' "$X/README.txt"; then ok=0; fi
grep -qF '/tree/v9.9.9' "$X/README.txt" || ok=0
row WEBPKG-03 "$ok" "every entry is byte-identical to its fixture; README.txt is the template with 9.9.9 and has no @"

# ---- WEBPKG-04: the version follows version.yaml ----
scen v10 9.9.10
run_pkg "$D"
ok=0
if [[ $RC -eq 0 && -f "$D/build/jnext-9.9.10-web.zip" ]]; then
    n=$(python3 -I -c 'import sys, zipfile
n = zipfile.ZipFile(sys.argv[1]).namelist()
print(all(x.startswith("jnext-9.9.10-web/") for x in n) and "jnext-9.9.10-web/jnext-9.9.10.js" in n and "jnext-9.9.10-web/jnext-9.9.10.wasm" in n and len(n) == 21)' "$D/build/jnext-9.9.10-web.zip")
    [[ "$n" == True ]] && ok=1
fi
row WEBPKG-04 "$ok" "version.yaml 9.9.10: output jnext-9.9.10-web.zip with 9.9.10 names"

# ---- WEBPKG-05: version.yaml says 9.9.9, tree has only 9.9.8 ----
scen stale
rm "$D/build/jnext-9.9.9.js" "$D/build/jnext-9.9.9.wasm"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "jnext-9.9.9.js" && nozip "$D" && ok=1
row WEBPKG-05 "$ok" "a tree holding only jnext-9.9.8.*: refused, stderr names jnext-9.9.9.js, no zip"

# ---- WEBPKG-06: malformed version ----
scen badver
echo "version: 9.9" >"$D/root/version.yaml"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "version.yaml" && nozip "$D" && ok=1
row WEBPKG-06 "$ok" "version: 9.9 refused, stderr names version.yaml, no zip"

# ---- WEBPKG-07: not a Release tree ----
scen rwdi
echo "CMAKE_BUILD_TYPE:STRING=RelWithDebInfo" >"$D/build/CMakeCache.txt"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "CMAKE_BUILD_TYPE" && nozip "$D" && ok=1
row WEBPKG-07 "$ok" "RelWithDebInfo tree refused, stderr names the build type, no zip"

# ---- WEBPKG-08: a licence source is missing ----
scen nomusl
rm "$D/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "missing source" && errhas "$D" "musl/COPYRIGHT" && nozip "$D" && ok=1
row WEBPKG-08 "$ok" "emsdk without musl/COPYRIGHT refused, stderr names the path, no zip"

# ---- WEBPKG-09: ambiguous port directory ----
scen twosdl
mkdir "$D/emsdk/upstream/emscripten/cache/ports/sdl3/SDL-release-8.8"
echo "other sdl" >"$D/emsdk/upstream/emscripten/cache/ports/sdl3/SDL-release-8.8/LICENSE.txt"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "exactly one" && errhas "$D" "SDL-release" && nozip "$D" && ok=1
row WEBPKG-09 "$ok" "two sdl3/SDL-release-* directories refused as ambiguous, no zip"

# ---- WEBPKG-10: a wasm with no preamble: refused, no zip left ----
scen nopre
printf 'NOTWASM!' >"$D/build/jnext-9.9.9.wasm"; truncate -s 2M "$D/build/jnext-9.9.9.wasm"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && nozip "$D" && errhas "$D" "preamble" && ok=1
row WEBPKG-10 "$ok" "a wasm without the preamble: build mode non-zero and no jnext-9.9.9-web.zip left"

# ---- WEBPKG-11: a second run is byte-identical ----
run_pkg "$GOOD"
ok=0; [[ $RC -eq 0 && "$(sha "$ZIP1")" == "$sha1" ]] && ok=1
row WEBPKG-11 "$ok" "a second run on the same tree gives a byte-identical zip"

# ---- WEBPKG-12: a different caller gives the same zip ----
find "$GOOD/build" "$GOOD/root" "$GOOD/emsdk" -type f -exec touch -d '2001-02-03 04:05:06' {} +
RC=0
( umask 077; env -u SOURCE_DATE_EPOCH "$EP" TZ=Asia/Kolkata LC_ALL=es_ES.UTF-8 \
    bash "$GOOD/root/packaging/web/package-web.sh" "$GOOD/build" "$GOOD/emsdk" >/dev/null 2>"$GOOD/stderr" ) || RC=$?
ok=0; [[ $RC -eq 0 && "$(sha "$ZIP1")" == "$sha1" ]] && ok=1
row WEBPKG-12 "$ok" "re-touched sources, umask 077, TZ=Asia/Kolkata, es_ES locale in the caller: byte-identical zip"

# ---- WEBPKG-13: the mechanism ----
r=$(python3 -I -c 'import sys, time, zipfile
z = zipfile.ZipFile(sys.argv[1]); want = time.gmtime(int(sys.argv[2]))[:6]
infos = z.infolist(); names = [i.filename for i in infos]
print(all(i.date_time == want and (i.external_attr >> 16) == 0o100644 and i.extra == b"" for i in infos) and names == sorted(names, key=lambda s: s.encode()))' "$ZIP1" "$EPOCH_FIX")
ok=0; [[ "$r" == True ]] && ok=1
row WEBPKG-13 "$ok" "every entry: date = SOURCE_DATE_EPOCH in UTC, mode 0100644, no extra field; names in C order"

# ---- WEBPKG-14: no date source ----
scen nodate
EP="" run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "SOURCE_DATE_EPOCH" && nozip "$D" && ok=1
row WEBPKG-14 "$ok" "no SOURCE_DATE_EPOCH and no git checkout: refused naming SOURCE_DATE_EPOCH, no zip"

# ---- WEBPKG-15: the HEAD commit time ----
scen gitdate
(
    cd "$D/root" || exit 1
    git init -q . && git add -A
    tree=$(git write-tree)
    c=$(GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t \
        GIT_AUTHOR_DATE="1700000000 +0000" GIT_COMMITTER_DATE="1700000000 +0000" git commit-tree "$tree" -m x)
    git update-ref HEAD "$c"
) >/dev/null 2>&1
EP="" run_pkg "$D"
r=x
[[ -f "$D/build/jnext-9.9.9-web.zip" ]] && r=$(python3 -I -c 'import sys, time, zipfile
want = time.gmtime(1700000000)[:6]
print(all(i.date_time == want for i in zipfile.ZipFile(sys.argv[1]).infolist()))' "$D/build/jnext-9.9.9-web.zip")
ok=0; [[ $RC -eq 0 && "$r" == True ]] && ok=1
row WEBPKG-15 "$ok" "no SOURCE_DATE_EPOCH, git checkout: every entry carries the HEAD commit time in UTC"

# ---- WEBPKG-16..21, 29, 31..33: --verify on the good zip and on crafted ones ----
mz() { python3 -I "$T/mkzip.py" "$ZIP1" "$@"; }
run_verify "$GOOD" "$ZIP1"
ok=0; [[ $RC -eq 0 ]] && ok=1
row WEBPKG-16 "$ok" "--verify on the good zip exits 0 (positive control)"

mz "$T/c17.zip" add:jnext-9.9.9-web/nextzxos-minimal.img:10
run_verify "$GOOD" "$T/c17.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "nextzxos-minimal.img has a NextZXOS image name" && ok=1
row WEBPKG-17 "$ok" "zip plus nextzxos-minimal.img refused as a NextZXOS image name"

mz "$T/c18.zip" add:card.IMG:10 add:jnext-9.9.9-web/stray.txt:10
run_verify "$GOOD" "$T/c18.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "card.IMG has a NextZXOS image name" && errhas "$GOOD" "unexpected entry jnext-9.9.9-web/stray.txt" && ok=1
row WEBPKG-18 "$ok" "card.IMG outside the top directory refused, named; any other extra entry refused too"

mz "$T/c19.zip" add:jnext-9.9.9-web/licenses/x/boot.mmc:10
run_verify "$GOOD" "$T/c19.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "licenses/x/boot.mmc has a NextZXOS image name" && ok=1
row WEBPKG-19 "$ok" "a nested boot.mmc refused, named"

mz "$T/c20.zip" drop:jnext-9.9.9-web/jnext-9.9.9.wasm
run_verify "$GOOD" "$T/c20.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "missing entry jnext-9.9.9-web/jnext-9.9.9.wasm" && ok=1
row WEBPKG-20 "$ok" "a zip without the .wasm refused, naming the missing entry"

mz "$T/c21.zip" nopre:jnext-9.9.9-web/jnext-9.9.9.wasm
run_verify "$GOOD" "$T/c21.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "preamble" && ok=1
row WEBPKG-21 "$ok" "a wasm entry without the preamble refused"

# ---- WEBPKG-22..26: size caps and js literals, on built trees ----
scen cap16
mkwasm "$D/build/jnext-9.9.9.wasm" $((16 * 1024 * 1024))
run_pkg "$D"
ok=0; [[ $RC -eq 0 && -f "$D/build/jnext-9.9.9-web.zip" ]] && ok=1
row WEBPKG-22 "$ok" "a wasm of exactly 16 MiB builds (the boundary)"

scen cap16p
mkwasm "$D/build/jnext-9.9.9.wasm" $((16 * 1024 * 1024 + 1))
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "jnext-9.9.9.wasm is 16777217 bytes" && nozip "$D" && ok=1
row WEBPKG-23 "$ok" "a wasm of 16 MiB + 1 byte refused on size, no zip (stands for an embedded card)"

scen js100
printf "createJnextModule jnext-9.9.9.wasm '9.9.9'" >"$D/build/jnext-9.9.9.js"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "jnext-9.9.9.js is" && nozip "$D" && ok=1
row WEBPKG-24 "$ok" "a js of under 100 bytes with all three literals refused (the floor)"

scen jsnowasm
mkjs "$D/build/jnext-9.9.9.js" 20480 "jnext.wasm" "9.9.9"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "does not contain jnext-9.9.9.wasm" && nozip "$D" && ok=1
row WEBPKG-25 "$ok" "a js naming jnext.wasm but not jnext-9.9.9.wasm refused"

scen jsnover
mkjs "$D/build/jnext-9.9.9.js" 20480 "jnext-9.9.9.wasm" "0.0.0"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "does not contain '9.9.9'" && nozip "$D" && ok=1
row WEBPKG-26 "$ok" "a js without the '9.9.9' API version literal refused"

# ---- WEBPKG-27..28: the pages ----
scen pgstale
sed 's/jnext-9.9.9.js/jnext-9.9.8.js/' "$D/build/plain.html" >"$D/p" && mv "$D/p" "$D/build/plain.html"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "plain.html refers to jnext-9.9.8" && nozip "$D" && ok=1
row WEBPKG-27 "$ok" "plain.html referencing jnext-9.9.8.js refused, naming the page and 9.9.8"

scen pgnone
grep -v 'jnext-9.9.9.js' "$D/build/plain.html" >"$D/p"; mv "$D/p" "$D/build/plain.html"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "plain.html does not load jnext-9.9.9.js" && nozip "$D" && ok=1
row WEBPKG-28 "$ok" "plain.html with no jnext-9.9.9.js refused"

# ---- WEBPKG-29: a huge page entry ----
mz "$T/c29.zip" zeros:jnext-9.9.9-web/plain.html:41943040
run_verify "$GOOD" "$T/c29.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "plain.html is 41943040 bytes" && ok=1
row WEBPKG-29 "$ok" "a plain.html entry of 40 MiB refused on size, the message naming it"

# ---- WEBPKG-30: no source link in the README template ----
scen nolink
sed 's#/tree/v@VERSION@#/tree/main#' "$D/root/packaging/web/README.txt.in" >"$D/t" && mv "$D/t" "$D/root/packaging/web/README.txt.in"
run_pkg "$D"
ok=0; [[ $RC -ne 0 ]] && errhas "$D" "/tree/v9.9.9" && nozip "$D" && ok=1
row WEBPKG-30 "$ok" "a README template without /tree/v@VERSION@: build refused, no zip"

# ---- WEBPKG-31: wrong zip name ----
cp "$ZIP1" "$T/jnext-9.9.8-web.zip"
run_verify "$GOOD" "$T/jnext-9.9.8-web.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "is not jnext-9.9.9-web.zip" && ok=1
row WEBPKG-31 "$ok" "the good entries in a zip named jnext-9.9.8-web.zip refused for 9.9.9"

# ---- WEBPKG-32: truncated LICENSE ----
mz "$T/c32.zip" trunc:jnext-9.9.9-web/LICENSE:100
run_verify "$GOOD" "$T/c32.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "LICENSE" && errhas "$GOOD" "differs from its source" && ok=1
mz "$T/c32b.zip" flip:jnext-9.9.9-web/CREDITS.md
run_verify "$GOOD" "$T/c32b.zip"
[[ $RC -ne 0 ]] && errhas "$GOOD" "CREDITS.md differs from its source" || ok=0
row WEBPKG-32 "$ok" "a truncated LICENSE entry and a same-size CREDITS.md with one bit flipped refused (identity)"

# ---- WEBPKG-33: a symlink entry ----
mz "$T/c33.zip" sym:jnext-9.9.9-web/CREDITS.md:/etc/passwd
run_verify "$GOOD" "$T/c33.zip"
ok=0; [[ $RC -ne 0 ]] && errhas "$GOOD" "CREDITS.md is a symlink" && ok=1
row WEBPKG-33 "$ok" "CREDITS.md as a symlink entry refused"

# ---- WEBPKG-34..36: the workflows ----
r=$(python3 -I -c 'import sys, yaml
w = yaml.safe_load(open(sys.argv[1]))
j = w["jobs"]; web = j.get("web")
need = lambda x: [x] if isinstance(x, str) else list(x or [])
ok = web is not None
if ok:
    ok = ("gate" in need(web.get("needs")) and web.get("if") == "needs.gate.outputs.build == \x27true\x27"
          and "continue-on-error" not in web
          and sum(1 for s in web["steps"] if str(s.get("run", "")).strip() == "LANG=C make package-web") == 1
          and "web" in need(j["publish"].get("needs")))
print(ok)' "$ROOT/.github/workflows/release.yml")
ok=0; [[ "$r" == True ]] && ok=1
row WEBPKG-34 "$ok" "release.yml: blocking job web (needs gate, build gate, LANG=C make package-web); publish needs it"

r=$(python3 -I -c 'import sys, yaml
web = yaml.safe_load(open(sys.argv[1]))["jobs"].get("web")
ok = web is not None
if ok:
    ups = [s for s in web["steps"] if str(s.get("uses", "")).startswith("actions/upload-artifact")]
    ok = (len(ups) >= 1 and not any(".jnext" in str(s) for s in web["steps"])
          and all(str(s["with"].get("path", "")).strip() == "build/wasm-core/jnext-*-web.zip"
                  and s["with"].get("if-no-files-found") == "error" for s in ups))
print(ok)' "$ROOT/.github/workflows/release.yml")
ok=0; [[ "$r" == True ]] && ok=1
row WEBPKG-35 "$ok" "release.yml web: no SD-image cache; every upload is the one exact zip glob, error if none"

r=$(python3 -I -c 'import sys, yaml
j = yaml.safe_load(open(sys.argv[1]))["jobs"]
ok = False
for job in j.values():
    steps = job.get("steps", [])
    if any(str(s.get("run", "")).strip() == "LANG=C make wasm-browser-test" for s in steps):
        ups = [s for s in steps if str(s.get("uses", "")).startswith("actions/upload-artifact")
               and s.get("with", {}).get("name") == "jnext-web-zip"]
        ok = (len(ups) == 1 and str(ups[0]["with"].get("path", "")).strip() == "build/wasm-core/jnext-*-web.zip"
              and ups[0]["with"].get("if-no-files-found") == "error")
print(ok)' "$ROOT/.github/workflows/ci.yml")
ok=0; [[ "$r" == True ]] && ok=1
row WEBPKG-36 "$ok" "ci.yml: the browser leg uploads jnext-web-zip, the one exact zip glob, error if none"

finish
