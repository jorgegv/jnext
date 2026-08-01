#!/usr/bin/env python3
"""GH #196 Phase 1.1 — classify every `missing` matrix row.

Parses TRACEABILITY-MATRIX.md table-by-table (the Status column index is read
from each table's own header, because 'missing' also appears in the location
column), then asks git whether an assertion for that ID EVER existed **inside
the row's own subsystem**.

  orphan  = an assertion existed (in-scope) and was deleted -> drop the row
  planned = never existed in scope                          -> real backlog

Section scoping (fix, GH #196 Phase 1.1 review REJECT)
--------------------------------------------------------
The first cut of this script (commit 3fbd2229) ran `git log -S` for the bare
ID across the WHOLE `test/` + `src/` tree, with no check that the found call
actually belonged to the row being judged. This codebase deliberately reuses
short IDs across UNRELATED subsystem plan docs (GH #118, "cross-section ID
collision") — the exact same bug that forced the real extractor,
`refresh-traceability-matrix.pl`, to become section-scoped (its
`subsystem_span()` / `@SUBSYS` machinery). The unscoped search regressed
straight into that: six confirmed false ORPHAN verdicts, found by reading the
actual historical `check()`/`skip()` call text against each row's own claim:
  - `Z80-04`, `HK-06`, `NR02-07` — CTC+Interrupts rows flagged via NMI
                    Source Pipeline's OWN identically-named IDs
                    (test/nmi/nmi_test.cpp)
  - `CFG-08`      — CTC+Interrupts row flagged via NextREG's OWN `CFG-08`
                    (also a THIRD, live `CFG-08` exists in mmu_test.cpp)
  - `BOOT-SD-01`  — Memory/MMU row flagged via SD Card's OWN `BOOT-SD-01`
                    (test/sdcard/sdcard_test.cpp)
  - `RT-01`       — Rewind row flagged via an unrelated
                    `host_key_latch_test.cpp` assertion about keyboard input,
                    nothing to do with Rewind's own `RT-01`
Reviewer estimate: ~10% of the 300 "orphan" verdicts affected, concentrated
in CTC+Interrupts — confirmed: before the fix CTC+Interrupts scored 26
orphan / 5 planned out of 31 total `missing` rows; after, 8 orphan / 23
planned — 18 of its rows reclassify.

Fix: mirror `refresh-traceability-matrix.pl`'s `subsystem_span()` — a row is
judged only against source files named in a header (`## ` or `### `) inside
its own top-level `## ` subsystem span (from that header to the next `## `
header). That file set already includes nested `### Companion ...` suites
automatically, because a row is routinely LISTED in the parent's table but
ASSERTED in the companion file (the same shape GH #121 fixed on the STATUS
side of the real extractor — see that script's comments above
`refresh_section()`). A `-S` hit found ONLY outside that file set is NOT
evidence for this row — the ID belongs to a different subsystem that happens
to reuse the string — so it is treated exactly as "nothing found" (PLANNED),
never as ORPHAN.

This is the deliberately SAFER direction, not a coin flip: at worst it keeps
a PLANNED backlog row that was actually deleted once elsewhere in ITS OWN
scope under a file this script doesn't know about (e.g. a since-renamed
source file, invisible to a plain `-S -- <path>` without `--follow`) —
recoverable, and Phase 1.4's full manual triage re-reads every row anyway.
The opposite error — deleting a row GH #118 would have kept, because an
unrelated subsystem happened to reuse the ID — is not recoverable from the
matrix alone. No third "ambiguous" bucket was added on top of scoping: the
six confirmed cases all resolve cleanly to PLANNED once scoped, and a
separate bucket would only ever mean "PLANNED, but something with this name
also exists somewhere else" — not actionable information for THIS row.

The call syntax still matters, unchanged from the original: a bare `-S "ID"`
also fires for a comment mention, which would misclassify a
never-implemented row as a deleted one. Require the `check(`/`skip(`/`stub(`
call syntax, as before.
"""
import re, subprocess, sys, collections

MATRIX = 'doc/testing/TRACEABILITY-MATRIX.md'

def parse_spans(lines):
    """One dict per top-level `## ` section, in document order:
    {'header': text, 'files': set-of-repo-relative-paths}.

    `files` is every backtick-quoted `test/...` or `src/...` path named on
    that `## ` header line OR on any nested `### ` header line before the
    next `## ` line — the same unit refresh-traceability-matrix.pl's
    subsystem_span() resolves a row's companion suites against. Non-path
    backtick text (e.g. a `### The `VHDL file:line` column` doc heading in
    the `## Summary` preamble) is filtered by requiring a `test/`/`src/`
    prefix, so it can never leak in as a bogus pathspec.
    """
    spans = []
    cur = None
    for line in lines:
        if line.startswith('## '):
            cur = {'header': line[3:].strip(), 'files': set()}
            spans.append(cur)
        if cur is not None and (line.startswith('## ') or line.startswith('### ')):
            for p in re.findall(r'`([^`]+)`', line):
                if p.startswith('test/') or p.startswith('src/'):
                    cur['files'].add(p)
    return spans

def parse_rows(path):
    """Yield (section, test_id, files) for rows whose Status cell == 'missing'.

    `section` is the immediate (## or ###) header text — unchanged, used only
    for reporting. `files` is the owning top-level `## ` span's scope (see
    parse_spans), used to restrict the -S search in ever_existed().
    """
    lines = open(path, encoding='utf-8').read().splitlines()
    spans = parse_spans(lines)
    rows, section, hdr, span_idx = [], '(none)', None, -1
    for s in lines:
        if s.startswith('## '):
            span_idx += 1
        if s.startswith('#'):
            section = s.lstrip('#').strip()
            hdr = None
            continue
        if not s.lstrip().startswith('|'):
            hdr = None
            continue
        cells = [c.strip() for c in s.strip().strip('|').split('|')]
        low = [c.lower() for c in cells]
        # A header row defines the column layout for the table that follows.
        if 'status' in low and ('test id' in low or 'id' in low):
            hdr = {'status': low.index('status'),
                   'id': low.index('test id') if 'test id' in low else low.index('id')}
            continue
        if hdr is None:
            continue
        if set(cells[0]) <= set('-: '):      # separator row
            continue
        if hdr['status'] >= len(cells) or hdr['id'] >= len(cells):
            continue
        if cells[hdr['status']].lower() == 'missing':
            files = spans[span_idx]['files'] if span_idx >= 0 else set()
            rows.append((section, cells[hdr['id']], files))
    return rows

def ever_existed(tid, files):
    """True only if a check()/skip()/stub() CALL for this ID ever existed
    INSIDE `files` — the row's own subsystem span. See module docstring for
    why this must be scoped and not run over the whole tree.

    An empty `files` set (should not happen for a real matrix row; guards
    against a parsing edge case) searches nothing and reports "not found",
    the same safe default as a hit found only out of scope.
    """
    if not files:
        return False, []
    pat = r'(check|skip|stub)\s*\(\s*"%s"' % re.escape(tid)
    r = subprocess.run(['git', 'log', '--oneline', '--pickaxe-regex', '-S', pat,
                        '--', *sorted(files)], capture_output=True, text=True)
    return bool(r.stdout.strip()), r.stdout.strip().splitlines()

def selftest():
    """Ad hoc discriminative sanity checks (GH #196 phase 1.1 commit
    message), re-verified against the NEW scoped ever_existed():
      - a garbage ID scores 0 (nothing found)
      - a live ID (CMG-01, NextREG's own scope) scores 1
      - a confirmed in-scope orphan (CON-01, Memory/MMU's own scope — added
        in 1b4c54cd, deleted/moved out in 49e20cd2) scores 2
    Also re-verifies the six confirmed false positives are no longer ORPHAN
    once scoped to their OWN section.
    """
    ok = True
    def check_score(label, tid, files, want_min, want_max=None):
        nonlocal ok
        existed, commits = ever_existed(tid, files)
        n = len(commits)
        want_max = want_min if want_max is None else want_max
        good = want_min <= n <= want_max
        ok &= good
        print('%-42s %-16s -> %d commit(s) %s' %
              (label, tid, n, 'OK' if good else 'FAIL (want %d..%d)' % (want_min, want_max)))

    nextreg_files = {'test/nextreg/nextreg_test.cpp', 'test/nextreg/nextreg_integration_test.cpp'}
    mmu_files = {'test/mmu/mmu_test.cpp', 'test/mmu/mmu_integration_test.cpp'}
    ctc_files = {'test/ctc/ctc_test.cpp', 'test/ctc_interrupts/ctc_interrupts_test.cpp'}
    rewind_files = {'test/rewind/rewind_test.cpp'}

    check_score('garbage ID (NextREG scope)', 'ZZZZ-NOPE-99', nextreg_files, 0, 0)
    check_score('live ID CMG-01 (NextREG scope)', 'CMG-01', nextreg_files, 1)
    check_score('in-scope orphan CON-01 (MMU scope)', 'CON-01', mmu_files, 2)

    for label, tid, files in [
        ('Z80-04 (CTC scope, real owner=NMI)', 'Z80-04', ctc_files),
        ('CFG-08 (CTC scope, real owner=NextREG/MMU)', 'CFG-08', ctc_files),
        ('HK-06 (CTC scope, real owner=NMI)', 'HK-06', ctc_files),
        ('NR02-07 (CTC scope, real owner=NMI)', 'NR02-07', ctc_files),
        ('BOOT-SD-01 (MMU scope, real owner=SD Card)', 'BOOT-SD-01', mmu_files),
        ('RT-01 (Rewind scope)', 'RT-01', rewind_files),
    ]:
        existed, commits = ever_existed(tid, files)
        good = not existed
        ok &= good
        print('%-42s %-16s -> %s %s' %
              (label, tid, 'PLANNED' if not existed else 'ORPHAN (BUG)',
               'OK' if good else 'FAIL'))

    print('\nSELFTEST %s' % ('PASS' if ok else 'FAIL'))
    return ok

def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--selftest':
        sys.exit(0 if selftest() else 1)

    rows = parse_rows(MATRIX)
    print('missing rows parsed: %d (unique IDs: %d)' % (len(rows), len(set(t for _, t, _ in rows))))
    orphan, planned = [], []
    for section, tid, files in rows:
        existed, commits = ever_existed(tid, files)
        (orphan if existed else planned).append((section, tid, commits[0] if commits else ''))
    print('ORPHAN  (assertion existed in scope, deleted): %d' % len(orphan))
    print('PLANNED (never existed in scope, real backlog): %d' % len(planned))
    with open('/home/jorgegv/tmp/gh196-orphans.tsv', 'w') as f:
        for sec, tid, c in orphan:
            f.write('%s\t%s\t%s\n' % (sec, tid, c))
    with open('/home/jorgegv/tmp/gh196-planned.tsv', 'w') as f:
        for sec, tid, _ in planned:
            f.write('%s\t%s\n' % (sec, tid))
    print('\nper-section ORPHAN counts:')
    for sec, n in collections.Counter(s for s, _, _ in orphan).most_common():
        print('  %-46s %d' % (sec[:46], n))
    print('\nper-section PLANNED counts:')
    for sec, n in collections.Counter(s for s, _, _ in planned).most_common():
        print('  %-46s %d' % (sec[:46], n))

main()
