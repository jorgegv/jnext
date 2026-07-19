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
#
# Usage:
#     perl test/refresh-traceability-matrix.pl
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
    ['## Audio — `test/audio/audio_test.cpp`',
     'build/test/audio_test',      'test/audio/audio_test.cpp'],
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
    'test/audio/audio_test.cpp'         => 'AUDIO',
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
#   next    the first check()/skip() call after the ID literal — the shared
#           assertion of a table-driven row block ({"MMU-01", ...} arrays,
#           where the ID lives in an initialiser and the check() is in the
#           loop below it)
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

sub grep_source {
    my ($source_rel) = @_;
    my $abs = "$ROOT/$source_rel";
    my (%checks, %skips);

    open(my $fh, '<', $abs) or die "open $abs: $!";
    my @src = <$fh>;
    close $fh;

    for my $lineno (1 .. scalar @src) {
        my $line = $src[$lineno - 1];
        while ($line =~ /$SKIP_RE/g) {
            $skips{$1} //= $lineno;
        }
    }
    for my $lineno (1 .. scalar @src) {
        my $line = $src[$lineno - 1];
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
        my $cite;
        for my $c (@calls) {
            if ($c->{s} <= $L && $L <= $c->{e}) { $cite = $c->{cite}; last; }
        }
        $cite //= $named{$tid};
        if (!defined $cite) {
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

sub line_for {
    my ($tid, $checks, $skips) = @_;
    my $resolved = resolve_ids($tid, $checks, $skips);
    for my $r (@$resolved) {
        return $checks->{$r} if exists $checks->{$r};
        return $skips->{$r}  if exists $skips->{$r};
    }
    return undef;
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

sub refresh_section {
    my ($lines, $start_idx, $binary, $source_rel, $drift) = @_;
    my $fails = run_fails($binary);
    my ($checks, $skips) = grep_source($source_rel);
    my $cites     = grep_citations($source_rel);
    my $tombstone = $TOMBSTONE{$source_rel};

    my ($pass_ct, $fail_ct, $skip_ct, $missing_ct) = (0, 0, 0, 0);
    my ($cited_ct, $uncited_ct, $drift_ct) = (0, 0, 0);
    my $touched = 0;
    my $i = $start_idx + 1;

    while ($i < scalar @$lines) {
        my $line = $lines->[$i];

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

                    my $ln = line_for($tid_raw, $checks, $skips);
                    my $location = defined($ln) ? "$source_rel:$ln" : 'missing';
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

sub main {
    open(my $in, '<', $MATRIX) or die "open $MATRIX: $!";
    my $text = do { local $/; <$in> };
    close $in;

    # Mirror Python's splitlines(keepends=False): strip trailing newline
    # from the last element if present.
    my @lines = split(/\n/, $text, -1);
    pop @lines if @lines && $lines[-1] eq '';

    my @report;
    my @drift;
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
        my @section_drift;
        my ($touched, $p, $f, $s, $m, $c, $u, $d) =
            refresh_section(\@lines, $idx, $binary, $source_rel, \@section_drift);
        push @drift, map { "$source_rel  $_" } @section_drift;
        push @report, [$header, $touched, $p, $f, $s, $m, $c, $u, $d];
    }

    open(my $out, '>', $MATRIX) or die "write $MATRIX: $!";
    print $out join("\n", @lines), "\n";
    close $out;

    # cited/uncit count only the rows this run filled or could not fill —
    # rows that already carried a hand-written citation are in neither.
    printf("\n%-22s %5s %5s %5s %5s %5s %6s %6s %6s\n",
           'Subsystem', 'rows', 'pass', 'fail', 'skip', 'miss',
           'cited', 'uncit', 'drift');
    print('-' x 76, "\n");
    my @totals = (0, 0, 0, 0, 0, 0, 0, 0);
    for my $row (@report) {
        my ($header, $touched, $p, $f, $s, $m, $c, $u, $d) = @$row;
        my $short = $header;
        $short =~ s/^## //;
        $short =~ s/ — .*//;
        printf("%-22s %5d %5d %5d %5d %5d %6d %6d %6d\n",
               $short, $touched, $p, $f, $s, $m, $c, $u, $d);
        $totals[$_] += (($touched, $p, $f, $s, $m, $c, $u, $d)[$_]) for 0 .. 7;
    }
    print('-' x 76, "\n");
    printf("%-22s %5d %5d %5d %5d %5d %6d %6d %6d\n",
           'TOTAL', @totals);

    if (@drift) {
        print "\nVHDL citations where the doc and the test source disagree ",
              "(doc kept, not overwritten):\n";
        print "  $_\n" for @drift;
    }
}

main();
