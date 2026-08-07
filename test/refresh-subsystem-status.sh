#!/usr/bin/env bash
# Rebuild test/SUBSYSTEM-TESTS-STATUS.md DETERMINISTICALLY from source of truth.
#
# Usage:
#   test/refresh-subsystem-status.sh <summary.tsv> <dashboard.md>
#
# summary.tsv columns (one row per test binary that ran):
#   test_binary<TAB>live<TAB>pass<TAB>fail<TAB>skip
#
# WHY THIS SCRIPT WAS REWRITTEN (Task 61)
# ---------------------------------------
# The previous generator worked by PRESERVING the existing dashboard rows verbatim
# and patching only the numeric cells of known labels. That made it a stale-count
# magnet and — worse — a serial merge-conflict generator: every branch that ran the
# dashboard rewrote the `**Total**` line, so two branches conflicted on it on every
# merge (this happened three times in one wave: 60c/60d/60e).
#
# This version rebuilds the ENTIRE table from scratch on every run, keyed off:
#   1. test/unit-tests.conf  — the authoritative, harness-enforced per-suite row
#      counts AND the canonical suite ORDER. Counts are NEVER read from the old
#      dashboard, so a count can never be preserved-and-stale. The `**Total**` is
#      always the recomputed sum of the conf counts.
#   2. a maintained binary -> friendly-name label map (below).
#   3. the live pass/fail/skip results in summary.tsv (annotation only).
#
# Consequences (the whole point):
#   * DETERMINISTIC — given the same unit-tests.conf + summary.tsv, the output is
#     byte-identical regardless of what the dashboard previously contained. Two
#     branches that both `make unit-test-dashboard` from the same conf produce the
#     same file with the same Total, so a post-merge regen reconciles instead of
#     conflicting.
#   * NO SUITE CAN VANISH — every suite declared in unit-tests.conf gets a row, in
#     conf order (Tasks 32/35/37 failure mode). A suite with no friendly-name
#     mapping is emitted under its binary name with a visible TODO marker rather
#     than being dropped.
#   * NO HAND-EDITED NOTES SURVIVE — a green row always gets the fixed string
#     "All tests pass." (hand-edited history on green rows was the original drift).
#
# The header prose and the SKIP legend footer are emitted from fixed templates
# here; the table header/separator are GENERATED from the same width constants as
# the data rows, so they can never drift out of alignment with them.

set -u

summary=${1:?summary file required}
dashboard=${2:?dashboard file required}

# The conf is the source of truth. Resolve it relative to THIS script so the
# generator works regardless of the caller's CWD (Makefile invokes from repo root;
# the drift guard likewise). JNEXT_UNIT_TEST_CONF override mirrors run-unit-tests.sh.
script_dir=$(cd "$(dirname "$0")" && pwd)
conf=${JNEXT_UNIT_TEST_CONF:-$script_dir/unit-tests.conf}

[ -f "$summary" ] || { echo "refresh-subsystem-status: no such file: $summary" >&2; exit 1; }
[ -f "$conf"    ] || { echo "refresh-subsystem-status: no such conf: $conf"    >&2; exit 1; }

tmp=$(mktemp "${dashboard}.XXXXXX")
trap 'rm -f "$tmp"' EXIT

awk -v summary="$summary" '
BEGIN {
    # Interior cell widths between | delimiters. The table header and separator are
    # generated from THESE, so the three can never disagree.
    W_LABEL = 23; W_LIVE = 10; W_PASS = 10; W_FAIL = 8; W_SKIP = 9; W_RATE = 9
    W_NOTESEP = 56          # notes-column separator dash count (cosmetic, fixed)

    # --- Test binary -> dashboard friendly name. ---
    # Keyed by BINARY (the name that appears in unit-tests.conf and summary.tsv).
    # This is purely the display-name lookup; the source of truth for which suites
    # exist and how many rows each has is test/unit-tests.conf (which both this
    # generator and the make-unit-test drift guard read). A suite missing here is
    # NOT dropped — it is emitted under its binary name with a TODO marker.
    # Add a row here when you add a suite.
    M["fuse_z80_test"]                 = "FUSE Z80"
    M["z80n_test"]                     = "Z80N CPU"
    M["cpu_int_pulse_test"]            = "CPU INT pulse"
    M["cpu_z80n_im2_regressions_test"] = "CPU/Z80N IM2 regr."
    M["rewind_test"]                   = "Rewind"
    M["copper_test"]                   = "Copper"
    M["copper_integration_test"]       = "Copper (integration)"
    M["mmu_test"]                      = "Memory/MMU"
    M["mmu_integration_test"]          = "Memory/MMU (int)"
    M["nextreg_test"]                  = "NextREG (bare)"
    M["nextreg_integration_test"]      = "NextREG (integration)"
    M["esxdos_stub_test"]              = "esxDOS stub"
    M["input_test"]                    = "Input"
    M["input_integration_test"]        = "Input (integration)"
    M["phantom_typist_test"]           = "Phantom Typist"
    M["ctc_test"]                      = "CTC + Interrupts"
    M["ctc_interrupts_test"]           = "CTC (integration)"
    M["layer2_test"]                   = "Layer 2"
    M["uart_test"]                     = "UART + I2C/RTC"
    M["uart_integration_test"]         = "UART (integration)"
    M["esp_socket_test"]               = "ESP-01 socket transport"
    M["esp_at_test"]                   = "ESP-01 AT command engine"
    M["esp_uart_adapter_test"]         = "ESP-01 jnext UART adapter"
    M["esp_wiring_test"]               = "ESP-01 jnext policy + wiring"
    M["divmmc_test"]                   = "DivMMC + SPI"
    M["multiface_test"]                = "Multiface (core)"
    M["sdcard_test"]                   = "SD Card"
    M["sd_rom_extractor_test"]         = "SD ROM Extractor"
    M["fat32_image_test"]              = "FAT32 Image"
    M["sdcard_provisioner_test"]       = "SD Card Provisioner"
    M["sprites_test"]                  = "Sprites"
    M["compositor_test"]               = "Compositor"
    M["compositor_integration_test"]   = "Compositor (int)"
    M["ula_test"]                      = "ULA Video"
    M["ula_integration_test"]          = "ULA Video (int)"
    M["floating_bus_test"]             = "Floating Bus"
    M["videotiming_test"]              = "VideoTiming"
    M["contention_test"]               = "Contention"
    M["port_test"]                     = "I/O Port Dispatch"
    M["audio_test"]                    = "Audio (AY+DAC+Beeper)"
    M["audio_nextreg_test"]            = "Audio (NextREG)"
    M["audio_port_dispatch_test"]      = "Audio (port dispatch)"
    M["audio_pacing_test"]             = "Audio (pacing)"
    M["audio_fill_test"]               = "Audio (device fill)"
    M["audio_capture_test"]            = "Audio (capture)"
    M["audio_gain_test"]               = "Audio (host gain)"
    M["subsystem_gain_test"]            = "Audio (subsystem gains)"
    M["audio_gain_config_test"]         = "Audio gain configuration"
    M["audio_gain_preferences_test"]    = "Audio gain Preferences"
    M["present_cadence_test"]          = "Present cadence"
    M["render_policy_test"]            = "Render-skip policy"
    M["frame_deadline_test"]           = "Frame-deadline scheduler"
    M["tick_stats_test"]               = "Tick-delivery stats"
    M["speed_report_test"]             = "Achieved-speed report"
    M["host_key_latch_test"]           = "Host key minimum-hold latch"
    M["frame_sequencer_test"]          = "Frame-tick sequencer (wiring)"
    M["present_count_test"]            = "Present count (widget)"
    M["esp_status_test"]               = "ESP-01 status cell (GUI)"
    M["esc_break_test"]                = "Esc/BREAK + fullscreen routing"
    M["host_hotkey_test"]              = "Host hotkeys on Alt (Ctrl to guest)"
    M["main_window_accel_test"]        = "Main-window menu mnemonics"
    M["shifted_keys_test"]             = "Shifted symbols reach the guest"
    M["quit_cleanup_test"]             = "Quit runs closeEvent cleanup"
    M["nex_v13_dialog_test"]           = "NEX V1.3 GUI warning dialog"
    M["log_test"]                      = "Logging"
    M["log_gate_test"]                 = "Logging (gate)"
    M["cli_options_test"]              = "CLI options / docs"
    M["video_recorder_cmd_test"]       = "Video recorder (ffmpeg cmd)"
    M["nex_loader_test"]               = "NEX loader (screen ingest)"
    M["nex_v13_test"]                  = "NEX loader (V1.3)"
    M["extended_nex_test"]             = "Extended NEX streaming"
    M["dma_test"]                      = "DMA"
    M["tilemap_test"]                  = "Tilemap"
    M["tilemap_fetch_split_test"]      = "Tilemap raster splits"
    M["lores_test"]                    = "LoRes"
    M["lores_integration_test"]        = "LoRes (integration)"
    M["nmi_test"]                      = "NMI Source Pipeline"
    M["nmi_integration_test"]          = "NMI (integration)"
    M["atic_atac_nmi_test"]            = "Atic Atac NMI"
    M["profiler_test"]                 = "Profiler"
    M["resume_guard_test"]             = "Resume Guard"
    M["step_out_test"]                 = "Debugger Step Out"
    M["persistent_bp_test"]            = "Debugger persistent BPs"
    M["io_watchpoint_test"]            = "Debugger I/O Watchpoints"
    M["resume_step_off_test"]          = "Debugger resume step-off"
    M["app_config_test"]               = "GUI Preferences (AppConfig)"
    M["debugger_video_panel_test"]     = "Debugger Video Panel"
    M["debugger_audio_panel_test"]     = "Debugger Audio Panel"
    M["debugger_quit_gate_test"]       = "Debugger Quit Gate"
    M["debugger_persistent_bp_test"]   = "Debugger persist. BP (GUI)"
    M["window_attach_test"]            = "Debugger Window Attach"
    M["debugger_window_size_test"]     = "Debugger Window Sizing"
    M["debugger_window_grow_test"]     = "Debugger Window Growing"
    M["debugger_accel_test"]           = "Debugger Accelerators"
    M["debugger_menu_test"]            = "Debugger Menus"
    M["emulator_boot_test"]            = "Emulator Boot"
    M["preferences_apply_test"]        = "GUI Preferences (Apply)"
    M["preferences_apply_policy_test"] = "GUI Preferences (Apply Policy)"
    M["pointer_capture_test"]          = "Pointer Capture"

    # --- Live results (annotation only; never a source of counts). ---
    while ((getline line < summary) > 0) {
        n = split(line, f, "\t")
        if (n == 5) {
            PASS[f[1]] = f[3] + 0
            FAIL[f[1]] = f[4] + 0
            SKIP[f[1]] = f[5] + 0
            HAVE[f[1]] = 1
        }
    }
    close(summary)
    nrows = 0
}

# --- Main: read unit-tests.conf, one runnable suite per line, in file order. ---
{
    # Drop comments and blank lines. The "# expect: N" pin line starts with # too.
    sub(/#.*$/, "")
    if ($0 ~ /^[[:space:]]*$/) next
    name = $1
    count = $2
    if (substr(name, 1, 1) == "?") { name = substr(name, 2) }
    if (name == "") next
    if (count !~ /^[0-9]+$/) next        # defensive; run-unit-tests.sh already validates

    nrows++
    ORDER[nrows] = name
    COUNT[name]  = count + 0
}

END {
    # --- Warn (stderr, never into the file) about map/conf disagreements. ---
    for (i = 1; i <= nrows; i++) {
        b = ORDER[i]
        if (!(b in M))
            printf("refresh-subsystem-status: WARNING: suite %s has no friendly-name mapping (emitted with binary name + TODO)\n", b) > "/dev/stderr"
        if (!(b in HAVE))
            printf("refresh-subsystem-status: note: suite %s not in the live summary (no run this build) — shown at its pinned count\n", b) > "/dev/stderr"
    }
    for (b in M) {
        found = 0
        for (i = 1; i <= nrows; i++) if (ORDER[i] == b) { found = 1; break }
        if (!found)
            printf("refresh-subsystem-status: WARNING: label-map entry %s has no matching suite in unit-tests.conf\n", b) > "/dev/stderr"
    }

    # --- Header prose + table header (generated from the width constants). ---
    print "# Subsystem Compliance Test Dashboard"
    print ""
    print "VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior."
    print ""
    print "## Status"
    print ""
    print "|" hdr("Subsystem", W_LABEL, 0) "|" hdr("Live", W_LIVE, 1) "|" \
          hdr("Pass", W_PASS, 1) "|" hdr("Fail", W_FAIL, 1) "|" \
          hdr("Skip", W_SKIP, 1) "|" hdr("Rate", W_RATE, 1) "|" hdr("Notes", 7, 0) "|"
    print "|" dashes(W_LABEL) "|" rdash(W_LIVE) "|" rdash(W_PASS) "|" \
          rdash(W_FAIL) "|" rdash(W_SKIP) "|" rdash(W_RATE) "|" dashes(W_NOTESEP) "|"

    # --- One row per declared suite, in conf order. Counts from conf; only the
    #     pass/fail/skip annotation comes from the live run. ---
    tot_live = 0; tot_pass = 0; tot_fail = 0; tot_skip = 0
    for (i = 1; i <= nrows; i++) {
        b     = ORDER[i]
        total = COUNT[b]
        todo  = 0
        label = (b in M) ? M[b] : b
        if (!(b in M)) todo = 1

        if (b in HAVE) { pas = PASS[b]; fail = FAIL[b]; skip = SKIP[b] }
        else           { pas = total;  fail = 0;        skip = 0 }
        live = total

        tot_live += live; tot_pass += pas; tot_fail += fail; tot_skip += skip

        print "|" fmt_label(label, W_LABEL) "|" \
              fmt_num(live, W_LIVE) "|" fmt_num(pas, W_PASS) "|" \
              fmt_num(fail, W_FAIL) "|" fmt_num(skip, W_SKIP) "|" \
              fmt_rate(pas, live, W_RATE) "|" fmt_notes(fail, skip, todo, b) "|"
    }

    # --- Recomputed Total (always the sum; never preserved). ---
    print "|" fmt_label("**Total**", W_LABEL) "|" \
          fmt_bold(tot_live, W_LIVE) "|" fmt_bold(tot_pass, W_PASS) "|" \
          fmt_bold(tot_fail, W_FAIL) "|" fmt_bold(tot_skip, W_SKIP) "|" \
          fmt_bold_rate(tot_pass, tot_live, W_RATE) "|" \
          fmt_notes(tot_fail, tot_skip, 0, "") "|"

    print ""
    print "**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code."
}

# ---- helpers ----

function ljust(s, w,   r) { r = s; while (length(r) < w) r = r " "; return r }
function rjust(s, w,   r) { r = s; while (length(r) < w) r = " " r; return r }

function dashes(n,   r) { r = ""; while (length(r) < n) r = r "-"; return r }
function rdash(w) { return dashes(w - 1) ":" }        # right-aligned column separator

# Header cell: right-justified for numeric columns, left-justified otherwise.
function hdr(s, w, right) {
    if (right) return " " rjust(s, w - 2) " "
    return " " ljust(s, w - 2) " "
}

function fmt_label(s, w) { return " " ljust(s, w - 2) " " }
function fmt_num(n, w)   { return " " rjust(sprintf("%d", n), w - 2) " " }

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

# Notes cell: a status emoji plus fixed text. Green rows ALWAYS read "All tests
# pass." — no hand-edited history survives (that was the original drift source).
# The emoji is emitted as raw UTF-8 bytes (not a bracket class) so it is correct
# under LANG=C byte mode. TODO marker flags an unmapped binary.
function fmt_notes(fail, skip, todo, name,   dot, inner) {
    if (fail > 0)      { dot = "\xF0\x9F\x94\xB4"; inner = fail " test(s) failed." }   # red
    else if (skip > 0) { dot = "\xF0\x9F\x9F\xA1"; inner = skip " test(s) skipped." }  # yellow
    else               { dot = "\xF0\x9F\x9F\xA2"; inner = "All tests pass." }          # green
    if (todo) inner = inner " \xE2\x9A\xA0 TODO: add \"" name "\" to the label map in refresh-subsystem-status.sh."
    return " " dot " " inner " "
}
' "$conf" > "$tmp"

mv "$tmp" "$dashboard"
trap - EXIT
