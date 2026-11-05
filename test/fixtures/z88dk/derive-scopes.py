#!/usr/bin/env python3
# Derives the expected per-function scope tables of a z88dk -debug map (GH #433, plan 2.5): the
# golden of row Z88CDB-40 in test/debug/z88dk_map_test.cpp.
#
# INDEPENDENCE. It reads ONLY the raw lines of the given prog.map and uses no jnext code, so the
# golden is derived, not copied from the suite's own output. If a fixture is regenerated
# (generate.sh, a newer z88dk) or the equal-line rule changes, re-derive; never paste the output
# of z88dk_map_test.
#
# COMMAND (prints the block of ONE map, the text between its R"GOLD( and )GOLD" in the row):
#   python3 -I test/fixtures/z88dk/derive-scopes.py test/fixtures/z88dk/master/sccz80-cvars/prog.map
# Row Z88CDB-40's table is that command over its 12 maps:
#   for m in master/sccz80-cvars master/sdcc-cvars master/80cc-cvars v2.4/sccz80-cvars \
#            v2.4/sccz80-debug master/sdcc-debug master/sccz80-debug master/80cc-debug \
#            v2.4/sccz80-fc master/sccz80-fc master/sdcc-fc master/80cc-fc; do
#     python3 -I test/fixtures/z88dk/derive-scopes.py test/fixtures/z88dk/$m/prog.map; done
# Output: per function "name module ENTRY" then per scope entry "  ADDR L<line> K|U <level>/<sub>/<block>"
# (K = scope known, U = unknown); functions sorted by (name, module), entries by address; a
# function with no entry is not printed. All addresses are the 16-bit value of the map (every map
# of the table is unbanked: its code is in a rule-4 section, page 4 under the NEX).
#
# RULES (they mirror the dev guide, 7.9, "The scope table"; the code is fill_scopes()):
#  1. Records: the `addr` lines named __C_LINE_ or __ASM_LINE_ that have a section, whose section
#     does not begin (case-insensitive) with "data", "bss" or "smc", and whose location ends in
#     :<line> with line >= 1. Others (line 0, no section, data/bss/smc) are ignored.
#  2. Location: "<file>:<line>", or "<file>::<func>::<level>::<block>:<line>" with a TAB allowed
#     right before the ":<line>" (zsdcc). Level and block must be integers (a leading "-" allowed,
#     as from_chars does) else the location has NO scope.
#  3. Records at one address (same section/space, here one page): the HIGHEST line owns it. If
#     several records share that highest line they must agree on (module, func, tab, scope given,
#     level, block), else the entry is unknown (U, 0/0/0); the module/func of the first of them in
#     that key order stands for the group.
#  4. Function: the functions are the F: records of the __CDBINFO__ lines (F:G = the PUBLIC symbol
#     _name, F:F = the symbol _name in the record's module). An entry belongs to the function of
#     the SAME module with the greatest entry address <= the entry's address (ties: the smaller
#     name); no such function: the entry is dropped.
#  5. Scope: if the owner has a scope and the group agrees: zsdcc's form when func is "x" and (a TAB
#     is present or the function at the address is not named "x"): level = n // 10000,
#     sub = n % 10000, block as written, known. Otherwise (sccz80, 80cc) known only if func is
#     the function at the address: level = n, sub 0, block as written.
#
# EXTENTS (GH #434, plan 2.1.1; row Z88CDB-41/42): `--extents` prints each function's extent instead.
#   python3 -I test/fixtures/z88dk/derive-scopes.py --extents test/fixtures/z88dk/master/sccz80-cross/prog.map
# Output: per function "name module ENTRY" (functions sorted by name, module), then one line per piece
# "  LO-HI +OFFSET" (all hex; HI exclusive) or "  unknown" when nothing bounds the function.
#  E1. Bound: the lowest map value above the function's entry, in the SAME section, of: the entry of
#      another function; an `addr` symbol that is not a debug record, or a line record (rule 1), of
#      ANOTHER module; the section's `__<section>_tail` const.
#  E2. Pieces: [entry, bound) split at the 8 KB boundaries of the 16-bit value (every map here is
#      unbanked, so the map value IS the logical address); OFFSET is the bytes from the entry.
import re, sys, collections

def unesc(s):
    o = ''; i = 0
    while i < len(s):
        if s[i] == '_': o += chr(int(s[i+1:i+3], 16)); i += 3
        else: o += s[i]; i += 1
    return o

def parse_loc(t):
    m = re.match(r'^(.*):(\d+)$', t)
    if not m: return None
    pre, line = m.group(1), int(m.group(2))
    tab = pre.endswith('\t')
    if tab: pre = pre[:-1]
    d = dict(line=line, tab=tab, given=False, func='', lv=0, bl=0)
    if '::' in pre:
        f, rest = pre.split('::', 1); parts = rest.split('::'); d['func'] = parts[0]
        num = lambda x: re.fullmatch(r'-?\d+', x) is not None
        if len(parts) >= 3 and num(parts[1]) and num(parts[2]):
            d['given'] = True; d['lv'] = int(parts[1]); d['bl'] = int(parts[2])
    return d

def read(mapfile):
    syms = []; cdb = []; recs = []; consts = {}
    for ln in open(mapfile, encoding='utf-8', errors='replace'):
        ln = ln.rstrip('\n')
        if ' ; ' not in ln: continue
        left, right = ln.split(' ; ', 1)
        name = left.split()[0]; val = int(left.split('$')[1], 16)
        f = right.split(', ', 5); typ, scope, _, mod, sec = f[0], f[1], f[2], f[3], f[4]
        loc = f[5] if len(f) > 5 else ''
        if typ == 'const': consts[name] = val
        if name.startswith('__CDBINFO__'): cdb.append((unesc(name[11:]), mod)); continue
        if name.startswith('__C_LINE_') or name.startswith('__ASM_LINE_'):
            if sec and typ == 'addr' and not re.match(r'^(data|bss|smc)', sec.lower()):
                d = parse_loc(loc)
                if d and d['line'] > 0: d.update(addr=val, mod=mod, sec=sec); recs.append(d)
            continue
        if typ == 'addr': syms.append((name, scope, mod, val, sec))
    fns = []
    for t, mod in cdb:
        m = re.match(r'F:(G|F)([^$]*)\$([^$]*)\$', t)
        if not m: continue
        kind, name = m.group(1), m.group(3)
        c = [s for s in syms if s[0] == '_' + name and ((kind == 'G' and s[1] == 'public') or (kind == 'F' and s[2] == mod))]
        if len(c) != 1: print('FN-AMBIG', name, len(c), file=sys.stderr); continue
        fns.append((name, c[0][2], c[0][3], c[0][4]))
    return syms, recs, consts, fns

def tables(mapfile):
    syms, recs, consts, fns = read(mapfile)
    byaddr = collections.defaultdict(list)
    for r in recs: byaddr[r['addr']].append(r)
    out = {}
    for a in sorted(byaddr):
        g = byaddr[a]; top = max(r['line'] for r in g); own = [r for r in g if r['line'] == top]
        key = lambda r: (r['mod'], r['func'], r['tab'], r['given'], r['lv'], r['bl'])
        agree = len(set(key(r) for r in own)) == 1
        o = sorted(own, key=key)[0]
        cand = [f for f in fns if f[1] == o['mod'] and f[2] <= a]
        if not cand: continue
        fn = sorted(cand, key=lambda f: (-f[2], f[0]))[0]
        known = False; lv = sub = bl = 0
        if agree and o['given']:
            if o['func'] == 'x' and (o['tab'] or fn[0] != 'x'):
                known = True; lv = o['lv'] // 10000; sub = o['lv'] % 10000; bl = o['bl']
            elif o['func'] == fn[0]:
                known = True; lv = o['lv']; bl = o['bl']
        out.setdefault((fn[0], fn[1], fn[2]), []).append((a, top, known, lv, sub, bl))
    lines = []
    for (n, m, e) in sorted(out, key=lambda k: (k[0], k[1])):
        lines.append('%s %s %04X' % (n, m, e))
        for a, top, k, lv, sub, bl in out[(n, m, e)]:
            lines.append('  %04X L%d %s %d/%d/%d' % (a, top, 'K' if k else 'U', lv, sub, bl))
    return '\n'.join(lines) + '\n'

def extents(mapfile):
    syms, recs, consts, fns = read(mapfile)
    lines = []
    for name, mod, entry, sec in sorted(fns, key=lambda f: (f[0], f[1])):
        bounds = [f[2] for f in fns if f[3] == sec and f[2] > entry]
        bounds += [s[3] for s in syms if s[4] == sec and s[2] != mod and s[3] > entry]
        bounds += [r['addr'] for r in recs if r['sec'] == sec and r['mod'] != mod and r['addr'] > entry]
        tail = consts.get('__%s_tail' % sec)
        if tail is not None and tail > entry: bounds.append(tail)
        lines.append('%s %s %04X' % (name, mod, entry))
        if not bounds: lines.append('  unknown'); continue
        end = min(bounds); at = entry
        assert end <= 0x10000, 'banked or 24-bit map: not derived here'
        while at < end:
            hi = min(end, (at & 0xE000) + 0x2000)
            lines.append('  %04X-%04X +%X' % (at, hi, at - entry))
            at = hi
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    args = sys.argv[1:]
    fn = tables
    if args and args[0] == '--extents': fn = extents; args = args[1:]
    for p in args: print(fn(p), end='')
