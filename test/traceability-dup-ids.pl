#!/usr/bin/env perl
# Refuse when one test row ID is asserted by two different suites.
#
# GH #196 phase 3.2. An ID is a GLOBAL name in this project: the traceability
# matrix, the plan docs and every report key on it, and #190's manufactured
# coverage came from exactly this — a row reading `pass` because an
# identically-named row in ANOTHER subsystem was vouching for it.
#
# ENUMERATED FROM test/unit-tests.conf — every suite it declares, the
# `?`-prefixed GUI-gated ones included — and deliberately NOT from the matrix's
# sections: tombstoned suites have no section, so a matrix-derived audit cannot
# see them at all.
#
# EVERY DECLARED SUITE MUST RESOLVE, or the run refuses (GH #243). This
# checker used to `next` past any suite it could not map to a source, and it
# did not strip the `?` prefix, so all 21 GUI-gated suites fell through that
# `next` in silence: 79 of 100 suites checked, "OK" reported, and 13 real
# collisions hiding in the other 21. A suite the gate cannot read is a suite
# the gate is not checking, and saying OK over it is the failure this file
# exists to prevent.
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
my (@suites, @unresolved);
while (my $l = <$mf>) {
    next if $l =~ /^\s*(#|$)/;
    if ($l !~ /^\s*(\S+)\s+\d+(?:\s|$)/) {
        chomp $l;
        push @unresolved, "unparseable manifest line: '$l'";
        next;
    }
    # `?` marks a GUI-gated suite; it is not part of the name (the matrix
    # generator strips it the same way — refresh-traceability-matrix.pl).
    (my $name = $1) =~ s/^\?//;
    push @suites, $name;
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
    my $path = $src{$suite};
    if (!defined $path) {
        push @unresolved, "$suite: no add_executable($suite <file>.cpp ...) in "
                        . "test/CMakeLists.txt, src/*/CMakeLists.txt or "
                        . "test/*/CMakeLists.txt";
        next;
    }
    my $fh;
    if (!open($fh, '<', $path)) {
        push @unresolved, "$suite: cannot read its source $path: $!";
        next;
    }
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

if (@unresolved) {
    printf STDERR "traceability-dup-ids: REFUSING — %d declared suite(s) could "
                . "not be read.\nA suite this gate cannot read is a suite it is "
                . "not checking; reporting OK\nover it would hide every collision "
                . "it holds (GH #243).\n\n",
           scalar @unresolved;
    print STDERR "  $_\n" for @unresolved;
    exit 2;
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
