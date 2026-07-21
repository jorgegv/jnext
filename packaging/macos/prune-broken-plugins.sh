#!/usr/bin/env bash
#
# prune-broken-plugins.sh <jnext.app>
#
# Delete Qt plugins that cannot load, and fail if an essential one is among them.
#
# macdeployqt deploys every plugin belonging to the Qt modules it finds, whether
# or not jnext uses them. On a Homebrew Qt — which is split into per-module
# prefixes (qtbase, qtsvg, qtdeclarative, qtvirtualkeyboard) — it then cannot
# always resolve those plugins' own framework dependencies, because the rpath
# list it searches only covers some of the prefixes. Observed on macos-latest
# (run 29852867578 / 29854836676):
#
#   ERROR: Cannot resolve rpath "@rpath/QtSvg.framework/Versions/A/QtSvg"
#   ERROR:  using QList("/opt/homebrew/opt/qtbase/lib", ... )   <- no qtsvg
#
# and it exits 0 regardless. The result is a bundle containing plugins whose
# frameworks are absent: the SVG image format, the PDF one, and the virtual
# keyboard input context. jnext (a Widgets app with an .icns icon) needs none of
# them, and a plugin that cannot load is strictly worse than an absent one — it
# is a dyld failure waiting for the first user whose workflow touches it.
#
# The rule is generic rather than a hard-coded list of plugin names, so it keeps
# working when Qt reshuffles its modules: remove any plugin with an unresolvable
# bundle-relative reference. Essential plugins are exempt from removal and FAIL
# the build instead — without the Cocoa platform plugin there is no GUI at all,
# and silently shipping a GUI-less .app would be a worse bug than the one this
# whole change exists to fix.
#
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <jnext.app>" >&2
    exit 2
fi

APP=$1
[ -d "$APP" ] || { echo "error: bundle not found: $APP" >&2; exit 1; }
command -v otool >/dev/null 2>&1 || { echo "error: otool not found" >&2; exit 1; }

# shellcheck source=packaging/macos/macho-refs.sh
. "$(dirname "$0")/macho-refs.sh"

# Plugins jnext cannot run without. Removing one of these would produce an app
# that starts and then dies with "no Qt platform plugin could be initialized".
ESSENTIAL_RE='^PlugIns/platforms/libqcocoa\.dylib$'

PLUGIN_DIR="$APP/Contents/PlugIns"
[ -d "$PLUGIN_DIR" ] || { echo "prune-broken-plugins: no PlugIns directory, nothing to do"; exit 0; }

pruned=0
essential_broken=0

while IFS= read -r plugin; do
    rel=${plugin#"$APP"/Contents/}
    broken_ref=""
    rplist=$(search_rpaths "$plugin")

    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        if ref_is_broken "$dep" "$plugin" "$rplist"; then
            broken_ref=$dep
            break
        fi
    done < <(macho_deps "$plugin")

    [ -n "$broken_ref" ] || continue

    if [[ "$rel" =~ $ESSENTIAL_RE ]]; then
        echo "FAIL: the Cocoa platform plugin cannot load — jnext has no GUI without it" >&2
        printf '  %s\n      -> %s (MISSING)\n' "$rel" "$broken_ref" >&2
        essential_broken=$((essential_broken + 1))
        continue
    fi

    printf 'prune-broken-plugins: removing %s (needs %s, not in the bundle)\n' \
        "$rel" "$broken_ref"
    rm -f "$plugin"
    pruned=$((pruned + 1))
done < <(find "$PLUGIN_DIR" -type f -name '*.dylib')

if [ "$essential_broken" -gt 0 ]; then
    exit 1
fi

# Tidy up plugin directories left empty by the removals, so the bundle does not
# ship empty folders Qt would scan at startup for nothing.
find "$PLUGIN_DIR" -type d -empty -delete 2>/dev/null || true

echo "prune-broken-plugins: $pruned unusable plugin(s) removed"
