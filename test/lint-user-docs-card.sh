#!/usr/bin/env bash
# Structural lint: the USER-FACING texts never tell anyone how to make, copy, serve or
# host a NextZXOS SD card or image (GH #458).
#
# WHY. NextZXOS is SpecNext's copyrighted software and only specnext.com hosts it. The
# browser build makes its card in the visitor's browser from the official zip; the
# recipes for making a card and serving one are local development and testing and live
# in the developer guide only. A user-facing text that brings a recipe, a card URL or a
# page option naming a card back would read as promoting piracy, and no unit row can see
# that. Owner decision, 2026-11-15.
#
# USAGE: bash test/lint-user-docs-card.sh [--list] [ROOT]
#   ROOT   the tree to check (default: the repository this script is in). The self-test
#          points it at a temporary copy.
#   --list print the files in scope, one per line, and exit (the self-test copies them).
#   exit 0 clean, 1 a hit or a missing statement, 2 a fault (a listed path that matches
#   nothing, so the list cannot rot, or a bad invocation).
#
# SCOPE: the user-facing texts, as explicit paths and globs below. NOT the developer
# guide, CLAUDE.md, doc/webassembly, tests or tools: they carry the recipes on purpose.
#
# BANNED PATTERNS: the table below, extended regexes, case-insensitive, each with its
# reason and (optionally) a narrower scope. One line per hit:
#   file:line: [name] reason
#
# POSITIVE CHECK: the user guide's 10.4 page must carry the three statements, as fixed
# substrings (lines joined first, so a wrapped sentence still counts), so they cannot be
# edited out.
#
# CANNOT CATCH, as examples and not as an exhaustive list: prose such as "put the card
# on your server", a recipe spelt in other words, a card named through a variable. It
# catches the known shapes; the reviewer of a change to chapter 10 reads it in full.
set -uo pipefail

ROOT_DEFAULT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
LIST=0
ROOT=""
for a in "$@"; do
    case "$a" in
        --list) LIST=1 ;;
        -*) echo "lint-user-docs-card: unknown option $a" >&2; exit 2 ;;
        *) ROOT=$a ;;
    esac
done
ROOT=${ROOT:-$ROOT_DEFAULT}
cd "$ROOT" || { echo "lint-user-docs-card: no such directory: $ROOT" >&2; exit 2; }

# Files in scope: every entry must match at least one file.
SCOPE=(
    'src/doc/user-guide/*.md'
    'src/doc/user-guide/*/*.md'
    'doc/man/jnext.1.md'
    'README.md'
    'FEATURES.md'
    'packaging/web/README.txt.in'
    # The web layer and the example pages ship to page authors in the bundle and their
    # comments are read. Enabled when the browser flow stops naming an image URL:
    #   'web/examples/*.in'
    #   'web/jnext-widget.js.in'
    #   'web/jnext_api.js.in'
    #   'web/jnext-image.js'
)
# Also in scope: ChangeLog, its Unreleased section only (older sections are frozen text).
CHANGELOG=ChangeLog
GUIDE_104='src/doc/user-guide/10-web-page/04-*.md'

# pat NAME REGEX REASON [SCOPE-GLOB]
PAT_NAME=(); PAT_RE=(); PAT_WHY=(); PAT_SCOPE=()
pat() { PAT_NAME+=("$1"); PAT_RE+=("$2"); PAT_WHY+=("$3"); PAT_SCOPE+=("${4:-}"); }
pat make-minimal      'make-minimal-(sdcard-image|card)' 'the card recipe is developer-guide material'
pat fix-sdcard-image  'fix-sdcard-image'                 'a recipe for making a card (scoped to the browser texts; the desktop guide has its own)' 'web/*|packaging/web/*|src/doc/user-guide/10-web-page/*'
pat http-server       'http\.server'                     'serving a card is developer-guide material'
pat data-image        'data-(image|sha256)'              'the widget takes no image'
pat query-image       '[?&](image|sha256)='              'a page takes no image in its query string'
pat image-object      'image:[[:space:]]*\{'             'a page names no image'
pat image-url         'image\.(url|sha256)'              'a page names no image'
pat card-img          '\bcard\.img\b'                    'an example card file name'
pat example-card-url  'example\.(com|org)/[^ )'"'"'"]*\.(img|zip)' 'an example URL for hosting a card'
pat jnextdev          'jnextDev'                         'the local-development seam is developer-guide material'
pat dollar-card       '\$card'                           'the internal card option is not documented'
pat distro-url-env    'JNEXT_SDCARD_DISTRO_URL'          'a way to point JNEXT at another distribution'

# Resolve the scope to a file list (a pattern that matches nothing is a fault).
FILES=()
fault=0
for g in "${SCOPE[@]}"; do
    matched=0
    for f in $g; do
        [[ -f "$f" ]] || continue
        FILES+=("$f"); matched=1
    done
    if [[ $matched -eq 0 ]]; then
        echo "lint-user-docs-card: scope entry matches no file: $g" >&2; fault=1
    fi
done
[[ -f "$CHANGELOG" ]] || { echo "lint-user-docs-card: scope entry missing: $CHANGELOG" >&2; fault=1; }
shopt -s nullglob
G104=( $GUIDE_104 )
shopt -u nullglob
[[ ${#G104[@]} -ge 1 ]] || { echo "lint-user-docs-card: scope entry matches no file: $GUIDE_104" >&2; fault=1; }
[[ $fault -eq 0 ]] || exit 2

if [[ $LIST -eq 1 ]]; then
    printf '%s\n' "${FILES[@]}" "$CHANGELOG" "${G104[0]}" | sort -u
    exit 0
fi

hits=0
scan() {  # scan FILE-LABEL INPUT-FILE
    local label=$1 in=$2 i
    for i in "${!PAT_NAME[@]}"; do
        if [[ -n "${PAT_SCOPE[$i]}" ]]; then
            local ok=0 g
            IFS='|' read -ra gl <<<"${PAT_SCOPE[$i]}"
            for g in "${gl[@]}"; do [[ "$label" == $g ]] && ok=1; done
            [[ $ok -eq 1 ]] || continue
        fi
        local out
        out=$(grep -nEi -e "${PAT_RE[$i]}" "$in" 2>/dev/null) || continue
        while IFS= read -r line; do
            echo "$label:${line%%:*}: [${PAT_NAME[$i]}] ${PAT_WHY[$i]}"
            hits=$((hits + 1))
        done <<<"$out"
    done
}

for f in "${FILES[@]}"; do scan "$f" "$f"; done

# ChangeLog: the Unreleased section only (line numbers kept: the rest is blanked).
tmp=$(mktemp) || exit 2
awk 'BEGIN{u=0} /^## /{u=($0 ~ /^## Unreleased/)} {print (u ? $0 : "")}' "$CHANGELOG" >"$tmp"
scan "$CHANGELOG" "$tmp"
rm -f "$tmp"

# The three statements of 10.4, as fixed substrings over the page with its lines joined.
flat=$(tr '\n' ' ' <"${G104[0]}" | tr -s ' ')
for s in "SpecNext's copyrighted software" "JNEXT does not ship, host or serve" "stays in the visitor's browser"; do
    if [[ "$flat" != *"$s"* ]]; then
        echo "${G104[0]}: missing statement: $s"
        hits=$((hits + 1))
    fi
done

if [[ $hits -gt 0 ]]; then
    echo "lint-user-docs-card: $hits problem(s) in the user-facing texts (see the header of test/lint-user-docs-card.sh)" >&2
    exit 1
fi
exit 0
