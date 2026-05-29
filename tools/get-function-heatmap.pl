#!/usr/bin/env perl
#
# get-function-heatmap.pl — Task 21 (2026-05-29) post-processing.
#
# Reads a jnext --profile output file on stdin and a z88dk .map file
# via -m FILE, then prints one line per code-section function
#   <function_name> <accumulated_tstates>
# sorted descending by T-states.
#
# Usage
#   cat profile.dat | tools/get-function-heatmap.pl -m demo/game/game.map
#
# z88dk .map file format (z80asm -m, also produced by z88dk's link
# stage). One label per line:
#   <symbol>                  = $<hex_addr> ; <type>, <scope>, <def>, <module>, <section>, <source>:<line>
#
# Where <type> is "addr" for real labels and "const" for section
# anchors (e.g. __code_compiler_head/__code_compiler_size). The
# <section> field is the section the symbol lives in. We keep ONLY
# entries where type == "addr" AND section starts with "code" (matches
# the standard z88dk sections code_compiler, code_crt0, code_user,
# code_lib_*, code_clib_*, etc.).
#
# Map-file physical-address support (`bbNNNN` format in the symbol
# address) is DEFERRED — v1 joins on the LOGICAL column (column 2) of
# the profile data. Column 1 (the physical bank + logical PC, e.g.
# `05c000`) is read but ignored for the lookup.
#
# Project convention: core Perl 5, no CPAN.

use strict;
use warnings;

my $map_file;
my @argv = @ARGV;
while (@argv) {
    my $arg = shift @argv;
    if ($arg eq '-m' && @argv) {
        $map_file = shift @argv;
    } elsif ($arg eq '-h' || $arg eq '--help') {
        print STDERR "usage: get-function-heatmap.pl -m FILE.map < profile.dat\n";
        exit 0;
    } else {
        die "unknown argument: $arg\n";
    }
}
die "usage: get-function-heatmap.pl -m FILE.map < profile.dat\n"
    unless defined $map_file;

# ── 1. Parse the .map file ─────────────────────────────────────────────
open my $mfh, '<', $map_file or die "cannot open map file '$map_file': $!\n";

# Labels we keep: type=addr AND section name matches a code section.
# z88dk's standard code sections all start with `code` (or `_CODE` for
# some legacy targets). Accept either form to be tolerant of older
# toolchains.
my @labels;   # list of [name => logical_addr]
while (my $line = <$mfh>) {
    chomp $line;
    # Format: NAME (whitespace) = $HEXADDR ; type, scope, def, module, section, src:line
    next unless $line =~ /^\s*(\S+)\s*=\s*\$?([0-9A-Fa-f]+)\s*;\s*(.*)$/;
    my ($name, $hex, $rest) = ($1, $2, $3);
    my @cols = split /\s*,\s*/, $rest;
    # cols: 0=type, 1=scope, 2=def, 3=module, 4=section, 5=source:line
    next unless @cols >= 5;
    my $type    = $cols[0];
    my $section = $cols[4];
    next unless defined $type && $type eq 'addr';
    next unless defined $section && $section =~ /^(?:code|_CODE)/i;
    my $addr = hex($hex);
    push @labels, [ $name, $addr ];
}
close $mfh;

die "no code-section labels found in '$map_file' — is this a z88dk .map?\n"
    unless @labels;

# Sort labels by address so we can binary-search the enclosing one for
# each profile sample.
@labels = sort { $a->[1] <=> $b->[1] } @labels;
my @addrs = map { $_->[1] } @labels;
my @names = map { $_->[0] } @labels;
my $n     = scalar @labels;

# Binary search: given $pc, return the index i such that
# $addrs[i] <= $pc < $addrs[i+1] (or i = -1 if pc < $addrs[0]).
sub enclosing_idx {
    my ($pc) = @_;
    return -1 if $pc < $addrs[0];
    my ($lo, $hi) = (0, $n - 1);
    while ($lo < $hi) {
        my $mid = int(($lo + $hi + 1) / 2);
        if ($addrs[$mid] <= $pc) {
            $lo = $mid;
        } else {
            $hi = $mid - 1;
        }
    }
    return $lo;
}

# ── 2. Stream the profile data on stdin ────────────────────────────────
my %hits;   # function name -> accumulated tstates
my $skipped_before_first_label = 0;

while (my $line = <STDIN>) {
    chomp $line;
    next if $line eq '';
    # Each line: PHYS_HEX LOG_HEX TSTATES
    my @f = split /\s+/, $line;
    next unless @f >= 3;
    my $log_pc  = hex($f[1]);
    my $tstates = $f[2] + 0;
    next if $tstates == 0;
    my $idx = enclosing_idx($log_pc);
    if ($idx < 0) {
        $skipped_before_first_label += $tstates;
        next;
    }
    $hits{ $names[$idx] } += $tstates;
}

# ── 3. Emit sorted by tstates desc ────────────────────────────────────
for my $name (sort { $hits{$b} <=> $hits{$a} || $a cmp $b } keys %hits) {
    next if $hits{$name} == 0;
    printf "%s %d\n", $name, $hits{$name};
}

if ($skipped_before_first_label > 0) {
    printf STDERR "# skipped %d t-states from PCs below first code label\n",
        $skipped_before_first_label;
}
