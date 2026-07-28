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
$code =~ s/\nexit\(main\(\)\);\s*$//s
    or die "selftest: could not strip main() from $SCRIPT — has it been renamed?\n";
$code =~ s/^my \$ROOT\s+= abs_path\(.*?\);$/my \$ROOT = "$FIXTURE_ROOT";/m
    or die "selftest: could not rebind \$ROOT in $SCRIPT\n";
eval $code;
die "selftest: failed to load $SCRIPT: $@" if $@;

my ($total, $passed, $failed) = (0, 0, 0);

# The arity guard is load-bearing, not defensive. `$x =~ /a/ && $x =~ /b/`
# written directly in this argument list evaluates the second match in LIST
# context: on failure it yields the empty list, so the detail string slides
# into the $ok slot and the row PASSES with a broken assertion. That is a
# harness that lies in exactly the direction nobody checks. Three rows were
# written that way (SELF-10 since the original twelve); they now wrap the
# condition in scalar(), and this guard stops the next one silently.
sub check {
    die "selftest: check() got " . scalar(@_) . " args, expected 4 — a "
      . "condition collapsed to a list (wrap it in scalar())\n"
        unless @_ == 4;
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

void group_continuation() {
    // A citation's line list may continue without repeating the filename
    // (GH #136). Both spellings below name TWO places in one file; stopping
    // at the first publishes half of what the assertion exercises.
    {
        check("CONT-01",
              "comma continuation — VHDL fixture_a.vhd:100, :200-204",
              cond, detail);
    }
    {
        check("CONT-02",
              "plus continuation — VHDL fixture_b.vhd:10 + :20 + :30-31",
              cond, detail);
    }
    // The boundary: prose between the two refs is NOT a separator. Reaching
    // across it is how a citation starts absorbing the sentence around it.
    {
        check("CONT-03",
              "prose interrupts — VHDL fixture_c.vhd:900 (the latch), :950",
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

# ── Filename-omitting continuations (GH #136) ─────────────────────────
#
# `zxnext.vhd:5080, :6188-6189` reads to a human as two places in one file,
# and the extractor used to stop at `:5080` — publishing a citation that
# covered half the assertion, with nothing anywhere to say so. When this was
# found the shorthand was truncating 154 distinct row IDs across 20 suites.
#
# SELF-63 is the discriminative counterpart: widening the separator set is
# only safe while it stays punctuation adjacent to the previous ref. The
# moment it reaches across prose, the citation starts absorbing the
# sentence, and a wrong citation is worse than an incomplete one.

check('SELF-61', 'a comma continuation that omits the filename is carried, not dropped',
      ($cites->{'CONT-01'} // '') eq 'fixture_a.vhd:100,200-204',
      "got " . ($cites->{'CONT-01'} // '(none)'));

check('SELF-62', 'a `+` continuation chain is carried and normalised to the published `,` form',
      ($cites->{'CONT-02'} // '') eq 'fixture_b.vhd:10,20,30-31',
      "got " . ($cites->{'CONT-02'} // '(none)'));

check('SELF-63', 'a continuation separated by prose is NOT reached across',
      ($cites->{'CONT-03'} // '') eq 'fixture_c.vhd:900',
      "got " . ($cites->{'CONT-03'} // '(none)'));

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

# ── Protected-row marker (GH #105) ────────────────────────────────────
#
# A hand-maintained row (e.g. a cross-file pointer like NR-C0-02 ->
# test/nmi/atic_atac_nmi_test.cpp) carries `<!-- protected -->` after its
# closing `|`. refresh_section must leave it byte-identical — before the
# fix its status+file cells were recomputed against the section's ONE
# source file and downgraded to `missing | missing` on every regen.
# An unprotected row in the same section must still regenerate normally.

my $src3 = write_fixture('test/fixture/section_test.cpp', <<'CPP');
void section() {
    {
        check("UNPROT-01", "regenerated normally — VHDL fixture_a.vhd:10",
              cond, detail);
    }
    {
        check("PROT-02", "locally covered yet marked protected",
              cond, detail);
    }
}
CPP

# Fixture suite binary: no FAIL lines, so source-scanned rows read `pass`.
my $bin = "$FIXTURE_ROOT/bin/section_suite";
mkdir "$FIXTURE_ROOT/bin";
open(my $bfh, '>', $bin) or die "write $bin: $!";
print $bfh "#!/bin/sh\n";
print $bfh "echo '  PASS UNPROT-01: ok'\n";
print $bfh "echo 'Total:    1  Passed:    1  Failed:    0  Skipped:    0'\n";
close $bfh;
chmod 0755, $bin;

my $prot_row = '| PROT-01   | hand-maintained cross-file row | fixture_b.vhd:200 | pass    | test/other/other_suite_test.cpp (OTHER-77) | <!-- protected: cross-file, hand-maintained -->';
my $prot2_row = '| PROT-02   | protected but locally covered  | fixture_c.vhd:300 | pass    | test/other/other_suite_test.cpp (OTHER-78) | <!-- protected -->';
my @mlines = (
    '## Fixture — `test/fixture/section_test.cpp`',
    '',
    '| Test ID   | Description                    | VHDL file:line    | Status  | Test file:line                             |',
    '|-----------|--------------------------------|-------------------|---------|--------------------------------------------|',
    '| UNPROT-01 | regenerated normally           | fixture_a.vhd:10  | missing | missing                                    |',
    $prot_row,
    $prot2_row,
);

my $sec_warnings = [];
my (@drift, @kept);
{
    local $SIG{__WARN__} = sub { push @$sec_warnings, $_[0] };
    refresh_section(\@mlines, 0, 'bin/section_suite',
                    'test/fixture/section_test.cpp', \@drift, \@kept);
}

check('SELF-09', 'protected row survives refresh_section byte-identical',
      $mlines[5] eq $prot_row,
      "got [$mlines[5]]");

check('SELF-10', 'unprotected row in the same section still regenerates',
      scalar($mlines[4] =~ /\|\s*pass\s*\|/
             && $mlines[4] =~ m{test/fixture/section_test\.cpp:\d+}),
      "got [$mlines[4]]");

check('SELF-11', 'protected rows are reported in the kept list',
      "@kept" eq 'PROT-01 PROT-02',
      "kept=[@kept]");

check('SELF-12', 'a marker shadowing a locally covered row is warned about',
      scalar(grep { /protected row PROT-02 is also covered/ } @$sec_warnings) == 1
        && scalar(grep { /protected row PROT-01 is also covered/ } @$sec_warnings) == 0,
      scalar(@$sec_warnings) . " warning(s): @$sec_warnings");

# ── The `unrecorded` direction (GH #117) ──────────────────────────────
#
# The script used to look only at matrix rows, so it could say "this row has
# no test" but never "this test has no row" — and never "this whole suite
# has no section". Both are reported now. The rows below pin the two things
# that make the report trustworthy: it must be PRECISE (a false accusation
# is as bad as a silent omission) and it must not read its own output.

my $src4 = write_fixture('test/fixture/rows_test.cpp', <<'CPP');
void rows() {
    set_group("GRP-01");
    // "PROSE-01" is named only in this comment — a cross-reference, not
    // an assertion. VHDL fixture_a.vhd:1.
    check("ROW-01", "a plain row", cond, detail);

    set_group("ROW-02");
    check("ROW-02", "a banner label that is also a real row", cond, detail);

    struct Row { const char* id; };
    const Row rows[] = { {"TAB-09"} };
    for (const Row& r : rows) {
        check(r.id, "shared assertion", cond, detail);
    }
}
CPP

my $rows = grep_row_ids($src4);

check('SELF-13', 'a set_group() banner label is not a row',
      !exists $rows->{'GRP-01'},
      "got " . (exists $rows->{'GRP-01'} ? "line $rows->{'GRP-01'}" : '(none)'));

check('SELF-14', 'set_group() is excluded per occurrence, not per ID: a label that is also asserted stays a row',
      exists $rows->{'ROW-02'},
      "got " . (exists $rows->{'ROW-02'} ? "line $rows->{'ROW-02'}" : '(none)'));

check('SELF-15', 'an ID quoted inside a // comment is not a row',
      !exists $rows->{'PROSE-01'},
      "got " . (exists $rows->{'PROSE-01'} ? "line $rows->{'PROSE-01'}" : '(none)'));

check('SELF-16', 'plain check() and table-driven initialiser IDs are both rows',
      exists $rows->{'ROW-01'} && exists $rows->{'TAB-09'},
      "got ROW-01=" . (exists $rows->{'ROW-01'} ? 'yes' : 'no')
        . " TAB-09=" . (exists $rows->{'TAB-09'} ? 'yes' : 'no'));

# What the document records, read across every table shape it uses — and
# NOT across its own generated Summary block. The fixture's Summary row is
# deliberately as wide as a real data row, so the ONLY thing that can keep
# it out of the recorded set is the marker-block skip.
my ($SUMMARY_BEGIN, $SUMMARY_END) = summary_markers();

my @doc = (
    '## Summary',
    $SUMMARY_BEGIN,
    '| Section    | Rows | pass | fail | missing |',
    '|------------|-----:|-----:|-----:|--------:|',
    '| GENSUM-01  |   12 |   12 |    0 |       0 |',
    $SUMMARY_END,
    '',
    '| Test ID | Plan row title | VHDL file:line | Status | Test file:line |',
    '|---------|----------------|----------------|--------|----------------|',
    '| MAIN-01 | five-column row with a Status cell | — | pass | x.cpp:1 |',
    '| X-01    | sub-letter parent row              | — | pass | x.cpp:2 |',
    '',
    '### Extra coverage (not in plan)',
    '',
    '| Test ID | Assertion description | VHDL file:line | Test file:line |',
    '|---------|-----------------------|----------------|----------------|',
    '| XTRA-01 | four-column row, no Status cell | — | x.cpp:3 |',
);
my $recorded = matrix_row_ids(\@doc);

check('SELF-17', 'an ID in a four-column "Extra coverage" table counts as recorded',
      matrix_records('XTRA-01', $recorded) && matrix_records('MAIN-01', $recorded),
      "XTRA-01=" . (matrix_records('XTRA-01', $recorded) ? 'yes' : 'no')
        . " MAIN-01=" . (matrix_records('MAIN-01', $recorded) ? 'yes' : 'no'));

check('SELF-18', 'a source sub-letter row X-01a is recorded by matrix row X-01',
      matrix_records('X-01a', $recorded) && !matrix_records('X-02a', $recorded),
      "X-01a=" . (matrix_records('X-01a', $recorded) ? 'yes' : 'no')
        . " X-02a=" . (matrix_records('X-02a', $recorded) ? 'yes' : 'no'));

# The generated Summary is a table too, and its first column holds section
# names. Harvesting it would let the previous run's output vouch for the
# next one — a generator validated against its own past output can never
# catch its own bad data.
check('SELF-19', 'rows inside the generated Summary block are not read back as recorded IDs',
      !matrix_records('GENSUM-01', $recorded),
      "got " . (matrix_records('GENSUM-01', $recorded) ? 'recorded' : '(not recorded)'));

# Suite-level symmetry: a whole suite this matrix does not trace.
#
# "Traced" means @SUBSYS SCANS the suite's source, not that some header
# mentions it. The first cut of this check used mention, and it had exactly
# the hole it exists to find: the Audio header named three suites while the
# code scanned one, so 51 rows asserted in the other two were published as
# `missing` and the gap satisfied the mention test at the same time.
write_fixture('test/unit-tests.conf', <<'CONF');
# name                             rows
mmu_test                            250
orphan_test                          42
mentioned_test                        7
fuse_z80_test                      1356
esp_at_test                         137
CONF

# `mmu_test` is scanned by the real @SUBSYS; `orphan_test` is nowhere;
# `mentioned_test` is named by a section header but scanned by nothing.
my $unmapped = unmapped_suites([
    '## Sectioned — `test/fixture/mentioned_test.cpp`',
]);
my %un = map { $_->[0] => $_->[1] } @$unmapped;

check('SELF-20', 'a suite @SUBSYS actually scans is not flagged unmapped',
      !exists $un{'mmu_test'},
      "flagged=[" . join(' ', map { $_->[0] } @$unmapped) . "]");

check('SELF-21', 'an untraced suite is flagged with its declared row count',
      ($un{'orphan_test'} // -1) == 42,
      "got " . ($un{'orphan_test'} // '(not flagged)'));

check('SELF-22', 'a suite a section header MENTIONS but no @SUBSYS entry scans is still flagged',
      exists $un{'mentioned_test'},
      "mentioned_test " . (exists $un{'mentioned_test'} ? 'flagged' : 'NOT flagged'));

check('SELF-23', 'a documented out-of-scope suite (FUSE data-driven runner) is exempt',
      !exists $un{'fuse_z80_test'},
      "flagged=[" . join(' ', map { $_->[0] } @$unmapped) . "]");

# A MODULE-RESIDENT suite: its @SUBSYS source is `src/esp01/test/esp_at_test.cpp`,
# outside `test/` entirely (GH #25 — the ESP-01 emulator ships its own proof
# inside the reusable component). unmapped_suites() matches the conf on the
# path's BASENAME, so a non-`test/` root must map exactly as a `test/`-rooted
# one does.
#
# This row exists because breaking that mapping would be INVISIBLE otherwise.
# The other half of the property — an @SUBSYS source path that does not exist,
# which is what a "tidy-up" making these paths `test/`-relative would produce —
# needs no fixture: source_lines() dies, and the script exits 2 leaving the
# matrix untouched (verified by mutation). But a broken BASENAME would merely
# add one more suite to the UNMAPPED list, and that signal is saturated: 50
# suites / 1358 rows are already unmapped, so the script exits 1 on a clean
# tree and one more entry changes nothing anyone would notice.
check('SELF-70', 'a suite whose @SUBSYS source lives OUTSIDE test/ still maps by basename',
      !exists $un{'esp_at_test'},
      "flagged=[" . join(' ', map { $_->[0] } @$unmapped) . "]");

# ── The generated Summary block ───────────────────────────────────────

my @sum = ('before', $SUMMARY_BEGIN, 'old line 1', 'old line 2', $SUMMARY_END, 'after');
replace_summary(\@sum, ['new']);
check('SELF-24', 'replace_summary swaps only the marked block, leaving its surroundings intact',
      "@sum" eq "before $SUMMARY_BEGIN new $SUMMARY_END after",
      "got [@sum]");

my @nomark = ('no markers here');
my $died = 0;
eval { replace_summary(\@nomark, ['new']); 1 } or $died = 1;
check('SELF-25', 'a missing generated-Summary marker is fatal, not a silent no-op',
      $died && "@nomark" eq 'no markers here',
      $died ? "left=[@nomark]" : 'replace_summary returned without dying');

# ── Section boundary (the double-count) ───────────────────────────────
#
# A companion `###` section is nested inside its parent `##` section, so
# without a stop index the parent's scan runs straight through the
# companion's rows and counts them a second time against the wrong source.

my $par_row = '| UNPROT-01 | parent row      | fixture_a.vhd:10 | missing | missing        |';
my $com_row = '| COMP-01   | companion row   | fixture_b.vhd:20 | pass    | other.cpp:7    |';
my @nested = (
    '## Fixture — `test/fixture/section_test.cpp`',
    '',
    '| Test ID   | Description     | VHDL file:line   | Status  | Test file:line |',
    '|-----------|-----------------|------------------|---------|----------------|',
    $par_row,
    '',
    '### Companion integration suite — `test/fixture/other_test.cpp`',
    '',
    $com_row,
);
my (@nd, @nk);
my ($n_touched) = refresh_section(\@nested, 0, 'bin/section_suite',
                                  'test/fixture/section_test.cpp', \@nd, \@nk, 6);

check('SELF-26', 'the parent section stops at a nested companion header and leaves its rows alone',
      $nested[8] eq $com_row && $n_touched == 1,
      "touched=$n_touched row=[$nested[8]]");

# ── Multi-suite sections ──────────────────────────────────────────────
#
# One section, several backing suites (the Audio shape: three suites whose
# rows are interleaved in a single table, no `### Companion` sub-tables).
# Both halves have to merge — the source scan AND the FAIL set — or rows
# implemented in the second suite are published as untested.

write_fixture('test/fixture/multi_a_test.cpp', <<'CPP');
void a() {
    check("MA-01", "row in the first suite — VHDL fixture_a.vhd:1", cond, detail);
}
CPP
write_fixture('test/fixture/multi_b_test.cpp', <<'CPP');
void b() {
    check("MB-01", "row in the second suite — VHDL fixture_b.vhd:2", cond, detail);
    check("MB-02", "failing row in the second suite", cond, detail);
}
CPP
for my $s (['multi_a', ''], ['multi_b', "echo '  FAIL MB-02: broke'\n"]) {
    my $path = "$FIXTURE_ROOT/bin/$s->[0]";
    open(my $h, '>', $path) or die "write $path: $!";
    print $h "#!/bin/sh\n", $s->[1];
    close $h;
    chmod 0755, $path;
}

my @multi = (
    '## Multi — `test/fixture/multi_a_test.cpp` + `test/fixture/multi_b_test.cpp`',
    '',
    '| Test ID | Description | VHDL file:line | Status  | Test file:line                    |',
    '|---------|-------------|----------------|---------|-----------------------------------|',
    '| MA-01   | first suite | —              | missing | missing                           |',
    '| MB-01   | second suite| —              | missing | missing                           |',
    '| MB-02   | second suite| —              | missing | missing                           |',
);
my (@md, @mk);
refresh_section(\@multi, 0,
                ['bin/multi_a', 'bin/multi_b'],
                ['test/fixture/multi_a_test.cpp', 'test/fixture/multi_b_test.cpp'],
                \@md, \@mk);

check('SELF-27', 'a row asserted only in the second suite resolves, and its Test file:line names THAT file',
      scalar($multi[5] =~ /\|\s*pass\s*\|/
             && $multi[5] =~ m{test/fixture/multi_b_test\.cpp:\d+}),
      "got [$multi[5]]");

check('SELF-28', 'the FAIL set is the union across every backing binary',
      scalar($multi[6] =~ /\|\s*fail\s*\|/ && $multi[4] =~ /\|\s*pass\s*\|/),
      "MB-02=[$multi[6]] MA-01=[$multi[4]]");

# ── The exit-status contract ──────────────────────────────────────────
#
# main() is stripped when this file loads the script, so the glue that
# turns the two gaps into a process exit status would otherwise be the one
# part of the contract nothing pins.
check('SELF-29', 'either gap non-empty exits 1',
      report_exit_code(1, []) == 1
        && report_exit_code(0, [['orphan_test', 42]]) == 1
        && report_exit_code(3, [['orphan_test', 42]]) == 1,
      sprintf("got %d/%d/%d", report_exit_code(1, []),
              report_exit_code(0, [['orphan_test', 42]]),
              report_exit_code(3, [['orphan_test', 42]])));

check('SELF-30', 'both gaps empty exits 0',
      report_exit_code(0, []) == 0,
      "got " . report_exit_code(0, []));

# ── Commented-out assertions (GH #119) ────────────────────────────────
#
# A `check()` inside a `//` comment does not run, so the row it names has
# no coverage. grep_row_ids() has known that since GH #117; grep_source()
# did not, so the two halves of this one tool disagreed about the same
# file — matrix row `7.3` (`test/dma/dma_test.cpp:785`, deliberately
# disabled pending re-enabled VHDL) was published `pass` for an assertion
# that does not exist, while the omission scanner correctly said nothing.
# Both now read through source_lines(), so they cannot drift apart again.

my $src5 = write_fixture('test/fixture/commented_test.cpp', <<'CPP');
void live() {
    //  {
    //      check("CMT-01", "disabled — VHDL fixture_a.vhd:900",
    //            cond, detail);
    //  }

    // CMT-02 is disabled HERE but asserted for real further down:
    //      check("CMT-02", "an older, disabled form", cond, detail);
    check("CMT-03", "a live row — VHDL fixture_b.vhd:901", cond, detail);
    check("CMT-02", "the live form — VHDL fixture_c.vhd:902", cond, detail);
}
CPP

my ($c5, $k5) = grep_source($src5);
my $r5 = grep_row_ids($src5);

check('SELF-31', 'a commented-out check() is a row to NEITHER scanner',
      scalar(!exists $c5->{'CMT-01'} && !exists $k5->{'CMT-01'}
             && !exists $r5->{'CMT-01'}),
      'grep_source=' . (exists $c5->{'CMT-01'} ? "line $c5->{'CMT-01'}" : '(none)')
        . ' grep_row_ids=' . (exists $r5->{'CMT-01'} ? "line $r5->{'CMT-01'}" : '(none)'));

check('SELF-32', 'an ID disabled in a comment but asserted live resolves to the LIVE line',
      scalar(exists $c5->{'CMT-02'} && $c5->{'CMT-02'} == 10),
      'got ' . ($c5->{'CMT-02'} // '(none)') . ', want 10');

# End-to-end: the status a disabled row is published with.
my @cmt = (
    '## Commented — `test/fixture/commented_test.cpp`',
    '',
    '| Test ID | Description | VHDL file:line | Status  | Test file:line                       |',
    '|---------|-------------|----------------|---------|--------------------------------------|',
    '| CMT-01  | disabled    | —              | pass    | test/fixture/commented_test.cpp:3    |',
    '| CMT-03  | live        | —              | missing | missing                              |',
);
my (@cd, @ck);
refresh_section(\@cmt, 0, 'bin/section_suite',
                'test/fixture/commented_test.cpp', \@cd, \@ck);

check('SELF-33', 'a row whose only check() is commented out regenerates as missing, not pass',
      scalar($cmt[4] =~ /\|\s*missing\s*\|\s*missing\s*\|/
             && $cmt[5] =~ /\|\s*pass\s*\|/),
      "CMT-01=[$cmt[4]] CMT-03=[$cmt[5]]");

# ── The sub-letter blind spot, made visible (GH #118) ─────────────────
#
# The aliasing stays (resolve_ids() maps the same pair the other way, and
# dropping only this half would let row `X-01` read `pass` *because*
# `X-01a` proves it while `X-01a` was reported recorded nowhere). What
# changed is that the set it hides is printed every run, so a distinct
# assertion wearing a sub-letter — `FB-04b`, `NA-01c`, `REG-03c` — is seen
# rather than inferred away. These pin the two ways that report can lie:
# by staying silent about an aliased ID, or by accusing one that is not.

check('SELF-34', 'an ID recorded ONLY via sub-letter aliasing is flagged; an exactly-recorded one is not',
      scalar(recorded_only_by_alias('X-01a', $recorded)
             && !recorded_only_by_alias('X-01', $recorded)
             && !recorded_only_by_alias('MAIN-01', $recorded)),
      'X-01a=' . recorded_only_by_alias('X-01a', $recorded)
        . ' X-01=' . recorded_only_by_alias('X-01', $recorded)
        . ' MAIN-01=' . recorded_only_by_alias('MAIN-01', $recorded));

check('SELF-35', 'an ID recorded nowhere is unrecorded, NOT aliased — the two reports never overlap',
      scalar(!recorded_only_by_alias('X-02a', $recorded)
             && !matrix_records('X-02a', $recorded)),
      'aliased=' . recorded_only_by_alias('X-02a', $recorded)
        . ' recorded=' . matrix_records('X-02a', $recorded));

# ── Section-scoped recording (GH #118) ────────────────────────────────
#
# "Is this row recorded?" used to be asked of the whole document, so an ID
# string reused by another subsystem answered yes on the wrong subsystem's
# behalf: `SD-16..SD-23` are asserted in `sdcard_test.cpp` and recorded
# nowhere in the SD Card section — the Audio section's identically-named
# rows were vouching for them. 29 rows across five subsystems were hidden.
#
# The unit is the SUBSYSTEM, not the @SUBSYS entry: a `###` companion is
# judged against its parent `##`, because several companions' rows are
# recorded in the parent's own table. Scoping to the entry would accuse the
# document of omitting 12 rows it plainly lists, and a false accusation
# costs as much as a silent omission.

my @scoped = (
    '## Alpha — `test/fixture/alpha_test.cpp`',
    '',
    $SUMMARY_BEGIN,
    '| Section    | Rows | pass | fail | missing |',
    '|------------|-----:|-----:|-----:|--------:|',
    '| GENSUM2-01 |   12 |   12 |    0 |       0 |',
    $SUMMARY_END,
    '',
    '| Test ID   | Plan row title      | VHDL file:line | Status | Test file:line |',
    '|-----------|---------------------|----------------|--------|----------------|',
    '| SHARED-01 | recorded in Alpha   | —              | pass   | a.cpp:1        |',
    '| A-01      | alpha row           | —              | pass   | a.cpp:2        |',
    '',
    '### Companion integration suite — `test/fixture/alpha_companion_test.cpp`',
    '',
    '| Test ID   | Plan row title      | VHDL file:line | Status | Test file:line |',
    '|-----------|---------------------|----------------|--------|----------------|',
    '| COMP-01   | companion row       | —              | pass   | c.cpp:1        |',
    '',
    '## Beta — `test/fixture/beta_test.cpp`',
    '',
    '| Test ID   | Plan row title      | VHDL file:line | Status | Test file:line |',
    '|-----------|---------------------|----------------|--------|----------------|',
    '| B-01      | beta row            | —              | pass   | b.cpp:1        |',
    '| SHARED-01 | same ID, other subsystem | —         | pass   | b.cpp:2        |',
);
my ($a_from, $a_to) = subsystem_span(\@scoped, 0);    # `## Alpha`
my ($c_from, $c_to) = subsystem_span(\@scoped, 14);   # `### Companion ...`
my ($b_from, $b_to) = subsystem_span(\@scoped, 20);   # `## Beta`
my $alpha = matrix_row_ids(\@scoped, $a_from, $a_to);
my $comp  = matrix_row_ids(\@scoped, $c_from, $c_to);
my $beta  = matrix_row_ids(\@scoped, $b_from, $b_to);

check('SELF-36', 'recording is section-scoped: a row recorded only in another subsystem does not count',
      scalar(!matrix_records('B-01', $alpha) && matrix_records('B-01', $beta)
             && !matrix_records('A-01', $beta)),
      "B-01-in-alpha=" . matrix_records('B-01', $alpha)
        . " B-01-in-beta=" . matrix_records('B-01', $beta)
        . " A-01-in-beta=" . matrix_records('A-01', $beta));

check('SELF-37', 'a ### companion is judged against its parent ##, so a parent-table row records it',
      scalar($c_from == $a_from && $c_to == $a_to
             && matrix_records('A-01', $comp) && matrix_records('COMP-01', $alpha)),
      "alpha=[$a_from,$a_to) companion=[$c_from,$c_to) "
        . "A-01-in-comp=" . matrix_records('A-01', $comp)
        . " COMP-01-in-alpha=" . matrix_records('COMP-01', $alpha));

check('SELF-38', 'a ranged read still skips a generated Summary block that falls inside the range',
      scalar(!matrix_records('GENSUM2-01', $alpha)
             && matrix_records('SHARED-01', $alpha)),
      "GENSUM2-01=" . matrix_records('GENSUM2-01', $alpha)
        . " SHARED-01=" . matrix_records('SHARED-01', $alpha));

# ── Companion sources on the STATUS side (GH #121) ────────────────────
#
# GH #117 let one @SUBSYS entry take several sources so RECORDING could see
# a companion suite; the STATUS computation was not extended the same way, so
# a row physically listed in a parent `##` table but asserted in its nested
# `###` companion published `missing` while the assertion ran and passed
# (PFF-G108-01/02/03, ULA-INT-04/06, NR-C2-01/NR-C3-01, INT-07 — 80 rows).
# The two halves of one tool disagreeing again, in a narrower place.
#
# These rows pin BOTH directions of the contract. Widening the search is the
# easy half and a careless fix would make everything read `pass`; the rows
# that matter are the refusals — a row asserted nowhere still reads `missing`,
# a companion's FAIL never lands on a row the primary source owns, and a
# companion's citation never answers for a row the primary source owns.

write_fixture('test/fixture/cparent_test.cpp', <<'CPP');
void parent() {
    check("CP-OWN-01",   "parent-owned — VHDL fixture_a.vhd:500", cond, detail);
    check("CP-BOTH-01",  "parent copy of a shared ID", cond, detail);
    check("CP-PFAIL-01", "parent copy; passes in THIS suite", cond, detail);
    check("PRIM-BARE-01","parent assertion carrying no citation", cond, detail);
    check("CC-PARENT-01","a companion-table row asserted in the PARENT source",
          cond, detail);
}
CPP
write_fixture('test/fixture/ccomp_test.cpp', <<'CPP');
void companion() {
    check("CP-COMP-01",  "companion-owned — VHDL fixture_b.vhd:600", cond, detail);
    check("CP-BOTH-01",  "companion copy of a shared ID", cond, detail);
    check("CP-CFAIL-01", "companion-owned and failing here", cond, detail);
    check("CP-PFAIL-01", "companion copy; FAILS in THIS suite", cond, detail);
    check("PRIM-BARE-01","companion copy — VHDL fixture_d.vhd:800", cond, detail);
    check("COMP-CITE-01","companion-owned — VHDL fixture_c.vhd:700", cond, detail);
}
CPP
for my $s (['cparent', ''],
           ['ccomp', "echo '  FAIL CP-CFAIL-01: broke'\necho '  FAIL CP-PFAIL-01: broke'\n"]) {
    my $path = "$FIXTURE_ROOT/bin/$s->[0]";
    open(my $h, '>', $path) or die "write $path: $!";
    print $h "#!/bin/sh\n", $s->[1];
    close $h;
    chmod 0755, $path;
}

# One document, a `##` parent whose table lists rows asserted on both sides,
# and its nested `###` companion whose table lists a row asserted in the
# parent. Column widths are wide enough that no row triggers the
# "exceeds column width" path — that is a separate contract (SELF-10).
my @cdoc = (
    '## CompParent — `test/fixture/cparent_test.cpp`',                                    # 0
    '',
    '| Test ID      | Description              | VHDL file:line | Status  | Test file:line                       |',
    '|--------------|--------------------------|----------------|---------|--------------------------------------|',
    '| CP-OWN-01    | asserted in the parent   | —              | missing | missing                              |',
    '| CP-COMP-01   | asserted in the companion| —              | missing | missing                              |',
    '| CP-NONE-01   | asserted nowhere         | —              | missing | missing                              |',
    '| CP-BOTH-01   | asserted in both         | —              | missing | missing                              |',
    '| CP-CFAIL-01  | companion-owned, fails   | —              | missing | missing                              |',
    '| CP-PFAIL-01  | parent-owned; comp fails | —              | missing | missing                              |',
    '| PRIM-BARE-01 | parent-owned, uncited    | —              | missing | missing                              |',
    '| COMP-CITE-01 | companion-owned, cited   | —              | missing | missing                              |',
    '',
    '### Companion integration suite — `test/fixture/ccomp_test.cpp`',                     # 13
    '',
    '| Test ID      | Description              | VHDL file:line | Status  | Test file:line                       |',
    '|--------------|--------------------------|----------------|---------|--------------------------------------|',
    '| CC-PARENT-01 | asserted in the parent   | —              | missing | missing                              |',
);
my $CDOC_COMPANION_IDX = 13;
my %cdoc_row = map { $cdoc[$_] =~ /^\|\s*([A-Za-z0-9._\-]+)\s*\|/ ? ($1 => $_) : () }
               0 .. $#cdoc;

my (@pd, @pk);
refresh_section(\@cdoc, 0, 'bin/cparent', 'test/fixture/cparent_test.cpp',
                \@pd, \@pk, $CDOC_COMPANION_IDX,
                [['bin/ccomp', 'test/fixture/ccomp_test.cpp']]);
my (@qd, @qk);
refresh_section(\@cdoc, $CDOC_COMPANION_IDX, 'bin/ccomp',
                'test/fixture/ccomp_test.cpp', \@qd, \@qk, undef,
                [['bin/cparent', 'test/fixture/cparent_test.cpp']]);

sub cdoc_row { return $cdoc[ $cdoc_row{ $_[0] } ]; }

my @cmapdoc = ('## Parent — `p`', '',
               '### Companion integration suite — `c`', '',
               '## Other — `o`');
my $cmap = companion_map(\@cmapdoc, [
    [0, ['## Parent — `p`',                        'bin/p', 'test/p.cpp']],
    [2, ['### Companion integration suite — `c`',  'bin/c', 'test/c.cpp']],
    [4, ['## Other — `o`',                         'bin/o', 'test/o.cpp']],
]);
sub cmap_srcs { return join(',', map { $_->[1] } @{ $cmap->{ $_[0] } }); }

check('SELF-39', 'companion_map pairs a ## parent with its ### companion, and nothing across a ## boundary',
      scalar(cmap_srcs(0) eq 'test/c.cpp'
             && cmap_srcs(2) eq 'test/p.cpp'
             && cmap_srcs(4) eq ''),
      sprintf('parent=[%s] companion=[%s] other=[%s]',
              cmap_srcs(0), cmap_srcs(2), cmap_srcs(4)));

check('SELF-40', 'a parent-table row asserted only in the companion resolves, naming the COMPANION file',
      scalar(cdoc_row('CP-COMP-01') =~ /\|\s*pass\s*\|/
             && cdoc_row('CP-COMP-01') =~ m{test/fixture/ccomp_test\.cpp:\d+}),
      'got [' . cdoc_row('CP-COMP-01') . ']');

# The refusal that makes the rest mean something: widening to the subsystem
# must not turn "asserted nowhere" into `pass`.
check('SELF-41', 'a row asserted in NEITHER source still reads missing',
      scalar(cdoc_row('CP-NONE-01') =~ /\|\s*missing\s*\|\s*missing\s*\|/),
      'got [' . cdoc_row('CP-NONE-01') . ']');

check('SELF-42', 'the primary source wins: an ID asserted in both names the PARENT file',
      scalar(cdoc_row('CP-BOTH-01') =~ /\|\s*pass\s*\|/
             && cdoc_row('CP-BOTH-01') =~ m{test/fixture/cparent_test\.cpp:\d+}),
      'got [' . cdoc_row('CP-BOTH-01') . ']');

# Resolving a row into a companion source without also reading that binary's
# FAIL lines would publish `pass` for an assertion that fails — the exact
# whitewash run_fails() refuses to allow for a missing binary.
check('SELF-43', 'the companion binary FAIL set is honoured for the rows the companion owns',
      scalar(cdoc_row('CP-CFAIL-01') =~ /\|\s*fail\s*\|/
             && cdoc_row('CP-CFAIL-01') =~ m{test/fixture/ccomp_test\.cpp:\d+}),
      'got [' . cdoc_row('CP-CFAIL-01') . ']');

# And the opposite error: merging the companion's FAIL set wholesale would
# publish a false `fail` on a row the primary source asserts and passes.
check('SELF-44', 'a companion FAIL for an ID the PRIMARY source owns does NOT leak into this section',
      scalar(cdoc_row('CP-PFAIL-01') =~ /\|\s*pass\s*\|/
             && cdoc_row('CP-PFAIL-01') =~ m{test/fixture/cparent_test\.cpp:\d+}),
      'got [' . cdoc_row('CP-PFAIL-01') . ']');

check('SELF-45', 'the relation is symmetric: a companion-table row asserted in the parent resolves',
      scalar(cdoc_row('CC-PARENT-01') =~ /\|\s*pass\s*\|/
             && cdoc_row('CC-PARENT-01') =~ m{test/fixture/cparent_test\.cpp:\d+}),
      'got [' . cdoc_row('CC-PARENT-01') . ']');

# Citations follow ownership for the same reason the `next` tier is fenced:
# a companion's row-local evidence must not answer for a row the primary
# source owns, or a plausible-but-wrong citation gets published.
check('SELF-46', 'a companion supplies a citation only for the rows it owns',
      scalar(cdoc_row('COMP-CITE-01') =~ /\|\s*fixture_c\.vhd:700\s*\|/
             && cdoc_row('PRIM-BARE-01') =~ /\|\s*—\s*\|/
             && cdoc_row('PRIM-BARE-01') !~ /fixture_d\.vhd/),
      'COMP-CITE-01=[' . cdoc_row('COMP-CITE-01') . '] '
        . 'PRIM-BARE-01=[' . cdoc_row('PRIM-BARE-01') . ']');

# ── Underscore-bearing row IDs (GH #125) ──────────────────────────────
#
# `NR_A0-01/02/03` are asserted in `test/uart/uart_integration_test.cpp` and
# listed in two matrix tables, and every one of them published `missing`: the
# ID prefix class was `[A-Z][A-Z0-9]*`, which stops dead at the underscore.
# All three ID scanners read that one pattern, so the rows were invisible on
# the status side AND to the `unrecorded` report that exists to catch a row
# with no matrix entry — the gap hid itself.
#
# Widening the pattern is the easy half and the dangerous one: an ID regex
# loose enough to match C++ identifiers would attach row status to enum
# members and log strings, silently, exactly as a too-eager citation tier
# attaches a neighbour's VHDL lines. So SELF-48 is the row that matters —
# the dash still separates an ID from an identifier, and `_` buys nothing
# beyond the uppercase prefix.

my $src6 = write_fixture('test/fixture/underscore_test.cpp', <<'CPP');
void rows() {
    check("NR_A0-01", "underscore row — VHDL fixture_a.vhd:1241", cond, detail);
    skip("NR_B1-02", "underscore skip row");

    emu.set_machine("ZXN_ISSUE2");
    emu.log("pi_uart_en-flag");
    emu.log("_A0-01");
}
CPP

my ($c6, $k6) = grep_source($src6);
my $r6 = grep_row_ids($src6);
my $cites6 = grep_citations($src6);

check('SELF-47', 'an underscore-bearing ID is a row to BOTH scanners, as a check and as a skip',
      scalar(exists $c6->{'NR_A0-01'} && exists $k6->{'NR_B1-02'}
             && exists $r6->{'NR_A0-01'} && exists $r6->{'NR_B1-02'}),
      'grep_source check=' . (exists $c6->{'NR_A0-01'} ? "line $c6->{'NR_A0-01'}" : '(none)')
        . ' skip=' . (exists $k6->{'NR_B1-02'} ? "line $k6->{'NR_B1-02'}" : '(none)')
        . '; grep_row_ids=' . join(',', sort keys %$r6));

check('SELF-48', 'the widening stops at the dash: an enum member, a lowercase-led identifier and a leading underscore are not rows',
      scalar(!exists $r6->{'ZXN_ISSUE2'} && !exists $c6->{'ZXN_ISSUE2'}
             && !exists $r6->{'pi_uart_en-flag'} && !exists $c6->{'pi_uart_en-flag'}
             && !exists $r6->{'_A0-01'} && !exists $c6->{'_A0-01'}),
      'rows seen: ' . join(',', sort keys %$r6));

check('SELF-49', 'an underscore-bearing row takes its own call-tier citation',
      ($cites6->{'NR_A0-01'} // '') eq 'fixture_a.vhd:1241',
      'got ' . ($cites6->{'NR_A0-01'} // '(none)'));

# ── Which source a drift line is charged to (GH #126) ─────────────────
#
# A drift line was prefixed with the section's FIRST source, whoever actually
# supplied the citation. Rows resolved through a companion suite were charged
# to the primary file — `test/uart/uart_test.cpp INT-07`, `test/input/
# input_test.cpp JOY-WIRE-01/02` — which never mentions them, so the reader is
# sent to a file where neither the ID nor the citation appears. It misdirects;
# it does not corrupt a matrix cell.
#
# The label must name the file the reported `source=[...]` was literally read
# from, and that is NOT always a test source: the plan-doc tier is a real
# citation source, and it is the one the two live examples actually came from
# (`uart_test.cpp` does not mention INT-07 at all — the citation is the
# UART-I2C plan doc's). Charging those to the companion because the ASSERTION
# lives there would substitute one plausible-but-wrong pointer for another:
# `JOY-WIRE-01`'s companion check cites `zxnext.vhd:5157-5158` while the drift
# line reports the plan's `membrane_stick.vhd:124-131`.
#
# The fixture borrows the real `test/uart/*` paths on purpose: %PLAN_DOC maps
# them, so the plan tier is exercised through the production mapping rather
# than an injected copy that could drift from it. The files are fixtures in a
# temp tree; nothing real is read.

write_fixture('doc/testing/UART-I2C-TEST-PLAN-DESIGN.md', <<'MD');
| Test ID    | Scenario            | Expected                          |
|------------|---------------------|-----------------------------------|
| DR-PLAN-01 | cited only by the plan | see fixture_c.vhd:33 for the rule |
MD

write_fixture('test/uart/uart_test.cpp', <<'CPP');
void primary() {
    check("DR-PRIM-01", "primary-owned and cited here — VHDL fixture_a.vhd:11",
          cond, detail);
    check("DR-PLAN-01", "primary-owned, carrying no citation of its own",
          cond, detail);
}
CPP
write_fixture('test/uart/uart_integration_test.cpp', <<'CPP');
void companion() {
    check("DR-COMP-01", "companion-owned and cited here — VHDL fixture_b.vhd:22",
          cond, detail);
}
CPP
for my $s (qw(dprimary dcompanion)) {
    my $path = "$FIXTURE_ROOT/bin/$s";
    open(my $h, '>', $path) or die "write $path: $!";
    print $h "#!/bin/sh\n";
    close $h;
    chmod 0755, $path;
}

# Every row carries a hand-written citation that disagrees with the extracted
# one, so every row drifts and each drift line's prefix is observable.
my @ddoc = (
    '## UART — `test/uart/uart_test.cpp`',
    '',
    '| Test ID    | Description               | VHDL file:line   | Status  | Test file:line                            |',
    '|------------|--------------------------|------------------|---------|-------------------------------------------|',
    '| DR-PRIM-01 | cited in the primary     | fixture_d.vhd:99 | missing | missing                                   |',
    '| DR-PLAN-01 | cited only by the plan   | fixture_d.vhd:99 | missing | missing                                   |',
    '| DR-COMP-01 | cited in the companion   | fixture_d.vhd:99 | missing | missing                                   |',
);
my (@dd, @dk);
refresh_section(\@ddoc, 0, 'bin/dprimary', 'test/uart/uart_test.cpp',
                \@dd, \@dk, undef,
                [['bin/dcompanion', 'test/uart/uart_integration_test.cpp']]);
my %drift_src = map { /^(\S+)\s+(\S+):/ ? ($2 => $1) : () } @dd;

check('SELF-50', 'a drift line for a row resolved through the companion names the COMPANION source',
      ($drift_src{'DR-COMP-01'} // '') eq 'test/uart/uart_integration_test.cpp',
      'got ' . ($drift_src{'DR-COMP-01'} // '(no drift line)') . "; drift=[@dd]");

# The refusal: charging everything to the companion would pass SELF-50 alone.
check('SELF-51', 'a drift line for a row the primary source cites still names the PRIMARY',
      ($drift_src{'DR-PRIM-01'} // '') eq 'test/uart/uart_test.cpp',
      'got ' . ($drift_src{'DR-PRIM-01'} // '(no drift line)') . "; drift=[@dd]");

# The tier the two live examples actually came from: no test source carries
# this citation, so naming one would be a fresh wrong pointer.
check('SELF-52', 'a citation supplied by the plan doc names the PLAN DOC, not a test source',
      ($drift_src{'DR-PLAN-01'} // '') eq 'doc/testing/UART-I2C-TEST-PLAN-DESIGN.md',
      'got ' . ($drift_src{'DR-PLAN-01'} // '(no drift line)') . "; drift=[@dd]");

# ── Row-local evidence outranks the plan doc ACROSS sources (GH #133) ─
#
# grep_citations() orders its tiers row-local-before-plan inside one file,
# and the merge across a section's sources used to throw that order away:
# the first source to answer locked the row in. A primary source answers for
# EVERY plan row, asserted there or not (the trailing plan loop), so its
# plan-doc answer beat the row-local citation of the companion suite that
# actually asserts the row. Live instance: `NR_A0-01` published
# `zxnext.vhd:1241`, a bare signal declaration, over the `zxnext.vhd:5080`
# reset default its assertions read — and reported no drift while doing it,
# because the hand-written cell agreed with the plan. Ten rows across four
# subsystems were affected, and four of them were published TWICE with
# different citations, the parent table taking the plan's and the companion's
# own table taking the row-local one.
#
# The upgrade is the easy half. The rows that matter are the four refusals:
# a plan-only row must KEEP its plan citation (inventing a row-local one is
# the borrowed-citation defect SELF-04 exists for), a row-local incumbent
# must stay put, and a citation may only come from the source this run names
# in `Test file:line` — so what is published always justifies what runs.
#
# Both fixtures borrow real `test/*` paths that %PLAN_DOC maps, so the plan
# tier is exercised through the production mapping rather than an injected
# copy that could drift from it. The files are written into the temp tree.

# ── Companion source vs the primary's plan-doc answer ────────────────
write_fixture('doc/testing/NEXTREG-TEST-PLAN-DESIGN.md', <<'MD');
| Test ID    | Scenario                  | Expected                        |
|------------|---------------------------|---------------------------------|
| PC-COMP-01 | asserted in the companion | plan says fixture_a.vhd:1       |
| PC-COMP-02 | asserted in the companion | plan says fixture_a.vhd:7       |
| PC-PLAN-01 | asserted in the primary   | plan says fixture_a.vhd:2       |
| PC-BARE-01 | asserted in the companion | plan says fixture_a.vhd:4       |
MD

# `PC-NEXT-01` is the primary's, and in the companion it sits immediately
# after the uncited `PC-BARE-01` — the borrowable neighbour that makes
# SELF-56 discriminative. The new precedence WIDENS what a broken `next`
# tier could do: the old merge would have masked the borrowed citation
# behind the plan-doc answer, this one would publish it.
write_fixture('test/nextreg/nextreg_test.cpp', <<'CPP');
void primary() {
    check("PC-PLAN-01", "primary-owned, carrying no citation of its own",
          cond, detail);
    check("PC-NEXT-01", "primary-owned and cited here — VHDL fixture_b.vhd:30",
          cond, detail);
}
CPP
write_fixture('test/nextreg/nextreg_integration_test.cpp', <<'CPP');
void companion() {
    check("PC-COMP-01", "companion-owned and cited here — VHDL fixture_c.vhd:40",
          cond, detail);
    check("PC-COMP-02", "companion-owned and cited here — VHDL fixture_c.vhd:41",
          cond, detail);
    check("PC-BARE-01", "companion-owned, carrying no citation of its own",
          cond, detail);
    check("PC-NEXT-01", "companion copy, cited differently — VHDL fixture_d.vhd:50",
          cond, detail);
}
CPP
for my $s (qw(pcprimary pccompanion)) {
    my $path = "$FIXTURE_ROOT/bin/$s";
    open(my $h, '>', $path) or die "write $path: $!";
    print $h "#!/bin/sh\n";
    close $h;
    chmod 0755, $path;
}

# PC-COMP-01's cell is empty, so the citation this run PUBLISHES is readable
# straight out of it. Every other cell carries a citation that disagrees with
# all of them, so those rows drift and both halves of each drift line — the
# citation and the file it is charged to — are observable.
my @pcdoc = (
    '## NextREG — `test/nextreg/nextreg_test.cpp`',
    '',
    '| Test ID    | Description              | VHDL file:line   | Status  | Test file:line                                  |',
    '|------------|--------------------------|------------------|---------|-------------------------------------------------|',
    '| PC-COMP-01 | companion-owned, cited   | —                | missing | missing                                         |',
    '| PC-COMP-02 | companion-owned, cited   | fixture_a.vhd:99 | missing | missing                                         |',
    '| PC-PLAN-01 | primary-owned, uncited   | fixture_a.vhd:99 | missing | missing                                         |',
    '| PC-BARE-01 | companion-owned, uncited | fixture_a.vhd:99 | missing | missing                                         |',
);
my (@pcd, @pck);
refresh_section(\@pcdoc, 0, 'bin/pcprimary', 'test/nextreg/nextreg_test.cpp',
                \@pcd, \@pck, undef,
                [['bin/pccompanion', 'test/nextreg/nextreg_integration_test.cpp']]);
my %pc_row = map { $pcdoc[$_] =~ /^\|\s*([A-Za-z0-9._\-]+)\s*\|/ ? ($1 => $pcdoc[$_]) : () }
             0 .. $#pcdoc;
my %pc_drift = map { /^(\S+)\s+(\S+): doc=\[([^\]]*)\] source=\[([^\]]*)\]/
                     ? ($2 => [$1, $4]) : () } @pcd;
sub pc_drift_src  { return ($pc_drift{ $_[0] } || ['(no drift line)'])->[0]; }
sub pc_drift_cite { return ($pc_drift{ $_[0] } || ['', '(no drift line)'])->[1]; }

check('SELF-53', 'an empty cell for a companion-asserted row is filled with the COMPANION check\'s citation, not the plan doc\'s',
      scalar(($pc_row{'PC-COMP-01'} // '') =~ /\|\s*fixture_c\.vhd:40\s*\|/
             && ($pc_row{'PC-COMP-01'} // '') !~ /fixture_a\.vhd:1\b/),
      'got [' . ($pc_row{'PC-COMP-01'} // '(row missing)') . ']');

check('SELF-54', 'the upgraded citation is charged to the companion source it was read from, not to the plan doc',
      scalar(pc_drift_cite('PC-COMP-02') eq 'fixture_c.vhd:41'
             && pc_drift_src('PC-COMP-02') eq 'test/nextreg/nextreg_integration_test.cpp'),
      'got src=' . pc_drift_src('PC-COMP-02')
        . ' cite=' . pc_drift_cite('PC-COMP-02') . "; drift=[@pcd]");

# The refusals.
check('SELF-55', 'a row whose only citation is the plan doc\'s keeps it, still charged to the plan doc',
      scalar(pc_drift_cite('PC-PLAN-01') eq 'fixture_a.vhd:2'
             && pc_drift_src('PC-PLAN-01') eq 'doc/testing/NEXTREG-TEST-PLAN-DESIGN.md'),
      'got src=' . pc_drift_src('PC-PLAN-01')
        . ' cite=' . pc_drift_cite('PC-PLAN-01') . "; drift=[@pcd]");

check('SELF-56', 'a companion-asserted row whose check carries no citation keeps the plan doc\'s rather than borrowing the next check\'s',
      scalar(pc_drift_cite('PC-BARE-01') eq 'fixture_a.vhd:4'
             && pc_drift_src('PC-BARE-01') eq 'doc/testing/NEXTREG-TEST-PLAN-DESIGN.md'),
      'got src=' . pc_drift_src('PC-BARE-01')
        . ' cite=' . pc_drift_cite('PC-BARE-01') . "; drift=[@pcd]");

# ── The same inversion inside one multi-suite section (the Audio shape) ─
#
# No companion sub-table here: three suites back one `##` section and their
# rows interleave in a single table, so the merge that inverts the tiers is
# the primary loop's own. `NR-43` is the live instance.
write_fixture('doc/testing/AUDIO-TEST-PLAN-DESIGN.md', <<'MD');
| Test ID      | Scenario                    | Expected                  |
|--------------|-----------------------------|---------------------------|
| PM-SECOND-01 | asserted in the second suite| plan says fixture_a.vhd:5 |
| PM-SECOND-02 | asserted in the second suite| plan says fixture_a.vhd:8 |
| PM-FIRST-01  | asserted in the first suite | plan says fixture_a.vhd:6 |
| PM-BOTH-01   | asserted in both suites     | plan says fixture_a.vhd:9 |
MD

write_fixture('test/audio/audio_test.cpp', <<'CPP');
void first() {
    check("PM-FIRST-01", "first-suite-owned, carrying no citation of its own",
          cond, detail);
    check("PM-BOTH-01", "first-suite copy, cited — VHDL fixture_d.vhd:80",
          cond, detail);
}
CPP
write_fixture('test/audio/audio_nextreg_test.cpp', <<'CPP');
void second() {
    check("PM-SECOND-01", "second-suite-owned, cited — VHDL fixture_b.vhd:60",
          cond, detail);
    check("PM-SECOND-02", "second-suite-owned, cited — VHDL fixture_b.vhd:61",
          cond, detail);
    check("PM-FIRST-01", "second-suite copy, cited — VHDL fixture_c.vhd:70",
          cond, detail);
    check("PM-BOTH-01", "second-suite copy, cited — VHDL fixture_b.vhd:81",
          cond, detail);
}
CPP
for my $s (qw(pmfirst pmsecond)) {
    my $path = "$FIXTURE_ROOT/bin/$s";
    open(my $h, '>', $path) or die "write $path: $!";
    print $h "#!/bin/sh\n";
    close $h;
    chmod 0755, $path;
}

my @pmdoc = (
    '## Audio — `test/audio/audio_test.cpp`',
    '',
    '| Test ID      | Description               | VHDL file:line   | Status  | Test file:line                            |',
    '|--------------|---------------------------|------------------|---------|-------------------------------------------|',
    '| PM-SECOND-01 | second-suite-owned, cited | —                | missing | missing                                   |',
    '| PM-SECOND-02 | second-suite-owned, cited | fixture_a.vhd:99 | missing | missing                                   |',
    '| PM-FIRST-01  | first-suite-owned, uncited| fixture_a.vhd:99 | missing | missing                                   |',
    '| PM-BOTH-01   | cited in both suites      | fixture_a.vhd:99 | missing | missing                                   |',
);
my (@pmd, @pmk);
refresh_section(\@pmdoc, 0,
                ['bin/pmfirst', 'bin/pmsecond'],
                ['test/audio/audio_test.cpp', 'test/audio/audio_nextreg_test.cpp'],
                \@pmd, \@pmk);
my %pm_row = map { $pmdoc[$_] =~ /^\|\s*([A-Za-z0-9._\-]+)\s*\|/ ? ($1 => $pmdoc[$_]) : () }
             0 .. $#pmdoc;
my %pm_drift = map { /^(\S+)\s+(\S+): doc=\[([^\]]*)\] source=\[([^\]]*)\]/
                     ? ($2 => [$1, $4]) : () } @pmd;
sub pm_drift_src  { return ($pm_drift{ $_[0] } || ['(no drift line)'])->[0]; }
sub pm_drift_cite { return ($pm_drift{ $_[0] } || ['', '(no drift line)'])->[1]; }

check('SELF-57', 'in a multi-suite section, an empty cell takes the SECOND suite\'s row-local citation over the plan doc answer the first suite supplied',
      scalar(($pm_row{'PM-SECOND-01'} // '') =~ /\|\s*fixture_b\.vhd:60\s*\|/
             && ($pm_row{'PM-SECOND-01'} // '') !~ /fixture_a\.vhd:5\b/),
      'got [' . ($pm_row{'PM-SECOND-01'} // '(row missing)') . ']');

check('SELF-58', 'that upgraded citation is charged to the second suite, not to the section\'s first source',
      scalar(pm_drift_cite('PM-SECOND-02') eq 'fixture_b.vhd:61'
             && pm_drift_src('PM-SECOND-02') eq 'test/audio/audio_nextreg_test.cpp'),
      'got src=' . pm_drift_src('PM-SECOND-02')
        . ' cite=' . pm_drift_cite('PM-SECOND-02') . "; drift=[@pmd]");

check('SELF-59', 'a second suite\'s citation does not answer for a row the FIRST suite owns: the plan doc\'s stands',
      scalar(pm_drift_cite('PM-FIRST-01') eq 'fixture_a.vhd:6'
             && pm_drift_src('PM-FIRST-01') eq 'doc/testing/AUDIO-TEST-PLAN-DESIGN.md'),
      'got src=' . pm_drift_src('PM-FIRST-01')
        . ' cite=' . pm_drift_cite('PM-FIRST-01') . "; drift=[@pmd]");

check('SELF-60', 'among row-local citations the first source still wins: the second suite\'s does not displace it',
      scalar(pm_drift_cite('PM-BOTH-01') eq 'fixture_d.vhd:80'
             && pm_drift_src('PM-BOTH-01') eq 'test/audio/audio_test.cpp'),
      'got src=' . pm_drift_src('PM-BOTH-01')
        . ' cite=' . pm_drift_cite('PM-BOTH-01') . "; drift=[@pmd]");

# ── Drift is judged on canonicalised spelling (GH #142) ───────────────
#
# The drift report is how a human notices a citation has gone wrong, so an
# entry that is only a different SPELLING of the same lines is noise hiding a
# real one. `zxnext.vhd:5633, 6260` was reported as disagreeing with the
# canonical `zxnext.vhd:5633,6260`; 20 of the 361 entries the live matrix
# produces were cosmetic that way.
#
# Both sides of the comparison are canonicalised. The STORED CELL IS NOT —
# a hand-written citation is never overwritten, and SELF-65 is the row that
# pins that: a comparator that "fixed" the cell instead would satisfy every
# other row here.
#
# SELF-68 and SELF-69 are the refusals, and they are what stops the whole
# thing degenerating into "never report drift": a different line number still
# drifts, and so does a REORDERING — `80, 70` against `70,80` is folded for
# whitespace and still reported, because the order a citation is written in
# is the author's statement about which line is the primary evidence.

my $ws_src = write_fixture('test/fixture/ws_test.cpp', <<'CPP');
void spellings() {
    check("WS-COMMA-01", "cited — VHDL fixture_a.vhd:100,200-204",
          cond, detail);
    check("WS-SLASH-01", "cited — VHDL fixture_b.vhd:10/20/30",
          cond, detail);
    check("WS-CONT-01", "cited — VHDL fixture_c.vhd:300, :400-402",
          cond, detail);
    check("WS-REAL-01", "cited — VHDL fixture_d.vhd:50,60",
          cond, detail);
    check("WS-ORDER-01", "cited — VHDL fixture_a.vhd:70,80",
          cond, detail);
}
CPP

my $ws_bin = "$FIXTURE_ROOT/bin/ws_suite";
open(my $wfh, '>', $ws_bin) or die "write $ws_bin: $!";
print $wfh "#!/bin/sh\n";
close $wfh;
chmod 0755, $ws_bin;

# Every VHDL cell below is hand-written, and each is a different spelling
# question. Widths are padded so no row trips the "exceeds column width"
# warning, which would drown the real signal.
my $ws_comma_cell = ' fixture_a.vhd:100, 200-204   ';
my @wslines = (
    '## Fixture — `test/fixture/ws_test.cpp`',
    '',
    '| Test ID     | Description             | VHDL file:line               | Status  | Test file:line                              |',
    '|-------------|-------------------------|------------------------------|---------|---------------------------------------------|',
    "| WS-COMMA-01 | space after a `,`       |$ws_comma_cell| missing | missing                                     |",
    '| WS-SLASH-01 | spaces around `/`       | fixture_b.vhd:10 / 20 / 30   | missing | missing                                     |',
    '| WS-CONT-01  | GH #136 `, :NNN` form   | fixture_c.vhd:300, :400-402  | missing | missing                                     |',
    '| WS-REAL-01  | a different line        | fixture_d.vhd:50, 61         | missing | missing                                     |',
    '| WS-ORDER-01 | the same lines, swapped | fixture_a.vhd:80, 70         | missing | missing                                     |',
);

my (@wsd, @wsk);
refresh_section(\@wslines, 0, 'bin/ws_suite', 'test/fixture/ws_test.cpp',
                \@wsd, \@wsk);
my %ws_drift = map { /^\S+\s+(\S+): doc=\[(.*)\] source=\[(.*)\]$/
                     ? ($1 => "$2 vs $3") : () } @wsd;

check('SELF-64', 'a stored cell spelled `100, 200-204` does not drift against the canonical `100,200-204`',
      !exists $ws_drift{'WS-COMMA-01'},
      'got drift ' . ($ws_drift{'WS-COMMA-01'} // '(none)'));

check('SELF-65', 'that cell is written back byte-identical — the comparison canonicalises, the document does not',
      (split(/\|/, $wslines[4], -1))[3] eq $ws_comma_cell,
      'got [' . (split(/\|/, $wslines[4], -1))[3] . "] want [$ws_comma_cell]");

check('SELF-66', 'whitespace around `/` separators is not drift either',
      !exists $ws_drift{'WS-SLASH-01'},
      'got drift ' . ($ws_drift{'WS-SLASH-01'} // '(none)'));

check('SELF-67', 'a stored cell using the filename-omitting `, :NNN` continuation does not drift',
      !exists $ws_drift{'WS-CONT-01'},
      'got drift ' . ($ws_drift{'WS-CONT-01'} // '(none)'));

check('SELF-68', 'the refusal: a genuinely different line number still drifts',
      ($ws_drift{'WS-REAL-01'} // '') eq 'fixture_d.vhd:50, 61 vs fixture_d.vhd:50,60',
      'got drift ' . ($ws_drift{'WS-REAL-01'} // '(none)'));

check('SELF-69', 'the refusal: order is NOT normalised — the same lines in a different order still drift',
      ($ws_drift{'WS-ORDER-01'} // '') eq 'fixture_a.vhd:80, 70 vs fixture_a.vhd:70,80',
      'got drift ' . ($ws_drift{'WS-ORDER-01'} // '(none)'));

printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
       $total, $passed, $failed, 0);
exit($failed ? 1 : 0);
