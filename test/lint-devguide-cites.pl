#!/usr/bin/env perl
# Developer-guide cite check: every file:line cite of a jnext source in the
# developer guide is verified against the code (GH #430).
#
# WHY. The guide cites code as a file, a colon and a line number. Nothing moved
# those cites when a cited file changed, so they drifted silently (one merge
# shifted 17 of chapter 7's). Owner decision (2026-11-02): keep the cites,
# anchor each to a named symbol, and gate them in `make docs-check`.
#
# SCOPE. Every *.md under src/doc/developer-guide/ and every diagrams/*.dot.
# Fenced code blocks (``` or ~~~, closed by the same character at least as long)
# are sample data of formats the guide describes and are not read for cites,
# EXCEPT as the excerpt a pointer introduces (see EXCERPTS). A fence still open
# at the end of a page FAILS, so a stray fence cannot hide the rest of a page.
#
# CITE FORMS. A "file token" is a path whose last component has an extension
# (.cpp .dot .spec .in ... any letter-start extension) or is `Makefile`, or any
# token with a directory part. The forms checked:
#   file:N   file:N-M        `file`:N        the colon may sit outside the span
#   `:N`  `:N-M`             bare: binds to the nearest file token EARLIER IN THE
#                            SAME PARAGRAPH (a whole table is one paragraph; a
#                            markdown link target is not a mention). It FAILS when
#                            no file is named there, when that file is an example
#                            program name or declared external, or when it does
#                            not resolve: name the file again.
#   line(s) N[-M] of|in `file`          `file` line(s) N   `file`, line N
#   `file` (line N)   file, line N      a "lines" phrase binds to the nearest file
#                                       token before it in its paragraph.
# A comma list of lines (file.cpp:12,34) FAILS: write one cite per line.
# Such a phrase with no file token earlier in its paragraph is not a cite
# (it is about something else, e.g. the lines of a listing being described).
#
# RESOLUTION, in this order, for a file token T:
#   1. T has no directory part and is one of the example-program names main.c
#      util.c util.h asmfn.asm main.bas equlabels.asm: a position in the program
#      the source map describes (main.c:17), not code. Out of scope.
#   2. T matches repo files (path equal or ending "/" + T, over `git ls-files
#      --cached --others --exclude-standard` when root has a .git, else a walk):
#      one match is checked; several FAIL, naming the candidates.
#   3. Nothing in the repo matches and T is one of the DECLARED EXTERNAL sets
#      below: out of scope. A name the repo does match is NEVER external, so a
#      later jnext file called support.cpp is checked, not exempted.
#   4. Otherwise FAIL ("not in the repository and in no declared external set").
# DECLARED EXTERNAL (nothing else is skipped): any *.vhd (the FPGA repository);
# z88dk (z88dk/..., zcc.c szx.c symtab1.c zxn_crt.asm.m4 zxn_crt.asm, src/{zcc,
# ticks,80cc,sccz80,z80asm}/...); sjasmplus (sjasm/..., sjio.cpp tables.cpp
# directives.cpp devices.cpp support.cpp preproc.cpp); Boriel (src/{zxbc,zxbasm}/...,
# keywords.py); NextBuild (nextbuild.py); NextZXOS (nexload.asm nexload2.asm
# readdir.asm esxapi.def).
#
# ANCHOR. The nearest backticked identifier BEFORE the cite in the same sentence,
# or, when the cite sits inside a parenthesis, the symbol in THAT parenthesis, or
# (a parenthesis with none of its own) the span ending right where it opens:
# `sym()` (file.cpp:12). No falling back to the sentence: a symbol elsewhere in it
# is not what the cite is about. (The prose forms "line N of `file`", "`file` (line N)"
# keep the sentence rule: their parenthesis holds the file, not a symbol.)
# (sentence = back to the previous ". " + capital/backtick/bracket/star, or the
# paragraph start: a blank line, a list bullet, a table, a heading, a fence; in
# .dot, consecutive // comment lines or one line). An identifier is a span like
# `name`, `Class::name`, `obj.member`, `name()` or `name(args)` (the last
# component is the anchor), a test ID like `QSRC-195`, or a double-quoted string
# literal such as `"unknown memory '"`, which anchors a cite when its text (between
# the quotes) appears verbatim, as a substring, on a cited line (an addition to the
# owner's four rules: it identifies a parser message by its own text). The anchor must appear
# as a whole word on the cited line; for a range N-M, on some line in N..M. There
# is NO +-1 tolerance. No identifier in the sentence: FAIL "unanchored".
#
# EXCERPTS. A "lines" phrase after a file token, in a paragraph that is directly
# followed by one or more fenced blocks, introduces an excerpt: the rows of those
# blocks (all of them, in order) must equal the cited lines in order, trailing
# blanks ignored, with the guide's two conventions: a run of X characters (the
# XXXXXXXX of a temp name) matches any word, and <TAB> is a literal TAB. A block
# whose every row starts with `N:`, in a paragraph saying "the number is the line",
# is an excerpt of the file named in that paragraph, row N being the text after the colon. Row counts must
# agree. A phrase with no fence after it is an ordinary cite and needs an anchor.
#
# A file token that does not resolve is still the nearest file of a bare :N, a
# "lines" phrase or an excerpt pointer, and those FAIL on it (a deleted or renamed
# file cannot be skipped or hand its cites to an earlier file). Exceptions: e.g./
# i.e., and a token that names a repo DIRECTORY (v2.4/sccz80-debug) is no file.
# A table row ends the sentence for the anchor search. The sjasmplus manual
# (docs/documentation.xml) is declared external.
#
# CANNOT CATCH, as examples and not as an exhaustive list: a cite whose sentence
# names a real identifier that is not the one the cite is about (the anchor
# proves the line holds that symbol, not that the sentence is right); a
# previous sentence's symbol anchoring a sentence that starts with a digit, a
# lowercase letter or a quote, or follows ; or :; in a table row, a symbol of
# another cell of the same row; a range whose symbol is anywhere in it rather than first; a cite
# split over lines inside one token; indented (non-fenced) code; a "line N"
# phrase in a paragraph that names a file for another reason.
#
# Env: JNEXT_CITES_DUMP=FILE appends every checked repo cite (merge-time map check, see
# the developer guide 4.5); JNEXT_CITES_FULL=1 untruncates the messages.
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

# a file token: optional directories, a last component with a letter-start
# extension (or Makefile), or any token with a directory part
my $FT = qr/(?:[\w.+-]+\/+)*[\w+-](?:[\w.+-]*[\w+-])?/;
sub looks_like_file {
    my $t = shift;
    return 1 if $t =~ /\/\w/ && $t !~ /^\d/;
    return 1 if $t =~ /\.[A-Za-z][A-Za-z0-9]{0,5}$/;
    return $t =~ /^(?:Makefile|Dockerfile)$/ ? 1 : 0;
}

my @EXTERNAL = (
    qr/\.vhd$/,
    qr/^z88dk\//, qr/^(?:zcc|szx|symtab1)\.c$/, qr/^(?:.*\/)?zxn_crt\.asm(?:\.m4)?$/,
    qr/^src\/(?:zcc|ticks|80cc|sccz80|z80asm)\//,
    qr/^(?:sjasm\/)?(?:sjio|tables|directives|devices|support|preproc)\.cpp$/, qr/^sjasm\//,
    qr/^src\/(?:zxbc|zxbasm)\//, qr/^(?:.*\/)?keywords\.py$/,
    qr/^(?:.*\/)?nextbuild\.py$/,
    qr/^(?:nexload2?\.asm|readdir\.asm|esxapi\.def)$/,
    qr/^docs\/documentation\.xml$/,
);
my %EXAMPLE = map { $_ => 1 } qw(main.c util.c util.h asmfn.asm main.bas equlabels.asm);
sub is_external { my $t = shift; for (@EXTERNAL) { return 1 if $t =~ $_ } 0 }

# ---- repo index: basename -> paths
my %by_base;
sub add_path { my $rel = shift; push @{ $by_base{ (split /\//, $rel)[-1] } }, $rel }
if (-e "$root/.git") {
    open my $gl, '-|', 'git', '-C', $root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard'
        or do { print STDERR "lint-devguide-cites: cannot run git\n"; exit 2 };
    local $/ = "\0";
    while (my $p = <$gl>) { chomp $p; add_path($p) if -f "$root/$p" }
    close $gl;
} else {
    find({ no_chdir => 1, wanted => sub {
        if (-d $_) {
            my $b = (split /\//, $_)[-1];
            $File::Find::prune = 1 if $b eq '.git' || $b =~ /^build/ || $b eq 'third_party';
            return;
        }
        return if -l $_;
        add_path(substr($_, length($root) + 1));
    }}, $root);
}

my %DIRS;
for my $list (values %by_base) {
    for my $f (@$list) { my @c = split /\//, $f; pop @c; my $d = ''; for my $c (@c) { $d = $d eq '' ? $c : "$d/$c"; $DIRS{$d} = 1 } }
}
sub is_dir {
    my $t = shift; $t =~ s/\/+$//; $t =~ s/\/+/\//g;
    return 1 if $DIRS{$t};
    for my $d (keys %DIRS) { return 1 if substr($d, -length($t) - 1) eq "/$t" }
    return 0;
}

sub candidates {
    my $t = shift;
    $t =~ s/\/+/\//g;
    my $b = (split /\//, $t)[-1];
    return grep { $_ eq $t || substr($_, -length($t) - 1) eq "/$t" } @{ $by_base{$b} // [] };
}

# -> ('example') | ('repo', rel) | ('ambig', @rel) | ('external') | ('none')
sub classify {
    my $t = shift;
    return ('example') if $t !~ /\// && $EXAMPLE{$t};
    my @c = candidates($t);
    return ('repo', $c[0]) if @c == 1;
    return ('ambig', sort @c) if @c > 1;
    return is_external($t) ? ('external') : ('none');
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
    $held = substr($held, 0, 70) . "..." if defined $held && length($held) > 73 && !$ENV{JNEXT_CITES_FULL};
    printf "%s:%d: %s | %s | %s\n", $page, $ln, $cite, $anchor // '-', $held // '-';
    $failed++;
}

# ---- the excerpt a "lines" phrase introduces
sub excerpt {
    my ($page, $ln, $rel, $ranges, $fences) = @_;
    my $fl = file_lines($rel);
    my (@want, @got);
    for my $r (@$ranges) {
        my ($n, $m) = @$r;
        if ($n < 1 || $m < $n || $m > @$fl) { fail($page, $ln, "$rel:$n-$m", undef, "$rel has " . scalar(@$fl) . " lines"); return }
        push @want, @$fl[$n - 1 .. $m - 1];
    }
    push @got, @{ $_->{rows} } for @$fences;
    s/\s+$// for @want, @got;
    $checked++;
    if (@want != @got) {
        fail($page, $ln, "$rel excerpt", undef, "cites " . scalar(@want) . " lines, the excerpt shows " . scalar(@got));
        return;
    }
    for my $i (0 .. $#want) {
        (my $re = quotemeta($got[$i])) =~ s/(?:\\?X){4,}/\\w+/g;
        $re =~ s/\\<TAB\\>/\\t/g;
        next if $want[$i] =~ /^$re$/;
        fail($page, $ln, "$rel excerpt row " . ($i + 1), undef, "the excerpt has: $got[$i] | the file has: $want[$i]");
        return;
    }
}

sub excerpt_numbered {
    my ($page, $ln, $rel, $fence) = @_;
    my $fl = file_lines($rel);
    $checked++;
    my $i = 0;
    for my $row (@{ $fence->{rows} }) {
        $i++;
        $row =~ /^(\d+):(.*)$/ or next;
        my ($n, $txt) = ($1, $2);
        (my $re = quotemeta($txt)) =~ s/\\<TAB\\>/\\t/g;
        my $got = $n >= 1 && $n <= @$fl ? $fl->[$n - 1] : '';
        $got =~ s/\s+$//;
        next if $got =~ /^$re\s*$/;
        fail($page, $fence->{start} + $i, "$rel excerpt line $n", undef, "the excerpt has: $txt | the file has: $got");
    }
}

# a code span that can anchor a cite: an identifier or a test ID, not a file or a cite
sub is_ident_span {
    my $c = shift;
    return 0 if $c =~ /:\d/ || ($c =~ /^$FT$/ && looks_like_file($c) && (classify($c))[0] ne 'none');
    return 1 if $c =~ /^[A-Z][A-Z0-9]*-\d+[a-z]?$/ || $c =~ /^".+"$/s;
    (my $id = $c) =~ s/\(.*\)\s*$//s;
    return $id =~ /^[A-Za-z_]\w*(?:(?:::|\.|->)[A-Za-z_]\w*)*$/ ? 1 : 0;
}

# ---- one paragraph: ($page, [ [lineno, text], ... ], [following fence blocks], $state)
sub paragraph {
    my ($page, $para, $fences, $state) = @_;
    return unless @$para;
    my ($text, @map, @rowat) = ('');
    for my $e (@$para) {                       # char offset -> source line
        push @map, [length($text), $e->[0]];
        push @rowat, length($text) if $e->[1] =~ /^\s*\|/;
        $text .= $e->[1] . ' ';
    }
    my $lineat = sub { my $o = shift; my $l = $map[0][1]; for (@map) { $l = $_->[1] if $_->[0] <= $o } $l };

    my @spans;
    while ($text =~ /`([^`]+)`/g) { push @spans, [ $-[0], $+[0], $1 ] }
    my $mask = $text;                          # span interiors blanked, for sentence breaks
    substr($mask, $_->[0], $_->[1] - $_->[0]) = 'x' x ($_->[1] - $_->[0]) for @spans;
    (my $nolink = $text) =~ s{\]\([^)]*\)}{' ' x length($&)}ge;

    # file mentions in this paragraph, for bare :N and "lines" phrases
    my @mentions;
    while ($nolink =~ /(?<![\w.\/+-])($FT)(?::\d+(?:-\d+)?)?(?![\w])/g) {
        my ($tok, $at) = ($1, $-[0]);
        next unless looks_like_file($tok);
        next if $tok =~ /^(?:e\.g|i\.e)$/ || is_dir($tok);
        push @mentions, [ $at, $tok ];
    }
    my $last_before = sub {
        my $s = shift; my $t;
        for my $mm (@mentions) { $t = $mm->[1] if $mm->[0] < $s }
        $t;
    };

    # cites: [start, end, token|undef, N, M, kind]
    my @cites;
    while ($text =~ /(?<![\w.\/+-])($FT)(`?):(\d+)(?:-(\d+))?(?!\d)/g) {
        my ($tok, $bt, $n, $m, $s, $e) = ($1, $2, $3, $4, $-[0], $+[0]);
        next unless looks_like_file($tok);
        if ($text =~ /\G,\d/) {
            fail($page, $lineat->($s), "$tok:$n,N", undef, "comma list of lines: write one cite per line");
        }
        push @cites, [ $s, $e, $tok, $n, $m // $n, 'colon' ];
    }
    while ($text =~ /`:(\d+)(?:-(\d+))?(,[\d,-]*)?`/g) {
        fail($page, $lineat->($-[0]), ":$1$3", undef, "comma list of lines: write one cite per line") if defined $3;
        push @cites, [ $-[0], $+[0], undef, $1, $2 // $1, 'bare' ];
    }
    # "lines" phrases, bound to the nearest file token before them
    my $RL = qr/\d+(?:-\d+)?/;
    my $RLIST = qr/$RL(?:\s*(?:,\s*and|,|and)\s*$RL)*/;
    my @phrases;
    while ($nolink =~ /\b(?:(?:at|on|around|near)\s+)?(?:file\s+)?lines?\s+($RLIST)(?!\s*[:\d])(?:\s+(?:of|in)\s+`($FT)`)?/g) {
        push @phrases, [ $-[0], $+[0], $1, $2 ];
    }
    my %phrase_done;
    for my $p (@phrases) {
        my ($s, $e, $list, $tok) = @$p;
        $tok //= $last_before->($s);
        next unless defined $tok;
        my ($k, @r) = classify($tok);
        my @ranges = map { /^(\d+)(?:-(\d+))?$/ ? [ $1, $2 // $1 ] : () } split /\s*(?:,\s*and|,|and)\s*/, $list;
        if ($k eq 'repo' && @$fences) {         # introduces an excerpt
            excerpt($page, $lineat->($s), $r[0], \@ranges, $fences);
            $state->{excerpt_done} = 1;
            next;
        }
        for my $rg (@ranges) { push @cites, [ $s, $e, $tok, $rg->[0], $rg->[1], 'prose' ] }
    }
    if (@$fences && !$state->{excerpt_done} && $text =~ /the number is the line/) {
        my $f = $fences->[0];
        if (@{ $f->{rows} } && !grep { $_ !~ /^\d+:/ } @{ $f->{rows} }) {
            my $tok = $last_before->(length $text);
            my ($k, @r) = $tok ? classify($tok) : ('none');
            if ($k eq 'repo') { excerpt_numbered($page, $lineat->(0), $r[0], $f) }
            elsif ($k eq 'ambig') { fail($page, $f->{start}, "numbered excerpt of $tok", undef, "ambiguous, write a longer path: " . join(' ', @r)) }
            elsif ($k eq 'none' || !$tok) { fail($page, $f->{start}, "numbered excerpt", undef, "no file named in the paragraph above it") }
        }
    }
    delete $state->{excerpt_done};

    @cites = sort { $a->[0] <=> $b->[0] } @cites;
    my %seen;
    for my $c (@cites) {
        my ($s, $e, $tok, $n, $m, $kind) = @$c;
        next if $seen{"$s:$n"}++;
        my $ln = $lineat->($s);
        my $show;
        if ($kind eq 'bare') {
            $show = ":$n" . ($m != $n ? "-$m" : '');
            $tok = $last_before->($s);
            if (!defined $tok) {
                $checked++; fail($page, $ln, $show, undef, "bare line cite with no file named earlier in its paragraph: name the file");
                next;
            }
            my ($k) = classify($tok);
            if ($k ne 'repo') {
                $checked++;
                fail($page, $ln, "$show ($tok)", undef, "bare line cite bound to " . ($k eq 'example' ? 'an example-program name' : $k eq 'external' ? 'an external file' : $k eq 'ambig' ? 'an ambiguous name' : 'an unresolvable name') . ": name the file");
                next;
            }
        } else {
            $show = "$tok:$n" . ($m != $n ? "-$m" : '');
        }
        my ($k, @r) = classify($tok);
        if ($k eq 'example' || $k eq 'external') { $oos++; next }
        $checked++;

        my $sent = 0;
        my $pre = substr($mask, 0, $s);
        while ($pre =~ /(?<!e\.g)(?<!i\.e)(?<!\bvs)[.!?]\s+(?=[A-Z`*(\[])/g) { $sent = $+[0] }
        for my $ra (@rowat) { $sent = $ra if $ra <= $s && $ra > $sent }
        # a cite inside a parenthesis is attached to the symbol INSIDE that parenthesis
        my @open;
        for my $i ($kind eq 'prose' ? () : ($sent .. $s - 1)) {
            my $ch = substr($mask, $i, 1);
            if ($ch eq '(') { push @open, $i } elsif ($ch eq ')') { pop @open }
        }
        my $lo = @open ? $open[-1] : $sent;
        my $adj;      # a parenthesis holding no symbol of its own belongs to the span just before it
        if (@open && !grep { $_->[0] >= $open[-1] && $_->[1] <= $s && is_ident_span($_->[2]) } @spans) {
            for my $sp (reverse @spans) {
                next if $sp->[1] > $open[-1];
                $adj = $sp if substr($mask, $sp->[1], $open[-1] - $sp->[1]) =~ /^[\s*_]*$/;
                last;
            }
        }
        my ($anchor, $loose, $lit);
        for my $sp (reverse @spans) {
            next if $adj && $sp != $adj;
            next if !$adj && ($sp->[1] > $s || $sp->[0] < $lo);
            my $c2 = $sp->[2];
            next if $c2 =~ /:\d/ || ($c2 =~ /^$FT$/ && looks_like_file($c2) && (classify($c2))[0] ne 'none');
            if ($c2 =~ /^[A-Z][A-Z0-9]*-\d+[a-z]?$/) { $anchor = $c2; last }
            if ($c2 =~ /^"(.+)"$/s) { $anchor = $1; $lit = 1; last }
            (my $id = $c2) =~ s/\(.*\)\s*$//s;
            if ($id =~ /^[A-Za-z_]\w*(?:(?:::|\.|->)[A-Za-z_]\w*)*$/) { ($anchor) = $id =~ /([A-Za-z_]\w*)$/; last }
        }

        if ($k eq 'none') { fail($page, $ln, $show, $anchor, "file not in the repository and in no declared external set"); next }
        if ($k eq 'ambig') { fail($page, $ln, $show, $anchor, "ambiguous, write a longer path: " . join(' ', @r)); next }
        my $rel = $r[0];
        if ($ENV{JNEXT_CITES_DUMP}) {      # merge-time map check: every repo cite, as page:line and file:N[-M]
            open my $dh, '>>', $ENV{JNEXT_CITES_DUMP} or die "cannot write $ENV{JNEXT_CITES_DUMP}: $!";
            print $dh "OK\t$page:$ln\t$rel:$n" . ($m != $n ? "-$m" : '') . "\n"; close $dh;
        }
        my $fl = file_lines($rel);
        if ($m < $n || $n < 1 || $m > @$fl) { fail($page, $ln, $show, $anchor, "$rel has " . scalar(@$fl) . " lines"); next }
        if (!defined $anchor && @open) {      # for the message: what the loose rule would have used
            for my $sp (reverse @spans) {
                next if $sp->[1] > $s || $sp->[0] < $sent;
                (my $id = $sp->[2]) =~ s/\(.*\)\s*$//s;
                if ($id =~ /^[A-Za-z_]\w*(?:(?:::|\.|->)[A-Za-z_]\w*)*$/ && $sp->[2] !~ /:\d/) { $loose = $sp->[2]; last }
            }
        }
        if (!defined $anchor) { fail($page, $ln, $show, undef, "unanchored: no backticked identifier " . (@open ? "in the cite's parenthesis or right before it" : "before the cite in its sentence") . " ($rel line $n: " . $fl->[$n-1] . ")" . (defined $loose ? " [sentence symbol: `$loose`]" : "")); next }
        my $ok = 0;
        for my $i ($n .. $m) {
            if ($lit ? index($fl->[$i-1], $anchor) >= 0 : $fl->[$i-1] =~ /(?<![A-Za-z0-9_])\Q$anchor\E(?![A-Za-z0-9_])/) { $ok = 1; last }
        }
        next if $ok;
        my $held = $fl->[$n-1]; $held =~ s/^\s+//;
        fail($page, $ln, "$rel:$n" . ($m != $n ? "-$m" : ''), $anchor, $held);
    }
}

# ---- split a page into paragraph and fence blocks
sub blocks_md {
    my ($f, $page) = @_;
    open my $fh, '<', $f or do { print STDERR "cannot read $f\n"; exit 2 };
    my (@blocks, @para, $istable);
    my ($fence, $fch, $flen, $fstart, @rows) = (0);
    my $no = 0;
    my $flush = sub { push @blocks, { t => 'p', lines => [@para] } if @para; @para = (); $istable = 0 };
    while (my $l = <$fh>) {
        $no++; $l =~ s/\r?\n\z//;
        if ($fence) {
            if ($l =~ /^\s*(\Q$fch\E{$flen,})\s*$/) {
                push @blocks, { t => 'f', rows => [@rows], start => $fstart };
                $fence = 0; @rows = ();
            } else { push @rows, $l }
            next;
        }
        if ($l =~ /^\s*(`{3,}|~{3,})/) {
            $flush->(); $fence = 1; $fch = substr($1, 0, 1); $flen = length $1; $fstart = $no; next;
        }
        if ($l =~ /^\s*\|/) {
            if (!$istable) { $flush->(); $istable = 1 }
            push @para, [$no, $l]; next;
        }
        if ($istable) { $flush->() }
        if ($l =~ /^\s*$/ || $l =~ /^#/ || $l =~ /^\s*(?:[-*+]|\d+\.)\s/) {
            $flush->();
            push @para, [$no, $l] unless $l =~ /^\s*$/;
            $flush->() if $l =~ /^#/;
            next;
        }
        push @para, [$no, $l];
    }
    $flush->();
    close $fh;
    fail($page, $fstart, "fence", undef, "the fence opened here is never closed (a closing fence is the same character, at least as long)") if $fence;
    return \@blocks;
}

sub blocks_dot {
    my ($f) = @_;
    open my $fh, '<', $f or do { print STDERR "cannot read $f\n"; exit 2 };
    my (@blocks, @para); my $no = 0;
    my $flush = sub { push @blocks, { t => 'p', lines => [@para] } if @para; @para = () };
    while (my $l = <$fh>) {
        $no++; $l =~ s/\r?\n\z//;
        if ($l =~ m{^\s*//}) { push @para, [$no, $l] }
        else { $flush->(); push @para, [$no, $l]; $flush->() }
    }
    $flush->();
    close $fh;
    return \@blocks;
}

my @pages;
find({ no_chdir => 1, wanted => sub { push @pages, $_ if -f $_ && /\.(?:md|dot)$/ } }, $guide);
for my $f (sort @pages) {
    my $page = substr($f, length($root) + 1);
    my $blocks = $f =~ /\.dot$/ ? blocks_dot($f) : blocks_md($f, $page);
    my %st;
    for my $i (0 .. $#$blocks) {
        my $b = $blocks->[$i];
        next unless $b->{t} eq 'p';
        my @fences;
        for (my $j = $i + 1; $j <= $#$blocks && $blocks->[$j]{t} eq 'f'; $j++) { push @fences, $blocks->[$j] }
        paragraph($page, $b->{lines}, \@fences, \%st);
    }
}
print "lint-devguide-cites: checked $checked cites, $failed failed, $oos out of scope (declared external)\n";
exit($failed ? 1 : 0);
