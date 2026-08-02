#!/usr/bin/env bash
# Owner-absolute-path lint (GH #204).
#
# Bans one machine's home directory from the tracked, EXECUTABLE surface of the
# repository: scripts, sources, build files and configuration. A path like
#
#     sys.path.insert(0, "/home/someone/src/spectrum/jnext/tools/cspect_dzrp")
#
# works for exactly one person. Every other clone, every worktree, every CI
# container and every contributor gets a silent failure or an ImportError.
#
# WHY A LINT AND NOT JUST A SWEEP. GH #204 fixed thirty-odd such paths by hand.
# Nothing in that fix was discriminative: the review pointed out — correctly,
# and proved it by re-running the pre-fix hook self-test — that every test in
# the repository still passed BEFORE the fix, because the hardcoded literal
# happens to name the maintainer's real checkout on the maintainer's machine.
# The bug class is invisible from the machine that has the bug. A static grep
# is the only gate that can see it from here, exactly as `lint-traps.sh` is the
# only gate that can see a stray `trap` from a green run.
#
# SCOPE — deliberately narrow, and stated rather than implied:
#
#   * TRACKED files only (`git ls-files`). Personal, untracked configuration
#     such as `.claude/settings.local.json` is nobody else's problem and is not
#     scanned. Untracking a file is therefore a legitimate way to satisfy this
#     lint, as legitimate as editing it.
#   * EXECUTABLE/CONFIG file types only (see EXTENSIONS below). Prose is not
#     scanned: `.md` design notes, `.prompts/` daily logs and `doc/issues/`
#     session records are historical records of what someone did on some day,
#     and rewriting them would falsify the record. Same reasoning exempts
#     `test/bench/baseline-*.txt`, which record the absolute paths of the
#     binaries a past benchmark actually ran.
#   * A home path whose user component is an obvious PLACEHOLDER is allowed —
#     `/home/user/...` in a test fixture is documentation, not a machine.
#
# WHAT IT CANNOT CATCH, stated so nobody mistakes a pass for a proof: a path
# assembled at run time ("/home/" + user), a hardcoded path to somewhere other
# than a home directory (`/opt/mytools/...`), and anything in a file type or a
# file state it does not scan. It catches the mistake that actually happened —
# a literal home path typed into a script — and does not attempt more.
#
# Usage:
#   bash test/lint-hardcoded-paths.sh              # scan; exit 1 on any offender
#   bash test/lint-hardcoded-paths.sh --selftest   # prove the matcher, both ways
#
# Run from anywhere; it resolves the repository itself.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# File types where a path is CODE or CONFIGURATION rather than prose. Extend
# this list when a new such type appears; do NOT extend it to documentation.
EXTENSIONS='sh|bash|py|pl|pm|js|c|cc|cpp|h|hpp|cs|cmake|mk|yml|yaml|json|conf|spec|desktop|in'
# Exact filenames with no extension of their own.
EXACT_NAMES='Makefile|CMakeLists.txt|Dockerfile'

# A user component that is plainly a stand-in rather than somebody's account.
# `/home/user/...` inside a test fixture documents a shape; it is not a machine.
PLACEHOLDER_USERS='user|username|youruser|someuser|me|USER|<user>'

# An absolute home directory: /home/<user>/ or macOS /Users/<user>/.
PATTERN='(^|[^A-Za-z0-9_/])/(home|Users)/[A-Za-z0-9._<>-]+/'

# ── Declared exceptions ───────────────────────────────────────────────────────
# path:reason — a file allowed to keep an owner-absolute path, with the reason
# spelled out here rather than hidden in a glob. Keep this list empty if you
# can: an entry is a promise that the path genuinely cannot be derived.
EXCEPTIONS=(
    # This file. Its doc-comment example and its --selftest fixtures are
    # deliberately literal instances of the banned pattern — that is what makes
    # them a test rather than a description. It was NOT self-exempt when first
    # written, and the omission hid itself: `scan()` walks `git ls-files`, so
    # while the file was still untracked it was invisible to its own scan, and
    # `git add` at commit time turned a green run red. Found by review.
    # Consequence, stated rather than hidden: a genuine hardcoded path added to
    # THIS file is the one thing the lint cannot catch.
    "test/lint-hardcoded-paths.sh:its own doc example and selftest fixtures are literal instances of the pattern"
)

is_excepted() {
    local f="$1" e
    for e in "${EXCEPTIONS[@]}"; do
        [[ "$f" == "${e%%:*}" ]] && return 0
    done
    return 1
}

# Strip the placeholder cases, then report whatever is left. Reads candidate
# `path:line:text` records on stdin.
filter_placeholders() {
    grep -Ev "/(home|Users)/($PLACEHOLDER_USERS)/" || true
}

scan() {
    local root="$1" files f hits=0 out
    cd "$root"
    # Tracked files only, of the declared types.
    mapfile -t files < <(git ls-files \
        | grep -E "(\.($EXTENSIONS)$)|(^|/)($EXACT_NAMES)$" || true)
    [ ${#files[@]} -gt 0 ] || return 0

    for f in "${files[@]}"; do
        is_excepted "$f" && continue
        [ -f "$f" ] || continue
        out="$(grep -nE "$PATTERN" -- "$f" 2>/dev/null | filter_placeholders || true)"
        if [ -n "$out" ]; then
            while IFS= read -r line; do
                printf '  %s:%s\n' "$f" "$line"
                hits=$((hits + 1))
            done <<< "$out"
        fi
    done
    return $((hits > 0 ? 1 : 0))
}

# ── Self-test ─────────────────────────────────────────────────────────────────
# Pins the matcher in BOTH directions against literal cases. A lint that only
# proves what it catches is half a lint: the false-positive side is what makes
# it safe to leave switched on.
selftest() {
    local pass=0 fail=0

    # want=1 -> must be reported; want=0 -> must NOT be reported.
    t() {
        local want="$1" desc="$2" text="$3" got=0
        if printf '%s\n' "$text" | grep -qE "$PATTERN" \
           && printf '%s\n' "$text" | filter_placeholders | grep -q .; then
            got=1
        fi
        if [ "$got" = "$want" ]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            printf '  FAIL (want=%s got=%s) %s\n      %s\n' "$want" "$got" "$desc" "$text"
        fi
    }

    # ── Must be caught ────────────────────────────────────────────────────
    t 1 'python sys.path against a clone'      'sys.path.insert(0, "/home/jorgegv/src/spectrum/jnext/tools")'
    t 1 'shell variable pinned to a checkout'  'REPO_ROOT="/home/jorgegv/src/spectrum/jnext"'
    t 1 'output file under a home directory'   "open('/home/jorgegv/tmp/report.tsv', 'w')"
    t 1 'a header comment citing a home path'  '//   /home/jorgegv/src/spectrum/FPGA/cores/zxnext/src/'
    t 1 'JSON hook command'                    '"command": "/home/jorgegv/src/spectrum/jnext/.claude/hooks/x.sh"'
    t 1 'macOS home'                           'DEST="/Users/jorgegv/Library/Application Support/jnext"'
    t 1 'mid-line after an equals sign'        'PATH_TO=/home/alice/bin/tool'
    t 1 'inside a single-quoted shell string'  "cp x '/home/bob/out/x'"
    t 1 'a dotted account name'                'D=/home/jorge.gv/src/jnext'
    t 1 'a hyphenated account name'            'D=/home/jorge-gv/src/jnext'
    t 1 'angle-bracket redaction of an owner'  'D=/home/<owner>/src/jnext'
    t 1 'trailing content after the path'      'x = "/home/jorgegv/a" + "/b"'

    # ── Must NOT be caught ────────────────────────────────────────────────
    t 0 'the generic /home/user placeholder'   'cfg = "/home/user/.jnext/jnext.conf"'
    t 0 'the generic /Users/user placeholder'  'cfg = "/Users/user/Library/jnext.conf"'
    t 0 'a tilde-relative path'                'SD="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    t 0 'a repo-relative path'                 'source "$(dirname "$0")/../.." '
    t 0 'the derived form the fix introduced'  'REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"'
    t 0 '${CLAUDE_PROJECT_DIR} form'           '"command": "${CLAUDE_PROJECT_DIR}/.claude/hooks/x.sh"'
    t 0 'an absolute path that is not a home'  'PREFIX=/usr/local/share/jnext'
    t 0 '/opt, out of scope by design'         'CSPECT=/opt/cspect/CSpect.exe'
    t 0 'the word home in prose'               '# put the file in your home directory'
    t 0 'a URL containing /home/'              'url = "https://example.org/home/index.html"'
    t 0 'a relative path segment named home'   'p = "assets/home/banner.png"'
    t 0 '/home with no user component'         'ls /home'

    printf '\n  selftest: %d passed, %d failed\n' "$pass" "$fail"
    [ "$fail" -eq 0 ]
}

if [ "${1:-}" = "--selftest" ]; then
    echo "lint-hardcoded-paths selftest"
    selftest
    exit $?
fi

# The matcher proves itself on every run. `--selftest` existing as an opt-in
# mode is not enough: nothing invoked it, which is precisely the "available but
# not enforced" gap this lint exists to close for its own subject matter.
# 24 string matches, so the cost is nil.
if ! selftest >/dev/null 2>&1; then
    echo "ERROR: lint-hardcoded-paths matcher self-test FAILED; refusing to report a result." >&2
    selftest >&2 || true
    exit 2
fi

echo "Scanning tracked code/config for owner-absolute home paths..."
if scan "$PROJECT_DIR"; then
    echo "  none found."
    exit 0
fi

cat >&2 <<'EOF'

The paths above name one machine's home directory in a tracked, executable
file. They work for exactly one person. Derive them instead:

  shell   REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
  python  sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
  git     "$(git rev-parse --path-format=absolute --git-common-dir)/.."
  output  a command-line argument, or tempfile.gettempdir()

If a path genuinely cannot be derived, untrack the file (personal config is
out of scope) or add it to EXCEPTIONS in test/lint-hardcoded-paths.sh with the
reason written down.
EOF
exit 1
