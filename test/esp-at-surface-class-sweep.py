#!/usr/bin/env python3
"""Cross-check ESP-AT-SURFACE.md's NARRATIVE class claims against its TABLE.

`doc/design/ESP-AT-SURFACE.md` states each AT command's class twice on purpose:
once in the prose of §5.2 (class B) and §5.3 (class C), where the REASON lives,
and once in §5.4's per-category table, which is what the headline counts are
derived from. Two statements of the same fact can disagree, and one did:
`AT+SAVETRANSLINK` was filed under Flash/OTA in §5.3 — whose stated reason,
"there is no firmware image", does not apply to it at all — while §5.4 correctly
had it as B. The counts survived, because they come from §5.4; the prose did
not.

This script is that check, made re-runnable. It exists because the first sweep
was done by hand and reported as done, which makes it an assertion rather than
evidence: a reviewer could not re-run it, and neither could anyone else.

It is DELIBERATELY NOT WIRED INTO A GATE. It checks one design document against
itself, not the product, and adding an ungated-document checker to every test
run is a cost the project has not been asked for. Run it by hand when that
document's tables change:

    python3 test/esp-at-surface-class-sweep.py

Exit status is 0 when the two sides agree, 1 when they do not.
"""
import collections
import os
import re
import sys

DOC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   '..', 'doc', 'design', 'ESP-AT-SURFACE.md')

# `AT`, `ATE0`, `AT+CIPSTART`, `+++`. Group 2 captures a FORM suffix when the
# document names one (`AT+CIPMODE=1`, `AT+CIPMUX?`): a row that names a form is
# making a claim about THAT form only, which matters because one command is
# deliberately split across two classes.
CMD = re.compile(r'`(AT\+[A-Z0-9_]+|ATE\d?|AT|\+\+\+)(=[0-9?]|\?)?`')


def section(text, start, end=None):
    i = text.index(start)
    return text[i:text.index(end, i)] if end else text[i:]


def table_rows(text):
    """Yield (first_column, class_column) for every markdown row."""
    for line in text.splitlines():
        if not line.startswith('|') or line.startswith('|---'):
            continue
        cols = [c.strip() for c in line.strip('|').split('|')]
        if len(cols) >= 2:
            yield cols[0], cols[1]


def main():
    text = open(DOC, encoding='utf-8').read()

    # §5.4 is authoritative: it is what the headline counts are computed from.
    authoritative = collections.defaultdict(set)
    for first, cls in table_rows(section(text, '### 5.4 The complete table',
                                         '### 5.5 What could not')):
        letters = re.sub(r'[^A-Za-z]', '', cls)
        if letters.startswith('Apartial'):
            letters = 'A'          # `**A** *(partial)*` — A for one form only
        if letters not in ('A', 'B', 'C', 'done', 'doneset'):
            continue
        for cmd, _form in CMD.findall(first):
            authoritative[cmd].add('done' if letters.startswith('done') else letters)

    # §5.2 asserts B for everything it lists; §5.3 asserts C.
    narrative = collections.defaultdict(set)
    form_scoped = collections.defaultdict(set)
    for start, end, cls in (('### 5.2 Class B', '### 5.3 Class C', 'B'),
                            ('### 5.3 Class C', '### 5.4 The complete table', 'C')):
        for first, _ in table_rows(section(text, start, end)):
            for cmd, form in CMD.findall(first):
                if form:
                    # A claim about ONE FORM. `AT+CIPMODE=1` being B says
                    # nothing about `AT+CIPMODE=0`, which is A — the split is
                    # deliberate and documented in both sections.
                    form_scoped[cmd + form].add(cls)
                else:
                    narrative[cmd].add(cls)

    problems = []
    for cmd in sorted(set(narrative) & set(authoritative)):
        if not narrative[cmd] & authoritative[cmd]:
            problems.append('%s: prose says %s, the §5.4 table says %s'
                            % (cmd, sorted(narrative[cmd]), sorted(authoritative[cmd])))

    # A command named only in the prose has no authoritative class at all. That
    # is not always wrong — the RF family is described as a group and only
    # `AT+RFPOWER` exists in 2.x — so it is REPORTED, not failed.
    orphans = sorted(set(narrative) - set(authoritative))

    print('commands classed in the §5.4 table : %d' % len(authoritative))
    print('commands named in §5.2 / §5.3 prose: %d' % len(narrative))
    if form_scoped:
        print('\nform-scoped prose claims (a command split across classes on purpose;')
        print('not comparable to a whole-command row, so reported rather than checked):')
        for name in sorted(form_scoped):
            print('  %-20s prose says %s' % (name, sorted(form_scoped[name])))
    if orphans:
        print('\nnamed in prose only (reported, not an error):')
        for cmd in orphans:
            print('  %-20s prose says %s' % (cmd, sorted(narrative[cmd])))
    if problems:
        print('\nDISAGREEMENTS:')
        for p in problems:
            print('  ' + p)
        return 1
    print('\nno disagreements: every command classed in both places agrees')
    return 0


if __name__ == '__main__':
    sys.exit(main())
