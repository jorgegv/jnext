#!/usr/bin/env bash
#
# bundle-dlopen-deps.sh <jnext.app>
#
# Copy in libraries the bundle loads with dlopen() rather than links.
#
# WHY NOTHING ELSE CAN DO THIS. Every other step in this pipeline — macdeployqt,
# complete-closure.sh, verify-bundle.sh — walks LC_LOAD_DYLIB. A dependency
# opened by dlopen() at run time appears in NO load command, so a dependency
# walk is structurally incapable of finding it. It has to be known and named.
#
# THE CASE THIS EXISTS FOR. Homebrew's `sdl2` formula is **sdl2-compat**: an
# SDL2 API implemented on top of SDL3, which dlopen()s libSDL3 by leaf name at
# start-up. `otool -L` on the bundled libSDL2 shows no SDL3 entry at all.
# macdeployqt therefore never copied it, every structural check passed, and the
# shipped .app aborted at launch on a clean Mac:
#
#     Abort trap: 6, from a load-time initializer inside libSDL2-2.0.0.dylib
#     "Failed to initialize internal SDL dynapi. As this would otherwise crash,
#      we have to abort now."
#
# Reported against v0.98.72 on macOS 26.5.2 arm64 (GH #46). CI could not see it:
# the smoke test runs the unbundled build/jnext, which finds SDL3 in the build
# machine's own Homebrew.
#
# THE SAME BUG WAS ALREADY FIXED ON WINDOWS. packaging/windows/bundle-dlls.sh
# detects the identical sdl2-compat shim (SDL2.dll LoadLibrary()s SDL3.dll, so
# it is absent from the PE import table) and bundles SDL3.dll. macOS shipped
# without the equivalent. When one platform's packaging grows a special case for
# a runtime-loaded dependency, every platform needs it — the shim is in the
# library, not in the platform.
#
# Destination is Contents/Frameworks/libSDL3.dylib because that is where
# sdl2-compat looks first (@loader_path/libSDL3.dylib, alongside the SDL2 that
# opens it). Confirmed by the reporter: dropping the library there fixed the
# crash outright.
#
# Runs BEFORE codesign so the copy is covered by the signature.
#
# NO TRANSITIVE CLOSURE HERE, DELIBERATELY. The Windows twin resolves SDL3.dll's
# own imports (bundle-dlls.sh resolve_queue) because nothing downstream of it
# ever re-checks the .zip. On macOS the copy is followed by verify-bundle.sh,
# which walks EVERY Mach-O in the finished bundle — including this one — and
# fails the build on any unresolved or out-of-bundle reference. So an SDL3 that
# grew an external dependency would break the package loudly rather than ship
# silently, and today it has none (CI run 29874058824: 58 Mach-O files, zero
# external load references). Running complete-closure.sh again here would be
# strictly redundant with that gate.
#
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <jnext.app>" >&2
    exit 2
fi

APP=$1
[ -d "$APP" ] || { echo "error: bundle not found: $APP" >&2; exit 1; }

FRAMEWORKS="$APP/Contents/Frameworks"
BREW_PREFIX=${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}

added=0

# --- sdl2-compat -> libSDL3 --------------------------------------------------
# `|| true` — see the note in verify-bundle.sh: a failing command substitution
# under `set -e` kills the script before it can report anything.
sdl2=$(find "$FRAMEWORKS" -maxdepth 1 -name 'libSDL2*.dylib' 2>/dev/null | head -1 || true)
if [ -n "$sdl2" ]; then
    # grep the binary directly (grep -a) rather than `strings | grep -q`: under
    # `set -o pipefail`, grep -q closes the pipe on its first match and SIGPIPEs
    # the producer, which fails the pipeline and would SILENTLY skip SDL3. The
    # Windows script carries the same warning for the same reason.
    if grep -qa 'libSDL3' "$sdl2"; then
        if [ -e "$FRAMEWORKS/libSDL3.dylib" ]; then
            echo "bundle-dlopen-deps: libSDL3.dylib already present"
        else
            src=""
            for cand in "$BREW_PREFIX"/opt/sdl3/lib/libSDL3.0.dylib \
                        "$BREW_PREFIX"/opt/sdl3/lib/libSDL3.dylib \
                        "$BREW_PREFIX"/lib/libSDL3.0.dylib \
                        "$BREW_PREFIX"/lib/libSDL3.dylib; do
                [ -e "$cand" ] && { src=$cand; break; }
            done
            if [ -z "$src" ]; then
                echo "FAIL: the bundled SDL2 is sdl2-compat, which dlopen()s libSDL3 at" >&2
                echo "      start-up, but no libSDL3 was found under $BREW_PREFIX." >&2
                echo "      Without it the app aborts at launch (GH #46). Install it:" >&2
                echo "          brew install sdl3" >&2
                exit 1
            fi
            cp -L "$src" "$FRAMEWORKS/libSDL3.dylib"
            chmod u+w "$FRAMEWORKS/libSDL3.dylib"
            install_name_tool -id "@rpath/libSDL3.dylib" "$FRAMEWORKS/libSDL3.dylib" 2>/dev/null || true
            echo "bundle-dlopen-deps: added libSDL3.dylib (dlopen'd by sdl2-compat, invisible to otool)"
            added=$((added + 1))
        fi
    fi
fi

echo "bundle-dlopen-deps: $added runtime-loaded librar(ies) added"
