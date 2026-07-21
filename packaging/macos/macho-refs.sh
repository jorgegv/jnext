#!/usr/bin/env bash
#
# macho-refs.sh — sourceable helpers for reading Mach-O references in a bundle.
#
# Shared by verify-bundle.sh (which only ever REPORTS) and prune-broken-plugins.sh
# (which MUTATES). They must agree exactly on what "this reference resolves"
# means: if the pruner and the checker ever disagreed, the pruner could delete a
# plugin the checker was happy with, or leave one the checker then fails on —
# and the build would be unfixable without reading both scripts. One definition,
# two callers.
#
# Callers must set APP to the bundle root before calling these.
#

# macho_files <app> — every Mach-O file in the bundle, one per line.
# Not just the main executable: a bundled dylib or a Qt plugin carries its own
# references, and dyld fails on those exactly as hard.
macho_files() {
    find "$1" -type f -perm -u+r \
        -exec sh -c 'file -b "$1" | grep -q "Mach-O" && echo "$1"' _ {} \;
}

# macho_deps <file> — the LC_LOAD_DYLIB paths of a Mach-O.
#
# Keep only tab-indented lines. `tail -n +2` is WRONG for a universal binary:
# otool re-prints a "<path> (architecture x):" header per slice, and those
# headers would be read as dependencies. Real dependency lines start with a tab.
macho_deps() {
    otool -L "$1" | awk '/^\t/ {print $1}'
}

# macho_id <file> — the LC_ID_DYLIB (install name), empty for an executable.
#
# It must be read separately: in `otool -L` output the id is line 2 and looks
# exactly like a dependency, but for an EXECUTABLE line 2 is a real dependency.
# Treating line 2 as the id would stop checking the main binary entirely — which
# is where the GH #46 bug actually lived.
#
# `|| true` under set -e/pipefail: a file otool cannot read yields an empty id,
# which excuses NOTHING. The failure direction is toward checking more.
macho_id() {
    otool -D "$1" 2>/dev/null | tail -n +2 | head -1 || true
}

# resolve_ref <ref> <referring-file> — echo the real path a bundle-relative
# reference names, or empty if it does not resolve. "@rpath/X" is searched
# against the referring file's own LC_RPATH entries, exactly as dyld does.
#
# A bundle-relative STRING proves nothing on its own: macdeployqt drops a
# framework it cannot resolve from its copy worklist while leaving the reference
# in place, so "@rpath/QtSvg.framework/..." can name a file that was never
# copied. dyld fails on that at launch just as hard as on an absolute
# /opt/homebrew path — the GH #46 symptom, one layer removed.
resolve_ref() {
    local ref=$1 macho=$2 loader_dir exe_dir cand
    loader_dir=$(dirname "$macho")
    exe_dir="$APP/Contents/MacOS"

    case "$ref" in
        @executable_path/*) echo "${exe_dir}/${ref#@executable_path/}"; return 0 ;;
        @loader_path/*)     echo "${loader_dir}/${ref#@loader_path/}";  return 0 ;;
        @rpath/*)
            local suffix=${ref#@rpath/} rp
            while IFS= read -r rp; do
                [ -n "$rp" ] || continue
                case "$rp" in
                    @executable_path/*) rp="${exe_dir}/${rp#@executable_path/}" ;;
                    @loader_path/*)     rp="${loader_dir}/${rp#@loader_path/}" ;;
                esac
                cand="$rp/$suffix"
                if [ -e "$cand" ]; then echo "$cand"; return 0; fi
            done < <(otool -l "$macho" 2>/dev/null |
                     awk '/LC_RPATH/{f=1} f&&/^ *path /{print $2; f=0}')
            echo ""
            return 0
            ;;
    esac
    echo ""
}

# ref_is_broken <ref> <referring-file> — true when a bundle-relative reference
# names a file that is not in the bundle. Absolute paths are not this function's
# business (verify-bundle.sh judges those against its allow-list).
ref_is_broken() {
    local ref=$1 macho=$2 target
    case "$ref" in
        @*) ;;
        *)  return 1 ;;
    esac
    target=$(resolve_ref "$ref" "$macho")
    [ -z "$target" ] || [ ! -e "$target" ]
}
