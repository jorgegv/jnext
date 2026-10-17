#!/usr/bin/env bash
# Structural lint: no em dash in the string literals under src/ (GH #382).
#
# WHY. jnext writes its own messages to stderr and stdout as raw UTF-8 bytes, and a
# Windows console decodes them in its OEM code page (850 on the reporter's machine):
# the three bytes E2 80 94 of an em dash come out as three accented Latin letters
# instead of a dash. Owner decision (2026-10-17): em dashes are not allowed in code,
# and there is no console-encoding workaround (no UTF-16 sink, no code-page change).
# This lint keeps the tree that way.
#
# SCOPE, literally: the string and character literals of every C and C++ source
# under src/ (*.cpp *.cc *.c *.h *.hpp), found by walking the directory, so the
# scanned set is the same before and after `git add`. Comments and everything outside
# a literal are never looked at. The lexer knows ordinary, u8, u, U and L literals,
# raw strings with any prefix and delimiter, // comments (a trailing backslash splices
# the next line), /* */ comments, and numbers with digit separators (28'000'000); the
# preprocessor lines are lexed like any other.
#
# THE RULE (owner; no exceptions; every file under src/, src/esp01/test/ included): a
# literal holding U+2014 fails, in any spelling: raw UTF-8, a u or U escape (either hex
# case), or the byte escapes \xE2\x80\x94 / \342\200\224 written consecutively in the
# one literal. Escapes are decoded one by one, so an escaped backslash followed by
# u2014 is plain text, and so is the same text inside a raw string, which has no
# escapes.
#
# CANNOT CATCH, as examples and not as an exhaustive list: text assembled at run time
# from code points or byte tables; an em dash split across adjacent literals
# ("\xE2" "\x80\x94"); token pasting; text read from a file; user data; and files that
# are not C or C++ (CMake, JSON, Qt resources, Markdown).
#
# Usage: bash test/lint-ascii-text.sh [root]   (root defaults to the repo)
# Exit: 0 clean, 1 violation, 2 self-test failure or missing perl.
# The lexer is perl (already a hard prerequisite of `make unit-test`), embedded below,
# and it is self-tested on every invocation, like lint-assertions.

set -euo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$(cd "$SCRIPT_DIR/.." && pwd)}"

if ! command -v perl &>/dev/null; then
    echo "ERROR: perl is required" >&2
    exit 2
fi

# The scanner. One record per offending literal on stdout, tab separated:
#   A <path> <line>       U+2014 in the literal that starts on <line>
IFS= read -r -d '' SCANNER <<'PERL' || true
use strict;
use warnings;
my ($root) = @ARGV;
die "usage: scanner ROOT\n" unless defined $root;

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

# The UTF-8 bytes of a code point; '?' for one that is not a scalar value.
sub utf8_of {
    my ($c) = @_;
    return chr($c) if $c < 0x80;
    return chr(0xC0 | ($c >> 6)) . chr(0x80 | ($c & 0x3F)) if $c < 0x800;
    return '?' if ($c >= 0xD800 && $c <= 0xDFFF) || $c > 0x10FFFF;
    return chr(0xE0 | ($c >> 12)) . chr(0x80 | (($c >> 6) & 0x3F)) . chr(0x80 | ($c & 0x3F)) if $c < 0x10000;
    return chr(0xF0 | ($c >> 18)) . chr(0x80 | (($c >> 12) & 0x3F)) . chr(0x80 | (($c >> 6) & 0x3F)) . chr(0x80 | ($c & 0x3F));
}

# The bytes a literal body carries, decoded escape by escape (a raw string has no
# escapes): raw bytes as they are, u/U escapes as UTF-8, x and octal escapes as the
# byte they name, every other escape as a placeholder.
sub bytes_of {
    my ($body, $raw) = @_;
    return $body if $raw;
    my $bytes = '';
    my $n = length $body;
    pos($body) = 0;
    while (pos($body) < $n) {
        if ($body =~ /\G\\u([0-9A-Fa-f]{4})/gc || $body =~ /\G\\U([0-9A-Fa-f]{8})/gc) {
            $bytes .= utf8_of(hex($1));
        }
        elsif ($body =~ /\G\\x([0-9A-Fa-f]+)/gc) {
            $bytes .= chr(hex(substr($1, -2)));
        }
        elsif ($body =~ /\G\\([0-7]{1,3})/gc) {
            $bytes .= chr(oct($1) & 0xFF);
        }
        elsif ($body =~ /\G\\(?:\r\n|.)/gcs || $body =~ /\G\\/gc) {
            $bytes .= '?';
        }
        elsif ($body =~ /\G([^\\]+)/gc) {
            $bytes .= $1;
        }
    }
    return $bytes;
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
        return unless $body =~ /[\x80-\xFF\\]/;
        print "A\t$rel\t$at\n" if index(bytes_of($body, $raw), $EM) >= 0;
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
exit 0;
PERL

# scan <root>: the scanner's records for <root>/src.
scan() { perl -e "$SCANNER" -- "$1"; }

# tokens: the records as one compact line, for the self-test to compare.
tokens() {
    awk -F'\t' '
        $1 == "A" { t = "A:" $3 }
        { out = out (out == "" ? "" : " ") t }
        END { print out }'
}

# fx <id> <expected tokens> <path> <line>...: one fixture, one file. Any difference is a
# self-test failure that names the fixture.
fx() {
    local id="$1" want="$2" rel="$3" d got
    shift 3
    d="$ST_DIR/$id"
    mkdir -p "$d/$(dirname "$rel")"
    printf '%s\n' "$@" >"$d/$rel"
    got=$(scan "$d" 2>/dev/null | tokens) || got="the scanner failed"
    if [[ "$got" != "$want" ]]; then
        echo "ERROR: self-test $id: expected [$want], got [$got]" >&2
        bad=1
    fi
}

self_test() {
    local bad=0 EM BS
    ST_DIR=$(mktemp -d) || exit 2
    EM=$(printf '\342\200\224')        # U+2014
    BS='\'

    # Flagged, with the line the literal starts on.
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

    # Clean: an em dash in a comment, text that merely looks like an escape, numbers.
    fx LAT-N01 ""        src/t.cpp "// a ${EM} b"
    fx LAT-N02 ""        src/t.cpp "/* a" " ${EM} b" "   c */"
    fx LAT-N03 ""        src/t.cpp "f(\"a - b\");"
    fx LAT-N04 ""        src/t.cpp "f(R\"(${BS}u2014)\");"
    fx LAT-N05 ""        src/t.cpp "f(\"${BS}${BS}u2014\");"
    fx LAT-N06 ""        src/t.cpp "x = 28'000'000; // ${EM}"
    fx LAT-N07 ""        src/t.cpp "// continued ${BS}" "f(\"${EM}\"); still the comment"

    rm -rf "$ST_DIR"
    if [[ "$bad" -ne 0 ]]; then
        echo "[lint-ascii-text] SELF-TEST FAILED: refusing to report on the tree." >&2
        exit 2
    fi
}

self_test

rc=0
out=$(scan "$ROOT") || rc=$?
if [[ "$rc" -ne 0 ]]; then
    echo "[lint-ascii-text] the scanner failed (exit $rc) on $ROOT" >&2
    exit 2
fi

if [[ -z "$out" ]]; then
    echo "[lint-ascii-text] clean"
    exit 0
fi

echo "[lint-ascii-text] em dash in a string literal under src/ (GH #382):" >&2
awk -F'\t' '$1 == "A" { printf "%s:%s: em dash (U+2014) in a string literal\n", $2, $3 }' <<<"$out" >&2
echo "Use a plain '-' in program text: Windows consoles show the UTF-8 bytes of an em dash as garbage." >&2
exit 1
