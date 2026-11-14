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
# MINIMUM macOS. Every Mach-O's minimum OS version (LC_BUILD_VERSION minos, or
# LC_VERSION_MIN_MACOSX version) is read; the highest one and the file that
# carries it are always printed. When JNEXT_MACOS_MIN is set (the dmg-time call
# of `make package-macos` passes MACOS_DEPLOYMENT_TARGET), any file whose minimum
# is NEWER fails the build, and so does a file whose minimum cannot be read: an
# unknown minimum is not "old enough", and the bundle's Info.plist must carry
# LSMinimumSystemVersion equal to the floor. dyld refuses a library built for a newer
# macOS than the one running, so a dependency bottled for the runner's OS would
# otherwise raise the app's real floor with no warning to anyone.
#
# Run by the install step (see the APPLE branch in CMakeLists.txt) and by
# `make package-macos`.
#
set -euo pipefail

# note <text> - diagnostic line on stderr. A failed write (EINTR on a CI
# runner's pipe, a closed stderr) must never change the verdict: the counters
# and exit codes below do not depend on it. Hence the `|| true` under set -e.
note() { printf '%s\n' "$*" >&2 || true; }

if [ "$#" -ne 1 ]; then
    note "usage: $0 <jnext.app>"
    exit 2
fi

APP=$1

[ -d "$APP" ] || { note "error: bundle not found: $APP"; exit 1; }
command -v otool >/dev/null 2>&1 || { note "error: otool not found (Xcode command line tools)"; exit 1; }

# Dependency paths that are fine to keep: bundle-relative, or part of macOS.
# The ^ anchors are load-bearing: without them a path merely CONTAINING
# "/usr/lib/" (e.g. /opt/homebrew/Cellar/foo/1.0/usr/lib/foo.dylib) would be
# accepted. VB-11 pins that.
ALLOWED_RE='^(@executable_path|@loader_path|@rpath)/|^/usr/lib/|^/System/'

# shellcheck source=packaging/macos/macho-refs.sh
. "$(dirname "$0")/macho-refs.sh"

# ver_gt <a> <b> - succeed iff version a > version b, compared per numeric
# component (15.10 > 15.9; macOS has no `sort -V`). Missing components are 0.
ver_gt() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        na = split(a, x, "."); nb = split(b, y, ".")
        n = (na > nb) ? na : nb
        for (i = 1; i <= n; i++) {
            if (x[i] + 0 > y[i] + 0) exit 0
            if (x[i] + 0 < y[i] + 0) exit 1
        }
        exit 1
    }'
}

# macho_minos <file> - the highest minimum macOS version the file declares
# (one per slice of a universal binary), empty if it declares none.
# otool -l layout: "      cmd LC_BUILD_VERSION" ... "    minos 15.0", or
# "      cmd LC_VERSION_MIN_MACOSX" ... "  version 10.13".
macho_minos() {
    local v best=""
    while IFS= read -r v; do
        [ -n "$v" ] || continue
        if [ -z "$best" ] || ver_gt "$v" "$best"; then best=$v; fi
    done < <(otool -l "$1" 2>/dev/null | awk '
        /^ *cmd / { mode = ($2 == "LC_BUILD_VERSION") ? "minos" : ($2 == "LC_VERSION_MIN_MACOSX") ? "version" : ""; next }
        mode != "" && $1 == mode { print $2; mode = "" }
    ' || true)
    echo "$best"
}

MIN_FLOOR=${JNEXT_MACOS_MIN:-}
max_minos=""
max_minos_file=""
minos_over=0
minos_unknown=0
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

    # Minimum macOS of this file: tracked for the report, gated when a floor is set.
    mv=$(macho_minos "$macho")
    if [ -z "$mv" ]; then
        if [ -n "$MIN_FLOOR" ]; then
            if [ "$minos_unknown" -eq 0 ] && [ "$minos_over" -eq 0 ]; then
                note "FAIL: minimum macOS version (floor $MIN_FLOOR):"
                note
            fi
            note "  ${macho#"$APP"/}"$'\n'"      -> minos UNREADABLE (no LC_BUILD_VERSION / LC_VERSION_MIN_MACOSX)"
            minos_unknown=$((minos_unknown + 1))
        fi
    else
        if [ -z "$max_minos" ] || ver_gt "$mv" "$max_minos"; then
            max_minos=$mv; max_minos_file=${macho#"$APP"/}
        fi
        if [ -n "$MIN_FLOOR" ] && ver_gt "$mv" "$MIN_FLOOR"; then
            if [ "$minos_unknown" -eq 0 ] && [ "$minos_over" -eq 0 ]; then
                note "FAIL: minimum macOS version (floor $MIN_FLOOR):"
                note
            fi
            note "  ${macho#"$APP"/}"$'\n'"      -> minos $mv (newer than $MIN_FLOOR)"
            minos_over=$((minos_over + 1))
        fi
    fi

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
                            note "FAIL: these bundle-relative references do not resolve to a"
                            note "      file inside the bundle - dyld will fail on them at"
                            note "      launch (GH #46):"
                            note
                        fi
                        note "  ${macho#"$APP"/}"$'\n'"      -> $dep (MISSING)"
                        dangling=$((dangling + 1))
                    fi
                    ;;
            esac
            continue
        fi

        if [ -n "$own_id" ] && [ "$dep" = "$own_id" ]; then
            # The file's own install name, not something it loads.
            if [ "$stale_ids" -eq 0 ]; then
                note "note: these bundled files kept their original install name"
                note "      (LC_ID_DYLIB). Cosmetic - dyld loads them via the"
                note "      referring binary's path, which is checked below:"
            fi
            note "  ${macho#"$APP"/}"$'\n'"      id -> $dep"
            stale_ids=$((stale_ids + 1))
            continue
        fi

        if [ "$violations" -eq 0 ]; then
            note "FAIL: jnext.app is not self-contained - these references"
            note "      point outside the bundle and will abort in dyld on a"
            note "      machine that does not have them (GH #46):"
            note
        fi
        note "  ${macho#"$APP"/}"$'\n'"      -> $dep"
        violations=$((violations + 1))
    # Keep only tab-indented lines. `tail -n +2` alone is wrong for a
    # universal binary: otool re-prints a "<path> (architecture x):" header per
    # slice, and those headers would be read as dependencies. Real dependency
    # lines always start with a tab.
    done < <(macho_deps "$macho")
done < <(macho_files "$APP")

# NOTE (GH #57): a runtime-loaded (dlopen) dependency rule used to live here.
# Everything above walks LC_LOAD_DYLIB, which by construction CANNOT see a
# library opened with dlopen() at run time, so such a dependency had to be
# named explicitly. The one case was Homebrew's `sdl2` being sdl2-compat — an
# SDL2 API over SDL3 that dlopen()s libSDL3 by leaf name — which is how
# v0.98.72 passed every structural check here and still aborted at launch on a
# clean Mac (GH #46). jnext links SDL3 directly now, so libSDL3 appears in a
# normal load command and the walk above covers it; the rule could no longer
# fire on anything and was removed rather than left as a comment pretending to
# guard something. The general lesson stands and is why this file exists:
# "otool says the bundle is complete" and "the bundle is complete" are not the
# same claim. If a dlopen'd dependency is ever introduced again, it must be
# named here explicitly — no walk will find it.

if [ "$checked" -eq 0 ]; then
    note "error: no Mach-O files found in $APP - the bundle is empty or malformed"
    exit 1
fi

if [ -n "$max_minos" ]; then
    printf '%s\n' "verify-bundle: highest minos $max_minos ($max_minos_file)" || true
else
    printf '%s\n' "verify-bundle: highest minos unknown (no file declares one)" || true
fi

# With a floor, the bundle must SAY it: Info.plist's LSMinimumSystemVersion equal
# to the floor, so the Finder refuses an older macOS instead of dyld killing the
# app. Whitespace is stripped so the key and its <string> may sit on separate lines.
plist_bad=0
if [ -n "$MIN_FLOOR" ]; then
    plist_val=""
    if [ -f "$APP/Contents/Info.plist" ]; then
        plist_val=$(tr -d ' \t\r\n' < "$APP/Contents/Info.plist" |
            sed -n 's|.*<key>LSMinimumSystemVersion</key><string>\([^<]*\)</string>.*|\1|p' || true)
    fi
    if [ "$plist_val" != "$MIN_FLOOR" ]; then
        note "FAIL: Contents/Info.plist LSMinimumSystemVersion is '${plist_val:-<absent>}', expected '$MIN_FLOOR'"
        plist_bad=1
    fi
fi

if [ "$minos_over" -gt 0 ] || [ "$minos_unknown" -gt 0 ] || [ "$plist_bad" -ne 0 ]; then
    note
    note "$minos_over file(s) newer than macOS $MIN_FLOOR and $minos_unknown with no"
    note "readable minimum, across $checked Mach-O file(s)."
    exit 1
fi

if [ "$violations" -gt 0 ] || [ "$dangling" -gt 0 ]; then
    note
    note "$violations external LOAD reference(s) and $dangling unresolved"
    note "bundle-relative reference(s) across $checked Mach-O file(s)."
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
        note "FAIL: code signature is not valid - the app will be killed at launch"
        note "      on Apple Silicon (Qt QTBUG-138019):"
        sed 's/^/  /' /tmp/vb-codesign.$$ >&2 || true
        rm -f /tmp/vb-codesign.$$
        exit 1
    fi
    rm -f /tmp/vb-codesign.$$
else
    note "note: codesign not available - signature validity NOT checked"
fi

echo "verify-bundle: OK - $checked Mach-O file(s); no external load references," \
     "every bundle-relative reference resolves, signature valid" \
     "($stale_ids cosmetic install-name(s) left unrewritten)" || true
