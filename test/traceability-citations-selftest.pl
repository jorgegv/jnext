#!/usr/bin/env perl
# Self-test for the VHDL-citation extractor in
# `test/refresh-traceability-matrix.pl`.
#
# The extractor decides which VHDL lines justify each traceability-matrix row.
# It gets that from four row-local evidence tiers, and the whole design rests
# on one rule: **a row that has no citation of its own must stay uncited**,
# never inherit a neighbour's. Getting that wrong is silent — the matrix looks
# more complete, and the wrong citation is self-consistent on every later run,
# so it never shows up in the drift report either. That is exactly what
# happened in review on 2026-07-20 (`CT-INT-03`, a harness-plumbing check with
# no VHDL basis at all, was published citing `zxula.vhd:582-595` lifted from an
# unrelated check ~100 lines away).
#
# So each case below pins one tier, and the last two pin the *refusals*.
#
# Usage:
#     perl test/traceability-citations-selftest.pl
#
# Output follows the project-wide line:
#     Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

use strict;
use warnings;
use File::Temp qw(tempdir);
use Cwd qw(abs_path);
use FindBin qw($RealBin);

my $ROOT   = abs_path("$RealBin/..");
my $SCRIPT = "$ROOT/test/refresh-traceability-matrix.pl";

# Load the extractor without running main(). $ROOT is rebound to the temp
# tree so grep_citations() reads the fixture, and JNEXT_FPGA_SRC points at a
# fake core holding exactly the .vhd names the fixture is allowed to cite —
# so the filename whitelist is exercised for real, not bypassed.
my $FIXTURE_ROOT = tempdir(CLEANUP => 1);
mkdir "$FIXTURE_ROOT/fpga";
for my $vhd (qw(fixture_a.vhd fixture_b.vhd fixture_c.vhd fixture_d.vhd)) {
    open(my $fh, '>', "$FIXTURE_ROOT/fpga/$vhd") or die "write $vhd: $!";
    print $fh "-- fixture\n";
    close $fh;
}
$ENV{JNEXT_FPGA_SRC} = "$FIXTURE_ROOT/fpga";

my $code = do { open(my $fh, '<', $SCRIPT) or die "open $SCRIPT: $!"; local $/; <$fh> };
$code =~ s/\nmain\(\);\s*$//s
    or die "selftest: could not strip main() from $SCRIPT — has it been renamed?\n";
$code =~ s/^my \$ROOT\s+= abs_path\(.*?\);$/my \$ROOT = "$FIXTURE_ROOT";/m
    or die "selftest: could not rebind \$ROOT in $SCRIPT\n";
eval $code;
die "selftest: failed to load $SCRIPT: $@" if $@;

my ($total, $passed, $failed) = (0, 0, 0);

sub check {
    my ($id, $desc, $ok, $detail) = @_;
    $total++;
    if ($ok) { $passed++; printf("  PASS %s: %s\n", $id, $desc); }
    else     { $failed++; printf("  FAIL %s: %s%s\n", $id, $desc,
                                 defined $detail ? " — $detail" : ''); }
}

sub write_fixture {
    my ($rel, $body) = @_;
    my $abs = "$FIXTURE_ROOT/$rel";
    (my $dir = $abs) =~ s{/[^/]+$}{};
    my @parts = grep { length } split m{/}, $dir;
    my $sofar = ($dir =~ m{^/}) ? '' : '.';
    for my $part (@parts) {
        $sofar .= "/$part";
        mkdir $sofar unless -d $sofar;
    }
    open(my $fh, '>', $abs) or die "write $abs: $!";
    print $fh $body;
    close $fh;
    return $rel;
}

# ── The fixture ───────────────────────────────────────────────────────
#
# Every shape the extractor has to tell apart, in one file:
#
#   OWN-01   check() carrying its own citation                    -> call tier
#   NAMED-01 comment block naming the ID, citation in the comment -> named tier
#   TAB-01/02 IDs in an initialiser, one shared check() below     -> next tier
#   BARE-01  own check(), NO citation, followed by a cited check()-> must stay
#            uncited (the review defect)
#   BOGUS-01 own check() citing a .vhd that does not exist        -> whitelist
#
my $src = write_fixture('test/fixture/fixture_test.cpp', <<'CPP');
#include "fixture.h"

void group_own() {
    {
        check("OWN-01",
              "slot write lands on the right page — VHDL fixture_a.vhd:100-104",
              cond, detail);
    }

    // NAMED-01: enable bit clears on reset.
    // VHDL: fixture_b.vhd:200 — the reset handler.
    {
        check("NAMED-01", "enable clears on reset", cond, detail);
    }
}

void group_table() {
    // Table-driven rows: the IDs live in the initialiser, the assertion is
    // the single check() in the loop below.
    struct Row { const char* id; int slot; };
    const Row rows[] = {
        {"TAB-01", 0},
        {"TAB-02", 1},
    };
    for (const Row& r : rows) {
        check(r.id, "shared assertion — VHDL fixture_c.vhd:300", cond, detail);
    }
}

void group_bare() {
    // BARE-01 has a check() of its own, but that check() embeds no VHDL
    // citation. The NEXT check() belongs to a different row entirely.
    {
        check("BARE-01", "reference file exists and conf mentions it",
              cond, detail);
    }
    {
        check("OTHER-01",
              "unrelated assertion — VHDL fixture_d.vhd:400-410",
              cond, detail);
    }
}

void group_bogus() {
    {
        check("BOGUS-01",
              "cites a file that is not in the core — VHDL not_a_real.vhd:1",
              cond, detail);
    }
}
CPP

my $cites = do {
    # The bogus-filename case warns by design; capture it rather than letting
    # it pollute the report, and assert on it below.
    my @warnings;
    local $SIG{__WARN__} = sub { push @warnings, $_[0] };
    my $c = grep_citations($src);
    $c->{'__warnings'} = \@warnings;
    $c;
};
my $warnings = delete $cites->{'__warnings'};

check('SELF-01', 'call tier: own check() citation is used',
      ($cites->{'OWN-01'} // '') eq 'fixture_a.vhd:100-104',
      "got " . ($cites->{'OWN-01'} // '(none)'));

check('SELF-02', 'named tier: comment block naming the ID supplies the citation',
      ($cites->{'NAMED-01'} // '') eq 'fixture_b.vhd:200',
      "got " . ($cites->{'NAMED-01'} // '(none)'));

check('SELF-03', 'next tier: table-driven row takes the loop check() citation',
      ($cites->{'TAB-01'} // '') eq 'fixture_c.vhd:300'
        && ($cites->{'TAB-02'} // '') eq 'fixture_c.vhd:300',
      "got TAB-01=" . ($cites->{'TAB-01'} // '(none)')
        . " TAB-02=" . ($cites->{'TAB-02'} // '(none)'));

# The regression case. Before the 2026-07-20 fix this returned
# fixture_d.vhd:400-410 — OTHER-01's citation, attributed to BARE-01.
check('SELF-04', 'a row with its own uncited check() does NOT borrow the next row\'s citation',
      !defined $cites->{'BARE-01'},
      "got " . ($cites->{'BARE-01'} // '(none)'));

check('SELF-05', 'the next row keeps its own citation',
      ($cites->{'OTHER-01'} // '') eq 'fixture_d.vhd:400-410',
      "got " . ($cites->{'OTHER-01'} // '(none)'));

check('SELF-06', 'whitelist: a .vhd name absent from the FPGA tree is dropped, not published',
      !defined $cites->{'BOGUS-01'},
      "got " . ($cites->{'BOGUS-01'} // '(none)'));

check('SELF-07', 'whitelist: the rejected filename is reported, not swallowed',
      scalar(grep { /not_a_real\.vhd/ } @$warnings) == 1,
      scalar(@$warnings) . " warning(s): @$warnings");

# `row.vhdl_line` in a printf argument list must not read as "row.vhd".
my $src2 = write_fixture('test/fixture/fixture2_test.cpp', <<'CPP');
void group() {
    {
        check("IDENT-01", "no citation here",
              cond, fmt("mode=%d line=%s", row.mode, row.vhdl_line));
    }
}
CPP
my $cites2 = grep_citations($src2);
check('SELF-08', 'a longer identifier ending in .vhd is not read as a citation',
      !defined $cites2->{'IDENT-01'},
      "got " . ($cites2->{'IDENT-01'} // '(none)'));

printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
       $total, $passed, $failed, 0);
exit($failed ? 1 : 0);
