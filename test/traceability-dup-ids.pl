#!/usr/bin/env perl
# Refuse when one test row ID is asserted by two different suites, or when a
# PLANNED row's ID is asserted by a suite that does not answer for it.
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
# PLANNED ROWS ARE IDs TOO (GH #243). The matrix mixes rows read from the
# subsystem plan docs with rows read from the test sources, so a planned ID
# that another suite asserts puts two different rows under one name: the CTC
# plan's JOY-01/02 sat in the matrix as `missing` beside uart_integration_test's
# passing JOY-01/02. The plan docs are read by refresh-traceability-matrix.pl
# and by nothing else, so this gate asks it (`--planned-ids`) rather than
# keeping a second parser that could drift from the first. A planned row
# asserted by one of the suites the matrix reads its status from — its own
# section's, its companions, its declared status fallbacks — is the ordinary
# planned-then-implemented row and is fine. Asserted anywhere else, it is a
# collision, and there is no baseline for that kind.
#
# The baseline in test/traceability-dup-ids.conf holds the asserted-vs-asserted
# collisions that already existed when this gate was written (29, measured
# 2026-08-01, by this script; 12 remain). Anything NOT in it is a refusal. The
# baseline shrinks by renaming one side of a pair, which means changing its
# plan doc and its test source together.
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

# A baseline entry that no longer describes a live collision is an amnesty
# waiting for its collision to come back. Refused, so the list shrinks on
# purpose and not by leaving dead entries behind (GH #243).
my @stale;
for my $id (sort keys %ALLOW) {
    my $live = join(', ', sort keys %{ $where{$id} || {} });
    push @stale, "$id: $ALLOW{$id}   (live: " . ($live eq '' ? 'none' : $live) . ")"
        unless $live eq $ALLOW{$id};
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

# Planned rows, from the matrix generator's own plan-doc reader.
my (@planned_clash, $planned);
{
    my $gen = "$RealBin/refresh-traceability-matrix.pl";
    open(my $ph, '-|', $^X, $gen, '--planned-ids')
        or die "traceability-dup-ids: cannot run $gen: $!\n";
    my @lines = <$ph>;
    if (!close $ph) {
        printf STDERR "traceability-dup-ids: REFUSING — %s --planned-ids "
                    . "failed (exit %d), so the planned rows could not be "
                    . "checked.\n", $gen, $? >> 8;
        exit 2;
    }
    for my $l (@lines) {
        chomp $l;
        my ($id, $doc, $owners) = split /\t/, $l;
        if (!defined $owners || $owners eq '') {
            print STDERR "traceability-dup-ids: REFUSING — unreadable "
                       . "--planned-ids line: '$l'\n";
            exit 2;
        }
        $planned++;
        my %own = map { $_ => 1 } split /,/, $owners;
        my @foreign = grep { !$own{$_} } sort keys %{ $where{$id} || {} };
        (my $plan = $doc) =~ s{.*/}{};
        push @planned_clash, "$id: planned in $plan (answered by $owners), "
                           . "asserted by " . join(', ', @foreign)
            if @foreign;
    }
}

if (@dups) {
    printf STDERR "traceability-dup-ids: REFUSING — %d test ID(s) asserted by "
                . "more than one suite.\nAn ID is a global name here; a duplicate "
                . "lets one subsystem's row vouch for\nanother's (GH #190). Rename "
                . "one side, or add it to test/traceability-dup-ids.conf.\n\n",
           scalar @dups;
    print STDERR "  $_\n" for @dups;
}
if (@planned_clash) {
    print STDERR "\n" if @dups;
    printf STDERR "traceability-dup-ids: REFUSING — %d PLANNED row ID(s) "
                . "asserted by a suite that does not\nanswer for them. The matrix "
                . "would carry two different rows under one name\n(GH #243). "
                . "Rename one side: the plan-doc row or the asserting suite's "
                . "row.\nThere is no baseline for this kind.\n\n",
           scalar @planned_clash;
    print STDERR "  $_\n" for @planned_clash;
}
if (@stale) {
    print STDERR "\n" if @dups || @planned_clash;
    printf STDERR "traceability-dup-ids: REFUSING — %d baseline entr%s in "
                . "test/traceability-dup-ids.conf no longer\nmatch%s a live "
                . "collision. Delete %s: a dead entry would silently re-admit\n"
                . "exactly that collision if it came back.\n\n",
           scalar @stale, @stale == 1 ? 'y' : 'ies', @stale == 1 ? 'es' : '',
           @stale == 1 ? 'it' : 'them';
    print STDERR "  $_\n" for @stale;
}
exit 2 if @dups || @planned_clash || @stale;
printf("traceability-dup-ids: OK — %d ids across %d suites and %d planned rows, "
     . "no collisions\n", scalar keys %where, scalar @suites, $planned // 0);
exit 0;
