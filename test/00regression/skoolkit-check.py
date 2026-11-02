#!/usr/bin/env python3
"""Reads what SKOOLKIT made of jnext's control files (GH #389), for the
`skoolkit-foreign-func` row. The row runs sna2skool.py / skool2asm.py itself;
this script only reads their output, so the verdict is skoolkit's and not ours.

    skoolkit-check.py labels <file.skool> <file.ctl>...
        every `@ $ADDR label=NAME` of the control files is `@label=NAME` in the
        skool file, directly before the instruction line at ADDR; and the block
        letter of every `c|b|u $ADDR` of the control files is the letter that
        the skool file's entry at ADDR starts with
    skoolkit-check.py asm <file.asm> <file.ctl>...
        every label of the control files is a line `NAME:` of the assembly
    skoolkit-check.py compare <a.ctl> <b.ctl>
        both control files hold the same `@ $ADDR label=NAME` lines and the same comment
        (the text of a B/C/S/T/W sub-block plus its `.` / `:` lines) at every address
    skoolkit-check.py comments <file.skool> <min>
        at least <min> instruction comments in the skool file (a `;` after an instruction)
    skoolkit-check.py z80n <file.skool> <min>
        at least <min> `DEFB $ED,$91,$xx,$yy` lines (a Z80N NEXTREG kept whole)
        and no `DEFB $ED,$91` cut to its two Z80 bytes

Prints `OK <summary>` and exits 0, or `FAIL <reason>` and exits 1.
"""
import re
import sys

LABEL = re.compile(r'^@ \$([0-9A-F]{4}) label=(\S+)$')
BLOCK = re.compile(r'^([cbu]) \$([0-9A-F]{4})$')
INSN = re.compile(r'^[a-zA-Z *]\$([0-9A-F]{4}) ')
SKOOL_LABEL = re.compile(r'^@label=(\S+)$')


def read_ctl(paths):
    labels, blocks = [], []
    for p in paths:
        with open(p, encoding='ascii') as f:
            for line in f:
                line = line.rstrip('\n')
                m = LABEL.match(line)
                if m:
                    labels.append((int(m.group(1), 16), m.group(2), p))
                m = BLOCK.match(line)
                if m:
                    blocks.append((int(m.group(2), 16), m.group(1), p))
    return labels, blocks


def fail(msg):
    print('FAIL ' + msg)
    sys.exit(1)


def cmd_labels(skool, ctls):
    labels, blocks = read_ctl(ctls)
    placed = {}      # (addr, name) -> True: `@label=name` directly before the line at addr
    letters = {}     # addr -> first character of its instruction line
    pending = []
    with open(skool, encoding='ascii') as f:
        for line in f:
            line = line.rstrip('\n')
            m = SKOOL_LABEL.match(line)
            if m:
                pending.append(m.group(1))
                continue
            if line.startswith('@'):
                continue
            m = INSN.match(line)
            if m:
                addr = int(m.group(1), 16)
                letters[addr] = line[0]
                for name in pending:
                    placed[(addr, name)] = True
                pending = []
            elif line.strip() == '' or line.startswith(';'):
                pending = []
    lost = [f'{n}@${a:04X}' for a, n, _ in labels if (a, n) not in placed]
    if lost:
        fail(f'{len(lost)} of {len(labels)} labels not in front of their line: ' + ' '.join(lost[:8]))
    wrong = [f'${a:04X} wants {c} has {letters.get(a, "nothing")}' for a, c, _ in blocks
             if letters.get(a, '').lower() != c]
    if wrong:
        fail(f'{len(wrong)} of {len(blocks)} block letters differ: ' + '; '.join(wrong[:6]))
    print(f'OK {len(labels)} labels and {len(blocks)} block letters as written')


def cmd_asm(asm, ctls):
    labels, _ = read_ctl(ctls)
    with open(asm, encoding='ascii') as f:
        names = {line.rstrip('\n')[:-1] for line in f if re.match(r'^\S+:$', line.rstrip('\n'))}
    lost = [n for _, n, _ in labels if n not in names]
    if lost:
        fail(f'{len(lost)} of {len(labels)} labels not emitted by skool2asm: ' + ' '.join(lost[:8]))
    print(f'OK skool2asm emitted all {len(labels)} labels')


def labels_and_comments(path):
    labels, comments = set(), {}
    cur = None
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            m = LABEL.match(line)
            if m:
                labels.add((int(m.group(1), 16), m.group(2)))
                cur = None
                continue
            m = re.match(r'^([BCSTW]) \$([0-9A-F]{4})(?:,\S+)?(?: (.*))?$', line)
            if m:
                cur = int(m.group(2), 16)
                if m.group(3):
                    comments[cur] = m.group(3)
                continue
            if line[:1] in ('.', ':') and cur is not None:
                comments[cur] = comments[cur] + '\n' + line[2:] if cur in comments else line[2:]
                continue
            cur = None
    return labels, comments


def cmd_compare(a, b):
    la, ca = labels_and_comments(a)
    lb, cb = labels_and_comments(b)
    if la != lb:
        fail(f'labels differ: only in a {sorted(la - lb)[:4]}, only in b {sorted(lb - la)[:4]}')
    if ca != cb:
        diff = sorted(set(ca.items()) ^ set(cb.items()))[:4]
        fail(f'comments differ: {diff}')
    if not ca:
        fail('no comments in either file: the comparison would prove nothing')
    print(f'OK {len(la)} labels and {len(ca)} comments equal')


def cmd_comments(skool, minimum):
    n = 0
    with open(skool, encoding='utf-8') as f:
        for line in f:
            if INSN.match(line) and re.search(r' ; \S', line):
                n += 1
    if n < minimum:
        fail(f'{n} instruction comments, wanted at least {minimum}')
    print(f'OK {n} instruction comments')


def cmd_z80n(skool, minimum):
    whole = cut = 0
    with open(skool, encoding='ascii') as f:
        for line in f:
            if re.search(r'DEFB \$ED,\$91,\$[0-9A-F]{2},\$[0-9A-F]{2}(\s|;|$)', line):
                whole += 1
            elif re.search(r'DEFB \$ED,\$91(\s|;|$)', line):
                cut += 1
    if cut:
        fail(f'{cut} NEXTREG instructions cut to DEFB $ED,$91')
    if whole < minimum:
        fail(f'{whole} whole NEXTREG DEFB lines, wanted at least {minimum}')
    print(f'OK {whole} NEXTREG instructions kept whole as DEFB $ED,$91,$xx,$yy')


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'labels':
        cmd_labels(sys.argv[2], sys.argv[3:])
    elif len(sys.argv) >= 4 and sys.argv[1] == 'asm':
        cmd_asm(sys.argv[2], sys.argv[3:])
    elif len(sys.argv) == 4 and sys.argv[1] == 'compare':
        cmd_compare(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == 'comments':
        cmd_comments(sys.argv[2], int(sys.argv[3]))
    elif len(sys.argv) == 4 and sys.argv[1] == 'z80n':
        cmd_z80n(sys.argv[2], int(sys.argv[3]))
    else:
        print(__doc__)
        sys.exit(2)
