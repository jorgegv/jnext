package SuiteSources;
# suite -> its test sources, read from the CMakeLists.txt that builds it.
#
# The ONE reader of that mapping. traceability-dup-ids.pl (and, through its
# `--sources`, test/run-unit-tests.sh's row-ID literal check) and
# refresh-traceability-matrix.pl used to keep a parser each, and they disagreed:
# one read every source of a suite, the other only the first, so a traced
# suite's second source could hold rows the duplicate gates checked and the
# matrix never saw. Both call this now.
#
# Rules, all of them inherited from the readers this replaces:
#   * every CMakeLists.txt under the root, except under third_party/, build*/
#     and .git/ (judged below the root);
#     a path is resolved relative to the CMakeLists.txt that declares it, so a
#     module-resident suite (src/esp01/test/...) needs no special case;
#   * whole-line `#` comments are ignored; a declaration may span lines;
#   * the sources are the `.cpp`/`.c` arguments, in order. A `${VAR}` argument
#     is not one: in first position it is a source LIST (the suite is refused
#     rather than guessed at), anywhere else it is an emulator translation unit
#     linked in (`${CMAKE_SOURCE_DIR}/src/core/wav_loader.cpp`), not a test file;
#   * a name declared twice with different sources keeps the first, with a
#     warning — silently keeping one would disagree with the build.
#
# Paths are returned relative to the root. Existence is NOT checked here: each
# caller refuses a missing file in its own words.
use strict;
use warnings;
use Exporter 'import';
our @EXPORT_OK = qw(cmake_suite_sources);

sub cmake_suite_sources {
    my ($root) = @_;
    my @lists;
    open(my $fh, '-|', 'find', $root, '-name', 'CMakeLists.txt')
        or die "SuiteSources: cannot run find under $root: $!\n";
    while (my $p = <$fh>) {
        chomp $p;
        # Excluded by the path BELOW the root, so a checkout that itself lives
        # under a directory called build* is not excluded wholesale.
        (my $rel = $p) =~ s{^\Q$root\E/}{};
        next if $rel =~ m{(?:^|/)(?:third_party|\.git|build[^/]*)/};
        push @lists, $p;
    }
    close $fh;

    my %src;
    for my $list (sort @lists) {
        (my $dir = $list) =~ s{/CMakeLists\.txt$}{};
        open(my $lf, '<', $list) or next;
        my $text = join '', grep { !/^\s*#/ } <$lf>;
        close $lf;
        while ($text =~ /\badd_executable\s*\(\s*([A-Za-z0-9_]+)\s+([^()]*)\)/g) {
            my ($name, @args) = ($1, grep { length } split /\s+/, $2);
            next if !@args || $args[0] =~ /^\$\{/;
            my @srcs;
            for my $a (@args) {
                next if $a =~ /^\$\{/ || $a !~ /\.(?:cpp|c)$/;
                (my $rel = "$dir/$a") =~ s{^\Q$root\E/}{};
                push @srcs, $rel;
            }
            next unless @srcs;
            if (exists $src{$name}) {
                warn "WARN: add_executable($name) declared twice with different "
                   . "sources: @{ $src{$name} } vs @srcs\n"
                    if "@{ $src{$name} }" ne "@srcs";
                next;
            }
            $src{$name} = \@srcs;
        }
    }
    return \%src;
}

1;
