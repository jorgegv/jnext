#!/usr/bin/env bash
# Regression-test ONE package's own binary (GH #487): unpack the artifact the
# package-<p> target just built and run the regression harness against the
# jnext inside it, never against a dev build.
#
# Usage: bash test/regression-package.sh [--clear] <rpm|deb|win|win-qt5|win32-qt5>
#        (`make regression-package PACKAGE=<p>` clears, builds, then runs.)
#        --clear only deletes the package's earlier artifacts (every version's, so the
#        build that follows leaves exactly one and the selection below cannot be ambiguous).
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
#                   member, add Qt's offscreen platform plugin to the scratch COPY only
#                   (and only what that plugin alone needs: bundle-dlls.sh stages twice, with
#                   and without it, and the DIFFERENCE is copied, so a file the zip lacks is
#                   never put back from the MinGW sysroot), never clobbering a member and never
#                   touching the zip, then run `regression.sh --platform` with the copy as the
#                   Qt tree (its target OS is read from jnext.exe's PE header: there is no
#                   CMakeCache.txt in a package; its wine environment has no MinGW sysroot).
#
# The run is green only if the harness exits 0, reports Fail 0 and Skip 0 and its
# total is exactly lints + 1 sdcard-provision + screenshots + functional rows (a
# filtered run skips regression.sh's own completeness check, so this is it).
#
# Env: JNEXT_REGRESSION_CONF overrides the screenshot manifest, like regression.sh.
#      JNEXT_REGRESSION_PACKAGE_RUNNER (TEST-ONLY, like the wine watchdog overrides
#      in regression.sh) is an executable run INSTEAD of
#      `bash test/00regression/regression.sh`, so a stub can record the arguments
#      and environment it is given. JNEXT_REGRESSION_PACKAGE_ARTIFACT (TEST-ONLY) names the
#      artifact file to unpack instead of the tree's current one (a fixture zip).
#      Exit: 0 green, 1 the run failed, 2 harness fault.
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

clear_only=false
if [[ "${1:-}" == --clear ]]; then clear_only=true; shift; fi
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

if $clear_only; then
    clear_artifacts "$tree" "$glob"
    exit 0
fi

if [[ -n "${JNEXT_REGRESSION_PACKAGE_ARTIFACT:-}" ]]; then
    art=$JNEXT_REGRESSION_PACKAGE_ARTIFACT
    [[ -f "$art" ]] || fault "JNEXT_REGRESSION_PACKAGE_ARTIFACT is not a file: $art"
else
    art=$(pick_artifact "$tree" "$glob" "$want") \
        || fault "cannot select the current artifact for '$pkg' in $tree (expected $want; pick_artifact's reason is above). Build it with make package-$pkg; make regression-package clears earlier versions first."
    art=$root/$art
fi

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
        # The plugin must be the ONLY thing added. bundle-dlls.sh stages a whole bundle (the
        # exe's DLL closure, every plugin, qt.conf, licences), so adding "whatever is absent"
        # from one staging would put back from the MinGW sysroot any file a faulty package
        # recipe left out of the zip, and the run would pass. So it stages TWICE into empty
        # dirs, with and without the plugin, and only the difference (the plugin and the DLLs
        # that only it needs) is copied; a file the zip lacks stays missing.
        # bundle-dlls.sh looks for libspdlog.dll under <exe dir>/third_party/spdlog, as in
        # a build tree, so the exe is staged from a dir laid out that way with the zip's own copy.
        stage_src=$scratch/stage-src
        stage_plain=$scratch/stage-plain
        stage_plug=$scratch/stage-plugin
        mkdir -p "$stage_plain" "$stage_plug" "$stage_src/third_party/spdlog"
        cp "$dir/jnext.exe" "$stage_src/"
        for f in "$dir"/libspdlog*.dll; do [[ ! -f "$f" ]] || cp "$f" "$stage_src/third_party/spdlog/"; done
        env -u BUNDLE_EXTRA_QT_PLUGINS bash packaging/windows/bundle-dlls.sh "$stage_src/jnext.exe" "$stage_plain" \
            || fault "cannot stage the plain bundle of the zip's jnext.exe"
        BUNDLE_EXTRA_QT_PLUGINS=platforms/qoffscreen.dll bash packaging/windows/bundle-dlls.sh "$stage_src/jnext.exe" "$stage_plug" \
            || fault "cannot stage platforms/qoffscreen.dll for the scratch copy"
        added=0
        while IFS= read -r -d '' f; do
            [[ ! -e "$stage_plain/$f" ]] || continue
            case "$f" in
                ./platforms/*.dll|./*.dll) ;;
                *) fault "the plugin's difference holds something that is not a DLL: $f" ;;
            esac
            added=$(( added + 1 ))
            [[ -e "$dir/$f" ]] && continue
            mkdir -p "$dir/$(dirname "$f")"
            cp "$stage_plug/$f" "$dir/$f"
        done < <(cd "$stage_plug" && find . -type f -print0)
        [[ "$added" -gt 0 ]] || fault "staging with the plugin added nothing over the plain bundle"
        rm -rf "$stage_src" "$stage_plain" "$stage_plug"
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
