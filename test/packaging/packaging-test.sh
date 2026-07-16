#!/usr/bin/env bash
#
# Integration tests for the `make package-*` targets. Each package type must
# produce a correctly-named, non-empty artifact that contains the jnext binary.
#
# Tooling-guarded: a package whose build tool is absent on this host SKIPs (not
# FAIL) — the packages are build-tool-dependent and not every dev box has every
# toolchain. macOS (.dmg) is deliberately excluded: it needs a Mac / the CI
# macos runner (see packaging/README.md).
#
# Invoked by `make package-test`. Exits non-zero if any tested package FAILs.
#
# NOTE: no `pipefail` — the assertions below use `cmd | grep -q`, and grep -q
# exits on the first match, which SIGPIPEs the producer (tar/dpkg-deb/unzip);
# under pipefail that spurious failure would fail the whole assertion.
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; BOLD='\033[1m'; RESET='\033[0m'
LOGDIR=$(mktemp -d)
trap 'rm -rf "$LOGDIR"' EXIT

pass=0; fail=0; skip=0
ok()  { printf "  ${GREEN}PASS${RESET} %-16s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-16s %s\n" "$1" "$2"; fail=$((fail+1)); }
skp() { printf "  ${YELLOW}SKIP${RESET} %-16s %s\n" "$1" "$2"; skip=$((skip+1)); }

MINGW_QT6=/usr/x86_64-w64-mingw32/sys-root/mingw/lib/cmake/Qt6/Qt6Config.cmake

printf "${BOLD}=== jnext packaging integration tests ===${RESET}\n\n"

# --- sync-version.sh contract (fast; runs on a throwaway copy) ----------------
# The version-consistency script the bump-* targets rely on. Kept first so a
# broken sync surfaces before the slow package builds.
if bash test/packaging/sync-version-test.sh >"$LOGDIR/syncver.log" 2>&1; then
    ok sync-version "idempotent, consistent, fails loud (see log for detail)"
else
    bad sync-version "contract test failed (see $LOGDIR/syncver.log)"
fi

# --- add-release.sh contract (releases.yaml allowlist helper) ----------------
if bash test/packaging/add-release-test.sh >"$LOGDIR/addrel.log" 2>&1; then
    ok add-release "starts/append/idempotent/fail-loud (see log for detail)"
else
    bad add-release "contract test failed (see $LOGDIR/addrel.log)"
fi

# --- package-src (source tarball) --------------------------------------------
if make package-src >"$LOGDIR/src.log" 2>&1; then
    tb=$(ls -1 build/dist/*.tar.gz 2>/dev/null | head -1)
    if [ -n "$tb" ] && [ -s "$tb" ] && tar tzf "$tb" 2>/dev/null | grep -q "/CMakeLists.txt$"; then
        ok package-src "$(basename "$tb")"
    else
        bad package-src "no tarball, empty, or missing CMakeLists.txt (see $LOGDIR/src.log)"
    fi
else
    bad package-src "make package-src failed (see $LOGDIR/src.log)"
fi

# --- package-rpm -------------------------------------------------------------
if command -v rpmbuild >/dev/null 2>&1; then
    if make package-rpm >"$LOGDIR/rpm.log" 2>&1; then
        r=$(ls -1 build/package-rpm/jnext-*.x86_64.rpm 2>/dev/null | head -1)
        if [ -n "$r" ] && rpm -qlp "$r" 2>/dev/null | grep -q "bin/jnext$"; then
            ok package-rpm "$(basename "$r")"
        else
            bad package-rpm "no .rpm with conventional name, or missing bin/jnext"
        fi
    else
        bad package-rpm "make package-rpm failed (see $LOGDIR/rpm.log)"
    fi
else
    skp package-rpm "rpmbuild not installed"
fi

# --- package-deb -------------------------------------------------------------
if command -v dpkg-deb >/dev/null 2>&1; then
    if make package-deb >"$LOGDIR/deb.log" 2>&1; then
        d=$(ls -1 build/package-deb/jnext_*_amd64.deb 2>/dev/null | head -1)
        if [ -n "$d" ] && dpkg-deb -c "$d" 2>/dev/null | grep -q "bin/jnext$"; then
            ok package-deb "$(basename "$d")"
        else
            bad package-deb "no .deb with conventional name, or missing bin/jnext"
        fi
    else
        bad package-deb "make package-deb failed (see $LOGDIR/deb.log)"
    fi
else
    skp package-deb "dpkg-deb not installed"
fi

# --- package-win (MinGW cross-build ZIP) -------------------------------------
if command -v mingw64-cmake >/dev/null 2>&1 && command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && [ -f "$MINGW_QT6" ]; then
    if make package-win >"$LOGDIR/win.log" 2>&1; then
        z=$(ls -1 build/gui-release-win/*.zip 2>/dev/null | head -1)
        # The ZIP must contain the exe AND its bundled runtime — the Qt6 core DLL,
        # the platforms/qwindows.dll plugin (no GUI without it), and SDL3.dll (the
        # sdl2-compat SDL2.dll runtime-loads it; missing it → "Failed loading SDL3").
        if [ -n "$z" ]; then
            list=$(unzip -l "$z" 2>/dev/null)
            if printf '%s' "$list" | grep -q "jnext.exe" \
               && printf '%s' "$list" | grep -q "Qt6Core.dll" \
               && printf '%s' "$list" | grep -q "platforms/qwindows.dll" \
               && printf '%s' "$list" | grep -qi "SDL3.dll"; then
                ok package-win "$(basename "$z") (jnext.exe + Qt6/SDL2/SDL3 DLLs + qwindows plugin)"
            else
                bad package-win ".zip missing bundled DLLs, qwindows plugin, or SDL3.dll (see $LOGDIR/win.log)"
            fi
        else
            bad package-win "no .zip produced (see $LOGDIR/win.log)"
        fi
    else
        bad package-win "make package-win failed (see $LOGDIR/win.log)"
    fi
else
    skp package-win "MinGW Qt6 cross toolchain not installed"
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
            ok package-flatpak "built (org.kde.Sdk present)"
        else
            bad package-flatpak "make package-flatpak failed (see $LOGDIR/flatpak.log)"
        fi
    else
        skp package-flatpak "manifest valid; org.kde.Sdk not installed — full build skipped"
    fi
else
    skp package-flatpak "flatpak-builder not installed"
fi

printf "\n${BOLD}=== Results ===${RESET}\n"
printf "  ${GREEN}Pass: %d${RESET}  ${RED}Fail: %d${RESET}  ${YELLOW}Skip: %d${RESET}\n" "$pass" "$fail" "$skip"
[ "$fail" -eq 0 ]
