#!/usr/bin/env bash
#
# verify-bundle.sh <jnext.app>
#
# Prove that jnext.app carries every non-system library it needs, so it runs on
# a Mac that has no Homebrew (GH #46: the v0.98.34 .dmg shipped a bare binary
# still referencing /opt/homebrew/opt/libpng/lib/libpng16.16.dylib and aborted
# in dyld at launch on the reporter's machine).
#
# It walks every Mach-O file in the bundle and reads its LC_LOAD_DYLIB entries
# with otool -L. A dependency is acceptable only if it is:
#   - bundle-relative: @executable_path/... @loader_path/... @rpath/...
#   - supplied by macOS itself: /usr/lib/... /System/...
# Anything else (/opt/homebrew, /usr/local, /opt/local, a build directory, any
# other absolute path) means the bundle depends on the machine that built it.
#
# This exists because macOS packaging cannot be exercised on the Linux dev host:
# it is the only thing standing between a deployment tool silently missing a
# library and another .dmg that runs nowhere but the CI runner. It must FAIL the
# build, never warn.
#
# Run by the install step (see the APPLE branch in CMakeLists.txt) and by
# `make package-macos`.
#
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <jnext.app>" >&2
    exit 2
fi

APP=$1

[ -d "$APP" ] || { echo "error: bundle not found: $APP" >&2; exit 1; }
command -v otool >/dev/null 2>&1 || { echo "error: otool not found (Xcode command line tools)" >&2; exit 1; }

# Dependency paths that are fine to keep: bundle-relative, or part of macOS.
# The ^ anchors are load-bearing: without them a path merely CONTAINING
# "/usr/lib/" (e.g. /opt/homebrew/Cellar/foo/1.0/usr/lib/foo.dylib) would be
# accepted. VB-11 pins that.
ALLOWED_RE='^(@executable_path|@loader_path|@rpath)/|^/usr/lib/|^/System/'

# shellcheck source=packaging/macos/macho-refs.sh
. "$(dirname "$0")/macho-refs.sh"

violations=0
stale_ids=0
dangling=0
checked=0

# Every Mach-O in the bundle, not just the main executable: a bundled dylib or
# a Qt plugin can carry an unfixed reference of its own, and dyld will fail on
# it exactly the same way.
while IFS= read -r macho; do
    checked=$((checked + 1))

    # A dylib's LC_ID_DYLIB (its install NAME) appears in `otool -L` output as
    # line 2, indistinguishable from a dependency — and macdeployqt leaves some
    # copied frameworks with their original absolute id (observed on
    # macos-latest, run 29852867578: 6 of 60 files). That is NOT a loading
    # failure: dyld resolves each load from the REFERRING binary's
    # LC_LOAD_DYLIB, never from the loaded file's own id. So the id is read
    # separately with `otool -D` and reported rather than failed on — otherwise
    # the gate blocks on something that cannot break the shipped app, and a
    # gate that cries wolf gets switched off. Executables have no id and
    # `otool -D` prints only the header line for them.
    # `|| true` under `set -e`/pipefail: a file otool -D cannot read yields an
    # empty id, which excuses NOTHING — the failure direction is toward
    # checking more, never less.
    own_id=$(macho_id "$macho")
    # Once per FILE, not once per reference — see search_rpaths in macho-refs.sh.
    rplist=$(search_rpaths "$macho")

    # otool -L prints the file name on line 1, then one dependency per line as
    # "\t<path> (compatibility version ...)". Drop line 1, keep the path field.
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue          # otool emits blank lines

        if [[ "$dep" =~ $ALLOWED_RE ]]; then
            # Bundle-relative: prove the file it names is actually THERE.
            case "$dep" in
                @*)
                    if ref_is_broken "$dep" "$macho" "$rplist"; then
                        if [ "$dangling" -eq 0 ]; then
                            echo "FAIL: these bundle-relative references do not resolve to a" >&2
                            echo "      file inside the bundle — dyld will fail on them at" >&2
                            echo "      launch (GH #46):" >&2
                            echo >&2
                        fi
                        printf '  %s\n      -> %s (MISSING)\n' "${macho#"$APP"/}" "$dep" >&2
                        dangling=$((dangling + 1))
                    fi
                    ;;
            esac
            continue
        fi

        if [ -n "$own_id" ] && [ "$dep" = "$own_id" ]; then
            # The file's own install name, not something it loads.
            if [ "$stale_ids" -eq 0 ]; then
                echo "note: these bundled files kept their original install name" >&2
                echo "      (LC_ID_DYLIB). Cosmetic — dyld loads them via the" >&2
                echo "      referring binary's path, which is checked below:" >&2
            fi
            printf '  %s\n      id -> %s\n' "${macho#"$APP"/}" "$dep" >&2
            stale_ids=$((stale_ids + 1))
            continue
        fi

        if [ "$violations" -eq 0 ]; then
            echo "FAIL: jnext.app is not self-contained — these references" >&2
            echo "      point outside the bundle and will abort in dyld on a" >&2
            echo "      machine that does not have them (GH #46):" >&2
            echo >&2
        fi
        printf '  %s\n      -> %s\n' "${macho#"$APP"/}" "$dep" >&2
        violations=$((violations + 1))
    # Keep only tab-indented lines. `tail -n +2` alone is wrong for a
    # universal binary: otool re-prints a "<path> (architecture x):" header per
    # slice, and those headers would be read as dependencies. Real dependency
    # lines always start with a tab.
    done < <(macho_deps "$macho")
done < <(macho_files "$APP")

# --- runtime-loaded (dlopen) dependencies ------------------------------------
# Everything above walks LC_LOAD_DYLIB, which by construction CANNOT see a
# library opened with dlopen() at run time: it appears in no load command. Such
# a dependency has to be named explicitly, here and in bundle-dlopen-deps.sh.
#
# This exact hole shipped. v0.98.72 passed every structural check above and then
# aborted at launch on a clean Mac, because Homebrew's sdl2 is sdl2-compat —
# an SDL2 API over SDL3 that dlopen()s libSDL3 by leaf name — and nothing had
# copied libSDL3 in (GH #46, reported on macOS 26.5.2 arm64). "otool says the
# bundle is complete" and "the bundle is complete" are not the same claim.
# `|| true`: find exits non-zero when Contents/Frameworks does not exist, and
# under `set -e` a failing command substitution in an assignment kills the whole
# script — silently, before any verdict is printed.
sdl2_lib=$(find "$APP/Contents/Frameworks" -maxdepth 1 -name 'libSDL2*.dylib' 2>/dev/null | head -1 || true)
if [ -n "$sdl2_lib" ] && grep -qa 'libSDL3' "$sdl2_lib"; then
    if [ ! -e "$APP/Contents/Frameworks/libSDL3.dylib" ]; then
        echo "FAIL: the bundled SDL2 is sdl2-compat, which dlopen()s libSDL3 at" >&2
        echo "      start-up, but Contents/Frameworks/libSDL3.dylib is MISSING." >&2
        echo "      otool cannot see this dependency; the app aborts at launch" >&2
        echo "      with \"Failed to initialize internal SDL dynapi\" (GH #46)." >&2
        exit 1
    fi
    echo "verify-bundle: sdl2-compat detected, libSDL3.dylib present"
fi

if [ "$checked" -eq 0 ]; then
    echo "error: no Mach-O files found in $APP — the bundle is empty or malformed" >&2
    exit 1
fi

if [ "$violations" -gt 0 ] || [ "$dangling" -gt 0 ]; then
    echo >&2
    echo "$violations external LOAD reference(s) and $dangling unresolved" >&2
    echo "bundle-relative reference(s) across $checked Mach-O file(s)." >&2
    exit 1
fi

# The signature must be VALID, not merely present. Every install_name_tool
# rewrite macdeployqt performs invalidates the signature of the file it edited,
# and on Apple Silicon the kernel refuses to map invalidly-signed pages: the app
# dies at launch. Nothing else here can see that — a `--version` run returns
# before Qt loads its Cocoa platform plugin, so the lazily-dlopen'd plugins that
# macdeployqt rewrote are never exercised until a real user double-clicks.
if command -v codesign >/dev/null 2>&1; then
    if ! codesign --verify --deep --strict "$APP" 2>/tmp/vb-codesign.$$; then
        echo "FAIL: code signature is not valid — the app will be killed at launch" >&2
        echo "      on Apple Silicon (Qt QTBUG-138019):" >&2
        sed 's/^/  /' /tmp/vb-codesign.$$ >&2
        rm -f /tmp/vb-codesign.$$
        exit 1
    fi
    rm -f /tmp/vb-codesign.$$
else
    echo "note: codesign not available — signature validity NOT checked" >&2
fi

echo "verify-bundle: OK — $checked Mach-O file(s); no external load references," \
     "every bundle-relative reference resolves, signature valid" \
     "($stale_ids cosmetic install-name(s) left unrewritten)"
