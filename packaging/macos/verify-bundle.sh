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
ALLOWED_RE='^(@executable_path|@loader_path|@rpath)/|^/usr/lib/|^/System/'

violations=0
checked=0

# Every Mach-O in the bundle, not just the main executable: a bundled dylib or
# a Qt plugin can carry an unfixed reference of its own, and dyld will fail on
# it exactly the same way.
while IFS= read -r macho; do
    checked=$((checked + 1))

    # otool -L prints the file name on line 1, then one dependency per line as
    # "\t<path> (compatibility version ...)". Drop line 1, keep the path field.
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue          # otool emits blank lines
        # A dylib's own install-name (id) is line 2 for a library; it is
        # harmless when bundle-relative and caught by the same rule when not.
        if [[ ! "$dep" =~ $ALLOWED_RE ]]; then
            if [ "$violations" -eq 0 ]; then
                echo "FAIL: jnext.app is not self-contained — these references" >&2
                echo "      point outside the bundle and will abort in dyld on a" >&2
                echo "      machine that does not have them (GH #46):" >&2
                echo >&2
            fi
            printf '  %s\n      -> %s\n' "${macho#"$APP"/}" "$dep" >&2
            violations=$((violations + 1))
        fi
    done < <(otool -L "$macho" | tail -n +2 | awk '{print $1}')
done < <(find "$APP" -type f -perm -u+r -exec sh -c 'file -b "$1" | grep -q "Mach-O" && echo "$1"' _ {} \;)

if [ "$checked" -eq 0 ]; then
    echo "error: no Mach-O files found in $APP — the bundle is empty or malformed" >&2
    exit 1
fi

if [ "$violations" -gt 0 ]; then
    echo >&2
    echo "$violations external reference(s) across $checked Mach-O file(s)." >&2
    exit 1
fi

echo "verify-bundle: OK — $checked Mach-O file(s), no references outside the bundle"
