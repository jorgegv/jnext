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
# Usage:
#     perl test/refresh-traceability-matrix.pl
#
# Exit status: 0 when the matrix records every row its mapped test sources
# assert and every declared suite has a section; 1 when it does not. The
# matrix is rewritten either way — a non-zero exit means "the file is
# refreshed AND it under-records; here is the backlog", never "nothing was
# written".
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

my $ROOT   = abs_path("$RealBin/..");
my $MATRIX = "$ROOT/doc/testing/TRACEABILITY-MATRIX.md";

# (section_header_line, test_binary, source_rel_path)
# Every non-Z80N subsystem with per-row check()/skip() tracking. Z80N is
# deliberately excluded: it uses the FUSE data-driven runner and its
# per-row status is permanently `missing` by design.
my @SUBSYS = (
    ['## Memory/MMU — `test/mmu/mmu_test.cpp`',
     'build/test/mmu_test',        'test/mmu/mmu_test.cpp'],
    ['## ULA Video — `test/ula/ula_test.cpp`',
     'build/test/ula_test',        'test/ula/ula_test.cpp'],
    ['## Layer2 — `test/layer2/layer2_test.cpp`',
     'build/test/layer2_test',     'test/layer2/layer2_test.cpp'],
    ['## Sprites — `test/sprites/sprites_test.cpp`',
     'build/test/sprites_test',    'test/sprites/sprites_test.cpp'],
    ['## Tilemap — `test/tilemap/tilemap_test.cpp`',
     'build/test/tilemap_test',    'test/tilemap/tilemap_test.cpp'],
    ['## Copper — `test/copper/copper_test.cpp`',
     'build/test/copper_test',     'test/copper/copper_test.cpp'],
    ['## Compositor — `test/compositor/compositor_test.cpp`',
     'build/test/compositor_test', 'test/compositor/compositor_test.cpp'],
    # Audio is the one section whose header names several suites but has no
    # `### Companion` sub-tables — its AY/BP/IO/MX/NR/SD rows are interleaved
    # in a single table. Scanning only audio_test.cpp reported 51 of its 79
    # `missing` rows as untested when they are asserted in the two companion
    # files, so the entry lists all three (GH #117 review). Both fields accept
    # an arrayref; every other section stays a plain scalar.
    ['## Audio — `test/audio/audio_test.cpp`',
     ['build/test/audio_test',
      'build/test/audio_nextreg_test',
      'build/test/audio_port_dispatch_test'],
     ['test/audio/audio_test.cpp',
      'test/audio/audio_nextreg_test.cpp',
      'test/audio/audio_port_dispatch_test.cpp']],
    ['## DMA — `test/dma/dma_test.cpp`',
     'build/test/dma_test',        'test/dma/dma_test.cpp'],
    ['## DivMMC+SPI — `test/divmmc/divmmc_test.cpp`',
     'build/test/divmmc_test',     'test/divmmc/divmmc_test.cpp'],
    ['## CTC+Interrupts — `test/ctc/ctc_test.cpp`',
     'build/test/ctc_test',        'test/ctc/ctc_test.cpp'],
    ['## UART+I2C/RTC — `test/uart/uart_test.cpp`',
     'build/test/uart_test',       'test/uart/uart_test.cpp'],
    ['## NextREG — `test/nextreg/nextreg_test.cpp`',
     'build/test/nextreg_test',    'test/nextreg/nextreg_test.cpp'],
    ['## IO Port Dispatch — `test/port/port_test.cpp`',
     'build/test/port_test',       'test/port/port_test.cpp'],
    ['## Input — `test/input/input_test.cpp`',
     'build/test/input_test',      'test/input/input_test.cpp'],
    ['## Rewind — `test/rewind/rewind_test.cpp`',
     'build/test/rewind_test',     'test/rewind/rewind_test.cpp'],
    ['## Floating Bus — `test/floating_bus/floating_bus_test.cpp`',
     'build/test/floating_bus_test', 'test/floating_bus/floating_bus_test.cpp'],
    ['## VideoTiming — `test/videotiming/videotiming_test.cpp`',
     'build/test/videotiming_test', 'test/videotiming/videotiming_test.cpp'],
    ['## Contention — `test/contention/contention_test.cpp`',
     'build/test/contention_test', 'test/contention/contention_test.cpp'],
    ['## SD Card — `test/sdcard/sdcard_test.cpp`',
     'build/test/sdcard_test',     'test/sdcard/sdcard_test.cpp'],
    ['## NMI Source Pipeline — `test/nmi/nmi_test.cpp`',
     'build/test/nmi_test',        'test/nmi/nmi_test.cpp'],
    # Companion integration suites (sub-section ### headers).
    ['### Companion integration suite — `test/ula/ula_integration_test.cpp`',
     'build/test/ula_integration_test',     'test/ula/ula_integration_test.cpp'],
    ['### Companion integration suite — `test/compositor/compositor_integration_test.cpp`',
     'build/test/compositor_integration_test', 'test/compositor/compositor_integration_test.cpp'],
    ['### Companion integration suite — `test/ctc_interrupts/ctc_interrupts_test.cpp`',
     'build/test/ctc_interrupts_test',      'test/ctc_interrupts/ctc_interrupts_test.cpp'],
    ['### Companion integration suite — `test/nextreg/nextreg_integration_test.cpp`',
     'build/test/nextreg_integration_test', 'test/nextreg/nextreg_integration_test.cpp'],
    ['### Companion integration suite — `test/nmi/nmi_integration_test.cpp`',
     'build/test/nmi_integration_test',     'test/nmi/nmi_integration_test.cpp'],
    ['### Companion integration suite — `test/input/input_integration_test.cpp`',
     'build/test/input_integration_test',   'test/input/input_integration_test.cpp'],
    ['### Companion integration suite — `test/uart/uart_integration_test.cpp`',
     'build/test/uart_integration_test',    'test/uart/uart_integration_test.cpp'],
);

# Per-suite plan doc, consulted as the last citation source when the test
# source carries none. Keyed by source_rel so @SUBSYS stays untouched.
my %PLAN_DOC = (
    'test/mmu/mmu_test.cpp'             => 'MEMORY-MMU',
    'test/ula/ula_test.cpp'             => 'ULA-VIDEO',
    'test/ula/ula_integration_test.cpp' => 'ULA-VIDEO',
    'test/layer2/layer2_test.cpp'       => 'LAYER2',
    'test/sprites/sprites_test.cpp'     => 'SPRITES',
    'test/tilemap/tilemap_test.cpp'     => 'TILEMAP',
    'test/copper/copper_test.cpp'       => 'COPPER',
    'test/compositor/compositor_test.cpp'             => 'COMPOSITOR',
    'test/compositor/compositor_integration_test.cpp' => 'COMPOSITOR',
    'test/audio/audio_test.cpp'               => 'AUDIO',
    'test/audio/audio_nextreg_test.cpp'       => 'AUDIO',
    'test/audio/audio_port_dispatch_test.cpp' => 'AUDIO',
    'test/dma/dma_test.cpp'             => 'DMA',
    'test/divmmc/divmmc_test.cpp'       => 'DIVMMC-SPI',
    'test/ctc/ctc_test.cpp'                     => 'CTC-INTERRUPTS',
    'test/ctc_interrupts/ctc_interrupts_test.cpp' => 'CTC-INTERRUPTS',
    'test/uart/uart_test.cpp'             => 'UART-I2C',
    'test/uart/uart_integration_test.cpp' => 'UART-I2C',
    'test/nextreg/nextreg_test.cpp'             => 'NEXTREG',
    'test/nextreg/nextreg_integration_test.cpp' => 'NEXTREG',
    'test/port/port_test.cpp'             => 'IO-PORT-DISPATCH',
    'test/input/input_test.cpp'             => 'INPUT',
    'test/input/input_integration_test.cpp' => 'INPUT',
    'test/floating_bus/floating_bus_test.cpp' => 'FLOATING-BUS',
    'test/videotiming/videotiming_test.cpp'   => 'VIDEOTIMING',
    'test/contention/contention_test.cpp'     => 'CONTENTION',
    'test/nmi/nmi_test.cpp'                   => 'NMI-PIPELINE',
    'test/nmi/nmi_integration_test.cpp'       => 'NMI-PIPELINE',
);

# Suites with no VHDL counterpart at all: their spec is a jnext-internal
# contract or an external standard, not the FPGA core. An empty `—` there
# reads as "citation missing"; a tombstone says "there is nothing to cite",
# which is a different — and permanent — fact. Keyed by source_rel.
my %TOMBSTONE = (
    'test/rewind/rewind_test.cpp'   => '(jnext-internal)',
    'test/sdcard/sdcard_test.cpp'   => '(SD SPI spec)',
);

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
# Citations are also validated against the real FPGA source tree, so a
# typo'd or renamed VHDL filename is reported rather than published.

my $FPGA_SRC = $ENV{JNEXT_FPGA_SRC}
    || '/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src';

# `\.vhd` must not be a prefix of a longer identifier, or `row.vhdl_line`
# in a printf argument list is read as a citation of "row.vhd".
my $VHDL_CITE_RE = qr{
    \b ( [A-Za-z0-9_]+ \.vhd ) (?! [A-Za-z0-9_] )
    (?: \s* : \s*
        ( \d+ (?: \s* [-–] \s* \d+ )?
          (?: \s* [/,] \s* \d+ (?: \s* [-–] \s* \d+ )? )* ) )?
}x;

# Plan row IDs as they appear unquoted inside a comment ("TM-01:", "TM-01/02").
my $ID_BARE_RE = qr{
    \b ( [A-Z][A-Z0-9]* (?: \.[A-Z][A-Z0-9]* )* - [A-Za-z0-9._\-+]*[A-Za-z0-9] )
}x;

# "  FAIL ID: ..." or "  FAIL ID [..." — robust across all known harnesses.
my $FAIL_RE = qr/^\s*FAIL\s+([A-Za-z0-9._\-]+)\s*[:\[]/;

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
#                      "G1.AT-01", "G10.SC-01", "S1.05-mode"
#   2. Numeric dotted: "9.7", "14.6", "14.7a" (DMA plan rows)
#   3. Section-dotted: "S13.14", "S2.08" (ULA sections)
my $ID_LITERAL_RE = qr{
    "
    (
        [A-Z][A-Z0-9]* (?: \.[A-Z][A-Z0-9]* )* - [A-Za-z0-9._\-+]+
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
# the `test/unit-tests.conf` suite name. Every entry needs a reason: this
# map is the ONLY way a suite escapes the report, so an unreasoned entry is
# a silent hole.
my %NO_MATRIX_SECTION = (
    # Data-driven runners: they walk tests.in/tests.expected and have no
    # in-source row IDs at all, so there is nothing to trace per row. The
    # matrix says so itself under "Discrepancies noted"; the `## Z80N`
    # section's rows are opcode names and are permanently `missing`.
    'fuse_z80_test' => 'data-driven FUSE runner, no per-row IDs',
    'z80n_test'     => 'data-driven FUSE-style runner, opcode names not row IDs',
    # Narrative sections: hand-maintained tables that summarise ID *ranges*
    # ("XNEX-01..04") or point at rows kept by hand, not per-row IDs this
    # script can regenerate.
    'extended_nex_test'  => 'narrative section, ID ranges not per-row IDs',
    'atic_atac_nmi_test' => 'narrative section, hand-maintained (feeds protected NR-C0-02)',
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

sub run_fails {
    my ($binary) = @_;
    my $abs = "$ROOT/$binary";

    # Mirror Python subprocess.run's FileNotFoundError: refuse to "run" a
    # missing binary and silently see an empty FAIL set (which would
    # pass-whitewash every row in that section).
    die "refresh-traceability-matrix: binary not executable: $abs\n"
        unless -x $abs;

    my %fails;
    my $pid = open(my $fh, '-|');
    if (!defined $pid) {
        die "fork failed for $binary: $!";
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
                $fails{$1} = 1;
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
        die "refresh-traceability-matrix: $binary timed out after 180s\n";
    }

    return \%fails;
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
    open(my $fh, '<', $abs) or die "open $abs: $!";
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

# Set of VHDL basenames that actually exist in the FPGA core, or undef when
# the core is not checked out next to jnext (CI, a fresh clone) — in which
# case validation is skipped rather than failing every citation.
my $VHDL_FILES;
sub vhdl_files {
    return $VHDL_FILES if defined $VHDL_FILES;
    my %seen;
    if (-d $FPGA_SRC && open(my $fh, '-|', 'find', $FPGA_SRC, '-name', '*.vhd')) {
        while (my $p = <$fh>) {
            chomp $p;
            $p =~ s{.*/}{};
            $seen{$p} = 1;
        }
        close $fh;
    }
    $VHDL_FILES = %seen ? \%seen : 0;
    return $VHDL_FILES;
}

# First VHDL citation in a blob of text, normalised to "file.vhd:lines".
sub cite_in {
    my ($text) = @_;
    return undef unless $text =~ /$VHDL_CITE_RE/;
    my ($file, $lines) = ($1, $2);
    my $known = vhdl_files();
    if ($known && !$known->{$file}) {
        warn "WARN: citation names '$file', which is not in $FPGA_SRC\n";
        return undef;
    }
    return $file unless defined $lines;
    $lines =~ s/\s+//g;
    return "$file:$lines";
}

# Read plan-doc rows: "| ID | ... | ... zxnext.vhd:1234 ... |" -> citation.
my %PLAN_CACHE;
sub plan_cites {
    my ($source_rel) = @_;
    my $stem = $PLAN_DOC{$source_rel};
    return {} unless defined $stem;
    return $PLAN_CACHE{$stem} if $PLAN_CACHE{$stem};
    my %cites;
    my $path = "$ROOT/doc/testing/$stem-TEST-PLAN-DESIGN.md";
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
sub grep_citations {
    my ($source_rel) = @_;
    my $abs = "$ROOT/$source_rel";
    open(my $fh, '<', $abs) or die "open $abs: $!";
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
        push @calls, { s => $i, e => $j, cite => cite_in($text) };
    }

    # Comment blocks that name a row ID, and the first line each ID appears on.
    my (%named, %id_line);
    my $i = 0;
    while ($i <= $#src) {
        if ($src[$i] =~ m{^\s*//}) {
            my ($j, $text) = ($i, '');
            while ($j <= $#src && $src[$j] =~ m{^\s*//}) { $text .= $src[$j]; $j++; }
            if (my $c = cite_in($text)) {
                for my $id ($text =~ /$ID_BARE_RE/g) {
                    next if $id =~ /\.vhd/;
                    $named{$id} //= $c;
                }
            }
            $i = $j;
            next;
        }
        my $line = $src[$i];
        while ($line =~ /$ID_LITERAL_RE/g) { $id_line{$1} //= $i; }
        $i++;
    }

    my $plan = plan_cites($source_rel);
    my %cites;
    for my $tid (keys %id_line) {
        my $L = $id_line{$tid};
        my ($cite, $owns_call);
        for my $c (@calls) {
            if ($c->{s} <= $L && $L <= $c->{e}) {
                $owns_call = 1;
                $cite = $c->{cite};
                last;
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
        if (!defined $cite && !$owns_call) {
            for my $c (@calls) { if ($c->{s} > $L) { $cite = $c->{cite}; last; } }
        }
        $cite //= $plan->{$tid};
        $cites{$tid} = $cite if defined $cite;
    }
    # Rows the plan doc cites but no test source mentions stay resolvable:
    # `missing` status still deserves its citation.
    for my $tid (keys %$plan) { $cites{$tid} //= $plan->{$tid}; }
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
        my @cells = split(/\|/, $line, -1);
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

# Declared suites this matrix does not trace — the suite-level half of the
# same blindness.
#
# A suite counts as traced when @SUBSYS actually SCANS its source file, not
# merely when some header mentions it. Mentioning was the first cut of this
# check and it had exactly the hole it was written to find: the Audio header
# named three suites while the code scanned one, so 51 rows implemented in
# the other two were published as `missing` AND the gap satisfied the
# mention test, making it invisible to both halves of the report. Anything
# genuinely outside the script's per-row scope is now an explicit, reasoned
# entry in %NO_MATRIX_SECTION rather than an accident of prose. (GH #117
# review.)
sub unmapped_suites {
    my ($lines) = @_;
    my %scanned;
    for my $entry (@SUBSYS) {
        for my $src (as_list($entry->[2])) {
            (my $suite = $src) =~ s{.*/}{};
            $suite =~ s/\.cpp$//;
            $scanned{$suite} = 1;
        }
    }
    my $conf = "$ROOT/test/unit-tests.conf";
    open(my $fh, '<', $conf) or die "open $conf: $!";
    my @out;
    while (my $line = <$fh>) {
        next if $line =~ /^\s*#/ || $line !~ /\S/;
        my ($name, $rows) = split(' ', $line);
        $name =~ s/^\?//;               # `?` marks a GUI-gated suite
        next if $scanned{$name};
        next if exists $NO_MATRIX_SECTION{$name};
        push @out, [$name, $rows + 0];
    }
    close $fh;
    return \@out;
}

# (suite count, pinned row count) declared in test/unit-tests.conf — the
# project's own claim about how much it tests, and the denominator the head
# Summary compares itself against.
sub declared_totals {
    my $conf = "$ROOT/test/unit-tests.conf";
    open(my $fh, '<', $conf) or die "open $conf: $!";
    my ($suites, $rows) = (0, 0);
    while (my $line = <$fh>) {
        next if $line =~ /^\s*#/ || $line !~ /\S/;
        my (undef, $n) = split(' ', $line);
        $suites++;
        $rows += $n;
    }
    close $fh;
    return ($suites, $rows);
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
    my ($tid, $checks, $skips, $where) = @_;
    my $resolved = resolve_ids($tid, $checks, $skips);
    for my $r (@$resolved) {
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

# $stop_idx, when given, is the line index of the next @SUBSYS section
# header. Companion `###` sections are nested inside their parent `##`
# section, so without it the parent's scan runs straight through the
# companion's rows and counts them a second time (as `missing`, against the
# wrong source file) before the companion's own pass rewrites them. The
# bytes written were already correct — the companion runs last — but the
# tally double-counted, which a generated Summary cannot do.
sub refresh_section {
    my ($lines, $start_idx, $binary, $source_rel, $drift, $kept, $stop_idx) = @_;
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

    my (%checks, %skips, %where, %cites);
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
        my $cs = grep_citations($src);
        for my $id (keys %$cs) { $cites{$id} //= $cs->{$id}; }
        $tombstone //= $TOMBSTONE{$src};
    }
    my ($checks, $skips, $cites) = (\%checks, \%skips, \%cites);

    my ($pass_ct, $fail_ct, $skip_ct, $missing_ct) = (0, 0, 0, 0);
    my ($cited_ct, $uncited_ct, $drift_ct) = (0, 0, 0);
    my $touched = 0;
    my $i = $start_idx + 1;

    while ($i < scalar @$lines) {
        my $line = $lines->[$i];

        last if defined $stop_idx && $i >= $stop_idx;
        last if $line =~ /^## / && $i > $start_idx + 1;

        if ($line =~ /^\| / && index(substr($line, 2), '|') != -1) {
            # split preserving trailing empty fields
            my @cells = split(/\|/, $line, -1);
            # cells: ('', ' ID ', ' title ', ' vhdl ', ' status ', ' file:line ', '')
            if (scalar @cells >= 7) {
                my $tid_raw = $cells[1];
                $tid_raw =~ s/^\s+|\s+$//g;

                # Skip header row and separator row (only dashes/colons/spaces).
                if ($tid_raw ne '' && $tid_raw ne 'Test ID' && $tid_raw !~ /^[-:\s]+$/) {
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
                    my $cur_cite = $cells[3];
                    $cur_cite =~ s/^\s+|\s+$//g;
                    my $new_cite = cite_for($tid_raw, $cites, $checks, $skips)
                                   // $tombstone;
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
                    } elsif (defined $new_cite && $new_cite ne $cur_cite) {
                        $drift_ct++;
                        push @$drift, "$tid_raw: doc=[$cur_cite] source=[$new_cite]";
                    }

                    my ($lsrc, $ln) = line_for($tid_raw, $checks, $skips, \%where);
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
            $cited_ct, $uncited_ct, $drift_ct);
}

# Short, unique label for a section. The seven companion suites all share
# the header text "### Companion integration suite — ...", so they are
# labelled by their source file instead.
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
    my ($report, $unmapped, $recorded_total, $declared_suites, $declared_rows) = @_;
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
    if (@$unmapped) {
        my $rows = 0;
        $rows += $_->[1] for @$unmapped;
        push @out, sprintf(
            '**Suites with no section in this matrix: %d, %d live rows.** '
          . 'Their coverage is not traced here at all:',
            scalar @$unmapped, $rows);
        push @out, '';
        push @out, join(', ', map { "`$_->[0]` ($_->[1])" } @$unmapped);
    } else {
        push @out, '**Every suite declared in `test/unit-tests.conf` has a section here.**';
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
    die "refresh-traceability-matrix: generated-summary markers not found in "
      . "$MATRIX — expected a line '$SUMMARY_BEGIN' followed by '$SUMMARY_END'\n"
        unless defined $b && defined $e;
    splice(@$lines, $b + 1, $e - $b - 1, @$body);
}

# The process exit status, as a function of the two gaps — split out of
# main() so it can be asserted directly: the selftest loads this file
# without running main(), so the glue would otherwise be the one piece of
# the contract nothing pins.
sub report_exit_code {
    my ($unrec_ct, $unmapped) = @_;
    return ($unrec_ct || scalar @$unmapped) ? 1 : 0;
}

sub main {
    open(my $in, '<', $MATRIX) or die "open $MATRIX: $!";
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
    my $recorded = matrix_row_ids(\@lines);
    my $unmapped = unmapped_suites(\@lines);

    # Resolve every section header first, so each section knows where the
    # next one starts (see refresh_section's $stop_idx).
    my @found;
    for my $entry (@SUBSYS) {
        my ($header, $binary, $source_rel) = @$entry;
        my $idx;
        for my $i (0 .. $#lines) {
            my $stripped = $lines[$i];
            $stripped =~ s/^\s+|\s+$//g;
            # Prefix match instead of exact equality: section headers may have
            # gained " + `companion_test.cpp`" suffixes after companion suites
            # were created (ULA Video + ula_integration_test, CTC+Interrupts +
            # ctc_interrupts_test). The leading "## <Name> — `<file>`" stays
            # the discriminator.
            if ($stripped eq $header || index($stripped, $header . ' ') == 0) {
                $idx = $i;
                last;
            }
        }
        if (!defined $idx) {
            print "NOT FOUND: $header\n";
            next;
        }
        push @found, [$idx, $entry];
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

    my @report;
    my @drift;
    my @kept;
    my @unrec;
    my @aliased;
    for my $f (@found) {
        my ($idx, $entry) = @$f;
        my ($header, $binary, $source_rel) = @$entry;
        my $stop;
        for my $h (@header_idx) { $stop = $h, last if $h > $idx; }

        my @section_drift;
        my @section_kept;
        my ($touched, $p, $f_ct, $s, $m, $c, $u, $d) =
            refresh_section(\@lines, $idx, $binary, $source_rel,
                            \@section_drift, \@section_kept, $stop);
        my @sources = as_list($source_rel);
        push @drift, map { "$sources[0]  $_" } @section_drift;
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
                       $p, $f_ct, $s, $m, $c, $u, $d, $absent_ct];
    }

    replace_summary(\@lines,
        render_summary([map { [@{$_}[0 .. 5], $_->[9]] } @report],
                       $unmapped, scalar(keys %$recorded),
                       declared_totals()));

    open(my $out, '>', $MATRIX) or die "write $MATRIX: $!";
    print $out join("\n", @lines), "\n";
    close $out;

    # cited/uncit count only the rows this run filled or could not fill —
    # rows that already carried a hand-written citation are in neither.
    # `unrec` is the GH #117 direction: source rows the document omits.
    printf("\n%-38s %5s %5s %5s %5s %5s %6s %6s %6s %6s\n",
           'Subsystem', 'rows', 'pass', 'fail', 'skip', 'miss',
           'cited', 'uncit', 'drift', 'unrec');
    print('-' x 99, "\n");
    my @totals = (0, 0, 0, 0, 0, 0, 0, 0, 0);
    for my $row (@report) {
        my ($label, @v) = @$row;
        printf("%-38s %5d %5d %5d %5d %5d %6d %6d %6d %6d\n", $label, @v);
        $totals[$_] += $v[$_] for 0 .. 8;
    }
    print('-' x 99, "\n");
    printf("%-38s %5d %5d %5d %5d %5d %6d %6d %6d %6d\n", 'TOTAL', @totals);

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
    if (@$unmapped) {
        my $rows = 0;
        $rows += $_->[1] for @$unmapped;
        printf("\nUNMAPPED SUITES — declared in test/unit-tests.conf with no ".
               "section in this matrix (%d suites, %d live rows):\n",
               scalar @$unmapped, $rows);
        print "  ", join(' ', map { "$_->[0]($_->[1])" } @$unmapped), "\n";
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

    if (report_exit_code($unrec_ct, $unmapped)) {
        print "\nThe matrix WAS rewritten; it under-records the two sets above.\n",
              "Close them by adding the rows/sections by hand — the description\n",
              "column is the point of a matrix row and cannot be derived from\n",
              "the test source. (GH #117)\n";
    }
    return report_exit_code($unrec_ct, $unmapped);
}

exit(main());
