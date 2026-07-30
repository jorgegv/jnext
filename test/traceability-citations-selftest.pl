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

# Kept before $ROOT is shadowed inside the eval'd copy below: the end-to-end
# rows need the REAL repository to build their fixture tree from.
my $REAL_ROOT = $ROOT;

# Load the extractor without running main(). $ROOT is rebound to the temp
# tree so grep_citations() reads the fixture, and JNEXT_FPGA_SRC points at a
# fake core holding exactly the .vhd names the fixture is allowed to cite —
# so the filename whitelist is exercised for real, not bypassed.
#
# The fake core is NOT flat. A citation may carry a directory prefix
# (GH #145), and the three answers that prefix can get — a real path, a wrong
# path beside a real basename, and a basename that names TWO files — are only
# distinguishable against a tree that has subdirectories and a duplicated
# basename in it, exactly as the real core does (`hdmi_plle2.vhd` lives under
# both `pll/A7/` and `pll/A7-Issue-5/`).
my $FIXTURE_ROOT = tempdir(CLEANUP => 1);
for my $vhd (qw(fixture_a.vhd fixture_b.vhd fixture_c.vhd fixture_d.vhd
                device/fixture_e.vhd
                pll/A7/fixture_dup.vhd pll/A7-Issue-5/fixture_dup.vhd)) {
    my $abs = "$FIXTURE_ROOT/fpga/$vhd";
    (my $dir = $abs) =~ s{/[^/]+$}{};
    my $sofar = '';
    for my $part (grep { length } split m{/}, $dir) {
        $sofar .= "/$part";
        mkdir $sofar unless -d $sofar;
    }
    open(my $fh, '>', $abs) or die "write $vhd: $!";
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
    // GH #144: a bare `, <digits>` continuation whose digits are followed by
    // a `:` is a VHDL BIT SLICE, not a second line reference. Absorbing it
    // publishes a citation the source does not support.
    {
        check("CONT-04",
              "bit slice after a bare comma — VHDL fixture_d.vhd:100, 15:0 field",
              cond, detail);
    }
    // The control that keeps CONT-04 from being satisfied by "never carry a
    // bare continuation": an ordinary bare `, <digits>` list is still carried.
    {
        check("CONT-05",
              "ordinary bare list — VHDL fixture_d.vhd:5633, 6260",
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

# ── A bare continuation must not swallow a bit slice (GH #144) ────────
#
# `fixture_d.vhd:100, 15:0 field` names ONE line; the `15` is the high index
# of a VHDL slice. The bare `, <digits>` arm read it as a second line and
# published `fixture_d.vhd:100,15` — a citation the source does not support,
# which this project ranks strictly below an honest em dash.
#
# SELF-112 is the discriminator. Without it, "never carry a bare
# continuation" — which would delete 852 correct citations across the tree —
# satisfies SELF-111 just as well.

check('SELF-111', 'a bit slice behind a bare comma is NOT absorbed as a second line reference',
      ($cites->{'CONT-04'} // '') eq 'fixture_d.vhd:100',
      "got " . ($cites->{'CONT-04'} // '(none)'));

check('SELF-112', 'the control: an ordinary bare `, <digits>` list is still carried',
      ($cites->{'CONT-05'} // '') eq 'fixture_d.vhd:5633,6260',
      "got " . ($cites->{'CONT-05'} // '(none)'));

# ── Directory-qualified citations (GH #145) ───────────────────────────
#
# The class could not express a directory at all, so a cell written the way
# the design docs write it — `device/copper.vhd:54-119`, the qualified
# relative path — could never equal the bare filename the extractor computed,
# and drifted on every run for ever. A permanent false entry in the drift
# report is noise hiding a real one.
#
# The prefix is now captured and VALIDATED against the real tree, and folded
# for the drift comparison only when the basename is unambiguous. SELF-116 is
# the refusal that keeps that fold honest.

write_fixture('test/fixture/path_test.cpp', <<'CPP');
void paths() {
    check("PATH-OK-01", "a real qualified path — VHDL device/fixture_e.vhd:54-119",
          cond, detail);
    check("PATH-UP-01", "spelled from one level up — VHDL fpga/fixture_a.vhd:7",
          cond, detail);
    check("PATH-BAD-01", "a wrong directory beside a real basename — VHDL nowhere/fixture_b.vhd:9",
          cond, detail);
    check("PATH-GONE-01", "neither path nor basename exists — VHDL nowhere/not_a_real.vhd:1",
          cond, detail);
    check("PATH-SUB-01", "bare restated beside qualified", cond,
          "fixture_e.vhd:54-119 and device/fixture_e.vhd:54-119,200");
    check("PATH-DUP-01", "an AMBIGUOUS basename keeps its directory — VHDL pll/A7/fixture_dup.vhd:3",
          cond, detail);
}
CPP
my @path_warnings;
my $pc = do {
    local $SIG{__WARN__} = sub { push @path_warnings, $_[0] };
    grep_citations('test/fixture/path_test.cpp');
};

check('SELF-113', 'a directory-qualified citation naming a real path is published verbatim, prefix kept',
      ($pc->{'PATH-OK-01'} // '') eq 'device/fixture_e.vhd:54-119',
      "got " . ($pc->{'PATH-OK-01'} // '(none)'));

check('SELF-114', 'a path spelled from one level up still validates — any suffix of the real path is accepted',
      ($pc->{'PATH-UP-01'} // '') eq 'fpga/fixture_a.vhd:7',
      "got " . ($pc->{'PATH-UP-01'} // '(none)'));

check('SELF-115', 'a WRONG directory beside a real basename falls back to the basename and is reported',
      scalar(($pc->{'PATH-BAD-01'} // '') eq 'fixture_b.vhd:9'
             && grep { /nowhere\/fixture_b\.vhd/ } @path_warnings),
      "got " . ($pc->{'PATH-BAD-01'} // '(none)')
        . "; warnings: @path_warnings");

check('SELF-117', 'the whitelist still refuses a name the core does not carry at all, prefix or no prefix',
      !defined $pc->{'PATH-GONE-01'},
      "got " . ($pc->{'PATH-GONE-01'} // '(none)'));

check('SELF-118', 'subset suppression sees through the spelling: a bare restatement is dropped beside the qualified one',
      ($pc->{'PATH-SUB-01'} // '') eq 'device/fixture_e.vhd:54-119,200',
      "got " . ($pc->{'PATH-SUB-01'} // '(none)'));

# canon_citation() is what the drift comparison judges by. It must fold an
# unambiguous prefix (SELF-116a) and must NOT fold an ambiguous one (SELF-116),
# because there the prefix is the whole content of the citation.
check('SELF-116a', 'an unambiguous directory prefix folds for the drift comparison',
      canon_citation('device/fixture_e.vhd:54-119') eq 'fixture_e.vhd:54-119',
      'got ' . canon_citation('device/fixture_e.vhd:54-119'));

check('SELF-116', 'THE REFUSAL: an AMBIGUOUS basename is never folded — two files, two citations',
      scalar(canon_citation('pll/A7/fixture_dup.vhd:3') eq 'pll/A7/fixture_dup.vhd:3'
             && canon_citation('pll/A7-Issue-5/fixture_dup.vhd:3')
                ne canon_citation('pll/A7/fixture_dup.vhd:3')),
      'A7=' . canon_citation('pll/A7/fixture_dup.vhd:3')
        . ' A7-Issue-5=' . canon_citation('pll/A7-Issue-5/fixture_dup.vhd:3'));

check('SELF-119', 'an ambiguous basename keeps its directory when published, too',
      ($pc->{'PATH-DUP-01'} // '') eq 'pll/A7/fixture_dup.vhd:3',
      "got " . ($pc->{'PATH-DUP-01'} // '(none)'));

# ── The `named` tier refuses an ambiguous block (GH #147) ─────────────
#
# The tier survived the banner-comment cull because it keys on an EXPLICIT
# mention of the row ID. Block scope reintroduced the misattribution anyway:
# the first citation anywhere in a block was handed to every ID named anywhere
# in it. `BP-06`'s file banner lists sixteen IDs and mentions an unrelated
# NR 0x08 line first, so BP-06 published that instead of the port 0xFE
# dispatch it tests.
#
# The rule refuses only where attribution is genuinely impossible — several
# rows AND several citations. SELF-121/122/123 are the accept cases that stop
# it collapsing into "the named tier never answers", and SELF-123 in
# particular pins that a prose hyphenation (`VHDL-correct`) is not counted as
# a second row.

write_fixture('test/fixture/named_test.cpp', <<'CPP');
void rows() {
    // AMB-01 and AMB-02 both live here. AMB-01 is about fixture_a.vhd:10 and
    // AMB-02 about fixture_b.vhd:20 — but nothing in this block says which is
    // which, so neither may be published.
    check("AMB-01", "no citation of its own", cond, detail);
    check("AMB-02", "no citation of its own", cond, detail);

    // ONE-01 and ONE-02 share a single fact: fixture_c.vhd:30.
    check("ONE-01", "no citation of its own", cond, detail);
    check("ONE-02", "no citation of its own", cond, detail);

    // SOLO-01, alone, and BOTH its citations belong to it:
    // fixture_a.vhd:40 and fixture_b.vhd:50.
    check("SOLO-01", "no citation of its own", cond, detail);

    // PROSE-CHK-01 alone, asserting the VHDL-correct behaviour, over
    // fixture_a.vhd:60 and fixture_b.vhd:70.
    check("PROSE-CHK-01", "no citation of its own", cond, detail);
}
CPP
my $nm = grep_citations('test/fixture/named_test.cpp');

check('SELF-120', 'THE REFUSAL: a block naming SEVERAL rows and offering SEVERAL citations publishes none of them',
      scalar(!defined $nm->{'AMB-01'} && !defined $nm->{'AMB-02'}),
      sprintf('AMB-01=%s AMB-02=%s', $nm->{'AMB-01'} // '(none)',
              $nm->{'AMB-02'} // '(none)'));

check('SELF-121', 'several rows with ONE citation are still answered — the block has only one answer to give',
      scalar(($nm->{'ONE-01'} // '') eq 'fixture_c.vhd:30'
             && ($nm->{'ONE-02'} // '') eq 'fixture_c.vhd:30'),
      sprintf('ONE-01=%s ONE-02=%s', $nm->{'ONE-01'} // '(none)',
              $nm->{'ONE-02'} // '(none)'));

check('SELF-122', 'ONE row with several citations keeps them all — they all belong to it',
      ($nm->{'SOLO-01'} // '') eq 'fixture_a.vhd:40, fixture_b.vhd:50',
      'got ' . ($nm->{'SOLO-01'} // '(none)'));

check('SELF-123', 'a prose hyphenation is not counted as a second row, so a single-row block is not refused by accident',
      ($nm->{'PROSE-CHK-01'} // '') eq 'fixture_a.vhd:60, fixture_b.vhd:70',
      'got ' . ($nm->{'PROSE-CHK-01'} // '(none)'));

# ── Hand-written cells are validated too (GH #150) ────────────────────
#
# Computed citations were validated against the FPGA tree from the start;
# hand-written ones never were. Because a hand-written cell is never
# overwritten — correctly — a wrong one was permanent AND invisible: it agrees
# with itself on every run, and drift needs a computed side to disagree with.
# Measured on the live matrix: 3 cells name a `.vhd` that does not exist and
# ~25 name jnext's own C++ in a column headed VHDL.
#
# SELF-127..129 are the refusals. Without them "complain about every cell"
# passes SELF-124..126 just as well, and a report that fires on all 3066 rows
# is the saturated warning this tool has already been bitten by twice.

check('SELF-124', 'a hand-written cell naming a .vhd the core does not have is reported',
      scalar(grep { /not in the FPGA core/ }
             bad_hand_citation('kempston_mouse.vhd')) == 1,
      'got [' . join(' | ', bad_hand_citation('kempston_mouse.vhd')) . ']');

check('SELF-125', 'a hand-written cell citing jnext\'s own source is reported — the VHDL is the oracle',
      scalar(grep { /jnext's own source/ }
             bad_hand_citation('fixture_a.vhd:10; emulator.cpp:1163')) == 1,
      'got [' . join(' | ', bad_hand_citation('fixture_a.vhd:10; emulator.cpp:1163')) . ']');

check('SELF-126', 'a hand-written cell carrying no citation at all is reported',
      scalar(grep { /no VHDL citation at all/ }
             bad_hand_citation('n/a (rendering)')) == 1,
      'got [' . join(' | ', bad_hand_citation('n/a (rendering)')) . ']');

# `scalar(bad_hand_citation(...))` would evaluate the sub in SCALAR context,
# where a `return ()` yields undef and a returned list yields its LAST element
# — so `== 0` is true for both an empty complaint list and a non-empty one
# (a string numifies to 0). Every refusal row below would have passed no
# matter what the sub did. Count through an array, once.
sub nbad { my @c = bad_hand_citation(@_); return scalar @c; }

check('SELF-127', 'THE REFUSAL: a valid citation, qualified or bare, is not reported',
      nbad('fixture_a.vhd:10, device/fixture_e.vhd:20') == 0,
      'got [' . join(' | ', bad_hand_citation('fixture_a.vhd:10, device/fixture_e.vhd:20')) . ']');

check('SELF-128', 'THE REFUSAL: a declared `(...)` tombstone is a claim, not an omission',
      scalar(nbad('(jnext-internal)') == 0 && nbad('(ESP-AT firmware)') == 0),
      'got [' . join(' | ', bad_hand_citation('(jnext-internal)'),
                            bad_hand_citation('(ESP-AT firmware)')) . ']');

check('SELF-129', 'THE REFUSAL: an empty or em-dash cell is the honest missing-citation state, not a defect',
      scalar(nbad('') == 0 && nbad('—') == 0 && nbad(undef) == 0),
      'got [' . join(' | ', bad_hand_citation(''), bad_hand_citation('—')) . ']');

# End to end: the complaint reaches the caller AND the cell survives untouched.
# Reporting without rewriting is the whole contract — a checker that "fixed"
# the cell would satisfy SELF-124 and destroy the record.
write_fixture('test/fixture/handcite_test.cpp', <<'CPP');
void h() {
    check("HC-BAD-01", "cell cites a nonexistent file — VHDL fixture_b.vhd:99",
          cond, detail);
    check("HC-OK-01", "cell is fine — VHDL fixture_a.vhd:10", cond, detail);
}
CPP
{
    my $p = "$FIXTURE_ROOT/bin/handcite_suite";
    mkdir "$FIXTURE_ROOT/bin" unless -d "$FIXTURE_ROOT/bin";
    open(my $h, '>', $p) or die "write $p: $!";
    print $h "#!/bin/sh\n";
    close $h;
    chmod 0755, $p;
}
my $hc_bad_cell = ' kempston_mouse.vhd ';
my @hclines = (
    '## Hand — `test/fixture/handcite_test.cpp`',
    '',
    '| Test ID   | Description | VHDL file:line     | Status  | Test file:line                        |',
    '|-----------|-------------|--------------------|---------|---------------------------------------|',
    "| HC-BAD-01 | bad cell    |$hc_bad_cell| missing | missing                               |",
    '| HC-OK-01  | good cell   | fixture_a.vhd:10   | missing | missing                               |',
);
my (@hcd, @hck, @hcinv);
refresh_section(\@hclines, 0, 'bin/handcite_suite',
                'test/fixture/handcite_test.cpp', \@hcd, \@hck, undef, undef,
                \@hcinv);

check('SELF-130', 'refresh_section collects the complaint for the bad cell and none for the good one',
      scalar(@hcinv) == 1 && $hcinv[0] =~ /^HC-BAD-01: names 'kempston_mouse\.vhd'/,
      'got [' . join(' | ', @hcinv) . ']');

check('SELF-131', 'and the reported cell is KEPT byte-identical — this reports, it never rewrites',
      (split_row_cells($hclines[4]))[3] eq $hc_bad_cell,
      'got [' . (split_row_cells($hclines[4]))[3] . "] want [$hc_bad_cell]");

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

# ── The suite accounting gate (GH #144) ───────────────────────────────
#
# `test/unit-tests.conf` is the driver. Every suite it declares must be
# TRACED (named in @SUBSYS) or TOMBSTONED (named in %NO_MATRIX_SECTION with a
# reason), exactly one of the two, and nothing may be accounted for without
# being declared. Anything else is a REFUSAL — the script exits 2 and rewrites
# nothing.
#
# The rows below pin the refusals, because a gate that only ever says "fine"
# is indistinguishable from no gate. Its predecessor was exactly that: a
# warning line inside a report that runs at version-bump time, saturated at
# 50 suites, so the 51st arrived invisible.
#
# suite_accounting() takes its three lists as arguments precisely so the
# contract can be asserted without a repository around it.
my $ACC_DECL = [ ['mmu_test', 250], ['orphan_test', 42], ['fuse_z80_test', 1356] ];
my $ACC_SUB  = [ ['## Memory/MMU — x', 'mmu_test'] ];
my $ACC_TOMB = { 'fuse_z80_test' => 'data-driven runner' };

sub acc { return scalar @{ suite_accounting(@_) }; }

check('SELF-20', 'a declared suite that is traced and a declared suite that is tombstoned both pass the gate',
      acc([ ['mmu_test', 250], ['fuse_z80_test', 1356] ], $ACC_SUB, $ACC_TOMB) == 0,
      "complaints=[" . join(' | ', @{ suite_accounting([ ['mmu_test',250], ['fuse_z80_test',1356] ], $ACC_SUB, $ACC_TOMB) }) . "]");

check('SELF-21', 'THE GATE: a declared suite that is neither traced nor tombstoned is a complaint naming its row count',
      scalar(grep { /^orphan_test \(42 rows\): declared/ }
             @{ suite_accounting($ACC_DECL, $ACC_SUB, $ACC_TOMB) }) == 1,
      "complaints=[" . join(' | ', @{ suite_accounting($ACC_DECL, $ACC_SUB, $ACC_TOMB) }) . "]");

check('SELF-22', 'the reverse direction: a suite traced by @SUBSYS but absent from the manifest is a complaint',
      scalar(grep { /^ghost_test: traced by \@SUBSYS/ }
             @{ suite_accounting([ ['mmu_test', 250] ], [ ['## A', 'mmu_test'], ['## B', 'ghost_test'] ], {}) }) == 1,
      "complaints=[" . join(' | ', @{ suite_accounting([ ['mmu_test',250] ], [ ['## A','mmu_test'], ['## B','ghost_test'] ], {}) }) . "]");

check('SELF-23', 'a tombstone for a suite the manifest does not declare is a complaint too',
      scalar(grep { /^gone_test: tombstoned .* but not/ }
             @{ suite_accounting([ ['mmu_test', 250] ], $ACC_SUB, { 'gone_test' => 'why' }) }) == 1,
      "complaints=[" . join(' | ', @{ suite_accounting([ ['mmu_test',250] ], $ACC_SUB, { 'gone_test' => 'why' }) }) . "]");

check('SELF-71', 'a suite that is BOTH traced and tombstoned is a complaint — it must be exactly one',
      scalar(grep { /^mmu_test: both traced/ }
             @{ suite_accounting([ ['mmu_test', 250] ], $ACC_SUB, { 'mmu_test' => 'why' }) }) == 1,
      "complaints=[" . join(' | ', @{ suite_accounting([ ['mmu_test',250] ], $ACC_SUB, { 'mmu_test' => 'why' }) }) . "]");

check('SELF-72', 'a suite traced by two @SUBSYS entries is a complaint (the Audio shape must use ONE entry with several suites)',
      scalar(grep { /^mmu_test: traced by two/ }
             @{ suite_accounting([ ['mmu_test', 250] ],
                                 [ ['## A', 'mmu_test'], ['## B', 'mmu_test'] ], {}) }) == 1,
      "complaints=[" . join(' | ', @{ suite_accounting([ ['mmu_test',250] ], [ ['## A','mmu_test'], ['## B','mmu_test'] ], {}) }) . "]");

check('SELF-73', 'an UNREASONED tombstone is a complaint — the reason is the whole point of the escape hatch',
      scalar(grep { /^quiet_test: tombstoned with an empty reason/ }
             @{ suite_accounting([ ['quiet_test', 3] ], [], { 'quiet_test' => '  ' }) }) == 1,
      "complaints=[" . join(' | ', @{ suite_accounting([ ['quiet_test',3] ], [], { 'quiet_test' => '  ' }) }) . "]");

check('SELF-74', 'a multi-suite @SUBSYS entry (the Audio shape) accounts for every suite it names',
      acc([ ['audio_test', 1], ['audio_nextreg_test', 2], ['audio_port_dispatch_test', 3] ],
          [ ['## Audio — x', ['audio_test', 'audio_nextreg_test', 'audio_port_dispatch_test']] ],
          {}) == 0,
      "complaints=[" . join(' | ', @{ suite_accounting([ ['audio_test',1], ['audio_nextreg_test',2], ['audio_port_dispatch_test',3] ], [ ['## Audio — x', ['audio_test','audio_nextreg_test','audio_port_dispatch_test']] ], {}) }) . "]");

# ── Suite -> source, resolved from CMake ──────────────────────────────
#
# The source path is READ FROM CMAKE, never written by hand and never derived
# from the suite name. 79 of 87 suite names do match their source basename and
# 8 do not, and a convention that is right 91% of the time is the worst kind:
# `cpu_int_pulse_test` is built from `cpu/int_pulse_test.cpp`, the six
# `debugger_*` suites likewise, and the two ESP-01 module suites are declared
# in `src/esp01/CMakeLists.txt` with their sources under `src/esp01/test/`.
#
# SELF-70 is the module-resident half, re-pointed from the basename mapping it
# used to pin (GH #25) onto the mechanism that replaced it (GH #144): the same
# property — a suite whose source lives OUTSIDE `test/` resolves correctly —
# now holds because the path is read from the CMakeLists.txt that declares it,
# resolved relative to that file's own directory.
write_fixture('test/CMakeLists.txt', <<'CML');
# a comment: add_executable(commented_test should/not/count.cpp)
add_executable(mmu_test mmu/mmu_test.cpp ${CMAKE_SOURCE_DIR}/src/core/wav_loader.cpp)
add_executable(cpu_int_pulse_test cpu/int_pulse_test.cpp)
if(ENABLE_DEBUGGER)
    add_executable(debugger_video_panel_test debugger/video_panel_test.cpp)
endif()
add_executable(jnext_tests ${GTEST_SOURCES})
CML
write_fixture('src/esp01/CMakeLists.txt', <<'CML');
    add_executable(esp_at_test test/esp_at_test.cpp)
CML
my $csrc = cmake_sources();

check('SELF-75', 'the first source of add_executable() is the suite source, extra translation units ignored',
      ($csrc->{'mmu_test'} // '') eq 'test/mmu/mmu_test.cpp',
      "got " . ($csrc->{'mmu_test'} // '(unresolved)'));

check('SELF-78', 'a suite whose NAME differs from its source basename resolves from CMake, not from the name',
      ($csrc->{'cpu_int_pulse_test'} // '') eq 'test/cpu/int_pulse_test.cpp',
      "got " . ($csrc->{'cpu_int_pulse_test'} // '(unresolved)'));

check('SELF-70', 'a MODULE-RESIDENT suite resolves relative to the CMakeLists.txt that declares it, outside test/',
      ($csrc->{'esp_at_test'} // '') eq 'src/esp01/test/esp_at_test.cpp',
      "got " . ($csrc->{'esp_at_test'} // '(unresolved)'));

check('SELF-76', 'an add_executable() inside an if() block still resolves — the gate is a build option, not a scoping rule',
      ($csrc->{'debugger_video_panel_test'} // '') eq 'test/debugger/video_panel_test.cpp',
      "got " . ($csrc->{'debugger_video_panel_test'} // '(unresolved)'));

check('SELF-77', 'a ${VAR} source LIST is refused rather than guessed at, and a commented-out declaration is ignored',
      !exists $csrc->{'jnext_tests'} && !exists $csrc->{'commented_test'},
      "jnext_tests=" . ($csrc->{'jnext_tests'} // '(absent)')
      . " commented_test=" . ($csrc->{'commented_test'} // '(absent)'));

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

# ── The `[FAIL]` harness spelling (GH #144) ───────────────────────────
#
# Three suites print `[FAIL] <name>` rather than the `  FAIL <name>:` form,
# and two of them — `cpu_int_pulse_test` and `cpu_z80n_im2_regressions_test` —
# are traced sections. A FAIL scanner blind to that spelling reads their FAIL
# set as EMPTY and publishes `pass` for a row whose assertion fails, which is
# the one direction a Status column must never be wrong in.
write_fixture('test/fixture/bracket_test.cpp', <<'CPP');
void b() {
    check(res, "BR-01", cond, "passing row — VHDL fixture_a.vhd:1");
    check(res, "BR-02", cond, "failing row");
}
CPP
{
    my $path = "$FIXTURE_ROOT/bin/bracket_suite";
    open(my $h, '>', $path) or die "write $path: $!";
    print $h "#!/bin/sh\necho '[PASS] BR-01'\necho '[FAIL] BR-02  detail here'\n";
    close $h;
    chmod 0755, $path;
}
my @bracket = (
    '## Bracket — `test/fixture/bracket_test.cpp`',
    '',
    '| Test ID | Description | VHDL file:line | Status  | Test file:line                  |',
    '|---------|-------------|----------------|---------|---------------------------------|',
    '| BR-01   | passing     | —              | missing | missing                         |',
    '| BR-02   | failing     | —              | missing | missing                         |',
);
my (@brd, @brk);
refresh_section(\@bracket, 0, 'bin/bracket_suite',
                'test/fixture/bracket_test.cpp', \@brd, \@brk);

check('SELF-79', 'a `[FAIL] ID` line is read as a failure, not whitewashed into `pass`',
      scalar($bracket[5] =~ /\|\s*fail\s*\|/),
      "got [$bracket[5]]");

check('SELF-80', 'the refusal: a `[PASS]` row in the same suite is NOT dragged into the FAIL set',
      scalar($bracket[4] =~ /\|\s*pass\s*\|/),
      "got [$bracket[4]]");

# ── `set_group()` must not shadow the row's own call (GH #144) ────────
#
# `set_group("ID")` prints a banner; it is not the row's assertion.
# grep_row_ids() has dropped it since GH #117. grep_citations() did not, so
# the banner became the ID's first non-comment occurrence, the call-span
# lookup found no call owning the row, and the citation silently fell through
# to the file-header comment block that names it — publishing a neighbour's
# lines over the ones the row's own check() carries. Measured over every
# suite: two rows, and both were wrong.
write_fixture('test/fixture/banner_test.cpp', <<'CPP');
// Header block naming SG-01 and SG-02, citing fixture_d.vhd:900 for both.
void g() {
    set_group("SG-01");
    check("SG-01", "row one — VHDL fixture_a.vhd:11", cond, detail);
    set_group("SG-02");
    check("SG-02", "row two — VHDL fixture_b.vhd:22", cond, detail);
}
CPP
my $banner = grep_citations('test/fixture/banner_test.cpp');

check('SELF-81', 'a row preceded by its own set_group() banner still takes the citation from its own check()',
      ($banner->{'SG-01'} // '') eq 'fixture_a.vhd:11'
        && ($banner->{'SG-02'} // '') eq 'fixture_b.vhd:22',
      sprintf("SG-01=%s SG-02=%s", $banner->{'SG-01'} // '(none)',
              $banner->{'SG-02'} // '(none)'));

check('SELF-82', 'the banner label itself is not published as a row citation',
      !exists $banner->{'fixture_d'},
      'banner label leaked as a row');

# ── EVERY citation in the evidence, not just the first (GH #144) ──────
#
# cite_in() was a scalar-context match, so a row whose check() named two files
# published one. 27 of 126 published cells were a strict prefix of the truth,
# and a prefix reads exactly like a complete answer — the drift report cannot
# see it either, because the extractor agreed with itself every run.
write_fixture('test/fixture/multicite_test.cpp', <<'CPP');
void m() {
    check("MC-01", "two files", cond, "fixture_a.vhd:10; fixture_b.vhd:20-22");
    check("MC-02", "prose interrupts the filename-omitting tail",
          cond, "fixture_a.vhd:30 (the gate), :44 (the latch)");
    check("MC-03", "the same lines restated twice", cond,
          "fixture_c.vhd:5 and again fixture_c.vhd:5");
    check("MC-04", "a real file and an imaginary one", cond,
          "fixture_a.vhd:70, not_in_the_core.vhd:99");
}
CPP
my $mc = grep_citations('test/fixture/multicite_test.cpp');

check('SELF-83', 'a second FILE in the same evidence is published, not dropped',
      ($mc->{'MC-01'} // '') eq 'fixture_a.vhd:10, fixture_b.vhd:20-22',
      "got " . ($mc->{'MC-01'} // '(none)'));

check('SELF-84', 'the refusal: a filename-omitting tail behind prose is NOT reached across',
      ($mc->{'MC-02'} // '') eq 'fixture_a.vhd:30',
      "got " . ($mc->{'MC-02'} // '(none)'));

check('SELF-85', 'a citation restated in the same evidence is published once, not twice',
      ($mc->{'MC-03'} // '') eq 'fixture_c.vhd:5',
      "got " . ($mc->{'MC-03'} // '(none)'));

check('SELF-86', 'an unknown filename is dropped from the list while its valid neighbour survives',
      ($mc->{'MC-04'} // '') eq 'fixture_a.vhd:70',
      "got " . ($mc->{'MC-04'} // '(none)'));

# Collecting every citation surfaced a legibility regression the single-match
# version could not have: a description naming the headline line and a detail
# naming the full set published `file:169, file:169,176`. A strict restatement
# is suppressed by verbatim TOKEN SUBSET on the same filename — nothing is
# merged, renumbered or reordered.
write_fixture('test/fixture/subset_test.cpp', <<'CPP');
void s() {
    check("SS-01", "headline in the description (fixture_a.vhd:169)",
          cond, "fixture_a.vhd:169,176");
    check("SS-02", "disjoint line sets for one file stay both",
          cond, "fixture_a.vhd:10 and fixture_a.vhd:20");
    check("SS-03", "same lines, two different files, both stay",
          cond, "fixture_a.vhd:5 mirrored at fixture_b.vhd:5");
    check("SS-04", "a bare filename is redundant beside a lined one",
          cond, "see fixture_c.vhd, specifically fixture_c.vhd:42");
}
CPP
my $ss = grep_citations('test/fixture/subset_test.cpp');

check('SELF-93', 'a strict restatement of the same file with fewer lines is suppressed',
      ($ss->{'SS-01'} // '') eq 'fixture_a.vhd:169,176',
      "got " . ($ss->{'SS-01'} // '(none)'));

check('SELF-94', 'the refusal: DISJOINT line sets for the same file are both kept, in source order',
      ($ss->{'SS-02'} // '') eq 'fixture_a.vhd:10, fixture_a.vhd:20',
      "got " . ($ss->{'SS-02'} // '(none)'));

check('SELF-95', 'the refusal: identical lines in two DIFFERENT files are both kept',
      ($ss->{'SS-03'} // '') eq 'fixture_a.vhd:5, fixture_b.vhd:5',
      "got " . ($ss->{'SS-03'} // '(none)'));

check('SELF-96', 'a bare filename is suppressed beside a lined citation of the same file',
      ($ss->{'SS-04'} // '') eq 'fixture_c.vhd:42',
      "got " . ($ss->{'SS-04'} // '(none)'));

# ── A guard call must not shadow the assertion (GH #144) ──────────────
#
# A row is routinely asserted twice: the real check(), and a fixture-init
# guard reusing the same ID, which is textually FIRST. Taking only the first
# occurrence let the guard — which carries no citation — shadow the assertion
# that does, and 21 Multiface rows fell through to their banner comment.
write_fixture('test/fixture/guard_test.cpp', <<'CPP');
// Banner naming GD-01 and GD-02 and citing fixture_d.vhd:900 for both.
void g() {
    if (!init()) {
        check("GD-01", "fixture init failed", false, "init returned false");
        return;
    }
    check("GD-01", "the real assertion", cond, "fixture_a.vhd:12");
    if (!init()) {
        check("GD-02", "fixture init failed", false, "init returned false");
        return;
    }
    check("GD-02", "an assertion with no citation of its own", cond, "no vhdl here");
}
CPP
my $guard = grep_citations('test/fixture/guard_test.cpp');

check('SELF-87', 'the CITED call wins over an earlier guard call reusing the same ID',
      ($guard->{'GD-01'} // '') eq 'fixture_a.vhd:12',
      "got " . ($guard->{'GD-01'} // '(none)'));

check('SELF-88', 'the refusal: when NO call carrying the ID cites anything, the `next` tier stays fenced off',
      ($guard->{'GD-02'} // '') eq 'fixture_d.vhd:900',
      "got " . ($guard->{'GD-02'} // '(none)'));

# ── `Test file:line` must name the SAME call the citation came from ───
#
# Fixing the citation half alone left the two halves of a row disagreeing:
# line_for() still resolved from the first occurrence, so 22 Multiface rows
# published the dormant fixture-init guard's line — a `check(id, "Emulator
# init failed", false, ...)` that never executes — beside a citation read from
# the real assertion six lines down. A reader following the column landed on a
# line that is not the assertion.
my (%gd_from, %gd_at);
grep_citations('test/fixture/guard_test.cpp', \%gd_from, \%gd_at);
my ($gd_checks, $gd_skips) = grep_source('test/fixture/guard_test.cpp');
my %gd_where = map { $_ => 'test/fixture/guard_test.cpp' }
               (keys %$gd_checks, keys %$gd_skips);

my ($gd1_src, $gd1_line) =
    line_for('GD-01', $gd_checks, $gd_skips, \%gd_where, \%gd_at);
my ($gd1_src_old, $gd1_line_old) =
    line_for('GD-01', $gd_checks, $gd_skips, \%gd_where);

check('SELF-97', 'Test file:line names the CITED call, not the guard that reused the ID first',
      defined $gd1_line && $gd1_line == 7 && $gd1_line_old == 4,
      sprintf("cited-call line=%s first-occurrence line=%s",
              $gd1_line // '(none)', $gd1_line_old // '(none)'));

check('SELF-98', 'the citation and the line come from ONE call — the two halves cannot disagree',
      ($guard->{'GD-01'} // '') eq 'fixture_a.vhd:12' && ($gd_at{'GD-01'} // 0) == 7,
      sprintf("cite=%s at=%s", $guard->{'GD-01'} // '(none)',
              $gd_at{'GD-01'} // '(none)'));

check('SELF-99', 'the refusal: a row whose citation came from a COMMENT gets no line from it, and falls back to its own occurrence',
      !defined $gd_at{'GD-02'},
      "got " . ($gd_at{'GD-02'} // '(undef, correct)'));

# ── Locating a section header (GH #144) ───────────────────────────────
#
# A suite named in @SUBSYS passes the accounting gate as TRACED, but if its
# header is not actually in the document then nothing scans it and its rows
# are recorded nowhere — the very condition the gate exists to make
# impossible. That is a REFUSAL, and it was inline in main(), which this file
# strips: the one part of the contract nothing could assert.
my @rs_doc = (
    '## Copper — `test/copper/copper_test.cpp`',
    '',
    '## ULA Video — `test/ula/ula_test.cpp` + `test/ula/ula_integration_test.cpp`',
    '',
    '## Copper Extra — `test/copper/other_test.cpp`',
);
my ($rs_found, $rs_missing) = resolve_sections(\@rs_doc, [
    ['## Copper — `test/copper/copper_test.cpp`',  ['bin/c'], ['test/c.cpp']],
    ['## ULA Video — `test/ula/ula_test.cpp`',     ['bin/u'], ['test/u.cpp']],
    ['## Nowhere — `test/nope/nope_test.cpp`',     ['bin/n'], ['test/n.cpp']],
]);

check('SELF-89', 'THE REFUSAL: a traced suite whose section header is absent is reported, not skipped silently',
      scalar(@$rs_missing) == 1
        && $rs_missing->[0] eq '## Nowhere — `test/nope/nope_test.cpp`',
      "missing=[" . join(' | ', @$rs_missing) . "]");

check('SELF-90', 'a header that gained a " + `companion.cpp`" suffix still resolves by prefix',
      scalar(@$rs_found) == 2 && $rs_found->[1][0] == 2,
      "found=[" . join(' ', map { $_->[0] } @$rs_found) . "]");

check('SELF-91', 'the refusal: the prefix match does not let `## Copper` swallow `## Copper Extra`',
      scalar(@$rs_found) >= 1 && $rs_found->[0][0] == 0,
      "Copper resolved to line " . ($rs_found->[0][0] // '(none)'));

# resolve_sections() answers WHICH headers are absent; the CONSEQUENCE — exit
# 2 and write nothing — was still inline in main(), which this file strips.
# A reviewer reverted that refusal to print-and-continue and the selftest
# stayed green, so it was a rule nothing could assert.
my @rf_none = section_refusal([]);
my @rf_some = section_refusal(['## Nowhere — `x`']);

check('SELF-100', 'a missing section header exits 2 AND forbids writing the matrix',
      $rf_some[0] == 2 && !$rf_some[1],
      sprintf("got rc=%d may_write=%d", @rf_some));

check('SELF-101', 'the refusal: with every section present the run proceeds and may write',
      $rf_none[0] == 0 && $rf_none[1],
      sprintf("got rc=%d may_write=%d", @rf_none));

# ── Exit 3 is internal error, and must not read as the gate's exit 2 ──
#
# `die` derives its status from errno, so "binary not found" exited 2 (ENOENT)
# — indistinguishable from a REFUSAL, which is the signal `make unit-test`
# now acts on. Every internal error goes through fatal() instead, which stays
# catchable so this file can assert the refusals at all.
my $fatal_died = 0;
eval { fatal('a synthetic internal error'); 1 } or $fatal_died = 1;
check('SELF-92', 'fatal() raises rather than exiting, so a caller can still catch it',
      $fatal_died && $@ =~ /^refresh-traceability-matrix: a synthetic internal error$/m,
      $fatal_died ? "message=[$@]" : 'fatal() returned without raising');

# ── The exit-status contract ──────────────────────────────────────────
#
# main() is stripped when this file loads the script, so the glue that
# turns the row-level gap into a process exit status would otherwise be the
# one part of the contract nothing pins. The SUITE-level gap is deliberately
# NOT an input here: it is a refusal that exits 2 before anything is read or
# written, so it cannot be expressed as a return value from this sub.
check('SELF-29', 'an unrecorded row exits 1',
      report_exit_code(1) == 1 && report_exit_code(3) == 1,
      sprintf("got %d/%d", report_exit_code(1), report_exit_code(3)));

check('SELF-30', 'no unrecorded row exits 0',
      report_exit_code(0) == 0,
      "got " . report_exit_code(0));

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

# ── An escaped `\|` in a Description is a literal, not a column (GH #157) ──
#
# Markdown spells a literal pipe inside a cell `\|`, and Descriptions need it
# (`RESET_HARD\|RESET_SOFT`). Splitting the row on a bare `|` breaks it AT the
# escape, so every later cell is one column right of where the writer thinks
# it is: the Status goes into the VHDL column, the test location into the
# Status column, and the real Test file:line is never updated again. That is
# how `RW-01` and `SR-05` shipped reading `pass` as their VHDL citation.
#
# The row still has the right number of columns, the counts still add up, and
# every later run reproduces it identically — so nothing downstream can see
# it. The rows below pin the whole shape, not just the citation: SELF-106
# that the three rewritten cells land in their OWN columns, SELF-107 that the
# pipe SURVIVES rather than being stripped (dropping it would align the
# columns and lose the text), SELF-108 the residual an escape cannot cover —
# a RAW pipe is a real column break and must be reported, not rewritten
# around — and SELF-109 the read side: the Description tail must not be read
# back as a citation and reported as drift. SELF-110 is the control — the
# same pipeline on a pipe-free row — so a broken fixture fails loudly instead
# of making SELF-106 vacuous.

write_fixture('test/fixture/pipe_test.cpp', <<'CPP');
void escaped() {
    check("PIPE-ESC-01",  "escaped — VHDL fixture_a.vhd:900", cond, detail);
    check("PIPE-CTRL-01", "control — VHDL fixture_b.vhd:910", cond, detail);
}
CPP

my $pipe_bin = "$FIXTURE_ROOT/bin/pipe_suite";
open(my $pfh, '>', $pipe_bin) or die "write $pipe_bin: $!";
print $pfh "#!/bin/sh\n";
close $pfh;
chmod 0755, $pipe_bin;

my $pipe_desc = ' reads `(a<<4) \| b`, pads bits[7:6]  ';
my @pplines = (
    '## Pipe — `test/fixture/pipe_test.cpp`',
    '',
    '| Test ID      | Description                          | VHDL file:line       | Status  | Test file:line                       |',
    '|--------------|--------------------------------------|----------------------|---------|--------------------------------------|',
    "| PIPE-ESC-01  |$pipe_desc| —                    | missing | missing                              |",
    '| PIPE-CTRL-01 | no pipe in this one                  | —                    | missing | missing                              |',
);

my (@ppd, @ppk);
refresh_section(\@pplines, 0, 'bin/pipe_suite', 'test/fixture/pipe_test.cpp',
                \@ppd, \@ppk);

check('SELF-106', 'a Description carrying `\|` still gets VHDL, Status and Test file:line in their OWN columns',
      scalar($pplines[4] =~ m{\|\s*fixture_a\.vhd:900\s*\|\s*pass\s*\|\s*test/fixture/pipe_test\.cpp:\d+\s*\|$}),
      "got [$pplines[4]]");

check('SELF-107', 'the escaped pipe SURVIVES in the Description — escaped, never stripped to make the columns line up',
      index($pplines[4], "|$pipe_desc|") >= 0,
      "got [$pplines[4]]");

# A RAW `|` is a genuine column break and nothing can tell it from an intended
# one, so the escape fixes nothing here — the only honest answer is to say so.
my @rawlines = (
    '## Pipe — `test/fixture/pipe_test.cpp`',
    '',
    '| Test ID      | Description                          | VHDL file:line       | Status  | Test file:line                       |',
    '|--------------|--------------------------------------|----------------------|---------|--------------------------------------|',
    '| PIPE-RAW-01  | reads (a<<4) | b, pads bits[7:6]     | —                    | missing | missing                              |',
);
my (@rwd, @rwk, @rawwarn);
{
    local $SIG{__WARN__} = sub { push @rawwarn, $_[0] };
    refresh_section(\@rawlines, 0, 'bin/pipe_suite',
                    'test/fixture/pipe_test.cpp', \@rwd, \@rwk);
}
my @raw_hits = grep { /PIPE-RAW-01.*unescaped/s } @rawwarn;

check('SELF-108', 'the residual an escape cannot cover: a RAW `|` in a Description is REPORTED, not silently rewritten around',
      scalar(@raw_hits) == 1,
      'warnings: ' . (join('; ', map { my $w = $_; chomp $w; $w } @rawwarn) || '(none)'));

# grep's LIST is greedy — without the parens it swallows the detail argument
# too, and the arity guard at the top of this file catches it.
my @pp_esc_drift = grep { /PIPE-ESC-01/ } @ppd;
check('SELF-109', 'the read side: the Description tail is not read back as a citation and reported as drift',
      scalar(@pp_esc_drift) == 0,
      'got drift ' . (join('; ', @pp_esc_drift) || '(none)'));

check('SELF-110', 'the control: the identical pipeline on a pipe-free row, so SELF-106 is not vacuous',
      scalar($pplines[5] =~ m{\|\s*fixture_b\.vhd:910\s*\|\s*pass\s*\|\s*test/fixture/pipe_test\.cpp:\d+\s*\|$}),
      "got [$pplines[5]]");

# ── END TO END: main()'s own glue, in a subprocess (GH #144) ─────────
#
# Every row above loads this file's target WITHOUT main() and asserts a sub in
# isolation. That has now missed the same structural gap three rounds running:
# report_exit_code(), companion_map(), resolve_sections() and section_refusal()
# were each extracted so they COULD be asserted, and each time main()'s USE of
# them stayed untested. The proof it was not being tested: deleting main()'s
# call to section_refusal() and restoring print-and-continue left this file
# 101/101 green. Extracting a fifth helper would not have closed it.
#
# So this pair runs the REAL script as a REAL process against a throwaway
# repository, and asserts the two things a refusal has to do: exit 2, and
# leave the document byte-identical. Every line of main() between the argument
# walk and the write is on that path.
#
# The fixture is the real repo's manifest, CMakeLists and matrix — because
# @SUBSYS is baked into the script under test and must agree with them — plus
# an EMPTY stub for every source and a do-nothing stub for every binary. Empty
# sources mean every row resolves `missing`, which is irrelevant here: what is
# under test is whether the process writes at all.
{
    my $E2E = "$FIXTURE_ROOT/e2e";
    my @suites;
    {
        open(my $fh, '<', "$REAL_ROOT/test/unit-tests.conf")
            or die "e2e: open unit-tests.conf: $!";
        while (my $l = <$fh>) {
            next if $l =~ /^\s*#/ || $l !~ /\S/;
            my ($n) = split(' ', $l);
            $n =~ s/^\?//;
            push @suites, $n;
        }
        close $fh;
    }

    # Suite -> source, parsed here rather than through cmake_sources(): that
    # sub is memoised and already answered for a different fixture above.
    my %src_of;
    for my $rel ('test/CMakeLists.txt', 'src/esp01/CMakeLists.txt') {
        (my $dir = $rel) =~ s{/CMakeLists\.txt$}{};
        open(my $fh, '<', "$REAL_ROOT/$rel") or next;
        while (my $l = <$fh>) {
            next if $l =~ /^\s*#/;
            next unless $l =~ /\badd_executable\s*\(\s*([A-Za-z0-9_]+)\s+([^\s()]+)/;
            next if $2 =~ /^\$\{/;
            $src_of{$1} = "$dir/$2";
        }
        close $fh;
    }

    my $build_e2e = sub {
        my ($drop_section) = @_;
        system('rm', '-rf', $E2E) == 0 or die "e2e: rm -rf: $?";
        for my $d ('test', 'doc/testing', 'build/test', 'src/esp01/test') {
            system('mkdir', '-p', "$E2E/$d") == 0 or die "e2e: mkdir $d: $?";
        }
        for my $f ('test/unit-tests.conf', 'test/CMakeLists.txt',
                   'src/esp01/CMakeLists.txt',
                   'test/refresh-traceability-matrix.pl') {
            system('cp', "$REAL_ROOT/$f", "$E2E/$f") == 0
                or die "e2e: cp $f: $?";
        }
        for my $s (values %src_of) {
            (my $d = "$E2E/$s") =~ s{/[^/]+$}{};
            system('mkdir', '-p', $d) == 0 or die "e2e: mkdir $d: $?";
            open(my $h, '>', "$E2E/$s") or die "e2e: write $s: $!";
            close $h;
        }
        for my $s (@suites) {
            open(my $h, '>', "$E2E/build/test/$s") or die "e2e: stub $s: $!";
            print $h "#!/bin/sh\n";
            close $h;
            chmod 0755, "$E2E/build/test/$s";
        }
        open(my $in, '<', "$REAL_ROOT/doc/testing/TRACEABILITY-MATRIX.md")
            or die "e2e: open matrix: $!";
        my @m = <$in>;
        close $in;
        @m = grep { !/^\Q$drop_section\E/ } @m if $drop_section;
        open(my $out, '>', "$E2E/doc/testing/TRACEABILITY-MATRIX.md")
            or die "e2e: write matrix: $!";
        print $out @m;
        close $out;
        return "$E2E/doc/testing/TRACEABILITY-MATRIX.md";
    };

    my $digest = sub {
        my ($p) = @_;
        open(my $h, '<', $p) or return '(unreadable)';
        local $/;
        my $body = <$h>;
        close $h;
        return length($body) . ':' . unpack('%32C*', $body);
    };

    my $run = sub {
        my $rc = system("perl '$E2E/test/refresh-traceability-matrix.pl' "
                      . ">/dev/null 2>&1");
        return $rc >> 8;
    };

    # (a) THE REFUSAL. One traced suite's `## ` header removed.
    my $mpath  = $build_e2e->('## Multiface — ');
    my $before = $digest->($mpath);
    my $rc_a   = $run->();
    my $after  = $digest->($mpath);

    check('SELF-102', 'END TO END: a traced suite whose section is absent makes the real process exit 2',
          $rc_a == 2, "got exit $rc_a");

    check('SELF-103', 'END TO END: and that refusal leaves the document byte-identical',
          $before eq $after,
          $before eq $after ? "unchanged" : "REWRITTEN ($before -> $after)");

    # (b) THE DISCRIMINATOR. Same tree, section present: the run must proceed
    # past the refusal and rewrite. Without this, a script that refused
    # unconditionally — or never wrote at all — would pass (a) too.
    my $mpath_b  = $build_e2e->(undef);
    my $before_b = $digest->($mpath_b);
    my $rc_b     = $run->();
    my $after_b  = $digest->($mpath_b);

    check('SELF-104', 'END TO END: the discriminator — with every section present the process does NOT refuse',
          $rc_b != 2, "got exit $rc_b");

    check('SELF-105', 'END TO END: and it reaches the write, so (a) pinned a refusal and not a no-op script',
          $before_b ne $after_b,
          $before_b ne $after_b ? "rewritten" : "NOT rewritten ($before_b)");
}

printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
       $total, $passed, $failed, 0);
exit($failed ? 1 : 0);
