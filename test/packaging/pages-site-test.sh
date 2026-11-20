#!/usr/bin/env bash
#
# Contract test for packaging/web/pages-site.sh and the Pages workflows (`make pages-site-test`):
# the GitHub Pages demo site, build/pages. Hermetic: a fake root and a fake web zip under a scratch
# directory, no emcc, no browser, no SD-card image, no game. The scratch directory is removed on exit.
#
# Oracles: the site contract of this suite's own literal lists (the site is the web zip minus the four
# example pages plus index.html; NEVER an image, a program or an archive: owner rule, CLAUDE.md);
# version.yaml as the only source of the version; GitHub Actions workflow syntax as PyYAML parses the
# real pages.yml and release.yml; the Makefile's own prerequisite lines. No VHDL: this is packaging.
#
# Rows PSITE-01 .. PSITE-07. A missing tool FAILS PSITE-01 and every other row then reports
# FAIL "not run": there is no SKIP path.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
EXPECTED_ROWS=7

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
not_run() {
    local i
    for ((i = $1; i <= EXPECTED_ROWS; i++)); do
        total=$((total + 1)); failed=$((failed + 1)); printf '  FAIL PSITE-%02d not run: PSITE-01 failed\n' "$i"
    done
    finish
}

T=$(mktemp -d "${TMPDIR:-$HOME/tmp}/pages-site-test.XXXXXX" 2>/dev/null || mktemp -d)
trap 'rm -rf "$T"' EXIT

missing=()
for t in python3 sha256sum diff find sort; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
python3 -I -c 'import yaml' 2>/dev/null || missing+=("python3-pyyaml")

# mkzip.py <out.zip> <version> [name:bytes ...]: a fake jnext-<ver>-web.zip by a foreign writer
# (zipfile). The base entries are the package's own list; each extra argument adds (or, with a
# leading "-", drops) an entry; a size is the entry's byte count (zeros).
cat >"$T/mkzip.py" <<'PYEOF'
import sys, zipfile
out, ver = sys.argv[1:3]
top = "jnext-%s-web/" % ver
ents = {
    "jnext-%s.js" % ver: b"var createJnextModule = 1; // jnext-%s.wasm\n" % (ver.encode()),
    "jnext-%s.wasm" % ver: b"\0asm\1\0\0\0" + bytes(1024),
    "plain.html": b"<html>plain</html>", "itch-host.html": b"<html>host</html>",
    "itch-game.html": b"<html>game</html>", "widget.html": b"<html>widget</html>",
    "jnext-widget.js": b"// widget\n", "README.txt": b"readme\n", "LICENSE": b"licence\n",
    "CREDITS.md": b"credits\n", "licenses/musl.txt": b"musl\n",
}
MAGICS = {"gz": b"\x1f\x8b\x08\x00" + bytes(60), "zip": b"PK\x03\x04" + bytes(60),
          "xz": b"\xfd7zXZ\x00" + bytes(60), "boot": bytes(510) + b"\x55\xaa",
          "wasm55": b"\0asm\x01\0\0\0" + bytes(502) + b"\x55\xaa", "mark": bytes(510) + b"\x55\xaa"}
# Arguments: name:size adds (zeros) or replaces; -name drops; name#magic adds content (gz zip xz boot);
# !name:size adds an entry with a RAW name (no top folder); @name adds a symlink; +name:size adds a
# second entry of an existing name.
extra = []
def entry(n, d, attr=0o100644 << 16):
    zi = zipfile.ZipInfo(n, (2025, 1, 1, 0, 0, 0))
    zi.external_attr = attr
    zi.compress_type = zipfile.ZIP_DEFLATED
    return zi, d
for a in sys.argv[3:]:
    if a.startswith("-"):
        del ents[a[1:]]
    elif a.startswith("!"):
        n, _, sz = a[1:].rpartition(":")
        extra.append(entry(n, bytes(int(sz))))
    elif a.startswith("@"):
        extra.append(entry(top + a[1:], b"target", 0o120777 << 16))
    elif a.startswith("+"):
        n, _, sz = a[1:].rpartition(":")
        extra.append(entry(top + n, bytes(int(sz))))
    elif "#" in a:
        n, _, k = a.rpartition("#")
        ents[n] = MAGICS[k]
    else:
        n, _, sz = a.rpartition(":")
        ents[n] = bytes(int(sz))
import warnings
warnings.simplefilter("ignore")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n in sorted(ents):
        zi, d = entry(top + n, ents[n])
        z.writestr(zi, d)
    for zi, d in extra:
        z.writestr(zi, d)
PYEOF

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "  missing tools: ${missing[*]} (dnf install python3 python3-pyyaml coreutils diffutils findutils)"
    row PSITE-01 0 "tools are present and the good zip gives the site"
    not_run 2
fi

VER=9.9.9
# scen <name> [zip args...]: a fresh fake root under $T/<name> (script, template, version.yaml) and a
# fake zip in its build/wasm-core; sets D
scen() {
    D=$T/$1; shift
    mkdir -p "$D/root/packaging/web" "$D/root/web/pages" "$D/root/build/wasm-core"
    cp "$ROOT/packaging/web/pages-site.sh" "$D/root/packaging/web/"
    cp "$ROOT/web/pages/index.html.in" "$D/root/web/pages/"
    echo "version: $VER" >"$D/root/version.yaml"
    python3 -I "$T/mkzip.py" "$D/root/build/wasm-core/jnext-$VER-web.zip" "$VER" "$@"
}
# run_site <dir>: sets RC; stderr in <dir>/stderr
run_site() {
    RC=0
    bash "$1/root/packaging/web/pages-site.sh" "$1/root/build/wasm-core" >"$1/stdout" 2>"$1/stderr" || RC=$?
}
nosite() { [[ ! -e "$1/root/build/pages" && ! -e "$1/root/build/pages.tmp" ]]; }
tree_list() { (cd "$1" && find . -type f | sort); }
tree_sums() { (cd "$1" && find . -type f -print0 | sort -z | xargs -0 sha256sum); }

# ---- PSITE-01: the site is index.html plus the zip's entries minus the example pages ----
scen good
run_site "$D"
GOOD=$D
want=$(printf '%s\n' ./CREDITS.md ./LICENSE ./README.txt ./index.html "./jnext-$VER.js" "./jnext-$VER.wasm" ./jnext-widget.js ./licenses/musl.txt | LC_ALL=C sort)
ok=0
if [[ $RC -eq 0 && "$(tree_list "$GOOD/root/build/pages")" == "$want" ]] \
   && grep -qF "JNEXT $VER," "$GOOD/root/build/pages/index.html" \
   && ! grep -qF '@PROJECT_VERSION@' "$GOOD/root/build/pages/index.html" \
   && [[ "$(tail -n1 "$GOOD/stdout")" == "$GOOD/root/build/pages" ]]; then ok=1; fi
row PSITE-01 "$ok" "the site is exactly index.html plus the zip minus the four example pages; index.html carries version.yaml's version"
[[ $ok == 1 ]] || { cat "$GOOD/stderr"; }

# ---- PSITE-02: two runs give byte-identical trees ----
cp -a "$GOOD/root/build/pages" "$T/first"
run_site "$GOOD"
ok=0
if [[ $RC -eq 0 ]] && diff -r "$T/first" "$GOOD/root/build/pages" >/dev/null \
   && [[ "$(tree_sums "$T/first")" == "$(tree_sums "$GOOD/root/build/pages")" ]]; then ok=1; fi
row PSITE-02 "$ok" "two runs give byte-identical trees (diff -r and the sha256 of every file)"

# ---- PSITE-03: an image, a program or an archive, at any depth or case, is refused ----
ok=1
for case in "x.img" "sub/GAME.NEX" "a.ZIP" "c.mmc" "48.rom" "sub/48.ROM" "card.img.gz" "x.nex.txt" "a.tar.xz" "a.7z" "d.dsk" "x.img." "x.img " "data.txt#gz" "data.txt#zip" "data.txt#xz" "boot.txt#boot"; do
    n=${case//\//_}; n=${n// /_}; n=${n//#/_}
    if [[ "$case" == *"#"* ]]; then scen "bad-$n" "$case"; else scen "bad-$n" "$case:16"; fi
    run_site "$D"
    nm=${case%%#*}
    if [[ $RC -ne 0 ]] && grep -qF -- "$nm" "$D/stderr" && nosite "$D"; then :; else
        ok=0; echo "    case $case: rc=$RC, absent=$(nosite "$D" && echo yes || echo no)"; cat "$D/stderr"
    fi
done
row PSITE-03 "$ok" "an extra image, ROM, program, archive or disk image (any depth, case, double extension, trailing dot or space) or a file with an archive or boot-sector content is refused, the name printed, build/pages absent"

# ---- PSITE-04: an oversized wasm and an oversized site are refused ----
ok=1
scen big-wasm "jnext-$VER.wasm:$((16 * 1024 * 1024 + 1))"
run_site "$D"
if [[ $RC -ne 0 ]] && grep -qF "jnext-$VER.wasm" "$D/stderr" && nosite "$D"; then :; else ok=0; echo "    big wasm: rc=$RC"; cat "$D/stderr"; fi
scen big-site "docs.txt:$((25 * 1024 * 1024))"
run_site "$D"
if [[ $RC -ne 0 ]] && grep -qF "over 24 MiB" "$D/stderr" && nosite "$D"; then :; else ok=0; echo "    big site: rc=$RC"; cat "$D/stderr"; fi
# the limit itself is allowed: a 16 MiB wasm
scen max-wasm "jnext-$VER.wasm:$((16 * 1024 * 1024))"
run_site "$D"
if [[ $RC -eq 0 ]]; then :; else ok=0; echo "    16 MiB wasm refused: rc=$RC"; cat "$D/stderr"; fi
row PSITE-04 "$ok" "a wasm over 16 MiB and a site over 24 MiB are each refused (16 MiB itself passes)"

# ---- PSITE-06: every other refusal: a symlink, .. and absolute paths, a missing required file, the zip's own
# index.html, a duplicate entry, an entry outside the top folder ----
ok=1
cases=("symlink|link.txt|@link.txt" "dotdot|x.txt|!jnext-$VER-web/../x.txt:16" "absolute|abs.txt|!/abs.txt:16"
       "no-wasm|jnext-$VER.wasm|-jnext-$VER.wasm" "no-widget|jnext-widget.js|-jnext-widget.js" "no-js|jnext-$VER.js|-jnext-$VER.js"
       "index|index.html|index.html:16" "duplicate|LICENSE|+LICENSE:16" "outside|other/x.txt|!other/x.txt:16")
for c in "${cases[@]}"; do
    IFS='|' read -r cn nm arg <<<"$c"
    scen "ref-$cn" "$arg" 2>/dev/null
    run_site "$D"
    if [[ $RC -ne 0 ]] && grep -qF -- "$nm" "$D/stderr" && nosite "$D" && [[ ! -e "$D/root/build/x.txt" && ! -e "$D/root/x.txt" ]]; then :; else
        ok=0; echo "    case $cn: rc=$RC, absent=$(nosite "$D" && echo yes || echo no)"; cat "$D/stderr"
    fi
done
row PSITE-06 "$ok" "a zip symlink, a .. or absolute path, a missing jnext js, wasm or widget, the zip's own index.html, a duplicate entry and an entry outside the top folder are each refused, the name printed, build/pages absent, nothing written outside it"

# ---- PSITE-07: the boot-sector mark: the wasm that carries it is accepted, any other file with the WebAssembly
# preamble and the mark is refused ----
ok=1
scen mark-wasm "jnext-$VER.wasm#wasm55"
run_site "$D"
if [[ $RC -ne 0 || ! -f "$D/root/build/pages/jnext-$VER.wasm" ]]; then ok=0; echo "    wasm with the mark refused: rc=$RC"; cat "$D/stderr"; fi
scen mark-txt "pre.txt#wasm55"
run_site "$D"
if [[ $RC -ne 0 ]] && grep -qF "pre.txt" "$D/stderr" && nosite "$D"; then :; else ok=0; echo "    pre.txt with preamble and mark accepted: rc=$RC"; cat "$D/stderr"; fi
scen mark-nopre "jnext-$VER.wasm#mark"
run_site "$D"
if [[ $RC -ne 0 ]] && grep -qF "jnext-$VER.wasm" "$D/stderr" && nosite "$D"; then :; else ok=0; echo "    jnext wasm with the mark and no preamble accepted: rc=$RC"; cat "$D/stderr"; fi
row PSITE-07 "$ok" "jnext-<ver>.wasm with 55 AA at byte 510 and its preamble is accepted; the same mark without the preamble, and a .txt with the preamble and the mark, are refused"

# ---- PSITE-05: the wiring (Makefile prerequisites, pages.yml, release.yml) ----
ok=1
chk() { if ! "$@"; then ok=0; echo "    wiring check failed: $*"; fi; }
chk grep -qE '^pages-site: package-web$' "$ROOT/Makefile"
chk grep -qE '^wasm-browser-test:.* pages-site( |$)' "$ROOT/Makefile"
chk grep -qE '^package-contract-test:.* pages-site-test( |$)' "$ROOT/Makefile"
chk grep -qxF '	bash packaging/web/pages-site.sh build/wasm-core' "$ROOT/Makefile"
wiring=$(python3 -I - "$ROOT/.github/workflows/pages.yml" "$ROOT/.github/workflows/release.yml" <<'PYEOF'
import sys, yaml
pg = yaml.safe_load(open(sys.argv[1]))
rl = yaml.safe_load(open(sys.argv[2]))
errs = []
def need(c, m):
    if not c: errs.append(m)
on = pg.get(True, pg.get("on"))   # PyYAML reads the key `on` as True
need(isinstance(on, dict) and "workflow_call" in on and "workflow_dispatch" in on, "pages.yml lacks workflow_call or workflow_dispatch")
need(pg.get("permissions") == {"contents": "read"}, "pages.yml top-level permissions are not contents: read")
steps = pg["jobs"]["build"]["steps"]
need(any(s.get("run") == "LANG=C make pages-site" for s in steps), "no run step exactly `LANG=C make pages-site`")
up = [s for s in steps if str(s.get("uses", "")).startswith("actions/upload-pages-artifact@")]
need(len(up) == 1 and up[0].get("with", {}).get("path") == "build/pages", "upload-pages-artifact with path build/pages missing")
dep = pg["jobs"]["deploy"]
need(dep.get("needs") == "build", "deploy does not need build")
need(dep.get("permissions") == {"pages": "write", "id-token": "write"}, "deploy permissions are not pages: write, id-token: write")
need(dep.get("environment", {}).get("name") == "github-pages", "deploy environment is not github-pages")
need(any(str(s.get("uses", "")).startswith("actions/deploy-pages@") for s in dep["steps"]), "no actions/deploy-pages step")
j = rl["jobs"].get("pages")
need(j is not None, "release.yml has no pages job")
if j:
    n = j.get("needs")
    need(isinstance(n, list) and "publish" in n, "the pages job does not need publish")
    need("needs.gate.outputs.publish == 'true'" in str(j.get("if", "")), "the pages job is not gated on publish == 'true'")
    need(j.get("uses") == "./.github/workflows/pages.yml", "the pages job does not use ./.github/workflows/pages.yml")
need(j is None or j.get("permissions") == {"contents": "read", "pages": "write", "id-token": "write"}, "the release.yml pages job permissions are not contents: read, pages: write, id-token: write")
need(pg.get("concurrency") == {"group": "pages", "cancel-in-progress": False}, "pages.yml concurrency is not {group: pages, cancel-in-progress: false}")
cond = str(dep.get("if", ""))
need("github.ref == 'refs/heads/main'" in cond and "startsWith(github.ref, 'refs/tags/v')" in cond and "||" in cond, "the deploy job's if lacks the main or the v* arm")
for e in errs: print(e)
PYEOF
) || { ok=0; echo "    python failed"; }
if [[ -n "$wiring" ]]; then ok=0; echo "$wiring" | sed 's/^/    /'; fi
row PSITE-05 "$ok" "wiring: pages-site after package-web, wasm-browser-test after pages-site; pages.yml triggers, make step, upload path, deploy permissions and environment; release.yml pages job needs publish and uses pages.yml"

finish
