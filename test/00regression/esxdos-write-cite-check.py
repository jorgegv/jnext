#!/usr/bin/env python3
# Checks every "[group] Snnnn" citation in test/esxdos_stub/esxdos_write_rows.cpp against
# the recording (test/fixtures/esxdos-write-probe/nextzxos.txt): the step must exist and be in
# that group (the [ro] group in the [ro] section, any other in [rw]). Steps are renumbered
# whenever the probe gains a step, and a citation that points at another group's step
# is evidence pointing at the wrong place. Usage: esxdos-write-cite-check.py [repo root]
# Exit 1 (listing each bad citation) when any is wrong.
import re, sys
W = sys.argv[1] if len(sys.argv) > 1 else '.'
rec = {'rw': {}, 'ro': {}}
sect = None
for ln in open(W + '/test/fixtures/esxdos-write-probe/nextzxos.txt'):
    if ln.startswith('['):
        sect = ln.strip()[1:-1]
        continue
    m = re.match(r'S([0-9A-F]{4}) .*?; (?:\[(\w+)\])?', ln)
    if m and sect:
        rec[sect][int(m.group(1), 16)] = m.group(2)
src = open(W + '/test/esxdos_stub/esxdos_write_rows.cpp').read()
bad = set()
n = 0
for m in re.finditer(r'\[(\w+)\]\s+((?:S[0-9A-F]{4}(?:\.\.S[0-9A-F]{4})?(?:,\s*)?)+)', src):
    grp = m.group(1)
    sec = 'ro' if grp == 'ro' else 'rw'
    for r in re.finditer(r'S([0-9A-F]{4})', m.group(2)):
        s = int(r.group(1), 16)
        n += 1
        if rec[sec].get(s) != grp:
            bad.add((grp, s, rec[sec].get(s)))
for g, s, a in sorted(bad):
    print('cites [%s] S%04X, which is [%s]' % (g, s, a))
print('citations checked: %d, bad: %d' % (n, len(bad)))
sys.exit(1 if bad else 0)
