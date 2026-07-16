#!/usr/bin/env bash
#
# bundle-dlls.sh <jnext.exe> <dest-dir>
#
# Copy every runtime DLL and Qt plugin that the MinGW-cross-built jnext.exe
# needs to run on a stock Windows machine, next to the executable in <dest-dir>.
# Without this, jnext.exe on a clean Windows box fails to start (missing
# Qt6Core.dll, and — even with the DLLs present — "no Qt platform plugin could
# be initialized" when platforms/qwindows.dll is absent).
#
# What it copies:
#   - the transitive closure of non-system DLL imports of jnext.exe and of the
#     shipped Qt plugins, resolved from the Fedora MinGW sysroot bin directory
#     (flat, next to the exe)
#   - libspdlog.dll — jnext's own shared build; it lives in the build tree, not
#     in the sysroot, so it is found relative to the exe
#   - Qt plugins, in the subdirectories where Qt looks for them:
#       platforms/qwindows.dll            (mandatory — no GUI without it)
#       styles/qmodernwindowsstyle.dll    (native Windows look)
#       imageformats/{qgif,qico,qjpeg}.dll
#   - qt.conf, so Qt resolves the plugin subdirs from the exe directory
#
# DLLs that Windows itself provides are never bundled (see SYS_DLL_RE).
#
# Env overrides: MINGW_SYSROOT, MINGW_OBJDUMP.
#
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <jnext.exe> <dest-dir>" >&2
    exit 2
fi

EXE=$1
DEST=$2
SYSROOT=${MINGW_SYSROOT:-/usr/x86_64-w64-mingw32/sys-root/mingw}
OBJDUMP=${MINGW_OBJDUMP:-x86_64-w64-mingw32-objdump}
BIN="$SYSROOT/bin"
QT_PLUGINS="$SYSROOT/lib/qt6/plugins"

[ -f "$EXE" ]     || { echo "error: exe not found: $EXE" >&2; exit 1; }
[ -d "$BIN" ]     || { echo "error: MinGW sysroot bin not found: $BIN" >&2; exit 1; }
command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "error: $OBJDUMP not found" >&2; exit 1; }

# DLLs supplied by Windows itself — resolving/bundling these is wrong.
SYS_DLL_RE='^(kernel32|user32|gdi32|shell32|shcore|advapi32|ole32|oleaut32|msvcrt|ws2_32|comdlg32|comctl32|winmm|imm32|setupapi|version|winspool|shlwapi|crypt32|dwmapi|uxtheme|rpcrt4|iphlpapi|netapi32|userenv|wtsapi32|dnsapi|secur32|bcrypt|ncrypt|authz|d3d9|d3d11|d3d12|dxgi|dwrite|opengl32|glu32|mpr|normaliz|wldap32|ntdll|api-ms-win.*)\.dll$'

# Qt plugins to ship, as paths relative to the plugin root. qwindows is the
# only mandatory one; the rest are graceful (warn, don't fail, if absent).
QT_PLUGIN_LIST="platforms/qwindows.dll styles/qmodernwindowsstyle.dll imageformats/qgif.dll imageformats/qico.dll imageformats/qjpeg.dll"

mkdir -p "$DEST"

# --- direct DLL imports of a PE file (lowercased basenames) -----------------
imports() { "$OBJDUMP" -p "$1" 2>/dev/null | awk '/DLL Name:/{print tolower($3)}'; }

# --- transitive closure resolver --------------------------------------------
# QUEUE is a bash array of DLL basenames to resolve; SEEN tracks processed ones.
# resolve_queue() copies each resolvable non-system DLL flat into $DEST and
# enqueues its own imports, until the queue drains.
declare -A SEEN
QUEUE=()
UNRESOLVED=0   # set if any non-system, non-sysroot DLL import is left unsatisfied

resolve_queue() {
    while [ "${#QUEUE[@]}" -gt 0 ]; do
        local d="${QUEUE[0]}"
        QUEUE=("${QUEUE[@]:1}")
        [ -z "$d" ] && continue
        [ -n "${SEEN[$d]:-}" ] && continue
        SEEN[$d]=1
        [[ "$d" =~ $SYS_DLL_RE ]] && continue
        local f
        f=$(find "$BIN" -maxdepth 1 -iname "$d" | head -n1)
        if [ -z "$f" ]; then
            # A required (non-delay) import that is neither a Windows system DLL
            # (SYS_DLL_RE) nor present in the sysroot would leave the bundle
            # unable to start — the exact failure this script exists to prevent.
            # Fail loud rather than ship a silently-broken zip. If a legitimately
            # new Windows system DLL surfaces here, add it to SYS_DLL_RE.
            echo "error: unresolved DLL (not in sysroot, not a known system DLL): $d" >&2
            UNRESOLVED=1
            continue
        fi
        cp -f "$f" "$DEST/"
        local sub
        for sub in $(imports "$f"); do QUEUE+=("$sub"); done
    done
}

# --- jnext's own shared spdlog build (lives in the build tree) --------------
# Copied and marked SEEN before resolving the exe's imports: the exe imports
# libspdlog.dll, which is NOT in the sysroot (it is a jnext build output), so
# resolving it via the sysroot would spuriously warn.
build_root=$(dirname "$EXE")
spdlog=$(find "$build_root" -iname 'libspdlog*.dll' | head -n1)
if [ -n "$spdlog" ]; then
    cp -f "$spdlog" "$DEST/"
    SEEN["$(basename "$spdlog" | tr '[:upper:]' '[:lower:]')"]=1
    for sub in $(imports "$spdlog"); do QUEUE+=("$sub"); done
else
    echo "warning: libspdlog.dll not found under $build_root — jnext.exe imports it" >&2
fi

# seed with the exe's own imports, then resolve
for d in $(imports "$EXE"); do QUEUE+=("$d"); done
resolve_queue

# --- Qt plugins (+ their own DLL closure) -----------------------------------
for rel in $QT_PLUGIN_LIST; do
    src="$QT_PLUGINS/$rel"
    if [ ! -f "$src" ]; then
        if [ "$rel" = "platforms/qwindows.dll" ]; then
            echo "error: mandatory Qt platform plugin missing: $src" >&2
            exit 1
        fi
        echo "warning: optional Qt plugin missing: $rel" >&2
        continue
    fi
    mkdir -p "$DEST/$(dirname "$rel")"
    cp -f "$src" "$DEST/$rel"
    # a plugin may drag in extra libs (e.g. qjpeg -> libjpeg); resolve them too
    for sub in $(imports "$src"); do QUEUE+=("$sub"); done
done
resolve_queue

# --- ensure the exe itself is in the bundle ---------------------------------
if [ "$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")" != "$(cd "$DEST" && pwd)/$(basename "$EXE")" ]; then
    cp -f "$EXE" "$DEST/"
fi

# --- qt.conf: let Qt find the plugin subdirs from the exe directory ---------
printf '[Paths]\nPlugins=.\n' > "$DEST/qt.conf"

# Fail loud if any required non-system DLL could not be resolved — a partially
# bundled zip would fail to start on Windows just like the bare exe does.
if [ "$UNRESOLVED" -ne 0 ]; then
    echo "error: bundle is incomplete (unresolved DLLs above) — not shipping it" >&2
    exit 1
fi

# Count DLLs only — $DEST may be the build dir itself (in-place bundling), whose
# total file count includes the whole CMake tree and would be meaningless here.
n=$(find "$DEST" -iname '*.dll' | wc -l)
echo "bundled $n DLLs (+ Qt plugins + qt.conf) into $DEST"
