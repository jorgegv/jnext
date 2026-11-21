#!/usr/bin/env bash
#
# pages-site.sh - build the GitHub Pages demo site into build/pages (`make pages-site`).
#
#   pages-site.sh <build-dir>
#       <build-dir> (build/wasm-core) holds jnext-<ver>-web.zip, made by `make package-web`.
#       The site is that zip's content (read by CPython's zipfile, a foreign reader of
#       Info-ZIP's output) minus the four example pages, plus index.html made from
#       web/pages/index.html.in with the version of version.yaml. <ROOT>/build/pages is
#       written whole or not at all: on a refusal it is removed and the exit is non-zero.
#
# NEVER A NextZXOS IMAGE, A DISTRIBUTION ZIP OR A PROGRAM (copyrighted; owner rule, CLAUDE.md).
# The page's visitors download the system image from SpecNext at run time; the site hosts none
# of it, and no game. The guard, on the zip's entries and again on the tree written:
#   - the file list must be exactly index.html plus the zip's entries minus the example pages
#     (an extra file is refused), and must hold jnext-<ver>.js, .wasm and jnext-widget.js;
#   - a name with any of these as a dot-separated part (so x.img.gz and x.nex.txt too) is refused at
#     any depth, in any case: images and ROMs (img mmc rom bin dsk trd scl hdf vhd mdr p), programs
#     and snapshots (nex tap tzx sna szx z80 jns rzx wav raw) and archives or compressed files (zip
#     gz tgz bz2 xz 7z rar tar); so is a name ending in a dot or whitespace;
#   - a file whose content starts with a zip, gzip, bzip2, xz, 7z or rar signature, or whose bytes
#     510 and 511 are 55 AA (a boot sector; only jnext-<ver>.wasm, with its preamble, is exempt), is refused whatever its
#     name;
#   - the wasm at most 16 MiB (an image embedded raw into the module is 40 MiB), the whole
#     site at most 24 MiB;
#   - no entry is an absolute path, contains .., or is a symlink.
# SCOPE: like package-web.sh, it does not try to catch an image deliberately compressed and
# embedded into a file with an allowed name.
#
# DETERMINISTIC: the same zip and template give a byte-identical tree.
#
# Standalone script, run with bash.
set -euo pipefail
export LC_ALL=C TZ=UTC

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

die() { echo "pages-site: $*" >&2; exit 1; }

# The version, from version.yaml only (same pattern as package-web.sh).
read_version() {
    local f="$ROOT/version.yaml" line
    [[ -f "$f" ]] || die "$f is missing"
    line=$(grep -E '^version: *[0-9]+\.[0-9]+\.[0-9]+ *$' "$f" | head -n1 || true)
    [[ -n "$line" ]] || die "no valid 'version: X.Y.Z' line in $f"
    line=${line#version:}
    echo "${line//[[:space:]]/}"
}

[[ $# -eq 1 ]] || die "usage: pages-site.sh <build-dir>"
BUILD=$(cd "$1" 2>/dev/null && pwd) || die "no build directory $1"
VER=$(read_version)
ZIP="$BUILD/jnext-$VER-web.zip"
TEMPLATE="$ROOT/web/pages/index.html.in"
OUT="$ROOT/build/pages"
[[ -f "$ZIP" ]] || die "$ZIP is missing (make package-web)"
[[ -f "$TEMPLATE" ]] || die "$TEMPLATE is missing"

STAGE="$ROOT/build/pages.tmp"
rm -rf "$OUT" "$STAGE"
mkdir -p "$ROOT/build"

if ! python3 -I -c '
import os, re, shutil, stat, sys, zipfile
zpath, ver, template, stage = sys.argv[1:5]
top = "jnext-%s-web/" % ver
MiB = 1 << 20
PAGES = {"plain.html", "itch-host.html", "itch-game.html", "widget.html"}
BANNED = {"img", "mmc", "rom", "bin", "dsk", "trd", "scl", "hdf", "vhd", "mdr", "p",
          "nex", "tap", "tzx", "sna", "szx", "z80", "jns", "rzx", "wav", "raw",
          "zip", "gz", "tgz", "bz2", "xz", "7z", "rar", "tar"}
MAGIC = (b"PK\x03\x04", b"PK\x05\x06", b"\x1f\x8b", b"BZh", b"\xfd7zXZ\x00", b"7z\xbc\xaf\x27\x1c", b"Rar!")
def banned_name(r):
    base = r.rsplit("/", 1)[-1]
    if base != base.rstrip(". \t\r\n") or not base: return True
    return any(part.lower() in BANNED for part in base.split(".")[1:])
def banned_content(r, d):
    if d.startswith(MAGIC): return True
    # the one module, jnext-<ver>.wasm with the WebAssembly preamble, is not a boot sector, whatever bytes
    # 510 and 511 are; under any other name the preamble does not excuse them
    if r == "jnext-%s.wasm" % ver and d.startswith(b"\0asm\x01\0\0\0"): return False
    return len(d) >= 512 and d[510:512] == b"\x55\xaa"
errs = []
def bad(m): errs.append(m)
try:
    z = zipfile.ZipFile(zpath)
except Exception as e:
    print("pages-site: cannot read %s: %s" % (zpath, e), file=sys.stderr); sys.exit(1)
files = {}
for i in z.infolist():
    n = i.filename
    if n.endswith("/"): continue
    if not n.startswith(top): bad("entry %s is outside %s" % (n, top)); continue
    r = n[len(top):]
    if n.startswith("/") or ".." in n.split("/"): bad("entry %s is an absolute path or contains .." % n); continue
    if stat.S_ISLNK(i.external_attr >> 16): bad("entry %s is a symlink" % n); continue
    if r in files: bad("duplicate entry %s" % n); continue
    files[r] = i
for r in sorted(files):
    if banned_name(r): bad("%s has a program, archive, ROM or image name" % r)
need = ["jnext-%s.js" % ver, "jnext-%s.wasm" % ver, "jnext-widget.js"]
for r in need:
    if r not in files: bad("the zip has no %s" % r)
keep = sorted(r for r in files if r not in PAGES)
if "index.html" in files: bad("the zip has an index.html of its own")
for r in keep:
    if r.endswith(".wasm") and files[r].file_size > 16 * MiB:
        bad("%s is %d bytes, over 16 MiB" % (r, files[r].file_size))
total = sum(files[r].file_size for r in keep)
page = open(template, encoding="utf-8").read().replace("@PROJECT_VERSION@", ver)
if "@PROJECT_VERSION@" in page: bad("index.html still holds @PROJECT_VERSION@")
total += len(page.encode("utf-8"))
if total > 24 * MiB: bad("the site is %d bytes, over 24 MiB" % total)
if not errs:
    for r in keep:
        d = os.path.join(stage, r)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        data = z.read(files[r])
        if banned_content(r, data): bad("%s holds an archive, image or boot sector, whatever its name" % r); continue
        with open(d, "wb") as f: f.write(data)
        os.chmod(d, 0o644)
    with open(os.path.join(stage, "index.html"), "w", encoding="utf-8", newline="") as f: f.write(page)
    os.chmod(os.path.join(stage, "index.html"), 0o644)
    # The tree as written, checked again: exactly index.html + the kept entries, no banned name.
    got = set()
    for dp, dn, fn in os.walk(stage):
        for x in fn:
            p = os.path.join(dp, x)
            got.add(os.path.relpath(p, stage).replace(os.sep, "/"))
            if os.path.islink(p): bad("%s is a symlink" % p)
    if got != set(keep) | {"index.html"}:
        bad("the site holds %s, expected exactly index.html and the package files" % sorted(got ^ (set(keep) | {"index.html"})))
    for g in sorted(got):
        if banned_name(g): bad("the site holds %s, a program, archive, ROM or image name" % g)
for e in errs: print("pages-site: refused: " + e, file=sys.stderr)
sys.exit(1 if errs else 0)
' "$ZIP" "$VER" "$TEMPLATE" "$STAGE"; then
    rm -rf "$STAGE" "$OUT"
    die "no site written"
fi
mv "$STAGE" "$OUT"
echo "$OUT"
