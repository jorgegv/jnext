# z88dk ground-truth fixtures (GH #343, E23-12)

What the two z88dk toolchains actually produce for the same small program, committed
so the source-level-debugging adapters (epic #23) have an oracle: every expected
address, line and section in a test row comes from these files, never from the code
under test (design: `doc/design/SOURCE-LEVEL-DEBUGGING.md` section 2.3). CI has no
z88dk, hence committed outputs and a hash check (rows Z88FIX-01/02 in `source_map_test`).

## The two installs

| dir | commit | describe | zsdcc |
|-----|--------|----------|-------|
| `v2.4/` | `4d530b6eb779ad0f2a1d13c0bb670cf717477501` | v2.4 | 4.5.0 #15242 |
| `master/` | `39421a01cdfc9cb2011dbe3da38ef8fd1f5500b7` | v2.4-2695-g39421a01cd | 4.6.0 #16639 |

The master tree has one local modification (`include/_DEVELOPMENT/common/string.h`, the
`__80CC` `__builtin_strlen_fastcall` block removed); our sources do not use `strlen`.
Each `PROVENANCE` and each `build.log` (first line) record the commit. The master tools
print a stale banner (`v23856-580777436e-20251005`, from an old generated `config.h`):
trust the commit, not `zcc`'s banner.

## Regeneration

    bash test/fixtures/z88dk/generate.sh

Env overrides: `Z88DK_24`, `Z88DK_MASTER` (install roots; defaults
`~/src/spectrum/z88dk-original-2.4` and `...-latest`). The output is deterministic:
`git diff --stat` must be empty unless an install changed. When it did, commit the new
outputs and `MANIFEST.sha256` together and say which rows' values moved. The script
exits non-zero if a build that should succeed fails, or one recorded as failing starts to
build. `README.md` is not hashed; everything else (sources, script, outputs) is.

## Builds

All `zcc +zxn`, `LANG=C`. Plain builds compile `main.c util.c asmfn.asm`; `subdir`
moves `util.c` to `game/util.c`; banked builds compile `banked.c` with `--codeseg<SEG>`,
then link `bmain.c bankin.asm banked.o`. Link flags: `-m --list -subtype=nex -o prog -create-app`.
The exact commands are the `+ zcc ...` lines of each `build.log`.

| build | flags | result | covers |
|-------|-------|--------|--------|
| v2.4/sccz80-debug | `-clib=new -debug` | ok | G1-G5, G11 |
| v2.4/sdcc-SO0, sdcc-SO2 | `-clib=sdcc_iy -SOn --c-code-in-asm` | ok | G6, G7 |
| v2.4/sdcc-debug | `-clib=sdcc_iy -debug` | **fails rc=1** | G12 |
| v2.4/sccz80-page20, sccz80-bank10 | `-clib=new -debug`, `PAGE_20` / `BANK_10` | ok | G8 |
| v2.4/sccz80-banked, sdcc-banked | `-DUSE_BANKED_KW`, `PAGE_20` | ok | G9 |
| v2.4/classic-bank10 | `-clib=classic`, `BANK_10` | ok, no C lines | classic bank prefix |
| v2.4/classic-debug | `-clib=classic -debug` | **fails rc=1** | Z1 |
| v2.4/sccz80-subdir | `-clib=new -debug -I.`, `game/util.c` | ok | G10 |
| v2.4/sccz80-tape | `zcc +zx -clib=new -debug -m --list -o prog -create-app` | ok, a `.tap` | GH #344 stage 3 (tape programs) |
| master/sccz80-debug | `-clib=new -debug` | ok | G4 fixed |
| master/sdcc-debug | `-clib=sdcc_iy -debug` | ok | sdcc map lines, Z2 |
| master/80cc-debug | `-clib=new -compiler=80cc -debug` | ok | 80cc map |
| master/sdcc-banked | `-clib=sdcc_iy -debug -DUSE_BANKED_KW`, `PAGE_20` | ok | G9 |
| master/classic-bank10 | `-clib=classic`, `BANK_10` | ok, no C lines | classic page prefix |
| master/classic-debug | `-clib=classic -debug`, `BANK_10` | **fails rc=1** | Z1 |

`sccz80-tape` is the one `+zx` build (a 48K tape: `prog.tap` holds a BASIC loader and one
CODE block at $8000 that equals `prog_CODE.bin`; link flags without `-subtype=nex`).

Kept per successful build: `prog.map` (trimmed), every user `*.c.lis` / `*.asm.lis`,
`prog.nex` (`prog.tap` for the tape build), every `prog_<GROUP>.bin` (classic: `prog`). Not kept: `prog.lis` (crt0
listing), `.sym`, `.o`.

### Not producible (recorded, `build.log` only)

- `v2.4/sdcc-debug` (G12): `defc Gcounter0_00==.` syntax errors in 2.4's sdcc `-debug`
  (z88dk issue 3172; fixed on master).
- `v2.4/classic-debug`, `master/classic-debug` (Z1): `Unknown option2 --hardware-keyboard`.
  "Classic +zxn banked -debug: not producible."

## Findings (evidence in this tree)

- G1 sccz80 emits one `__C_LINE_18_` ($8C9D) though `main.c.lis` has four `C_LINE 18,`
  (`v2.4/sccz80-debug`).
- G2 80 `= $0000 ; addr, local, , <mod>, ,` lines in that map.
- G3 lines 14/15 share $8C92, 16/17 $8C95, 24/25 $8D12 (2.4 sccz80).
- G4 line 20 is $8CBE on 2.4 and $8CCD on master; master 24 $8D12, 25/26 $8D22.
- G5 `call l_debug_push_frame` in `main.c.lis`; the helper symbols are kept in the map.
- G6 2.4 sdcc maps have no `__C_LINE_`; the lines are `;main.c:N:` comments in the
  `.lis` (`--c-code-in-asm`).
- G7 `-SO0` and `-SO2` order the code differently (back jump `jp l_main_00105` under
  `;main.c:26:` vs `jr` after `;main.c:29:`); `_util_inc` appears twice ($8C48 main_c, $8CBB util_c).
- G8 newlib `PAGE_20` / `BANK_10`: `_banked_add = $C000`, NEX "Bank 10".
- G9 `call banked_call` + `defq _banked_add` in `bmain.c.lis`; `banked_call`, `l_jphl` kept.
- G10 `sccz80-subdir`: module `game_util_c`, `game/util.c.lis`.
- G11 `__CDBINFO__` count: 9 (2.4 sccz80), 9 (master sccz80), 24 (master sdcc), 11 (master 80cc), 0 (2.4 sdcc).
- G12 see above.
- Classic `BANK_10`: `_banked_add` is `$AC000` on 2.4 and `$14C000` on master
  (`memory_map.asm`: `ORG 0x140000 + CRT_ORG_BANK_10`; commit 72ea9d221b).
- Z1 (candidate upstream bug): `-debug` makes zcc forward ALIAS options to the compiler.
  `zcc +zx --vt100 -c util.c -o u.o` succeeds, `zcc +zx --vt100 -debug -c util.c -o u.o`
  fails with `Unknown option2 --vt100`, on both installs.
- Z2 (known upstream TODO): master sdcc labels header code compiled into a second module
  with the wrong line (`util_inc` in `util_c` shows as `util.h:9`, really line 7).

## Normalisation and trimming (in `generate.sh`)

Install paths become `z88dk/` (plain) and `z88dk_2f` (z80asm's symbol encoding); zcc temp
names `tmpXX<6 chars>` become `tmpXXXXXXXX`; the `Module compile time:` date becomes
`(normalised)`; the scratch directory is removed from logs. A map keeps a line iff its
module is one of ours, or it is a `__C_LINE_` / `__ASM_LINE_` record of any module (every
newlib `-debug` map has 26 in the crt0 module `zxn_crt_asm_m4`, `zxn_crt.asm.m4`: 3 in
`CODE` ($8000, $8009, $800C), 1 in `code_crt_main` ($8035), and 22 in data/bss sections), or it is `__<S>_head/_tail/_size` of a non-empty section, a
`CRT_ORG_*` of the CODE / BANK_10 / PAGE_20 / PAGE_21 sections, or one of
`l_debug_push_frame l_debug_pop_frame __debug_framepointer banked_call l_jphl`. The
`/tmp/tmpXX....i:2` marker lines of each module are always dropped (random names sort
randomly). Lines stay verbatim and in z88dk's order. Never hand-edit: widen the rule and regenerate.

## Licence

`src/` is ours. The `.nex` / `.bin` outputs contain z88dk runtime library code (Clarified
Artistic License, GPL-compatible), as every z88dk-built `.nex` in `demo/` does.
