#!/usr/bin/env perl
# Refresh per-row Status + Test file:line columns in
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
#   4. Edit the matrix in place: for each data row whose first cell is a
#      test ID, rewrite the Status cell and the Test file:line cell
#      preserving column widths. Section boundaries are matched by exact
#      header line.
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
);

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

sub refresh_section {
    my ($lines, $start_idx, $binary, $source_rel) = @_;
    my $fails = run_fails($binary);
    my ($checks, $skips) = grep_source($source_rel);

    my ($pass_ct, $fail_ct, $skip_ct, $missing_ct) = (0, 0, 0, 0);
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

                    my $ln = line_for($tid_raw, $checks, $skips);
                    my $location = defined($ln) ? "$source_rel:$ln" : 'missing';
                    my $orig_loc = $cells[5];
                    my $loc_width = length($orig_loc) - 2;
                    $loc_width = 0 if $loc_width < 0;
                    if (length($location) > $loc_width) {
                        $location = substr($location, 0, $loc_width);
                    }
                    $cells[5] = ' ' . sprintf("%-${loc_width}s", $location) . ' ';

                    $lines->[$i] = join('|', @cells);
                    $touched++;
                }
            }
        }
        $i++;
    }

    return ($touched, $pass_ct, $fail_ct, $skip_ct, $missing_ct);
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
    for my $entry (@SUBSYS) {
        my ($header, $binary, $source_rel) = @$entry;
        my $idx;
        for my $i (0 .. $#lines) {
            my $stripped = $lines[$i];
            $stripped =~ s/^\s+|\s+$//g;
            if ($stripped eq $header) {
                $idx = $i;
                last;
            }
        }
        if (!defined $idx) {
            print "NOT FOUND: $header\n";
            next;
        }
        my ($touched, $p, $f, $s, $m) =
            refresh_section(\@lines, $idx, $binary, $source_rel);
        push @report, [$header, $touched, $p, $f, $s, $m];
    }

    open(my $out, '>', $MATRIX) or die "write $MATRIX: $!";
    print $out join("\n", @lines), "\n";
    close $out;

    printf("\n%-22s %5s %5s %5s %5s %5s\n",
           'Subsystem', 'rows', 'pass', 'fail', 'skip', 'miss');
    print('-' x 52, "\n");
    my @totals = (0, 0, 0, 0, 0);
    for my $row (@report) {
        my ($header, $touched, $p, $f, $s, $m) = @$row;
        my $short = $header;
        $short =~ s/^## //;
        $short =~ s/ — .*//;
        printf("%-22s %5d %5d %5d %5d %5d\n",
               $short, $touched, $p, $f, $s, $m);
        $totals[0] += $touched;
        $totals[1] += $p;
        $totals[2] += $f;
        $totals[3] += $s;
        $totals[4] += $m;
    }
    print('-' x 52, "\n");
    printf("%-22s %5d %5d %5d %5d %5d\n",
           'TOTAL', @totals);
}

main();
