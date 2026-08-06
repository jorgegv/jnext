#!/usr/bin/perl
#
# Generate the user guide's "9.1 Command-line options" page from the OPTIONS
# section of doc/man/jnext.1.md (GH #213).
#
# That file is the SINGLE SOURCE for the CLI reference. It already generates
# doc/man/jnext.1 (roff) and USAGE.md (gfm); this script makes the user guide
# the third rendering of the same text, so the guide can carry the full,
# explained option list without becoming a copy that drifts.
#
# The output is COMMITTED (src/doc/user-guide/09-reference/01-command-line-options.md)
# and staleness-gated by `make docs-man-check` — a hand edit to it is a FAIL on
# every `make unit-test` and `make regression`, exactly like a stale man page.
#
# Two transformations are applied on top of the pandoc conversion, and neither
# touches the man source:
#
#   * The `# OPTIONS` heading is dropped. The page's own H1 comes from the
#     hand-written preamble (src/doc/user-guide-cli-preamble.md), so the man
#     page's `##` group headings land at the right level under it.
#
#   * References to OTHER man-page sections ("see **NETWORKING** below") are
#     rewritten to links into the guide. A reference to a section with no
#     mapping is a hard ERROR rather than a dangling bold run: the guide is
#     built with `mkdocs --strict`, which catches a broken link but has nothing
#     to say about a cross-reference that silently stopped being one.
#
# Usage: tools/gen-userguide-cli.pl [OUTPUT]
#   OUTPUT defaults to the committed page; `make docs-man-check` passes a temp
#   path instead and diffs the result.

use strict;
use warnings;
use File::Basename qw(dirname);
use File::Spec;

my $ROOT     = File::Spec->rel2abs( dirname($0) . '/..' );
my $MAN_SRC  = "$ROOT/doc/man/jnext.1.md";
my $PREAMBLE = "$ROOT/src/doc/user-guide-cli-preamble.md";
my $OUT      = $ARGV[0]
    // "$ROOT/src/doc/user-guide/09-reference/01-command-line-options.md";

# Man-page section title (parenthetical suffix stripped) -> guide link. The
# keys are validated against the real section titles below, so a renamed
# section is caught here rather than producing a link to nothing.
my %SECTION_LINK = (
    'SD CARD AND ROMS' =>
        '[Why JNEXT needs an SD-card image]'
        . '(../03-first-run/01-why-jnext-needs-an-sd-card-image.md)',
    'NETWORKING' =>
        '[5.6 Networking](../05-running-programs/06-networking.md)',
    # The guide has no logging chapter, so this one points at the man page's
    # own section, rendered in USAGE.md.
    'LOGGING' =>
        '[the LOGGING section of the man page]'
        . '(https://github.com/jorgegv/jnext/blob/main/USAGE.md#logging)',
);

sub fail { die "gen-userguide-cli: $_[0]\n" }

# --------------------------------------------------------------------------
# Read the man source: its section titles, and the body of OPTIONS.
# --------------------------------------------------------------------------
open( my $in, '<', $MAN_SRC ) or fail("cannot read $MAN_SRC: $!");
my @lines = <$in>;
close $in;

my %is_section;     # normalised title -> 1
my @options;        # the OPTIONS body, heading excluded
my $in_options = 0;
for my $line (@lines) {
    if ( $line =~ /^# (.+?)\s*$/ ) {
        my $title = $1;
        # "NETWORKING (ESP-01 WiFi)" is referred to as "NETWORKING" in the
        # prose, so index sections by their title without the parenthetical.
        ( my $norm = $title ) =~ s/\s*\(.*\)\s*$//;
        $is_section{$norm} = 1;
        $in_options = ( $title eq 'OPTIONS' );
        next;       # the OPTIONS heading itself is not part of the body
    }
    push @options, $line if $in_options;
}

fail("no '# OPTIONS' section in $MAN_SRC") unless @options;

# Guard against a silently broken extraction: an empty or tiny page would
# otherwise sail through every check downstream. Same idea as CLI-DOC-00.
my $defs = grep { /^\*\*\\?-/ } @options;
fail("only $defs option definitions found in $MAN_SRC - extraction broke")
    if $defs < 40;

my $body = join '', @options;

# --------------------------------------------------------------------------
# Rewrite cross-references to other man-page sections.
#
# This happens BEFORE pandoc, on the source text, so pandoc parses the links
# and lays them out itself. It also has to be wrap-tolerant: the man source
# wraps its prose, so a reference can straddle a line break, and a pattern
# that only matched a reference on one line would silently leave the other
# ones as dangling bold runs -- and the check below would miss them too.
# --------------------------------------------------------------------------
sub bold_ref_re {           # "SD CARD AND ROMS" -> /\*\*SD\s+CARD\s+.../
    my $words = join '\s+', map { quotemeta } split /\s+/, $_[0];
    return qr/\*\*$words\*\*/;
}

for my $title ( sort { length($b) <=> length($a) } keys %SECTION_LINK ) {
    fail("'$title' is not a section of $MAN_SRC any more")
        unless $is_section{$title};
    my $re = bold_ref_re($title);
    # "below"/"above" are positions within the man page; they are meaningless
    # once the reference has become a link to another page of the guide.
    $body =~ s/$re(?:\s+(?:below|above))?/$SECTION_LINK{$title}/g;
}

my @dangling;
while ( $body =~ /\*\*([A-Z][A-Z0-9\s,-]*[A-Z0-9])\*\*/g ) {
    ( my $norm = $1 ) =~ s/\s+/ /g;
    push @dangling, $norm if $is_section{$norm};
}
if (@dangling) {
    my %seen;
    my @uniq = grep { !$seen{$_}++ } @dangling;
    fail( "unmapped man-page section reference(s): "
        . join( ', ', @uniq )
        . "\n  add each to \%SECTION_LINK in $0, pointing at the guide page"
        . " that covers it" );
}

# --------------------------------------------------------------------------
# Convert pandoc-markdown to the markdown mkdocs reads.
#
# pandoc is the tool that owns this file's dialect, so it does the conversion:
# the guide then says exactly what the man page says. `-t markdown-smart`
# keeps the definition lists (python-markdown's def_list extension reads the
# same syntax) while stopping pandoc escaping `--` as `\--`, which it does
# only because roff would turn it into an en dash.
# --------------------------------------------------------------------------
my $pid = open( my $pandoc, '-|' );
fail("cannot fork: $!") unless defined $pid;
if ( $pid == 0 ) {
    open( my $w, '|-', 'pandoc', '-f', 'markdown', '-t', 'markdown-smart' )
        or fail("cannot run pandoc: $!");
    print {$w} $body;
    close $w or fail( "pandoc failed with status " . ( $? >> 8 ) );
    exit 0;
}
$body = do { local $/; <$pandoc> };
close $pandoc or fail( "pandoc failed with status " . ( $? >> 8 ) );

# --------------------------------------------------------------------------
# Emit: generated-file banner, hand-written preamble, converted options.
# --------------------------------------------------------------------------
open( my $pre, '<', $PREAMBLE ) or fail("cannot read $PREAMBLE: $!");
my $preamble = do { local $/; <$pre> };
close $pre;

my $banner = <<'BANNER';
<!-- GENERATED FILE - DO NOT EDIT. -->
<!-- This page is generated by tools/gen-userguide-cli.pl from the OPTIONS -->
<!-- section of doc/man/jnext.1.md, the single source it shares with the -->
<!-- jnext(1) man page and USAGE.md. To change the text of an option, edit -->
<!-- that file; to change the words around it, edit -->
<!-- src/doc/user-guide-cli-preamble.md. Then run `make docs-man` followed -->
<!-- by `make docs-userguide`, and commit both results. -->
<!-- A hand edit here FAILS `make docs-check`, and so fails every unit-test -->
<!-- and regression run. -->
BANNER

$preamble =~ s/\s+\z//;
$body     =~ s/\A\s+//;
$body     =~ s/\s+\z//;

open( my $out, '>', $OUT ) or fail("cannot write $OUT: $!");
print {$out} $banner, "\n", $preamble, "\n\n", $body, "\n";
close $out or fail("cannot close $OUT: $!");
