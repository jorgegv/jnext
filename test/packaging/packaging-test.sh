#!/usr/bin/env bash
#
# Integration tests for the `make package-*` targets. Each package type must
# produce a correctly-named, non-empty artifact that contains the jnext binary.
#
# Two entry points, because the suite has two very different halves (GH #61):
#
#   bash packaging-test.sh --contracts-only   (`make package-contract-test`)
#       The six packaging-script contract suites. Hermetic — pure bash on
#       throwaway fake roots, no compiler, no packaging toolchain, ~4 seconds.
#       This half holds the highest-value rows (verify-bundle is the GH #46
#       gate; sync-version is GH #60), so it is a prerequisite of `make
#       unit-test` and runs on every local test run, exactly like docs-check.
#
#   bash packaging-test.sh                    (`make package-test`)
#       The above PLUS a real build of every package (src/rpm/deb/win/flatpak)
#       with content assertions. ~4 minutes and toolchain-dependent, so it runs
#       as its own parallel job in ci.yml rather than inside `make unit-test`.
#
# Tooling-guarded: a package whose build tool is absent SKIPs (not FAIL) on a
# dev box — not every host has every toolchain. In CI a missing tool is a FAIL
# instead (see skp_ci_fail below): the workflow installs it deliberately, so a
# silent skip there would read as a pass and hide the very regression this
# suite exists to catch. macOS (.dmg) is deliberately excluded everywhere: it
# needs a Mac / the CI macos runner (see packaging/README.md).
#
# Exits non-zero if any tested package FAILs.
#
# NOTE: no `pipefail` — the assertions below use `cmd | grep -q`, and grep -q
# exits on the first match, which SIGPIPEs the producer (tar/dpkg-deb/unzip);
# under pipefail that spurious failure would fail the whole assertion.
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

mode=all
case "${1:-}" in
    --contracts-only) mode=contracts ;;
    "")               ;;
    *) echo "usage: $0 [--contracts-only]" >&2; exit 2 ;;
esac

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; BOLD='\033[1m'; RESET='\033[0m'
LOGDIR=$(mktemp -d)
trap 'rm -rf "$LOGDIR"' EXIT

pass=0; fail=0; skip=0
ok()  { printf "  ${GREEN}PASS${RESET} %-16s %s\n" "$1" "$2"; pass=$((pass+1)); }
# bad <name> <message> [logfile] — a failure PRINTS its evidence.
#
# Every row here used to end "(see $LOGDIR/x.log)", but $LOGDIR is a mktemp
# directory removed by the EXIT trap above — so on a CI runner the log is gone
# before anyone can read it, and the pointer is worse than useless because it
# reads like the evidence exists. That is how a CI-only failure (PyYAML absent
# in the fedora:44 container) stood red across two pushes: the summary said
# add-release failed and there was no way to learn why without reproducing the
# container by hand. Print the tail inline instead.
bad() {
    printf "  ${RED}FAIL${RESET} %-16s %s\n" "$1" "$2"; fail=$((fail+1))
    if [ -n "${3:-}" ] && [ -s "$3" ]; then
        printf "    ---- %s (last 40 lines) ----\n" "$(basename "$3")"
        # `awk 1` normalises a MISSING final newline (a tool whose last line has
        # none would otherwise glue the footer onto it) without adding a blank
        # line to the common newline-terminated case.
        tail -40 "$3" | sed -e 's/^/    | /' | awk 1
        printf "    ---- end ----\n"
    fi
}
skp() { printf "  ${YELLOW}SKIP${RESET} %-16s %s\n" "$1" "$2"; skip=$((skip+1)); }

# Missing tooling for a row CI is expected to cover: SKIP on a dev box, FAIL in
# CI. ci.yml's `package` job installs rpmbuild / dpkg-deb / the MinGW Qt6 cross
# toolchain on purpose; if one silently vanishes (renamed dnf package, dropped
# install line) the row would skip and the job would still go green — a suite
# that quietly stops testing something is worse than no suite. Same discipline
# as docs-man-check's pandoc guard in the Makefile. Rows CI deliberately does
# NOT provision (flatpak: needs org.kde.Sdk, a multi-GB privileged install)
# keep using plain skp().
skp_ci_fail() {
    if [ -n "${CI:-}" ]; then
        bad "$1" "$2 — MISSING IN CI. This row would otherwise skip silently and read as a pass; install the tool in the workflow, or drop the row deliberately."
    else
        skp "$1" "$2"
    fi
}

summary() {
    printf "\n${BOLD}=== Results ===${RESET}\n"
    printf "  ${GREEN}Pass: %d${RESET}  ${RED}Fail: %d${RESET}  ${YELLOW}Skip: %d${RESET}\n" "$pass" "$fail" "$skip"
}

MINGW_QT6=/usr/x86_64-w64-mingw32/sys-root/mingw/lib/cmake/Qt6/Qt6Config.cmake
MINGW_QT5=/usr/x86_64-w64-mingw32/sys-root/mingw/lib/cmake/Qt5/Qt5Config.cmake

if [ "$mode" = contracts ]; then
    printf "${BOLD}=== jnext packaging contract tests (scripts only, no builds) ===${RESET}\n\n"
else
    printf "${BOLD}=== jnext packaging integration tests ===${RESET}\n\n"
fi

# --- sync-version.sh contract (fast; runs on a throwaway copy) ----------------
# The version-consistency script the bump-* targets rely on. Kept first so a
# broken sync surfaces before the slow package builds.
if bash test/packaging/sync-version-test.sh >"$LOGDIR/syncver.log" 2>&1; then
    ok sync-version "idempotent, consistent, fails loud (see log for detail)"
else
    bad sync-version "contract test failed" "$LOGDIR/syncver.log"
fi

# --- add-release.sh contract (releases.yaml allowlist helper) ----------------
if bash test/packaging/add-release-test.sh >"$LOGDIR/addrel.log" 2>&1; then
    ok add-release "starts/append/idempotent/fail-loud (see log for detail)"
else
    bad add-release "contract test failed" "$LOGDIR/addrel.log"
fi

# --- verify-bundle.sh contract (macOS self-containment gate, GH #46) ---------
# Runs everywhere: the gate's decision logic is tested against stubbed
# otool/file, so the Linux dev host covers it even though the .dmg itself
# cannot be built here.
if bash test/packaging/verify-bundle-test.sh >"$LOGDIR/verifybundle.log" 2>&1; then
    ok verify-bundle "rejects Homebrew/absolute deps, nested files, empty bundles"
else
    bad verify-bundle "contract test failed" "$LOGDIR/verifybundle.log"
fi

# --- prune-broken-plugins.sh contract (deletes files — both directions pinned)
if bash test/packaging/prune-plugins-test.sh >"$LOGDIR/pruneplugins.log" 2>&1; then
    ok prune-plugins "removes unloadable plugins, keeps working ones, refuses on libqcocoa"
else
    bad prune-plugins "contract test failed" "$LOGDIR/pruneplugins.log"
fi

# --- complete-closure.sh contract (adds files to the shipped bundle) ---------
if bash test/packaging/complete-closure-test.sh >"$LOGDIR/closure.log" 2>&1; then
    ok complete-closure "copies missing libs transitively, no-ops when complete"
else
    bad complete-closure "contract test failed" "$LOGDIR/closure.log"
fi

# --- bundle-dlopen-deps.sh contract (adds the library no walk can see) -------
if bash test/packaging/bundle-dlopen-deps-test.sh >"$LOGDIR/dlopendeps.log" 2>&1; then
    ok bundle-dlopen-deps "copies libSDL3 for sdl2-compat, no-ops otherwise, fails loud when absent"
else
    bad bundle-dlopen-deps "contract test failed" "$LOGDIR/dlopendeps.log"
fi

# ---- end of the hermetic contract half --------------------------------------
# Everything above needs nothing but bash; everything below builds real
# packages. `make package-contract-test` (a prerequisite of `make unit-test`)
# stops here.
if [ "$mode" = contracts ]; then
    summary
    [ "$fail" -eq 0 ]
    exit
fi

# --- package-src (source tarball + release zip) ------------------------------
if make package-src >"$LOGDIR/src.log" 2>&1; then
    tb=$(ls -1 build/dist/*.tar.gz 2>/dev/null | head -1)
    if [ -n "$tb" ] && [ -s "$tb" ] && tar tzf "$tb" 2>/dev/null | grep -q "/CMakeLists.txt$"; then
        ok package-src "$(basename "$tb")"
    else
        bad package-src "no tarball, empty, or missing CMakeLists.txt" "$LOGDIR/src.log"
    fi
    # The release source zip: jnext-<ver>-src.zip must exist, be a valid zip,
    # contain the top-level CMakeLists.txt AND the vendored submodule content
    # (third_party/spdlog/CMakeLists.txt) a naive `git archive` would drop.
    z=$(ls -1 build/dist/jnext-*-src.zip 2>/dev/null | head -1)
    if [ -n "$z" ] && [ -s "$z" ]; then
        zl=$(unzip -l "$z" 2>/dev/null)
        if printf '%s' "$zl" | grep -q "/CMakeLists.txt$" \
           && printf '%s' "$zl" | grep -q "third_party/spdlog/CMakeLists.txt$"; then
            ok package-src-zip "$(basename "$z")"
        else
            bad package-src-zip "zip missing CMakeLists.txt or vendored submodule content" "$LOGDIR/src.log"
        fi
    else
        bad package-src-zip "no jnext-<ver>-src.zip produced" "$LOGDIR/src.log"
    fi
else
    bad package-src "make package-src failed" "$LOGDIR/src.log"
fi

# --- package-rpm -------------------------------------------------------------
if command -v rpmbuild >/dev/null 2>&1; then
    if make package-rpm >"$LOGDIR/rpm.log" 2>&1; then
        r=$(ls -1 build/rpm-release/jnext-*.x86_64.rpm 2>/dev/null | head -1)
        if [ -n "$r" ] && rpm -qlp "$r" 2>/dev/null | grep -q "bin/jnext$"; then
            ok package-rpm "$(basename "$r")"
        else
            bad package-rpm "no .rpm with conventional name, or missing bin/jnext"
        fi
    else
        bad package-rpm "make package-rpm failed" "$LOGDIR/rpm.log"
    fi
else
    skp_ci_fail package-rpm "rpmbuild not installed"
fi

# --- package-deb -------------------------------------------------------------
if command -v dpkg-deb >/dev/null 2>&1; then
    if make package-deb >"$LOGDIR/deb.log" 2>&1; then
        d=$(ls -1 build/deb-release/jnext_*_amd64.deb 2>/dev/null | head -1)
        if [ -n "$d" ] && dpkg-deb -c "$d" 2>/dev/null | grep -q "bin/jnext$"; then
            ok package-deb "$(basename "$d")"
        else
            bad package-deb "no .deb with conventional name, or missing bin/jnext"
        fi
    else
        bad package-deb "make package-deb failed" "$LOGDIR/deb.log"
    fi
else
    skp_ci_fail package-deb "dpkg-deb not installed"
fi

# --- package-win (MinGW cross-build ZIP) -------------------------------------
if command -v mingw64-cmake >/dev/null 2>&1 && command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && [ -f "$MINGW_QT6" ]; then
    if make package-win >"$LOGDIR/win.log" 2>&1; then
        z=$(ls -1 build/win-release/*.zip 2>/dev/null | head -1)
        # The ZIP must contain the exe AND its bundled runtime — the Qt6 core DLL,
        # the platforms/qwindows.dll plugin (no GUI without it), and SDL3.dll (the
        # sdl2-compat SDL2.dll runtime-loads it; missing it → "Failed loading SDL3").
        if [ -n "$z" ]; then
            list=$(unzip -l "$z" 2>/dev/null)
            # GH #108 Phase B: the Windows provisioner is WinHTTP+BCrypt, so
            # NO curl/OpenSSL DLL (nor any of their transitive-only deps) may
            # appear in a Windows bundle. A reappearing libcrypto would drag
            # PathCchRemoveFileSpec (Win8+) back in and silently re-raise the
            # SDL bundle's OS floor.
            if printf '%s' "$list" | grep -qiE "libcurl|libcrypto|libssl|libssh|libidn2|libpsl|libunistring"; then
                bad package-win "curl/OpenSSL chain DLLs leaked back into the zip (GH #108 Phase B regression)" "$LOGDIR/win.log"
            elif printf '%s' "$list" | grep -q "jnext.exe" \
               && printf '%s' "$list" | grep -q "Qt6Core.dll" \
               && printf '%s' "$list" | grep -q "platforms/qwindows.dll" \
               && printf '%s' "$list" | grep -qi "SDL3.dll"; then
                ok package-win "$(basename "$z") (jnext.exe + Qt6/SDL2/SDL3 DLLs + qwindows plugin, no curl/OpenSSL)"
            else
                bad package-win ".zip missing bundled DLLs, qwindows plugin, or SDL3.dll" "$LOGDIR/win.log"
            fi
        else
            bad package-win "no .zip produced" "$LOGDIR/win.log"
        fi
    else
        bad package-win "make package-win failed" "$LOGDIR/win.log"
    fi
else
    skp_ci_fail package-win "MinGW Qt6 cross toolchain not installed"
fi

# --- package-win subsystem (GUI, not console) --------------------------------
# jnext.exe must be linked as a GUI-subsystem binary (PE Subsystem field = 2,
# IMAGE_SUBSYSTEM_WINDOWS_GUI) so a double-click launch opens no console window.
# A revert of -Wl,--subsystem,windows would silently drop back to console (3,
# which objdump prints as "Windows CUI"); this row makes that regression a FAIL.
# Discriminative: it requires "Windows GUI" and explicitly rejects the console
# "CUI" string.
WIN_EXE=build/win-release/jnext.exe
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1 && [ -f "$WIN_EXE" ]; then
    subsys=$(x86_64-w64-mingw32-objdump -p "$WIN_EXE" 2>/dev/null | grep -i "^Subsystem")
    if printf '%s' "$subsys" | grep -qi "Windows GUI" \
       && ! printf '%s' "$subsys" | grep -qi "CUI"; then
        ok package-win-subsys "GUI subsystem ($(printf '%s' "$subsys" | tr -s ' '))"
    else
        bad package-win-subsys "not GUI subsystem — got: ${subsys:-<none>}"
    fi
else
    skp_ci_fail package-win-subsys "objdump or jnext.exe absent (package-win not built here)"
fi

# --- package-win UTF-8 manifest (GH #62) -------------------------------------
# jnext.exe must embed the activeCodePage=UTF-8 application manifest
# (packaging/assets/jnext.manifest, pulled in by jnext.rc as resource 1 of type
# RT_MANIFEST). Without it Windows' narrow CRT decodes jnext's UTF-8
# std::string paths as the ANSI code page, so any non-ASCII path (accented
# home directory) fails to open — that IS GH #62. The manifest XML is embedded
# verbatim (ASCII bytes) in the .rsrc section, so a binary grep for its
# distinctive element name is a zero-dependency presence check. A deleted
# manifest file or a dropped rc line must FAIL here, not resurface as #62.
if [ -f "$WIN_EXE" ]; then
    n=$(grep -a -c "activeCodePage" "$WIN_EXE" 2>/dev/null || true)
    if [ "${n:-0}" -ge 1 ]; then
        ok package-win-manifest "embeds activeCodePage=UTF-8 manifest ($n match)"
    else
        bad package-win-manifest "jnext.exe does NOT embed the activeCodePage manifest (GH #62 would regress)"
    fi
else
    skp_ci_fail package-win-manifest "jnext.exe absent (package-win not built here)"
fi

# --- package-win-sdl (SDL-only Windows 8+ variant, GH #108) ------------------
# The SDL-only zip must contain the exe and the SDL2+SDL3 pair, and must NOT
# contain any Qt DLL or plugin: a leaked Qt6 DLL would silently re-raise the
# bundle's OS floor to Windows 10 (fedora's Qt6Gui hard-imports d3d12.dll —
# see doc/design/WINDOWS-COMPAT-PLAN.md). No Qt toolchain needed here.
if command -v mingw64-cmake >/dev/null 2>&1 && command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    if make package-win-sdl >"$LOGDIR/win-sdl.log" 2>&1; then
        z=$(ls -1 build/win-sdl-release/*.zip 2>/dev/null | head -1)
        if [ -n "$z" ]; then
            list=$(unzip -l "$z" 2>/dev/null)
            # Same GH #108 Phase B assertion as package-win, but here it IS the
            # Win7 floor: libcrypto's PathCchRemoveFileSpec import was the one
            # post-Win7 import in this bundle (WINDOWS-COMPAT-PLAN.md §2).
            if printf '%s' "$list" | grep -qiE "libcurl|libcrypto|libssl|libssh|libidn2|libpsl|libunistring|iconv"; then
                bad package-win-sdl "curl/OpenSSL chain DLLs leaked back into the zip — Win7 floor regression (GH #108 Phase B)" "$LOGDIR/win-sdl.log"
            elif printf '%s' "$list" | grep -q "jnext.exe" \
               && printf '%s' "$list" | grep -qi "SDL2.dll" \
               && printf '%s' "$list" | grep -qi "SDL3.dll" \
               && ! printf '%s' "$list" | grep -q "Qt6" \
               && ! printf '%s' "$list" | grep -q "platforms/"; then
                ok package-win-sdl "$(basename "$z") (jnext.exe + SDL2/SDL3, no Qt, no curl/OpenSSL)"
            else
                bad package-win-sdl ".zip missing exe/SDL DLLs, or Qt files leaked in" "$LOGDIR/win-sdl.log"
            fi
        else
            bad package-win-sdl "no .zip produced" "$LOGDIR/win-sdl.log"
        fi
    else
        bad package-win-sdl "make package-win-sdl failed" "$LOGDIR/win-sdl.log"
    fi
else
    skp_ci_fail package-win-sdl "MinGW cross toolchain not installed"
fi

# --- package-win-qt5 (Qt5 full-GUI Win7/8 legacy leg, GH #108 Phase A) -------
# The Qt5 zip must contain the exe, the Qt5 core DLL and the qwindows platform
# plugin, and must NOT contain any Qt6 DLL (a mixed bundle would re-raise the
# OS floor to Windows 10 via Qt6Gui's d3d12 import) nor the curl/OpenSSL chain
# (Phase B: Windows provisioning is WinHTTP+BCrypt; libcrypto's PathCch import
# would break the audited Win7 floor). Note: iconv.dll is legitimate here —
# fedora's Qt5Core imports it (audited Win7-clean); it is only forbidden in the
# SDL bundle's list. The exe must also be a GUI-subsystem binary, same
# assertion as package-win-subsys.
if command -v mingw64-cmake >/dev/null 2>&1 && command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && [ -f "$MINGW_QT5" ]; then
    if make package-win-qt5 >"$LOGDIR/win-qt5.log" 2>&1; then
        z=$(ls -1 build/win-qt5-release/*.zip 2>/dev/null | head -1)
        if [ -n "$z" ]; then
            list=$(unzip -l "$z" 2>/dev/null)
            subsys=$(x86_64-w64-mingw32-objdump -p build/win-qt5-release/jnext.exe 2>/dev/null | grep -i "^Subsystem")
            if printf '%s' "$list" | grep -qiE "libcurl|libcrypto|libssl|libssh|libidn2|libpsl|libunistring"; then
                bad package-win-qt5 "curl/OpenSSL chain DLLs in the zip — Win7 floor regression (GH #108)" "$LOGDIR/win-qt5.log"
            elif printf '%s' "$list" | grep -q "Qt6"; then
                bad package-win-qt5 "Qt6 DLLs leaked into the Qt5 bundle — Win10 floor regression (GH #108)" "$LOGDIR/win-qt5.log"
            elif ! printf '%s' "$subsys" | grep -qi "Windows GUI" \
               || printf '%s' "$subsys" | grep -qi "CUI"; then
                bad package-win-qt5 "jnext.exe not GUI subsystem — got: ${subsys:-<none>}" "$LOGDIR/win-qt5.log"
            elif printf '%s' "$list" | grep -q "jnext.exe" \
               && printf '%s' "$list" | grep -q "Qt5Core.dll" \
               && printf '%s' "$list" | grep -q "platforms/qwindows.dll" \
               && printf '%s' "$list" | grep -qi "SDL3.dll"; then
                ok package-win-qt5 "$(basename "$z") (jnext.exe GUI-subsys + Qt5/SDL2/SDL3 DLLs + qwindows, no Qt6, no curl/OpenSSL)"
            else
                bad package-win-qt5 ".zip missing exe, Qt5 DLLs, qwindows plugin, or SDL3.dll" "$LOGDIR/win-qt5.log"
            fi
        else
            bad package-win-qt5 "no .zip produced" "$LOGDIR/win-qt5.log"
        fi
    else
        bad package-win-qt5 "make package-win-qt5 failed" "$LOGDIR/win-qt5.log"
    fi
else
    skp_ci_fail package-win-qt5 "MinGW Qt5 cross toolchain not installed"
fi

# --- package-flatpak ---------------------------------------------------------
# A full flatpak-builder run needs org.kde.Sdk installed (a large runtime) and
# network access, so it is only attempted when the SDK is present. Always at
# least (a) validate the manifest parses and (b) guard against the placeholder
# sha256 the manifest once shipped with (the concrete bug that broke the build).
if command -v flatpak-builder >/dev/null 2>&1; then
    manifest=$(ls -1 packaging/flatpak/*.yml 2>/dev/null | head -1)
    if [ -z "$manifest" ]; then
        bad package-flatpak "no manifest under packaging/flatpak/"
    elif grep -q "REPLACE_WITH_REAL" "$manifest"; then
        bad package-flatpak "manifest still contains a placeholder sha256"
    elif ! flatpak-builder --show-manifest "$manifest" >/dev/null 2>&1; then
        bad package-flatpak "manifest failed to validate (flatpak-builder --show-manifest)"
    elif flatpak list 2>/dev/null | grep -q "org.kde.Sdk"; then
        if make package-flatpak >"$LOGDIR/flatpak.log" 2>&1; then
            b=$(ls -1 build/jnext-*-x86_64.flatpak 2>/dev/null | head -1)
            if [ -n "$b" ] && [ -s "$b" ]; then
                ok package-flatpak "$(basename "$b")"
            else
                bad package-flatpak "no .flatpak bundle produced" "$LOGDIR/flatpak.log"
            fi
        else
            bad package-flatpak "make package-flatpak failed" "$LOGDIR/flatpak.log"
        fi
    else
        skp package-flatpak "manifest valid; org.kde.Sdk not installed — full build skipped"
    fi
else
    skp package-flatpak "flatpak-builder not installed"
fi

summary
[ "$fail" -eq 0 ]
