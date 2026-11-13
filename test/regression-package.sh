#!/usr/bin/env bash
# Regression-test ONE package's own binary (GH #487): unpack the artifact the
# package-<p> target just built and run the regression harness against the
# jnext inside it, never against a dev build.
#
# Usage: bash test/regression-package.sh <rpm|deb|win|win-qt5|win32-qt5>
#        (`make regression-package PACKAGE=<p>` builds the package first.)
#
# Why a script of its own: the suite's regular runs test build/gui-release (and
# the Windows run a build tree), which is a different file from the one each
# package ships, in a different workflow. release.yml runs this after each
# package step, in the job that built it, so the published file is the tested one.
#
# Rows: every screenshot row of regression_tests.conf (derived, and checked
# against that file's `# expect:` pin) plus the functional rows named in
# FUNC_ROWS below, each of which needs only the package's Qt jnext (no SDL
# tree, no X server, no mtools). Never stamped: the run is filtered.
#
#   rpm | deb       extract the payload into build/regression-package/<p>/ and run
#                   regression.sh with JNEXT=<scratch>/usr/bin/jnext.
#   win*            unzip into the scratch dir, record a sha256 manifest of every
#                   member, add Qt's offscreen platform plugin (and its DLL closure)
#                   to the scratch COPY only, never clobbering a member and never
#                   touching the zip, then run `regression.sh --platform` with the
#                   copy as the Qt tree (its target OS is read from jnext.exe's PE
#                   header: there is no CMakeCache.txt in a package).
#
# The run is green only if the harness exits 0, reports Fail 0 and Skip 0 and its
# total is exactly lints + 1 sdcard-provision + screenshots + functional rows (a
# filtered run skips regression.sh's own completeness check, so this is it).
#
# Env: JNEXT_REGRESSION_CONF overrides the screenshot manifest, like regression.sh.
#      JNEXT_REGRESSION_PACKAGE_RUNNER (TEST-ONLY, like the wine watchdog overrides
#      in regression.sh) is an executable run INSTEAD of
#      `bash test/00regression/regression.sh`, so a stub can record the arguments
#      and environment it is given. Exit: 0 green, 1 the run failed, 2 harness fault.
set -euo pipefail
export LANG=C

FUNC_ROWS=(
    exit-frames-func cli-bare-file-func magic-port-func silent-func
    screenshot-io-qt-func snapshot-save-func snapshot-jns-roundtrip-func snapshot-machine-func
    tape-save-boot-func rzx-playback-func audio-gain-func reset-to-nextzxos-func
    esxdos-hostfs-func paused-idle-qt-func dzrp-qt-func gdb-qt-func zrcp-qt-func
)

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

fault() { printf 'regression-package: %s\n' "$*" >&2; exit 2; }

pkg=${1:-}
# shellcheck source=test/packaging/artifact-select.sh
. test/packaging/artifact-select.sh
VER=$(artifact_version .) || fault "cannot read version.yaml"
case "$pkg" in
    rpm)       tree=build/rpm-release;         glob='jnext-*.x86_64.rpm'; want="jnext-$VER-1.x86_64.rpm" ;;
    deb)       tree=build/deb-release;         glob='jnext_*_amd64.deb';  want="jnext_${VER}_amd64.deb" ;;
    win)       tree=build/win-release;         glob='*.zip';              want="jnext-$VER-windows-x64.zip" ;;
    win-qt5)   tree=build/win-qt5-release;     glob='*.zip';              want="jnext-$VER-windows-x64-legacy.zip" ;;
    win32-qt5) tree=build/win32-qt5-release;   glob='*.zip';              want="jnext-$VER-windows-x86-legacy.zip" ;;
    *) fault "name the package: rpm | deb | win | win-qt5 | win32-qt5 (got '${pkg}')" ;;
esac

art=$(pick_artifact "$tree" "$glob" "$want") \
    || fault "no current artifact for '$pkg': run make package-$pkg (looked for $tree/$want)"
art=$root/$art

# --- the rows ---
conf=${JNEXT_REGRESSION_CONF:-$root/test/00regression/regression_tests.conf}
[[ -f "$conf" ]] || fault "screenshot manifest not found: $conf"
mapfile -t shots < <(awk '!/^[[:space:]]*(#|$)/ { print $1 }' "$conf")
pin=$(sed -n 's/^# *expect: *\([0-9][0-9]*\).*/\1/p' "$conf" | head -n1)
[[ -n "$pin" ]] || fault "$conf carries no '# expect: N' pin"
[[ ${#shots[@]} -eq $pin ]] || fault "$conf declares ${#shots[@]} screenshot rows but pins $pin"
rows=("${shots[@]}" "${FUNC_ROWS[@]}")

# --- unpack ---
scratch=$root/build/regression-package/$pkg
rm -rf "$scratch"
mkdir -p "$scratch"
reg_args=()
case "$pkg" in
    rpm)
        command -v rpm2cpio >/dev/null 2>&1 && command -v cpio >/dev/null 2>&1 || fault "rpm2cpio and cpio are needed to unpack an rpm"
        (cd "$scratch" && rpm2cpio "$art" | cpio -idm --quiet)
        ;;
    deb)
        command -v dpkg-deb >/dev/null 2>&1 || fault "dpkg-deb is needed to unpack a deb"
        dpkg-deb -x "$art" "$scratch"
        ;;
esac

run_env=()
case "$pkg" in
    rpm|deb)
        jn=$scratch/usr/bin/jnext
        [[ -x "$jn" ]] || fault "the unpacked package has no executable usr/bin/jnext"
        run_env=(JNEXT="$jn")
        lints=$(sed -n 's/^ *reg_lints=\([0-9][0-9]*\).*/\1/p' test/00regression/regression.sh | head -n1)
        [[ -n "$lints" ]] || fault "cannot read reg_lints out of test/00regression/regression.sh"
        ;;
    win*)
        command -v unzip >/dev/null 2>&1 || fault "unzip is needed to unpack a Windows package"
        zip_sha=$(sha256sum "$art")
        zip_sha=${zip_sha%% *}
        # The test plugin must not already be in what ships (it would pass for staged).
        zlist=$(unzip -Z1 "$art")
        [[ "$zlist" != *platforms/qoffscreen.dll* ]] || fault "the shipped zip already carries platforms/qoffscreen.dll (the test plugin never ships)"
        unzip -q "$art" -d "$scratch"
        dir=$scratch/${want%.zip}
        [[ -f "$dir/jnext.exe" ]] || fault "the zip has no ${want%.zip}/jnext.exe"
        manifest=$scratch/manifest.sha256
        (cd "$dir" && find . -type f -print0 | sort -z | xargs -0 sha256sum) > "$manifest"
        # The plugin and its DLL closure come through bundle-dlls.sh into an EMPTY dir
        # beside the copy; only files ABSENT from the copy are added (Qt5's qoffscreen
        # needs libfreetype, which a bare cp of the plugin would miss, and a clobbering
        # copy would replace the zip's own DLLs with the sysroot's).
        # bundle-dlls.sh looks for libspdlog.dll under <exe dir>/third_party/spdlog, as in
        # a build tree, so the exe is staged from a dir laid out that way with the zip's own copy.
        stage=$scratch/plugin-stage
        stage_src=$scratch/plugin-stage-src
        mkdir -p "$stage" "$stage_src/third_party/spdlog"
        cp "$dir/jnext.exe" "$stage_src/"
        for f in "$dir"/libspdlog*.dll; do [[ ! -f "$f" ]] || cp "$f" "$stage_src/third_party/spdlog/"; done
        BUNDLE_EXTRA_QT_PLUGINS=platforms/qoffscreen.dll bash packaging/windows/bundle-dlls.sh "$stage_src/jnext.exe" "$stage" \
            || fault "cannot stage platforms/qoffscreen.dll for the scratch copy"
        while IFS= read -r -d '' f; do
            [[ -e "$dir/$f" ]] && continue
            mkdir -p "$dir/$(dirname "$f")"
            cp "$stage/$f" "$dir/$f"
        done < <(cd "$stage" && find . -type f -print0)
        rm -rf "$stage" "$stage_src"
        [[ -f "$dir/platforms/qoffscreen.dll" ]] || fault "the scratch copy has no platforms/qoffscreen.dll after staging"
        (cd "$dir" && sha256sum --check --quiet "$manifest") || fault "a member of the zip changed in the scratch copy"
        [[ "$(sha256sum "$art")" == "$zip_sha  $art" ]] || fault "the zip itself changed"
        run_env=(JNEXT_TARGET_BUILD="$dir" JNEXT_TARGET_SDL_BUILD="$root/build/win-sdl-release"
                 TEST_BIN_DIR="$root/build/sdl-unit-test/test")
        reg_args=(--platform)
        lints=0
        ;;
esac

# --- run ---
if [[ -n "${JNEXT_REGRESSION_PACKAGE_RUNNER:-}" ]]; then
    runner=("$JNEXT_REGRESSION_PACKAGE_RUNNER")
else
    runner=(bash "$root/test/00regression/regression.sh")
fi
log=$scratch/run.log
printf 'regression-package: %s: %d screenshot + %d functional rows; %s\n' \
    "$pkg" "${#shots[@]}" "${#FUNC_ROWS[@]}" "${run_env[*]}"
set +e
env "${run_env[@]}" "${runner[@]}" ${reg_args[@]+"${reg_args[@]}"} "${rows[@]}" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
set -e

line=$(sed -e 's/\x1b\[[0-9;]*m//g' "$log" | sed -n 's/^ *Pass: *\([0-9]*\) *Fail: *\([0-9]*\) *Skip: *\([0-9]*\).*/\1 \2 \3/p' | tail -n1)
[[ -n "$line" ]] || { printf 'regression-package: %s: the run printed no "Pass: N Fail: F Skip: S" line (exit %s)\n' "$pkg" "$rc" >&2; exit 1; }
read -r n f s <<<"$line"
expect=$(( lints + 1 + ${#shots[@]} + ${#FUNC_ROWS[@]} ))
bad=""
[[ "$rc" -eq 0 ]] || bad+=" exit status $rc;"
[[ "$f" -eq 0 ]] || bad+=" $f FAIL;"
[[ "$s" -eq 0 ]] || bad+=" $s SKIP;"
[[ "$n" -eq "$expect" ]] || bad+=" $n passes, expected $expect ($lints lint + 1 sdcard-provision + ${#shots[@]} screenshot + ${#FUNC_ROWS[@]} functional);"
if [[ -n "$bad" ]]; then
    printf 'regression-package: %s FAILED:%s scratch kept in %s\n' "$pkg" "$bad" "$scratch" >&2
    exit 1
fi
printf 'regression-package: %s green: %d rows\n' "$pkg" "$n"
# A Windows scratch copy carries a ~1.7 GB wine prefix; do not pile three up.
[[ "$pkg" != win* ]] || rm -rf "$scratch"
exit 0
