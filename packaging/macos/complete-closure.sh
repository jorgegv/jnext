#!/usr/bin/env bash
#
# complete-closure.sh <jnext.app>
#
# Copy in libraries that the bundle references but does not contain.
#
# macdeployqt does not always finish the job. Observed on macos-latest (run
# 29856062806): it copied libwebp/libwebpmux/libwebpdemux into
# Contents/Frameworks but not libsharpyuv.0.dylib, which all three load — so
# the bundle referenced a file that was nowhere in it, and dyld would fail on
# that at launch exactly as it failed on the absolute /opt/homebrew path this
# whole change exists to remove. It exits 0 in that state.
#
# Deliberately ADD-ONLY. The alternative fix — deleting the frameworks nothing
# should have pulled in — needs a reachability analysis over dlopen'd plugins to
# be safe, and this runs on a platform we cannot test on. A wrong addition costs
# a few hundred KB in the .dmg; a wrong deletion produces an app that dies at
# launch. When the failure mode cannot be tested, choose the benign one.
#
# Iterates to a fixed point: a library copied in brings its own dependencies.
#
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <jnext.app>" >&2
    exit 2
fi

APP=$1
[ -d "$APP" ] || { echo "error: bundle not found: $APP" >&2; exit 1; }
command -v otool >/dev/null 2>&1 || { echo "error: otool not found" >&2; exit 1; }
command -v install_name_tool >/dev/null 2>&1 || { echo "error: install_name_tool not found" >&2; exit 1; }

# shellcheck source=packaging/macos/macho-refs.sh
. "$(dirname "$0")/macho-refs.sh"

FRAMEWORKS="$APP/Contents/Frameworks"
mkdir -p "$FRAMEWORKS"

# Where to look for a library that is missing from the bundle. The reference
# itself is bundle-relative by the time we see it — macdeployqt has already
# rewritten it — so the original absolute path is gone and we search by
# basename. HOMEBREW_PREFIX lets the search be pointed elsewhere (tests do this).
BREW_PREFIX=${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}

find_library() {
    local base=$1 hit
    for dir in "$BREW_PREFIX"/lib "$BREW_PREFIX"/opt/*/lib; do
        [ -d "$dir" ] || continue
        hit="$dir/$base"
        [ -e "$hit" ] && { echo "$hit"; return 0; }
    done
    echo ""
}

copied_total=0

for pass in 1 2 3 4 5; do
    copied_this_pass=0

    while IFS= read -r macho; do
        rplist=$(search_rpaths "$macho")
        while IFS= read -r dep; do
            [ -n "$dep" ] || continue
            ref_is_broken "$dep" "$macho" "$rplist" || continue

            base=$(basename "$dep")
            # Only ever satisfy a reference with a plain library name. A
            # framework-shaped reference (Foo.framework/Versions/A/Foo) is
            # macdeployqt's business and copying one file out of it would
            # produce a malformed framework.
            case "$dep" in
                *.framework/*) continue ;;
            esac

            [ -e "$FRAMEWORKS/$base" ] && continue

            src=$(find_library "$base")
            if [ -z "$src" ]; then
                echo "complete-closure: cannot find $base for ${macho#"$APP"/} under $BREW_PREFIX" >&2
                continue
            fi

            echo "complete-closure: adding $base (needed by ${macho#"$APP"/})"
            cp -L "$src" "$FRAMEWORKS/$base"
            chmod u+w "$FRAMEWORKS/$base"
            install_name_tool -id "@rpath/$base" "$FRAMEWORKS/$base" 2>/dev/null || true

            # Point its own absolute non-system dependencies back into the
            # bundle, so the next pass can satisfy them by basename too.
            while IFS= read -r sub; do
                case "$sub" in
                    /usr/lib/*|/System/*|@*) continue ;;
                esac
                install_name_tool -change "$sub" "@rpath/$(basename "$sub")" \
                    "$FRAMEWORKS/$base" 2>/dev/null || true
            done < <(macho_deps "$FRAMEWORKS/$base")

            copied_this_pass=$((copied_this_pass + 1))
            copied_total=$((copied_total + 1))
        done < <(macho_deps "$macho")
    done < <(macho_files "$APP")

    [ "$copied_this_pass" -eq 0 ] && break
    echo "complete-closure: pass $pass added $copied_this_pass librar(ies), looking again"
done

echo "complete-closure: $copied_total librar(ies) added"
