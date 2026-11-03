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
| v2.4/sdcc-SO0, sdcc-SO2 | `-clib=sdcc_iy -SOn --c-code-in-asm` | ok | G6, G7; `sdcc-SO2` also keeps the CRT listing (G13-G15) |
| v2.4/sdcc-debug | `-clib=sdcc_iy -debug` | **fails rc=1** | G12 |
| v2.4/sccz80-page20, sccz80-bank10 | `-clib=new -debug`, `PAGE_20` / `BANK_10` | ok | G8 |
| v2.4/sccz80-banked, sdcc-banked | `-DUSE_BANKED_KW`, `PAGE_20` | ok | G9 |
| v2.4/sccz80-banked-run | `-clib=new -debug -DUSE_BANKED_KW -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000`, `PAGE_20` | ok | G9 runnable: Into/Over/Out across `banked_call` (GH #345) |
| v2.4/sccz80-banked-crt, master/sccz80-banked-crt | the `-run` flags, `KEEP_CRT` | ok | `code_crt_init` shared with the banking driver (GH #426 F4) |
| v2.4/classic-bank10 | `-clib=classic`, `BANK_10` | ok, no C lines | classic bank prefix; keeps the classic CRT listing (G19-G21) |
| v2.4/classic-debug | `-clib=classic -debug` | **fails rc=1** | Z1 |
| v2.4/sccz80-subdir | `-clib=new -debug -I.`, `game/util.c` | ok | G10 |
| v2.4/sccz80-incmid, master/sccz80-incmid, master/80cc-incmid | `incmid.c` only: `-clib=new [-compiler=80cc] -debug` | ok | line-0 records in a code section (G22) |
| v2.4/sccz80-intarg, master/sccz80-intarg, master/80cc-intarg | `intarg.c` only: `-clib=new [-compiler=80cc] -debug` | ok | GH #349: an int parameter, so sccz80 calls `l_gintspsp` (`EX (SP),HL; JP (HL)`, a return with no RET); 80cc emits no such call for it (the unchanged control) |
| v2.4/sccz80-longsw, master/sccz80-longsw, master/80cc-longsw | `longsw.c` only: `-clib=new [-compiler=80cc] -debug` | ok | GH #349 R1': `long` subtraction (sccz80 calls `l_long_sub`: `POP IX ... JP (IX)`) and a 6-case `switch` (`l_case`: `POP HL` as the case table, `JP (HL)`); 80cc inlines the subtraction (control) but calls `l_case` |
| v2.4/sccz80-tape | `zcc +zx -clib=new -debug -m --list -o prog -create-app` | ok, a `.tap` | GH #344 stage 3 (tape programs) |
| master/sccz80-debug | `-clib=new -debug` | ok | G4 fixed |
| master/sdcc-debug | `-clib=sdcc_iy -debug` | ok | sdcc map lines, Z2; also keeps the CRT listing (G13-G15) |
| master/80cc-debug | `-clib=new -compiler=80cc -debug` | ok | 80cc map |
| master/sdcc-banked | `-clib=sdcc_iy -debug -DUSE_BANKED_KW`, `PAGE_20` | ok | G9 |
| master/sdcc-banked-run | `-clib=sdcc_iy -debug -DUSE_BANKED_KW` + the same two pragmas, `PAGE_20` | ok | G9 runnable (GH #345) |
| v2.4/sccz80-dot, sccz80-dotx, sdcc-dot; v2.4-dotn/sccz80 | `dot.c dutil.c`, `-subtype=dot` / `dotx` / `dotn`, `-clib=new -debug` (sdcc-dot: `-clib=sdcc_iy -SO2 --c-code-in-asm`: lines in the `.lis` only) | ok, `PROG` (`PROG.X` for dotx) | GH #351 dot commands; dotn lives in `v2.4-dotn/` so that Z88CN-10's walk over `v2.4/` and `master/` (every map shows a C name) is not tripped by its refused C symbols |
| master/sccz80-dot, sccz80-dotx, sdcc-dot, 80cc-dot | `-clib=new -debug` / `-clib=sdcc_iy -debug` / `-clib=new -compiler=80cc -debug` | ok | GH #351 |
| master/classic-bank10 | `-clib=classic`, `BANK_10` | ok, no C lines | classic page prefix; keeps the classic CRT listing (G19-G21) |
| master/classic-debug | `-clib=classic -debug`, `BANK_10` | **fails rc=1** | Z1 |

The dot builds (`dot:<subtype>` in `generate.sh`) compile `dot.c dutil.c` with `-subtype=dot|dotx|dotn`
instead of `-subtype=nex`. Classic dot is not built: `-debug` fails (Z1) and without it there are no lines.
They also keep `PROG` / `PROG.X` (the file NextZXOS runs), and their trimmed map keeps `startupoffset`,
`__crt_org_code` and the `__<G>_head/_tail/_size` of every group that has a `prog_<G>.bin` (a dotx's
`__MAIN_head` has size 0 and the old rule dropped it).

`sccz80-tape` is the one `+zx` build (a 48K tape: `prog.tap` holds a BASIC loader and one
CODE block at $8000 that equals `prog_CODE.bin`; link flags without `-subtype=nex`).

Kept per successful build: `prog.map` (trimmed), every user `*.c.lis` / `*.asm.lis`,
`prog.nex` (`prog.tap` for the tape build), every `prog_<GROUP>.bin` (classic: `prog`). `prog.lis` (the crt0
listing, 12-18K lines) is kept for exactly four builds, `v2.4/sdcc-SO2`, `master/sdcc-debug`, and both `classic-bank10`
(`KEEP_CRT=1` in `generate.sh`, GH #348); not kept for the others, nor `.sym`, `.o`.

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
- G13 `prog.lis` lists the false branches of `IF` too, with no bytes: `__stdio_closed_file_list:` is at
  m4 line 9207 (with bytes) and again at 9266. The map names the defining line (9207), so a label
  matches only at that line. The m4 file's lines are 1..9689 with none missing (2.4 and master).
- G14 z80asm defines `__ASM_LINE_<n>_<file>` for a label only while the location is not C source:
  master sdcc has `__ASM_LINE_224/226_main.c` for `_counter` / `_table`, which sit before the first
  `C_LINE`; those numbers are lines of the generated assembly, not of `main.c`.
- G15 `code_crt_init` (38 bytes) and `code_crt_return` (14) have no labels; in each the CRT's listed
  piece is the whole section (`__<s>_tail - __<s>_head`), so it starts at `__<s>_head`.
- G16 sccz80 without `-debug` emits `C_LINE` only at function starts (`v2.4/classic-bank10`: every
  `banked.c` instruction is line 6).
- G17 at `-SO0` the back jump `jp l_main_00105` is at $8CBF, 3 bytes; `util.h:7` has three starts in
  `v2.4/sdcc-SO2`: $8C48 (main_c copy), $8CBB (util_c copy) and $8CD6 (`inc a` inlined into `util_twice`).
- G19 z80asm keeps the LAST `MODULE`: the classic CRT listing has zcc's `MODULE zxn_crt0_asm` (line 2) and
  then `MODULE zxn_crt0` (line 13); the map names `zxn_crt0` (16 `addr` symbols, both installs).
- G20 a false `IF` branch is listed exactly like a taken one, only without bytes, so the listing cannot say which
  branch was assembled; MACRO bodies are listed once, at their definition (`MACRO name`, `name MACRO`,
  `name: MACRO`, to `ENDM`), and a macro invocation row carries all the bytes the expansion emitted, even those
  that went to another section; REPT bytes sit on the `ENDR` row. The classic CRT defines
  `MACRO setup_static_fp` whose body switches section.
- G21 a section's listed addresses start at 0 and are continuous (ORG changes nothing; ALIGN and DEFS list their
  fill). With MACRO bodies skipped this holds for every listing in the committed fixtures and the z88dk libraries'
  own. The classic CRT then places exactly: 47 rows, every instruction's first byte equal to `prog`.
- G22 sccz80 and 80cc write `C_LINE 0,"<file>::<func>::0::1"` on entering a file (an `#include` after a function);
  inside a code section z80asm turns it into `__C_LINE_0_... ; addr, ..., <file>::<func>::0::1:0`. Line 0 is
  never a source line (`incmid` builds: $8C76 on 2.4 and master sccz80, $8C72 on 80cc).
- G18 a listing's `MODULE` line names the file alone (`util_c`), the map the path it was given
  (`game_util_c`) in `sccz80-subdir`.
- Classic `BANK_10`: `_banked_add` is `$AC000` on 2.4 and `$14C000` on master
  (`memory_map.asm`: `ORG 0x140000 + CRT_ORG_BANK_10`; commit 72ea9d221b).
- G23 (GH #351) dot commands. `startupoffset` is `$0100` (dot), `$0200` (dotx) or `$0300` (dotn): the
  `-startupoffset=` of `lib/config/zxn.cfg` (:34-39 on master, :35-40 on 2.4), in the map as a `const`
  of the crt module, absent from a `-subtype=nex` map; in newlib and classic alike. `__CODE_head` is
  `$2000`. A plain dot is one `PROG` equal to `prog_CODE.bin` (4690 B here, everything in the CODE group,
  no main-RAM part); a dotx is `PROG` (the CODE group, `$2000`-`$267C`: crt and library) plus
  `PROG.X` (= `prog_MAIN.bin`, at `CRT_ORG_MAIN` `$8000`, where `dot.c` is: `code_compiler` at `$87xx`;
  `__MAIN_size` is 0 though it has content); a dotn is `PROG` of 16384 B whose main bank and
  `PAGE` / `DIV` sections are LOGICAL pages that NextZXOS maps at run time (`__z_page_table`, appmake
  `zxn_dotn_command`). `CRT_ORG_DIV_<n>` and `__DIV_<n>_head` are in every newlib map: not a signal.
  The 2.4 sdcc `-SO2 --c-code-in-asm` dot has no line record in its map (G12): lines only in the `.lis`.
- G24 (GH #351, measured in jnext booting NextZXOS from the cached card, `--rtc 2026-07-30T12:00:00`,
  `.prog` typed at the prompt, a script logging port 0xE3 writes and executions): the file is copied
  into DivMMC RAM bank 2 (E3 alternates $02 / $00 during the copy), then `E3 <- $82` (conmem + bank 2)
  and the CPU executes `$2000` with the file's bytes; inside the command E3 toggles $80 (bank 0, the
  RST $08 hook) and back. The same for dotx (after `CLEAR 32767`, which the crt requires; the main part
  runs from `Page{4}`/`Page{5}`) and dotn (the main part runs from pages NextZXOS allocates: $DC $DB $DE $DD
  in the run). The machine resets while NextZXOS boots, so a map attached at start-up meets a reset
  before the command can be typed. Not measured: NextZXOS's 48K personality, esxDOS proper.
- Z3 (candidate upstream bug, GH #345): newlib `+zxn` `__banked` programs built with the
  defaults cannot run. `CRT_ORG_PAGE_20` is 16-bit ($C000), but `banked_call` reads the page
  from bits 16-23, so `defq _banked_add` is `00 C0 00 00` and it pages in page 0; and the
  default stack ($FF58) is in the banking segment (MMU7), so `banked_call`'s `ret` pops from
  the wrong page and returns to $FFFF. The `-run` builds pass `CRT_ORG_PAGE_20=0x14C000` and
  `REGISTER_SP=0xC000` and run on both installs (`__PAGE_20_head` is then 24-bit, read by its
  low 16 bits, design 9.4). `sccz80-page20`, `sccz80-bank10`, `sccz80-banked`, `sdcc-banked`
  (and `bankin.asm`'s `bank_in`) are map/identity fixtures only and do not run.
- Z4 (candidate upstream bug, GH #345): z88dk-gdb (2.4 and master) segfaults loading a map
  with a 24-bit `__C_LINE_` value (the `-run` maps).
  Z3/Z4 are for the owner to decide on; nothing is filed by this work.
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
`l_debug_push_frame l_debug_pop_frame __debug_framepointer banked_call l_jphl`. With `KEEP_CRT=1`
the `addr` symbols of the module named by `prog.lis`'s `MODULE` line (`zxn_crt_asm_m4`, 31 of them)
are kept too, so a listing's labels can be matched to the map. The
`/tmp/tmpXX....i:2` marker lines of each module are always dropped (random names sort
randomly). Lines stay verbatim and in z88dk's order. Never hand-edit: widen the rule and regenerate.

## Licence

`src/` is ours. The `.nex` / `.bin` outputs contain z88dk runtime library code (Clarified
Artistic License, GPL-compatible), as every z88dk-built `.nex` in `demo/` does.
