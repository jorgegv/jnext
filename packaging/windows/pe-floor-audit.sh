#!/usr/bin/env bash
#
# pe-floor-audit.sh <pe-file>...
#
# Windows OS-floor audit of PE binaries (GH #108). For each file, prints the
# PE header OS/subsystem versions and every imported symbol/DLL known to be
# unavailable on Windows 7 SP1 (the audit baseline), tiered WIN8 / WIN8.1 /
# WIN10. Only the STATIC import table is inspected: GetProcAddress-resolved
# APIs (e.g. SDL3's DPI/D3D probing) do not raise a load-time floor and are
# deliberately out of scope.
#
# The API lists are curated, not exhaustive: an unflagged binary means "none
# of the known post-Win7 imports", not a proof of Win7 compatibility. Grow the
# lists when a new post-Win7 API shows up in a bundled DLL.
#
# Evidence base + audited results: doc/design/WINDOWS-COMPAT-PLAN.md.
#
set -uo pipefail

OBJDUMP=${MINGW_OBJDUMP:-x86_64-w64-mingw32-objdump}
command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "error: $OBJDUMP not found" >&2; exit 2; }
[ "$#" -ge 1 ] || { echo "usage: $0 <pe-file>..." >&2; exit 2; }

# Function-name patterns (full-match against the imported symbol).
WIN8='WaitOnAddress|WakeByAddressSingle|WakeByAddressAll|GetSystemTimePreciseAsFileTime|CreateFile2|CopyFile2|GetOverlappedResultEx|PrefetchVirtualMemory|PathCch.*|RoInitialize|RoGetActivationFactory|WindowsCreateString.*|SetProcessMitigationPolicy'
WIN81='GetDpiForMonitor|SetProcessDpiAwareness'
WIN10='SetThreadDescription|GetThreadDescription|GetDpiForWindow|GetDpiForSystem|GetSystemMetricsForDpi|AdjustWindowRectExForDpi|SystemParametersInfoForDpi|EnableNonClientDpiScaling|SetProcessDpiAwarenessContext|SetThreadDpiAwarenessContext|GetWindowDpiAwarenessContext|GetThreadDpiAwarenessContext|AreDpiAwarenessContextsEqual|GetAwarenessFromDpiAwarenessContext|GetDpiFromDpiAwarenessContext|VirtualAlloc2|MapViewOfFile3|CreatePseudoConsole|DiscardVirtualMemory'
# DLLs that do not exist (or do not resolve these imports) on Windows 7 SP1.
NOT_ON_WIN7_DLLS='^(d3d12|shcore|combase|dcomp|windows[.].*|api-ms-win-core-synch-l1-2-.*|api-ms-win-core-path-.*|api-ms-win-core-winrt-.*|api-ms-win-crt-.*)[.]dll$'

rc=0
for f in "$@"; do
    echo "### $(basename "$f")"
    "$OBJDUMP" -x "$f" 2>/dev/null \
        | grep -E "Major(OSystem|Subsystem)Version" | tr -s ' \t' ' ' | tr '\n' ';'
    echo
    out=$("$OBJDUMP" -p "$f" 2>/dev/null | awk \
        -v w8="$WIN8" -v w81="$WIN81" -v w10="$WIN10" -v bad="$NOT_ON_WIN7_DLLS" '
        /DLL Name:/ { dll=tolower($3);
                      if (dll ~ bad) printf "  DLL-NOT-ON-WIN7: %s\n", dll; next }
        /^\t[0-9a-f]+/ { fn=$4; if (fn=="") next;
            if      (fn ~ "^("w10")$") printf "  WIN10: %s <- %s\n", fn, dll;
            else if (fn ~ "^("w81")$") printf "  WIN8.1: %s <- %s\n", fn, dll;
            else if (fn ~ "^("w8")$")  printf "  WIN8: %s <- %s\n", fn, dll; }')
    if [ -n "$out" ]; then echo "$out"; rc=1; fi
done
exit $rc
