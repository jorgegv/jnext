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
sub parse_args {
    for my $arg (@_) {
        if ($arg eq '--check-accounting') { $CHECK_ONLY = 1; next; }
        fatal("unknown option '$arg'\n"
            . "usage: refresh-traceability-matrix.pl [--check-accounting]");
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
    # THREE `##` SECTIONS, NOT ONE PARENT WITH `###` COMPANIONS. The suites
    # reuse row IDs across files on purpose — `TRACE-01..04` mean different
    # things in the socket suite (what the TRANSPORT logs) and in the AT suite
    # (what the ENGINE logs), and `HOOK-01/02` (AT) continue as `HOOK-03..06b`
    # (adapter). Recording and the companion status fallback are both scoped to
    # the owning `##` subsystem, so filing them as one subsystem would let one
    # suite's `TRACE-01` vouch for the other's — the exact cross-section
    # collision GH #118 closed. Separate sections keep each scope honest.
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

my $FPGA_SRC = $ENV{JNEXT_FPGA_SRC}
    || '/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src';

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
    # four start where it ends: SDL pacing, WAV capture and the user gain
    # controls, none of which exist in the core.
    'audio_pacing_test'   => 'host SDL audio pacing/underrun policy, downstream of the mixer',
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

    # ── Host input translation ───────────────────────────────────────
    # Qt/SDL event -> ZX matrix translation on the HOST side. The guest-side
    # membrane matrix these feed is traced by `## Input`; what is asserted
    # here is the host key latch and the frontend key bindings, which the core
    # does not contain (it sees a PS/2 stream and a membrane, not a Qt event).
    'host_key_latch_test' => 'host key latch/debounce compensation; guest matrix is `## Input`',
    'pointer_capture_test' => 'host mouse-capture policy (window-manager behaviour)',
    'esc_break_test'      => 'host ESC->BREAK binding; guest matrix is `## Input`',
    'host_hotkey_test'    => 'host hotkey bindings (Alt vs the guest Symbol Shift)',
    'shifted_keys_test'   => 'host shifted-scancode translation; guest matrix is `## Input`',

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
    'debugger_video_panel_test' => 'debugger panel RENDERING; the hardware it displays is traced in `## Compositor`/`## Layer2`/`## ULA Video` (GUI-gated build)',
    'debugger_audio_panel_test' => 'debugger panel RENDERING; the hardware it displays is traced in `## Audio` (GUI-gated build)',
    'debugger_quit_gate_test'   => 'debugger quit gating (host GUI lifecycle)',
    'debugger_window_size_test' => 'debugger window geometry (host GUI)',
    'debugger_window_grow_test' => 'debugger window geometry (host GUI)',
    'debugger_accel_test'       => 'debugger keyboard accelerators (host GUI)',
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
    if (-d $FPGA_SRC && open(my $fh, '-|', 'find', $FPGA_SRC, '-name', '*.vhd')) {
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
    while ($text =~ /$VHDL_CITE_RE/g) {
        my ($cited, $lines) = ($1, $2);
        my ($verdict, $file) = resolve_vhd($cited);
        if ($verdict eq 'unknown') {
            warn "WARN: citation names '$cited', which is not in $FPGA_SRC\n";
            next;
        }
        # A wrong DIRECTORY beside a real basename: publish the basename (the
        # pre-GH #145 answer, so nothing is lost) and say so once per distinct
        # spelling. Once, because a single wrong path is written at dozens of
        # sites and a warning repeated forty times is the saturated-warning
        # failure this tool has already been bitten by twice.
        if ($verdict eq 'rehomed' && !$REHOMED_WARNED{$cited}++) {
            warn "WARN: citation names '$cited'; $FPGA_SRC has no such path, "
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
    my @calls;
    for my $i (0 .. $#src) {
        next unless $src[$i] =~ /\b(?:check|check_pred|check_eq|skip|stub)\s*\(/;
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
        push @calls, { s => $i, e => $j, cite => cite_in($text),
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
        # KNOWN HAZARD, measured 2026-07-31 (GH #188): the reach is UNBOUNDED,
        # and the table-driven signature this tier serves does not actually
        # require a loop with a check() in it. `layer2_test.cpp`'s
        # log_deferred() holds a 45-entry `deferred[]` table whose loop pushes
        # a TestResult directly and calls nothing — so all 45 rows resolve to
        # the first check() ANYWHERE below the table. That call happened to be
        # uncited, so the rows fell through to the plan doc and nobody noticed;
        # putting a citation in it handed one row's VHDL lines to all 45 at
        # once. Anyone citing a call must check what the tier then attributes
        # to whom. Not fixed here: bounding the reach is a tier rule change and
        # needs its own blast-radius measurement, in the manner of GH #147/#184.
        if (!defined $cite && !$owns_call) {
            for my $c (@calls) { if ($c->{s} > $L) { $cite = $c->{cite}; last; } }
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
sub refresh_section {
    my ($lines, $start_idx, $binary, $source_rel, $drift, $kept, $stop_idx,
        $companions, $invalid, $frozen) = @_;
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
    my $i = $start_idx + 1;

    while ($i < scalar @$lines) {
        my $line = $lines->[$i];

        last if defined $stop_idx && $i >= $stop_idx;
        last if $line =~ /^## / && $i > $start_idx + 1;

        if ($line =~ /^\| / && index(substr($line, 2), '|') != -1) {
            # split preserving trailing empty fields; `\|` is an escaped
            # literal, not a column break (GH #157)
            my @cells = split_row_cells($line);
            # cells: ('', ' ID ', ' title ', ' vhdl ', ' status ', ' file:line ', '')
            if (scalar @cells >= 7) {
                my $tid_raw = $cells[1];
                $tid_raw =~ s/^\s+|\s+$//g;

                # Skip header row and separator row (only dashes/colons/spaces).
                if ($tid_raw ne '' && $tid_raw ne 'Test ID' && $tid_raw !~ /^[-:\s]+$/) {
                    # Belt and braces for the residual GH #157 case the escape
                    # cannot cover: a RAW pipe in a Description is a genuine
                    # column break and nothing can tell it from an intended
                    # one. Say so instead of rewriting the wrong three cells.
                    warn "WARN: row $tid_raw has " . scalar(@cells)
                       . " cells, expected 7 — an unescaped `|` in a cell "
                       . "shifts VHDL/Status/Test-file right; write it `\\|`\n"
                        if scalar @cells > 7;

                    # Protected row (strategy point 6): leave it byte-identical.
                    # The existing Status cell is trusted for the counts, so
                    # the section tally stays truthful.
                    if ($line =~ $PROTECTED_RE) {
                        my $cur = $cells[4];
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

                    my $new_status = status_for($tid_raw, $fails, $checks, $skips);
                    if    ($new_status eq 'pass')    { $pass_ct++;    }
                    elsif ($new_status eq 'fail')    { $fail_ct++;    }
                    elsif ($new_status eq 'skip')    { $skip_ct++;    }
                    else                             { $missing_ct++; }

                    # Preserve column widths exactly. Guard against negative
                    # widths (narrow/empty cells): Perl sprintf with a
                    # negative field width flips alignment, Python ljust(-n)
                    # is a no-op — clamp to 0 to match.
                    my $orig_status = $cells[4];
                    my $width = length($orig_status) - 2;
                    $width = 0 if $width < 0;
                    $cells[4] = ' ' . sprintf("%-${width}s", $new_status) . ' ';

                    # VHDL citation: fill only when the cell is empty. A cell
                    # that already carries a citation was written by hand and
                    # stays — but a disagreement with the extracted one is
                    # reported, so drift surfaces without being clobbered.
                    # "Disagreement" is judged on canon_citation() of BOTH
                    # sides, so a cell spelled `5633, 6260` is not reported
                    # against the canonical `5633,6260`. The cell is compared
                    # normalised and written back untouched. (GH #142)
                    my $cur_cite = $cells[3];
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
                            my $cw = length($cells[3]) - 2;
                            $cw = 0 if $cw < 0;
                            $cells[3] = length($new_cite) > $cw
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

                    my ($lsrc, $ln) = line_for($tid_raw, $checks, $skips,
                                               \%where, \%cite_line);
                    my $location = defined($ln) ? "$lsrc:$ln" : 'missing';
                    my $orig_loc = $cells[5];
                    my $loc_width = length($orig_loc) - 2;
                    $loc_width = 0 if $loc_width < 0;
                    # Don't silently truncate — if the citation no longer fits
                    # the existing column width (typical when test functions
                    # grow past line 999), preserve the full text and warn.
                    # The row's column alignment is locally lost but the data
                    # is correct; markdown renderers tolerate per-row width
                    # variance. (Bug fixed 2026-04-27 after Task 7 r1+r2 broke
                    # ~213 citations via silent truncation.)
                    if (length($location) > $loc_width) {
                        warn "WARN: citation '$location' (id=$tid_raw) exceeds column width $loc_width; keeping full text\n";
                        $cells[5] = ' ' . $location . ' ';
                    } else {
                        $cells[5] = ' ' . sprintf("%-${loc_width}s", $location) . ' ';
                    }

                    $lines->[$i] = join('|', @cells);
                    $touched++;
                }
            }
        }
        $i++;
    }

    return ($touched, $pass_ct, $fail_ct, $skip_ct, $missing_ct,
            $cited_ct, $uncited_ct, $drift_ct, $frozen_ct, $tomb_ct);
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
    my ($report, $tombstoned, $recorded_total, $declared_suites, $declared_rows) = @_;
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

# Thin wrapper so every fatal() becomes exit 3 rather than an errno-derived
# status. The tail of this file stays the literal `exit(main());` the selftest
# strips to load the subs without running them.
sub main {
    my $rc = eval { parse_args(@ARGV); main_body() };
    if ($@) { print STDERR $@; return 3; }
    return $rc;
}

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
    if ($CHECK_ONLY) {
        printf("accounting OK: %d suites declared in test/unit-tests.conf, "
             . "%d traced, %d tombstoned, 0 unaccounted\n",
               scalar @$declared,
               scalar(grep { defined } map { @{[as_list($_->[1])]} } @SUBSYS),
               scalar(grep { exists $NO_MATRIX_SECTION{ $_->[0] } } @$declared));
        return 0;
    }

    open(my $in, '<', $MATRIX) or fatal("open $MATRIX: $!");
    my $text = do { local $/; <$in> };
    close $in;

    # Mirror Python's splitlines(keepends=False): strip trailing newline
    # from the last element if present.
    my @lines = split(/\n/, $text, -1);
    pop @lines if @lines && $lines[-1] eq '';

    # Read BEFORE any section is rewritten. The global set is still needed
    # for the Summary's "recorded anywhere in this document" figure; the
    # `unrecorded` question is now asked per subsystem (GH #118), against
    # the scope subsystem_span() resolves below.
    my $recorded   = matrix_row_ids(\@lines);
    my $tombstoned = tombstoned_suites($declared);

    # Resolve every section header first, so each section knows where the
    # next one starts (see refresh_section's $stop_idx).
    my ($found, $missing_sections) = resolve_sections(\@lines, $subsys);
    my @found            = @$found;
    my @missing_sections = @$missing_sections;

    # The same hole one level down. A suite named in @SUBSYS is TRACED as far
    # as the accounting gate is concerned, but if its section header is not
    # actually in the document then nothing scans it and its rows are recorded
    # nowhere — the exact condition the gate exists to make impossible. This
    # used to print `NOT FOUND` and carry on, which is a warning line inside a
    # report again. Refuse, and write nothing.
    my ($section_rc, $may_write) = section_refusal(\@missing_sections);
    if (!$may_write) {
        print STDERR
            "refresh-traceability-matrix: REFUSING to run — \@SUBSYS traces a\n"
          . "suite whose section is not in $MATRIX. A traced suite with no\n"
          . "section is recorded nowhere, which is what tracing is supposed to\n"
          . "prevent. Add the section, or tombstone the suite.\n\n";
        print STDERR "  $_\n" for @missing_sections;
        print STDERR "\nThe matrix was NOT rewritten.\n";
        return $section_rc;
    }
    my @header_idx = sort { $a <=> $b } map { $_->[0] } @found;

    # Per-subsystem recording scope, resolved up front from the unrewritten
    # document (GH #118). Keyed by the entry's own header index; several
    # entries can share one scope (a `##` parent and its `###` companions).
    my %scope;
    for my $f (@found) {
        my ($from, $to) = subsystem_span(\@lines, $f->[0]);
        $scope{ $f->[0] } = matrix_row_ids(\@lines, $from, $to);
    }

    # Which other suites of this subsystem a section may fall back to for a
    # row its own source does not assert (GH #121).
    my $companion = companion_map(\@lines, \@found);

    my @report;
    my @drift;
    my @kept;
    my @unrec;
    my @aliased;
    my @invalid;
    my @frozen;
    my $tomb_excluded = 0;
    for my $f (@found) {
        my ($idx, $entry) = @$f;
        my ($header, $binary, $source_rel) = @$entry;
        my $stop;
        for my $h (@header_idx) { $stop = $h, last if $h > $idx; }

        my @section_drift;
        my @section_kept;
        my @section_invalid;
        my @section_frozen;
        my ($touched, $p, $f_ct, $s, $m, $c, $u, $d, $fz, $tz) =
            refresh_section(\@lines, $idx, $binary, $source_rel,
                            \@section_drift, \@section_kept, $stop,
                            $companion->{$idx}, \@section_invalid,
                            \@section_frozen);
        my @sources = as_list($source_rel);
        # Labelled by SECTION, not by source file: a hand-written cell has no
        # source file behind it — that is what makes it hand-written.
        push @invalid, map { section_label($header, $sources[0]) . "  $_" }
                       @section_invalid;
        # Same labelling as @invalid, and for the same reason: a frozen cell
        # is hand-written, so it has no source file behind it.
        push @frozen, map { [section_label($header, $sources[0]), @$_] }
                      @section_frozen;
        $tomb_excluded += $tz;
        # Drift lines arrive already charged to the file that supplied the
        # citation (GH #126) — which may be a companion suite or a plan doc,
        # neither of which is $sources[0]. Protected rows have no such file
        # by construction: the marker exists because the coverage is
        # elsewhere and hand-maintained, so they stay labelled by section.
        push @drift, @section_drift;
        push @kept,  map { "$sources[0]  $_" } @section_kept;

        # The other direction (GH #117): rows these sources assert that the
        # document records nowhere. Reported per source file, so the backlog
        # names the file to edit even for a multi-suite section.
        my $absent_ct = 0;
        my $scope = $scope{$idx};
        for my $src (@sources) {
            my $rows = grep_row_ids($src);
            # `ID:line` so the backlog can be walked straight to the assertion.
            my @absent = map { "$_:$rows->{$_}" }
                         sort grep { !matrix_records($_, $scope) } keys %$rows;
            if (@absent) {
                push @unrec, [$src, \@absent];
                $absent_ct += scalar @absent;
            }
            # The sub-letter blind spot, made visible (GH #118).
            my @alias = map { "$_:$rows->{$_}" }
                        sort grep { recorded_only_by_alias($_, $scope) }
                        keys %$rows;
            push @aliased, [$src, \@alias] if @alias;
        }

        push @report, [section_label($header, $sources[0]), $touched,
                       $p, $f_ct, $s, $m, $c, $u, $d, $absent_ct, $fz];
    }

    replace_summary(\@lines,
        render_summary([map { [@{$_}[0 .. 5], $_->[9]] } @report],
                       $tombstoned, scalar(keys %$recorded),
                       declared_totals()));

    open(my $out, '>', $MATRIX) or fatal("write $MATRIX: $!");
    print $out join("\n", @lines), "\n";
    close $out;

    # cited/uncit count only the rows this run filled or could not fill —
    # rows that already carried a hand-written citation are in neither.
    # `unrec` is the GH #117 direction: source rows the document omits.
    printf("\n%-38s %5s %5s %5s %5s %5s %6s %6s %6s %6s %6s\n",
           'Subsystem', 'rows', 'pass', 'fail', 'skip', 'miss',
           'cited', 'uncit', 'drift', 'unrec', 'froz');
    print('-' x 106, "\n");
    my @totals = (0) x 10;
    for my $row (@report) {
        my ($label, @v) = @$row;
        printf("%-38s %5d %5d %5d %5d %5d %6d %6d %6d %6d %6d\n", $label, @v);
        $totals[$_] += $v[$_] for 0 .. 9;
    }
    print('-' x 106, "\n");
    printf("%-38s %5d %5d %5d %5d %5d %6d %6d %6d %6d %6d\n", 'TOTAL', @totals);

    if (@drift) {
        print "\nVHDL citations where the doc and the test source disagree ",
              "(doc kept, not overwritten):\n";
        print "  $_\n" for @drift;
    }

    if (@kept) {
        print "\nProtected rows kept byte-identical ",
              "(<!-- protected --> marker):\n";
        print "  $_\n" for @kept;
    }

    # ── The GH #150 report ────────────────────────────────────────────
    #
    # Hand-written cells validated the same way computed ones are. Reported,
    # never rewritten — a hand-written citation is somebody's judgement and the
    # tool has no standing to overrule it, only to say it does not check out.
    if (@invalid) {
        printf("\nHAND-WRITTEN CITATIONS THAT DO NOT VALIDATE (%d). The cell is "
             . "KEPT; fix it by\nhand. A citation to jnext's own source is not "
             . "evidence — the VHDL is the oracle:\n", scalar @invalid);
        print "  $_\n" for @invalid;
    }

    # ── The GH #188 report ────────────────────────────────────────────
    #
    # Hand-written cells with NO computed side. GH #150 made a hand-written
    # cell complain when it does not validate; this is the other half — the
    # cells nothing can even disagree with. Listed so the number is
    # shrinkable, split by sub-class because the remedies differ, and NEVER
    # rewritten: closing one of these means a human reading the VHDL.
    if (@frozen) {
        my %by;
        push @{ $by{ $_->[2] } }, $_ for @frozen;
        my %what = (
            a => 'no citation attempt — cite the VHDL in the row\'s own '
               . 'check(), which makes the cell computed and drift-checked',
            b => 'line numbers with the FILENAME left out ("VHDL 7163-7176") '
               . '— write the .vhd in the row\'s own check()',
            c => 'this section\'s sources do not assert the row (status '
               . 'missing) — record it where it runs, or mark it planned',
        );
        printf("\nFROZEN CITATIONS — hand-written cells the extractor computes "
             . "NOTHING for (%d).\nDrift needs a computed side to disagree "
             . "with, so nothing can ever contradict\nthese and they stay "
             . "frozen: GH #187 was one of them. The cell is KEPT.\n"
             . "Excluded: %d cells whose suite carries a declared %%TOMBSTONE "
             . "— the tombstone IS\ntheir computed side, so those ARE compared "
             . "and can appear in the drift list above.\n",
               scalar @frozen, $tomb_excluded);
        for my $cls (sort keys %by) {
            printf("  (%s) %s — %d\n", $cls, $what{$cls},
                   scalar @{ $by{$cls} });
            for my $r (@{ $by{$cls} }) {
                printf("      %s  %s: [%s]\n", $r->[0], $r->[1], $r->[3]);
            }
        }
    }

    # ── The GH #117 report ────────────────────────────────────────────
    my $unrec_ct = 0;
    $unrec_ct += scalar @{ $_->[1] } for @unrec;
    if (@unrec) {
        print "\nUNRECORDED — rows the test source asserts that this matrix ",
              "does not list in the\nowning subsystem's section ($unrec_ct):\n";
        for my $u (@unrec) {
            printf("  %s (%d)\n", $u->[0], scalar @{ $u->[1] });
            print "    ", join(' ', @{ $u->[1] }), "\n";
        }
    }
    if (@$tombstoned) {
        my $rows = 0;
        $rows += $_->[1] for @$tombstoned;
        printf("\nTOMBSTONED SUITES — declared in test/unit-tests.conf, no ".
               "section here, reason recorded (%d suites, %d live rows).\n".
               "Not a gap: each has an authority that is not the FPGA core. ".
               "See the Summary table.\n",
               scalar @$tombstoned, $rows);
    }

    # ── The GH #118 sub-letter blind spot, made visible ───────────────
    #
    # Not a gate: these ARE recorded, by their parent row, and 90 of the
    # 102 triaged in GH #118 were right to be. Printed so the next one that
    # is NOT — a distinct regression wearing a sub-letter, as `FB-04b`,
    # `NA-01c` and `REG-03c` were — lands in a list a human reads.
    my $alias_ct = 0;
    $alias_ct += scalar @{ $_->[1] } for @aliased;
    if (@aliased) {
        print "\nALIASED — rows recorded ONLY by sub-letter aliasing, i.e. the ",
              "matrix lists\nthe parent `X-01` but not `X-01b` ($alias_ct). ",
              "Not a gap by itself: check that\neach is a sub-case of its ",
              "parent plan row, not a distinct assertion (GH #118):\n";
        for my $a (@aliased) {
            printf("  %s (%d)\n", $a->[0], scalar @{ $a->[1] });
            print "    ", join(' ', @{ $a->[1] }), "\n";
        }
    }

    if (report_exit_code($unrec_ct)) {
        print "\nThe matrix WAS rewritten; it under-records the UNRECORDED set above.\n",
              "Close it by adding the rows by hand — the description column is the\n",
              "point of a matrix row and cannot be derived from the test source.\n",
              "(GH #117)\n";
    }
    return report_exit_code($unrec_ct);
}

exit(main());
