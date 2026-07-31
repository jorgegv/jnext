#!/usr/bin/env python3
"""GH #196 Phase 1.1 — classify every `missing` matrix row.

Parses TRACEABILITY-MATRIX.md table-by-table (the Status column index is read
from each table's own header, because 'missing' also appears in the location
column), then asks git whether an assertion for that ID EVER existed.

  orphan  = an assertion existed and was deleted  -> drop the plan-doc row
  planned = never existed in any commit           -> real backlog, keep the row
"""
import re, subprocess, sys, collections

MATRIX = 'doc/testing/TRACEABILITY-MATRIX.md'

def parse_rows(path):
    """Yield (section, test_id) for rows whose Status cell == 'missing'."""
    rows, section, hdr = [], '(none)', None
    for line in open(path, encoding='utf-8'):
        s = line.rstrip('\n')
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
            rows.append((section, cells[hdr['id']]))
    return rows

def ever_existed(tid):
    """True only if a check()/skip()/stub() CALL for this ID ever existed.

    -S on the bare quoted ID also fires for a mere comment mention, which would
    over-classify a never-implemented row as a deleted one — and deleting a plan
    row on that basis destroys a real backlog entry. Require the call syntax.
    """
    pat = r'(check|skip|stub)\s*\(\s*"%s"' % re.escape(tid)
    r = subprocess.run(['git', 'log', '--oneline', '--pickaxe-regex', '-S', pat,
                        '--', 'test/', 'src/'], capture_output=True, text=True)
    return bool(r.stdout.strip()), r.stdout.strip().splitlines()

def main():
    rows = parse_rows(MATRIX)
    print('missing rows parsed: %d (unique IDs: %d)' % (len(rows), len(set(t for _, t in rows))))
    orphan, planned = [], []
    for section, tid in rows:
        existed, commits = ever_existed(tid)
        (orphan if existed else planned).append((section, tid, commits[0] if commits else ''))
    print('ORPHAN  (assertion existed, deleted): %d' % len(orphan))
    print('PLANNED (never existed, real backlog): %d' % len(planned))
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
