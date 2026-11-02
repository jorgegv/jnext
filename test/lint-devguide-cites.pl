#!/usr/bin/env perl
# Developer-guide cite check: every file:line cite of a jnext source in the
# developer guide is verified against the code (GH #430).
#
# WHY. The guide cites code as `file.cpp:123`. Nothing moved those cites when a
# cited file changed, so they drifted silently (one merge shifted 17 of chapter
# 7's). Owner decision (2026-11-02): keep the cites, anchor each to a named
# symbol, and gate them in `make docs-check`.
#
# SCOPE, literally: every *.md under src/doc/developer-guide/ and every
# diagrams/*.dot, outside fenced code blocks (``` ... ```), which hold sample
# data of formats the guide describes (SLD/map/listing excerpts), not cites.
# Nothing in prose is skipped: see "UNKNOWN EXTENSION" below.
#
# CITE FORMS (all on prose lines or .dot text):
#   path/file.ext:N          file.ext:N-M         a repo file, N or N..M a 1-based line
#   `:N`  `:N-M`             a backticked span holding only the line number: it
#                            resolves to the MOST RECENT file named earlier on the
#                            same page (any file.ext token, cited with a line or
#                            not, in this page's text order). If that file is
#                            declared external the cite is out of scope with it.
#   line(s) N[-M] of `file`  the prose form; same rules.
#   Known extensions: cpp cc c h hpp asm vhd py sh pl cmake txt conf md def m4
#   inc bas yaml yml json lis map sld.
#
# RESOLUTION. A path or bare name resolves to the repo files whose path equals it
# or ends with "/" + it (walk of the root, skipping .git, build*, third_party).
# No match, or the cite has an unknown extension (name.xyz:N in prose): FAIL.
# More than one match: FAIL, naming the candidates (write a longer path suffix).
#
# ANCHOR. The nearest backticked identifier BEFORE the cite in the same sentence
# (sentence = back to the previous ". " + capital/backtick/bracket, or the
# paragraph start: a blank line, a list bullet, a table row, a heading, a fence;
# in .dot, consecutive // comment lines or one line). An identifier is a span
# like `name`, `Class::name`, `obj.member`, `name()` or `name(args)` (the last
# component is the anchor), or a test ID like `QSRC-195`. The anchor must appear
# as a whole word on a cited line; for a range N-M, on some line in N..M. There
# is NO +-1 tolerance. No identifier in the sentence: FAIL "unanchored".
#
# OUT OF SCOPE BY DECLARATION (the only skips; everything else is checked):
#   1. any *.vhd                      the FPGA repository, not this one
#   2. z88dk/..., zcc.c szx.c symtab1.c zxn_crt.asm.m4 and src/{zcc,ticks,80cc,
#      sccz80,z80asm}/...             z88dk sources
#   3. sjasm/... (sjio.cpp tables.cpp directives.cpp devices.cpp support.cpp
#      preproc.cpp)                   sjasmplus sources; src/{zxbc,zxbasm}/*.py
#                                     Boriel ZX Basic; nexload.asm nexload2.asm
#                                     readdir.asm esxapi.def              NextZXOS;
#                                     nextbuild.py keywords.py zxn_crt.asm  NextBuild, Boriel, z88dk
#   4. BARE (no directory part) main.c util.c util.h asmfn.asm main.bas
#      equlabels.asm                  positions in the example PROGRAM the source
#                                     map describes (main.c:17), not code cites.
#      Written WITH a directory (test/fixtures/z88dk/src/main.c:14) they are
#      repo cites and checked.
#
# A comma list of lines (file.cpp:12,34) FAILS: write one cite each.
#
# CANNOT CATCH, as examples and not as an exhaustive list: a cite in a fenced
# block; a cite whose sentence names a real identifier that is not the one the
# cite is about (the anchor proves the line holds that symbol, not that the
# sentence is right); a cite split over lines inside one token; a symbol that is
# also mentioned on the neighbouring line of a range; an extension outside the
# list above whose failure message says "unknown extension".
#
# Usage: perl test/lint-devguide-cites.pl [root]     (root defaults to the repo)
# Output: one line per failure `page:line: cite | anchor | cited line`, then a
# `checked N cites, F failed, S out of scope` summary line.
# Exit: 0 clean, 1 any failure, 2 usage / unreadable guide.

use strict;
use warnings;
use File::Find;
use File::Basename qw(dirname);
use Cwd qw(abs_path);

$ENV{LC_ALL} = 'C';
my $root = shift // abs_path(dirname($0) . '/..');
my $guide = "$root/src/doc/developer-guide";
-d $guide or do { print STDERR "lint-devguide-cites: no $guide\n"; exit 2 };

my $EXT = qr/(?:cpp|cc|c|h|hpp|asm|vhd|py|sh|pl|cmake|txt|conf|md|def|m4|inc|bas|yaml|yml|json|lis|map|sld)/;
my $FILE = qr/(?:[A-Za-z0-9_+.\/-]*\/)?[A-Za-z0-9_+-]+(?:\.[A-Za-z0-9_+-]+)*?\.$EXT/;

my @EXTERNAL = (
    qr/\.vhd$/,
    qr/(?:^|\/)z88dk\//, qr/^(?:zcc|szx|symtab1)\.c$/, qr/(?:^|\/)zxn_crt\.asm\.m4$/,
    qr/^src\/(?:zcc|ticks|80cc|sccz80|z80asm)\//,
    qr/^sjasm\//, qr/^(?:sjio|tables|directives|devices|support|preproc)\.cpp$/,
    qr/^src\/(?:zxbc|zxbasm)\//,
    qr/^(?:nexload2?\.asm|readdir\.asm|esxapi\.def)$/,
    qr/(?:^|\/)(?:nextbuild\.py|keywords\.py|zxn_crt\.asm)$/,
    qr/^(?:main\.c|util\.c|util\.h|asmfn\.asm|main\.bas|equlabels\.asm)$/,
);
sub is_external { my $t = shift; for (@EXTERNAL) { return 1 if $t =~ $_ } 0 }

# ---- repo index: path -> exists; suffix lookup by basename
my %by_base;
find({ no_chdir => 1, wanted => sub {
    if (-d $_) {
        my $b = (split /\//, $_)[-1];
        $File::Find::prune = 1 if $b eq '.git' || $b =~ /^build/ || $b eq 'third_party';
        return;
    }
    return if -l $_;
    my $rel = substr($_, length($root) + 1);
    my $b = (split /\//, $rel)[-1];
    push @{ $by_base{$b} }, $rel;
}}, $root);

sub candidates {
    my $t = shift;
    my $b = (split /\//, $t)[-1];
    return grep { $_ eq $t || substr($_, -length($t) - 1) eq "/$t" } @{ $by_base{$b} // [] };
}

my %lines_cache;
sub file_lines {
    my $rel = shift;
    return $lines_cache{$rel} //= do {
        open my $fh, '<', "$root/$rel" or die "cannot read $rel: $!";
        my @l = <$fh>; close $fh;
        s/\r?\n\z// for @l;
        \@l;
    };
}

my ($checked, $failed, $oos) = (0, 0, 0);
sub fail {
    my ($page, $ln, $cite, $anchor, $held) = @_;
    $held = substr($held, 0, 70) . '...' if defined $held && length($held) > 73;
    printf "%s:%d: %s | %s | %s\n", $page, $ln, $cite, $anchor // '-', $held // '-';
    $failed++;
}

# ---- one paragraph: [ [lineno, text], ... ]
sub paragraph {
    my ($page, $para, $state) = @_;
    return unless @$para;
    my ($text, @map) = ('');
    for my $e (@$para) {                       # char offset -> source line
        push @map, [length($text), $e->[0]];
        $text .= $e->[1] . ' ';
    }
    my $lineat = sub { my $o = shift; my $l = $map[0][1]; for (@map) { $l = $_->[1] if $_->[0] <= $o } $l };

    # code spans (offset, end, content)
    my @spans;
    while ($text =~ /`([^`]+)`/g) { push @spans, [ $-[0], $+[0], $1 ] }
    # a copy with span interiors blanked, for sentence-boundary search
    my $mask = $text;
    substr($mask, $_->[0], $_->[1] - $_->[0]) = 'x' x ($_->[1] - $_->[0]) for @spans;

    # find cites: [start, end, file-token-or-undef, N, M]
    my @cites;
    while ($text =~ /(?<![A-Za-z0-9_.\/+-])($FILE):(\d+)(?:-(\d+))?(?!\d)/g) {
        push @cites, [ $-[0], $+[0], $1, $2, $3 // $2 ];
    }
    while ($text =~ /`:(\d+)(?:-(\d+))?`/g) {
        push @cites, [ $-[0], $+[0], undef, $1, $2 // $1 ];
    }
    while ($text =~ /\blines?\s+(\d+)(?:-(\d+))?\s+of\s+`($FILE)`/g) {
        push @cites, [ $-[0], $+[0], $3, $1, $2 // $1, 'prose' ];
    }
    # unknown-extension cite: name.xyz:N in prose (not 4.5:1, not an IP)
    while ($text =~ /(?<![A-Za-z0-9_.\/+-])([A-Za-z0-9_+-]+(?:\.[A-Za-z0-9_+-]+)*\.([A-Za-z][A-Za-z0-9_]*)):(\d+)/g) {
        my ($tok, $ext) = ($1, $2);
        next if $ext =~ /^$EXT$/;
        fail($page, $lineat->($-[0]), "$tok:$3", undef, "unknown extension .$ext (add it to the script's list or fix the text)");
    }
    # a comma list (file.cpp:12,34) would leave the second line unchecked
    while ($text =~ /(?<![A-Za-z0-9_.\/+-])(($FILE):\d+(?:-\d+)?),\d/g) {
        next if is_external($2);
        fail($page, $lineat->($-[0]), $1 . ",N", undef, "comma list of lines: write one cite per line");
    }
    # file mentions, for the bare :N rule
    (my $nolink = $text) =~ s{\]\([^)]*\)}{' ' x length($&)}ge;
    my @mentions;
    while ($nolink =~ /(?<![A-Za-z0-9_.\/+-])($FILE)(?::\d+)?(?![A-Za-z0-9_])/g) { push @mentions, [ $-[0], $1 ] }

    $state->{pending} = $mentions[-1][1] if @mentions;
    @cites = sort { $a->[0] <=> $b->[0] } @cites;
    my %seen;
    for my $c (@cites) {
        my ($s, $e, $tok, $n, $m, $kind) = @$c;
        next if $seen{"$s"}++;
        my $ln = $lineat->($s);
        my $show;
        if (!defined $tok) {                   # bare :N -> last file named before it
            for my $mm (reverse @mentions) { if ($mm->[0] < $s) { $tok = $mm->[1]; last } }
            $tok //= $state->{last};
            $show = ":$n" . ($m != $n ? "-$m" : '');
            if (!defined $tok) {
                $checked++; fail($page, $ln, $show, undef, "bare line cite with no file named earlier on the page");
                next;
            }
            $show .= " (" . $tok . ")";
        } else {
            $show = "$tok:$n" . ($m != $n ? "-$m" : '');
        }
        if (is_external($tok)) { $oos++; next }
        $checked++;

        # anchor: nearest identifier span before the cite, inside its sentence
        my $sent = 0;
        my $pre = substr($mask, 0, $s);
        while ($pre =~ /(?<!e\.g)(?<!i\.e)(?<!\bvs)[.!?]\s+(?=[A-Z`*(\[])/g) { $sent = $+[0] }
        my $anchor;
        for my $sp (reverse @spans) {
            next if $sp->[1] > $s || $sp->[0] < $sent;
            my $c2 = $sp->[2];
            next if $c2 =~ /:\d/ || $c2 =~ /^$FILE$/;
            if ($c2 =~ /^[A-Z][A-Z0-9]*-\d+[a-z]?$/) { $anchor = $c2; last }
            (my $id = $c2) =~ s/\(.*\)\s*$//s;
            if ($id =~ /^[A-Za-z_]\w*(?:(?:::|\.|->)[A-Za-z_]\w*)*$/) { ($anchor) = $id =~ /([A-Za-z_]\w*)$/; last }
        }

        my @cand = candidates($tok);
        if (!@cand) { fail($page, $ln, $show, $anchor, "file not in the repository and in no declared external set"); next }
        if (@cand > 1) { fail($page, $ln, $show, $anchor, "ambiguous, write a longer path: " . join(' ', sort @cand)); next }
        my $fl = file_lines($cand[0]);
        if ($m < $n || $n < 1 || $m > @$fl) { fail($page, $ln, $show, $anchor, "$cand[0] has " . scalar(@$fl) . " lines"); next }
        if (!defined $anchor) { fail($page, $ln, $show, undef, "unanchored: no backticked identifier before the cite in its sentence ($cand[0] line $n: " . $fl->[$n-1] . ")"); next }
        my $ok = 0;
        for my $i ($n .. $m) { if ($fl->[$i-1] =~ /(?<![A-Za-z0-9_])\Q$anchor\E(?![A-Za-z0-9_])/) { $ok = 1; last } }
        next if $ok;
        my $held = $fl->[$n-1]; $held =~ s/^\s+//;
        fail($page, $ln, "$cand[0]:$n" . ($m != $n ? "-$m" : ''), $anchor, $held);
    }
    $state->{last} = $state->{pending} if defined $state->{pending};
}

# ---- walk the pages
my @pages;
find({ no_chdir => 1, wanted => sub { push @pages, $_ if -f $_ && /\.(?:md|dot)$/ } }, $guide);
for my $f (sort @pages) {
    open my $fh, '<', $f or do { print STDERR "cannot read $f\n"; exit 2 };
    my $page = substr($f, length($root) + 1);
    my $dot = $f =~ /\.dot$/;
    my ($fence, @para, $no) = (0);
    my %st;
    my $flush = sub { paragraph($page, [@para], \%st); @para = () };
    while (my $l = <$fh>) {
        $no++; $l =~ s/\r?\n\z//;
        if (!$dot && $l =~ /^\s*```/) { $flush->(); $fence = !$fence; next }
        next if $fence;
        if ($dot) {
            if ($l =~ m{^\s*//}) { push @para, [$no, $l] } else { $flush->(); push @para, [$no, $l]; $flush->() }
            next;
        }
        if ($l =~ /^\s*$/ || $l =~ /^#/ || $l =~ /^\s*\|/ || $l =~ /^\s*(?:[-*+]|\d+\.)\s/) {
            $flush->();
            push @para, [$no, $l] unless $l =~ /^\s*$/;
            if ($l =~ /^#/ || $l =~ /^\s*\|/) { $flush->() }
            next;
        }
        push @para, [$no, $l];
    }
    $flush->();
    close $fh;
}
print "lint-devguide-cites: checked $checked cites, $failed failed, $oos out of scope (declared external)\n";
exit($failed ? 1 : 0);
