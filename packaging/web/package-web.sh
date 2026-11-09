#!/usr/bin/env bash
#
# package-web.sh - zip the browser build: jnext-<ver>-web.zip (`make package-web`).
#
#   package-web.sh <build-dir> <emsdk-dir>
#       Build <build-dir>/jnext-<ver>-web.zip from the `jnext_web` output of the
#       WebAssembly tree <build-dir> (build/wasm-core), verify it, print its path.
#   package-web.sh --verify <zip> <build-dir> <emsdk-dir>
#       Check a zip against its sources; exit non-zero on any failure.
#
# WHAT IT HOLDS (one list, manifest() below): jnext-<ver>.js and .wasm, the three
# example pages, README.txt (with the source link), LICENSE, CREDITS.md and
# licenses/, the licence texts of the libraries linked into jnext_web. The
# <ver> comes from version.yaml and nowhere else.
#
# NEVER A NextZXOS IMAGE (copyrighted: owner rule, CLAUDE.md). The page fetches
# the system image from SpecNext at run time. The guard, in verify:
#   - the entry names must equal the manifest exactly (an extra file is refused);
#   - a name ending in .img or .mmc is refused at any depth, in any case;
#   - size caps read from the central directory: the wasm at most 16 MiB, so an
#     image embedded raw into the module (Emscripten --embed-file stores bytes
#     uncompressed; the minimal card is 40 MiB) or renamed into the package is
#     refused; every other file has a cap too.
# SCOPE: it does not try to catch an image deliberately compressed and embedded
# into the module, nor a crafted zip whose central directory lies.
#
# REPRODUCIBLE: the same sources give a byte-identical zip whatever the caller's
# TZ, umask, locale or file mtimes. Entry time = SOURCE_DATE_EPOCH, else the
# commit time of HEAD (never "now"); TZ=UTC (zip stores local DOS time); mode
# 0644; names sorted under LC_ALL=C; `zip -X -D` (no uid/gid, no directory entries).
#
# LINK SET of jnext_web, measured 2026-11-08 (verbose wasm-ld line of the
# `jnext_web` relink; emcc -v): jnext's own libraries; libspdlog, libzot_tzx,
# FatFs (in jnext_core), nlohmann-json (header only); the ports SDL3, zlib and
# libpng; musl and Emscripten's own libraries (-lc -lhtml5 -lGL-getprocaddr -lal
# -lsockets -lstubs -lnoexit -ldlmalloc, covered by Emscripten's LICENSE;
# dlmalloc is public domain); LLVM's libc++, libc++abi, libunwind and
# compiler-rt. A library linked later needs a line in manifest() below; no
# test can see the link set.
#
# Standalone script, run with bash.
set -euo pipefail
export LC_ALL=C TZ=UTC

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PAGES=(plain.html itch-host.html itch-game.html)

die() { echo "package-web: $*" >&2; exit 1; }

# The version, from version.yaml only (same pattern as gen-debian-changelog.sh).
read_version() {
    local f="$ROOT/version.yaml" line
    [[ -f "$f" ]] || die "$f is missing"
    line=$(grep -E '^version: *[0-9]+\.[0-9]+\.[0-9]+ *$' "$f" | head -n1 || true)
    [[ -n "$line" ]] || die "no valid 'version: X.Y.Z' line in $f"
    line=${line#version:}
    echo "${line//[[:space:]]/}"
}

# manifest <ver> <build> <emsdk> <readme-file>: print "name<TAB>source" for every
# entry, names relative to the top directory. Prints each unresolvable source to
# stderr and returns 1 if there is any.
manifest() {
    local ver=$1 build=$2 emsdk=$3 readme=$4 bad=0
    local em="$emsdk/upstream/emscripten" lib="$emsdk/upstream/emscripten/system/lib"
    ent() {
        if [[ ! -f "$2" ]]; then echo "package-web: missing source $2 (for $1)" >&2; bad=1; fi
        printf '%s\t%s\n' "$1" "$2"
    }
    port() {   # port <name> <glob>: exactly one match
        local m=()
        mapfile -t m < <(compgen -G "$2" || true)
        if [[ ${#m[@]} -ne 1 ]]; then
            echo "package-web: ${#m[@]} matches, need exactly one, for $2 (for $1)" >&2; bad=1
            printf '%s\t%s\n' "$1" "$2"
        else
            ent "$1" "${m[0]}"
        fi
    }
    ent "jnext-$ver.js" "$build/jnext-$ver.js"
    ent "jnext-$ver.wasm" "$build/jnext-$ver.wasm"
    local p
    for p in "${PAGES[@]}"; do ent "$p" "$build/$p"; done
    ent README.txt "$readme"
    ent LICENSE "$ROOT/LICENSE"
    ent CREDITS.md "$ROOT/CREDITS.md"
    ent licenses/emscripten.txt "$em/LICENSE"
    ent licenses/musl.txt "$em/system/lib/libc/musl/COPYRIGHT"
    ent licenses/libcxx.txt "$lib/libcxx/LICENSE.TXT"
    ent licenses/libcxxabi.txt "$lib/libcxxabi/LICENSE.TXT"
    ent licenses/libunwind.txt "$lib/libunwind/LICENSE.TXT"
    ent licenses/compiler-rt.txt "$lib/compiler-rt/LICENSE.TXT"
    port licenses/sdl3.txt "$em/cache/ports/sdl3/SDL-release-*/LICENSE.txt"
    port licenses/zlib.txt "$em/cache/ports/zlib/zlib-*/LICENSE"
    port licenses/libpng.txt "$em/cache/ports/libpng/libpng-*/LICENSE"
    ent licenses/spdlog.txt "$ROOT/third_party/spdlog/LICENSE"
    ent licenses/nlohmann-json.txt "$ROOT/third_party/nlohmann-json/LICENSE.MIT"
    ent licenses/zot.txt "$ROOT/third_party/zot/LICENSE"
    ent licenses/fatfs.txt "$ROOT/third_party/fatfs/LICENSE.txt"
    return $bad
}

# verify <zip> <ver> <manifest-file>: a foreign reader (CPython zipfile) of Info-ZIP's
# output. Never extracts. Reports every failure.
verify() {
    python3 -I -c '
import os, re, stat, sys, zipfile
zpath, ver, mfile = sys.argv[1:4]
top = "jnext-%s-web/" % ver
MiB = 1 << 20
src = {}
for line in open(mfile, encoding="utf-8", errors="surrogateescape"):
    n, p = line.rstrip("\n").split("\t")
    src[top + n] = p
errs = []
def bad(m): errs.append(m)
if os.path.basename(zpath) != "jnext-%s-web.zip" % ver:
    bad("zip name %s is not jnext-%s-web.zip" % (os.path.basename(zpath), ver))
try:
    z = zipfile.ZipFile(zpath)
except Exception as e:
    print("package-web: cannot read %s: %s" % (zpath, e), file=sys.stderr); sys.exit(1)
infos = {i.filename: i for i in z.infolist()}
if len(infos) != len(z.infolist()):
    bad("duplicate entry names")
for n in infos:
    if re.search(r"\.(img|mmc)$", n, re.I):
        bad("entry %s has a NextZXOS image name" % n)
    if n.startswith("/") or ".." in n.split("/"):
        bad("entry %s is an absolute path or contains .." % n)
    if stat.S_ISLNK(infos[n].external_attr >> 16):
        bad("entry %s is a symlink" % n)
for n in sorted(set(src) - set(infos)):
    bad("missing entry %s" % n)
for n in sorted(set(infos) - set(src)):
    bad("unexpected entry %s" % n)
rel = lambda n: n[len(top):]
def cap(n):
    r = rel(n)
    if r.endswith(".wasm"): return (1 * MiB, 16 * MiB)
    if r.endswith(".js"): return (16 * 1024, 4 * MiB)
    if r.endswith(".html"): return (0, 256 * 1024)
    if r == "README.txt": return (0, 16 * 1024)
    return (0, 1 * MiB)
ok = {}
for n, i in infos.items():
    if n not in src: continue
    lo, hi = cap(n)
    if not lo <= i.file_size <= hi:
        bad("entry %s is %d bytes, outside [%d, %d]" % (n, i.file_size, lo, hi))
        continue
    p = src[n]
    if not os.path.isfile(p): continue
    if os.path.getsize(p) != i.file_size:
        bad("entry %s (%d bytes) differs from its source %s (%d bytes)" % (n, i.file_size, p, os.path.getsize(p)))
        continue
    data = z.read(n)
    if data != open(p, "rb").read():
        bad("entry %s differs from its source %s" % (n, p))
        continue
    ok[n] = data
w = top + "jnext-%s.wasm" % ver
if w in infos:
    with z.open(w) as f:
        if f.read(8) != b"\x00asm\x01\x00\x00\x00":
            bad("%s does not start with the WebAssembly preamble" % w)
j = top + "jnext-%s.js" % ver
if j in ok:
    for lit in ("jnext-%s.wasm" % ver, "\x27%s\x27" % ver, "createJnextModule"):
        if lit.encode() not in ok[j]:
            bad("%s does not contain %s" % (j, lit))
for pg in ("plain.html", "itch-host.html", "itch-game.html"):
    n = top + pg
    if n not in ok: continue
    t = ok[n].decode("utf-8", "replace")
    for v in sorted(set(re.findall(r"jnext-([0-9]+\.[0-9]+\.[0-9]+)", t))):
        if v != ver: bad("%s refers to jnext-%s, not %s" % (pg, v, ver))
    if pg != "itch-host.html" and ("jnext-%s.js" % ver) not in t:
        bad("%s does not load jnext-%s.js" % (pg, ver))
    if pg == "itch-host.html" and "itch-game.html" not in t:
        bad("%s does not embed itch-game.html" % pg)
r = top + "README.txt"
if r in ok:
    t = ok[r].decode("utf-8", "replace")
    if "/tree/v%s" % ver not in t: bad("README.txt has no /tree/v%s link" % ver)
    if "@" in t: bad("README.txt still holds an @")
for e in errs: print("package-web: verify: " + e, file=sys.stderr)
sys.exit(1 if errs else 0)
' "$1" "$2" "$3"
}

WORK=""
cleanup() { [[ -z "$WORK" ]] || rm -rf "$WORK"; }
trap cleanup EXIT

if [[ "${1:-}" == --verify ]]; then
    [[ $# -eq 4 ]] || die "usage: package-web.sh --verify <zip> <build-dir> <emsdk-dir>"
    ZIP=$2; BUILD=$3; EMSDK=$4
    VER=$(read_version)
    WORK=$(mktemp -d)
    sed "s/@VERSION@/$VER/g" "$ROOT/packaging/web/README.txt.in" >"$WORK/README.txt"
    manifest "$VER" "$BUILD" "$EMSDK" "$WORK/README.txt" >"$WORK/manifest" || die "a source is missing"
    verify "$ZIP" "$VER" "$WORK/manifest"
    exit
fi

[[ $# -eq 2 ]] || die "usage: package-web.sh <build-dir> <emsdk-dir> | --verify <zip> <build-dir> <emsdk-dir>"
BUILD=$(cd "$1" 2>/dev/null && pwd) || die "no build directory $1"
EMSDK=$2

VER=$(read_version)
NAME="jnext-$VER-web"
ZIP="$BUILD/$NAME.zip"
rm -f "$ZIP"   # first, so that no refusal below leaves an earlier zip behind

[[ -f "$BUILD/CMakeCache.txt" ]] || die "$BUILD/CMakeCache.txt is missing"
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$BUILD/CMakeCache.txt" \
    || die "$BUILD is not a Release tree (CMAKE_BUILD_TYPE must be Release)"

EPOCH=${SOURCE_DATE_EPOCH:-}
if [[ -z "$EPOCH" && -e "$ROOT/.git" ]]; then
    EPOCH=$(git -C "$ROOT" log -1 --format=%ct HEAD 2>/dev/null || true)
fi
[[ "$EPOCH" =~ ^[0-9]+$ ]] || die "no date source: set SOURCE_DATE_EPOCH (not a git checkout, or no commit)"

WORK=$(mktemp -d)
sed "s/@VERSION@/$VER/g" "$ROOT/packaging/web/README.txt.in" >"$WORK/README.txt"
manifest "$VER" "$BUILD" "$EMSDK" "$WORK/README.txt" >"$WORK/manifest" || die "a source is missing, no zip written"

DIST="$BUILD/dist"
rm -rf "$DIST"
mkdir -p "$DIST/$NAME/licenses"
while IFS=$'\t' read -r n p; do
    cp "$p" "$DIST/$NAME/$n"
    chmod 0644 "$DIST/$NAME/$n"
done <"$WORK/manifest"
find "$DIST" -print0 | xargs -0 touch -h -d "@$EPOCH"

( cd "$DIST" && find "$NAME" -type f | sort | zip -X -D -9 -q "../$NAME.zip" -@ )

if ! verify "$ZIP" "$VER" "$WORK/manifest"; then
    rm -f "$ZIP"
    die "the zip failed verification and was deleted"
fi
echo "$ZIP"
