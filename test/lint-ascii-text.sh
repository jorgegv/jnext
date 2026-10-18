#!/usr/bin/env bash
# Structural lint: program text under src/ is ASCII (GH #382).
#
# WHY. jnext writes its own messages to stderr and stdout as raw UTF-8 bytes, and a
# Windows console decodes them in its OEM code page (850 on the reporter's machine):
# the three bytes E2 80 94 of an em dash come out as three accented Latin letters
# instead of a dash. Owner decision (2026-10-17): em dashes are not allowed in code,
# the other non-ASCII characters that reach a console become ASCII too, and there is
# no console-encoding workaround (no UTF-16 sink, no code-page change). This lint
# keeps the tree that way.
#
# SCOPE, literally: the string and character literals of every C and C++ source
# under src/ (*.cpp *.cc *.c *.h *.hpp), found by walking the directory, so the
# scanned set is the same before and after `git add`. Comments and everything outside
# a literal are never looked at. The lexer knows ordinary, u8, u, U and L literals,
# raw strings with any prefix and delimiter, // comments (a trailing backslash splices
# the next line), /* */ comments, and numbers with digit separators (28'000'000); the
# preprocessor lines are lexed like any other.
#
#   Rule A (owner; no exceptions; every file under src/, src/esp01/test/ included):
#     a literal holding U+2014 fails, in any spelling: raw UTF-8, a u or U escape
#     (either hex case), or the byte escapes \xE2\x80\x94 / \342\200\224 written
#     consecutively in the one literal. Escapes are decoded one by one, so an escaped
#     backslash followed by u2014 is plain text, and so is the same text inside a raw
#     string, which has no escapes.
#   Rule B (product sources: src/ except src/esp01/test/, whose literals are test-row
#     descriptions): a literal holding any other code point >= U+0080, as raw UTF-8 or
#     a u/U escape, fails unless its exact (path, literal as written) pair is declared
#     in test/lint-ascii-text.conf. The conf is for Qt widget text that is only ever
#     drawn by Qt and never written to a console. \x and octal escapes are byte data
#     (binary protocol payloads) and never count for rule B.
#
# CONF, test/lint-ascii-text.conf: one declaration per line, three TAB-separated
# fields: the path relative to the root, the literal body exactly as written in the
# source (escapes unexpanded, spaces significant), and the reason. '#' lines and blank
# lines are ignored. A declaration that matches no literal in its file is stale and
# fails; a line without exactly three non-empty fields is a malformed conf (exit 2).
# A declaration excuses rule B only.
#
# CANNOT CATCH, as examples and not as an exhaustive list: text assembled at run time
# from code points or byte tables; an em dash split across adjacent literals
# ("\xE2" "\x80\x94"); token pasting; text read from a file; user data; files that are
# not C or C++ (CMake, JSON, Qt resources, Markdown); and sources behind a symbolic
# link, which the walk skips. A root with no src/ scans nothing and reports clean (the
# gate always uses this script's own checkout, so only a typed root can do that).
#
# MAY WRONGLY FLAG: a byte that is not valid UTF-8 inside a literal is reported as
# non-ASCII (rule B), whatever the byte was meant to be.
#
# Usage: bash test/lint-ascii-text.sh [root]   (root defaults to the repo)
# Exit: 0 clean, 1 violation, 2 self-test failure, missing perl or malformed conf.
# The lexer is perl (already a hard prerequisite of `make unit-test`), embedded below,
# and it is self-tested on every invocation, like lint-assertions.

set -euo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$(cd "$SCRIPT_DIR/.." && pwd)}"
CONF_REL="test/lint-ascii-text.conf"

if ! command -v perl &>/dev/null; then
    echo "ERROR: perl is required" >&2
    exit 2
fi

# The scanner. Records on stdout, tab separated:
#   A <path> <line>                       rule A: U+2014 in the literal
#   B <path> <line> <U+XXXX|0xNN,...>     rule B: other non-ASCII text
#   S <conf line> <path> <body>           a declaration matching no literal
# Exit status 2 on a malformed conf.
IFS= read -r -d '' SCANNER <<'PERL' || true
use strict;
use warnings;
my ($root, $conf) = @ARGV;
die "usage: scanner ROOT CONF\n" unless defined $root && defined $conf;

# ---- the declaration file (rule B exemptions) ----
my (%decl, %seen, @decl);
if (-f $conf) {
    open(my $ch, '<:raw', $conf) or die "cannot read $conf: $!\n";
    my $ln = 0;
    while (my $l = <$ch>) {
        $ln++;
        $l =~ s/\r?\n\z//;
        next if $l =~ /\A[ \t]*#/ || $l =~ /\A[ \t]*\z/;
        my @f = split /\t/, $l, -1;
        if (@f != 3 || $f[0] eq '' || $f[1] eq '' || $f[2] eq '') {
            print STDERR "$conf:$ln: want exactly three TAB-separated, non-empty fields: path, literal as written, reason\n";
            exit 2;
        }
        $decl{"$f[0]\0$f[1]"} = 1;
        push @decl, [$ln, $f[0], $f[1]];
    }
    close($ch);
}

# ---- the files: every C/C++ source under ROOT/src, sorted ----
my @files;
sub walk {
    my ($d, $rel) = @_;
    opendir(my $dh, $d) or return;
    my @e = sort grep { $_ ne '.' && $_ ne '..' } readdir($dh);
    closedir($dh);
    for my $e (@e) {
        my ($p, $r) = ("$d/$e", "$rel/$e");
        next if -l $p;
        if (-d $p) { walk($p, $r) }
        elsif ($e =~ /\.(?:cpp|cc|c|h|hpp)\z/ && -f $p) { push @files, [$p, $r] }
    }
}
walk("$root/src", 'src');

# ---- decoding ----
# The code points of the non-ASCII characters in a byte string read as UTF-8; a byte
# outside any valid sequence is reported as a negative number (minus the byte).
sub raw_cps {
    my ($b) = @_;
    my @out;
    return @out unless $b =~ /[\x80-\xFF]/;
    my ($i, $n) = (0, length $b);
    while ($i < $n) {
        my $c = ord(substr($b, $i, 1));
        if ($c < 0x80) { $i++; next }
        my ($len, $min, $cp);
        if    ($c >= 0xC2 && $c <= 0xDF) { ($len, $min, $cp) = (2, 0x80,    $c & 0x1F) }
        elsif ($c >= 0xE0 && $c <= 0xEF) { ($len, $min, $cp) = (3, 0x800,   $c & 0x0F) }
        elsif ($c >= 0xF0 && $c <= 0xF4) { ($len, $min, $cp) = (4, 0x10000, $c & 0x07) }
        else { push @out, -$c; $i++; next }
        my $ok = ($i + $len <= $n);
        if ($ok) {
            for my $k (1 .. $len - 1) {
                my $cc = ord(substr($b, $i + $k, 1));
                if (($cc & 0xC0) != 0x80) { $ok = 0; last }
                $cp = ($cp << 6) | ($cc & 0x3F);
            }
        }
        if ($ok && $cp >= $min && $cp <= 0x10FFFF && !($cp >= 0xD800 && $cp <= 0xDFFF)) {
            push @out, $cp; $i += $len;
        } else { push @out, -$c; $i++ }
    }
    return @out;
}

# The UTF-8 bytes of a code point; '?' for one that is not a scalar value.
sub utf8_of {
    my ($c) = @_;
    return chr($c) if $c < 0x80;
    return chr(0xC0 | ($c >> 6)) . chr(0x80 | ($c & 0x3F)) if $c < 0x800;
    return '?' if ($c >= 0xD800 && $c <= 0xDFFF) || $c > 0x10FFFF;
    return chr(0xE0 | ($c >> 12)) . chr(0x80 | (($c >> 6) & 0x3F)) . chr(0x80 | ($c & 0x3F)) if $c < 0x10000;
    return chr(0xF0 | ($c >> 18)) . chr(0x80 | (($c >> 12) & 0x3F)) . chr(0x80 | (($c >> 6) & 0x3F)) . chr(0x80 | ($c & 0x3F));
}

# A literal body, decoded escape by escape (a raw string has no escapes). Two views:
# $bytes holds every byte the literal carries (raw bytes, u/U escapes as UTF-8, x and
# octal escapes as the byte they name), for the em dash search; @cps holds the code
# points of the TEXT in it (raw bytes read as UTF-8, and u/U escapes; never x or octal
# escapes, which are byte data), for rule B.
sub analyze {
    my ($body, $raw) = @_;
    return ($body, [raw_cps($body)]) if $raw;
    my (@cps, $bytes, $run);
    ($bytes, $run) = ('', '');
    my $flush = sub {
        return unless length $run;
        push @cps, raw_cps($run);
        $bytes .= $run;
        $run = '';
    };
    my $n = length $body;
    pos($body) = 0;
    while (pos($body) < $n) {
        if ($body =~ /\G\\u([0-9A-Fa-f]{4})/gc || $body =~ /\G\\U([0-9A-Fa-f]{8})/gc) {
            my $cp = hex($1);
            $flush->();
            $bytes .= utf8_of($cp);
            push @cps, $cp if $cp >= 0x80;
        }
        elsif ($body =~ /\G\\x([0-9A-Fa-f]+)/gc) {
            $flush->();
            $bytes .= chr(hex(substr($1, -2)));
        }
        elsif ($body =~ /\G\\([0-7]{1,3})/gc) {
            $flush->();
            $bytes .= chr(oct($1) & 0xFF);
        }
        elsif ($body =~ /\G\\(?:\r\n|.)/gcs || $body =~ /\G\\/gc) {
            $flush->();
            $bytes .= '?';
        }
        elsif ($body =~ /\G([^\\]+)/gc) {
            $run .= $1;
        }
    }
    $flush->();
    return ($bytes, \@cps);
}

# ---- the lexer ----
my $EM = "\xE2\x80\x94";
for my $f (@files) {
    my ($p, $rel) = @$f;
    open(my $fh, '<:raw', $p) or next;
    my $s = do { local $/; <$fh> };
    close($fh);
    my $n    = length $s;
    my $line = 1;
    pos($s) = 0;
    my $emit = sub {                                 # emit(body, raw, first line)
        my ($body, $raw, $at) = @_;
        $seen{"$rel\0$body"} = 1 if %decl && exists $decl{"$rel\0$body"};
        return unless $body =~ /[\x80-\xFF\\]/;
        my ($bytes, $cps) = analyze($body, $raw);
        print "A\t$rel\t$at\n" if index($bytes, $EM) >= 0;
        return if $rel =~ m{\Asrc/esp01/test/} || exists $decl{"$rel\0$body"};
        my %d;
        my @bad = grep { !$d{$_}++ } grep { $_ != 0x2014 } @$cps;
        print "B\t$rel\t$at\t", join(',', map { $_ < 0 ? sprintf('0x%02X', -$_) : sprintf('U+%04X', $_) } @bad), "\n" if @bad;
    };
    while (pos($s) < $n) {
        $s =~ /\G[^\n"'\/A-Za-z_0-9.]+/gc;           # nothing that can start a token of interest
        last if pos($s) >= $n;
        if ($s =~ /\G\n/gc) { $line++; next }
        if ($s =~ m{\G//}gc) {                       # line comment; a trailing backslash splices
            while (1) {
                $s =~ /\G[^\n]*/gc;
                my $e = pos($s);
                last if $e >= $n;
                my $before = ($e >= 2 && substr($s, $e - 1, 1) eq "\r") ? substr($s, $e - 2, 1) : substr($s, $e - 1, 1);
                last unless $before eq "\\";
                $s =~ /\G\n/gc;
                $line++;
            }
            next;
        }
        if ($s =~ m{\G/\*}gc) {                      # block comment
            my $a = pos($s);
            my $e = index($s, '*/', $a);
            $e = $e < 0 ? $n : $e + 2;
            $line += (substr($s, $a, $e - $a) =~ tr/\n//);
            pos($s) = $e;
            next;
        }
        if ($s =~ m{\G/}gc) { next }
        if ($s =~ /\G(?:u8|u|U|L)?R"([^()\\ \t\n\r\f\x0B]{0,16})\(/gc) {   # raw string
            my $d = $1;
            my $a = pos($s);
            my $e = index($s, ")$d\"", $a);
            my ($body, $stop);
            if ($e < 0) { $body = substr($s, $a); $stop = $n }
            else        { $body = substr($s, $a, $e - $a); $stop = $e + length($d) + 2 }
            $emit->($body, 1, $line);
            $line += (substr($s, $a, $stop - $a) =~ tr/\n//);
            pos($s) = $stop;
            next;
        }
        if ($s =~ /\G([A-Za-z_][A-Za-z_0-9]*)/gc) { # an identifier, or an encoding prefix
            next unless $1 eq 'L' || $1 eq 'u' || $1 eq 'U' || $1 eq 'u8';
            next unless substr($s, pos($s), 1) =~ /["']/;
        }
        elsif ($s =~ /\G\.?[0-9](?:[eEpP][+-]|'[0-9A-Za-z_]|[0-9A-Za-z_.])*/gc) { next }   # pp-number
        elsif ($s =~ /\G\./gc) { next }
        if ($s =~ /\G"((?:[^"\\\n]++|\\\r\n|\\.)*+)/gcs) {
            my $body = $1;
            $emit->($body, 0, $line);
            $line += ($body =~ tr/\n//);
            $s =~ /\G"/gc;
            next;
        }
        if ($s =~ /\G'((?:[^'\\\n]++|\\\r\n|\\.)*+)/gcs) {
            my $body = $1;
            $emit->($body, 0, $line);
            $line += ($body =~ tr/\n//);
            $s =~ /\G'/gc;
            next;
        }
        pos($s) = pos($s) + 1;                       # not reached; never stall
    }
}

# ---- declarations that match no literal ----
for my $d (@decl) {
    my ($ln, $path, $body) = @$d;
    print "S\t$ln\t$path\t$body\n" unless $seen{"$path\0$body"};
}
exit 0;
PERL

# scan <root>: the scanner's records for <root>/src, with <root>/test/lint-ascii-text.conf.
scan() { perl -e "$SCANNER" -- "$1" "$1/$CONF_REL"; }

# tokens: the records as one compact line, for the self-test to compare.
tokens() {
    awk -F'\t' '
        $1 == "A" { t = "A:" $3 }
        $1 == "B" { t = "B:" $3 ":" $4 }
        $1 == "S" { t = "S:" $2 }
        { out = out (out == "" ? "" : " ") t }
        END { print out }'
}

# fx <id> <expected tokens> <path> <line>...: one fixture, one file. A conf text left
# in CONF_TEXT is written as the fixture's conf. Any difference is a self-test failure
# that names the fixture.
fx() {
    local id="$1" want="$2" rel="$3" d got
    shift 3
    d="$ST_DIR/$id"
    mkdir -p "$d/$(dirname "$rel")" "$d/test"
    printf '%s\n' "$@" >"$d/$rel"
    if [[ -n "$CONF_TEXT" ]]; then printf '%s' "$CONF_TEXT" >"$d/$CONF_REL"; fi
    CONF_TEXT=''
    got=$(scan "$d" 2>/dev/null | tokens) || got="the scanner failed"
    if [[ "$got" != "$want" ]]; then
        echo "ERROR: self-test $id: expected [$want], got [$got]" >&2
        bad=1
    fi
}

# verdict <root>: scan <root> and report. Exit status: 0 clean, 1 violations, 2 the scanner
# failed (a malformed conf, say).
verdict() {
    local root="$1" rc=0 out na nb ns
    out=$(scan "$root") || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        echo "[lint-ascii-text] the scanner failed (exit $rc) on $root" >&2
        return 2
    fi
    na=$(awk -F'\t' '$1 == "A" { n++ } END { print n + 0 }' <<<"$out")
    nb=$(awk -F'\t' '$1 == "B" { n++ } END { print n + 0 }' <<<"$out")
    ns=$(awk -F'\t' '$1 == "S" { n++ } END { print n + 0 }' <<<"$out")
    if [[ "$na" -eq 0 && "$nb" -eq 0 && "$ns" -eq 0 ]]; then
        echo "[lint-ascii-text] clean"
        return 0
    fi
    echo "[lint-ascii-text] $na literal(s) with an em dash, $nb with other non-ASCII text, $ns stale declaration(s) (GH #382):" >&2
    awk -F'\t' -v conf="$CONF_REL" '
        $1 == "A" { printf "%s:%s: em dash (U+2014) in a string literal\n", $2, $3 }
        $1 == "B" { l = $4; gsub(/,/, ", ", l); printf "%s:%s: non-ASCII %s in a string literal\n", $2, $3, l }
        $1 == "S" { printf "%s:%s: no literal in %s matches the declared text: %s\n", conf, $2, $3, $4 }' <<<"$out" >&2
    echo "Use ASCII in program text (- for an em dash; -> <- ... > section x for the rest); Qt-only widget text may be declared in $CONF_REL with a reason." >&2
    return 1
}

# fxrc <id> <expected exit status> <conf text>: the scanner's exit status on a tree with
# that conf (a malformed conf must be refused, not skipped).
fxrc() {
    local id="$1" want="$2" conf="$3" d rc=0
    d="$ST_DIR/$id"
    mkdir -p "$d/src" "$d/test"
    printf 'f("a - b");\n' >"$d/src/t.cpp"
    printf '%s' "$conf" >"$d/$CONF_REL"
    verdict "$d" >/dev/null 2>&1 || rc=$?
    if [[ "$rc" -ne "$want" ]]; then
        echo "ERROR: self-test $id: expected exit $want, got $rc" >&2
        bad=1
    fi
}

self_test() {
    local bad=0 EM ARR BAD BS CR rc
    ST_DIR=$(mktemp -d) || exit 2
    CONF_TEXT=''
    EM=$(printf '\342\200\224')        # U+2014
    ARR=$(printf '\342\206\222')       # U+2192
    BAD=$(printf '\377')               # a byte that is not UTF-8
    BS='\'
    CR=$(printf '\r')

    # Rule A: flagged, with the line the literal starts on.
    fx LAT-A01 "A:1"     src/t.cpp "f(\"a ${EM} b\");"
    fx LAT-A02 "A:1 A:1" src/t.cpp "f(u8\"a ${EM} b\"); g(L\"${EM}\");"
    fx LAT-A03 "A:1 A:2" src/t.cpp "f(\"a ${BS}u2014 b\");" "g(\"${BS}U00002014\");"
    fx LAT-A04 "A:1 A:1" src/t.cpp "f(\"${BS}xE2${BS}x80${BS}x94\"); g(\"${BS}xe2${BS}x80${BS}x94\");"
    fx LAT-A05 "A:1"     src/t.cpp "f(\"${BS}342${BS}200${BS}224\");"
    fx LAT-A06 "A:1"     src/t.cpp "f(R\"x(a" " ${EM} )x\");"
    fx LAT-A07 "A:1"     src/t.cpp "f(\"http://x ${EM} y\");"
    fx LAT-A08 "A:1"     src/t.cpp "f(\"/* ${EM} */\");"
    fx LAT-A09 "A:1"     src/t.cpp "char q = '\"'; int n = 1'000; f(\"${EM}\");"
    fx LAT-A10 "A:6"     src/t.cpp "/* one" "   two ${EM}" "   three */" "const char* s = R\"(four" "five)\";" "f(\"${EM}\");"
    fx LAT-A11 "A:1"     src/t.cpp "f(\"${BS}${BS}${BS}u2014\");"
    fx LAT-A13 "A:1"     src/sub/dir/t.h   "const char* s = \"${EM}\";"
    fx LAT-A14 "A:1"     src/t.hpp "const char* s = \"${EM}\";"
    fx LAT-A15 "A:1"     src/t.cc  "const char* s = \"${EM}\";"
    fx LAT-A16 "A:1"     src/t.c   "const char* s = \"${EM}\";"
    fx LAT-A17 "A:1"     src/t.cpp "char16_t c = u'${EM}';"
    fx LAT-A18 "A:1"     src/t.cpp "f(R\"x(a)\" ${EM} b)x\");"          # the delimiter ends a raw string, not )"
    fx LAT-A19 "A:2"     src/t.cpp "// it's a comment, not a character literal" "f(\"${EM}\");"
    fx LAT-A20 "A:1"     src/t.cpp "/* a \"quote */ f(\"${EM}\");"
    fx LAT-A21 "A:1"     src/t.cpp "f(\"a \\\"b ${EM} c\");"             # an escaped quote does not end the literal
    fx LAT-A22 "A:1"     src/t.cpp "f(\"abc ${BS}" "${EM} def\");"        # a spliced literal starts on its first line
    fx LAT-A23 "A:3"     src/t.cpp "f(\"abc ${BS}" "def\");" "g(\"${EM}\");"
    fx LAT-A24 "A:1 A:2" src/t.cpp "f(\"${EM}\");${CR}" "g(\"${EM}\");${CR}"
    fx LAT-A25 "A:1"     src/t.cpp "f(\"abc ${BS}${CR}" "${EM} def\");${CR}"      # a CRLF splice inside a string literal
    fx LAT-A26 "A:1"     src/t.cpp "char16_t c = u'${BS}${CR}" "${EM}';${CR}"    # ... and inside a character literal
    fx LAT-A27 "A:1"     src/t.cpp "char16_t c = u'${BS}" "${EM}';"             # ... and an LF splice inside a character literal
    fx LAT-A28 "A:1"     src/t.cpp "char q = '${BS}\"'; f(\"${EM}\");"          # an escaped quote in a character literal opens no string
    fx LAT-A29 "A:3"     src/t.cpp "char16_t c = u'${BS}" "x';" "f(\"${EM}\");" # the line count survives a splice inside a character literal
    # Rule A is not excused by a declaration, and the declaration is not stale for it.
    CONF_TEXT=$(printf 'src/t.cpp\ta %s b\treason\n' "$EM")
    fx LAT-A12 "A:1"     src/t.cpp "f(\"a ${EM} b\");"

    # Clean: an em dash in a comment, text that merely looks like an escape, numbers.
    fx LAT-N01 ""        src/t.cpp "// a ${EM} b"
    fx LAT-N02 ""        src/t.cpp "/* a" " ${EM} b" "   c */"
    fx LAT-N03 ""        src/t.cpp "f(\"a - b\");"
    fx LAT-N04 ""        src/t.cpp "f(R\"(${BS}u2014)\");"
    fx LAT-N05 ""        src/t.cpp "f(\"${BS}${BS}u2014\");"
    fx LAT-N06 ""        src/t.cpp "x = 28'000'000; // ${EM}"
    fx LAT-N07 ""        src/t.cpp "// continued ${BS}" "f(\"${EM}\"); still the comment"
    fx LAT-N08 ""        src/notes.md "f(\"${EM}\");"            # not a C or C++ source
    fx LAT-N09 ""        test/t.cpp   "f(\"${EM}\");"            # outside src/
    fx LAT-N10 ""        src/t.cpp "x = 1'000; // ${EM}"                    # ONE separator: no literal starts there
    fx LAT-N11 ""        src/t.cpp "f(u8R\"(${BS}u2014)\", uR\"(${BS}u2014)\", UR\"(${BS}u2014)\", LR\"(${BS}u2014)\");"
    fx LAT-N12 ""        src/t.cpp "// continued ${BS}${CR}" "f(\"${EM}\");${CR}"
    fx LAT-N13 ""        src/t.cpp "// the \"${EM}\" glyph, quoted inside a comment"
    fx LAT-N14 ""        src/t.cpp "/* the \"${EM}\" glyph," "   quoted inside a comment */"
    fx LAT-N15 ""        src/t.cpp "char c = 'a'; // a ${EM} b"                 # a character literal ends at its closing quote

    # Rule B, the declarations, and the conf itself.
    fx LAT-B01 "B:1:U+2192" src/t.cpp "f(\"a ${ARR} b\");"
    fx LAT-B02 "B:1:U+25B8" src/t.cpp "f(\"${BS}u25B8\");"
    fx LAT-B03 ""           src/t.cpp "f(\"${BS}xFF${BS}xFE\");"
    CONF_TEXT=$(printf 'src/t.cpp\ta %s b\treason\n' "$ARR")
    fx LAT-B04 ""           src/t.cpp "f(\"a ${ARR} b\");"
    CONF_TEXT=$(printf 'src/t.cpp\ta %s b\treason\n' "$ARR")
    fx LAT-B05 "S:1"        src/t.cpp "f(\"a - b\");"
    fx LAT-B06 ""           src/esp01/test/t.cpp "f(\"a ${ARR} b\");"
    fx LAT-B07 "B:1:0xFF"   src/t.cpp "f(\"a ${BAD} b\");"
    fxrc LAT-B08 2 "$(printf 'src/t.cpp\tonly two fields')"
    fxrc LAT-B09 2 "$(printf 'src/t.cpp\ta %s b\t' "$ARR")"                 # an empty reason
    fxrc LAT-B10 2 "$(printf 'src/t.cpp\ta %s b\treason\textra' "$ARR")"   # four fields
    CONF_TEXT=$(printf '# a note\n\nsrc/t.cpp\ta %s b\treason\n\n' "$ARR")
    fx LAT-B11 ""           src/t.cpp "f(\"a ${ARR} b\");"
    fx LAT-B12 "B:1:U+2192,U+25B8" src/t.cpp "f(\"${ARR} ${BS}u25B8 ${ARR}\");"
    fx LAT-B13 "A:1 B:1:U+2192" src/t.cpp "f(\"${EM} ${ARR}\");"
    fx LAT-B14 "B:1:U+00E9,U+2713,U+1F600" src/t.cpp "f(\"$(printf '\303\251 \342\234\223 \360\237\230\200')\");"
    CONF_TEXT=$(printf 'src/t.cpp\ta %s b \treason\n' "$ARR")      # a trailing space: not that literal
    fx LAT-B15 "B:1:U+2192 S:1" src/t.cpp "f(\"a ${ARR} b\");"
    CONF_TEXT=$(printf 'src/other.cpp\ta %s b\treason\n' "$ARR")   # another file
    fx LAT-B16 "B:1:U+2192 S:1" src/t.cpp "f(\"a ${ARR} b\");"
    CONF_TEXT=$(printf 'src/t.cpp\ta %s b\treason\n' "$ARR")       # one literal declared, one not
    fx LAT-B17 "B:2:U+2192" src/t.cpp "f(\"a ${ARR} b\");" "g(\"c ${ARR} d\");"
    fx LAT-B18 "B:1:U+25B8" src/t.cpp "f(\"${BS}u25b8\");"
    # The rule B scope: src/esp01/test/ alone is outside it (LAT-B06); its neighbours are not.
    fx LAT-B19 "B:1:U+2192" src/esp01/src/t.cpp     "f(\"a ${ARR} b\");"
    fx LAT-B20 "B:1:U+2192" src/esp01/testing/t.cpp "f(\"a ${ARR} b\");"
    # A stale declaration is a violation by itself: nothing else is wrong in this tree.
    fxrc LAT-B21 1 "$(printf 'src/t.cpp\ta %s b\treason\n' "$ARR")"
    # The exclusion is anchored at the root of src/: the same words deeper down are not excluded.
    fx LAT-B22 "B:1:U+2192" src/gui/src/esp01/test/t.cpp "f(\"a ${ARR} b\");"

    rm -rf "$ST_DIR"
    if [[ "$bad" -ne 0 ]]; then
        echo "[lint-ascii-text] SELF-TEST FAILED: refusing to report on the tree." >&2
        exit 2
    fi
}

self_test

rc=0
verdict "$ROOT" || rc=$?
exit "$rc"
