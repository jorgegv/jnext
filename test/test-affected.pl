#!/usr/bin/env perl
# GH #547: the plan behind `make test-affected`. Given the files a branch changed, print which
# unit suites, regression rows and make targets must run, which of the platform runs
# (unit-test-sdl / -win / -wasm) the change needs, and WHEN IT CANNOT SAY: then the plan is FULL
# (`make unit-test`). It never decides "nothing" for a file it does not know.
#
# The map is DERIVED from the build tree, not written by hand (C/C++):
#   * every object has a compiler depfile, `<obj>.o.d` (the source and every header it read);
#   * every test executable has `<dir>/CMakeFiles/<suite>.dir/link.d` (the inputs its link
#     opened) and, because test/CMakeLists.txt links with -Wl,-Map, `<dir>/<suite>.map`, whose
#     "Archive member included" section names the exact archive members the linker pulled;
#   * an archive's own `link.txt` (`ar qc lib.a objs...`) says which object is which member.
#   files(suite) = union of the depfiles of the suite's own objects and of the members it pulled;
#   a changed file selects every suite whose files() hold it, headers and sources alike,
#   transitively by construction. A suite with no map is taken with its archives' WHOLE
#   membership (more suites, never fewer).
#
# What no depfile can see is classified by test/test-affected.conf (glob -> action, read in
# order, first match wins). A file that is in no depfile and matches no glob is UNMAPPED, and one
# unmapped file makes the plan FULL, named. `--check` lists every tracked file that is neither
# (the gate that makes a new kind of file get classified in the change that adds it).
#
# Usage: test/test-affected.pl [options]
#   --root DIR       source root (default: this script's parent)
#   --build DIR      build tree (default: ROOT/build)
#   --manifest FILE  unit-test manifest (default: ROOT/test/unit-tests.conf)
#   --conf FILE      classification (default: ROOT/test/test-affected.conf)
#   --base REF       git base (default: merge-base of HEAD and main)
#   --files FILE     changed files, one per line (instead of git)
#   --smoke a,b,c    suites that always run (each must be declared in the manifest, else exit 2)
#   --plan FILE      write the machine-readable plan here (key=value lines)
#   --explain-miss a,b  print `TEST-AFFECTED MISS` for each named suite this plan would NOT have selected
#                    (stateless: run-unit-tests.sh asks it after a full run fails)
#   --check          list tracked files that are unmapped; exit 1 if any (--all: list every one)
#   --tracked FILE   the tracked-file list for --check (default: git ls-files)
# Exit: 0 ok, 1 --check found unmapped files, 2 refusal (bad input).
use strict;
use warnings;
use File::Basename qw(dirname basename);
use File::Spec;
use Cwd qw(abs_path);
use File::Temp;

my $here = abs_path(dirname($0));
my %o = (root => abs_path("$here/.."));
my @args = @ARGV;
while (@args) {
    my $a = shift @args;
    if    ($a eq '--check') { $o{check} = 1 }
    elsif ($a eq '--all')   { $o{all} = 1 }
    elsif ($a =~ /^--(root|build|manifest|conf|base|files|smoke|plan|tracked|explain-miss)$/) {
        defined(my $v = shift @args) or die "test-affected: $a needs a value\n";
        $o{$1} = $v;
    } else { die "test-affected: unknown argument '$a'\n" }
}
my $root = abs_path($o{root}) or die "test-affected: no such root $o{root}\n";
my $build = $o{build} // "$root/build";
$build = abs_path($build) // $build;
my $manifest = $o{manifest} // "$root/test/unit-tests.conf";
my $confile = $o{conf} // "$root/test/test-affected.conf";

sub refuse { print STDERR "test-affected: REFUSED: @_\n"; exit 2 }

# ---------------------------------------------------------------- manifest
my (@declared, %args_of);
{
    open(my $fh, '<', $manifest) or refuse("cannot read manifest $manifest");
    while (<$fh>) {
        next if /^\s*#/;
        s/#.*//;
        next unless /^\s*\??(\S+)\s+(\d+)\s*(.*?)\s*$/;
        push @declared, $1;
        $args_of{$1} = $3;
    }
}
my %is_declared = map { $_ => 1 } @declared;

# ---------------------------------------------------------------- build tree
open(my $cf, '<', "$build/CMakeCache.txt") or refuse("$build is not a configured build tree (no CMakeCache.txt); run `make unit-test-build`");
my $home = '';
while (<$cf>) { $home = $1 if /^CMAKE_HOME_DIRECTORY:[A-Z]+=(.*)$/ }
close $cf;
$home = abs_path($home) // $home;

sub norm {   # lexical canonical path: no //, no /./, no dir/..
    my @o;
    for my $c (split m{/+}, shift) { next if $c eq '.'; if ($c eq '..' && @o && $o[-1] ne '..') { pop @o } else { push @o, $c } }
    return '/' . join('/', grep { length } @o);
}

sub rel {   # path inside the source root -> root-relative, else undef
    my $p = shift;
    $p = norm($p);
    for my $r ($root, $home) {
        next unless length $r;
        return substr($p, length($r) + 1) if index($p, "$r/") == 0;
    }
    return undef;
}

sub parse_depfile {   # make-style "target: \ f1 f2 ..." -> list of files
    my $f = shift;
    open(my $fh, '<', $f) or return ();
    local $/; my $t = <$fh>; close $fh;
    $t =~ s/\\\n/ /g;
    $t =~ s/^[^:\n]*:\s*//;           # target
    $t =~ s/\\ /\x01/g;               # escaped spaces
    return map { s/\x01/ /gr } grep { length } split /\s+/, $t;
}

my @files = `find '$build' -type f \\( -name '*.o.d' -o -name link.txt -o -name link.d \\) 2>/dev/null`;
chomp @files;

my (%members, %obj_deps);   # members{archive}{basename} -> [objects]
for my $lt (grep { m{/link\.txt$} } @files) {
    open(my $fh, '<', $lt) or next;
    my $line = <$fh> // ''; close $fh;
    next unless $line =~ m{\bar\s+\S+\s+(\S+\.a)\s+(.*?)\s*$};
    my ($arch, $objs) = ($1, $2);
    my $bin = dirname(dirname(dirname($lt)));       # <bindir>/CMakeFiles/<t>.dir/link.txt
    my $aabs = norm("$bin/$arch");
    for my $ob (split /\s+/, $objs) {
        my $oabs = norm("$bin/$ob");
        push @{ $members{$aabs}{ basename($ob) } }, $oabs;
    }
}

# object -> repo-relative files it read
my %dep_cache;
sub obj_files {
    my $obj = shift;
    return $dep_cache{$obj} //= do {
        my %s;
        # ccache with a base_dir writes the depfile's paths relative to the compile's working
        # directory, which is the target's build directory (<bin>/CMakeFiles/<t>.dir/..).
        my ($bin) = $obj =~ m{^(.*?)/CMakeFiles/};
        for my $f (parse_depfile("$obj.d")) {
            $f = "$bin/$f" if $f !~ m{^/} && defined $bin;
            my $r = rel($f); $s{$r} = 1 if defined $r;
        }
        \%s;
    };
}

# per suite file sets
my (%suite_files, %nomap, %suite_present, %suite_defs, %suite_rootdef, %suite_text_cache);
my %any_mapped;   # file -> 1 : read by some object in the tree
for my $d (grep { m{/link\.d$} } @files) {
    my $tdir = dirname($d);
    next unless $tdir =~ m{/CMakeFiles/([^/]+)\.dir$};
    my $suite = $1;
    my $bin = dirname(dirname($tdir));
    my @in = parse_depfile($d);
    my (%objs, @archs);
    for my $i (@in) {
        my $abs = $i =~ m{^/} ? $i : "$bin/$i";
        $abs = norm($abs);
        if    ($abs =~ /\.o$/) { $objs{$abs} = 1 if index($abs, $build) == 0 }
        elsif ($abs =~ /\.a$/) { push @archs, $abs if $members{$abs} }
    }
    my $map = "$bin/$suite.map";
    my %pulled; my $have_map = 0;
    if (-r $map) {
        open(my $mf, '<', $map);
        my $in = 0;
        while (<$mf>) {
            if (/^Archive member included/) { $in = 1; $have_map = 1; next }
            last if $in && /^(Discarded input sections|Memory Configuration|Linker script)/;
            next unless $in;
            if (m{^(\S+\.a)\((.+)\)\s*$}) {
                my $a = norm("$bin/$1");
                $pulled{$a}{$2} = 1;
            }
        }
        close $mf;
    }
    $nomap{$suite} = 1 unless $have_map;
    for my $a (@archs) {
        for my $m (keys %{ $members{$a} }) {
            next if $have_map && !$pulled{$a}{$m};
            $objs{$_} = 1 for @{ $members{$a}{$m} };
        }
    }
    my %fs;
    for my $ob (keys %objs) { %fs = (%fs, %{ obj_files($ob) }) }
    $suite_files{$suite} = \%fs;
    $suite_present{$suite} = 1;
    # Files the suite reads AT RUN TIME through a path baked in as a compile definition
    # (-DJNEXT_MAIN_SRC="<root>/src/main.cpp", a fixture directory, ...): no depfile sees them.
    if (open(my $fm, '<', "$tdir/flags.make")) {
        while (<$fm>) {
            next unless /^(?:CXX|C)_DEFINES\s*=\s*(.*)$/;
            my $defs = $1;
            while ($defs =~ /-D(\w+)=\\?"([^"\\]*)\\?"/g) {
                my ($name, $v) = ($1, $2);
                # Qt's QFINDTESTDATA roots: skipped unless the suite's own sources call QFINDTESTDATA
                next if $name =~ /^QT_TESTCASE_/ && suite_text($suite) !~ /QFINDTESTDATA|qFindTestData/;

                next unless $v =~ m{^/};
                $v = norm($v);
                if ($v eq $root || $v eq $home) { $suite_rootdef{$suite} = 1; next }
                my $r = rel($v);
                push @{ $suite_defs{$suite} }, $r if defined $r && length $r;
            }
        }
        close $fm;
    }
}
# files read by any object at all (the application binary, tools): the "mapped" set
for my $f (grep { m{\.o\.d$} } @files) {
    (my $ob = $f) =~ s/\.d$//;
    $any_mapped{$_} = 1 for keys %{ obj_files($ob) };
}
my %file_suites;
for my $s (keys %suite_files) { push @{ $file_suites{$_} }, $s for keys %{ $suite_files{$s} } }

# ---------------------------------------------------------------- classification conf
my @rules;
{
    open(my $fh, '<', $confile) or refuse("cannot read $confile");
    while (<$fh>) {
        next if /^\s*(#|$)/;
        chomp;
        my ($glob, $action) = split /\s+/, $_, 2;
        refuse("$confile:$.: expected '<glob> <action>'") unless defined $action;
        $action =~ s/\s+$//;
        my $re = '';
        for my $tok (split /(\*\*\/|\*\*|\*|\?)/, $glob) {
            if    ($tok eq '**/') { $re .= '(?:.*/)?' }
            elsif ($tok eq '**')  { $re .= '.*' }
            elsif ($tok eq '*')   { $re .= '[^/]*' }
            elsif ($tok eq '?')   { $re .= '[^/]' }
            else                  { $re .= quotemeta $tok }
        }
        push @rules, [ qr/^$re$/, $action, $glob ];
    }
}
sub classify { my $f = shift; for my $r (@rules) { return $r->[1] if $f =~ $r->[0] } return undef }

# ---------------------------------------------------------------- --check
my ($GIT_RC, $git_bad) = (0, 0);   # exit status of the last git(); gitc() latches a failure into the plan (FULL)
sub gitc { my @l = git(@_); $git_bad = 1 if $GIT_RC != 0; return @l }
sub git {   # stderr is silenced: a root that is not a git checkout (a fixture) just yields no lines
    my @c = ('git', '-C', $root, @_);
    open(my $olderr, '>&', \*STDERR); open(STDERR, '>', '/dev/null');
    my $ok = open(my $p, '-|', @c);
    my @l = $ok ? <$p> : (); close $p if $ok;
    $GIT_RC = $ok ? $? : -1;
    open(STDERR, '>&', $olderr);
    chomp @l; return @l;
}

if ($o{check}) {
    my @tracked;
    if ($o{tracked}) { open(my $t, '<', $o{tracked}) or refuse("cannot read $o{tracked}"); chomp(@tracked = <$t>) }
    else { @tracked = git('ls-files') }
    my @un = grep { !$any_mapped{$_} && !defined classify($_) } @tracked;
    if (@un) {
        print "test-affected-check: ", scalar(@un), " tracked file(s) are in no depfile of $build and match no glob of $confile:\n";
        my $cap = $o{all} ? $#un : ($#un < 49 ? $#un : 49);
        print "  $_\n" for @un[0 .. $cap];
        print "  ... and ", scalar(@un) - 50, " more (--all lists them)\n" if @un > 50 && !$o{all};
        print "Classify each in $confile in the change that adds it.\n";
        exit 1;
    }
    printf "test-affected-check: all %d tracked files are mapped or classified\n", scalar @tracked;
    exit 0;
}

# ---------------------------------------------------------------- changed files
my (@changed, $base, $head);
$head = (git('rev-parse', 'HEAD'))[0] // '';
if ($o{files}) {
    open(my $t, '<', $o{files}) or refuse("cannot read $o{files}");
    chomp(@changed = grep { /\S/ } <$t>);
    $base = $o{base} // '(files list)';
} else {
    $base = $o{base} // (git('merge-base', 'HEAD', 'main'))[0] // (git('merge-base', 'HEAD', 'origin/main'))[0];
    refuse("no base: pass --base REF (no `main` or `origin/main` to take a merge-base with)") unless $base;
    my %s;
    $s{$_} = 1 for gitc('diff', '--name-only', '--no-renames', $base);
    $s{$_} = 1 for gitc('diff', '--name-only', '--no-renames', $base, 'HEAD');   # a gitlink with no checkout shows only here
    $s{$_} = 1 for gitc('ls-files', '--others', '--exclude-standard');
    @changed = sort keys %s;
}

# smoke suites
if (!defined $o{smoke} && defined $o{'explain-miss'} && open(my $mk, '<', "$root/Makefile")) {   # the set `make test-affected` always runs
    while (<$mk>) { if (/^SMOKE_SUITES\s*:=\s*(.*?)\s*$/) { ($o{smoke} = $1) =~ s/\s+/,/g; last } }
}
my @smoke = grep { length } split /,/, ($o{smoke} // '');
for my $s (@smoke) { refuse("smoke suite '$s' is not declared in $manifest") unless $is_declared{$s} }

# ---------------------------------------------------------------- plan
my (%suites, %rows, %makes, %notes, @unmapped, $docs, $full, %full_why);
my %heavy;   # reported, not run
$docs = 0; $full = 0;

sub rows_declared {
    my %r;
    for my $c ("$root/test/00regression/functional_tests.conf", "$root/test/00regression/regression_tests.conf") {
        open(my $fh, '<', $c) or next;
        while (<$fh>) { next if /^\s*#/; $r{$1} = 1 if /^\s*(\S+)/ }
    }
    return \%r;
}
my $declared_rows = rows_declared();

sub suite_text {   # the sources of the suite's own objects plus its manifest arguments
    my $s = shift;
    return $suite_text_cache{$s} //= do {
        my $text = $args_of{$s} // '';
        for my $f (sort keys %{ $suite_files{$s} }) {
            next unless $f =~ m{^(test|src)/.*\.(cpp|h|cc|c|hpp)$};
            open(my $fh, '<', "$root/$f") or next;
            local $/; my $src = <$fh>; close $fh;
            # Comments name files without reading them (a header's "see doc/man/jnext.1.md").
            $src =~ s{/\*.*?\*/}{}gs;
            $src =~ s{(^|\s)//[^\n]*}{$1}g;
            $text .= $src;
        }
        $text;
    };
}

# Suites that read changed file $f at run time, whatever its class: a compile-definition path
# equal to it or a directory holding it; a literal mention of its path in the suite's sources
# (every suite for a file no object compiles, only the suites given the whole source root
# (JNEXT_SRC_DIR) for one that is compiled).
sub edge_suites {
    my ($f, $mapped) = @_;
    my %r;
    for my $s (keys %suite_defs) {
        for my $dp (@{ $suite_defs{$s} }) { if ($f eq $dp || index($f, "$dp/") == 0) { $r{$s} = 1; last } }
    }
    my $needle = qr/\Q$f\E(?![A-Za-z0-9_.\-])/;
    for my $s (keys %suite_files) {
        next if $mapped && !$suite_rootdef{$s};
        $r{$s} = 1 if suite_text($s) =~ $needle;
    }
    return \%r;
}

sub grep_refs {   # suites and rows whose sources/args/row scripts name path $p (or its directory)
    my $p = shift;
    # A reference to the file itself, or to its directory; a reference to a SUBdirectory or to a
    # longer name that merely starts the same does not count.
    my @needles = (qr/\Q$p\E(?![A-Za-z0-9_.\-])/);
    my $dir = dirname($p);
    push @needles, qr{\Q$dir\E/?(?![A-Za-z0-9_.\-/])} if $dir =~ m{/};
    my (%ss, %rr);
    for my $s (keys %suite_files) {
        my $text = $args_of{$s} // '';
        for my $f (keys %{ $suite_files{$s} }) {
            next unless $f =~ m{^(test|src)/.*\.(cpp|h|cc|c|hpp)$};
            open(my $fh, '<', "$root/$f") or next;
            local $/; $text .= <$fh>; close $fh;
        }
        for my $n (@needles) { if ($text =~ $n) { $ss{$s} = 1; last } }
    }
    my @scripts = glob("$root/test/00regression/scripts/*.sh");
    for my $sc (@scripts) {
        open(my $fh, '<', $sc) or next;
        local $/; my $text = <$fh>; close $fh;
        for my $n (@needles) {
            if ($text =~ $n) {
                (my $row = basename($sc)) =~ s/\.sh$//;
                $rr{$row} = 1 if $declared_rows->{$row};
                last;
            }
        }
    }
    return (\%ss, \%rr);
}

sub slurp { my $f = shift; open(my $fh, '<', "$root/$f") or return ''; local $/; my $t = <$fh>; close $fh; $t }

# The statements of a CMake file as an ORDERED list (CMake is order-dependent: directory options
# apply to later targets only, a GLOB precedes its foreach). Each entry is [context, text]: the
# enclosing if()/foreach() chain (else/elseif included) and the statement, comments stripped
# quote-aware. Returns undef when the text uses anything this scanner cannot read with confidence
# (bracket arguments or comments, an unterminated statement or quote, a stray token, unbalanced
# end*()): the caller then plans FULL.
sub cmake_statements {
    my $t = shift;
    my (@out, @stack);
    my $i = 0; my $n = length $t;
    while ($i < $n) {
        my $c = substr($t, $i, 1);
        if ($c =~ /\s/) { $i++; next }
        if ($c eq '#') {
            return undef if substr($t, $i, 2) eq '#[';
            $i++ while $i < $n && substr($t, $i, 1) ne "\n"; next;
        }
        return undef unless $c =~ /[A-Za-z_]/;
        my $s = $i;
        $i++ while $i < $n && substr($t, $i, 1) =~ /[A-Za-z0-9_]/;
        my $cmd = lc substr($t, $s, $i - $s);
        my $j = $i; $j++ while $j < $n && substr($t, $j, 1) =~ /[ \t]/;
        return undef unless $j < $n && substr($t, $j, 1) eq '(';
        my ($depth, $q, $body) = (0, 0, '');
        my $k = $j; my $closed = 0;
        for (; $k < $n; $k++) {
            my $ch = substr($t, $k, 1);
            if ($q) {
                if ($ch eq '\\') { $body .= $ch . substr($t, ++$k, 1); next }
                $q = 0 if $ch eq '"';
                $body .= $ch; next;
            }
            if    ($ch eq '\\') { $body .= $ch . substr($t, ++$k, 1); next }
            elsif ($ch eq '"')  { $q = 1 }
            elsif ($ch eq '#')  { return undef if substr($t, $k, 2) eq '#['; $k++ while $k < $n && substr($t, $k, 1) ne "\n"; $body .= ' '; next }
            elsif ($ch eq '[')  { return undef if substr($t, $k) =~ /^\[=*\[/ }
            elsif ($ch eq '(')  { $depth++ }
            elsif ($ch eq ')')  { if (--$depth == 0) { $body .= $ch; $closed = 1; last } }
            $body .= $ch;
        }
        return undef unless $closed && !$q;
        $body =~ s/\s+/ /g;
        my $st = "$cmd$body";
        $i = $k + 1;
        if    ($cmd =~ /^(if|foreach|while|function|macro|block)$/) { push @stack, [$st] }
        elsif ($cmd eq 'elseif' || $cmd eq 'else') { return undef unless @stack; push @{ $stack[-1] }, $st }
        elsif ($cmd =~ /^end(if|foreach|while|function|macro|block)$/) { return undef unless @stack; pop @stack }
        else { push @out, [join(' >> ', map { join(' | ', @$_) } @stack), $st] }
    }
    return undef if @stack;
    return \@out;
}

# Commands whose statement belongs to the suite(s) it names; any other command is shared.
my $TARGET_CMD = qr/^(add_executable|add_test|add_dependencies|set_target_properties|set_tests_properties|target_\w+)\(/;

sub diff_lines {   # normal diff of two line lists -> [ [type, old_from, old_to, new_from, new_to], ... ]
    my ($x, $y) = @_;
    my $d = File::Temp->newdir();
    for my $p (['x', $x], ['y', $y]) { open(my $fh, '>', "$d/$p->[0]") or return undef; print $fh "$_\n" for @{ $p->[1] }; close $fh }
    open(my $ph, '-|', 'env', 'LANG=C', 'diff', "$d/x", "$d/y") or return undef;
    my @h;
    while (<$ph>) {
        chomp;
        next unless /^([\d,]+)([acd])([\d,]+)$/;
        my ($o, $ty, $nw) = ($1, $2, $3);
        my ($of, $ot) = split /,/, $o; $ot //= $of;
        my ($nf, $nt) = split /,/, $nw; $nt //= $nf;
        push @h, [$ty, $of, $ot, $nf, $nt];
    }
    close $ph;
    return undef unless !($? & 127) && ($? >> 8) <= 1;   # only diff's 0 (same) and 1 (differ) are success
    return \@h;
}

my $have_git = -e "$root/.git";
for my $f (@changed) {
    my $edges = edge_suites($f, ($file_suites{$f} || $any_mapped{$f}) ? 1 : 0);
    $suites{$_} = 1 for keys %$edges;
    # A submodule pointer (gitlink) changes every object that includes the submodule.
    if ($have_git) {
        my ($ls) = gitc('ls-files', '-s', '--', $f);
        if (defined $ls && $ls =~ /^160000\b/) { $full = 1; $full_why{"$f (a submodule pointer)"} = 1; next }
    }
    if ($file_suites{$f}) {
        my @decl = grep { $is_declared{$_} } @{ $file_suites{$f} };
        $suites{$_} = 1 for @decl;
        $notes{"$f is compiled into the application only (no unit suite links it): regression rows are the gate's, run the ones it touches"} = 1 if !@decl && !%$edges;
        next;
    }
    if ($any_mapped{$f}) { $notes{"$f is compiled into the application only (no unit suite reads it): regression rows are the gate's, run the ones it touches"} = 1; next }
    my $actstr = classify($f);
    if (!defined $actstr) { push @unmapped, $f unless %$edges; next }   # a suite that reads it places it
    for my $act (split ' ', $actstr) {   # a rule may name several actions
    if    ($act eq 'docs')   { $docs = 1 }
    elsif ($act eq 'full')   { $full = 1; $full_why{$f} = 1 }
    elsif ($act eq 'ci')     { $notes{"$f: CI itself runs it; the contract tests that read it are planned above"} = 1 }
    elsif ($act eq 'browser'){ $heavy{"$f: needs `make wasm-browser-test` (heavy, run by the author)"} = 1 }
    elsif ($act eq 'ignore') { }
    elsif ($act =~ /^make:(\S+)$/) { $makes{$1} = 1 }
    elsif ($act eq 'row-script') {
        (my $row = basename($f)) =~ s/\.sh$//;
        if ($declared_rows->{$row}) { $rows{$row} = 1 } else { $full = 1; $full_why{"$f (names no declared row)"} = 1 }
    }
    elsif ($act eq 'row-ref') {
        (my $row = basename($f)) =~ s/-reference\.png$//;
        if ($declared_rows->{$row}) { $rows{$row} = 1 } else { $full = 1; $full_why{"$f (names no declared row)"} = 1 }
    }
    elsif ($act eq 'row-conf') {
        my @d = $o{files} ? () : gitc('diff', '-U0', $base, '--', $f);
        my $n = 0;
        for my $l (@d) {
            next unless $l =~ /^[+-]\s*([A-Za-z0-9][A-Za-z0-9._-]*)(\s|$)/ && $l !~ /^(\+\+\+|---)/;
            my $r = $1;
            next if $r =~ /^expect/;
            $rows{$r} = 1 if $declared_rows->{$r}; $n++;
        }
        if (!$n) { $full = 1; $full_why{"$f (could not read which rows changed)"} = 1 }
    }
    elsif ($act eq 'suite-conf') {
        # test/unit-tests.conf (GH #564): the suites whose lines the change added, removed or
        # re-pinned. A directive (# gate:, # os:) or the # expect: total is proven by the whole
        # manifest checks every --only run still makes (registry, gate, os, total); a line this
        # cannot read makes the plan FULL.
        my @d = ($have_git && !$o{files}) ? gitc('diff', '-U0', '--no-renames', $base, '--', $f) : ();
        my $n = 0;
        for my $l (@d) {
            next unless $l =~ /^[+-]/ && $l !~ /^(\+\+\+|---)/;
            (my $b = $l) =~ s/^.//;
            next if $b =~ /^\s*$/;
            if ($b =~ /^\s*#/) {
                $notes{"$f: a directive or the # expect: total changed; run-unit-tests.sh checks the whole manifest on every --only run"} = 1 if $b =~ /^\s*#\s*(gate|os|expect):/;
                next;
            }
            if ($b =~ /^\s*\??(\S+)\s+\d+(\s|$)/) { $suites{$1} = 1; $n++; next }
            $n = -1_000_000;   # a line of no known shape
        }
        if ($n < 0 || !@d) { $full = 1; $full_why{"$f (could not read which suites changed)"} = 1 }
    }
    elsif ($act eq 'cmake-suites') {
        # test/CMakeLists.txt (GH #564): the suites whose statements the change added, removed or
        # moved into another if()/foreach() context. A changed statement that names no declared
        # suite (a shared setting, a library, a variable) makes the plan FULL.
        my ($old) = ($have_git && !$o{files}) ? join("\n", gitc('show', "$base:$f")) : '';
        my %names = %is_declared;
        for my $l ($have_git && !$o{files} ? git('show', "$base:test/unit-tests.conf") : ()) {
            $names{$1} = 1 if $l =~ /^\s*\??(\S+)\s+\d+/ && $l !~ /^\s*#/;
        }
        if (!length $old) { $full = 1; $full_why{"$f (could not read the base version)"} = 1; next }
        my $new = slurp($f);
        my ($os, $ns) = (cmake_statements($old), cmake_statements($new));
        if (!$os || !$ns) { $full = 1; $full_why{"$f (the CMake text uses syntax the planner cannot read with confidence)"} = 1; next }
        my $key = sub { "$_[0][0]\x01$_[0][1]" };
        my $suite_of = sub {   # suites a statement names, or () when it is shared
            my $st = $_[0][1];
            return () unless $st =~ $TARGET_CMD;
            return grep { $names{$_} } $st =~ /([A-Za-z_][A-Za-z0-9_]*)/g;
        };
        my $attribute = sub {
            my ($e) = @_;
            my @hit = $suite_of->($e);
            if (@hit) { $suites{$_} = 1 for @hit; return }
            (my $st = $e->[1]) =~ s/\s+/ /g; $st = substr($st, 0, 60) . '...' if length $st > 63;
            $full = 1; $full_why{"$f (a changed statement names no declared suite: $st)"} = 1;
        };
        # The statements, in order, as lines: a changed line is a statement added, removed or moved.
        my $dl = diff_lines([ map { $key->($_) } @$os ], [ map { $key->($_) } @$ns ]);
        if (!$dl) { $full = 1; $full_why{"$f (could not diff the statements)"} = 1; next }
        for my $h (@$dl) {
            my ($type, $of, $ot, $nf, $nt) = @$h;
            $attribute->($os->[$_ - 1]) for ($type ne 'a' ? ($of .. $ot) : ());
            $attribute->($ns->[$_ - 1]) for ($type ne 'd' ? ($nf .. $nt) : ());
        }
        # The same diff with every suite's statement made one placeholder: what is left is the
        # position of suite statements among shared ones. A statement deleted in one place and
        # added in another is a move across shared statements (options apply to later targets).
        my $ph = sub { $suite_of->($_[0]) ? "<S>" : $key->($_[0]) };
        my $pl = diff_lines([ map { $ph->($_) } @$os ], [ map { $ph->($_) } @$ns ]);
        if (!$pl) { $full = 1; $full_why{"$f (could not diff the statements)"} = 1; next }
        my (%del, %add);
        for my $h (@$pl) {
            my ($type, $of, $ot, $nf, $nt) = @$h;
            $del{ $key->($os->[$_ - 1]) }++ for ($type ne 'a' ? ($of .. $ot) : ());
            $add{ $key->($ns->[$_ - 1]) }++ for ($type ne 'd' ? ($nf .. $nt) : ());
        }
        my @moved = grep { $add{$_} } keys %del;
        if (@moved) { $full = 1; $full_why{"$f (a statement moved past shared statements)"} = 1 }
    }
    elsif ($act eq 'fixture') {
        my ($ss, $rr) = grep_refs($f);
        if (!%$ss && !%$rr && !%$edges) { $full = 1; $full_why{"$f (no suite source or row script names it)"} = 1 }
        $suites{$_} = 1 for keys %$ss;
        $rows{$_} = 1 for keys %$rr;
    }
    else { refuse("$confile: unknown action '$act' (glob matched $f)") }
    }
}
if ($git_bad) { $full = 1; $full_why{"(a git command that reads the change failed: the change set cannot be trusted)"} = 1 }
if (@unmapped) { $full = 1; $full_why{"$_ (UNMAPPED: in no depfile, matches no glob)"} = 1 for @unmapped }
my $only_docs = $docs && !$full && !%suites && !%rows && !%makes;
$suites{$_} = 1 for @smoke;
for my $s (keys %suites) { delete $suites{$s} unless $is_declared{$s} }

# ---------------------------------------------------------------- platform check
my (%plat);
my $harness_re = qr{^test/(run-unit-tests\.sh|unit-tests\.conf|row_id\.h)$};
for my $f (@changed) {
    my $t = ($f =~ m{\.(cpp|h|hpp|c|cc|mm|cmake|sh|txt|mjs|js|in)$} || $f =~ /CMakeLists\.txt$|^Makefile$/) ? slurp($f) : '';
    # What the change REMOVED counts too: deleting the last _WIN32 arm still changes that tree.
    $t .= "\n" . join("\n", map { substr($_, 1) } grep { /^-[^-]/ } gitc('diff', '-U0', $base, '--', $f)) if $have_git && !$o{files};
    my $browser_only = $f =~ m{^src/platform/(browser_frontend\.|web/)};
    my $iscmake = $f =~ /(^|\/)CMakeLists\.txt$|\.cmake$/;
    # sdl
    if    ($browser_only) { }
    elsif ($f =~ m{^src/(platform|gui)/}) { push @{ $plat{sdl} }, "$f (frontend/platform code the SDL-only tree builds)" }
    elsif ($t =~ /\b(ENABLE_QT_UI|ENABLE_DEBUGGER)\b/) { push @{ $plat{sdl} }, "$f (mentions ENABLE_QT_UI/ENABLE_DEBUGGER)" }
    elsif ($iscmake)               { push @{ $plat{sdl} }, "$f (CMake file)" }
    elsif ($f =~ $harness_re)      { push @{ $plat{sdl} }, "$f (the unit harness)" }
    # win
    my $w;
    if    ($f =~ m{(^|/)win_[^/]*$})   { $w = "$f (win_* file)" }
    elsif ($t =~ /\b_WIN32\b/)        { $w = "$f (contains _WIN32)" }
    elsif ($t =~ /winsock2\.h|sys\/socket\.h/) { $w = "$f (sockets)" }
    elsif ($t =~ /std::filesystem|<filesystem>/) { $w = "$f (std::filesystem)" }
    elsif ($t =~ /\bfork\s*\(|posix_spawn|CreateProcess/) { $w = "$f (process spawning)" }
    elsif ($iscmake && $t =~ /MINGW/) { $w = "$f (MINGW CMake arm)" }
    elsif ($f eq 'test/wine-run.sh')  { $w = "$f (the wine runner)" }
    elsif ($f =~ $harness_re)         { $w = "$f (the unit harness)" }
    push @{ $plat{win} }, $w if $w;
    # wasm
    my $m;
    if    ($browser_only)             { $m = "$f (compiled only by the browser build)" }
    elsif ($t =~ /__EMSCRIPTEN__/)    { $m = "$f (contains __EMSCRIPTEN__)" }
    elsif ($iscmake && $t =~ /EMSCRIPTEN/) { $m = "$f (EMSCRIPTEN CMake arm)" }
    elsif ($f eq 'test/node-run.sh' || $f =~ m{^web/}) { $m = "$f (WebAssembly runner or web tree)" }
    elsif ($f =~ $harness_re)         { $m = "$f (the unit harness)" }
    push @{ $plat{wasm} }, $m if $m;
}

# ---------------------------------------------------------------- output
my $mode = $full ? 'full' : 'subset';
my @sel = sort keys %suites;
if ($only_docs) { $mode = 'docs' }
printf "test-affected: base %s, head %s, %d changed file(s)\n", substr($base, 0, 12), substr($head, 0, 12), scalar @changed;
print "plan: ", uc($mode), "\n";
if ($full) { print "  FULL because:\n"; print "    $_\n" for sort keys %full_why }
else {
    print "  suites (", scalar(@sel), " of ", scalar(grep { $suite_present{$_} } @declared), " built, smoke set included): @sel\n" if $mode ne 'docs';
    print "  rows: ", join(' ', sort keys %rows), "\n" if %rows;
    print "  make: ", join(' ', sort keys %makes), "\n" if %makes;
    print "  docs-check: yes\n" if $docs;
}
print "  note: $_\n" for sort keys %notes;
print "  HEAVY: $_\n" for sort keys %heavy;
for my $p (qw(sdl win wasm)) {
    if ($plat{$p}) {
        my @r = @{ $plat{$p} };
        print "  platform: unit-test-$p NEEDED: ", join('; ', @r[0 .. ($#r < 2 ? $#r : 2)]), (@r > 3 ? "; ... (" . scalar(@r) . " files)" : ''), "\n";
    } else { print "  platform: unit-test-$p not needed\n" }
}
my @nomaps = grep { $nomap{$_} } @sel;
print "  note: no linker map for: @nomaps (selected on the archives' whole membership)\n" if @nomaps;

if ($o{plan}) {
    open(my $po, '>', $o{plan}) or refuse("cannot write $o{plan}");
    print $po "mode=$mode\n";
    print $po "suites=", join(',', @sel), "\n";
    print $po "rows=", join(' ', sort keys %rows), "\n";
    print $po "makes=", join(' ', sort keys %makes), "\n";
    print $po "docs=$docs\n";
    print $po "platforms=", join(' ', grep { $plat{$_} } qw(sdl win wasm)), "\n";
    close $po;
}
# Stateless miss detection: run-unit-tests.sh asks which of the suites that just failed this plan
# would not have selected (only meaningful when the plan is a subset; FULL would have run them).
if (defined $o{'explain-miss'}) {
    my %sel = map { $_ => 1 } @sel;
    if ($mode ne 'full') {   # a DOCS plan ran no suite at all: every failing suite is a miss
        for my $n (grep { length } split /,/, $o{'explain-miss'}) {
            print "TEST-AFFECTED MISS: $n was not selected for ", join(' ', @changed), "\n" unless $sel{$n};
        }
    }
}
exit 0;
