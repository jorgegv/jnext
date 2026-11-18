#!/usr/bin/env perl
# wasm-function-size.pl <dir> -- refuse a WebAssembly unit-test module that holds an oversized function.
#
# Under node (V8) a wasm function is compiled whole, and the compiler's memory grows faster than
# the function's body: a unit suite written as one long main() becomes one huge function and costs
# gigabytes to compile (a 1.5 MB body needed 14.5 GB of RSS). This reads every <dir>/*.wasm, finds
# its largest function body (the size prefix of each body in the code section) and fails when one
# is over the bound. The remedy is in the message: split the function into [[gnu::noinline]] static
# parts (developer guide, section 4.7).
#
# Exit status: 0 = every module is within the bound; 1 = at least one module is over it (one FAIL
# line per offending module, all of them); 2 = cannot check, never a pass: no *.wasm in the
# directory, a file that is not a wasm module or is truncated, or a module with no name section
# (the link lost --profiling-funcs, so the offender could not be named).
use strict;
use warnings;

# The bound, in bytes of one function body. Measured (peak RSS of the suite under node, Liftoff
# compile of the function): 205 KB -> 174 MB, 0.48 GB; 196 KB -> 221 MB, 0.63 GB; 267 KB -> 174 MB,
# 0.51 GB; then the curve turns: 434 KB -> 378 MB, 1.3 GB; 621 KB -> 1.1 GB, 2.7 GB; 874 KB -> 2.9 GB,
# 5.6 GB; 1.5 MB -> 5.3 GB, 14.5 GB. The core's own largest function is 181 KB, so 256 KiB leaves it
# 44 % headroom. Changing it is a deliberate two-place edit: the self-test pins it independently.
use constant BOUND => 262144;

die "usage: wasm-function-size.pl <dir>\n" unless @ARGV == 1;
my $dir = $ARGV[0];
my @files = sort glob("$dir/*.wasm");
if (!@files) {
    print STDERR "wasm-function-size: no *.wasm module in $dir (nothing to check)\n";
    exit 2;
}

# A cursor over one module's bytes; every read past the end dies "truncated".
{
    package Rd;
    sub new { my ($c, $d) = @_; return bless { d => $d, p => 0 }, $c }
    sub eof { my $s = shift; return $s->{p} >= length $s->{d} }
    sub byte {
        my $s = shift;
        die "truncated\n" if $s->{p} >= length $s->{d};
        return ord substr($s->{d}, $s->{p}++, 1);
    }
    sub leb {    # unsigned LEB128 (up to 64 bits; Perl's native integer is enough here)
        my $s = shift;
        my ($v, $sh) = (0, 0);
        while (1) {
            my $b = $s->byte;
            $v |= ($b & 0x7F) << $sh;
            last unless $b & 0x80;
            $sh += 7;
            die "bad LEB128\n" if $sh > 63;
        }
        return $v;
    }
    sub bytes {
        my ($s, $n) = @_;
        die "truncated\n" if $s->{p} + $n > length $s->{d};
        my $r = substr($s->{d}, $s->{p}, $n);
        $s->{p} += $n;
        return $r;
    }
    sub name { my $s = shift; return $s->bytes($s->leb) }
}

# analyse(<file>): (largest body size, its function name, name-section present)
sub analyse {
    my $file = shift;
    open(my $fh, '<:raw', $file) or die "cannot open: $!\n";
    local $/;
    my $data = <$fh>;
    close $fh;
    die "not a wasm module (bad magic or version)\n"
        unless defined $data && length($data) >= 8 && substr($data, 0, 8) eq "\0asm\1\0\0\0";
    my $r = Rd->new(substr($data, 8));
    my ($imports, @bodies, %names, $have_names);
    while (!$r->eof) {
        my $id = $r->byte;
        my $sz = $r->leb;
        my $sec = Rd->new($r->bytes($sz));
        if ($id == 2) {    # import section: function imports come first in the index space
            my $n = $sec->leb;
            for (1 .. $n) {
                $sec->name; $sec->name;
                my $kind = $sec->byte;
                if    ($kind == 0) { $sec->leb; $imports++ }                                   # func: type
                elsif ($kind == 1) { $sec->byte; my $f = $sec->byte; $sec->leb; $sec->leb if $f & 1 }   # table
                elsif ($kind == 2) { my $f = $sec->byte; $sec->leb; $sec->leb if $f & 1 }       # memory
                elsif ($kind == 3) { $sec->byte; $sec->byte }                                   # global
                elsif ($kind == 4) { $sec->byte; $sec->leb }                                    # tag
                else { die "unknown import kind $kind\n" }
            }
            die "malformed import section (not consumed exactly)\n" unless $sec->eof;
        } elsif ($id == 10) {    # code section: a vector of size-prefixed bodies
            my $n = $sec->leb;
            for (1 .. $n) {
                my $bs = $sec->leb;
                $sec->bytes($bs);
                push @bodies, $bs;
            }
            die "malformed code section (not consumed exactly)\n" unless $sec->eof;
        } elsif ($id == 0) {     # custom section: the "name" one carries the function names
            my $cname = $sec->name;
            if ($cname eq 'name') {
                $have_names = 1;
                while (!$sec->eof) {
                    my $sub = $sec->byte;
                    my $ssz = $sec->leb;
                    my $s = Rd->new($sec->bytes($ssz));
                    if ($sub == 1) {
                        my $cnt = $s->leb;
                        for (1 .. $cnt) { my $idx = $s->leb; $names{$idx} = $s->name }
                    }
                }
            }
        }
    }
    die "no code section\n" unless @bodies;
    $imports ||= 0;
    my ($best, $bi) = (-1, 0);
    for my $i (0 .. $#bodies) { ($best, $bi) = ($bodies[$i], $i) if $bodies[$i] > $best }
    my $idx = $imports + $bi;
    return ($best, exists $names{$idx} ? $names{$idx} : "function #$idx", $have_names);
}

my (@fail, @err, $top);
for my $f (@files) {
    my ($size, $fname, $named) = eval { analyse($f) };
    if ($@) {
        my $m = $@; chomp $m;
        push @err, "ERROR $f: $m";
        next;
    }
    if (!$named) {
        push @err, "ERROR $f: no name section, so an oversized function could not be named "
                 . "(the test link lost --profiling-funcs, test/CMakeLists.txt)";
        next;
    }
    $top = [$f, $fname, $size] if !$top || $size > $top->[2];
    push @fail, sprintf("FAIL %s: function %s has a %d-byte body, over the %d-byte bound; "
                      . "split it into [[gnu::noinline]] static parts (developer guide, section 4.7)",
                        $f, $fname, $size, BOUND)
        if $size > BOUND;
}
print "$_\n" for @fail;
print STDERR "$_\n" for @err;
if (@err) {
    print STDERR "wasm-function-size: cannot check " . scalar(@err) . " module(s); that is a failure, never a pass\n";
    exit 2;
}
if (@fail) {
    printf STDERR "wasm-function-size: %d of %d module(s) over the bound\n", scalar(@fail), scalar(@files);
    exit 1;
}
printf "wasm-function-size: %d module(s) checked, largest function %s (%s) = %d bytes, bound %d\n",
       scalar(@files), $top->[1], $top->[0], $top->[2], BOUND;
exit 0;
