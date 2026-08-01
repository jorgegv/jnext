#!/usr/bin/env perl
# Refuse when one test row ID is asserted by two different suites.
#
# GH #196 phase 3.2. An ID is a GLOBAL name in this project: the traceability
# matrix, the plan docs and every report key on it, and #190's manufactured
# coverage came from exactly this — a row reading `pass` because an
# identically-named row in ANOTHER subsystem was vouching for it.
#
# ENUMERATED FROM test/unit-tests.conf, all 90 suites, and deliberately NOT
# from the matrix's sections: 49 suites are tombstoned and have no section, so
# a matrix-derived audit cannot see them at all.
#
# The baseline in test/traceability-dup-ids.conf holds the 29 collisions that
# already existed when this gate was written (measured 2026-08-01, by this
# script). Anything NOT in it is a refusal. The baseline shrinks by renaming
# one side of a pair, which means changing its plan doc and its test source
# together.
use strict;
use warnings;
use FindBin qw($RealBin);

my $ROOT = "$RealBin/..";

my %ALLOW;
{
    my $base = "$ROOT/test/traceability-dup-ids.conf";
    if (open(my $fh, '<', $base)) {
        while (my $l = <$fh>) {
            next if $l =~ /^\s*(#|$)/;
            my ($id, $suites) = $l =~ /^\s*(\S+)\s*:\s*(.+?)\s*$/ or next;
            $ALLOW{$id} = join(', ', sort split /\s*,\s*/, $suites);
        }
        close $fh;
    }
}

my $ID_RE = qr{"([A-Z][A-Z0-9_]*(?:\.[A-Z0-9_]+)*-[A-Za-z0-9._\-+]+|\d+\.\d+[a-z]?|S\d+\.\d+[a-z]?)"};

open(my $mf, '<', "$ROOT/test/unit-tests.conf") or die "open manifest: $!\n";
my @suites;
while (my $l = <$mf>) {
    next if $l =~ /^\s*(#|$)/;
    push @suites, $1 if $l =~ /^\s*(\S+)\s+\d+/;
}
close $mf;

# suite -> source, read from CMake exactly as the matrix generator does.
my %src;
for my $cm (glob("$ROOT/test/CMakeLists.txt"), glob("$ROOT/src/*/CMakeLists.txt"),
            glob("$ROOT/test/*/CMakeLists.txt")) {
    open(my $fh, '<', $cm) or next;
    my $dir = $cm; $dir =~ s{/CMakeLists\.txt$}{};
    my $text = do { local $/; <$fh> };
    close $fh;
    while ($text =~ /add_executable\s*\(\s*(\w+)\s+([^\)]+)\)/gs) {
        my ($suite, $files) = ($1, $2);
        for my $f (split /\s+/, $files) {
            next unless $f =~ /\.cpp$/;
            my $p = "$dir/$f";
            $src{$suite} //= $p if -f $p;
        }
    }
}

my (%where, @dups);
for my $suite (@suites) {
    my $path = $src{$suite} or next;
    open(my $fh, '<', $path) or next;
    while (my $line = <$fh>) {
        next if $line =~ m{^\s*//};
        $line =~ s/\bset_group\s*\(\s*"[^"]*"/set_group(/g;
        while ($line =~ /$ID_RE/g) { $where{$1}{$suite} = 1; }
    }
    close $fh;
}

for my $id (sort keys %where) {
    my @s = sort keys %{ $where{$id} };
    next if @s < 2;
    my $sig = join(', ', @s);
    next if defined $ALLOW{$id} && $ALLOW{$id} eq $sig;
    push @dups, "$id: $sig";
}

if (@dups) {
    printf STDERR "traceability-dup-ids: REFUSING — %d test ID(s) asserted by "
                . "more than one suite.\nAn ID is a global name here; a duplicate "
                . "lets one subsystem's row vouch for\nanother's (GH #190). Rename "
                . "one side, or add it to test/traceability-dup-ids.conf.\n\n",
           scalar @dups;
    print STDERR "  $_\n" for @dups;
    exit 2;
}
printf("traceability-dup-ids: OK — %d ids across %d suites, no collisions\n",
       scalar keys %where, scalar @suites);
exit 0;
