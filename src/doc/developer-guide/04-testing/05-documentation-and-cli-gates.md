# 4.5 The documentation and CLI gates

Several documents in this repository are **generated and then committed**: the
man page, `USAGE.md`, the rendered user guide, and the rendered developer guide
with its Graphviz figures. Committing generated output buys something real — a
fresh clone can read all of it with no toolchain installed — but it also
creates a silent lie waiting to happen, because nothing else in the build can
notice when a committed output stops matching the source it came from.

`make docs-check` is what closes that hole, and it is a prerequisite of **both**
`make unit-test` and `make regression`, so a stale document fails the test run
itself rather than waiting for CI or a reviewer to spot it.

## The four parts

`docs-check` runs each part as its own target and aggregates the results, so
that one stale document never hides another and each one prints its own
remediation line:

- **`docs-man-check`** regenerates `doc/man/jnext.1` and `USAGE.md` from
  `doc/man/jnext.1.md` with pandoc, and diffs them. The fix is `make docs-man`,
  then commit.
- **`docs-userguide-check`** rebuilds the user guide from `src/doc/user-guide`
  with `mkdocs build --strict` and diffs the resulting tree against
  `doc/user-guide`. The fix is `make docs-userguide`, then commit.
- **`docs-devguide-check`** does the same for `src/doc/developer-guide` against
  `doc/developer-guide`, **and** additionally re-renders every
  `diagrams/*.dot` to SVG and diffs those too. Checking only the rendered site
  would let a hand-edited SVG through, because mkdocs copies it across
  unchanged. The fix is `make docs-devguide`, then commit both.

- **`docs-devguide-cites-check`** verifies the developer guide's
  code cites. It is not a freshness check: no renderer is involved. See
  [the cite rule](#the-developer-guide-cite-rule) below.

On a machine that has no pandoc, mkdocs or graphviz the relevant part
**fails**, there and in CI alike (the old `$CI` split is gone: CI and local do
the same thing), because a check that cannot run is not a pass.

## The developer-guide cite rule

The guide cites code as a file name, a colon and a line number, and every such
cite of a file in this repository is checked by `test/lint-devguide-cites.pl`
(a Perl script like the other lints; `make docs-devguide-cites-check` runs it,
and `docs-check` runs that). Before it existed one merge shifted 17 cites of
chapter 7 and only a reviewer's script noticed. The rule, in full:

- **Every cite is anchored to a symbol.** The nearest backticked identifier
  before the cite in the same sentence (a function, class, member, constant or
  test ID; `Class::name` and `name()` count, the last component is the symbol)
  must appear as a whole word on the cited line, or, for a range, on a line of
  the range. There is **no +-1 tolerance**: a cite one line off fails.
  A sentence break (a full stop followed by a capital) ends the sentence, so the
  symbol of the previous sentence does not anchor this one. A cite inside a
  parenthesis is attached to the symbol in **that parenthesis**, or to the one
  that ends right where it opens (``` `sym()` (`file:N`) ```, also through `**`
  markup); a symbol elsewhere in the sentence never anchors it, however well it
  happens to match the line. A cite with no
  such symbol fails as *unanchored*: add the symbol, in backticks, before it.
- **The forms** are a file token (any path whose last component has an
  extension, or `Makefile`; a bare name when it is unique in the repository,
  else a path that ends the same way) with `:N` or `:N-M`, the colon inside or
  outside the code span; a bare backticked `:N` or `:N-M`, which binds to **the
  nearest file named earlier in the same paragraph** (a whole table counts as
  one paragraph; a markdown link target is not a mention) and **fails** when
  none is named there, or when that file is an example-program name or one of
  the declared external files: name the file again in every cite; the prose
  forms *line N in* or *of* a file, a file followed by *(line N)* or *, line N*.
  A comma list of lines fails: write one cite per line.
- **Excerpts.** A "lines" phrase after a repository file, in a paragraph that is
  directly followed by fenced blocks, introduces an excerpt, and the rows of
  those blocks must equal the cited lines (a run of `X` matches a temp name and
  `<TAB>` is a tab); a block of `N:`-prefixed rows after "the number is the
  line" is compared line by line the same way. Without a fence after it the
  phrase is an ordinary cite and needs an anchor.
- **Resolution** goes in order: a *bare* example-program name (`main.c`,
  `util.c`, `util.h`, `asmfn.asm`, `main.bas`, `equlabels.asm`: positions in the
  debugged program, not code) is out of scope; a token that matches repository
  files is checked (several matches fail, naming them), and is **never**
  external, so the repository's own `test/fixtures/z88dk/` files and a future
  `support.cpp` are checked; only a token that matches nothing in the
  repository and belongs to a declared external set is out of scope, and any
  other token fails. A file token that does not resolve is still the nearest
  file of a bare `:N`, a "lines" phrase or an excerpt pointer, and those fail on
  it, so a deleted or renamed file cannot go quiet (the exceptions: *e.g.*, *i.e.*
  and a token that names a repository directory are no file). A table row ends
  the sentence for the anchor search.
- **Out of scope by declaration**, listed in the script's header and nowhere
  else: the VHDL (a different repository), and the z88dk, sjasmplus, Boriel,
  NextBuild and NextZXOS sources the format chapters cite, the sjasmplus manual,
  and the pinned foreign tools the export chapter cites (the skoolkit files
  `__init__.py`, `ctlparser.py` and `sna2skool.py`, and the Ghidra script
  `ImportSymbolsScript.py`), when the repository has no such file. Fenced code blocks are sample data and are not read for
  cites (only as the excerpt above), but a fence that is never closed fails, and
  a closing fence must be the opening character, at least as long.
- **Quoted text anchors too.** A backticked double-quoted string, such as
  `"unknown memory '"`, anchors a cite when its text between the quotes appears
  verbatim (case-sensitive) on a cited line: a parser message identifies its own
  line. The text must be at least 4 characters with a letter or digit among them,
  so `" "` or `"y"` cannot anchor almost any line.
- **The output** is one line per failure, `page:line: cite | anchor | what the
  cited line holds`, then a count; the exit status is non-zero on any failure.

**At a merge, the gate is not enough** (a merge-time procedure, not a gate). The
rule cannot see a cite whose number did not move while its code did, when the
symbol is still somewhere in the cited range or a weak anchor happens to match the
line now there: ten cites after one merge, and a plain `text` that matched a
comment. After merging code into a branch whose guide has cites, map every cite
whose number is unchanged through the code diff and re-point the ones whose cited
lines moved:

1. `JNEXT_CITES_DUMP=FILE perl test/lint-devguide-cites.pl` appends one
   `OK<TAB>page:line<TAB>file:N[-M]` row per repository cite it checked; run it on
   the tree before the merge and on the merged tree.
2. For each cite present, with the same text, on the same page in both dumps, take
   `git diff -U0 BEFORE HEAD -- FILE` and translate the cited lines: an old line
   above a hunk keeps its number, one below it moves by the hunk's added minus
   removed lines, one inside a hunk was edited.
3. Any cite whose translated lines differ from its number is stale: re-point it,
   to the epic's number when the other side already did.

Take care in conflict resolution that "keep my side" does not discard the other
side's corrected numbers.

When a cited file changes, run the check and re-point the cites it reports;
re-point the cite, never the anchor, unless the sentence is wrong. The script's
rows are `HS-102`..`HS-156` of `make harness-selftest`.

## The renderer fingerprint

pandoc, mkdocs-material and graphviz all emit byte-different output across
their own versions for identical input. A byte-diff against outputs that were
generated by a different toolchain therefore reports a **version gap** as
staleness — which is exactly what broke CI once, when it ran a stock Ubuntu
runner's pandoc 3.1.3 against outputs made with 3.7.0.2, with nothing in the
failure message to hint that the version rather than an edit was the cause.

So each generator writes a fingerprint file next to its output, recording the
tool versions that produced it, and each check compares fingerprints before
anything else. When they do not match, the check **fails and says so**, naming the two
fingerprints: a freshness it cannot verify is not a pass (owner decision,
2026-10-06; it used to skip the byte-diff). The reference platform is the Fedora
container CI uses; on another distribution CI is the verdict. CI sidesteps the whole
class by running in a Fedora container that matches the maintainer's own
distribution, instead of pinning tool versions one at a time.
`SOURCE_DATE_EPOCH` is pinned for a related reason: mkdocs stamps
`sitemap.xml.gz` with the build date, so an unchanged guide rendered tomorrow
would otherwise differ from the same guide rendered today.

## What `docs-check` does NOT prove

It proves that the generated outputs match `jnext.1.md`. **It proves nothing
about whether `jnext.1.md` describes the CLI that `src/main.cpp` actually
parses.**

That seam was held together by developer diligence alone, and
it failed twice with every gate green. The first time, five flags were entirely
undocumented. The second time, a released man page confidently described a
wrong scale range, two GUI menus that do not exist, and a status-bar indicator
that does not exist. Both were found by reading the running product while
writing the user guide, and not by any check.

## `make cli-check`

The fix was to stop treating the flag set as control flow.
`src/core/cli_options.h` now holds it as a **data table** — one row per accepted
spelling, carrying its arity, a documentation class and its `OptId` — and
`main.cpp` dispatches from that table, which means a flag that is not in the
table cannot be parsed at all. `cli_options_test` then diffs the table against
the OPTIONS section of `doc/man/jnext.1.md` in both directions:

- `CLI-DOC-01` catches **implemented but undocumented**: every
  documented-class spelling must have a man page entry.
- `CLI-DOC-02` catches **documented but unimplemented**: every man page OPTIONS
  entry must be a spelling the table accepts.
- `CLI-DOC-03` catches an **arity disagreement**: the value arguments the
  parser consumes must equal the metavars the man page shows.
- `CLI-DOC-00` guards the scrape itself, so that a broken parse of the man page
  cannot make `CLI-DOC-02` vacuously true.
- `CLI-SRC-01` fails if `main.cpp`'s parse loop reintroduces a hand-rolled
  `arg == "--flag"` comparison. That is the pattern that made the flag set
  unenumerable in the first place, and it would re-open the gap invisibly,
  because such a flag appears neither in the table nor in this check.
- `CLI-BIN-01` runs the real binary end to end, proving that the table drives
  the actual parser rather than only this test. It, and every row that starts
  the built `jnext` as a child process (among them `CLI-BIN-04`, the binary's
  `--help` against the table), is in `cli_options_native_test`, a second suite
  because the WebAssembly build starts no process.
- `CLI-NUM-05` gives every option that takes a value the argument `x` and fails
  on any crash, so a future flag parsed with a throwing call is caught without
  anyone listing it.
- `CLI-NUM-06` checks each numeric option refuses a bad value by name and value,
  accepts its bounds, and that its case table covers every numeric option in
  `cli::OPTIONS`.

The two suites run both as `make cli-check` (which runs `cli_options_test` and
then `cli_options_native_test`), a prerequisite of `make regression`, and as
declared rows of `test/unit-tests.conf`. It hangs off `unit-test-build`
rather than off `docs-check`, because the table is C++ and `docs-check`
deliberately needs no compiler.

## Declaring a deliberate exception

Exceptions are declared **in the table**, never as a grep exclusion inside the
checker. `Doc::UndocumentedAlias` marks a flag that is intentionally absent
from the man page, with the reason written in a comment on its row; today the
only one is `--sd-card`, a back-compatibility alias for `--sdcard` that should
not be advertised. `CLI-DOC-05` then asserts that such a flag really is absent,
because a deliberate exception that quietly became documented is drift too.
`Doc::ShortAlias` covers short forms that are documented inline on their long
form's entry.

Adding a flag is therefore four edits: an `OptId`, a table row, a `case` in
`main.cpp` — which the compiler enforces through `-Wswitch` — and an entry in
`doc/man/jnext.1.md`. Skip the last one and `cli-check` fails.

## The table of `jnext.conf` keys

`src/platform/config_keys.h` is the one place the keys of `jnext.conf` are
named: section, key, value type, bounds, accepted words, the dedicated flag with
the same effect, and the frontends where it takes effect. It is Qt-free, so the
SDL-only and headless builds can parse the same names. `AppConfig` and
`DebuggerWindow` read their section and key names, bounds and word lists from
it; the one literal left is the legacy root key `config_version` that
`normalise_legacy_layout()` migrates. `config_keys::parse_assignment()` turns a
typed `SECTION.KEY=VALUE` into the canonical text `AppConfig::save()` writes.

**Adding a Preferences field is adding a row to that table.** The rule is held
by tests, not by review: `AC-94` saves a fully populated `AppConfig` and fails
when the file holds a key with no `AppConfig`-owned row, or a row is not written;
`AC-95` and `AC-96` hold the setter and the file reader to one meaning of every
value; `DWK-01` does the same for the keys `DebuggerWindow` writes. A key that
is not a setting (the remembered geometry, the recent-files history, derived
names, the file format version) is declared in the table with its reason, and
`SETK-03` pins that list.

### `--set`, the other half of the same table

`--set SECTION.KEY=VALUE` is parsed by
`config_keys::parse_assignment()` and applied in `main.cpp` by one lambda,
`apply_assignment`, whose `switch (a.id)` over `config_keys::KeyId` has **no
`default`**: the `jnext` target builds with `-Werror=switch`, so a row added to
the table with no arm there is a compile error. The man page's `## Settings for
\--set` subsection is diffed against the table both ways (`CLI-SET-01` lists
every settable key and every debugger action id, `CLI-SET-02` finds nothing
else), and `--help` prints the same list from the table (`CLI-SET-03`). A
dedicated flag that has a key (`flag` in the row, checked by `SETK-04`) asks the
table for its value too, so the flag and `--set` accept the same values
(`CLI-SET-12`).

The Qt session must never save a command-line value. `main.cpp` merges `--set`
into its own copy of the saved values (`merge_cli_assignments`, `AC-97`), which
is never saved; `MainWindow` keeps a *session* copy of the four settings it
reads live (`PA-29`..`PA-32`), the debugger window keeps `attach_saved_` apart
from `attach_enabled_` (`DWK-02`), and the debugger keymap of the run is
`set_session_debug_keys` (`DWK-03`).

## The residual gap, stated plainly

`cli-check` covers the **flag set**, not the **prose**. The wrong scale range
and the phantom status-bar indicator were both narrative text in the man page,
and nothing checks narrative text. The same is true of every page in this
guide: a stale paragraph here is the same class of defect as a stale man page,
with the difference that no gate can detect it. Keep reading the running
product.
