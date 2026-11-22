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
#   --last FILE      write the record of this selection (base, head, files, suites)
#   --check          list tracked files that are unmapped; exit 1 if any (--all: list every one)
#   --tracked FILE   the tracked-file list for --check (default: git ls-files)
# Exit: 0 ok, 1 --check found unmapped files, 2 refusal (bad input).
use strict;
use warnings;
use File::Basename qw(dirname basename);
use File::Spec;
use Cwd qw(abs_path);

my $here = abs_path(dirname($0));
my %o = (root => abs_path("$here/.."));
my @args = @ARGV;
while (@args) {
    my $a = shift @args;
    if    ($a eq '--check') { $o{check} = 1 }
    elsif ($a eq '--all')   { $o{all} = 1 }
    elsif ($a =~ /^--(root|build|manifest|conf|base|files|smoke|plan|last|tracked)$/) {
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
my (%suite_files, %nomap, %suite_present);
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
sub git { my @c = ('git', '-C', $root, @_); open(my $p, '-|', @c) or die "git: $!"; my @l = <$p>; close $p; chomp @l; return @l }

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
    $base = $o{base} // (git('merge-base', 'HEAD', 'main'))[0];
    refuse("no base: pass --base REF (no `main` branch to take a merge-base with)") unless $base;
    my %s;
    $s{$_} = 1 for git('diff', '--name-only', '--no-renames', $base);
    $s{$_} = 1 for git('ls-files', '--others', '--exclude-standard');
    @changed = sort keys %s;
}

# smoke suites
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

for my $f (@changed) {
    if ($file_suites{$f}) { $suites{$_} = 1 for @{ $file_suites{$f} }; next }
    if ($any_mapped{$f}) { $notes{"$f is compiled into the application only (no unit suite reads it): regression rows are the gate's, run the ones it touches"} = 1; next }
    my $act = classify($f);
    if (!defined $act) { push @unmapped, $f; next }
    if    ($act eq 'docs')   { $docs = 1 }
    elsif ($act eq 'full')   { $full = 1; $full_why{$f} = 1 }
    elsif ($act eq 'ci')     { $notes{"$f: CI only, nothing runs locally"} = 1 }
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
        my @d = $o{files} ? () : git('diff', '-U0', $base, '--', $f);
        my $n = 0;
        for my $l (@d) {
            next unless $l =~ /^[+-]\s*([A-Za-z0-9][A-Za-z0-9._-]*)(\s|$)/ && $l !~ /^(\+\+\+|---)/;
            my $r = $1;
            next if $r =~ /^expect/;
            $rows{$r} = 1 if $declared_rows->{$r}; $n++;
        }
        if (!$n) { $full = 1; $full_why{"$f (could not read which rows changed)"} = 1 }
    }
    elsif ($act eq 'fixture') {
        my ($ss, $rr) = grep_refs($f);
        if (!%$ss && !%$rr) { $full = 1; $full_why{"$f (no suite source or row script names it)"} = 1 }
        $suites{$_} = 1 for keys %$ss;
        $rows{$_} = 1 for keys %$rr;
    }
    else { refuse("$confile: unknown action '$act' (glob matched $f)") }
}
if (@unmapped) { $full = 1; $full_why{"$_ (UNMAPPED: in no depfile, matches no glob)"} = 1 for @unmapped }
my $only_docs = $docs && !$full && !%suites && !%rows && !%makes;
$suites{$_} = 1 for @smoke;
for my $s (keys %suites) { delete $suites{$s} unless $is_declared{$s} }

# ---------------------------------------------------------------- platform check
my (%plat);
sub slurp { my $f = shift; open(my $fh, '<', "$root/$f") or return ''; local $/; my $t = <$fh>; close $fh; $t }
my $harness_re = qr{^test/(run-unit-tests\.sh|unit-tests\.conf|row_id\.h)$};
for my $f (@changed) {
    my $t = ($f =~ m{\.(cpp|h|hpp|c|cc|mm|cmake|sh|txt|mjs|js|in)$} || $f =~ /CMakeLists\.txt$|^Makefile$/) ? slurp($f) : '';
    my $iscmake = $f =~ /(^|\/)CMakeLists\.txt$|\.cmake$/;
    # sdl
    if    ($f =~ m{^src/(platform|gui)/}) { push @{ $plat{sdl} }, "$f (frontend/platform code the SDL-only tree builds)" }
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
    if    ($t =~ /__EMSCRIPTEN__/)    { $m = "$f (contains __EMSCRIPTEN__)" }
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
if ($o{last}) {
    open(my $lo, '>', $o{last}) or refuse("cannot write $o{last}");
    print $lo "base=$base\nhead=$head\nmode=$mode\n";
    print $lo "files=", join(' ', @changed), "\n";
    print $lo "suites=", join(',', @sel), "\n";
    close $lo;
}
exit 0;
