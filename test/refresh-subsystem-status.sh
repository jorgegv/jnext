#!/usr/bin/env bash
# Refresh numeric cells in test/SUBSYSTEM-TESTS-STATUS.md from unit-test results.
#
# Usage:
#   test/refresh-subsystem-status.sh <summary.tsv> <dashboard.md>
#
# summary.tsv columns (one row per test binary that ran):
#   test_binary<TAB>live<TAB>pass<TAB>fail<TAB>skip
#
# Rows whose label is in the known label->binary map are rewritten from the
# TSV. Rows not present in the TSV keep their current numbers (partial-run
# safe). The **Total** row is recomputed from the resulting row values.
# Label and Notes cells are preserved verbatim.

set -u

summary=${1:?summary file required}
dashboard=${2:?dashboard file required}

[ -f "$summary"   ] || { echo "refresh-subsystem-status: no such file: $summary"   >&2; exit 1; }
[ -f "$dashboard" ] || { echo "refresh-subsystem-status: no such file: $dashboard" >&2; exit 1; }

tmp=$(mktemp "${dashboard}.XXXXXX")
trap 'rm -f "$tmp"' EXIT

awk -v summary="$summary" '
BEGIN {
    # Interior cell widths between | delimiters, derived from the separator row.
    W_LIVE = 10; W_PASS = 10; W_FAIL = 8; W_SKIP = 9; W_RATE = 9

    # Dashboard label -> test binary name.
    L["FUSE Z80"]              = "fuse_z80_test"
    L["Z80N CPU"]              = "z80n_test"
    L["Rewind"]                = "rewind_test"
    L["Copper"]                = "copper_test"
    L["Copper (integration)"]  = "copper_integration_test"
    L["Memory/MMU"]            = "mmu_test"
    L["Memory/MMU (int)"]      = "mmu_integration_test"
    L["NextREG (bare)"]        = "nextreg_test"
    L["NextREG (integration)"] = "nextreg_integration_test"
    L["Input"]                 = "input_test"
    L["Input (integration)"]   = "input_integration_test"
    L["CTC + Interrupts"]      = "ctc_test"
    L["CTC (integration)"]     = "ctc_interrupts_test"
    L["Layer 2"]               = "layer2_test"
    L["UART + I2C/RTC"]        = "uart_test"
    L["UART (integration)"]    = "uart_integration_test"
    L["DivMMC + SPI"]          = "divmmc_test"
    L["Multiface (core)"]      = "multiface_test"
    L["SD Card"]               = "sdcard_test"
    L["Sprites"]               = "sprites_test"
    L["Compositor"]            = "compositor_test"
    L["Compositor (int)"]      = "compositor_integration_test"
    L["ULA Video"]             = "ula_test"
    L["ULA Video (int)"]       = "ula_integration_test"
    L["Floating Bus"]          = "floating_bus_test"
    L["VideoTiming"]           = "videotiming_test"
    L["Contention"]            = "contention_test"
    L["I/O Port Dispatch"]     = "port_test"
    L["Audio (AY+DAC+Beeper)"] = "audio_test"
    L["Audio (NextREG)"]       = "audio_nextreg_test"
    L["Audio (port dispatch)"] = "audio_port_dispatch_test"
    L["DMA"]                   = "dma_test"
    L["Tilemap"]               = "tilemap_test"
    L["NMI Source Pipeline"]   = "nmi_test"
    L["NMI (integration)"]     = "nmi_integration_test"
    L["Debugger Video Panel"]  = "debugger_video_panel_test"
    L["Debugger Audio Panel"]  = "debugger_audio_panel_test"
    L["CPU INT pulse"]         = "cpu_int_pulse_test"
    L["CPU/Z80N IM2 regr."]    = "cpu_z80n_im2_regressions_test"
    L["Phantom Typist"]        = "phantom_typist_test"
    L["SD ROM Extractor"]      = "sd_rom_extractor_test"
    L["FAT32 Image"]           = "fat32_image_test"
    L["SD Card Provisioner"]   = "sdcard_provisioner_test"
    L["Audio (pacing)"]        = "audio_pacing_test"
    L["Logging"]               = "log_test"
    L["Logging (gate)"]        = "log_gate_test"
    L["Profiler"]              = "profiler_test"
    L["Resume Guard"]          = "resume_guard_test"

    while ((getline line < summary) > 0) {
        n = split(line, f, "\t")
        if (n == 5) {
            LIVE[f[1]] = f[2] + 0
            PASS[f[1]] = f[3] + 0
            FAIL[f[1]] = f[4] + 0
            SKIP[f[1]] = f[5] + 0
            HAVE[f[1]] = 1
        }
    }
    close(summary)
}

{ lines[NR] = $0 }

END {
    tot_live = 0; tot_pass = 0; tot_fail = 0; tot_skip = 0
    total_idx = 0

    for (i = 1; i <= NR; i++) {
        line = lines[i]
        if (line !~ /^\|[^|]*\|[^|]*\|[^|]*\|[^|]*\|[^|]*\|[^|]*\|[^|]*\|/) continue
        # Skip the markdown separator row (|---...|---...|).
        if (line ~ /^\|-/) continue

        m = split(line, c, "|")
        if (m < 9) continue

        label_cell = c[2]; live_cell = c[3]; pass_cell = c[4]
        fail_cell  = c[5]; skip_cell = c[6]; rate_cell = c[7]
        notes_cell = c[8]

        lab = label_cell
        gsub(/^ +| +$/, "", lab)

        if (lab == "Subsystem") continue

        if (lab ~ /^\*\*Total\*\*/) {
            total_idx = i
            total_label = label_cell
            total_notes = notes_cell
            continue
        }

        # Resolve numbers: known+have-data -> TSV; otherwise read current cells.
        if ((lab in L) && (L[lab] in HAVE)) {
            live = LIVE[L[lab]]; pas = PASS[L[lab]]
            fail = FAIL[L[lab]]; skip = SKIP[L[lab]]
        } else {
            live = extract_num(live_cell); pas = extract_num(pass_cell)
            fail = extract_num(fail_cell); skip = extract_num(skip_cell)
        }

        tot_live += live; tot_pass += pas
        tot_fail += fail; tot_skip += skip

        # Only rewrite rows whose label is in the map. Unknown rows contribute
        # to totals (via extract_num above) but are preserved verbatim, so the
        # dashboard can be extended without the script losing the Total count.
        if (!(lab in L)) continue

        lines[i] = "|" label_cell "|" \
                   fmt_num(live, W_LIVE) "|" \
                   fmt_num(pas,  W_PASS) "|" \
                   fmt_num(fail, W_FAIL) "|" \
                   fmt_num(skip, W_SKIP) "|" \
                   fmt_rate(pas, live, W_RATE) "|" \
                   fmt_notes(notes_cell, fail, skip) "|"
    }

    if (total_idx > 0) {
        lines[total_idx] = "|" total_label "|" \
                           fmt_bold(tot_live, W_LIVE) "|" \
                           fmt_bold(tot_pass, W_PASS) "|" \
                           fmt_bold(tot_fail, W_FAIL) "|" \
                           fmt_bold(tot_skip, W_SKIP) "|" \
                           fmt_bold_rate(tot_pass, tot_live, W_RATE) "|" \
                           fmt_notes(total_notes, tot_fail, tot_skip) "|"
    }

    for (i = 1; i <= NR; i++) print lines[i]
}

function extract_num(s,   r) {
    r = s
    gsub(/[^0-9]/, "", r)
    if (r == "") return 0
    return r + 0
}

function rjust(s, w,   r) {
    r = s
    while (length(r) < w) r = " " r
    return r
}

function fmt_num(n, w) {
    return " " rjust(sprintf("%d", n), w - 2) " "
}

function fmt_rate(pas, live, w,   r, s) {
    r = (live > 0) ? int(pas * 100 / live) : 0
    s = sprintf("%d%%", r)
    return " " rjust(s, w - 2) " "
}

function fmt_bold(n, w,   s) {
    s = "**" n "**"
    if (length(s) + 2 <= w) return " " rjust(s, w - 2) " "
    if (length(s) + 1 <= w) return " " rjust(s, w - 1)
    return rjust(s, w)
}

function fmt_bold_rate(pas, live, w,   r, s) {
    r = (live > 0) ? int(pas * 100 / live) : 0
    s = "**" r "%**"
    if (length(s) + 2 <= w) return " " rjust(s, w - 2) " "
    if (length(s) + 1 <= w) return " " rjust(s, w - 1)
    return rjust(s, w)
}

# Prefix the notes cell with a status emoji so the row state shows up
# in GitHub-rendered markdown (where inline <span style="..."> is
# stripped). Strips any prior emoji or <span> wrap before prefixing so
# re-runs are idempotent.
#  - red   if any FAILs
#  - yellow if any SKIPs (no FAILs)
#  - green otherwise
# A fully green row (0 FAIL, 0 SKIP) gets the fixed text "All tests
# pass." — any historical detail in the Notes cell is dropped, so notes
# written while a suite still had skips cannot outlive the skips
# (2026-07-14: five rows carried task history from long-closed skips).
function fmt_notes(cell, fail, skip,   inner, dot) {
    inner = cell
    # Migrate from earlier <span style=...>...</span> wrap if present.
    sub(/^[[:space:]]*<span[^>]*>/, "", inner)
    sub(/<\/span>[[:space:]]*$/, "", inner)
    # Strip a previous emoji prefix (red/yellow/green circles) so we
    # do not stack them on re-runs. Use ALTERNATION of the full UTF-8
    # sequences, NOT a bracket class: under LANG=C (byte mode) a
    # bracketed multi-byte class matches a SINGLE byte, stripping only
    # the emoji lead byte only, leaving orphan continuation bytes
    # (the historical replacement-char mojibake in this dashboard).
    sub(/^[[:space:]]*(🔴|🟡|🟢)[[:space:]]*/, "", inner)
    # Sweep any U+FFFD replacement chars left by the old buggy strip.
    gsub(/\357\277\275/, "", inner)
    # Trim leading and trailing whitespace inside the cell.
    sub(/^[[:space:]]+/, "", inner)
    sub(/[[:space:]]+$/, "", inner)
    if (fail > 0)      dot = "\xF0\x9F\x94\xB4"  # 🔴
    else if (skip > 0) dot = "\xF0\x9F\x9F\xA1"  # 🟡
    else             { dot = "\xF0\x9F\x9F\xA2"; inner = "All tests pass." }  # 🟢
    return " " dot " " inner " "
}
' "$dashboard" > "$tmp"

mv "$tmp" "$dashboard"
trap - EXIT
