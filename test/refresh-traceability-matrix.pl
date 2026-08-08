#!/usr/bin/env perl
# Refresh per-row Status, VHDL file:line and Test file:line columns in
# `doc/testing/TRACEABILITY-MATRIX.md` for the 15 non-Z80N subsystems.
#
# Strategy (format-agnostic across test harnesses):
#
#   1. For each test source file, grep for `check("ID", ...)` and `skip("ID",
#      ...)` first-arg string literals. These are the ground truth for which
#      plan row IDs the test file exercises and whether as a live check or
#      an honest skip.
#   2. For each test binary, run it and collect only the **FAIL** set — this
#      is the one output line ("  FAIL ID: ...") that every harness agrees
#      on.
#   3. Derive per-ID status:
#        fail    if ID is in the binary's FAIL set
#        skip    if ID is a `skip()` call in source AND not in FAIL set
#        pass    if ID is a `check()` call in source AND not in FAIL set
#        missing if ID is not found in source at all
#      "source" here is the section's own file(s) FIRST, then — only for IDs
#      they do not mention — the other suites of the same `##` subsystem,
#      which is the unit recording has used since GH #117/#118. Rows are
#      routinely listed in a parent's table and asserted in its `###`
#      companion suite; scanning the entry's own file alone published those
#      as `missing` while the assertion passed. (GH #121)
#   4. Recover each row's VHDL citation from row-local evidence (see the
#      "VHDL citation extraction" block below).
#   5. Edit the matrix in place: for each data row whose first cell is a
#      test ID, rewrite the Status cell, the VHDL file:line cell and the
#      Test file:line cell preserving column widths. Section boundaries are
#      matched by exact header line.
#   6. Protected rows: a data row carrying an explicit `<!-- protected -->`
#      HTML comment after its closing `|` is hand-maintained (typically a
#      cross-file pointer to a suite outside the section's source file,
#      e.g. NR-C0-02 -> test/nmi/atic_atac_nmi_test.cpp) and is left
#      byte-identical; its existing Status cell is trusted for the report
#      counts. The marker is explicit and local to the row it protects —
#      cross-file *scanning* is deliberately NOT done, for the same reason
#      the banner/nearest-comment citation tiers were rejected: it infers,
#      and a plausible-but-wrong row is worse than an honest `missing`.
#      (GH #105)
#   7. Report the OTHER direction too: row IDs the test source asserts that
#      no row of the owning subsystem's section records (per section, not
#      globally, since GH #118 — an ID string reused by another subsystem
#      must not vouch for this one). Until GH #117 this script
#      looked only at matrix rows, so it could report `missing` (a matrix
#      row with no test) but never `unrecorded` (a test with no matrix
#      row) — every row added since the matrix was last hand-extended was
#      simply absent, and the document silently under-claimed coverage.
#      The same blindness applied a level up: a whole test suite with no
#      section here was invisible. Both are reported now, and a non-empty
#      report is a non-zero exit (see "Exit status" below).
#   8. Regenerate the head `## Summary` table from this same run, between
#      the `<!-- BEGIN/END GENERATED SUMMARY -->` markers. It used to be
#      hand-typed, and had drifted three months and ~2000 rows out of date;
#      hand-typing fresh numbers into it only restarts that drift.
#
# Why `unrecorded` is reported and NOT auto-added: the payload of a matrix
# row is its *description* — the human-readable statement of what the row
# covers — and this script has no honest source for one. The plan doc has a
# description only for IDs that are plan rows (which are largely recorded
# already); the test's own second argument is an assertion-failure message,
# not a row title. Emitting a row whose only human-readable field is `—`
# would make the count look right while the traceability the document exists
# for is absent, and would be self-consistent on every later run so it would
# never surface again. That is the same failure the banner/nearest-comment
# citation tiers were rejected for. Nor can the script tell which of a
# section's two tables ("plan rows" vs "Extra coverage (not in plan)") a new
# ID belongs in — that is precisely the human judgement being recorded. So
# the omission is reported, loudly, and closing it stays a deliberate act,
# exactly as `test/unit-tests.conf` pins row counts rather than silently
# adopting whatever a suite happens to emit. (GH #117)
#
# ── `test/unit-tests.conf` is the driver (GH #144) ────────────────────
#
# WHICH suites this document covers is not a second list any more. The
# manifest is the driver, and every suite it declares must be accounted for
# in exactly one of two ways:
#
#   traced      it appears in @SUBSYS, which says which `##` section it
#               belongs to — the one genuinely editorial fact left;
#   tombstoned  it appears in %NO_MATRIX_SECTION with a written reason.
#
# Anything else is a REFUSAL: exit 2, matrix untouched, in the manner of
# `test/run-unit-tests.sh` refusing to run when its manifest and CMake
# disagree. Both directions are checked, and so are duplicates.
#
# This replaces an advisory warning that had saturated. The old @SUBSYS was a
# hand-written table of (header, binary, source path); it traced 28 suites for
# the whole v0.98 series while the manifest grew 49 -> 80, so ~31 suites were
# added and never traced. Each addition arrived as one more name on a warning
# line that already listed fifty, inside a report that only runs at
# version-bump time. Making it a hard failure is the anti-drift mechanism:
# adding a suite now forces a deliberate decision, exactly as adding a test row
# forces the manifest's row count to be edited.
#
# SOURCE PATHS ARE READ FROM CMAKE, not from a hand-written path and not from
# a name convention — see cmake_sources(). Two distinct things would have to
# be guessed, and each has real exceptions:
#
#   the BASENAME — 80 of 87 suite names match their source basename and 7 do
#   not: `cpu_int_pulse_test` is built from `int_pulse_test.cpp`, and the six
#   `debugger_*` suites from `video_panel_test.cpp`, `audio_panel_test.cpp`,
#   `quit_gate_test.cpp`, `window_size_test.cpp`, `window_grow_test.cpp`,
#   `accel_test.cpp`;
#
#   the DIRECTORY — the two ESP-01 module suites keep their basename but live
#   under `src/esp01/test/`, declared by `src/esp01/CMakeLists.txt` rather
#   than `test/`'s, so no `test/`-rooted search finds them.
#
# A convention that is right 92% of the time is the worst kind.
# This mirrors run-unit-tests.sh, which already treats CMake as the authority
# for what a suite's binary is.
#
# THE GATE IS WIRED IN. `--check-accounting` runs the accounting above and
# nothing else — no test binary, no test source, the matrix neither opened nor
# written — in 0.01 s with no build prerequisite, and `make unit-test` depends
# on it exactly as it does on `docs-check` and `cli-check`. That matters more
# than the rule itself: the warning this replaced was reachable only by a human
# following the version-bump checklist, which is precisely how it stayed
# saturated for a whole release series. A lock is worth what its door is worth.
#
# The FULL refresh is deliberately not in the build: it needs a built test tree
# and still exits 1 on the 821-row `unrecorded` backlog. That is a row-level
# question with its own exit code, and the suite-level gate does not have to
# wait for it.
#
# Usage:
#     perl test/refresh-traceability-matrix.pl                  # full refresh
#     perl test/refresh-traceability-matrix.pl --check-accounting
#
# Exit status:
#     0  clean
#     1  the matrix was rewritten and under-records rows (the GH #117 backlog)
#     2  REFUSAL: the suite list is not accounted for; nothing was written
#     3  internal error (unreadable file, un-runnable binary, missing marker)
#
# On 1 the matrix IS rewritten: it means "refreshed AND under-recording, here
# is the backlog", never "nothing was written". On 2 and 3 nothing is written.
# 3 is not decoration — `die` derives its status from errno, so "binary not
# found" used to exit 2 (ENOENT) and read as a refusal, which is the one signal
# `make unit-test` now acts on.
#
# Dependencies: the test binaries must already be built under `build/test/`.
# This script does not build them.
#
# See `doc/testing/UNIT-TEST-PLAN-EXECUTION.md` for the broader refresh
# workflow this script implements.

use strict;
use warnings;
use File::Spec;
use Cwd qw(abs_path);
use FindBin qw($RealBin);

# ── Exit status vocabulary ────────────────────────────────────────────
#
#   0  clean
#   1  the matrix was rewritten and under-records rows (the GH #117 backlog)
#   2  REFUSAL: the suite list is not accounted for; nothing was written
#   3  internal error (unreadable file, un-runnable binary, missing marker)
#
# 3 exists because `die` derives its status from errno, so "binary not found"
# exited 2 (ENOENT) and "binary not executable" exited 255 — the first
# indistinguishable from a refusal, which is precisely the signal a caller
# will act on once the gate is wired into `make unit-test`. Every internal
# error goes through fatal() instead.
sub fatal {
    my ($msg) = @_;
    # `die`, not `exit`, so a caller may still catch it — the selftest asserts
    # several of these refusals (SELF-25) and would otherwise be killed
    # mid-run by the first one. main() turns it into exit 3.
    die "refresh-traceability-matrix: $msg\n";
}

my $ROOT   = abs_path("$RealBin/..");
my $MATRIX = "$ROOT/doc/testing/TRACEABILITY-MATRIX.md";

# `--check-accounting` runs ONLY the suite-accounting gate and exits: no test
# binary is run, no source is read, the matrix is neither opened nor written.
# That is what lets it be a prerequisite of `make unit-test` alongside
# `docs-check` and `cli-check` — 0.01 s and no build dependency, against 2.3 s
# and a built test tree for the full refresh.
#
# Wiring it in is the point. A lock is only worth its door: the predecessor
# warning was reachable only by a human following the version-bump checklist,
# and that is exactly the trigger under which it saturated unnoticed for the
# whole v0.98 series. The ROW-level backlog (821 unrecorded rows, exit 1) is
# what keeps the full refresh out of the build for now; the gate is a
# different question with a different exit code and does not have to wait for
# it.
# Parsed from main(), never at file scope: the selftest LOADS this file to get
# at its subs, and a file-scope @ARGV walk would then read the selftest's own
# arguments and exit before a single row ran.
my $CHECK_ONLY = 0;
my $DUMP_DESC  = 0;
my $EMIT_SECTION;
my $EMIT_TO;
# --fpga-src=PATH (GH #202). Declared HERE, with the other option variables,
# rather than beside fpga_src() further down: parse_args() below assigns it,
# and a `my` is not in scope before its own declaration.
my $FPGA_SRC_OPT;
sub parse_args {
    for my $arg (@_) {
        if ($arg eq '--check-accounting') { $CHECK_ONLY = 1; next; }
        # GH #196 phase 2 — print, for every traced source, the description
        # row_descriptions() derives and the ones it cannot. Reads nothing but
        # the test sources and writes nothing, so it is safe to run against a
        # dirty tree; it exists so the derived-position rule is MEASURED
        # against all 90 suites rather than asserted from a sample.
        if ($arg eq '--dump-descriptions') { $DUMP_DESC = 1; next; }
        # GH #196 phase 2.1 — print ONE section exactly as the emitter would
        # write it, so its output can be diffed against the committed section
        # before the emitter is allowed anywhere near the file.
        if ($arg =~ /^--emit-section=(.+)$/) { $EMIT_SECTION = $1; next; }
        # Write the WHOLE emitted document to a path of the caller's choosing,
        # so it can be diffed against the committed matrix before the emitter
        # is allowed to replace it.
        if ($arg =~ /^--emit-to=(.+)$/) { $EMIT_TO = $1; next; }
        # GH #202 — WHERE the FPGA core is checked out. Citations are validated
        # against it, and a run without it emits DIFFERENT bytes, so CI (which
        # has no checkout of its own) has to be told rather than left to guess.
        # Refused up front when it is not a directory: a typo'd path would
        # otherwise degrade silently into the unvalidated mode this exists to
        # make impossible.
        if ($arg =~ /^--fpga-src=(.+)$/) {
            $FPGA_SRC_OPT = $1;
            fatal("--fpga-src='$1' is not a directory") unless -d $FPGA_SRC_OPT;
            next;
        }
        fatal("unknown option '$arg'\n"
            . "usage: refresh-traceability-matrix.pl [--check-accounting] "
            . "[--dump-descriptions] [--fpga-src=PATH]");
    }
}

# (section_header_line, suite | [suites]) — WHICH `##` section a declared
# suite's rows belong to. This is the whole of what stays hand-written about
# the suite list, and it is genuinely editorial: nothing can derive that the
# Audio section deliberately groups three suites under one heading, or that
# `ula_integration_test` is a `###` companion of `## ULA Video` rather than a
# subsystem of its own.
#
# The suite names are `test/unit-tests.conf` names. Binary and source path are
# DERIVED (build/test/<suite>, and cmake_sources()); neither is written here.
#
# Z80N is deliberately absent: it uses the FUSE data-driven runner and its
# per-row status is permanently `missing` by design — see %NO_MATRIX_SECTION.
my @SUBSYS = (
    ['## Memory/MMU — `test/mmu/mmu_test.cpp`',      'mmu_test'],
    ['## ULA Video — `test/ula/ula_test.cpp`',       'ula_test'],
    ['## Layer2 — `test/layer2/layer2_test.cpp`',    'layer2_test'],
    ['## Sprites — `test/sprites/sprites_test.cpp`', 'sprites_test'],
    ['## Tilemap — `test/tilemap/tilemap_test.cpp`', 'tilemap_test'],
    ['## Copper — `test/copper/copper_test.cpp`',    'copper_test'],
    ['## Compositor — `test/compositor/compositor_test.cpp`', 'compositor_test'],
    # Audio is the one section whose header names several suites but has no
    # `### Companion` sub-tables — its AY/BP/IO/MX/NR/SD rows are interleaved
    # in a single table. Scanning only audio_test.cpp reported 51 of its 79
    # `missing` rows as untested when they are asserted in the two companion
    # files, so the entry lists all three (GH #117 review). The field accepts
    # an arrayref; every other section stays a plain scalar.
    ['## Audio — `test/audio/audio_test.cpp`',
     ['audio_test', 'audio_nextreg_test', 'audio_port_dispatch_test']],
    ['## DMA — `test/dma/dma_test.cpp`',             'dma_test'],
    ['## DivMMC+SPI — `test/divmmc/divmmc_test.cpp`', 'divmmc_test'],
    ['## Multiface — `test/multiface/multiface_test.cpp`', 'multiface_test'],
    ['## CTC+Interrupts — `test/ctc/ctc_test.cpp`',  'ctc_test'],
    ['## UART+I2C/RTC — `test/uart/uart_test.cpp`',  'uart_test'],
    ['## NextREG — `test/nextreg/nextreg_test.cpp`', 'nextreg_test'],
    ['## IO Port Dispatch — `test/port/port_test.cpp`', 'port_test'],
    ['## Input — `test/input/input_test.cpp`',       'input_test'],
    ['## Rewind — `test/rewind/rewind_test.cpp`',    'rewind_test'],
    ['## Floating Bus — `test/floating_bus/floating_bus_test.cpp`', 'floating_bus_test'],
    ['## VideoTiming — `test/videotiming/videotiming_test.cpp`', 'videotiming_test'],
    ['## Contention — `test/contention/contention_test.cpp`', 'contention_test'],
    ['## LoRes — `test/lores/lores_test.cpp`',       'lores_test'],
    ['## SD Card — `test/sdcard/sdcard_test.cpp`',   'sdcard_test'],
    ['## NMI Source Pipeline — `test/nmi/nmi_test.cpp`', 'nmi_test'],
    # CPU-side regression suites. Separate `##` sections rather than companions
    # of `## Z80N`: that section is the data-driven FUSE runner's, its rows are
    # opcode names, and merging scopes would let one vouch for the other.
    ['## CPU interrupt pulse — `test/cpu/int_pulse_test.cpp`', 'cpu_int_pulse_test'],
    ['## CPU/Z80N/IM2 regressions — `test/cpu/cpu_z80n_im2_regressions_test.cpp`',
     'cpu_z80n_im2_regressions_test'],
    # The emulated ESP-01 (GH #25). Its two MODULE suites are the first in this
    # project whose sources live outside `test/`: they ship inside the
    # self-contained component at `src/esp01/`, so a consumer gets the proof
    # with the code (src/esp01/CMakeLists.txt). Nothing here says so — the
    # source paths come from CMake, which is where that fact already lives, and
    # the BINARIES are under `build/test/` like every other suite because
    # src/esp01/CMakeLists.txt sets RUNTIME_OUTPUT_DIRECTORY to
    # ${CMAKE_BINARY_DIR}/test for precisely that reason. SELF-70 pins it.
    #
    # THREE `##` SECTIONS, NOT ONE PARENT WITH `###` COMPANIONS. `HOOK-01/02`
    # (AT) deliberately continue as `HOOK-03..06b` (adapter) — one logical
    # sequence spanning two files. `TRACE-01..04` used to look like the same
    # kind of deliberate split (what the TRANSPORT logs, socket suite, vs. what
    # the ENGINE logs, AT suite), but GH #196 Phase 1.2 found that pairing was
    # an accidental collision instead, and renamed the socket suite's rows to
    # `SOCK-TRACE-01..04`; the AT suite's bare `TRACE-01..09` is unaffected.
    # Recording and the companion status fallback are both scoped to the
    # owning `##` subsystem, so filing all three suites as one subsystem would
    # hide exactly that kind of collision from the duplicate-ID report instead
    # of surfacing it — the cross-section blind spot GH #118 closed. Separate
    # sections keep each scope honest.
    ['## ESP-01 socket transport — `src/esp01/test/esp_socket_test.cpp`',
     'esp_socket_test'],
    ['## ESP-01 AT engine — `src/esp01/test/esp_at_test.cpp`', 'esp_at_test'],
    ['## ESP-01 jnext UART adapter — `test/esp/esp_uart_adapter_test.cpp`',
     'esp_uart_adapter_test'],
    # Companion suites (sub-section ### headers), nested inside their parent
    # `##` and judged against its scope.
    ['### Companion integration suite — `test/mmu/mmu_integration_test.cpp`',
     'mmu_integration_test'],
    ['### Companion integration suite — `test/ula/ula_integration_test.cpp`',
     'ula_integration_test'],
    ['### Companion integration suite — `test/compositor/compositor_integration_test.cpp`',
     'compositor_integration_test'],
    ['### Companion integration suite — `test/copper/copper_integration_test.cpp`',
     'copper_integration_test'],
    ['### Companion regression suite — `test/tilemap/tilemap_fetch_split_test.cpp`',
     'tilemap_fetch_split_test'],
    ['### Companion integration suite — `test/lores/lores_integration_test.cpp`',
     'lores_integration_test'],
    ['### Companion integration suite — `test/ctc_interrupts/ctc_interrupts_test.cpp`',
     'ctc_interrupts_test'],
    ['### Companion integration suite — `test/nextreg/nextreg_integration_test.cpp`',
     'nextreg_integration_test'],
    ['### Companion integration suite — `test/nmi/nmi_integration_test.cpp`',
     'nmi_integration_test'],
    ['### Companion integration suite — `test/input/input_integration_test.cpp`',
     'input_integration_test'],
    ['### Companion integration suite — `test/uart/uart_integration_test.cpp`',
     'uart_integration_test'],
);

# Per-suite plan doc, consulted as the last citation source when the test
# source carries none. Keyed by SUITE NAME, like every other hand-written
# table here.
my %PLAN_DOC = (
    'mmu_test'                    => 'MEMORY-MMU',
    'ula_test'                    => 'ULA-VIDEO',
    'ula_integration_test'        => 'ULA-VIDEO',
    'layer2_test'                 => 'LAYER2',
    'sprites_test'                => 'SPRITES',
    'tilemap_test'                => 'TILEMAP',
    'copper_test'                 => 'COPPER',
    'compositor_test'             => 'COMPOSITOR',
    'compositor_integration_test' => 'COMPOSITOR',
    'audio_test'                  => 'AUDIO',
    'audio_nextreg_test'          => 'AUDIO',
    'audio_port_dispatch_test'    => 'AUDIO',
    'dma_test'                    => 'DMA',
    'divmmc_test'                 => 'DIVMMC-SPI',
    'ctc_test'                    => 'CTC-INTERRUPTS',
    'ctc_interrupts_test'         => 'CTC-INTERRUPTS',
    'uart_test'                   => 'UART-I2C',
    'uart_integration_test'       => 'UART-I2C',
    'nextreg_test'                => 'NEXTREG',
    'nextreg_integration_test'    => 'NEXTREG',
    'port_test'                   => 'IO-PORT-DISPATCH',
    'input_test'                  => 'INPUT',
    'input_integration_test'      => 'INPUT',
    'floating_bus_test'           => 'FLOATING-BUS',
    'videotiming_test'            => 'VIDEOTIMING',
    'contention_test'             => 'CONTENTION',
    'lores_test'                  => 'LORES',
    'lores_integration_test'      => 'LORES',
    # Companions share their parent's plan doc — that shared doc IS the
    # parent/companion relationship the emitter reads (GH #196). Three were
    # absent, so their parents never got the GH #121 status fallback and
    # published rows as `missing` that the companion asserts.
    'mmu_integration_test'        => 'MEMORY-MMU',
    'copper_integration_test'     => 'COPPER',
    'tilemap_fetch_split_test'    => 'TILEMAP',
    'multiface_test'              => 'MULTIFACE',
    'nmi_test'                    => 'NMI-PIPELINE',
    'nmi_integration_test'        => 'NMI-PIPELINE',
);

# Suites with no VHDL counterpart at all: their spec is a jnext-internal
# contract or an external standard, not the FPGA core. An empty `—` there
# reads as "citation missing"; a tombstone says "there is nothing to cite",
# which is a different — and permanent — fact. Keyed by suite name.
#
# NOT the same thing as %NO_MATRIX_SECTION below: this is a CITATION tombstone
# on the rows of a suite that IS traced. A suite that has no section at all is
# accounted for there instead.
my %TOMBSTONE = (
    'rewind_test'   => '(jnext-internal)',
    'sdcard_test'   => '(SD SPI spec)',
    # The ESP-01 is a THIRD-PARTY module hanging off the Next's UART 0 header.
    # The Next-side wire is VHDL (`zxnext.vhd:1611-1612` drives o_UART0_TX from
    # uart0_tx_esp; `:3381` labels the channel "uart 0 (esp)"), but nothing
    # inside the ESP is: the FPGA core has no ESP8266 in it, only the pins that
    # reach one. These two suites test what is on the FAR side of those pins,
    # so there is no line of the core to cite — a permanent fact, not a missing
    # citation. Their authority is the Espressif AT firmware surface (as
    # evidenced by the NextZXOS ESPAT.DRV, NXtel and nextsync clients, see
    # GH #25) and the host OS socket API plus the RFC address ranges the
    # security policy encodes.
    'esp_socket_test' => '(host sockets)',
    'esp_at_test'     => '(ESP-AT firmware)',
    # DELIBERATELY ABSENT: esp_uart_adapter_test. It is the one
    # ESP suite that is a MIXTURE — HOOK-03/03b/03c drive `Uart::tick`'s device
    # gate and HOOK-06/06b the framing bit-7 UART reset, both of which the FPGA
    # core does specify (`uart.vhd`, `uart_tx.vhd`, `uart_rx.vhd`), while the
    # ADP and LOG rows are jnext-internal seam contracts. A tombstone is
    # applied to every uncited row of its suite, so putting one here would
    # stamp "there is nothing to cite" onto rows that have something to cite.
    # Those rows read `—` instead: honest, and recoverable by citing the VHDL
    # in the test source, which is an edit for the branch that owns that file.
);

# ── Suite -> source path, read from CMake ─────────────────────────────
#
# CMake already knows what every suite is built from; a second copy of that
# fact in this file is a copy that can drift, and a name convention derived
# from it is worse — 80 of 87 suite names match their source basename and 7 do
# not (and two more keep the basename but sit outside `test/`), so the
# convention is right often enough to look correct and wrong often enough to
# lie. `test/run-unit-tests.sh` already treats CMake as the authority
# for what a suite's binary is; this is the same rule for its source.
#
# One line, one `add_executable(<name> <first-source> ...)`, resolved relative
# to the CMakeLists.txt that declares it — which is what makes the two
# module-resident ESP-01 suites (declared in `src/esp01/CMakeLists.txt`, source
# under `src/esp01/test/`) need no special case at all. SELF-70 pins that.
#
# Only the FIRST source is taken: a suite compiles its own test file plus, at
# most, a handful of emulator translation units it needs (`mmu_test` links
# `src/core/wav_loader.cpp`), and the row IDs live in the first. A `${VAR}`
# first argument is skipped — it is a source LIST (`jnext_tests ${GTEST_SOURCES}`),
# and guessing which file inside it holds the rows is exactly the kind of
# inference this script refuses elsewhere. Such a suite simply resolves to
# nothing, and the accounting gate below turns that into a refusal rather than
# a silent omission.
my %CMAKE_SRC;
my $CMAKE_SCANNED = 0;
sub cmake_sources {
    return \%CMAKE_SRC if $CMAKE_SCANNED;
    $CMAKE_SCANNED = 1;
    my @lists;
    if (open(my $fh, '-|', 'find', $ROOT, '-name', 'CMakeLists.txt',
                          '-not', '-path', '*/third_party/*',
                          '-not', '-path', '*/build*/*',
                          '-not', '-path', '*/.git/*')) {
        while (my $p = <$fh>) { chomp $p; push @lists, $p; }
        close $fh;
    }
    for my $list (sort @lists) {
        (my $dir = $list) =~ s{/CMakeLists\.txt$}{};
        open(my $lf, '<', $list) or next;
        while (my $line = <$lf>) {
            next if $line =~ /^\s*#/;
            next unless $line =~ /\badd_executable\s*\(\s*([A-Za-z0-9_]+)\s+([^\s()]+)/;
            my ($name, $src) = ($1, $2);
            next if $src =~ /^\$\{/;
            my $rel = "$dir/$src";
            $rel =~ s{^\Q$ROOT\E/}{};
            # A name declared twice with two different sources is CMake's
            # problem, but silently keeping one of them would make this file
            # disagree with the build. Say so.
            if (exists $CMAKE_SRC{$name} && $CMAKE_SRC{$name} ne $rel) {
                warn "WARN: add_executable($name) declared twice with "
                   . "different sources: $CMAKE_SRC{$name} vs $rel\n";
                next;
            }
            $CMAKE_SRC{$name} = $rel;
        }
        close $lf;
    }
    return \%CMAKE_SRC;
}

# source path -> suite name, for the two suite-keyed editorial tables above.
#
# Filled by main() from the resolved CMake map, which is authoritative. The
# basename fallback exists for the SELFTEST only: it loads this file with
# $ROOT rebound to a fixture tree that has no CMakeLists.txt, and its fixtures
# use real `test/<dir>/<suite>.cpp` paths whose basename is the suite name. A
# wrong answer here can only lose a plan-doc citation or a tombstone — it can
# never invent one — so the fallback cannot publish anything false.
my %SUITE_OF_SRC;
sub suite_for_source {
    my ($src) = @_;
    return $SUITE_OF_SRC{$src} if exists $SUITE_OF_SRC{$src};
    (my $base = $src) =~ s{.*/}{};
    $base =~ s/\.cpp$//;
    return $base;
}

# ── VHDL citation extraction ──────────────────────────────────────────
#
# The `VHDL file:line` column sat at `—` on ~1600 rows because this script
# only ever rewrote Status and Test file:line. The citations were never
# missing — they live in the test source, next to the row they justify.
#
# Four row-local evidence tiers are trusted, in order:
#
#   call    the check()/skip() call carrying this row's own ID literal
#   named   a comment block that names this row ID explicitly
#   next    the first check()/skip() call after the ID literal, but ONLY when
#           the ID has no call of its own — the table-driven signature
#           ({"MMU-01", ...} arrays, where the ID lives in an initialiser and
#           the shared check() is in the loop below it)
#   plan    the subsystem plan doc's row for this ID
#
# Vaguer evidence — a category banner comment, the nearest *unrelated*
# preceding comment — is deliberately NOT used. Both were prototyped; they
# reach further but attribute a neighbouring row's VHDL lines to this one,
# and a plausible-but-wrong citation is worse than an honest `—`.
#
# The order holds ACROSS a section's sources too, not just within one file:
# see cite_upgrades(). A `plan` answer read through one source is provisional
# and yields to row-local evidence from the source that actually asserts the
# row (GH #133).
#
# Citations are also validated against the real FPGA source tree, so a
# typo'd or renamed VHDL filename is reported rather than published.

# WHERE the core is, in precedence order (GH #202):
#
#   --fpga-src=PATH        explicit, wins over everything
#   $JNEXT_FPGA_SRC        the environment form, which is what CI sets
#   upward search          a sibling checkout, found by walking up from the
#                          script's own directory — and, when that directory
#                          is inside a LINKED git worktree, also from the main
#                          checkout the worktree belongs to
#
# The default used to be one hardcoded absolute path under the maintainer's
# home. That was invisible to everyone else, and CI — which has no FPGA
# checkout at all — silently took the "no tree" branch of resolve_vhd() and
# published every citation unvalidated. The matrix therefore came out with
# DIFFERENT BYTES in CI than locally, which a byte-exact staleness gate can
# never reconcile: traceability-check could not pass in both places at once.
#
# The search walks up rather than using a fixed depth because the repo and its
# agent worktrees sit at different depths — `spectrum/jnext/test` is two levels
# below the sibling, a `<worktree>/test` is wherever the worktree was put — so
# any constant `../..` is correct for exactly one of them. It starts from
# $RealBin, NOT $ROOT, because the selftest rebinds $ROOT to a fixture tree
# while the script itself stays in the real repo.
#
# The MAIN-CHECKOUT start exists because agent worktrees no longer live beside
# the repo (they moved to ~/tmp/worktrees, 2026-08-03), so no ancestor of the
# worktree contains the FPGA sibling and the walk from $RealBin alone finds
# nothing. Without it every `make unit-test` in such a worktree regenerated an
# UNVALIDATED matrix whose bytes differ from the committed one, and the
# staleness gate failed — in a worktree only, which reads as a phantom
# regression. Resolving the main checkout from the worktree's `.git` file
# fixes this for ANY worktree location by construction, with no per-run
# $JNEXT_FPGA_SRC export and no owner-specific path (GH #204's class).
my $FPGA_SRC;        # resolved once by fpga_src(), '' when there is no tree

# The parent walk is spelled with a regex rather than File::Basename::dirname
# ON PURPOSE: that module is packaged separately on Fedora (perl-File-Basename)
# and is NOT in ci.yml's explicit dependency list. Relying on it arriving
# transitively is the exact failure that list exists to prevent — CI once broke
# on `Can't locate FindBin.pm` with no change to script or workflow. Adding no
# dependency beats declaring one here.
sub discover_fpga_src {
    for my $start ($RealBin, worktree_main_checkout($RealBin)) {
        next unless defined $start;
        my $dir = $start;
        for (1 .. 8) {
            my $cand = "$dir/ZX_Spectrum_Next_FPGA/cores/zxnext/src";
            return $cand if -d $cand;
            last if $dir eq '/';
            (my $up = $dir) =~ s{/[^/]*$}{};
            $up = '/' if $up eq '';
            $dir = $up;
        }
    }
    return undef;
}

# The main checkout a linked git worktree belongs to, or undef when $from is
# not inside one. In a linked worktree the checkout root's `.git` is a plain
# FILE reading `gitdir: <main>/.git/worktrees/<name>`; the main checkout is
# read straight out of that line — no git invocation, no new module. A normal
# checkout has a `.git` DIRECTORY, which ends the walk with undef (there is no
# indirection to follow). A relative gitdir (git's optional
# worktree.useRelativePaths form) is resolved against the directory holding
# the `.git` file before matching, via the already-imported abs_path.
sub worktree_main_checkout {
    my ($from) = @_;
    my $dir = $from;
    for (1 .. 8) {
        if (-f "$dir/.git") {
            open(my $fh, '<', "$dir/.git") or return undef;
            my $line = <$fh> // '';
            close $fh;
            return undef unless $line =~ m{^gitdir:\s*(.*?)\s*$};
            my $gitdir = $1;
            $gitdir = "$dir/$gitdir" unless $gitdir =~ m{^/};
            $gitdir = abs_path($gitdir) // return undef;
            return undef unless $gitdir =~ m{^(.*)/\.git/worktrees/[^/]+$};
            return -d $1 ? $1 : undef;
        }
        last if -d "$dir/.git";
        last if $dir eq '/';
        (my $up = $dir) =~ s{/[^/]*$}{};
        $up = '/' if $up eq '';
        $dir = $up;
    }
    return undef;
}

# Resolved lazily and memoised, so it is correct whether or not parse_args()
# ran — the selftest loads this file and calls its subs directly, without ever
# walking @ARGV.
sub fpga_src {
    return $FPGA_SRC if defined $FPGA_SRC;
    $FPGA_SRC = $FPGA_SRC_OPT // $ENV{JNEXT_FPGA_SRC} // discover_fpga_src();
    if (!defined $FPGA_SRC || !-d $FPGA_SRC) {
        # LOUD, because silence here is the actual defect: an unvalidated run
        # looks exactly like a validated one and its output differs.
        warn "refresh-traceability-matrix: WARNING — no FPGA core found"
           . (defined $FPGA_SRC && length $FPGA_SRC ? " at '$FPGA_SRC'" : '')
           . ".\n"
           . "  VHDL citations cannot be validated, so a typo'd or renamed\n"
           . "  filename will be published verbatim and a bare basename will\n"
           . "  not be folded. The output WILL differ from a run that has the\n"
           . "  core, so do not commit a matrix generated this way.\n"
           . "  Point at a checkout with --fpga-src=PATH or \$JNEXT_FPGA_SRC.\n";
        $FPGA_SRC = '';
    }
    return $FPGA_SRC;
}

# `\.vhd` must not be a prefix of a longer identifier, or `row.vhdl_line`
# in a printf argument list is read as a citation of "row.vhd".
#
# A citation MAY carry a directory prefix, and until GH #145 the class could
# not express one: `[A-Za-z0-9_]+\.vhd` never captured a directory, so a cell
# written the way the design docs write it — `device/copper.vhd:54-119`, the
# qualified relative path — could never equal the bare filename the extractor
# computed, and drifted on every run for ever. A permanent false entry in the
# drift report is noise hiding a real one (the same argument as GH #142).
#
# The prefix is CAPTURED rather than discarded, and then validated: see
# resolve_vhd() for how `device/copper.vhd` (a real path), `src/zxnext.vhd` (a
# real path spelled from one level up) and `bogus/copper.vhd` (not a path at
# all) are told apart. Folding a prefix away unconditionally was the other
# option and is refused — it would need basenames to be unique across the FPGA
# tree, and `hdmi_plle2.vhd` exists twice (`pll/A7/`, `pll/A7-Issue-5/`).
#
# A citation's line list may continue in two spellings, and BOTH have to be
# consumed or the tail is silently dropped (GH #136):
#
#   zxnext.vhd:5080, 6188-6189     bare continuation
#   zxnext.vhd:5080, :6188-6189    filename-omitting continuation
#
# The second reads correctly to a human — the filename is understood to carry
# forward — which is why it went unnoticed: `uart_integration_test.cpp:701`
# published `zxnext.vhd:5080` and dropped the `:6188-6189` read-mask lines its
# assertion actually exercises.
#
# Measured over the blobs this regex is fed: 197 dropped tails from test
# sources plus 17 from plan-doc rows. 183 of the 197 use a punctuation
# separator (`,` 125, `+` 33, `/` 25) and are consumed below; the remaining
# 14 use a word (`vs` 12, `and` 2) and are deliberately NOT.
#
# `+` is admitted ONLY in the colon-carrying spelling. The colon is what makes
# the tail unambiguously a line reference; a bare `foo.vhd:100 + 200` would be
# indistinguishable from arithmetic, and no instance of it exists in the tree
# to justify the risk.
#
# The word separators stay out (`zxnext.vhd:5391-5393 vs :5462`) because they
# are prose, not citation syntax, and a regex that consumes English words is
# one step from consuming the sentence around it — whose failure mode is
# publishing a WRONG citation. Stopping early publishes a
# correct-but-incomplete one, which this project ranks strictly better.
#
# THE BARE CONTINUATION MUST NOT SWALLOW A BIT RANGE (GH #144). `zxnext.vhd:100,
# 15:0 field` names ONE line and a VHDL slice; the bare `, <digits>` arm read
# the `15` as a second line reference and published `zxnext.vhd:100,15` — a
# citation the source does not support, which is the failure this project ranks
# below an honest em dash and the exact reasoning that got the banner-comment
# and nearest-comment tiers rejected.
#
# The guard is a negative lookahead on the BARE arm only: a continuation whose
# digits are themselves followed by `:` is a slice, not a line, so the citation
# stops before it. It cannot mis-fire on a real list — `, 200` is followed by
# prose or nothing, never by a colon — and it is measured: zero `, N:M` shapes
# exist anywhere in the tree today, so the guard costs nothing and closes the
# hole before the first one arrives.
#
# What is deliberately NOT done is the issue's first suggestion, "require the
# colon on continuations". Measured over the test sources and plan docs the
# extractor reads: 852 continuations use the bare spelling against 210 using
# the colon-carrying one. Requiring the colon would delete 852 CORRECT
# citations to guard against a shape that occurs zero times — destroying good
# evidence, which is a different act from refusing a bad one.
my $VHDL_CITE_RE = qr{
    \b ( (?: [A-Za-z0-9_.\-]+ / )* [A-Za-z0-9_]+ \.vhd ) (?! [A-Za-z0-9_] )
    (?: \s* : \s*
        ( \d+ (?: \s* [-–] \s* \d+ )?
          (?: (?: \s* [/,+] \s* : \s*
                | \s* [/,] \s* (?! \d+ \s* : ) )
              \d+ (?: \s* [-–] \s* \d+ )? )* ) )?
}x;

# ── A line reference with the FILENAME LEFT OUT (GH #188) ─────────────
#
# `(VHDL 7163-7176)`, `(VHDL :195,:203)`, `VHDL:5080` — the author wrote the
# evidence and omitted the one token that makes it resolvable. 253 quoted
# strings in the tree carry this shape.
#
# THIS IS A REPORT LABEL AND NOTHING ELSE. It never yields a citation, and
# widening $VHDL_CITE_RE to accept it is refused: the missing filename can
# only come from somewhere OUTSIDE the row — the nearest `.vhd` in the file,
# the enclosing banner — which is exactly the banner/nearest-comment tier this
# extractor rejected twice and re-litigated in GH #147 and GH #184. A rule
# that has to guess the filename publishes a plausible-but-wrong citation, and
# this project ranks that strictly below an honest `—`. The remedy is to spell
# the filename in the row's own check(), which the frozen report now asks for
# by name.
#
# `\bVHDL\b` is required, so a bare `(7163-7176)` — which could be anything —
# does not qualify. A text that ALSO names a real `.vhd` is not this shape;
# callers test that separately (the citation is computed and the row never
# reaches the frozen report at all).
#
# The trailing `(?![-\w])` is what tells a LINE REFERENCE from prose that
# happens to put a number after the word. Built by trying to break the first
# draft against the real corpus, which turned up three shapes it mislabelled:
# `VHDL 9-bit` and `VHDL 13-bit` (a width, not a line) and `VHDL: 0x9F` (a port
# value). Requiring the number to END — not to run on into `-bit` or `x9F` —
# refuses all three while still accepting every real spelling in the tree:
# `VHDL 7214)`, `VHDL 7163-7176 else`, `VHDL :195,:203)`, `VHDL:4636-4644).`,
# `VHDL :882/3716/3729).`, `VHDL: :3814`.
#
# DECLARED RESIDUAL: `VHDL 2 cycles` — a small number followed by a SPACE and
# a word — is still read as a line reference. Zero instances exist in the tree
# today (the three found were all hyphen- or `x`-joined), and the cost of one
# is a mislabelled bucket in a report, never a published citation: sub-class
# (b) says "the filename is missing", and a human who looks finds prose and
# reclassifies it as (a). Tightening further would mean guessing how many
# digits a line number has, which is a rule about VHDL file lengths, not about
# citations.
my $VHDL_NOFILE_RE = qr{
    \b VHDL \b \s* :? \s* :?
    \d+ (?: \s* [-–] \s* \d+ )?
    (?! [-\w] )
}x;

# Plan row IDs as they appear unquoted inside a comment ("TM-01:", "TM-01/02").
my $ID_BARE_RE = qr{
    \b ( [A-Z][A-Z0-9]* (?: \.[A-Z][A-Z0-9]* )* - [A-Za-z0-9._\-+]*[A-Za-z0-9] )
}x;

# The prefix-carrying shorthand — `TM-01/02`, `DVP-18b/18c`, `UTB-50/51` — is
# ONE token to $ID_BARE_RE, which stops at the `/` (its class has no slash) and
# never resumes, because `02` does not start with `[A-Z]`. So a block naming two
# rows in the shorthand was counted as naming ONE, and the header comment above
# has claimed `TM-01/02` works since the class was written. That under-count is
# load-bearing in the `named` tier: `DVP-18b/18c` read as a single row is what
# kept the ambiguity refusal below from firing on the block GH #184 was filed
# about (three citations, two rows). (GH #184)
#
# Expansion carries the prefix up to the LAST `-` forward onto each `/`-joined
# tail: `G56-CR-NR06-04/05` -> `G56-CR-NR06-05`. A tail that is itself a full ID
# (`UDIS-02/UDIS-03`) is NOT synthesised — the /g scan matches it on its own —
# and neither is one carrying `.vhd`, so a `FOO-01/device/copper.vhd` shape
# cannot manufacture a row named after a path component.
#
# Returns (\@literal, \@expanded). The split is the whole point and was
# measured: a synthesised ID may only COUNT toward the ambiguity refusal, never
# RECEIVE a citation. Letting it receive one made things worse, not better —
# `port_test.cpp:120` is a helper-function comment reading "V18-NMP-02/03/04
# helper. Enable the DAC via NR 0x08 bit 3 ... zxnext.vhd:5179", and assigning
# through the expansion turned one row holding that unrelated DAC-enable line
# into three. Expansion widens what the extractor can SEE, and seeing more rows
# in a block can only ever make it refuse more; it must not widen what the
# extractor is willing to CLAIM.
sub bare_ids_in {
    my ($text) = @_;
    my (@lit, @all, %seen);
    while ($text =~ /$ID_BARE_RE/g) {
        my $id = $1;
        next if $id =~ /\.vhd/;
        unless ($seen{$id}++) { push @lit, $id; push @all, $id; }
        my ($prefix) = $id =~ /^(.*-)/;
        while ($text =~ m{\G/ ( [A-Za-z0-9][A-Za-z0-9._+\-]* )}gcx) {
            my $tail = $1;
            last if $tail =~ /\.vhd/;
            last if $tail =~ /^[A-Z][A-Z0-9]*(?:\.[A-Z][A-Z0-9]*)*-/;
            my $syn = "$prefix$tail";
            push @all, $syn unless $seen{$syn}++;
        }
    }
    return (\@lit, \@all);
}

# The IDs a comment line is ABOUT, as opposed to the ones it merely mentions:
# the run of row IDs the line OPENS with, once the `//` marker and any leading
# box-drawing/bullet punctuation is stripped. IDs may be joined by `/`, `+`,
# `,`, `&`, `..` or the word `and`; the run stops at the first token that is
# neither an ID nor one of those joiners.
#
# This is deliberately structural, not linguistic. The alternative considered
# and REJECTED was a cross-reference PHRASE list ("sibling of", "see", "cf.",
# "unlike", "as in", "compare") — that is a regex consuming English, the exact
# move this file refuses for citation continuations two screens up, and English
# has unbounded ways to say it ("... below asserts", "... is X's job", "not
# duplicated here", "mirrors ..."). A phrase list also measured as refusing
# ZERO of the 277 named-tier rows in the traced corpus, i.e. it is untestable
# against real data. Where an ID sits in the line is a fact; what the sentence
# around it means is not. (GH #184)
sub heading_ids_in {
    my ($text) = @_;
    my %head;
    for my $line (split /\n/, $text) {
        (my $c = $line) =~ s{^\s*//+}{};
        $c =~ s/^[^A-Za-z0-9]+//;
        while ($c =~ /^$ID_BARE_RE/) {
            my $id = $1;
            last if $id =~ /\.vhd/;
            $head{$id} = 1;
            $c = substr($c, length $id);
            my ($prefix) = $id =~ /^(.*-)/;
            while ($c =~ m{^/ ( [A-Za-z0-9][A-Za-z0-9._+\-]* )}x) {
                my $tail = $1;
                last if $tail =~ /\.vhd/;
                last if $tail =~ /^[A-Z][A-Z0-9]*(?:\.[A-Z][A-Z0-9]*)*-/;
                $head{"$prefix$tail"} = 1;
                $c = substr($c, 1 + length $tail);
            }
            last unless $c =~ s{^ \s* (?: [/+,&] | \.\. | \band\b ) \s* }{}x;
        }
    }
    return \%head;
}

# "  FAIL ID: ...", "  FAIL ID [...", or "[FAIL] ID" — the three spellings the
# suites actually print.
#
# The bracketed form is not cosmetic. `cpu_int_pulse_test`,
# `cpu_z80n_im2_regressions_test` and `cli_options_test` print `[FAIL] <name>`,
# and the first two are traced sections. Without this arm their FAIL set would
# read empty and every row of both would publish `pass` while its assertion
# fails — the one direction a status column must never be wrong in. Found when
# they were first traced (GH #144); pinned by SELF-79/80.
#
# The `[FAIL]` arm requires the ID to be the whole rest of the line up to
# optional trailing detail, and is anchored at the start, so a `FAIL` appearing
# inside a description cannot match.
my $FAIL_RE = qr/^\s*(?:FAIL\s+([A-Za-z0-9._\-]+)\s*[:\[]
                     |\[FAIL\]\s+([A-Za-z0-9._\-]+)\b)/x;

# Hand-maintained row marker: `<!-- protected: reason -->` after the row's
# closing `|`. GFM ignores content beyond the header's column count and it
# is an HTML comment anyway, so it renders invisibly.
my $PROTECTED_RE = qr/<!--\s*protected\b[^>]*-->/;

# Hand-maintained TABLE marker (GH #192): the prose above a table that has no
# `Test file:line` and no `VHDL file:line` column must say so, because nothing
# in such a table is computed and a reader has no other way to tell. Checked
# per table and reported when absent — a location that LOOKS computed and is
# not is the defect class GH #187/#188 spent two days on.
my $UNREFRESHED_MARK = 'NOT REFRESHED';

# skip("ID", ...) or stub("ID", ...) first-arg string literal. Both helpers
# flag "not reachable via current C++ API" and are aggregated under the
# Skip/Stub column in the Summary table.
my $SKIP_RE = qr/\b(?:skip|stub)\s*\(\s*"([A-Za-z0-9._\-]+)"/;

# Plan-row-shaped string literal anywhere in the source. Three shapes:
#   1. Dashed prefix:  "MMU-01", "AY-110", "TM-CB5", "I2C-P05a",
#                      "G1.AT-01", "G10.SC-01", "S1.05-mode", "NR_A0-01"
#   2. Numeric dotted: "9.7", "14.6", "14.7a" (DMA plan rows)
#   3. Section-dotted: "S13.14", "S2.08" (ULA sections)
#
# The prefix admits `_` (GH #125). Without it the class stopped dead at the
# underscore, so `NR_A0-01/02/03` — asserted in `uart_integration_test.cpp`
# and listed in two matrix tables — matched nothing and published `missing`
# no matter what asserted them. The blindness was total and silent: all three
# ID scanners (grep_source, grep_row_ids, grep_citations' id_line) read this
# one pattern, so the rows were absent from the status side AND from the
# `unrecorded` report that exists to catch exactly that. $SKIP_RE and $FAIL_RE
# had always accepted `_`, so a skip()ped underscore row was half-visible —
# the two halves of one tool disagreeing about what an ID is.
#
# Widening an ID regex fails the same way a loose citation tier does: match
# too much and row status silently attaches to things that are not rows.
# So `_` is admitted ONLY inside the uppercase prefix, and the dash still
# does the real work of separating an ID from an identifier. Refused, by
# construction and pinned in SELF-48:
#   "ZXN_ISSUE2"       enum member — uppercase and underscored, but no dash
#   "pi_uart_en-flag"  C++ variable — dashed, but does not start uppercase
#   "_A0-01"           leading underscore is not an ID prefix
# Measured over all 81 files under `test/` (*.cpp + *.h, comment lines
# included, so the figure is an upper bound), the widening admits exactly
# four new literals, all in `test/uart/uart_integration_test.cpp`: the three
# NR_A0 rows and the `set_group("NR_A0-INT")` banner, which the set_group
# filter in grep_row_ids drops as it drops every other banner.
my $ID_LITERAL_RE = qr{
    "
    (
        [A-Z][A-Z0-9_]* (?: \.[A-Z][A-Z0-9_]* )* - [A-Za-z0-9._\-+]+
      | \d+ \. \d+ [a-z]?
      | S \d+ \. \d+ [a-z]?
    )
    "
}x;

my @SUBLETTERS = ('a', 'b', 'c');

# `set_group("NAME")` prints a banner over a group of rows; NAME is a group
# label, never a row. It is the only non-assertion helper in the 28 mapped
# suites that wraps an ID-shaped literal (163 call sites, measured), so
# dropping just this one call's argument is enough to make the `unrecorded`
# report precise. The substitution is per-occurrence, not per-ID: an ID that
# is *both* a group banner and a real assertion elsewhere stays a row.
my $SET_GROUP_RE = qr/\bset_group\s*\(\s*"[^"]*"/;

# Declared suites deliberately outside the script's per-row scope. Keyed by
# the `test/unit-tests.conf` suite name. Every entry needs a reason: this map
# is the ONLY way a suite escapes the accounting gate, so an unreasoned entry
# is a silent hole — and one blanket reason covering forty suites is the same
# hole with more words. The reasons below are grouped by the AUTHORITY the
# suite is actually written against, because that is the fact a reader needs:
# a tombstone claims there is no line of the FPGA core to cite, and that claim
# has to be true and specific for each group.
#
# This document is the map from VHDL-derived plan row -> test -> citation. A
# suite whose oracle is a file format, a host API, a GUI contract or a
# jnext-internal policy has no plan row to map, so it has no section here. It
# is still fully COUNTED — `test/unit-tests.conf` pins its row count and
# `make unit-test` runs it — and its runtime view lives in
# `test/SUBSYSTEM-TESTS-STATUS.md`.
my %NO_MATRIX_SECTION = (
    # ── Data-driven runners ──────────────────────────────────────────
    # They walk tests.in/tests.expected and have no in-source row IDs at all,
    # so there is nothing to trace per row. The matrix says so itself under
    # "Discrepancies noted"; the `## Z80N` section's rows are opcode names and
    # are permanently `missing`.
    'fuse_z80_test' => 'data-driven FUSE runner, no per-row IDs',
    'z80n_test'     => 'data-driven FUSE-style runner, opcode names not row IDs',

    # ── Narrative sections ───────────────────────────────────────────
    # Hand-maintained tables that summarise ID *ranges* ("XNEX-01..04") or
    # point at rows kept by hand, not per-row IDs this script can regenerate.
    'extended_nex_test'  => 'narrative section, ID ranges not per-row IDs',
    'atic_atac_nmi_test' => 'narrative section, hand-maintained (feeds protected NR-C0-02)',

    # ── Host-side file formats and media provisioning ────────────────
    # The oracle is a published on-disk format or jnext's own provisioning
    # policy. The FPGA core never parses a file — it sees SPI blocks, which
    # `## SD Card` traces against the SD SPI spec.
    'nex_loader_test'         => 'NEX file-format spec (host loader), no core counterpart',
    'nex_v13_test'            => 'NEX V1.3 file-format spec + nexload2.asm (host loader), no core counterpart',
    'sd_rom_extractor_test'   => 'FAT32 + TBBlue SD path layout (host ROM extraction)',
    'fat32_image_test'        => 'FAT32 on-disk format (host image reader)',
    'sdcard_provisioner_test' => 'jnext SD-image download/patch policy (host side)',
    'video_recorder_cmd_test' => 'FFmpeg command-line construction (host encoder)',

    # ── Guest-firmware surfaces jnext stands in for ──────────────────
    # The oracle is the NextZXOS/esxDOS API contract and jnext's own trap
    # policy, not the FPGA core: the core has no esxDOS in it.
    'esxdos_stub_test'    => 'esxDOS API surface + jnext trap policy, not core logic',
    'phantom_typist_test' => 'jnext auto-typing state machine (host keystroke injection)',

    # ── Host audio pipeline, downstream of the modelled mixer ────────
    # `## Audio` traces the VHDL-modelled AY/DAC/beeper/mixer chain. These
    # five start where it ends: SDL pacing, the device-boundary fill, WAV
    # capture and the user gain controls, none of which exist in the core.
    'audio_pacing_test'   => 'host SDL audio pacing/underrun policy, downstream of the mixer',
    'audio_fill_test'     => 'host SDL device-boundary fill/hold policy (GH #208), downstream of the mixer',
    'audio_capture_test'  => 'host WAV capture of the mixer output',
    'audio_gain_test'     => 'host output-gain control (a user setting, not a core register)',
    'subsystem_gain_test' => 'host per-subsystem gain control (a user setting)',

    # ── Host frame pacing, presentation and boot choreography ────────
    # Wall-clock scheduling of run_frame()/present() on the host. The core
    # free-runs off a 28 MHz clock and has no notion of a frame deadline, a
    # dropped present or a speed percentage.
    'present_cadence_test' => 'host present cadence policy (wall-clock, not core timing)',
    'present_count_test'   => 'host present accounting (wall-clock, not core timing)',
    'render_policy_test'   => 'host render/skip policy (wall-clock, not core timing)',
    'frame_deadline_test'  => 'host frame-deadline scheduling (wall-clock)',
    'frame_sequencer_test' => 'host frame sequencer (wall-clock run/present ordering)',
    'tick_stats_test'      => 'host tick accounting for the status bar',
    'speed_report_test'    => 'host speed-percentage reporting',
    'emulator_boot_test'   => 'host cold-boot choreography (GH #40 contract, no VHDL oracle)',

    # ── Host policy around the emulated ESP-01 ───────────────────────
    # `## ESP-01 socket transport` / `AT engine` / `jnext UART adapter` trace
    # the module and its seam. This suite is one layer further out again: the
    # hostname allowlist, the connection-event log the GUI status cell reads,
    # and the Emulator wiring (off by default, inert under replay). The FPGA
    # core has no allowlist, no status bar and no rewind, so there is nothing
    # in it to cite — the oracle is the owner's security decisions recorded in
    # doc/design/ESP01-EMULATOR-DESIGN.md §8.
    'esp_wiring_test'  => 'jnext host ESP policy/visibility/wiring, no core counterpart',
    'esp_status_test'  => 'host status-bar ESP indicator (GUI), no core counterpart',

    # ── NEX V1.3 gate dialog ─────────────────────────────────────────
    # GH #228 — the GUI warning dialog's Proceed/Cancel wiring for the
    # experimental-NEX-V1.3 policy. Pure jnext policy + Qt glue; the FPGA
    # core has no loader and no dialog, so there is nothing to cite. The
    # policy predicate/probe/enforcement rows live in nex_loader_test.
    'nex_v13_dialog_test' => 'experimental NEX V1.3 warning dialog (GUI), no core counterpart',

    # ── Host input translation ───────────────────────────────────────
    # Qt/SDL event -> ZX matrix translation on the HOST side. The guest-side
    # membrane matrix these feed is traced by `## Input`; what is asserted
    # here is the host key latch and the frontend key bindings, which the core
    # does not contain (it sees a PS/2 stream and a membrane, not a Qt event).
    'host_key_latch_test' => 'host key latch/debounce compensation; guest matrix is `## Input`',
    'pointer_capture_test' => 'host mouse-capture policy (window-manager behaviour)',
    'esc_break_test'      => 'host ESC->BREAK binding; guest matrix is `## Input`',
    'host_hotkey_test'    => 'host hotkey bindings (Alt vs the guest Symbol Shift)',
    'main_window_accel_test' => 'main-window menu mnemonics (host GUI)',
    'shifted_keys_test'   => 'host shifted-scancode translation; guest matrix is `## Input`',
    'window_scale_test'   => 'main-window scale geometry (host GUI)',

    # ── CLI, configuration, logging, profiling ───────────────────────
    # jnext-internal contracts. `cli_options_test` is checked against the man
    # page by `make cli-check`, which is its own two-way gate.
    'cli_options_test'              => 'CLI flag table vs the man page (see `make cli-check`)',
    'log_test'                      => 'jnext logging façade (spdlog wiring)',
    'log_gate_test'                 => 'jnext log-level gating',
    'profiler_test'                 => 'jnext profiler output format (a developer tool)',
    'app_config_test'               => 'jnext.conf schema/precedence (host settings file)',
    'preferences_apply_test'        => 'Preferences dialog wiring (host GUI)',
    'preferences_apply_policy_test' => 'Preferences apply/revert policy (host GUI)',
    'audio_gain_config_test'        => 'gain settings persistence (host settings file)',
    'audio_gain_preferences_test'   => 'gain controls in the Preferences dialog (host GUI)',

    # ── Debugger and window-manager GUI ──────────────────────────────
    # Host GUI behaviour. Two of these — the video and audio panel suites —
    # DO cite VHDL, and the citations are real: they justify what the correct
    # DISPLAY is by pointing at the hardware behaviour behind it (e.g. NR 0x08
    # bit 5 is a single stereo bit, `zxnext.vhd:5177` vs the independent bit 4
    # at `:5178`). But the assertion is about the PANEL's rendering, and the
    # hardware behaviour it leans on is already traced in `## Audio`,
    # `## Compositor`, `## Layer2` and `## ULA Video`. They are also `?`-gated
    # in the manifest — built only under `-DENABLE_DEBUGGER=ON` — so a section
    # here would make this document's content depend on the host's build
    # configuration, which a traceability record must not.
    'window_attach_test'        => 'host window-attach geometry (GH #39 contract, no VHDL oracle)',
    'quit_cleanup_test'         => 'host shutdown ordering (GUI lifecycle)',
    'resume_guard_test'         => 'debugger resume-confirmation policy (jnext-internal)',
    'step_out_test'             => 'debugger Step Out execution control (jnext-internal); the T80N core has no debugger',
    'persistent_bp_test'        => 'debugger breakpoint arming policy (GH #219, jnext-internal); the T80N core has no debugger',
    'io_watchpoint_test'        => 'debugger I/O watchpoints (GH #222, jnext-internal); the T80N core has no debugger',
    'resume_step_off_test'      => 'debugger resume/step-off execution control (GH #221, jnext-internal); the T80N core has no debugger',
    'debugger_persistent_bp_test' => 'debugger window raise-on-hit (host GUI lifecycle, GH #219)',
    'debugger_video_panel_test' => 'debugger panel RENDERING; the hardware it displays is traced in `## Compositor`/`## Layer2`/`## ULA Video` (GUI-gated build)',
    'debugger_audio_panel_test' => 'debugger panel RENDERING; the hardware it displays is traced in `## Audio` (GUI-gated build)',
    'debugger_quit_gate_test'   => 'debugger quit gating (host GUI lifecycle)',
    'debugger_window_size_test' => 'debugger window geometry (host GUI)',
    'debugger_window_grow_test' => 'debugger window geometry (host GUI)',
    'debugger_accel_test'       => 'debugger keyboard accelerators (host GUI)',
    'debugger_menu_test'        => 'debugger menu reachability (host GUI)',
);

# The head Summary table is generated between these markers. They are HTML
# comments, so they render invisibly.
my $SUMMARY_BEGIN = '<!-- BEGIN GENERATED SUMMARY — written by test/refresh-traceability-matrix.pl; do not edit by hand -->';
my $SUMMARY_END   = '<!-- END GENERATED SUMMARY -->';

# The selftest loads this file without running main(), so it sees the subs
# but not the file-lexical markers. Hand them over rather than let the
# selftest re-declare the literals: a copy could drift from the original
# and the selftest would still pass.
sub summary_markers { return ($SUMMARY_BEGIN, $SUMMARY_END); }

# @SUBSYS binary/source fields are a plain scalar for a single-suite section
# and an arrayref for a multi-suite one.
sub as_list {
    my ($v) = @_;
    return ref $v eq 'ARRAY' ? @$v : ($v);
}

# Memoised: since GH #121 a section also consults the FAIL set of the other
# suites in its own subsystem, so the seven companion binaries would each be
# run twice (once for their own section, once for their parent's). The suites
# are deterministic and the process makes no attempt to rebuild between
# calls, so one run per binary per process is the same answer for less work.
#
# Keyed by PATH: a fixture that rewrites a stub binary at a path already run
# would silently get the first content's FAIL set. Give each fixture stub its
# own name.
my %FAILS_CACHE;

sub run_fails {
    my ($binary) = @_;
    return $FAILS_CACHE{$binary} if exists $FAILS_CACHE{$binary};
    my $abs = "$ROOT/$binary";

    # Mirror Python subprocess.run's FileNotFoundError: refuse to "run" a
    # missing binary and silently see an empty FAIL set (which would
    # pass-whitewash every row in that section).
    fatal("binary not executable: $abs") unless -x $abs;

    my %fails;
    my $pid = open(my $fh, '-|');
    if (!defined $pid) {
        fatal("fork failed for $binary: $!");
    }
    if ($pid == 0) {
        # Child: merge stderr into stdout so pipe captures both.
        open(STDERR, '>&', \*STDOUT) or exit 127;
        exec($abs) or exit 127;
    }

    my $timed_out = 0;
    eval {
        local $SIG{ALRM} = sub { die "timeout\n" };
        alarm(180);
        while (my $line = <$fh>) {
            if ($line =~ $FAIL_RE) {
                # Two alternatives, so the ID lands in whichever group matched.
                $fails{ defined $1 ? $1 : $2 } = 1;
            }
        }
        alarm(0);
    };
    if ($@) {
        alarm(0);
        $timed_out = 1;
        kill 'TERM', $pid;
        sleep 1;
        kill 'KILL', $pid;
    }

    close $fh;
    waitpid($pid, 0);

    if ($timed_out) {
        fatal("$binary timed out after 180s");
    }

    return $FAILS_CACHE{$binary} = \%fails;
}

# The source file, read once, with whole-line `//` comments blanked to the
# empty string. Blanked and not removed, so an index still equals its line
# number minus one.
#
# Both ID scanners need exactly this rule, and for the same reason: an ID
# quoted inside a `//` line is prose or a disabled assertion, never a live
# one. grep_row_ids() has applied it since GH #117; grep_source() did not,
# so the commented-out `check("7.3", ...)` at `test/dma/dma_test.cpp:785`
# published matrix row 7.3 as `pass` for an assertion that does not run —
# the two halves of one tool disagreeing about the same file (GH #119).
# One reader, one rule, so they cannot drift apart again.
#
# `grep_citations()` deliberately does NOT use this: its `named` tier reads
# comment blocks on purpose, and it already skips comment lines when
# harvesting ID literals.
#
# Block comments are still not stripped. Measured: no `/* */` body in any of
# the 28 mapped suites contains an ID-shaped literal, and stripping them
# needs a string-literal-aware scanner (a `"/*"` inside a description would
# otherwise swallow the rest of the file) — a larger change than the defect
# warrants, and one that trades a loud wrong answer for a silent one.
sub source_lines {
    my ($abs) = @_;
    open(my $fh, '<', $abs) or fatal("open $abs: $!");
    my @src = <$fh>;
    close $fh;
    for my $l (@src) { $l = '' if $l =~ m{^\s*//}; }
    return \@src;
}

sub grep_source {
    my ($source_rel) = @_;
    my $src = source_lines("$ROOT/$source_rel");
    my (%checks, %skips);

    for my $lineno (1 .. scalar @$src) {
        my $line = $src->[$lineno - 1];
        while ($line =~ /$SKIP_RE/g) {
            $skips{$1} //= $lineno;
        }
    }
    for my $lineno (1 .. scalar @$src) {
        my $line = $src->[$lineno - 1];
        while ($line =~ /$ID_LITERAL_RE/g) {
            my $tid = $1;
            next if exists $skips{$tid};
            $checks{$tid} //= $lineno;
        }
    }
    return (\%checks, \%skips);
}

# What the FPGA core actually contains, or 0 when it is not checked out next
# to jnext (CI, a fresh clone) — in which case validation is skipped rather
# than failing every citation.
#
#   base   basename -> number of files carrying it. The COUNT, not a flag:
#          `hdmi_plle2.vhd` exists twice (`pll/A7/`, `pll/A7-Issue-5/`), and
#          that is exactly the case where a directory prefix carries real
#          information and must not be folded away (GH #145).
#   spell  every ACCEPTED spelling of a file -> its basename. A spelling is
#          any path SUFFIX of the file's absolute path, cut at a `/`. So
#          `zxula.vhd`, `video/zxula.vhd`, `src/video/zxula.vhd` and
#          `cores/zxnext/src/video/zxula.vhd` are all accepted for the same
#          file, which is what lets `device/copper.vhd` (the design docs'
#          convention) and `src/zxnext.vhd` (24 sites, written from one level
#          up) both validate without a special case for either.
my $VHDL_FILES;
sub vhdl_files {
    return $VHDL_FILES if defined $VHDL_FILES;
    my (%base, %spell);
    my $src = fpga_src();
    if (length $src && open(my $fh, '-|', 'find', $src, '-name', '*.vhd')) {
        while (my $p = <$fh>) {
            chomp $p;
            my @seg = split m{/}, $p;
            my $bn  = $seg[-1];
            $base{$bn}++;
            for my $i (0 .. $#seg) {
                $spell{ join('/', @seg[$i .. $#seg]) } = $bn;
            }
        }
        close $fh;
    }
    # A directory that EXISTS but holds no .vhd degrades to exactly the same
    # unvalidated mode as no directory at all, and fpga_src()'s -d check cannot
    # see it: the wrong-but-real path (a botched sparse-checkout, one level too
    # deep, the repo root instead of cores/zxnext/src) is the likelier typo of
    # the two, and it was silent. Warn on the condition that actually matters —
    # "citations cannot be validated" — rather than only on the missing path.
    if (!%base && length $src) {
        warn "refresh-traceability-matrix: WARNING — '$src' exists but contains\n"
           . "  no .vhd files, so VHDL citations cannot be validated. The output\n"
           . "  WILL differ from a run against the real core. Check the path\n"
           . "  points at cores/zxnext/src inside a ZX Next FPGA checkout.\n";
    }
    $VHDL_FILES = %base ? { base => \%base, spell => \%spell } : 0;
    return $VHDL_FILES;
}

# What the FPGA core says about a cited filename, which may be qualified.
# Returns (verdict, published_name):
#
#   'ok'       the spelling names a real file — published verbatim, prefix and
#              all, because the prefix is information the reader asked for
#   'rehomed'  the DIRECTORY is wrong but the basename is real: published as
#              the bare basename (never worse than the pre-GH #145 behaviour,
#              which discarded every prefix) and REPORTED, because a wrong
#              directory is a wrong citation even when the file exists
#   'unknown'  nothing in the core carries that basename — dropped, as before
#   'unchecked' the core is not on this machine; nothing is validated
sub resolve_vhd {
    my ($cited) = @_;
    my $known = vhdl_files();
    return ('unchecked', $cited) unless $known;
    return ('ok', $cited) if exists $known->{spell}{$cited};
    (my $bn = $cited) =~ s{.*/}{};
    return ('rehomed', $bn) if exists $known->{base}{$bn};
    return ('unknown', $cited);
}

# True when a bare basename identifies exactly one file in the core, i.e. when
# a directory prefix adds nothing and may be folded away for COMPARISON.
# False for `hdmi_plle2.vhd`, and false when the core is not checked out —
# refusing to fold is the answer that cannot be wrong.
sub vhd_basename_unique {
    my ($bn) = @_;
    my $known = vhdl_files();
    return 0 unless $known;
    return (($known->{base}{$bn} // 0) == 1) ? 1 : 0;
}

# EVERY VHDL citation in a blob of text, normalised to "file.vhd:lines" and
# joined with ", " in the order they appear.
#
# This was a scalar-context match returning only the FIRST citation, and the
# consequence was silent: 27 of the 126 cells the new sections published named
# one file when the row's own `check()` named two or three. `MF-MUX-07` was the
# worst — it published `multiface.vhd:64,103`, where :64 is a bare port
# DECLARATION, and dropped `zxnext.vhd:2816`, the gate the row is actually
# about. A cell that is a strict prefix of the truth reads exactly like a
# complete one, so nothing downstream could tell them apart; the drift report
# cannot either, because the extractor agreed with itself on every later run.
#
# NO REGEX LOOSENING. Each match is still anchored on a real `*.vhd` token and
# still validated against the FPGA tree. What this does NOT recover is the
# filename-omitting continuation once prose interrupts it — `multiface.vhd:158
# (clear), :165 (eff)` still publishes only `:158`, because reaching across
# `(clear)` means consuming English, whose failure mode is publishing a WRONG
# citation. Stopping early publishes a correct-but-incomplete one, which this
# project ranks strictly better (see $VHDL_CITE_RE above, same trade).
#
# Duplicates are collapsed: a row whose evidence names the same lines twice
# (common when a comment restates the call's citation) gets one entry, not two.
#
# So is a strict RESTATEMENT of the same file with fewer lines. The common
# shape is a description naming the headline line and the detail naming the
# full set — `check("MF-M1G-01", "... multiface.vhd:169)", cond,
# "multiface.vhd:169,176")` — which would otherwise publish
# `multiface.vhd:169, multiface.vhd:169,176`. Suppression is by verbatim TOKEN
# SUBSET on the same filename, so nothing is merged, renumbered or reordered:
# an entry is dropped only when every line reference it makes already appears,
# spelled identically, in another entry for that same file. `:169` goes;
# `:169` vs `:176` would both stay, and so would two different files.
#
# cite_list() is the same computation returning the entries as a LIST, because
# HOW MANY citations a blob offers is itself evidence: the `named` tier refuses
# a comment block that names several rows AND offers several citations, since
# there is then no row-local basis for saying which belongs to which (GH #147).
# cite_in() is the joined form every existing caller wants.
my %REHOMED_WARNED;
sub cite_in {
    my $l = cite_list(@_);
    return @$l ? join(', ', @$l) : undef;
}
sub cite_list {
    my ($text) = @_;
    my (@out, %seen);
    my $core = fpga_src();    # memoised; named once rather than interpolated
                              # as ${\ fpga_src() } at each warn site
    while ($text =~ /$VHDL_CITE_RE/g) {
        my ($cited, $lines) = ($1, $2);
        my ($verdict, $file) = resolve_vhd($cited);
        if ($verdict eq 'unknown') {
            warn "WARN: citation names '$cited', which is not in $core\n";
            next;
        }
        # A wrong DIRECTORY beside a real basename: publish the basename (the
        # pre-GH #145 answer, so nothing is lost) and say so once per distinct
        # spelling. Once, because a single wrong path is written at dozens of
        # sites and a warning repeated forty times is the saturated-warning
        # failure this tool has already been bitten by twice.
        if ($verdict eq 'rehomed' && !$REHOMED_WARNED{$cited}++) {
            warn "WARN: citation names '$cited'; $core has no such path, "
               . "but does carry '$file' — publishing the bare filename\n";
        }
        my $cite;
        if (!defined $lines) {
            $cite = $file;
        } else {
            $lines =~ s/\s+//g;
            # Canonical published form is the one already in the matrix: line
            # refs joined by `,` or `/`, no filename repeated. So fold the two
            # spellings the regex accepts back onto it — `+` becomes `,` (it is
            # only ever a list separator here) and the carried-forward `:` is
            # dropped. Every `:` left in $lines is a continuation marker by
            # construction: the citation's own `:` is matched outside this
            # capture group. (GH #136)
            $lines =~ s/\+/,/g;
            $lines =~ s/://g;
            $cite = "$file:$lines";
        }
        # Both the duplicate check and the subset suppression below are asked
        # about the FILE, and after GH #145 a file has more than one accepted
        # spelling — so both are keyed on the canonical form, or
        # `copper.vhd:87-89` sits beside `device/copper.vhd:87-89,92-97`
        # instead of being suppressed by it (measured: TIM-CYC-02 and FNK-01
        # both regressed exactly that way while this was keyed on the literal
        # spelling). canon_citation() folds a prefix only when the basename is
        # unambiguous, so the two `hdmi_plle2.vhd` files still never merge.
        next if $seen{ canon_citation($cite) }++;
        push @out, { cite => $cite, file => canon_citation($file),
                     tok  => { map { $_ => 1 }
                               split(/[,\/]/, defined $lines ? $lines : '') } };
    }
    my @kept;
    for my $i (0 .. $#out) {
        my $redundant = 0;
        for my $j (0 .. $#out) {
            next if $i == $j;
            next unless $out[$j]{file} eq $out[$i]{file};
            # Proper subset only, so two identical entries cannot delete each
            # other (%seen has already removed those anyway).
            next unless scalar(keys %{ $out[$j]{tok} })
                      > scalar(keys %{ $out[$i]{tok} });
            my $covered = 1;
            for my $t (keys %{ $out[$i]{tok} }) {
                $covered = 0, last unless $out[$j]{tok}{$t};
            }
            $redundant = 1, last if $covered;
        }
        push @kept, $out[$i]{cite} unless $redundant;
    }
    return \@kept;
}

# Is the apostrophe at $i a C++14 DIGIT SEPARATOR (`500'000`, `0x1234'5678`)
# rather than a char-literal quote? Shared by the two quote scanners below.
#
# Found in review of GH #189: a lone separator opens a char literal that never
# closes, so the scanner runs to end of line and code_prefix() hands back the
# line UNMODIFIED — silently reverting to the raw-line matching this commit
# exists to remove, for that line only. 14 lines in the corpus carry one;
# `frame_sequencer_test.cpp:358` (`fx.advance(500'000);  // 500 ms stall`) was
# reproduced live. Harmless today only because no such comment happens to say
# `check(` — one word added to any of the 14 reintroduces the phantom call.
#
# The test is structural: a separator is flanked by hex digits on BOTH sides,
# and a char literal cannot be. It is consulted only where a literal would be
# OPENED — a closing quote is recognised by the in-literal branch and never
# reaches here.
#
# DECLARED, on the strength of the fixtures rather than assumed: only the
# PRECEDING-character half is discriminated by anything. Dropping the
# following-character half was mutation-tested and no row moves, because the
# half only ever fires on an opening quote and an opening char-literal quote
# cannot be directly preceded by a hex digit in valid C++ (`5'a'` does not
# parse). It is kept because losing it widens the predicate, and a wider
# predicate mistakes a real quote for a separator — the direction that leaves
# the phantom call alive. Dropping the preceding half IS pinned (SELF-165).
#
# DECLARED RESIDUAL: a genuine one-character literal that is itself a hex
# digit AND butts directly against a hex digit on its other side (`0xa'b'`)
# would be read as a separator. No such spelling is valid C++ and none occurs;
# it is written down rather than guarded against.
sub is_digit_separator {
    my ($s, $i) = @_;
    return 0 if $i == 0;
    return substr($s, $i - 1, 1) =~ /[0-9A-Fa-f]/
        && substr($s, $i + 1, 1) =~ /[0-9A-Fa-f]/;
}

# The CODE part of one line: everything before the first `//` that starts a
# comment. (GH #189 defect 2 — see the call scan in grep_citations().)
#
# Quote-aware, because `//` is ordinary content inside a string literal and
# truncating there would DELETE a real assertion call — the dangerous
# direction, since a row whose call vanishes stops owning one and becomes
# eligible for the `next` tier it was fenced off from. The scanner tracks
# double quotes, single quotes and backslash escapes, which is enough for
# every shape in the corpus: a `'"'` char literal (5 lines), a `"http://"`-
# style literal (2 lines) and `R"(...)"` raw strings all parse correctly,
# the last because the raw string's own `"` opens and closes it.
#
# Verified over all 41 traced sources: of the lines whose helper-regex match
# disappears under this truncation, exactly ONE is a real trailing-comment
# phantom and ZERO are real calls.
sub code_prefix {
    my ($line) = @_;
    my ($in_dq, $in_sq) = (0, 0);
    my $n = length($line);
    for (my $i = 0; $i < $n - 1; $i++) {
        my $ch = substr($line, $i, 1);
        if ($ch eq '\\')                       { $i++;                next; }
        if ($in_dq)                            { $in_dq = 0 if $ch eq '"';  next; }
        if ($in_sq)                            { $in_sq = 0 if $ch eq "'";  next; }
        if ($ch eq '"')                        { $in_dq = 1;          next; }
        if ($ch eq "'")                        { $in_sq = 1 unless is_digit_separator($line, $i);
                                                 next; }
        return substr($line, 0, $i)
            if $ch eq '/' && substr($line, $i + 1, 1) eq '/';
    }
    return $line;
}

# The FIRST argument of an assertion call — the slot the row ID goes in.
# (GH #189 defect 1 — see the `next` tier's refusal in grep_citations().)
#
# Quote-aware and nesting-aware, so a `,` inside a string or inside a nested
# call does not end the argument early. Returns '' when the text holds no
# helper call at all, which reads as "names no row" — the permissive answer,
# and safe here because this is only ever consulted for a call the scan
# already found.
sub first_arg {
    my ($text) = @_;
    return '' unless $text =~ /\b(?:check|check_pred|check_eq|skip|stub)\s*\(/g;
    my ($depth, $in_dq, $in_sq, $out) = (0, 0, 0, '');
    for (my $i = pos($text); $i < length($text); $i++) {
        my $ch = substr($text, $i, 1);
        $out .= $ch;
        if ($ch eq '\\')                    { $out .= substr($text, ++$i, 1); next; }
        if ($in_dq)                         { $in_dq = 0 if $ch eq '"';  next; }
        if ($in_sq)                         { $in_sq = 0 if $ch eq "'";  next; }
        if    ($ch eq '"')                  { $in_dq = 1; }
        elsif ($ch eq "'")                  { $in_sq = 1
                                                  unless is_digit_separator($text, $i); }
        elsif ($ch eq '(')                  { $depth++;   }
        elsif ($ch eq ')')                  { last if $depth == 0; $depth--; }
        elsif ($ch eq ',' && $depth == 0)   { last;       }
    }
    return $out;
}

# ── Row DESCRIPTIONS from the test source (GH #196 phase 2) ───────────
#
# Inverting the generator makes a live row's description come from its own
# assertion instead of a hand-written matrix cell, which is what lets the
# `unrecorded` class die by construction rather than be reported forever.
#
# WHICH ARGUMENT HOLDS IT CANNOT BE ASSUMED, and this is the whole reason the
# three subs below exist. Measured across the tree: **26 distinct helper
# signatures**, not one convention. The description sits in argument 1 for the
# ~4359 rows of 76 files that spell `check(id, desc, cond, detail)`, but in
# argument 2 for `ctc_test` (`check(id, cond, desc, detail)`, 132 rows) and
# `tilemap_fetch_split_test`, in argument 3 for `tilemap_test`'s
# `check(id, actual, expected, note)` (72 rows), and nowhere at all for the
# `check(id, cond)` shape (100 rows, every one in a tombstoned suite).
#
# A filename-keyed table of positions would work today and rot tomorrow — it is
# the same hand-maintained second source of truth this whole issue exists to
# delete. So the position is DERIVED from the declaration the file already
# carries:
#
#   the ID parameter is the first string-typed parameter (or the second, when
#   the first is a `Result&` — `cpu_z80n_im2_regressions_test`);
#   the DESCRIPTION is the first string-typed parameter AFTER it.
#
# Validated against all 26 signatures, including the two that legitimately
# resolve to "none". A suite that grows a new helper shape is handled by
# construction instead of by somebody remembering to edit a table.
#
# What this deliberately does NOT do is invent a description. An argument that
# is not a plain string literal (a `fmt(...)` call, a variable) yields undef,
# and undef reaches the caller as "this row has no derivable description" —
# the honest answer, and the one the exceptions file exists to answer for the
# rows that genuinely have none.
sub helper_arg_positions {
    my ($source_rel) = @_;
    my $src = source_lines("$ROOT/$source_rel");
    # source_lines() keeps each line's trailing newline, so the parts are
    # joined with NOTHING. Joining with "\n" inserts a second newline per
    # line, which silently doubles every byte offset this sub converts back
    # into a line number — and a nearest-preceding lookup against doubled
    # line numbers matches nothing at all.
    my $text = join('', @$src);
    my %pos;
    # WRAPPERS COUNT, AND ARE FOUND RATHER THAN LISTED.
    #
    # A row's assertion is not always a direct `check()`. Suites wrap it —
    # `single(id, desc, scancode, row, col)` in input_test (a LAMBDA, not a
    # function), `expect_default` -> `expect_verdict` in esp_socket (two
    # levels) — and to a source reader those calls are where the row's
    # description lives.
    #
    # Adding "single", "expect_default", ... to a list of known names would
    # work until the next suite invents its own, which is the drift this whole
    # issue exists to remove. Instead a definition is ADOPTED as a helper when
    # its body calls a helper: the base five seed the set, and it is closed to
    # a fixpoint so a wrapper of a wrapper is found too. Candidates must also
    # take a string as their ID slot, and every call site still refuses an
    # argument that is not an ID-shaped literal, so adopting a function that
    # merely happens to call check() cannot invent rows.
    my @cand;
    # Free functions, with optional attributes/qualifiers, and lambdas bound
    # to a name (`auto single = [&](...)`). Both spellings are in live use.
    while ($text =~ /^[ \t]*(?:\[\[[^\]]*\]\][ \t]*)*(?:static[ \t]+)?
                     (?:inline[ \t]+)?(?:void|bool|int)[ \t]+
                     ([A-Za-z_]\w*)[ \t]*\(([^)]*)\)/gmx) {
        push @cand, { name => $1, params => $2, at => $-[0] };
    }
    while ($text =~ /\bauto[ \t]+([A-Za-z_]\w*)[ \t]*=[ \t]*\[[^\]]*\][ \t]*\(([^)]*)\)/g) {
        push @cand, { name => $1, params => $2, at => $-[0] };
    }
    # Body of each candidate: from its parameter list to the matching close
    # brace, so "does it call a helper" is asked of the definition and not of
    # the whole file.
    for my $c (@cand) {
        my $open = index($text, '{', $c->{at});
        next if $open < 0;
        my ($depth, $i) = (0, $open);
        for (; $i < length($text); $i++) {
            my $ch = substr($text, $i, 1);
            $depth++ if $ch eq '{';
            if ($ch eq '}') { $depth--; last if $depth == 0; }
        }
        $c->{body} = substr($text, $open, $i - $open + 1);
    }
    my %helper = map { $_ => 1 } qw(check check_eq check_pred skip stub);
    my $added = 1;
    while ($added) {
        $added = 0;
        for my $c (@cand) {
            next if $helper{ $c->{name} } || !defined $c->{body};
            my $calls = 0;
            for my $h (keys %helper) {
                $calls = 1, last if $c->{body} =~ /\b\Q$h\E\s*\(/;
            }
            next unless $calls;
            $helper{ $c->{name} } = 1;
            $added = 1;
        }
    }

    for my $c (@cand) {
        next unless $helper{ $c->{name} };
        my ($helper, $params) = ($c->{name}, $c->{params});
        my @p = grep { /\S/ } split(/,/, $params);
        next unless @p;
        my $is_str = sub { $_[0] =~ /char\s*\*|string/ };
        my $id = $is_str->($p[0]) ? 0
               : (@p > 1 && $is_str->($p[1])) ? 1 : undef;
        next unless defined $id;
        my $desc;
        for my $i ($id + 1 .. $#p) { $desc = $i, last if $is_str->($p[$i]); }
        # First declaration wins: a file that overloads a helper declares the
        # ID-bearing form first in every suite measured, and a later overload
        # must not silently move the slot the earlier calls use.
        $pos{$helper} //= { id => $id, desc => $desc };
    }
    return \%pos;
}

# The argument list of the first helper call in $text, as raw source strings.
# Same quote- and nesting-aware scan first_arg() uses — a `,` inside a string
# or a nested call does not end an argument — generalised to every argument
# rather than just the first. first_arg() is left alone: it is consulted on a
# hot path by the citation tiers and its contract ("the slot the row ID goes
# in") is narrower than this one.
sub call_args {
    my ($text, $names) = @_;
    my $re = $names || 'check|check_pred|check_eq|skip|stub';
    return () unless $text =~ /\b(?:$re)\s*\(/g;
    my ($depth, $in_dq, $in_sq, $cur, @args) = (0, 0, 0, '');
    for (my $i = pos($text); $i < length($text); $i++) {
        my $ch = substr($text, $i, 1);
        if ($ch eq '\\')                    { $cur .= $ch . substr($text, ++$i, 1); next; }
        if ($in_dq)                         { $in_dq = 0 if $ch eq '"';  $cur .= $ch; next; }
        if ($in_sq)                         { $in_sq = 0 if $ch eq "'";  $cur .= $ch; next; }
        if    ($ch eq '"')                  { $in_dq = 1; }
        elsif ($ch eq "'")                  { $in_sq = 1
                                                  unless is_digit_separator($text, $i); }
        elsif ($ch eq '(')                  { $depth++;   }
        elsif ($ch eq ')')                  { if ($depth == 0) { push @args, $cur; last }
                                              $depth--;   }
        elsif ($ch eq ',' && $depth == 0)   { push @args, $cur; $cur = ''; next; }
        $cur .= $ch;
    }
    return @args;
}

# The value of a C++ string-literal argument, or undef when the argument is
# not one. Adjacent literals concatenate, exactly as the compiler joins them,
# because a description long enough to matter is routinely written across
# several lines. Anything that is not purely literals — a variable, a
# `fmt(...)` call, a ternary — is undef rather than a guess.
sub literal_value {
    my ($arg) = @_;
    return undef unless defined $arg;
    my $rest = $arg;
    my $out  = '';
    my $seen = 0;
    while ($rest =~ /\G\s*"((?:[^"\\]|\\.)*)"/gc) { $out .= $1; $seen = 1; }
    return undef unless $seen;
    # Nothing but whitespace may follow the last literal; a trailing token
    # means the argument was an expression that merely began with a string.
    return undef if substr($rest, pos($rest) // 0) =~ /\S/;
    $out =~ s/\\n/ /g;
    $out =~ s/\\t/ /g;
    $out =~ s/\\"/"/g;
    $out =~ s/\\\\/\\/g;
    $out =~ s/\s+/ /g;
    $out =~ s/^\s+|\s+$//g;
    return $out;
}

# Split a comma-separated list at TOP level only — quote-, paren- and
# brace-aware. Shared by the call-argument scan and the table-entry scan so
# the two cannot disagree about where an element ends.
sub split_top_level {
    my ($text) = @_;
    my ($depth, $in_dq, $in_sq, $cur, @out) = (0, 0, 0, '');
    for (my $i = 0; $i < length($text); $i++) {
        my $ch = substr($text, $i, 1);
        if ($ch eq '\\')                  { $cur .= $ch . substr($text, ++$i, 1); next; }
        if ($in_dq)                       { $in_dq = 0 if $ch eq '"'; $cur .= $ch; next; }
        if ($in_sq)                       { $in_sq = 0 if $ch eq "'"; $cur .= $ch; next; }
        if    ($ch eq '"')                { $in_dq = 1; }
        elsif ($ch eq "'")                { $in_sq = 1 unless is_digit_separator($text, $i); }
        elsif ($ch =~ /[\(\[\{]/)         { $depth++; }
        elsif ($ch =~ /[\)\]\}]/)         { $depth--; }
        elsif ($ch eq ',' && $depth == 0) { push @out, $cur; $cur = ''; next; }
        $cur .= $ch;
    }
    push @out, $cur if $cur =~ /\S/;
    return @out;
}

# Every `struct T { ... }` + `T name[] = { {...}, ... }` pair in a file, as
# ordered records carrying the line they start on.
#
# The line matters and is not bookkeeping: `struct Row` is declared with
# DIFFERENT fields in different functions of the same suite, and two loops can
# iterate arrays of the same name. A lookup therefore resolves to the NEAREST
# PRECEDING declaration, never to "the one with that name" — the latter reads
# a sibling function's table and would attribute one group's descriptions to
# another, which is precisely the borrowed-evidence failure this extractor
# refuses everywhere else.
sub file_tables {
    my ($source_rel) = @_;
    my $src  = source_lines("$ROOT/$source_rel");
    # Line numbers are taken from the ARRAY INDEX, never counted from the
    # newlines in the joined text. source_lines() blanks a comment-only line
    # to the empty string — which drops its newline — so counting newlines
    # loses one line per comment and every offset converts back SHORT: the
    # ula S1 table at line 214 was computed as 172, and the nearest-preceding
    # lookup then resolved a later function's `struct Row` (id, fe, exp, why)
    # for it. Recording where each line starts is exact and immune to
    # whatever the blanking does to a line's contents.
    my (@start, $text);
    $text = '';
    for my $k (0 .. $#$src) {
        push @start, length($text);
        my $l = $src->[$k];
        # Keep the line slot for a blanked comment, or the lines either side
        # of it are concatenated into one.
        $l = "\n" if $l eq '';
        $text .= $l;
    }
    my $line_of = sub {
        my ($off) = @_;
        my ($lo, $hi) = (0, scalar @start);
        while ($lo < $hi) { my $m = int(($lo + $hi) / 2);
                            $start[$m] <= $off ? ($lo = $m + 1) : ($hi = $m); }
        return $lo;      # @start is 0-based; the count IS the 1-based line
    };

    my @structs;
    while ($text =~ /\bstruct\s+([A-Za-z_]\w*)\s*\{([^{}]*)\}/g) {
        my ($name, $body, $off) = ($1, $2, $-[0]);
        my @fields;
        for my $decl (split /;/, $body) {
            next unless $decl =~ /\S/;
            # ONE DECLARATION CAN DECLARE SEVERAL FIELDS: `int py, px;` is two
            # columns, not one. Taking only the last identifier dropped `py`
            # from ula's S1 table, which does not merely lose a column — it
            # shifts every index after it, so a later lookup reads the wrong
            # one. The first declarator carries the type and contributes its
            # last identifier; the rest are bare names.
            my @parts = split /,/, $decl;
            for my $k (0 .. $#parts) {
                next unless $parts[$k] =~ /\S/;
                push @fields, $1
                    if $parts[$k] =~ /([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*$/;
            }
        }
        push @structs, { line => $line_of->($off), name => $name, fields => \@fields };
    }

    my @arrays;
    while ($text =~ /\b(?:static\s+|const\s+|constexpr\s+)*([A-Za-z_]\w*)\s+
                     ([A-Za-z_]\w*)\s*\[[^\]]*\]\s*=\s*\{/gx) {
        my ($type, $var, $off) = ($1, $2, $-[0]);
        # walk to the matching close brace of the initialiser
        my ($depth, $i, $start) = (0, $+[0] - 1, $+[0] - 1);
        for (; $i < length($text); $i++) {
            my $ch = substr($text, $i, 1);
            $depth++ if $ch eq '{';
            if ($ch eq '}') { $depth--; last if $depth == 0; }
        }
        next if $depth != 0;
        my $body = substr($text, $start + 1, $i - $start - 1);
        my @rows;
        for my $entry (split_top_level($body)) {
            next unless $entry =~ /\{(.*)\}/s;
            push @rows, [ split_top_level($1) ];
        }
        push @arrays, { line => $line_of->($off), type => $type,
                        var => $var, rows => \@rows };
    }
    return { structs => \@structs, arrays => \@arrays };
}

# Values of one column of the table a loop iterates, or undef.
#
# $var is the loop variable's member expression (`r.id`, `c.id_35`); $at is the
# line the call sits on, which anchors every nearest-preceding lookup above.
sub table_column {
    my ($source_rel, $at, $expr) = @_;
    my ($loopvar, $field) = $expr =~ /^\s*([A-Za-z_]\w*)\s*(?:\.|->)\s*([A-Za-z_]\w*)\s*$/
        or return undef;
    my $src = source_lines("$ROOT/$source_rel");

    # Which array does this loop variable range over? Read from the `for`
    # header rather than guessed, and bounded to the 200 lines above the call
    # so a distant unrelated loop cannot answer.
    my $arr;
    for (my $i = $at - 1; $i >= 0 && $i > $at - 200; $i--) {
        # Matches every declarator spelling the suites use — `const Row& r`,
        # `const auto& r`, `auto r`, `const Case &c` — by anchoring on the
        # variable itself rather than on what precedes it. `[^:;]*?` keeps the
        # scan inside this loop's own header.
        if ($src->[$i] =~ /\bfor\s*\([^:;]*?\b\Q$loopvar\E\s*:\s*([A-Za-z_]\w*)/) {
            $arr = $1;
            last;
        }
    }
    return undef unless defined $arr;

    my $t = file_tables($source_rel);
    my ($table) = sort { $b->{line} <=> $a->{line} }
                  grep { $_->{var} eq $arr && $_->{line} <= $at } @{ $t->{arrays} };
    return undef unless $table;
    my ($struct) = sort { $b->{line} <=> $a->{line} }
                   grep { $_->{name} eq $table->{type} && $_->{line} <= $table->{line} }
                   @{ $t->{structs} };
    return undef unless $struct;
    my ($idx) = grep { $struct->{fields}[$_] eq $field } 0 .. $#{ $struct->{fields} };
    return undef unless defined $idx;
    return [ map { $_->[$idx] } @{ $table->{rows} } ];
}

# id -> description, for every row this source file asserts.
#
# Keyed the same way grep_row_ids() keys its IDs, and deliberately built from
# the SAME call spans the citation extractor walks, so a row cannot be found
# by one reader and missed by the other.
#
# An argument resolves to EITHER a string literal or a table column, and the
# two zip together, which is what covers the shared-assertion shape that owns
# most rows without a description:
#
#   check(r.id, r.what, ...)   both columns  -> one description per row, the
#                                               richest case (input_test)
#   check(r.id, "literal", ...) column + literal -> the loop's one sentence,
#                                               shared by its rows (mmu_test)
#   check(r.id, desc, ...)      column + variable -> undef, never a guess
sub row_descriptions {
    my ($source_rel) = @_;
    my $src = source_lines("$ROOT/$source_rel");
    my $pos = helper_arg_positions($source_rel);
    my $helper_re = join('|', map { quotemeta } sort { length($b) <=> length($a) }
                                                keys %$pos);
    return {} unless length $helper_re;
    my %desc;
    for my $i (0 .. $#$src) {
        my $head = code_prefix($src->[$i]);
        # The helper names come from THIS file's own definitions (base five
        # plus every wrapper adopted above), so a suite's private wrapper is
        # matched without being named here.
        next unless $head =~ /\b($helper_re)\s*\(/;
        my $helper = $1;
        my $p = $pos->{$helper} or next;
        # Same bounded span the citation scan uses, and bounded for the same
        # reason: an unbalanced paren inside a literal must not run away.
        my ($depth, $started, $text, $j) = (0, 0, '', $i);
        while ($j <= $#$src && $j < $i + 40) {
            $text .= $src->[$j];
            for my $ch (split //, $src->[$j]) {
                if    ($ch eq '(') { $depth++; $started = 1; }
                elsif ($ch eq ')') { $depth--; }
            }
            last if $started && $depth <= 0;
            $j++;
        }
        my @args = call_args($text, $helper_re);
        next unless @args > $p->{id};
        next unless defined $p->{desc} && @args > $p->{desc};

        # The ID side: one literal, or a whole table column.
        my $lit = literal_value($args[ $p->{id} ]);
        my @ids;
        if (defined $lit) {
            @ids = ($lit);
        } else {
            my $col = table_column($source_rel, $i + 1, $args[ $p->{id} ]) or next;
            @ids = map { literal_value($_) } @$col;
        }

        # The description side: one literal shared by every ID of the call, or
        # a column zipped to them one-for-one.
        my $dlit = literal_value($args[ $p->{desc} ]);
        my @ds;
        if (defined $dlit) {
            @ds = ($dlit) x scalar @ids;
        } else {
            my $col = table_column($source_rel, $i + 1, $args[ $p->{desc} ]) or next;
            # A length mismatch means the two references resolved to different
            # tables; pairing them anyway would attribute the wrong sentence to
            # every row after the first divergence.
            next unless @$col == @ids;
            @ds = map { literal_value($_) } @$col;
        }

        for my $k (0 .. $#ids) {
            my $id = $ids[$k];
            # Re-quoted before matching because $ID_LITERAL_RE spells the
            # quotes itself — it is written to run over raw source, and this is
            # the one caller holding an already-unquoted value.
            next unless defined $id && "\"$id\"" =~ /^$ID_LITERAL_RE$/;
            $desc{$id} //= $ds[$k] if defined $ds[$k];
        }
    }
    return \%desc;
}

# Repo-relative path of the plan doc backing a suite, or undef when it has
# none. Shared by the reader below and by the provenance label grep_citations
# hands out, so the two can never name different files for the same tier.
sub plan_doc_path {
    my ($source_rel) = @_;
    my $stem = $PLAN_DOC{ suite_for_source($source_rel) };
    return undef unless defined $stem;
    return "doc/testing/$stem-TEST-PLAN-DESIGN.md";
}

# Read plan-doc rows: "| ID | ... | ... zxnext.vhd:1234 ... |" -> citation.
my %PLAN_CACHE;
my %PLAN_DESC_CACHE;
my %PLAN_ROWS_CACHE;

# First-column header spellings that declare row IDs in a plan doc, plus the
# matrix's own. Exact strings: `Retracted ID` and `ULA plan ID (retired)` head
# ID-shaped columns too, and are deliberately absent.
my $EXCEPTIONS = 'test/traceability-exceptions.conf';
# Suites that ASSERT rows a section's plan doc owns, without being that
# section's companion (GH #196 phase 4).
#
# The plan-doc-derived pairing in emit_matrix() can only attach a `###`
# companion to the one `##` parent sharing its plan. This is the other shape:
# the asserting suite belongs to a DIFFERENT plan doc entirely. LoRes is the
# clearest case and not an accident of filing — LoRes is not a layer, it
# SUBSTITUTES the ULA-slot pixel (zxnext.vhd:6980), so its behaviour is
# naturally asserted in the compositor suite.
#
# This is STATUS-ONLY, exactly like the companion fallback: emit_section_rows()
# builds its row set from @sources alone, so a suite named here never imports
# its own rows into the borrowing section — they stay in its own.
#
# It could NOT be done by adding the suite to the borrowing @SUBSYS entry the
# way `## Audio` names its three: Audio's two extra suites have no section of
# their own, so that is their FIRST mention, while every suite below already
# owns a section and a second mention is refused by the accounting gate. That
# refusal is correct and is left alone.
my %EXTRA_STATUS_FALLBACK = (
    'LoRes'            => ['compositor_test', 'nextreg_integration_test'],
    # `sdcard_test` BOOT-SD-01/02 (mount round-trip, unmount mid-transfer),
    # `divmmc_test` PRI-01/02/04 (the DivMMC-over-MMU-over-Layer2 decode
    # priority chain) and `audio_port_dispatch_test` SD2-01/02 (NR 0x84 b2
    # suppressing the colliding 0x7FF1/0xDFF9/0x1FF1 paging writes) all assert
    # rows the MMU plan owns. The MMU plan already SAYS so for SD2-01 —
    # "PASS — audio_port_dispatch_test SD2-01" — which is what makes it
    # certain these are the same rows and not a name clash.
    'Memory/MMU'       => ['nextreg_integration_test', 'sdcard_test',
                           'divmmc_test', 'audio_port_dispatch_test'],
    # VT-GH181-01..06 are VideoTiming rows the Copper plan owns: the GH #181
    # fix was a Copper defect (WAIT hpos fed 28 MHz cycles instead of the
    # 7 MHz hc_ula), but what the rows assert is the hc_ula origin, which is
    # `videotiming_test`'s to prove.
    'Copper'           => ['nextreg_integration_test', 'videotiming_test'],
    'Tilemap'          => ['nextreg_integration_test'],
    'DivMMC+SPI'       => ['nextreg_integration_test'],
);

my @PLAN_ID_HEADERS = ('ID', '#', 'Test', 'Row', 'Row ID', 'Test ID', 'Test IDs');
sub plan_cites {
    my ($source_rel) = @_;
    my $stem = $PLAN_DOC{ suite_for_source($source_rel) };
    return {} unless defined $stem;
    return $PLAN_CACHE{$stem} if $PLAN_CACHE{$stem};
    my %cites;
    my $path = "$ROOT/" . plan_doc_path($source_rel);
    if (open(my $fh, '<', $path)) {
        while (my $line = <$fh>) {
            next unless $line =~ /^\|\s*`?([A-Za-z0-9][A-Za-z0-9._\-+]*)`?\s*\|/;
            my $id = $1;
            my $c  = cite_in($line);
            $cites{$id} //= $c if defined $c;
        }
        close $fh;
    }
    return $PLAN_CACHE{$stem} = \%cites;
}

# Plan-doc rows, in document order, as [id, description] pairs (GH #196 ph 2).
#
# ONLY tables that declare row IDs are read, and that is decided from the
# table's OWN header row — the lesson GH #192 already taught this script for
# the matrix's five table shapes, applied to plan docs, which have more.
#
# The set is MEASURED, not guessed: counting, across every plan doc, how many
# ID-shaped first cells each header spelling actually heads gives
# ID 2029 rows / # 165 / Test 82 / Row 30 / Row ID 29 for the row tables, and
# `ULA plan ID (retired)` 8 / `Retracted ID` 6 for the two maps that must stay
# out. Exact-string matching separates them; a first guess of just
# ID/Row ID/Test ID silently dropped 102 real plan rows.
#
# A permissive reader adopts tables that are not row tables at all. The
# floating-bus plan carries a retired-ID map headed
# `| ULA plan ID (retired) | Floating-bus plan ID |`, whose left column is an
# OLD name and whose right column is the NEW one; reading it emitted eight
# S10.* rows that nothing asserts, each carrying another row's ID ("FB-01") as
# its description. The accepted spellings are a CLOSED set, measured across
# every plan doc: `ID` (276 tables), `Row ID` (7), plus the matrix's own
# `Test ID`/`Test IDs`. `Retracted ID` and the retired map are excluded by
# construction rather than by a name list of exclusions.
sub plan_table_rows {
    my ($source_rel) = @_;
    my $stem = $PLAN_DOC{ suite_for_source($source_rel) };
    return [] unless defined $stem;
    return $PLAN_ROWS_CACHE{$stem} if $PLAN_ROWS_CACHE{$stem};

    my (@rows, %seen);
    my $path = "$ROOT/" . plan_doc_path($source_rel);
    if (open(my $fh, '<', $path)) {
        my $accept = 0;
        my $prev   = '';
        while (my $line = <$fh>) {
            chomp $line;
            if ($line !~ /^\|/) { $accept = 0; $prev = $line; next; }
            # The separator row identifies the line before it as the header.
            if ($line =~ /^\|[\s:-]+\|/ && $prev =~ /^\|/) {
                my @h = split_row_cells($prev);
                my $first = @h >= 2 ? $h[1] : '';
                $first =~ s/^\s+|\s+$//g;
                $first =~ s/^`|`$//g;
                $accept = (grep { $first eq $_ } @PLAN_ID_HEADERS) ? 1 : 0;
                $prev = $line;
                next;
            }
            $prev = $line;
            next unless $accept;
            my @cells = split_row_cells($line);
            next unless @cells >= 3;
            next if is_header_row(\@cells);
            my ($id, $d) = ($cells[1], $cells[2]);
            for ($id, $d) { s/^\s+|\s+$//g; s/^`|`$//g; }
            # The SAME id shape the source scan accepts, not a looser one.
            next unless "\"$id\"" =~ /^$ID_LITERAL_RE$/;
            next if $seen{$id}++;
            push @rows, [$id, (length $d && $d ne '—' && $d ne '-') ? $d : undef];
        }
        close $fh;
    }
    return $PLAN_ROWS_CACHE{$stem} = \@rows;
}

# The two views onto it, so they cannot disagree about which rows exist.
sub plan_rows { return [ map { $_->[0] } @{ plan_table_rows($_[0]) } ]; }

sub plan_descriptions {
    my ($source_rel) = @_;
    my $stem = $PLAN_DOC{ suite_for_source($source_rel) };
    return {} unless defined $stem;
    return $PLAN_DESC_CACHE{$stem} if $PLAN_DESC_CACHE{$stem};
    my %desc;
    for my $r (@{ plan_table_rows($source_rel) }) {
        $desc{ $r->[0] } //= $r->[1] if defined $r->[1];
    }
    return $PLAN_DESC_CACHE{$stem} = \%desc;
}

# id -> VHDL citation, from the four row-local tiers described above.
#
# $from, when given, is filled in parallel with the file each citation was
# literally read from: this source for the three row-local tiers, the plan
# doc for the plan tier. It is what the drift report is charged against
# (GH #126), and the plan-doc case is the whole reason it cannot be inferred
# from the caller's source list — `test/uart/uart_test.cpp` supplies INT-07's
# citation without ever mentioning INT-07, because the UART-I2C plan doc does.
#
# $noref, when given, is filled with the IDs whose OWN CALL carries a
# filename-less VHDL line reference and no resolvable citation — sub-class (b)
# of the GH #188 frozen report. Row-local by construction: only calls whose
# span contains this row's own ID literal are consulted, the same standard the
# `call` tier applies. It labels a report bucket; it never produces a citation.
sub grep_citations {
    my ($source_rel, $from, $at, $noref) = @_;
    my $abs = "$ROOT/$source_rel";
    open(my $fh, '<', $abs) or fatal("open $abs: $!");
    my @src = <$fh>;
    close $fh;

    # Span + citation of every check()/skip() helper call, in file order.
    #
    # ── A COMMENT IS NOT A CALL (GH #189 defect 2) ────────────────────
    #
    # The match runs on code_prefix(), not on the raw line: prose mentioning
    # `check()` or `skip()` is not an assertion, and taking it for one invents
    # a call out of English. It fired on 68 lines across the 41 traced sources
    # — 67 whole-line comments and one trailing comment
    # (`std::vector<SkipNote> g_skipped;  // populated by skip() for SKIP rows`,
    # `ula_integration_test.cpp:65`) — and a phantom call does real damage in
    # three directions:
    #
    #   - it SHADOWS the real call for the `next` tier, which reaches only to
    #     the FIRST following call. That is not hypothetical: #188's own
    #     explanatory comment in `layer2_test.cpp` sat one line above the
    #     `check("G10-01", ...)` it describes, so all 44 rows of the
    #     `deferred[]` table above it reached the COMMENT and not the call;
    #   - its citation, if the prose carries one, is published as the call's —
    #     a citation sourced from prose, which is the exact failure this
    #     extractor exists to refuse;
    #   - its paren scan is not balanced by real code, so an unmatched `(` in
    #     prose runs the span up to 40 lines forward and swallows whatever
    #     real ID literals lie in it.
    #
    # DECLARED RESIDUAL: `/* ... */` block comments are NOT stripped. Measured
    # across every traced source: zero helper-regex matches on a block-comment
    # line, so there is nothing to fix and a second comment grammar would be
    # untestable against this corpus. If one ever appears it behaves as the
    # pre-#189 code did.
    my @calls;
    for my $i (0 .. $#src) {
        next unless code_prefix($src[$i])
                    =~ /\b(?:check|check_pred|check_eq|skip|stub)\s*\(/;
        my ($depth, $started, $text, $j) = (0, 0, '', $i);
        # 40 lines is well past the longest real call and bounds the scan if
        # the paren balance is ever thrown off by a string literal.
        while ($j <= $#src && $j < $i + 40) {
            $text .= $src[$j];
            for my $ch (split //, $src[$j]) {
                if    ($ch eq '(') { $depth++; $started = 1; }
                elsif ($ch eq ')') { $depth--; }
            }
            last if $started && $depth <= 0;
            $j++;
        }
        # The scan is not string-literal-aware, so an unbalanced-looking paren
        # inside a description could run it past the real end of the call.
        # Today no call in any suite trips this; say so out loud if one ever
        # does, rather than silently mis-attributing its citation.
        warn "WARN: $source_rel:@{[$i+1]} — call did not close within 40 lines;"
           . " citation span may be wrong\n"
            if $started && $depth > 0;
        # Does this call name a row ID in its FIRST argument — the slot the ID
        # goes in? That, and not "quotes an ID-shaped literal anywhere", is
        # what "the call is some row's own assertion" means, and it is what
        # the `next` tier's refusal below tests. A shared loop assertion
        # passes the ID through a variable (`check(r.id, ...)`), so its first
        # argument holds no literal; a call that spells one out owns that row.
        # Reading the whole argument list instead would refuse a shared
        # assertion whose DESCRIPTION happens to quote an ID-shaped token —
        # a machine name like "ZXN-ISSUE2", a mode string — which is the
        # over-refusal SELF-157 exists to catch.
        push @calls, { s => $i, e => $j, cite => cite_in($text),
                       owns_row => (first_arg($text) =~ /$ID_LITERAL_RE/ ? 1 : 0),
                       nofile => ($text =~ /$VHDL_NOFILE_RE/ ? 1 : 0) };
    }

    # The first line each ID appears on, OUTSIDE a comment. Collected in a pass
    # of its own because the `named` tier below needs the complete set to tell a
    # row ID from a prose hyphenation (GH #147): `VHDL-correct` and `SYM-hyst`
    # match the bare-ID shape, and counting them as rows would make the block
    # rule fire on blocks that name exactly one.
    my %id_line;
    for (my $i = 0; $i <= $#src; $i++) {
        # `set_group("ID")` is a group BANNER, not the row's assertion, and it
        # is dropped here for the same reason grep_row_ids() drops it — one
        # reader, one rule. Without the mask the banner is the ID's FIRST
        # non-comment occurrence, so the call-span lookup below finds no call
        # owning it and the row silently falls through to the `named` or
        # `next` tier while its own check() sat a few lines lower carrying the
        # right citation. Measured over every suite: two rows, `LR-163` and
        # `LR-164`, and both were wrong — LR-164's own call cites
        # `zxula.vhd:573` and the file header's block comment (which names both
        # rows) was answering `zxnext.vhd:6603-6631` for it. That is the
        # borrowed-citation failure this extractor is built to refuse, arriving
        # through the one door left open. (GH #144)
        next if $src[$i] =~ m{^\s*//};
        my $line = $src[$i];
        $line =~ s/$SET_GROUP_RE/set_group(/g;
        while ($line =~ /$ID_LITERAL_RE/g) { push @{ $id_line{$1} }, $i; }
    }

    # ── The `named` tier, and where it REFUSES (GH #147) ──────────────
    #
    # A comment block that names a row ID explicitly is row-local evidence, and
    # that explicit mention is the whole reason this tier survived when the
    # banner-comment and nearest-comment tiers were rejected. But the block was
    # taken as ONE scope: the first citation anywhere in it was handed to every
    # ID named anywhere in it, and `//=` locked that in. Whenever a block spans
    # several VHDL topics that is the rejected banner tier, arriving through the
    # surviving door.
    #
    # `BP-06` is the measured instance. Its file's top banner lists sixteen row
    # IDs and mentions `zxnext.vhd:5179` (an NR 0x08 DAC-enable fact) first, so
    # BP-06 published :5179 instead of the port 0xFE dispatch its assertion is
    # about — while BP-01, whose own check() carries a citation, was shielded by
    # the higher-precedence call tier.
    #
    # BLAST RADIUS, measured across every traced source before choosing a rule:
    # 283 rows take their citation from this tier; 39 of those come from a block
    # naming more than one REAL row ID; 10 of those come from a block that also
    # offers more than one citation.
    #
    # THE RULE (GH #147): refuse the block when it names more than one real row
    # ID AND offers more than one citation. Then there is no row-local basis for
    # deciding which citation belongs to which row, and taking the first is
    # exactly the misattribution above. One ID with several citations is kept —
    # they all belong to that row. Several IDs with ONE citation are kept — the
    # block has only one answer to give. Ten rows lose a citation and read `—`,
    # which is honest and recoverable by citing the VHDL in the row's own
    # check() call, the tier that outranks this one.
    #
    # ── ...AND THAT IS STILL NOT ROW-LOCALITY (GH #184) ───────────────
    #
    # The rule above guards against cross-topic SHARING. It does not guard
    # against a block that merely MENTIONS a row in passing and lends it a
    # citation about something else — the one-ID-many-citations shape it
    # deliberately kept. The counter-example, built by #147's own reviewer:
    # `video_panel_test.cpp`'s NR 0x43 palette-SELECTOR block says "Sibling of
    # DVP-05 (palette CONTENT replay) for the palette SELECTOR" and cites
    # `zxnext.vhd:5391-5392`, the NR 0x43 selector latches. DVP-05 tests
    # palette CONTENT and has no business with those lines; it was the only
    # real ID in the block (the block's own subject, `DVP-PALSEL`, is a
    # `set_group()` banner and therefore masked out), so nothing tripped.
    #
    # SECOND RULE: the block answers for an ID only when the ID HEADS a line of
    # it — see heading_ids_in(). Mentioning a row inside a sentence is a
    # cross-reference; opening a line with it is a claim that the line is about
    # it. Structural, not linguistic: the rejected alternative was a phrase list
    # ("sibling of", "see", "cf.", "unlike"), which is a regex consuming
    # English and refuses zero of the corpus's rows, i.e. cannot be tested.
    #
    # BLAST RADIUS, measured over the 41 traced sources before choosing, by
    # diffing the whole computed citation set before against after:
    # 277 rows take this tier; 254 already name the row at the head of a line.
    # The diff is 6 rows LOST, 0 gained, 15 CHANGED.
    #
    # The 6 losses are all borrowings. `G9.RO-02`/`G9.MI-04` were taking
    # `sprites.vhd:817-819` out of a "G9.RO-03 / G9.RO-04 — COVERED ELSEWHERE"
    # note about two OTHER rows (their own descriptions say 813 and 811,813,
    # which is where `spr_x_mirr_eff` actually lives); `STEN-20`, `UTB-40/41`
    # and `G56-CR-NR06-04` likewise came out of notes whose subject is a
    # different row.
    #
    # The 15 changes are the point of the rule, and every one lands on the
    # block that heads with the row: `REG-08` moves off the REG-05 block's
    # `zxnext.vhd:2625` onto its own `:2593`; `ARB-02` off ARB-01's
    # `:4769,4775-4777` onto its own `:4769` (the `elsif cpu_req='1' and
    # copper_req='0'` hold clause it asserts); `RST-01/02` off `:4610-4618`
    # onto `:4611-4618`, the eight MMU reset assignments without the `if
    # reset='1'` guard line; `G4-02..04` off a bare `layer2.vhd` with no line
    # numbers at all onto `zxnext.vhd:5249`/`:5278-5281`; `JCAL-03` off a bare
    # `membrane_stick.vhd` onto `:172-183`.
    #
    # NOTHING here reads `—` that did not read `—` before, beyond those 6.
    # Coverage was re-routed, not lost.
    #
    # DECLARED RESIDUAL — the shape this rule still misattributes on: a
    # cross-reference that is not inside a sentence but at the head of a line,
    # in either of the two shapes the corpus writes headings in. On its own
    # line:
    #
    #     // ── PALSEL-01: the palette SELECTOR path.
    #     // CONTENT-01 is the sibling row for palette CONTENT, tested elsewhere.
    #     // VHDL fixture_a.vhd:10 (the selector latches).
    #
    # or joined into a heading by the `and` / `/` convention used throughout:
    #
    #     // PALSEL-01 and CONTENT-01: the SELECTOR path — CONTENT-01 is the
    #     // sibling row and is tested elsewhere.  VHDL fixture_a.vhd:10.
    #
    # Both hand `CONTENT-01` a citation that belongs to PALSEL-01. Both built
    # and confirmed live against this code, not assumed. The second reaches
    # further than the first: heading joins are ordinary style here, so this is
    # not an exotic shape.
    #
    # TIGHTENING WAS TRIED AND REJECTED, but NOT on a coverage argument — that
    # argument was measured and does not hold. Requiring a delimiter
    # (`:`, `—`, `(`, `,`, end-of-line) after the heading ID group costs only
    # 2 rows (`V11-NMP-02/03`) and changes 3, and it does NOT disturb
    # `RAM-BK-03`, which computes `zxnext.vhd:4886` identically either way.
    # (An earlier draft of this comment claimed 49 lost and `RAM-BK-03` moving.
    # Both were artefacts of the measuring patch, not of the rule: it deleted
    # the whole block's heading set rather than the one line's, and spelled the
    # em dash `\x{2014}` against byte strings, so every em-dash-delimited
    # heading — the corpus's most common — was refused. Re-measured after an
    # independent reviewer failed to reproduce it.)
    #
    # It is rejected because it refuses GH #147's own accept case: SELF-121
    # (`ONE-01 and ONE-02 share a single fact:` — the joined group is followed
    # by a bare verb), SELF-123 and SELF-135 all fail under it, verified by
    # running the selftest with the rule applied. Breaking a deliberate,
    # documented accept case to refuse one constructed shape is the wrong
    # trade, so the shape is declined and written down instead. The residual is
    # strictly narrower than what #184 closed: the block must ALSO satisfy the
    # #147 rule to reach this point.
    my %named;
    {
        my $i = 0;
        while ($i <= $#src) {
            if ($src[$i] !~ m{^\s*//}) { $i++; next; }
            my ($j, $text) = ($i, '');
            while ($j <= $#src && $src[$j] =~ m{^\s*//}) { $text .= $src[$j]; $j++; }
            $i = $j;
            my $list = cite_list($text);
            next unless @$list;
            my ($lit, $all) = bare_ids_in($text);
            my $head = heading_ids_in($text);
            my @real = grep { exists $id_line{$_} } @$all;
            next if @real > 1 && @$list > 1;
            my @ids = grep { $head->{$_} } @$lit;
            next unless @ids;
            my $c = join(', ', @$list);
            $named{$_} //= $c for @ids;
        }
    }

    my $plan      = plan_cites($source_rel);
    my $plan_path = plan_doc_path($source_rel);
    my %cites;
    for my $tid (keys %id_line) {
        my $L = $id_line{$tid}[0];
        my ($cite, $owns_call);
        # EVERY call carrying this row's own ID is candidate evidence, not just
        # the first. A row is routinely asserted twice — once in the real
        # `check()` and once in a fixture-init guard that reuses the same ID
        # (`check("MF-MUX-07", "Emulator init failed", false, ...)`) — and the
        # guard is textually FIRST. Taking only the first occurrence let the
        # guard, which carries no citation, shadow the assertion that does, so
        # 21 Multiface rows fell through to their banner comment instead.
        # `MF-MUX-07` was the worst: it published `multiface.vhd:64,103` (:64
        # is a bare port declaration) while its own call names the gate at
        # `zxnext.vhd:2816`.
        #
        # This is not a loosening: every candidate literally contains the row's
        # own ID, which is the same row-local standard the `call` tier always
        # applied. Order is preserved, so the first CITED call wins, and
        # $owns_call still latches on any owning call — the `next` tier stays
        # fenced off for a row that has a call but no citation in it.
        # $at records WHICH call won, so `Test file:line` can point at the
        # same assertion the citation came from. Without it the two halves of
        # one row disagreed: line_for() resolved from the FIRST occurrence, so
        # 22 Multiface rows named the dormant fixture-init guard — a
        # `check(id, "Emulator init failed", false, ...)` that never executes —
        # while the citation beside it came from the real assertion six lines
        # down. Filled ONLY for a cited-call win, never for the `named`,
        # `next` or `plan` tiers: those have no line in this file to point at,
        # and inventing one is how a column starts lying. (GH #144)
        my $cite_at;
        OCC: for my $occ (@{ $id_line{$tid} }) {
            for my $c (@calls) {
                if ($c->{s} <= $occ && $occ <= $c->{e}) {
                    $owns_call = 1;
                    # GH #188 sub-class (b): this row's own call names line
                    # numbers with no filename. Recorded even when a later
                    # occurrence turns out to be cited — the caller only asks
                    # once nothing was computed for the row at all.
                    $noref->{$tid} = 1 if $noref && $c->{nofile}
                                          && !defined $c->{cite};
                    if (defined $c->{cite}) {
                        $cite    = $c->{cite};
                        $cite_at = $c->{s} + 1;
                        last OCC;
                    }
                    last;
                }
            }
        }
        $cite //= $named{$tid};
        # The `next` tier applies ONLY when the ID literal has no call of its
        # own — the table-driven signature, where the ID sits in an
        # initialiser and the assertion is in the loop below it. "The row's
        # own call exists but embeds no citation" is a different fact, and
        # must NOT fall through: the following call belongs to the NEXT row,
        # and borrowing from it publishes a plausible-but-wrong citation.
        # (Caught in review, 2026-07-20: CT-INT-03 — a harness-plumbing check
        # with no VHDL basis at all — had been given zxula.vhd:582-595,
        # lifted from an unrelated check ~100 lines further down.)
        #
        # ── BOUNDING THE REACH (GH #189 defect 1) ─────────────────────
        #
        # The hazard, measured 2026-07-31 under GH #188: the reach was
        # UNBOUNDED, and the table-driven signature this tier serves does not
        # actually require a loop with a check() in it. `layer2_test.cpp`'s
        # log_deferred() holds a 44-entry `deferred[]` table whose loop pushes
        # a TestResult directly and calls nothing — so all 44 rows resolved to
        # the first check() ANYWHERE below the table, 37 to 54 lines away and
        # in a different function. That call was uncited, so
        # they fell through to the plan doc and nobody noticed; putting a
        # citation in it handed ONE row's VHDL lines to all 44 at once.
        #
        # THE RULE: the tier refuses when the following call SPELLS A ROW ID
        # OUT IN ITS FIRST ARGUMENT. The signature this tier exists for passes
        # the ID through a variable — `check(r.id, ...)` under a
        # `struct Row rows[] = {...}` — so the shared assertion's ID slot
        # holds no literal. A call that DOES spell one out is that row's own
        # assertion, and borrowing from it is precisely the SELF-04 defect
        # (`BARE-01` taking `OTHER-01`'s citation) at a distance.
        #
        # Refusal is a STOP, not a skip: the first following call is the only
        # candidate the table-driven shape can offer. Scanning past it to a
        # later call would be a widening, not a bound.
        #
        # HOW TO RE-DERIVE EVERY NUMBER BELOW. Instrument THIS arm: push
        # { id => $tid, dist => $c->{s} - $L, cite => $c->{cite},
        #   refused => $c->{owns_row} } for every row it fires on (before the
        # `last`, so refusals are recorded too), then call grep_citations()
        # over the sources resolve_subsys(\@SUBSYS) returns. Every figure in
        # this comment came out of that instrumentation and none was derived
        # by hand — three that were, in review, were all wrong.
        #
        # BLAST RADIUS, measured over all 41 traced sources before choosing:
        # 248 rows reach this tier; 63 of them reach a CITED call and publish
        # today, 185 reach an uncited one and fall through. The rule refuses
        # 135 of the 185 and **0 of the 63**. Nothing that reads a citation
        # today loses one; 135 latent leaks — every one of which would have
        # published the moment somebody cited that call — are closed. The four
        # big fan-in groups are all in the 135: esp_socket 52 + 9 + 1,
        # layer2 44, compositor 17, input 12.
        #
        # The 50 left are the tier's designed shape and are correct to keep:
        # `struct Row rows[] = {...}; for (...) check(row.id, ...)` in
        # port/contention/input/compositor. Many-to-one is right there — the
        # loop's call IS every listed row's assertion.
        #
        # WHY THE ALTERNATIVES LOSE:
        #   - A LINE-DISTANCE CAP cannot separate the two populations, because
        #     the populations OVERLAP. Of the 135 rows the chosen rule
        #     refuses, 28 reach no further than the furthest CITED row does —
        #     27 land inside the cited span and one below it — so no threshold
        #     keeps all 63 cited rows and also closes those 28. Distance is
        #     only a proxy for the defect, and the proxy fails on a fifth of
        #     what has to be refused. The spans, for the record: cited 4..23,
        #     the layer2 leak 37..54, the esp_socket leak (all 62 rows across
        #     its three groups) 3..90.
        #   - A FAN-IN CAP is refuted by the corpus outright, not merely made
        #     fragile by it. The largest CITED group is `ula_test`'s S1 table
        #     at 12 rows; the `input_test.cpp:4684` LEAK is ALSO 12. They are
        #     TIED, so no threshold on fan-in can ever separate them — every
        #     cap that closes that leak takes a correct, cited 12-row table
        #     with it. Fan-in is the tier's own signature, not a symptom of
        #     the defect. (The free thresholds start at >12, not >20 as an
        #     earlier draft claimed: >12..>16 cost no cited row and close 113
        #     of the 135, >17 and above close only 96 by missing compositor's
        #     17. And a cap that closes compositor's 17-row leak refuses a
        #     legitimate 17-entry table for exactly the same reason.)
        #   - REFUSING TO CROSS AN INTERVENING ID LITERAL guts the tier, and
        #     does it arbitrarily: a table's entries sit between its first
        #     entry and the loop by construction, so `S1.01` is separated from
        #     its own call by `S1.02..S1.12`. 54 of the 63 cited rows die. The
        #     9 survivors are exactly the LAST entry of each of the 9 cited
        #     groups — the only entry with nothing after it — so the rule
        #     would keep a row for its position in an initialiser rather than
        #     for any evidence about it.
        #   - REFUSING TO CROSS A COLUMN-0 `}` (a function boundary) was
        #     measured and is a strict SUBSET of the chosen rule: it closes
        #     the 44 layer2 rows and nothing else, because every other leak
        #     lives in the same function as the call it reaches.
        #
        # DECLARED RESIDUAL — the shape this rule still gets wrong: a table
        # with no call of its own, followed by a DIFFERENT table's shared loop
        # assertion. The reached call passes ITS id through a variable, so it
        # spells no row out, the refusal does not fire, and the first table
        # borrows the second table's citation:
        #
        #     const char* deferred[] = { "EVADE-01", "EVADE-02" };
        #     for (const char* id : deferred) { record(id); }
        #
        #     const Row other[] = { {"EVADE-TAB-01"} };
        #     for (const Row& r : other) {
        #         check(r.id, "... VHDL zxnext.vhd:800", cond, detail);
        #     }
        #
        # Built and confirmed live against this code, not assumed: all three
        # rows publish `zxnext.vhd:800`. It does NOT occur in the corpus — all
        # 50 rows the rule still accepts were read individually and every one
        # is an ID initialiser followed by ITS OWN loop.
        #
        # TIGHTENING WAS TRIED AND REJECTED. Refusing to cross an intervening
        # ID literal cannot see it: `EVADE-TAB-01` sits in an initialiser too,
        # so it takes this same tier and reaches the same call, and no
        # "crosses a row that resolves elsewhere" test fires. Refusing to
        # cross ANY intervening `}` (at any indentation) does catch it — and
        # takes the entire tier with it: measured, it refuses ALL 63 cited
        # rows and ALL 50 the rule accepts, because a table's initialiser ends
        # in `};` and that brace is in every span. (The COLUMN-0 form is the
        # harmless one, and it is the strict subset described above.)
        # Destroying the tier to catch one constructed shape is the wrong
        # trade (the same call GH #184 made), so the shape is declined and
        # written down. Telling the two apart needs the loop's iterand, i.e.
        # real C++ parsing, which this extractor deliberately does not do.
        if (!defined $cite && !$owns_call) {
            for my $c (@calls) {
                next unless $c->{s} > $L;
                last if $c->{owns_row};
                $cite = $c->{cite};
                last;
            }
        }
        # Everything above is row-local evidence read out of this file; only
        # the plan tier comes from somewhere else, and only it is charged
        # elsewhere.
        my $prov = $source_rel;
        if (!defined $cite && defined $plan->{$tid}) {
            $cite = $plan->{$tid};
            $prov = $plan_path;
        }
        next unless defined $cite;
        $cites{$tid} = $cite;
        $from->{$tid} //= $prov if $from;
        $at->{$tid}   //= $cite_at if $at && defined $cite_at;
    }
    # Rows the plan doc cites but no test source mentions stay resolvable:
    # `missing` status still deserves its citation.
    for my $tid (keys %$plan) {
        next if defined $cites{$tid};
        $cites{$tid} = $plan->{$tid};
        $from->{$tid} //= $plan_path if $from;
    }
    return \%cites;
}

# ── The `unrecorded` direction (GH #117) ──────────────────────────────
#
# grep_source() above answers "may the matrix name this ID?"; this one
# answers the opposite question, "does the test source assert a row the
# matrix does not list?". Both must be precise, and about the same thing:
#
#   - whole-line `//` comments are skipped, by the shared source_lines()
#     reader — a quoted ID inside prose is a cross-reference and a quoted ID
#     inside a disabled `check()` is not an assertion at all (measured: 9
#     such phrases across the 28 suites, e.g. `// "ROM3-only" (NR 0xB9
#     bit=0) activates ...`). Until GH #119 only this scanner applied the
#     rule, which is how matrix row `7.3` read `pass`;
#   - `set_group()` arguments are dropped here only (group banner, not a
#     row). grep_source() stays loose about them on purpose: a false
#     positive there is harmless because it only matters for IDs the matrix
#     already lists, whereas a false positive here is an accusation.
#
# Everything surviving both filters is a row: it is either the first
# argument of an assertion helper (`check`, `check_pred`, a suite-local
# wrapper lambda, `skip`, `stub`) or a table-driven initialiser entry.
sub grep_row_ids {
    my ($source_rel) = @_;
    my $src = source_lines("$ROOT/$source_rel");

    my %ids;
    for my $lineno (1 .. scalar @$src) {
        my $line = $src->[$lineno - 1];
        $line =~ s/$SET_GROUP_RE/set_group(/g;
        while ($line =~ /$ID_LITERAL_RE/g) {
            $ids{$1} //= $lineno;
        }
    }
    return \%ids;
}

# Every ID the document records over `[$from, $to)` — the whole file when
# the bounds are omitted. Deliberately not restricted to the 5-column status
# tables: an ID listed under "Extra coverage (not in plan)" (4 columns, no
# Status) is recorded just as much as a plan row.
#
# The generated Summary block is skipped wherever it falls. It is a table
# too, and its Section column is full of section names and companion source
# files; harvesting those would let the previous run's output count as
# evidence for the next one — a generator validated against its own past
# output can never catch its own bad data.
#
# The bounds are what makes recording SECTION-SCOPED (GH #118). Asked
# globally, an ID string used by two subsystems counted as recorded for
# both: `SD-16..SD-23` are asserted in `sdcard_test.cpp` and recorded
# nowhere in the SD Card section — the Audio section's identically-named
# rows were vouching for them. 29 such rows were hidden this way, across
# five subsystems.

# Split a matrix row into its Markdown cells.
#
# A literal pipe inside a cell is written `\|` — that is how Markdown renders
# one, and several Descriptions need it (`RESET_HARD\|RESET_SOFT`). Splitting
# on a bare `|` breaks the row AT the escape, so every later cell shifts one
# column right and the rewrite lands VHDL/Status/Test-file in the wrong
# places. That is not hypothetical: it is how `RW-01` and `SR-05` came to
# carry `pass` in their VHDL column and grow a sixth column (GH #157).
#
# So split on UNESCAPED pipes only. The escape stays inside the cell, the
# column count is the one the table header declares, and `join('|', @cells)`
# puts the row back byte-identical — which is what makes the read side
# round-trip rather than merely stop corrupting.
sub split_row_cells {
    my ($line) = @_;
    return split(/(?<!\\)\|/, $line, -1);
}

# Is this table row the HEADER row? Both spellings the document uses, and
# nothing else — a data row whose ID happened to start with "Test ID" would
# have to BE that string exactly.
sub is_header_row {
    my ($cells) = @_;
    my $t = $cells->[1] // '';
    $t =~ s/^\s+|\s+$//g;
    return ($t eq 'Test ID' || $t eq 'Test IDs') ? 1 : 0;
}

# Which column is which, read from the table's OWN header row instead of
# assumed by position. (GH #192)
#
# Position was assumed for two years, as `>= 7` cells and hard indices 3/4/5,
# and the assumption is false: the document carries FIVE table shapes, and the
# 4-column "Extra coverage (not in plan)" one splits into SIX cells, so every
# row of all eight such tables fell through the gate and was never refreshed —
# 85 rows whose `Test file:line` had rotted where no re-run could correct it.
#
# Returns (cite, status, loc) column indices, each undef when the table has no
# such column. Two spellings of the location header are in use (`Test file:line`
# and, in two sections, `Test file`), so it is matched by prefix; the other two
# are matched exactly, because `Contract/source reference` and `RTL/source
# reference` are deliberately NOT VHDL citations and must not be read as one.
sub table_columns {
    my ($hdr) = @_;
    my ($cite, $status, $loc);
    for my $c (1 .. $#$hdr - 1) {
        my $h = $hdr->[$c];
        $h =~ s/^\s+|\s+$//g;
        $cite   = $c if !defined $cite   && $h eq 'VHDL file:line';
        $status = $c if !defined $status && $h eq 'Status';
        $loc    = $c if !defined $loc    && $h =~ /^Test file/;
    }
    return ($cite, $status, $loc);
}

sub matrix_row_ids {
    my ($lines, $from, $to) = @_;
    $from //= 0;
    $to   //= scalar @$lines;
    my %ids;
    my $skipping = 0;
    for my $i ($from .. $to - 1) {
        my $line = $lines->[$i];
        if (!$skipping && index($line, $SUMMARY_BEGIN) == 0) { $skipping = 1; next; }
        if ($skipping) { $skipping = 0 if index($line, $SUMMARY_END) == 0; next; }
        next unless $line =~ /^\|/;
        my @cells = split_row_cells($line);
        next unless scalar @cells >= 5;
        my $tid = $cells[1];
        $tid =~ s/^\s+|\s+$//g;
        $tid =~ s/^`|`$//g;
        next if $tid eq '' || $tid =~ /^Test ID/ || $tid =~ /^[-:\s]+$/;
        $ids{$tid} = 1;
    }
    return \%ids;
}

# The scope a source file's rows are judged against: its owning top-level
# `## ` subsystem section, from that header to the next `## ` line.
#
# The unit is the SUBSYSTEM, not the @SUBSYS entry. A `###  Companion
# integration suite` sub-section is nested inside its parent `##` and its
# rows are part of the same subsystem's coverage story — several are
# recorded in the parent's main table (`PFF-G108-01..03` under `## Compositor`,
# `ULA-INT-04/06` under `## CTC+Interrupts`, `INT-07` under `## UART+I2C/RTC`).
# Scoping to the @SUBSYS entry instead would report those 12 as "the matrix
# does not list this row" when the matrix plainly does, and a reader acting
# on that would add duplicates. A false accusation is as expensive as a
# silent omission — the whole point of GH #117. So the companion is judged
# against its parent, and the cross-SUBSYSTEM collision, which is the actual
# defect, is what gets caught.
sub subsystem_span {
    my ($lines, $idx) = @_;
    my $start = $idx;
    if ($lines->[$idx] !~ /^## /) {
        for (my $i = $idx; $i >= 0; $i--) {
            if ($lines->[$i] =~ /^## /) { $start = $i; last; }
        }
    }
    my $stop = scalar @$lines;
    for my $i ($start + 1 .. $#$lines) {
        if ($lines->[$i] =~ /^## /) { $stop = $i; last; }
    }
    return ($start + 1, $stop);
}

# Mirror of resolve_ids()'s sub-letter aliasing, in the other direction: a
# source row `MMU-01a` is recorded by matrix row `MMU-01`.
#
# GH #118 triaged all 102 IDs this was hiding. 90 are decompositions of the
# parent plan row — `G2-01a/b/c` are three sample coordinates proving one
# "256x192 row-major address" row, `AY-50a/b` are the "period 0 **or 1**"
# the parent title already names. 12 were distinct assertions and now have
# rows of their own (`NA-01b`, `NA-01c`, `NR-12a`, `NR-12b`, `HK-07b`,
# `MF-G162-01b`, `REG-01b`, `REG-02b`, `REG-03a/b/c`, `S5.10c`), joining
# the earlier `FB-04b` / `IORQ-02b` / `IORQ-02c`.
#
# The aliasing is KEPT, for a reason independent of that 90/12 split:
# resolve_ids() uses the SAME mapping in the other direction to compute a
# parent row's Status from its sub-rows. Drop it here and the tool holds two
# contradictory opinions about the same string — row `X-01` would read
# `pass` *because* `X-01a` proves it, while `X-01a` was simultaneously
# reported as recorded nowhere. That is precisely the half-of-the-tool-
# disagrees-with-the-other-half defect GH #119 removed, and re-introducing
# it to close a blind spot would be a bad trade.
#
# What closes the blind spot instead is visibility: every ID recorded ONLY
# by this aliasing is now listed on every run (see recorded_only_by_alias()
# and the ALIASED report in main()), so the next distinct sub-letter row
# shows up in a list a human reads rather than waiting for someone to think
# of asking. The set is a report, not a gate — 90 of these are legitimate
# and failing the run on them would only teach people to ignore it.
sub matrix_records {
    my ($id, $recorded) = @_;
    return 1 if $recorded->{$id};
    for my $s (@SUBLETTERS) {
        next unless length($id) > length($s) && substr($id, -length($s)) eq $s;
        return 1 if $recorded->{ substr($id, 0, length($id) - length($s)) };
    }
    return 0;
}

# True when the sub-letter aliasing is the ONLY thing recording this ID —
# the matrix lists `X-01` but not `X-01b`. This is the blind spot made
# visible; see matrix_records() for why the aliasing itself stays. (GH #118)
sub recorded_only_by_alias {
    my ($id, $recorded) = @_;
    return 0 if $recorded->{$id};
    return matrix_records($id, $recorded) ? 1 : 0;
}

# [name, rows] for every suite `test/unit-tests.conf` declares, in file order.
# The `?` prefix marks a GUI-gated suite and is not part of the name.
sub declared_suites {
    my $conf = "$ROOT/test/unit-tests.conf";
    open(my $fh, '<', $conf) or fatal("open $conf: $!");
    my @out;
    while (my $line = <$fh>) {
        next if $line =~ /^\s*#/ || $line !~ /\S/;
        my ($name, $rows) = split(' ', $line);
        $name =~ s/^\?//;
        push @out, [$name, $rows + 0];
    }
    close $fh;
    return \@out;
}

# (suite count, pinned row count) declared in test/unit-tests.conf — the
# project's own claim about how much it tests, and the denominator the head
# Summary compares itself against.
sub declared_totals {
    my $declared = declared_suites();
    my $rows = 0;
    $rows += $_->[1] for @$declared;
    return (scalar @$declared, $rows);
}

# ── The accounting gate (GH #144) ─────────────────────────────────────
#
# Every declared suite must be TRACED (named in @SUBSYS) or TOMBSTONED (named
# in %NO_MATRIX_SECTION with a reason), and nothing may be both, neither, or
# accounted for without being declared. Returns the list of complaints; empty
# means accounted for.
#
# This is set algebra over three lists and takes them as arguments so it can
# be asserted directly, without a repository around it.
#
# Why it is a REFUSAL and not a report: the predecessor was a warning line
# inside a document that only regenerates at version-bump time, and it had
# been firing for 21+ suites since before the tooling was finished. A
# saturated warning is indistinguishable from no warning — the 51st name on a
# line that already has fifty is not a signal. `test/run-unit-tests.sh` had
# already learnt this and refuses to run at all when its manifest and CMake
# disagree; this is the same posture for the same class of drift.
sub suite_accounting {
    my ($declared, $subsys, $tombstones) = @_;

    my (%declared_rows, @complaints);
    for my $d (@$declared) {
        push @complaints, "$d->[0]: declared twice in test/unit-tests.conf"
            if exists $declared_rows{ $d->[0] };
        $declared_rows{ $d->[0] } = $d->[1];
    }

    my %traced;
    for my $entry (@$subsys) {
        for my $suite (as_list($entry->[1])) {
            push @complaints, "$suite: traced by two \@SUBSYS entries"
                if exists $traced{$suite};
            $traced{$suite} = $entry->[0];
        }
    }

    for my $suite (sort keys %traced) {
        push @complaints, "$suite: traced by \@SUBSYS ($traced{$suite}) but "
                        . "not declared in test/unit-tests.conf"
            unless exists $declared_rows{$suite};
        push @complaints, "$suite: both traced by \@SUBSYS and tombstoned in "
                        . "%NO_MATRIX_SECTION — it must be exactly one"
            if exists $tombstones->{$suite};
    }

    for my $suite (sort keys %$tombstones) {
        push @complaints, "$suite: tombstoned in %NO_MATRIX_SECTION but not "
                        . "declared in test/unit-tests.conf"
            unless exists $declared_rows{$suite};
        push @complaints, "$suite: tombstoned with an empty reason"
            unless defined $tombstones->{$suite}
                   && $tombstones->{$suite} =~ /\S/;
    }

    for my $d (@$declared) {
        my ($suite, $rows) = @$d;
        next if exists $traced{$suite} || exists $tombstones->{$suite};
        push @complaints,
             "$suite ($rows rows): declared in test/unit-tests.conf but "
           . "neither traced by \@SUBSYS nor tombstoned in %NO_MATRIX_SECTION";
    }

    return \@complaints;
}

# Tombstoned suites with their declared row counts and reasons, for the head
# Summary. Ordered as the manifest declares them.
sub tombstoned_suites {
    my ($declared) = @_;
    return [ map  { [$_->[0], $_->[1], $NO_MATRIX_SECTION{ $_->[0] }] }
             grep { exists $NO_MATRIX_SECTION{ $_->[0] } } @$declared ];
}

# @SUBSYS, with the derived halves filled in: [header, [binaries], [sources]],
# which is the shape everything downstream already consumed. Also fills
# %SUITE_OF_SRC, so the suite-keyed editorial tables resolve without a name
# convention.
#
# A traced suite whose source CMake does not declare, or declares as a file
# that is not on disk, is returned as a complaint rather than resolved: the
# alternative is source_lines() dying halfway through with the matrix already
# half-rewritten.
sub resolve_subsys {
    my ($subsys) = @_;
    my $src_of = cmake_sources();
    my (@resolved, @complaints);
    for my $entry (@$subsys) {
        my ($header, $suites) = @$entry;
        my (@bins, @srcs);
        for my $suite (as_list($suites)) {
            my $src = $src_of->{$suite};
            if (!defined $src) {
                push @complaints, "$suite: traced by \@SUBSYS but no "
                                . "add_executable($suite ...) found in any "
                                . "CMakeLists.txt";
                next;
            }
            if (!-f "$ROOT/$src") {
                push @complaints, "$suite: CMake builds it from '$src', which "
                                . "does not exist under $ROOT";
                next;
            }
            $SUITE_OF_SRC{$src} = $suite;
            push @bins, "build/test/$suite";
            push @srcs, $src;
        }
        push @resolved, [$header, \@bins, \@srcs];
    }
    return (\@resolved, \@complaints);
}

sub resolve_ids {
    my ($tid, $checks, $skips) = @_;
    return [$tid] if exists $checks->{$tid} || exists $skips->{$tid};
    my @variants;
    for my $s (@SUBLETTERS) {
        my $v = "$tid$s";
        push @variants, $v if exists $checks->{$v} || exists $skips->{$v};
    }
    return \@variants;
}

sub status_for {
    my ($tid, $fails, $checks, $skips) = @_;
    my $resolved = resolve_ids($tid, $checks, $skips);
    return 'missing' unless @$resolved;

    my $any_fail = 0;
    for my $r (@$resolved) { $any_fail = 1, last if exists $fails->{$r}; }
    return 'fail' if $any_fail;

    my $any_skip = 0;
    my $all_skip = 1;
    for my $r (@$resolved) {
        if (exists $skips->{$r}) { $any_skip = 1; }
        else                     { $all_skip = 0; }
    }
    return 'skip' if $any_skip && $all_skip;
    return 'pass';
}

# Returns (source_rel, line) — the source is carried because a section can
# be backed by several suites and the row must point at the one that
# actually holds the assertion.
sub line_for {
    my ($tid, $checks, $skips, $where, $cite_line) = @_;
    my $resolved = resolve_ids($tid, $checks, $skips);
    for my $r (@$resolved) {
        # The line of the call whose CITATION was published, when there is
        # one. %checks/%skips hold the FIRST occurrence of the ID, which for
        # a row asserted twice — the real check() plus a fixture-init guard
        # reusing the same ID — is the guard: a `check(id, "Emulator init
        # failed", false, ...)` that never executes. 22 Multiface rows pointed
        # at one. Preferring the cited call keeps the two halves of a row
        # agreeing: `VHDL file:line` and `Test file:line` name the same
        # assertion, or the row has neither.
        #
        # Only ever a line from THIS row's own call (grep_citations fills it
        # for the call tier alone), and %where already resolved to the source
        # that call lives in, so the pair cannot come from different files.
        return ($where->{$r}, $cite_line->{$r})
            if $cite_line && defined $cite_line->{$r}
               && (exists $checks->{$r} || exists $skips->{$r});
        return ($where->{$r}, $checks->{$r}) if exists $checks->{$r};
        return ($where->{$r}, $skips->{$r})  if exists $skips->{$r};
    }
    return (undef, undef);
}

# Citation for a row ID, following the same "MMU-01 -> MMU-01a/b/c" sub-row
# resolution the status lookup uses.
sub cite_for {
    my ($tid, $cites, $checks, $skips) = @_;
    return $cites->{$tid} if exists $cites->{$tid};
    my $resolved = resolve_ids($tid, $checks, $skips);
    for my $r (@$resolved) { return $cites->{$r} if exists $cites->{$r}; }
    return undef;
}

# Did this row's own call carry a filename-less VHDL line reference? Same
# sub-row resolution as cite_for(), so a parent row asked about `X-01` sees
# what `X-01b`'s call wrote, exactly as it does for the citation. (GH #188)
sub noref_for {
    my ($tid, $noref, $checks, $skips) = @_;
    return 1 if $noref->{$tid};
    my $resolved = resolve_ids($tid, $checks, $skips);
    for my $r (@$resolved) { return 1 if $noref->{$r}; }
    return 0;
}

# ── The frozen class (GH #188) ────────────────────────────────────────
#
# A hand-written cell the extractor computes NOTHING for. Nothing can
# contradict it: drift needs a computed side to disagree with, so no gate can
# ever check the value and it stays frozen for ever. 187 cells are in this
# state, and GH #187 was one of them — three sprite rows publishing a citation
# that belongs to two rows which are not even check() rows.
#
# The three sub-classes need DIFFERENT remedies, so they are reported apart:
#
#   c  the section's own sources do not assert the row at all (status
#      `missing`), so no tier can fire and none ever will while the row is
#      recorded here. Either it is a planned row with no test yet, or its
#      assertion lives in another subsystem's suite and the ROW is mis-homed.
#      Both need a human decision about the row, not about the citation.
#   b  the row runs here and its own call names line numbers with the
#      FILENAME LEFT OUT — `(VHDL 7163-7176)`. The intent is present and only
#      the spelling is unresolvable. Remedy: write the `.vhd` in the call.
#   a  the row runs here and offers no citation at all. Remedy: cite the VHDL
#      in its own check(), which makes the cell COMPUTED and therefore
#      drift-checked from then on — the GH #187 route.
#
# Deliberately NOT done here: guessing. Nothing in this classification
# produces or alters a citation, and a `(...)` tombstone cell never reaches it
# (the tombstone IS the computed side for those suites).
sub frozen_class {
    my ($status, $has_noref) = @_;
    return 'c' if $status eq 'missing';
    return 'b' if $has_noref;
    return 'a';
}

# The file cite_for()'s answer was literally read from. Same sub-row
# resolution, so the citation and the file it is charged to can never come
# from different rows. (GH #126)
sub cite_src_for {
    my ($tid, $from, $checks, $skips) = @_;
    return $from->{$tid} if exists $from->{$tid};
    my $resolved = resolve_ids($tid, $checks, $skips);
    for my $r (@$resolved) { return $from->{$r} if exists $from->{$r}; }
    return undef;
}

# May a citation just read from a source displace the one already held?
#
# grep_citations() orders its four tiers row-local-first, and that order has
# to survive the merge ACROSS a section's sources too. It did not: the first
# source to answer locked the row in, so a citation the primary source only
# had from the PLAN DOC beat the row-local citation of the companion suite
# that actually asserts the row. `NR_A0-01` published `zxnext.vhd:1241` — a
# bare signal declaration — over the `zxnext.vhd:5080` reset default its
# assertions exercise, and reported no drift while doing it, because the
# hand-written cell agreed with the plan. Worse, one run computed TWO answers
# for one row ID: a row listed in both a parent table and its companion's
# resolved locally when the companion's section was walked (and reported
# drift) and to the plan when the parent's was (and reported none) —
# `NR_A0-03`, `DUAL-05`, `HOTKEY-01`, `JOY-WIRE-01`. (GH #133)
#
# So a plan-doc citation is provisional: it holds the row until row-local
# evidence turns up. ONE fence stops that becoming the borrowed-citation
# defect the `next` tier is fenced against (SELF-04) and the companion
# ownership gate refuses (SELF-46): the displacing citation must have been
# read from the source that OWNS the row — the one this run will name in
# `Test file:line`. What is published therefore always justifies what runs,
# never a copy of it somewhere else.
#
# That single test also covers the two cases it might look like it misses,
# because only ever ONE source is being merged in at a time:
#
#   - a plan-doc candidate is refused, since %where never holds a plan-doc
#     path (and a second plan-doc answer could not differ anyway — every
#     source of a subsystem reads the same plan doc);
#   - a ROW-LOCAL incumbent is untouchable: it was read from the owner, so
#     no other source's citation can equal the owner. Row-local citations
#     keep merging first-source-wins, exactly as before. Spelling that out
#     as its own guard was tried and is unreachable — mutation-testing
#     SELF-60 found it dead, and dead code in a precedence rule reads as a
#     case that is handled when it is only shadowed.
#
# A row asserted nowhere ($owner undef) keeps whatever the plan gave it.
#
# $have: the citation held so far. $new_from: the file the candidate was
# read from. $owner: the source %where resolved this row to, or undef when
# no source asserts it.
sub cite_upgrades {
    my ($have, $new_from, $owner) = @_;
    return 1 unless defined $have;      # nothing held yet
    return 0 unless defined $owner;     # asserted nowhere
    return (defined $new_from && $new_from eq $owner) ? 1 : 0;
}

# ── Hand-written cells are validated too (GH #150) ────────────────────
#
# The extractor validated every citation it COMPUTED against the real FPGA
# tree and refused a filename it did not recognise, and never once looked at
# the hand-written cells it preserves. Since a hand-written cell is never
# overwritten — correctly — a wrong one was permanent AND invisible: it agreed
# with itself on every later run, and the drift report only fires when the
# computed side disagrees, which needs a computed side to exist.
#
# Measured over the live matrix: 3 cells name `kempston_mouse.vhd`, which does
# not exist (the Kempston mouse is inline in `zxnext.vhd`), and ~25 name
# jnext's own C++ in a column headed VHDL. Our implementation is not evidence;
# the VHDL is the oracle.
#
# Returns a list of complaints, one per problem, or the empty list when the
# cell is fine. It REPORTS — nothing here rewrites a cell, ever.
#
# A `(...)` cell is a declared tombstone (`(jnext-internal)`, `(SD SPI spec)`,
# `(host sockets)`, `(ESP-AT firmware)`) and says "there is nothing to cite",
# which is a claim, not an omission. 314 cells carry one. It is accepted as
# written: whether a suite deserves a tombstone is decided in %TOMBSTONE, and
# re-litigating it per row would just duplicate that judgement badly.
sub bad_hand_citation {
    my ($cell) = @_;
    return () if !defined $cell || $cell eq '' || $cell eq '—';
    return () if $cell =~ /^\(.*\)$/;          # a declared tombstone
    my @out;
    my $saw_vhd = 0;
    while ($cell =~ /$VHDL_CITE_RE/g) {
        $saw_vhd = 1;
        my ($verdict, $resolved) = resolve_vhd($1);
        if ($verdict eq 'unknown') {
            push @out, "names '$1', which is not in the FPGA core";
        } elsif ($verdict eq 'rehomed') {
            push @out, "names '$1'; the core carries it as '$resolved'";
        }
    }
    # A cell naming a jnext source file is the second measured class, and it
    # is worth its own wording: it is not a typo, it is the wrong ORACLE.
    push @out, "cites jnext's own source, not the VHDL: "
             . join(' ', $cell =~ /(\b[\w\/.\-]+\.(?:cpp|hpp|hh|cc|[ch])\b)/g)
        if $cell =~ /\.(?:cpp|hpp|hh|cc|[ch])\b/;
    push @out, "carries no VHDL citation at all"
        if !$saw_vhd && $cell !~ /\.(?:cpp|hpp|hh|cc|[ch])\b/;
    return @out;
}

# A citation string reduced to the form the drift comparison judges it by.
#
# ONLY the comparison is normalised. The stored cell is not touched — a
# hand-written citation is never overwritten, and this changes nothing about
# that. What it removes is a false *report*: the drift list is how a human
# notices a citation has gone wrong, so every entry that is merely a
# different spelling of the same lines is noise hiding a real one.
#
# Two spellings are folded, and both are in the matrix today:
#
#   zxnext.vhd:5633, 6260    whitespace around a `,` or `/` list separator
#   zxnext.vhd:5179, :6436   the filename-omitting continuation of GH #136,
#                            which cite_in() already folds away on the
#                            computed side
#
# Nothing else is, and every number below is MEASURED, over the 361 drift
# entries the current matrix produces.
#
# The two rules are LAYERED, not additive, and the difference is what the
# next person tuning this needs to know. Rule 1 alone closes 18 — and SD-17
# and IO-10 are not among them. Rule 2 alone closes ZERO: the cells it exists
# for are spelled `5179, :6436`, with a space between the separator and the
# carried-forward colon, so `([,/]):` cannot match until rule 1 has closed
# that gap. Together they close 20. Remove rule 1, and rule 2 closes nothing
# on its own — the 20 is not 18 plus 2.
#
# Rules deliberately left out, each measured ON TOP of the two above and each
# closing ZERO further entries: whitespace around a `:`, whitespace around a
# range's `-`, and even collapsing all whitespace outright — no cell is
# spelled `foo.vhd : 10` or `10 - 20`. Admitting any of them would add a rule
# no fixture exercises, and an unexercised rule inside a comparator is one
# nobody notices going wrong. Folding `+` to `,` (which cite_in does do on the
# computed side) closes zero further too, and is not whitespace at all: the
# ULA section's S17.02 cell reads `zxnext.vhd:3614+`, prose for "3614
# onwards", and folding it would rewrite what it says.
#
# ORDER is deliberately NOT normalised. `a.vhd:10,20` and `a.vhd:20,10` name
# the same lines, but the order is the author's statement about which line is
# the primary evidence, and equating them would silently swallow a real
# disagreement between the human and the extractor about exactly that.
# Measured over the same 361: zero entries are pure reorderings, so the
# strictness costs nothing today and keeps the report honest when one appears.
#
# A DIRECTORY PREFIX is the third rule, and it is not cosmetic — it is the one
# spelling difference that could never be reconciled at all (GH #145). The
# design docs write the qualified relative path (`device/copper.vhd:54-119`)
# and the extractor computed a bare filename, so those cells drifted on every
# run for ever, and a permanent false entry is noise hiding a real one.
#
# It is folded ONLY when the basename identifies exactly one file in the core.
# `hdmi_plle2.vhd` exists under both `pll/A7/` and `pll/A7-Issue-5/`, and there
# the prefix is the whole content of the citation; folding it would equate two
# citations that name different files. When the core is not checked out,
# nothing is folded — refusing is the answer that cannot be wrong.
#
# The prefix is folded for the COMPARISON only. The stored cell keeps whatever
# was written, here as everywhere else in this sub.
#
# A FOURTH rule, and the same argument again (GH #188): the matrix and the
# design docs separate citations of DIFFERENT files with `; `, and cite_in()
# joins them with `, `. At that position the two punctuation marks mean the
# identical thing — "another citation follows" — so a cell spelled
# `keymaps.vhd:43-44; membrane.vhd:253` could never equal the computed
# `keymaps.vhd:43-44, membrane.vhd:253` and drifted on every run for ever.
#
# MEASURED over the 320 drift entries the pre-GH #188 matrix produces: 14
# carry a `;`, and folding it closes 5 of them — RAM-BK-02, TIM-CYC-01,
# RST-04, SD-19, GH115-11, every one a pure separator-spelling difference. The
# other 9 stay, and they are real disagreements (S5-PSL.01/02/03, RAM-BK-03,
# NR-32, INT-07, CT-FUSE-01/02, INT-ULANEXT-02 name different lines, not the
# same lines punctuated differently), so the rule folds spelling and not
# substance. Yield 5 is the same order as rules 1+2 (20) and the prefix rule,
# and it is the reason a row may now put a two-file citation in its own
# check() without manufacturing a permanent false drift entry.
sub canon_citation {
    my ($c) = @_;
    $c =~ s{\s*;\s*}{,}g;         # "a.vhd:1; b.vhd:2" -> "a.vhd:1,b.vhd:2"
    $c =~ s{\s*([,/])\s*}{$1}g;   # "5633, 6260" -> "5633,6260"
    $c =~ s{([,/]):}{$1}g;        # "5179,:6436" -> "5179,6436"
    # "device/copper.vhd:54" -> "copper.vhd:54", when unambiguous.
    $c =~ s{ \b (?: [A-Za-z0-9_.\-]+ / )+ ( [A-Za-z0-9_]+ \.vhd ) (?! [A-Za-z0-9_] ) }
           { vhd_basename_unique($1) ? $1 : $& }gex;
    return $c;
}

# $stop_idx, when given, is the line index of the next @SUBSYS section
# header. Companion `###` sections are nested inside their parent `##`
# section, so without it the parent's scan runs straight through the
# companion's rows and counts them a second time (as `missing`, against the
# wrong source file) before the companion's own pass rewrites them. The
# bytes written were already correct — the companion runs last — but the
# tally double-counted, which a generated Summary cannot do.
#
# $companions, when given, is an arrayref of [binary, source_rel] pairs for
# the OTHER @SUBSYS entries that share this section's `##` subsystem — see
# the fallback block below (GH #121).
#
# $invalid, when given, collects hand-written cells that do not validate
# against the FPGA core (GH #150). Optional and last, so every existing caller
# keeps working unchanged.
#
# $frozen, when given, collects [id, class, cell] for every hand-written cell
# the extractor computes nothing for (GH #188). Optional and last, so every
# existing caller keeps working unchanged.
#
# $unref, when given, collects one hashref per table this section leaves
# ENTIRELY alone because it has neither a `Test file:line` nor a `VHDL
# file:line` column (GH #192). Optional and last, same reason.
sub refresh_section {
    my ($lines, $start_idx, $binary, $source_rel, $drift, $kept, $stop_idx,
        $companions, $invalid, $frozen, $unref) = @_;
    $kept //= [];

    # A section may be backed by several suites (see the Audio entry). The
    # merge is first-source-wins and the per-row `Test file:line` names the
    # file the row was actually found in, so a merged section still points
    # at one exact assertion.
    my @binaries = as_list($binary);
    my @sources  = as_list($source_rel);

    my %fails;
    for my $b (@binaries) {
        my $f = run_fails($b);
        $fails{$_} = 1 for keys %$f;
    }
    my $fails = \%fails;

    my (%checks, %skips, %where, %cites, %cite_from, %cite_line, %noref);
    my $tombstone;
    for my $src (@sources) {
        my ($c, $k) = grep_source($src);
        for my $id (keys %$k) {
            next if exists $checks{$id} || exists $skips{$id};
            $skips{$id} = $k->{$id};
            $where{$id} = $src;
        }
        for my $id (keys %$c) {
            next if exists $checks{$id} || exists $skips{$id};
            $checks{$id} = $c->{$id};
            $where{$id} = $src;
        }
        # %where is already updated for this source above, so cite_upgrades()
        # can tell "this source owns the row" from "this source merely read
        # the row out of the shared plan doc". (GH #133)
        my (%cf, %cl, %nr);
        my $cs = grep_citations($src, \%cf, \%cl, \%nr);
        $noref{$_} = 1 for keys %nr;
        for my $id (keys %$cs) {
            next unless cite_upgrades($cites{$id}, $cf{$id}, $where{$id});
            $cites{$id}     = $cs->{$id};
            $cite_from{$id} = $cf{$id};
            # Assigned, not //=: a citation that just lost to a better one
            # must not keep the loser's line. undef is the right answer when
            # the winner came from the plan doc or a comment block.
            $cite_line{$id} = $cl{$id};
        }
        $tombstone //= $TOMBSTONE{ suite_for_source($src) };
    }

    # ── Companion sources: same subsystem, different @SUBSYS entry ────────
    #
    # A `###` companion suite is nested inside its parent `##` section, and
    # rows are routinely LISTED in the parent's table while being ASSERTED
    # in the companion — `PFF-G108-01/02/02b/03` under `## Compositor`,
    # `ULA-INT-04/06` + `NR-C2-01/NR-C3-01` under `## CTC+Interrupts`,
    # `INT-07` under `## UART+I2C/RTC`. Recording has judged those against
    # the whole SUBSYSTEM since GH #118 (see subsystem_span); the status
    # computation kept scanning the entry's own sources only, so the two
    # halves of one tool disagreed again and those rows published `missing`
    # while their assertion runs and passes. GH #117 fixed exactly this
    # shape for RECORDING by letting one entry take several sources; this
    # is the same fix on the STATUS side. (GH #121)
    #
    # Strictly a FALLBACK. Primary sources are merged first and win every
    # key, so no row whose own source asserts it can change; only rows that
    # would otherwise read `missing` can move. The search widens to the
    # subsystem and no further — a row asserted nowhere in it still reads
    # `missing`, and a neighbouring subsystem's identically-named row never
    # answers for this one.
    for my $comp (@{ $companions || [] }) {
        my ($cbin, $csrc) = @$comp;
        my %owned;
        for my $src (as_list($csrc)) {
            my ($c, $k) = grep_source($src);
            for my $id (keys %$k) {
                next if exists $checks{$id} || exists $skips{$id};
                $skips{$id} = $k->{$id};
                $where{$id} = $src;
                $owned{$id} = 1;
            }
            for my $id (keys %$c) {
                next if exists $checks{$id} || exists $skips{$id};
                $checks{$id} = $c->{$id};
                $where{$id} = $src;
                $owned{$id} = 1;
            }
        }
        next unless %owned;

        # Citations, but ONLY for the IDs this companion actually owns. Its
        # plan doc is the same one the primary already merged, and adopting
        # its whole citation map would let a companion's row-local evidence
        # answer for a row the primary source owns — the borrowed-citation
        # defect the `next` tier is fenced against.
        my (%ccites, %cfrom, %cline);
        for my $src (as_list($csrc)) {
            my (%cf, %cl, %nr);
            my $cs = grep_citations($src, \%cf, \%cl, \%nr);
            # Restricted to the rows this companion OWNS, exactly as the
            # citation merge below is: a companion's no-filename note must not
            # label a row the primary source asserts.
            for my $id (keys %nr) { $noref{$id} = 1 if $owned{$id}; }
            for my $id (keys %$cs) {
                next unless cite_upgrades($ccites{$id}, $cf{$id}, $where{$id});
                $ccites{$id} = $cs->{$id};
                $cfrom{$id}  = $cf{$id};
                $cline{$id}  = $cl{$id};
            }
        }
        # The ownership gate stays: only rows this companion asserts. What
        # changed is that a row-local citation now displaces a PLAN-DOC one
        # the primary merged for a row it does not assert — that inversion is
        # GH #133. A row-local incumbent is still untouchable (SELF-46).
        for my $id (keys %owned) {
            next unless defined $ccites{$id};
            next unless cite_upgrades($cites{$id}, $cfrom{$id}, $where{$id});
            $cites{$id}     = $ccites{$id};
            $cite_from{$id} = $cfrom{$id};
            $cite_line{$id} = $cline{$id};
        }

        # The FAIL set is restricted the same way, and this is the half that
        # must not be forgotten: resolving a row into a companion source
        # without also reading that binary's FAIL lines would publish `pass`
        # for an assertion that fails. Merging it blind is the opposite
        # error — a companion FAIL for an ID the PRIMARY source asserts is
        # the companion's own row, and would publish a false `fail` here.
        for my $b (as_list($cbin)) {
            my $f = run_fails($b);
            for my $id (keys %$f) { $fails{$id} = 1 if $owned{$id}; }
        }
    }

    my ($checks, $skips, $cites) = (\%checks, \%skips, \%cites);

    my ($pass_ct, $fail_ct, $skip_ct, $missing_ct) = (0, 0, 0, 0);
    my ($cited_ct, $uncited_ct, $drift_ct, $frozen_ct, $tomb_ct) = (0) x 5;
    my $touched = 0;
    my $xtra_ct = 0;
    my $i = $start_idx + 1;

    # Column layout of the table currently being walked, re-read at every
    # header row so two tables of different shapes inside one section are each
    # refreshed on their own terms (GH #192). $cur_unref is the entry in
    # $unref for a table that has no computable column at all.
    my (@hdr, $col_cite, $col_status, $col_loc, $cur_unref);

    while ($i < scalar @$lines) {
        my $line = $lines->[$i];

        last if defined $stop_idx && $i >= $stop_idx;
        last if $line =~ /^## / && $i > $start_idx + 1;

        if ($line =~ /^\| / && index(substr($line, 2), '|') != -1) {
            # split preserving trailing empty fields; `\|` is an escaped
            # literal, not a column break (GH #157)
            my @cells = split_row_cells($line);
            my $tid_raw = $cells[1] // '';
            $tid_raw =~ s/^\s+|\s+$//g;

            if (is_header_row(\@cells)) {
                @hdr = @cells;
                ($col_cite, $col_status, $col_loc) = table_columns(\@hdr);
                $cur_unref = undef;
                # A table with no location AND no citation column has nothing
                # this tool can compute. It is left ENTIRELY alone and
                # reported, so it is never read as generated output. (GH #192)
                if (!defined $col_loc && !defined $col_cite) {
                    $cur_unref = { header => $line, rows => 0, marked => 0,
                                   title => '(no heading)' };
                    # Walk back to the heading that introduces this table,
                    # checking the prose in between for the marker.
                    for (my $j = $i - 1; $j > $start_idx; $j--) {
                        if ($lines->[$j] =~ /^#/) {
                            $cur_unref->{title} = $lines->[$j];
                            last;
                        }
                        $cur_unref->{marked} = 1
                            if index($lines->[$j], $UNREFRESHED_MARK) != -1;
                    }
                    push @$unref, $cur_unref if $unref;
                }
                $i++;
                next;
            }

            # Separator row (only dashes/colons/spaces), or an empty ID.
            if ($tid_raw ne '' && $tid_raw !~ /^[-:\s]+$/) {
                if (!@hdr) {
                    warn "WARN: row $tid_raw precedes any table header — not "
                       . "refreshed, its column layout is unknown\n";
                    $i++;
                    next;
                }
                if (defined $cur_unref) {
                    $cur_unref->{rows}++;
                    $i++;
                    next;
                }
                # The cell count must match this table's own header. MORE
                # cells is the residual GH #157 case the `\|` escape cannot
                # cover: a raw pipe in a Description is a genuine column break
                # and nothing can tell it from an intended one — warn, but
                # carry on, because the columns to its LEFT still line up.
                # FEWER cells is unrecoverable: every column after the missing
                # break shifts LEFT, so `Status` would be rewritten into the
                # location cell. Refuse that one.
                if (scalar @cells > scalar @hdr) {
                    warn "WARN: row $tid_raw has " . scalar(@cells)
                       . " cells, its table header has " . scalar(@hdr)
                       . " — an unescaped `|` in a cell shifts the columns "
                       . "right; write it `\\|`\n";
                } elsif (scalar @cells < scalar @hdr) {
                    warn "WARN: row $tid_raw has " . scalar(@cells)
                       . " cells, its table header has " . scalar(@hdr)
                       . " — not refreshed, the columns cannot be mapped\n";
                    $i++;
                    next;
                }
                if (1) {
                    # `if (1)`, NOT a bare block: a bare block is a loop that
                    # runs once, so every `next` below would leave the BLOCK and
                    # fall through to the `$i++` at the bottom of the while —
                    # advancing twice and silently skipping the row after each
                    # protected one. NR-C0-03 vanished from the counts exactly
                    # that way while this was being written.
                    #
                    # Protected row (strategy point 6): leave it byte-identical.
                    # The existing Status cell is trusted for the counts, so
                    # the section tally stays truthful.
                    if ($line =~ $PROTECTED_RE) {
                        if (!defined $col_status) {
                            # No Status cell to trust, so nothing to tally —
                            # the row is still kept byte-identical.
                            push @$kept, $tid_raw;
                            $xtra_ct++;
                            $i++;
                            next;
                        }
                        my $cur = $cells[$col_status];
                        $cur =~ s/^\s+|\s+$//g;
                        if    ($cur eq 'pass')    { $pass_ct++;    }
                        elsif ($cur eq 'fail')    { $fail_ct++;    }
                        elsif ($cur eq 'skip')    { $skip_ct++;    }
                        elsif ($cur eq 'missing') { $missing_ct++; }
                        else {
                            warn "WARN: protected row $tid_raw carries "
                               . "unrecognised status '$cur'\n";
                        }
                        # A marker on a row this section's own source DOES
                        # cover is masking a locally computable status —
                        # say so rather than silently freezing the row.
                        warn "WARN: protected row $tid_raw is also covered by "
                           . "$source_rel; the marker overrides a locally "
                           . "computable status\n"
                            if @{ resolve_ids($tid_raw, $checks, $skips) };
                        push @$kept, $tid_raw;
                        $touched++;
                        $i++;
                        next;
                    }

                    # Always COMPUTED, even when the table has no Status column
                    # to publish it in: frozen_class() below classifies on it,
                    # and a wrong class is as misleading as a wrong cell.
                    my $new_status = status_for($tid_raw, $fails, $checks, $skips);
                    if (defined $col_status) {
                        if    ($new_status eq 'pass')    { $pass_ct++;    }
                        elsif ($new_status eq 'fail')    { $fail_ct++;    }
                        elsif ($new_status eq 'skip')    { $skip_ct++;    }
                        else                             { $missing_ct++; }

                        # Preserve column widths exactly. Guard against negative
                        # widths (narrow/empty cells): Perl sprintf with a
                        # negative field width flips alignment, Python ljust(-n)
                        # is a no-op — clamp to 0 to match.
                        my $orig_status = $cells[$col_status];
                        my $width = length($orig_status) - 2;
                        $width = 0 if $width < 0;
                        $cells[$col_status] =
                            ' ' . sprintf("%-${width}s", $new_status) . ' ';
                    }

                    # VHDL citation: fill only when the cell is empty. A cell
                    # that already carries a citation was written by hand and
                    # stays — but a disagreement with the extracted one is
                    # reported, so drift surfaces without being clobbered.
                    # "Disagreement" is judged on canon_citation() of BOTH
                    # sides, so a cell spelled `5633, 6260` is not reported
                    # against the canonical `5633,6260`. The cell is compared
                    # normalised and written back untouched. (GH #142)
                    #
                    # Guarded on the column EXISTING, exactly as the status and
                    # location blocks are. No table in the document is shaped
                    # this way today; one written tomorrow (a location column,
                    # no `VHDL file:line`) used to index $cells[undef] — cell 0,
                    # the empty string before the leading `|` — and publish the
                    # citation THERE, breaking the row's first column. Probed
                    # for deliberately while GH #192 was being written.
                    if (defined $col_cite) {
                        my $cur_cite = $cells[$col_cite];
                        $cur_cite =~ s/^\s+|\s+$//g;
                        # A hand-written cell is preserved, and now also CHECKED —
                        # it is the one citation nothing validated (GH #150).
                        if ($invalid) {
                            push @$invalid, "$tid_raw: $_ — [$cur_cite]"
                                for bad_hand_citation($cur_cite);
                        }
                        my $computed = cite_for($tid_raw, $cites, $checks, $skips);
                        my $new_cite = $computed // $tombstone;
                        # A hand-written cell whose only computed side is the
                        # suite's declared tombstone. NOT frozen — the tombstone
                        # is a real answer, so the cell IS compared and can drift
                        # (BOOT-SD-01/02 do). Counted so the frozen total below
                        # can be audited against the raw one. (GH #188)
                        $tomb_ct++ if !defined $computed && defined $tombstone
                                      && $cur_cite ne '' && $cur_cite ne '—';
                        if ($cur_cite eq '' || $cur_cite eq '—') {
                            if (defined $new_cite) {
                                my $cw = length($cells[$col_cite]) - 2;
                                $cw = 0 if $cw < 0;
                                $cells[$col_cite] = length($new_cite) > $cw
                                    ? ' ' . $new_cite . ' '
                                    : ' ' . sprintf("%-${cw}s", $new_cite) . ' ';
                                $cited_ct++;
                            } else {
                                $uncited_ct++;
                            }
                        } elsif (!defined $new_cite) {
                            # FROZEN (GH #188): a hand-written cell with no
                            # computed side. Drift needs something to disagree
                            # with, so this value is checked by nobody and stays
                            # frozen for ever — GH #187 lived here. Reported,
                            # never rewritten.
                            $frozen_ct++;
                            push @$frozen,
                                 [$tid_raw,
                                  frozen_class($new_status,
                                               noref_for($tid_raw, \%noref,
                                                         $checks, $skips)),
                                  $cur_cite]
                                if $frozen;
                        } elsif (canon_citation($new_cite)
                                 ne canon_citation($cur_cite)) {
                            $drift_ct++;
                            # Charge the line to the file `source=[...]` was read
                            # from, not to the section's first source (GH #126).
                            # The fallback covers the one citation with no file
                            # behind it — a %TOMBSTONE, which is per suite and
                            # only ever set on a single-source section.
                            my $from = cite_src_for($tid_raw, \%cite_from,
                                                    $checks, $skips)
                                       // $sources[0];
                            push @$drift,
                                 "$from  $tid_raw: doc=[$cur_cite] source=[$new_cite]";
                        }
                    }

                    if (defined $col_loc) {
                        my ($lsrc, $ln) = line_for($tid_raw, $checks, $skips,
                                                   \%where, \%cite_line);
                        my $location = defined($ln) ? "$lsrc:$ln" : 'missing';
                        my $orig_loc = $cells[$col_loc];
                        my $loc_width = length($orig_loc) - 2;
                        $loc_width = 0 if $loc_width < 0;
                        # Don't silently truncate — if the citation no longer
                        # fits the existing column width (typical when test
                        # functions grow past line 999), preserve the full text
                        # and warn. The row's column alignment is locally lost
                        # but the data is correct; markdown renderers tolerate
                        # per-row width variance. (Bug fixed 2026-04-27 after
                        # Task 7 r1+r2 broke ~213 citations via silent
                        # truncation.)
                        if (length($location) > $loc_width) {
                            warn "WARN: citation '$location' (id=$tid_raw) exceeds column width $loc_width; keeping full text\n";
                            $cells[$col_loc] = ' ' . $location . ' ';
                        } else {
                            $cells[$col_loc] =
                                ' ' . sprintf("%-${loc_width}s", $location) . ' ';
                        }
                    }

                    $lines->[$i] = join('|', @cells);
                    # `rows` in the Summary means "rows carrying a published
                    # Status", and it must keep equalling pass+fail+skip+
                    # missing. A 4-column "Extra coverage" row has no Status
                    # cell, so it is counted separately: refreshed, but never
                    # tallied into a total no cell in the document displays.
                    if (defined $col_status) { $touched++; } else { $xtra_ct++; }
                }
            }
        }
        $i++;
    }

    return ($touched, $pass_ct, $fail_ct, $skip_ct, $missing_ct,
            $cited_ct, $uncited_ct, $drift_ct, $frozen_ct, $tomb_ct,
            $xtra_ct);
}

# Short, unique label for a section. The companion suites all share the header
# text "### Companion integration suite — ...", so they are labelled by their
# source file instead.
sub section_label {
    my ($header, $source_rel) = @_;
    if ($header =~ /^###/) {
        (my $base = $source_rel) =~ s{.*/}{};
        $base =~ s/\.cpp$//;
        return "Companion: $base";
    }
    my $short = $header;
    $short =~ s/^#+\s*//;
    $short =~ s/ — .*//;
    return $short;
}

# The head Summary table, built from this same run. No timestamp: a
# regenerated date churns the diff on every run even when nothing moved,
# and git already records when the file changed.
sub render_summary {
    my ($report, $tombstoned, $recorded_total, $declared_suites, $declared_rows,
        $xtra_total, $unref) = @_;
    my @out;
    push @out, '| Section                                    |  Rows | pass | fail | skip | missing | unrecorded |';
    push @out, '|--------------------------------------------|------:|-----:|-----:|-----:|--------:|-----------:|';
    my @t = (0, 0, 0, 0, 0, 0);
    for my $row (@$report) {
        my ($label, $rows, $p, $f, $s, $m, $u) = @$row;
        push @out, sprintf('| %-42s | %5d | %4d | %4d | %4d | %7d | %10d |',
                           $label, $rows, $p, $f, $s, $m, $u);
        my @v = ($rows, $p, $f, $s, $m, $u);
        $t[$_] += $v[$_] for 0 .. 5;
    }
    push @out, sprintf('| %-42s | %5d | %4d | %4d | %4d | %7d | %10d |',
                       '**Total**', @t);
    push @out, '';
    push @out, sprintf(
        'Rows the sections above carry: **%d**. Distinct row IDs recorded anywhere in '
      . 'this document (every table, including "Extra coverage"): **%d**. Rows the '
      . '%d suites declared in `test/unit-tests.conf` run live: **%d**.',
        $t[0], $recorded_total, $declared_suites, $declared_rows);
    push @out, '';
    # GH #192. The `Rows` column above must keep equalling pass+fail+skip+
    # missing, so rows that publish no Status cannot join it — but they ARE
    # refreshed now, and saying nothing would leave 85 recomputed rows invisible
    # in the one table that is supposed to say how much this document computes.
    my $unref_rows = 0;
    $unref_rows += $_->{rows} for @{ $unref || [] };
    push @out, sprintf(
        'The `Rows` column counts rows that publish a **`Status`**, so it equals '
      . 'pass+fail+skip+missing by construction. A further **%d** rows live in the '
      . '4-column "Extra coverage (not in plan)" tables, which have no `Status` '
      . 'column: their `VHDL file:line` and `Test file:line` ARE recomputed on '
      . 'every run (they were not, for two years — GH #192), and a row asserted '
      . 'nowhere reads `missing` in the location column exactly as it would in a '
      . 'main table. A further **%d** rows sit in **%d** tables that carry neither '
      . 'column and are therefore not refreshed at all; each says so above itself.',
        $xtra_total, $unref_rows, scalar @{ $unref || [] });
    push @out, '';
    push @out, '**`missing`** = a row this document lists that its suite\'s test source '
             . 'no longer asserts. **`unrecorded`** = the reverse: a row the test source '
             . 'asserts that this document does not list **in the owning subsystem\'s '
             . 'section** — asked per section, not globally, so an ID string reused by '
             . 'another subsystem cannot vouch for it (GH #118). Both are real gaps; '
             . 'neither is auto-repaired, because the description that makes a row worth '
             . 'recording cannot be derived from the source (GH #117).';
    push @out, '';
    push @out, '**One deliberate looseness remains, so treat this column as a floor.** '
             . '*Sub-letter aliasing*: a source row `X-01b` counts as recorded by matrix '
             . 'row `X-01`, matching how the Status lookup resolves sub-rows. It is kept '
             . 'because `resolve_ids()` uses the same mapping in the other direction — '
             . 'drop it and row `X-01` would read `pass` *because* `X-01a` proves it while '
             . '`X-01a` was reported as recorded nowhere. All 102 IDs it was hiding were '
             . 'triaged (GH #118): 90 are decompositions of their parent plan row, and 12 '
             . 'were distinct assertions that now have rows of their own — `NA-01b`, '
             . '`NA-01c`, `NR-12a`, `NR-12b`, `HK-07b`, `MF-G162-01b`, `REG-01b`, '
             . '`REG-02b`, `REG-03a/b/c`, `S5.10c` — joining the earlier `FB-04b`, '
             . '`IORQ-02b` and `IORQ-02c`. The set is now printed on every run (the '
             . '`ALIASED` report), so the next one that is not a sub-case is visible '
             . 'instead of inferred. The second looseness — *cross-section ID collision* '
             . '— is closed: recording is asked against the owning `##` subsystem section '
             . 'rather than globally, which surfaced 29 rows that an identically-named row '
             . 'in a different subsystem had been vouching for (`SD-16..SD-23` by Audio, '
             . '`PR-01..PR-05` by IO Port Dispatch, the `G108-*` set by ULA Video, '
             . '`NR-10/11/13/14` + `PRI-01/02/04` by Audio and Memory/MMU, `SD2-01/02` by '
             . 'Memory/MMU). A `###` companion sub-section is judged against its parent '
             . '`##`, not separately: its rows are part of the same subsystem\'s coverage '
             . 'story and several are recorded in the parent\'s own table (GH #118).';
    push @out, '';
    push @out, '### Suites with no section here, and why';
    push @out, '';
    push @out, 'Every suite `test/unit-tests.conf` declares is accounted for: it is either '
             . 'traced by a section above or listed below with the authority it is actually '
             . 'written against. **Anything else is a hard failure** — `test/refresh-traceability-matrix.pl` '
             . 'refuses to run (exit 2) and rewrites nothing, in the manner of '
             . '`test/run-unit-tests.sh` refusing when its manifest and CMake disagree. That '
             . 'refusal is the anti-drift mechanism: the traced-suite count sat at 28 for the '
             . 'whole v0.98 series while the manifest grew 49 → 80, because each of the ~31 '
             . 'additions arrived as one more name on a warning line that already listed fifty.';
    push @out, '';
    if (@$tombstoned) {
        my $rows = 0;
        $rows += $_->[1] for @$tombstoned;
        push @out, sprintf(
            'These %d suites (%d live rows) have no VHDL-derived plan row to map, so they '
          . 'have no section here. They are still declared, counted and run; their runtime '
          . 'view is `test/SUBSYSTEM-TESTS-STATUS.md`.',
            scalar @$tombstoned, $rows);
        push @out, '';
        push @out, '| Suite | Rows | Authority it is written against |';
        push @out, '|-------|-----:|---------------------------------|';
        for my $t (@$tombstoned) {
            push @out, sprintf('| `%s` | %d | %s |', $t->[0], $t->[1], $t->[2]);
        }
    } else {
        push @out, 'There are none: every declared suite has a section above.';
    }
    push @out, '';
    push @out, 'The runtime pass/fail view of all declared suites lives in '
             . '`test/SUBSYSTEM-TESTS-STATUS.md` (`make unit-test-dashboard`), which is '
             . 'its canonical source; this table is the *document\'s own* view — what the '
             . 'matrix records and what it misses.';
    return \@out;
}

# Swap the generated block in place. A missing marker is fatal: silently
# appending (or silently doing nothing) is how the old hand-typed table
# drifted three months out of date in the first place.
sub replace_summary {
    my ($lines, $body) = @_;
    my ($b, $e);
    for my $i (0 .. $#$lines) {
        $b = $i if !defined $b && index($lines->[$i], $SUMMARY_BEGIN) == 0;
        $e = $i if defined $b && index($lines->[$i], $SUMMARY_END) == 0;
        last if defined $e;
    }
    fatal("generated-summary markers not found in $MATRIX — expected a line "
        . "'$SUMMARY_BEGIN' followed by '$SUMMARY_END'")
        unless defined $b && defined $e;
    splice(@$lines, $b + 1, $e - $b - 1, @$body);
}

# The process exit status, as a function of the row-level gap — split out of
# main() so it can be asserted directly: the selftest loads this file
# without running main(), so the glue would otherwise be the one piece of
# the contract nothing pins.
#
# The SUITE-level gap is not an input here: it is a refusal, checked before
# anything is read or written, and exits 2 from main() directly.
sub report_exit_code {
    my ($unrec_ct) = @_;
    return $unrec_ct ? 1 : 0;
}

# header index -> [[binary, source_rel], ...] for the OTHER @SUBSYS entries
# that live in the same `##` subsystem. $found is main()'s [[idx, entry], ...].
#
# Split out of main() for the same reason report_exit_code() was: this is the
# glue that decides which sources refresh_section may fall back to, and main()
# is stripped when the selftest loads this file, so nothing else could pin it.
#
# The grouping key is subsystem_span() — the SAME unit recording has used
# since GH #118. That is the whole point of GH #121: the two halves of this
# tool now read one set of sources, so they cannot disagree about whether a
# row asserted in a companion suite is covered. The relation is symmetric (a
# parent lists its companions, a companion lists its parent) and stops dead at
# the `##` boundary — a different subsystem is never a companion, so a row
# asserted nowhere in this subsystem still reads `missing`.
# Locate each resolved @SUBSYS entry's section header in the document.
# Returns ([[line_idx, entry], ...], [header, ...]) — the ones found, and the
# ones that are not there.
#
# Split out of main() for the same reason report_exit_code() and
# companion_map() were: the selftest loads this file with main() stripped, so
# anything left inline in main() is contract that nothing can assert. The
# missing-section list feeds a REFUSAL, which makes it exactly the kind of
# thing that must be pinned rather than trusted.
#
# The header match is a PREFIX match, not equality: section headers may have
# gained a " + `companion_test.cpp`" suffix after a companion suite was
# created (ULA Video + ula_integration_test, CTC+Interrupts +
# ctc_interrupts_test). The leading "## <Name> — `<file>`" stays the
# discriminator, and the trailing space in the prefix test stops
# "## Copper" from matching "## Copper Extra".
sub resolve_sections {
    my ($lines, $subsys) = @_;
    my (@found, @missing);
    for my $entry (@$subsys) {
        my ($header) = @$entry;
        my $idx;
        for my $i (0 .. $#$lines) {
            my $stripped = $lines->[$i];
            $stripped =~ s/^\s+|\s+$//g;
            if ($stripped eq $header || index($stripped, $header . ' ') == 0) {
                $idx = $i;
                last;
            }
        }
        if (!defined $idx) { push @missing, $header; next; }
        push @found, [$idx, $entry];
    }
    return (\@found, \@missing);
}

# The CONSEQUENCE of a missing section header, as a pure predicate: exit code,
# and whether the matrix may be written.
#
# resolve_sections() answers "which headers are absent"; this answers "and
# what then". Splitting them is not ceremony — the refusal was inline in
# main(), which the selftest strips, and a reviewer reverted it to
# print-and-continue with the selftest still 96/96 green. A rule nothing can
# assert is a rule that has already been silently deleted once.
#
# Returns (exit_code, may_write): (0, 1) when nothing is missing, (2, 0) when
# something is. `may_write` is the load-bearing half — a refusal that rewrote
# the document anyway would leave it half-refreshed against a section list
# the tool just declared incoherent.
sub section_refusal {
    my ($missing) = @_;
    return (0, 1) unless @$missing;
    return (2, 0);
}

sub companion_map {
    my ($lines, $found) = @_;
    my (%span_of, %by_span, %entry_of);
    for my $f (@$found) {
        my ($from) = subsystem_span($lines, $f->[0]);
        $span_of{ $f->[0] }  = $from;
        $entry_of{ $f->[0] } = $f->[1];
        push @{ $by_span{$from} }, $f->[0];
    }
    my %comp;
    for my $idx (keys %span_of) {
        my @c;
        for my $other (sort { $a <=> $b } @{ $by_span{ $span_of{$idx} } }) {
            next if $other == $idx;
            push @c, [ $entry_of{$other}[1], $entry_of{$other}[2] ];
        }
        $comp{$idx} = \@c;
    }
    return \%comp;
}

# Test IDs asserted in more than one `##` SUBSYSTEM's sources. (GH #192)
#
# $found is main()'s [[header_idx, [header, binaries, sources]], ...]; $lines
# is the matrix. Entries are grouped by subsystem_span() — the SAME unit
# recording (GH #118) and the companion status fallback (GH #121) use, so a
# `###` companion never counts as a second subsystem against its own parent.
# That restriction is the whole point: a parent and its companion sharing an ID
# is ordinary (they share a plan doc); two unrelated subsystems sharing one is
# the collision that made `CFG-05..07` and `KEMP-17` misreport.
#
# Returns [[id, [[subsystem_label, "src:line"], ...]], ...], sorted by ID, one
# entry per ID that spans two or more subsystems. Deterministic: sources are
# walked in @found order and labels sorted, so the report does not churn.
sub duplicate_ids {
    my ($found, $lines) = @_;
    my (%seen, %label);
    for my $f (@$found) {
        my ($idx, $entry) = @$f;
        my ($header, undef, $source_rel) = @$entry;
        my ($from) = subsystem_span($lines, $idx);
        # subsystem_span returns the line AFTER the `##` header, so the header
        # itself is one line back. It names the subsystem; a `###` companion
        # resolves to its parent's, which is what folds it in.
        my $lab = $lines->[$from - 1];
        $lab =~ s/^#+\s*//;
        $lab =~ s/ — .*//;
        $label{$from} = $lab;
        for my $src (as_list($source_rel)) {
            my ($c, $k) = grep_source($src);
            for my $id (keys %$c) { $seen{$id}{$from} //= "$src:$c->{$id}"; }
            for my $id (keys %$k) { $seen{$id}{$from} //= "$src:$k->{$id}"; }
        }
    }
    my @out;
    for my $id (sort keys %seen) {
        my @spans = keys %{ $seen{$id} };
        next unless @spans > 1;
        push @out, [$id,
                    [ map  { [$label{$_}, $seen{$id}{$_}] }
                      sort { $label{$a} cmp $label{$b} } @spans ]];
    }
    return \@out;
}

# Thin wrapper so every fatal() becomes exit 3 rather than an errno-derived
# status. The tail of this file stays the literal `exit(main());` the selftest
# strips to load the subs without running them.

# The accounting gate on its own: no test binary is run, no source is read,
# the matrix is not opened. Returns (exit_code, subsys) — $subsys undef when
# it refused.
#
# Split out because `--check-accounting` has to be cheap enough to be a
# prerequisite of `make unit-test`: 0.01 s and no build dependency, against
# 2.3 s and a built test tree for the full refresh. It is the same code path
# either way — there is no second, weaker copy of the rule.
sub check_accounting {
    my $declared   = declared_suites();
    my $complaints = suite_accounting($declared, \@SUBSYS, \%NO_MATRIX_SECTION);
    my ($subsys, $resolve_complaints) = @$complaints ? ([], [])
                                                     : resolve_subsys(\@SUBSYS);
    push @$complaints, @$resolve_complaints;
    return (0, $subsys, $declared) unless @$complaints;
    print STDERR
        "refresh-traceability-matrix: REFUSING — the suite list is not\n"
      . "accounted for. `test/unit-tests.conf` is the driver: every suite it\n"
      . "declares must be traced by \@SUBSYS in\n"
      . "test/refresh-traceability-matrix.pl or tombstoned in\n"
      . "%NO_MATRIX_SECTION there with a reason, and nothing may be both,\n"
      . "neither, or accounted for without being declared.\n\n";
    print STDERR "  $_\n" for @$complaints;
    return (2, undef, undef);
}

# ── The emitter (GH #196 phase 2.1) ──────────────────────────────────
#
# Builds ONE section's rows from the sources, with no reference whatsoever to
# what the committed matrix currently says. That is the whole inversion: the
# old refresh_section() read the document, matched each row it found, and
# rewrote three cells while leaving everything else — descriptions, row set,
# ordering — under hand control. Everything is computed here instead, so
# `frozen`, doc-vs-computed `drift`, `unrecorded` and stale locations cannot
# exist as classes.
#
# Row order is deliberate and NOT sorted: plan rows first, in the plan doc's
# own order (which groups by behaviour and is editorial), then live rows the
# plan does not list, in source order.
sub emit_section_rows {
    my ($binary, $source_rel, $opt) = @_;
    $opt ||= {};
    my @sources = as_list($source_rel);
    # A `###` companion does NOT re-list its parent's plan (GH #196 fix).
    # plan_doc_path() resolves a companion's source to the SAME
    # *-TEST-PLAN-DESIGN.md as its parent, so emitting plan rows for both
    # republished every row the PARENT asserts as `missing` under the
    # companion: 939 phantom rows, 731 of them in companions. `EXT-01` read
    # `pass` under `## Input` and `missing` under its companion in the same
    # document.
    my $want_plan = $opt->{plan} // 1;
    # Sources of this section's `###` companions, consulted ONLY as a status
    # fallback for a row this section's own sources do not assert. That is the
    # GH #121 rule: a row is routinely listed in a parent's table and asserted
    # in its companion suite, and reading the parent's file alone published it
    # as `missing` while the assertion passed.
    my @fallback = as_list($opt->{fallback} || []);
    # A section can name several suites (Audio names three), so the FAIL sets
    # of every binary are merged — a row failing in any of them fails.
    my %fails;
    for my $b (as_list($binary), as_list($opt->{fallback_bins} || [])) {
        my $f = run_fails($b);
        $fails{$_} = $f->{$_} for keys %$f;
    }
    my $fails = \%fails;

    my (%checks, %skips, %desc, %cites, %where, %cite_line);
    for my $src (@sources, @fallback) {
        my ($c, $s) = grep_source($src);
        # First file wins, matching the companion-fallback order the status
        # lookup has used since GH #121.
        for my $id (keys %$c) { $checks{$id} //= $c->{$id}; $where{$id} //= $src; }
        for my $id (keys %$s) { $skips{$id}  //= $s->{$id}; $where{$id} //= $src; }
        my $d = row_descriptions($src);
        for my $id (keys %$d) { $desc{$id} //= $d->{$id}; }
        # Same call shape refresh_section() uses: the citation map is the
        # return value, provenance/line/no-file are filled through out-params.
        my (%cf, %cl, %nr);
        my $ct = grep_citations($src, \%cf, \%cl, \%nr);
        for my $id (keys %$ct) { $cites{$id} //= $ct->{$id}; }
        for my $id (keys %cl)  { $cite_line{$id} //= $cl{$id}; }
    }

    my $plan_desc = plan_descriptions($sources[0]);
    my $plan_cite = plan_cites($sources[0]);

    my (@ids, %seen);
    push @ids, grep { !$seen{$_}++ } @{ plan_rows($sources[0]) } if $want_plan;
    # Live rows the plan does not list — 2.3. Ordered by the line they are
    # asserted on, so the table reads in the same order as the file.
    #
    # The set comes from grep_row_ids(), NOT from grep_source(). grep_source
    # treats every ID-shaped literal as a row, which was harmless while it only
    # ever answered questions about rows the matrix already listed, but here it
    # BUILDS the row set — and it would adopt every `set_group("FB-1-Border")`
    # banner as a row that nothing asserts. grep_row_ids() masks the banners,
    # which is the same reason it was taught to (GH #144).
    my %real;
    for my $src (@sources) {
        my $ids = grep_row_ids($src);
        $real{$_} = 1 for keys %$ids;
    }   # @fallback is deliberately absent: it answers about STATUS, and its
        # own rows belong to its own section.
    my @live = sort { ($checks{$a} // $skips{$a} // 0) <=> ($checks{$b} // $skips{$b} // 0)
                      || $a cmp $b }
               grep { $real{$_} && !$seen{$_}++ } (keys %checks, keys %skips);
    push @ids, @live;

    my @rows;
    for my $id (@ids) {
        my $status = status_for($id, $fails, \%checks, \%skips);
        my $d = $desc{$id} // $plan_desc->{$id} // '—';
        my $c = cite_for($id, \%cites, \%checks, \%skips) // $plan_cite->{$id}
             // $TOMBSTONE{ suite_for_source($sources[0]) } // '—';
        my ($lf, $ll) = line_for($id, \%checks, \%skips, \%where, \%cite_line);
        my $l = (defined $lf && defined $ll) ? "$lf:$ll" : '—';
        push @rows, [$id, $d, $c, $status, $l];
    }
    return \@rows;
}

# One section's markdown: header, a pointer to the plan doc that carries the
# prose, and the table.
sub emit_section {
    my ($header, $binary, $source_rel) = @_;
    my @sources = as_list($source_rel);
    return emit_section_from_rows($header, $sources[0],
                                  emit_section_rows($binary, $source_rel));
}

sub emit_section_from_rows {
    my ($header, $source, $rows) = @_;
    my @sources = as_list($source);
    my @out = ($header, '');
    if (my $pd = plan_doc_path($sources[0])) {
        push @out, "Notes and rationale: [" . (split m{/}, $pd)[-1] . "]("
                 . (split m{/}, $pd)[-1] . ").", '';
    }
    # A suite with a declared citation tombstone has no VHDL counterpart, and
    # in practice these suites also assert WITHOUT row IDs — rewind_test uses a
    # bare `CHECK(cond, text)` macro and carries no ID literal at all. Their
    # rows therefore read `missing` because no assertion can be matched to
    # them BY NAME, not because the behaviour is untested. Saying so is the
    # difference between an honest gap and a false one. (Found in review.)
    if (my $tomb = $TOMBSTONE{ suite_for_source($sources[0]) }) {
        # The ID-less sentence is emitted only where it is TRUE — measured,
        # not assumed from the presence of a tombstone. The ESP suites carry a
        # tombstone (nothing in the FPGA core to cite) but assert with proper
        # row IDs, so claiming otherwise for them would be a fresh false
        # statement of exactly the kind this document is being cleaned of.
        my $ids = 0;
        for my $src (@sources) { $ids += scalar keys %{ grep_row_ids($src) } }
        push @out, $ids == 0
          ? "> **Rows below read `missing` because this suite asserts WITHOUT "
          . "row IDs**, not because the behaviour is untested: it uses a bare "
          . "`CHECK(cond, text)` macro and carries no ID literal at all, so no "
          . "assertion can be matched to a row by name. Its citation column is "
          . "the declared tombstone `$tomb` — it has no VHDL counterpart."
          : "> Citations in this section are the declared tombstone `$tomb`: "
          . "this suite has no VHDL counterpart to cite.", '';
    }
    push @out, '| Test ID | Description | VHDL file:line | Status | Test file:line |';
    push @out, '|---------|-------------|----------------|--------|----------------|';
    for my $r (@$rows) {
        my @c = @$r;
        # A literal pipe in a description must stay escaped or it splits the
        # row — the GH #157 defect, in the other direction.
        $c[1] =~ s/(?<!\\)\|/\\|/g;
        push @out, '| ' . join(' | ', @c) . ' |';
    }
    push @out, '';
    return @out;
}

# The document's own preamble and the VHDL-column explainer.
#
# These describe the GENERATOR's semantics — what `missing` means, which
# evidence tiers fill a citation — so they belong to the generator, not to a
# hand-edited head that can drift from the code it describes. The per-section
# prose moved the other way, into each subsystem's plan doc, where it sits
# next to the rows it explains (owner decision, 2026-08-01).
sub emit_preamble {
    return (
'# Test Plan Traceability Matrix',
'',
'> **GENERATED — do not edit this file by hand.** Every cell is computed by',
'> `test/refresh-traceability-matrix.pl` from the test sources, the subsystem',
'> plan docs and the suite manifest. Edit those and re-run the script; an edit',
'> made here is overwritten on the next run and proves nothing in the meantime.',
'',
'This document maps plan row → test ID → VHDL citation → test location for the',
'jnext subsystem unit test suites. See',
'[UNIT-TEST-PLAN-EXECUTION.md](UNIT-TEST-PLAN-EXECUTION.md) for the authoring',
'process and [EMULATOR-DESIGN-PLAN.md](../design/EMULATOR-DESIGN-PLAN.md) §Phase 9',
'for the task tree. Each section links the plan doc carrying that subsystem\'s',
'notes and rationale.',
'',
'A row reads **`missing`** when the plan doc lists it and no suite of the owning',
'subsystem asserts it — a real backlog, and the only remaining hand-made claim',
'in the document. Rows the sources assert are emitted whether or not a plan doc',
'mentions them, so a test can no longer be absent from this document.',
'',
    );
}

sub emit_cite_explainer {
    return (
'### The `VHDL file:line` column',
'',
'Recovered from four **row-local** evidence tiers, in order: the',
'`check()`/`skip()` call carrying the row\'s own ID; a comment block naming that',
'ID; the first call after a table-driven ID literal, for rows with no call of',
'their own; and the row\'s plan-doc entry. Every citation is validated against',
'the real FPGA source tree, so a typo\'d or renamed `.vhd` is reported rather',
'than published.',
'',
'Banner-comment and nearest-unrelated-comment tiers were prototyped and',
'**rejected**: both attribute a neighbouring row\'s VHDL lines to this one, and a',
'plausible-but-wrong citation is worse than an honest `—`. A `—` here is a real,',
'visible gap; the fix is to cite the VHDL in the row\'s own assertion, which also',
'makes the cell drift-checked from then on.',
'',
    );
}

# The exceptions file — the ONE hand-maintained input (GH #196 phase 2.2).
#
# Parsed strictly: three `|`-separated fields, and ANYTHING else is a refusal
# (exit 2) rather than a skipped line. A malformed record must stop the run,
# because silently dropping one restores exactly the failure this issue
# removes — a document that under-claims and says nothing about it.
sub read_exceptions {
    my $path = "$ROOT/$EXCEPTIONS";
    return {} unless -e $path;
    open(my $fh, '<', $path) or fatal("open $path: $!");
    my (%by, %seen, @bad);
    my $lineno = 0;
    while (my $line = <$fh>) {
        $lineno++;
        chomp $line;
        next if $line =~ /^\s*(#|$)/;
        my @f = split /\s*\|\s*/, $line, -1;
        if (@f != 3) {
            push @bad, "$EXCEPTIONS:$lineno: expected 3 `|`-separated fields, got "
                     . scalar(@f) . ": $line";
            next;
        }
        my ($sec, $id, $text) = @f;
        for ($sec, $id, $text) { s/^\s+|\s+$//g }
        if (!length $sec || !length $id || !length $text) {
            push @bad, "$EXCEPTIONS:$lineno: empty field: $line";
            next;
        }
        if ($seen{"$sec\0$id"}++) {
            push @bad, "$EXCEPTIONS:$lineno: duplicate record for '$sec' / '$id'";
            next;
        }
        push @{ $by{$sec} }, [$id, $text];
    }
    close $fh;
    if (@bad) {
        print STDERR "refresh-traceability-matrix: REFUSING to run — the "
                   . "exceptions file is malformed.\n";
        print STDERR "  $_\n" for @bad;
        exit 2;
    }
    return \%by;
}

# The WHOLE document, assembled from sources. No read-modify-write.
sub emit_matrix {
    my ($subsys, $declared) = @_;
    my $exceptions = read_exceptions();
    my @report;
    my @sections;
    my @all_rows;
    # Which `###` companions belong to which `##` parent, read from @SUBSYS
    # order: a companion follows its parent. The relationship is load-bearing
    # in BOTH directions — the companion must not re-list the parent's plan,
    # and the parent must consult the companion before calling a row missing
    # (GH #121).
    my (%comp_srcs, %comp_bins);
    {
        # Attached by SHARED PLAN DOC, not by position. @SUBSYS lists every
        # `##` parent first and the `###` companions afterwards, so walking the
        # list and remembering the last parent seen hangs all eleven companions
        # off whichever parent happens to be last — which is what it did, and
        # the GH #121 fallback then fired for exactly one section.
        # plan_doc_path() is the real relationship: a companion resolves to the
        # same *-TEST-PLAN-DESIGN.md as its parent, which is also why the
        # companion must not re-list that plan.
        my %parent_of_doc;
        for my $entry (@$subsys) {
            my ($header, $bins, $srcs) = @$entry;
            next unless $header =~ /^##[^#]/;
            my $doc = plan_doc_path((as_list($srcs))[0]) or next;
            $parent_of_doc{$doc} //= $header;
        }
        for my $entry (@$subsys) {
            my ($header, $bins, $srcs) = @$entry;
            next unless $header =~ /^###/;
            my $doc = plan_doc_path((as_list($srcs))[0]) or next;
            my $parent = $parent_of_doc{$doc} or next;
            push @{ $comp_srcs{$parent} }, as_list($srcs);
            push @{ $comp_bins{$parent} }, as_list($bins);
        }
        # The declared cross-plan-doc fallbacks, merged in beside them.
        my $src_of = cmake_sources();
        for my $entry (@$subsys) {
            my ($header) = @$entry;
            my ($label) = $header =~ /^#+\s*([^—]+?)\s*—/ or next;
            for my $suite (@{ $EXTRA_STATUS_FALLBACK{$label} || [] }) {
                my $src = $src_of->{$suite}
                    or fatal("%EXTRA_STATUS_FALLBACK names '$suite', which "
                           . "CMake does not build");
                push @{ $comp_srcs{$header} }, as_list($src);
                push @{ $comp_bins{$header} }, "build/test/$suite";
            }
        }
    }

    for my $entry (@$subsys) {
        # Entries arrive from check_accounting() already resolved to
        # (header, binary, source(s)) — the binary path is not rebuilt here.
        my ($header, $bins, $srcs) = @$entry;
        my @flat    = as_list($srcs);
        my $binary  = $bins;
        my $is_comp = ($header =~ /^###/) ? 1 : 0;
        my $rows    = emit_section_rows($binary, \@flat, {
            plan          => !$is_comp,
            fallback      => $comp_srcs{$header} || [],
            fallback_bins => $comp_bins{$header} || [],
        });
        # Declared rows for a suite with no plan doc. They emit as `missing`
        # by construction: nothing asserts them, which is what makes them a
        # backlog rather than a coverage claim.
        my ($label) = split /\s+—\s+/, ($header =~ s/^#+\s*//r);
        my %have = map { $_->[0] => 1 } @$rows;
        for my $e (@{ $exceptions->{$label} || [] }) {
            next if $have{ $e->[0] };
            push @$rows, [$e->[0], $e->[1], '—', 'missing', '—'];
        }
        push @sections, emit_section_from_rows($header, $flat[0], $rows);
        push @all_rows, $rows;
        my %n;
        $n{ $_->[3] }++ for @$rows;
        my $uncited = grep { $_->[2] eq '—' } @$rows;
        push @report, [section_label($header, $flat[0]), scalar @$rows,
                       $n{pass} // 0, $n{fail} // 0, $n{skip} // 0,
                       $n{missing} // 0, (scalar @$rows) - $uncited, $uncited];
    }
    my @out = emit_preamble();
    push @out, '## Summary', '';
    push @out, $SUMMARY_BEGIN;
    # render_summary() returns an ARRAYREF, not a list.
    # $recorded_total is COMPUTED, not passed as a literal 0. It was, and the
    # committed document consequently published "Distinct row IDs recorded
    # anywhere in this document: 0" directly under a table listing 4105 rows —
    # a fabricated counter in a file whose own banner says every cell is
    # computed, and one no diff-based staleness gate can ever catch because it
    # is deterministic. (Found in review.)
    #
    # $xtra_total and $unref stay 0/empty because they are now STRUCTURALLY
    # zero: the "Extra coverage (not in plan)" tables and the unrefreshed
    # tables they counted do not exist in an emitted document.
    my %distinct;
    $distinct{ $_->[0] } = 1 for map { @$_ } @all_rows;
    push @out, @{ render_summary([map { [@{$_}[0 .. 5], 0] } @report],
                                 tombstoned_suites($declared),
                                 scalar(keys %distinct),
                                 declared_totals(), 0, []) };
    push @out, $SUMMARY_END, '';
    push @out, emit_cite_explainer();
    push @out, @sections;
    # The report is returned, never recomputed. main_body() used to build its
    # own by calling emit_section_rows() a SECOND time without the companion
    # options, so the stdout table applied the pre-fix "a companion re-lists
    # its parent's plan" rule and contradicted the Summary written into the
    # document by the same run (mmu_integration_test: 265 rows/206 missing on
    # stdout against 59/0 in the document). Two computations of one fact will
    # always drift; there is now one.
    return (\@out, \@report);
}

sub main_body {
    # ── The accounting gate, BEFORE anything is read or written ───────
    #
    # A refusal must leave the document untouched, so it runs first and exits
    # 2 without opening the matrix at all.
    my ($rc, $subsys, $declared) = check_accounting();
    if ($rc) {
        print STDERR "\nThe matrix was NOT rewritten.\n" unless $CHECK_ONLY;
        return $rc;
    }
    # Resolve the core HERE, before any emitting, so "I cannot validate
    # citations" is announced deterministically for every real run.
    #
    # It used to surface only as a side effect of the first citation lookup,
    # which means a tree that happens to cite no VHDL degrades in total
    # silence — and that is not hypothetical: the selftest fixture is such a
    # tree, and the first version of these rows passed VACUOUSLY against it,
    # asserting the absence of a warning that was never going to be emitted.
    # The condition being reported is a property of the RUN, not of whichever
    # rows happen to carry citations, so it is evaluated like one.
    # Not for --check-accounting (which never emits) and not for
    # --dump-descriptions, which is documented as reading nothing but the
    # test sources: it resolves no citation, so warning it about the core
    # is a diagnostic about a thing it does not do (found in review).
    vhdl_files() unless $CHECK_ONLY || $DUMP_DESC;
    if (defined $EMIT_TO) {
        # emit_matrix() returns TWO array refs, (\@out, \@report) — so a plain
        # `my @doc = emit_matrix(...)` collects the REFS, and join() stringified
        # them: this wrote a 2-line file reading "ARRAY(0x...)" twice, whatever
        # the document contained. The other two callers unpack it correctly
        # (the real writer takes ($doc, $rep); --emit-section takes ($doc)),
        # which is why only this flag was affected and nothing noticed — it has
        # no caller in the Makefile, the gates, the selftest or CI.
        #
        # It matters anyway: this flag exists to inspect the emitted document
        # BEFORE letting it replace the committed one, so silently emitting two
        # lines of hex is worst exactly when someone reaches for it.
        my ($doc) = emit_matrix($subsys, $declared);
        open(my $out, '>', $EMIT_TO) or fatal("write $EMIT_TO: $!");
        print $out join("\n", @$doc), "\n";
        close $out;
        printf("emitted %d lines to %s\n", scalar @$doc, $EMIT_TO);
        return 0;
    }
    if (defined $EMIT_SECTION) {
        for my $entry (@SUBSYS) {
            my ($header, $suite) = @$entry;
            next unless $header =~ /\Q$EMIT_SECTION\E/;
            # Emit the WHOLE document and print the requested section out of
            # it, rather than emitting the section standalone. A standalone
            # emit does not know its companions, so it would show the pre-fix
            # behaviour — and this flag exists to VALIDATE the emitter, which
            # makes a divergent debug view worse than no debug view.
            my ($doc) = emit_matrix($subsys, $declared);
            my $on = 0;
            for my $l (@$doc) {
                if ($l =~ /^#{2,3} /) { $on = ($l =~ /\Q$EMIT_SECTION\E/) ? 1 : 0; }
                print "$l\n" if $on;
            }
            return 0;
        }
        fatal("no section matching '$EMIT_SECTION'");
    }
    if ($DUMP_DESC) {
        my ($tot, $with, %shape) = (0, 0);
        for my $entry (@SUBSYS) {
            for my $suite (as_list($entry->[1])) {
                for my $src (as_list(cmake_sources()->{$suite})) {
                    next unless defined $src;
                    my $ids  = grep_row_ids($src);
                    my $desc = row_descriptions($src);
                    my $p    = helper_arg_positions($src);
                    my $plan = plan_descriptions($src);
                    my @from_plan = sort grep { !defined $desc->{$_}
                                                && defined $plan->{$_} } keys %$ids;
                    my @none = sort grep { !defined $desc->{$_}
                                           && !defined $plan->{$_} } keys %$ids;
                    $tot  += scalar keys %$ids;
                    $with += scalar keys %$ids;
                    $with -= scalar @none;
                    $shape{ join(',', map { "$_:id=$p->{$_}{id},desc="
                                          . (defined $p->{$_}{desc} ? $p->{$_}{desc} : 'NONE') }
                                      sort keys %$p) }++;
                    printf("%-52s %4d rows  %4d src  %4d plan  %4d NOT\n",
                           $src, scalar keys %$ids,
                           (scalar keys %$ids) - scalar @none - scalar @from_plan,
                           scalar @from_plan, scalar @none);
                    # Every one of them, never a sample: this list is the
                    # work-list for closing the gap, and a truncated one reads
                    # as a smaller problem than it is.
                    printf("      no description: %s\n", join(' ', @none))
                        if @none;
                    # $JNEXT_DESC_SHOW prints the DERIVED text for the ids it
                    # matches. Counting how many rows got a description says
                    # nothing about whether the right one was attributed —
                    # which is the failure a wrapper- or table-resolver makes.
                    if (my $pat = $ENV{JNEXT_DESC_SHOW}) {
                        for my $id (sort keys %$ids) {
                            next unless $id =~ /$pat/;
                            printf("      %-16s = %s\n", $id,
                                   defined $desc->{$id} ? $desc->{$id}
                                 : (defined $plan->{$id} ? "[plan] $plan->{$id}"
                                                         : '(none)'));
                        }
                    }
                }
            }
        }
        printf("\nTOTAL traced rows %d, described %d (%.1f%%), not %d\n",
               $tot, $with, $tot ? 100 * $with / $tot : 0, $tot - $with);
        printf("distinct declared helper shapes across traced suites: %d\n",
               scalar keys %shape);
        return 0;
    }
    if ($CHECK_ONLY) {
        printf("accounting OK: %d suites declared in test/unit-tests.conf, "
             . "%d traced, %d tombstoned, 0 unaccounted\n",
               scalar @$declared,
               scalar(grep { defined } map { @{[as_list($_->[1])]} } @SUBSYS),
               scalar(grep { exists $NO_MATRIX_SECTION{ $_->[0] } } @$declared));
        return 0;
    }

    # ── The document is EMITTED, never edited (GH #196 phase 2.1) ────
    #
    # What used to happen here: read the committed matrix, find each row it
    # already listed, rewrite three of its cells, leave everything else —
    # descriptions, the row set, the ordering — under hand control. That split
    # authority is what produced `frozen` (a hand cell with no computed side),
    # doc-vs-computed `drift`, `unrecorded` (a test with no row) and stale
    # locations. None of those classes can exist now: every cell is computed,
    # so there is nothing for a hand edit to disagree with.
    my ($doc, $rep) = emit_matrix($subsys, $declared);
    open(my $out, '>', $MATRIX) or fatal("write $MATRIX: $!");
    print $out join("\n", @$doc), "\n";
    close $out;

    # The SAME rows the document was written from — see emit_matrix().
    my @report = map { [@{$_}[0 .. 5], $_->[6], $_->[7], 0, 0, 0, 0] } @$rep;

    printf("\n%-38s %5s %5s %5s %5s %5s %6s %6s %6s %6s %6s %6s\n",
           'Subsystem', 'rows', 'pass', 'fail', 'skip', 'miss',
           'cited', 'uncit', 'drift', 'unrec', 'froz', 'xtra');
    print('-' x 113, "\n");
    my @totals = (0) x 11;
    for my $row (@report) {
        my ($label, @v) = @$row;
        printf("%-38s %5d %5d %5d %5d %5d %6d %6d %6d %6d %6d %6d\n", $label, @v);
        $totals[$_] += $v[$_] for 0 .. 10;
    }
    print('-' x 113, "\n");
    printf("%-38s %5d %5d %5d %5d %5d %6d %6d %6d %6d %6d %6d\n", 'TOTAL', @totals);

    # ── The one report that survives (GH #196 phase 2.4) ─────────────
    #
    # Every other class the old reports listed — frozen cells, doc-vs-computed
    # drift, unrecorded rows, protected rows, stale locations — is gone by
    # construction, because there is no longer a hand-written side to disagree
    # with. What CAN still disagree is two independent sources: a plan doc and
    # a test source citing different VHDL for the same row. That is genuine
    # signal about the spec, not bookkeeping, so it stays visible.
    my @cite_drift;
    for my $entry (@$subsys) {
        my ($header, $bins, $srcs) = @$entry;
        my @flat = as_list($srcs);
        my $plan = plan_cites($flat[0]);
        for my $src (@flat) {
            my (%cf, %cl, %nr);
            my $src_cites = grep_citations($src, \%cf, \%cl, \%nr);
            for my $id (sort keys %$src_cites) {
                next unless exists $plan->{$id};
                next unless defined $plan->{$id} && defined $src_cites->{$id};
                next if $plan->{$id} eq $src_cites->{$id};
                push @cite_drift,
                     sprintf('%-14s plan %-28s source %s (%s)',
                             $id, $plan->{$id}, $src_cites->{$id}, $src);
            }
        }
    }
    if (@cite_drift) {
        printf("\nPLAN-vs-SOURCE CITATION DISAGREEMENTS (%d). Two independent "
             . "sources cite\ndifferent VHDL for one row; read the VHDL and fix "
             . "whichever is wrong:\n", scalar @cite_drift);
        print "  $_\n" for @cite_drift;
    }

    return 0;
}

sub main {
    my $rc = eval { parse_args(@ARGV); main_body() };
    if ($@) { print STDERR $@; return 3; }
    return $rc;
}

exit(main());
