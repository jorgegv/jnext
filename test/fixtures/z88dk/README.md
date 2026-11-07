# z88dk ground-truth fixtures (GH #343, E23-12)

What the two z88dk toolchains actually produce for the same small program, committed
so the source-level-debugging adapters (epic #23) have an oracle: every expected
address, line and section in a test row comes from these files, never from the code
under test (design: `doc/design/SOURCE-LEVEL-DEBUGGING.md` section 2.3). CI has no
z88dk, hence committed outputs and a hash check (rows Z88FIX-01/02 in `source_map_test`).

## The installs

| dir | commit | describe | zsdcc |
|-----|--------|----------|-------|
| `v2.4/` | `4d530b6eb779ad0f2a1d13c0bb670cf717477501` | v2.4 | 4.5.0 #15242 |
| `master/` | `97731e4e2988543e2c849cd1ae1d0aeddf372fcd` | v2.4-2883-g97731e4e29 | 4.6.0 #16639 pr3093 Nov 3 2026 |
| `master-fixed/`, `master-fixed-zsdcc/`, `master-fixed-80cc/`, `master-fixed-sccz80/` | `0f0a1cf70e48856af8191af6f86096d2e1362482` | v2.4-2954-g0f0a1cf70e | 4.6.0 #16639 pr3093 Nov 5 2026 |
| `nightly/` | `965fd0e628` (master, 2026-11-06 21:28 UTC), nightly `z88dk-20261107-965fd0e628-26852.tgz` | v26852-965fd0e628-20261107 | none (the kit has no zsdcc) |
| `v2.4-fp/` | the `v2.4/` install | v2.4 | 4.5.0 #15242 |

`master/` is master BEFORE the debug-record fixes that merged on 2026-11-04 and 2026-11-05 (z88dk PRs #3227, #3229, #3230, #3231, #3237,
#3238, #3241, #3242, #3243, #3244, #3250 to #3253, #3257, #3258, #3260 to #3265; #3240, floats, was not merged yet; it merged on 2026-11-06 and `nightly/` has it); `master-fixed/`, `master-fixed-zsdcc/`, `master-fixed-80cc/` and `master-fixed-sccz80/`
are master with them (GH #438), the tip of z88dk master when the owner rebuilt `-latest` on 2026-11-05. The name `master-fixed` is kept from the days
when the fixed install was a hand-patched `a52baaf7d9` (the first six fixes only); it now means "real master after the fixes". `master-fixed-zsdcc/`
holds the zsdcc builds (own directory: the rows that walk every map of `master-fixed/` count sccz80 and 80cc modules).
The pre-fix install is `~/src/spectrum/z88dk-original-97731e4e29`, a reflink copy kept for `master/` (never the other two); the post-fix one is
`~/src/spectrum/z88dk-original-latest`.
That install has since moved on in git (HEAD `1ead95546d`) without its compilers being rebuilt: regenerating the four `master-fixed*` trees with it gives byte-identical maps, listings and
binaries, and only each `PROVENANCE` and the first line of each `build.log` (the commit) differ. Those four trees therefore keep `0f0a1cf70e`'s provenance and are not regenerated;
`generate.sh` run today writes the newer commit into them, which a commit must not take (check `git diff --stat` and keep only the new directories).
`nightly/` is built with `~/src/spectrum/z88dk-original-965fd0e628` (`Z88DK_NIGHTLY`): the nightly source kit, downloaded from <http://nightly.z88dk.org/>, sha256
`8362689a3cd10138c3c082eadb2cb8f947ad32220150e667ff1a5c7ca60736db`, with its own prebuilt libraries and its binaries built locally by `./build.sh -l`
(`ulimit -v 4194304`, `nice`). It has no `.git`: the commit is in its `NIGHTLY` file (tarball, sha256, URL, commit) and `generate.sh` reads it from there. There is no zsdcc in it.
`v2.4-fp/` is the same program as `nightly/` built with the 2.4 install, which writes no float format (the fallback A/B).
All four fixture trees have no local modification (`PROVENANCE`: `modified tracked files:` is empty) except `v2.4/`, which lists two modified yaz180 `.inc` files (a target this tree never builds), older than GH #438.
Each `PROVENANCE` and each `build.log` (first line) record the commit. The master tools
print a stale banner (`v23856-580777436e-20251005`, from an old generated `config.h`):
trust the commit, not `zcc`'s banner.

## Regeneration

    bash test/fixtures/z88dk/generate.sh

Env overrides: `Z88DK_24`, `Z88DK_MASTER`, `Z88DK_FIXED`, `Z88DK_NIGHTLY` (install roots; defaults
`~/src/spectrum/z88dk-original-2.4`, `...-97731e4e29` (`master/`), `...-latest` (`master-fixed*/`) and `...-965fd0e628` (`nightly/`)). The output is deterministic:
`git diff --stat` must be empty unless an install changed. When it did, commit the new
outputs and `MANIFEST.sha256` together and say which rows' values moved. The script
exits non-zero if a build that should succeed fails, or one recorded as failing starts to
build. `README.md` is not hashed; everything else (sources, script, outputs) is.

## Builds

All `zcc +zxn`, `LANG=C`. Plain builds compile `main.c util.c asmfn.asm` (the `*-cvars` builds `vars.c other.c`, `*-fc` `fc.c`; the new banked sources are `bk.c` and `bkmain.c`, since `bmain.c` is the older banked fixture's); `subdir`
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
| v2.4/sccz80-calleejp, master/sccz80-calleejp | `calleejp.c,calleejpfn.asm`: `-clib=new -debug` | ok | GH #349 R1'': a `__z88dk_callee` routine (ours, `addtwo`) and `bit_beep_raw_callee` jump internally by `JP (HL)`/`JP (IX)` with SP above their CALL's slot. No 80cc build: master's 80cc does not honour `__z88dk_callee` on an extern declaration (it emits `call _addtwo; pop bc; pop de`) |
| v2.4/sccz80-tape | `zcc +zx -clib=new -debug -m --list -o prog -create-app` | ok, a `.tap` | GH #344 stage 3 (tape programs) |
| master/sccz80-debug | `-clib=new -debug` | ok | G4 fixed |
| master/sdcc-debug | `-clib=sdcc_iy -debug` | ok | sdcc map lines, Z2; also keeps the CRT listing (G13-G15) |
| master/80cc-debug | `-clib=new -compiler=80cc -debug` | ok | 80cc map |
| master/sdcc-banked | `-clib=sdcc_iy -debug -DUSE_BANKED_KW`, `PAGE_20` | ok | G9 |
| master/sdcc-banked-run | `-clib=sdcc_iy -debug -DUSE_BANKED_KW` + the same two pragmas, `PAGE_20` | ok | G9 runnable (GH #345) |
| v2.4/sccz80-dot, sccz80-dotx, sdcc-dot; v2.4-dotn/sccz80 | `dot.c dutil.c`, `-subtype=dot` / `dotx` / `dotn`, `-clib=new -debug` (sdcc-dot: `-clib=sdcc_iy -SO2 --c-code-in-asm`: lines in the `.lis` only) | ok, `PROG` (`PROG.X` for dotx) | GH #351 dot commands; dotn lives in `v2.4-dotn/` so that Z88CN-10's walk over `v2.4/` and `master/` (every map shows a C name) is not tripped by its refused C symbols |
| master/sccz80-dot, sccz80-dotx, sdcc-dot, 80cc-dot | `-clib=new -debug` / `-clib=sdcc_iy -debug` / `-clib=new -compiler=80cc -debug` | ok | GH #351 |
| master/classic-bank10 | `-clib=classic`, `BANK_10` | ok, no C lines | classic page prefix; keeps the classic CRT listing (G19-G21) |
| master/classic-debug | `-clib=classic -debug`, `BANK_10` | ok (z88dk v2.4-2883; failed with Z1 on v2.4-2695) | Z1 fixed on master, GH #445 |
| v2.4/sccz80-cvars, master/sccz80-cvars, master/sdcc-cvars, master/80cc-cvars | `vars.c other.c` (+ `types.h`): `-clib=new -debug` / `-clib=sdcc_iy -debug` / `-clib=new -compiler=80cc -debug` | ok | GH #431: C variable records of every type (section "C variables") |
| v2.4/sdcc-cvars | `-clib=sdcc_iy -debug`, `vars.c other.c` | **fails rc=1** | z88dk#3172, as `v2.4/sdcc-debug` |
| v2.4/sccz80-cvars-banked, master/{sccz80,sdcc,80cc}-cvars-banked | `bk.c` compiled with `--codesegPAGE_20 --constsegPAGE_20 --datasegPAGE_20`, then `bkmain.c bk.o`, the compiler flags of the line above + `-pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000` | ok | GH #431: banked code AND data (runnable, as the `-run` builds) |
| master/sccz80-extern, master/sdcc-extern | `ext_use.c ext_def.c` (+ `ext.h`): `extern int g;` in one module, `int g = 5;` in the other; `-clib=new -debug` / `-clib=sdcc_iy -debug` | ok | GH #433: the record of an `extern` global (section "C variables") |
| v2.4/sccz80-fc, master/{sccz80,sdcc,80cc}-fc | `fc.c` only, as the `incmid` builds | ok | GH #431: `__z88dk_fastcall` and `__z88dk_callee` |
| v2.4/sccz80-cross, master/{sccz80,sdcc,80cc}-cross | `cross.c` only: `-clib=new -debug` / `-clib=sdcc_iy -debug` / `-clib=new -compiler=80cc -debug` | ok | GH #434 F-A: `cross()` holds 8192 bytes of padding (`defs 8192`: `#asm` for sccz80 and 80cc, `__asm` for zsdcc), so its code runs from the CODE slot at $8000 into $A000-$BFFF (extent `8C6B-A000` + `A000-AC81` on sccz80); `derive-scopes.py --extents` derives the pieces |
| v2.4/sccz80-floats, master/{sccz80,sdcc}-floats | `floats.c` only: `-clib=new -debug` / `-clib=sdcc_iy -debug` | ok | GH #434 stage 2 (P2): six float constants (1.5, -2.5, 0.1, 1e10, 1e-10, 0); sccz80 stores them in z88dk's 48-bit format (`00 00 00 00 40 81` is 1.5), zsdcc as IEEE binary32 (`00 00 C0 3F`); the bytes are the oracle of `c_value_test` CVALF-01..06. Not built: `--math32` (`{4}SF` from sccz80, not measured) and `_Float16` (`S6`), so CVALF-07's binary16 rows rest on the IEEE 754 tables only |
| master/sdcc-cvars-O0 | `vars.c other.c`, `-clib=sdcc_iy -debug -O0` | ok | GH #434 stage 3 (P1): zcc's `-O0` keeps zsdcc's `C_LINE` records (the `x::<level>::<block>` form) and writes NO `__CDBINFO__` record (`-SO0` writes all 63): the case `c_variables_status()` names "zsdcc wrote no C variable records for this build" |
| v2.4/sccz80-{blocks,bits,fm}, master/{sccz80,sdcc,80cc}-{blocks,bits}, master/{sccz80,80cc}-fm, master-fixed/{sccz80,80cc}-{bits,fm}, master-fixed/sccz80-blocks | `blocks.c`, `bits.c` or `fm.c` only: `-clib=new [-compiler=80cc] -debug` / `-clib=sdcc_iy -debug` | ok | GH #438: block scope (`for` headers, sibling blocks), bitfields, a fastcall function with two parameters and arrays sized by their initialiser (section "Debug-record bugs") |
| master/80cc-u5, master-fixed/80cc-u5 | `u5.c` only: `-clib=new -compiler=80cc -debug` | ok | GH #438: z88dk#3233's program, a local 80cc keeps in a register |
| master/{sccz80,sdcc,80cc}-xmod | `./xm.c 1st.c`: `-clib=new` / `-clib=sdcc_iy` / `-clib=new -compiler=80cc`, `-debug` | ok | GH #438: z80asm prefixes `X` to a sccz80 or 80cc module whose path starts with `.` or a digit (`X__xm_c`, `X1st_c`); zsdcc names its modules `xm_c` and `_1st_c` |
| master/classic-mbf32, master/classic-math32 | `fl.c` only: `zcc +zx -debug --math-mbf32` / `--math32` | ok, a `.tap` | GH #438: the map names the float library (`__code_fp_mbf32_size` / `__code_fp_math32_size`), the record says only `{4}SF` |
| master-fixed/{sccz80,80cc}-cvars, master-fixed/sccz80-fc | as the `master/` builds of the same name | ok | GH #438: the records after the fixes (sccz80 and 80cc also write the `N:` / `E:` enum records of z88dk#3241) |
| master-fixed-zsdcc/sdcc-{cvars,blocks,xmod,debug} | `-clib=sdcc_iy -debug`: `vars.c other.c`, `blocks.c`, `./xm.c 1st.c`, `main.c util.c asmfn.asm` | ok | GH #438: zsdcc after z88dk#3262 (no TAB after the `x` of a C_LINE) and #3265 (the `-debug` modules are named by the whole path: `__xm_c`, `blocks_c` with the `.c` in its `M:` and local-scope records) |
| master-fixed-80cc/80cc-scope | `f3254.c` only: `-clib=new -compiler=80cc -debug` | ok | GH #438 S4a: our own program (z88dk issue 3254's shape): an `if` block, a `for` that declares `i`, a brace-less inner `for` over `j`, loop-rotated code; 80cc writes PR #3243's levels and blocks, with `C_LINE 10` three times. Own directory: the rows that walk `master-fixed/` count its modules |
| master-fixed-80cc/80cc-ifelse | `ifelse.c` only: `-clib=new -compiler=80cc -debug` | ok | GH #438 S4b (review 7 F1): `f1` (if/else chains, loops, a shadowing block), `k9` (brace-less then-branch), `c1` (a trailing `if` block), `d2` (a hoisted multiply, the temporary `1licm_0`): the places where 80cc stamps code with another block's scope |
| master-fixed-80cc/80cc-x | `x.c`: a function really named `x` | ok | GH #438 S4b: the module is 80cc, not zsdcc's `x` placeholder |
| master-fixed-80cc/80cc-shadow, master-fixed-sccz80/sccz80-shadow, master-fixed-zsdcc/sdcc-shadow | `shadow.c`: an inner `{ int16_t t = 5; g += t; }` over the function's `t` | ok | GH #438 S4b: shadowed names (zsdcc keeps the outer t in DE and records the inner t as `[]`) |
| master-fixed-zsdcc/sdcc-stub | `stub1.c stub2.c`: `-clib=sdcc_iy -debug`; `void p(int a) {}` | ok | GH #438 S4b (review 7 F3): a zsdcc module with only level-0 `x` records |

| nightly/sccz80-zx-{math48, genmath, lmz} | `fpfmt.c` for `+zx` (classic library): `-debug --math48` / `--genmath` / `-lmz` | ok | GH #477: float format 0/128/5 (z80, 6-byte floats) |
| nightly/sccz80-zx-{math32, lm, am9511} | `-debug --math32` / `-lm` / `--am9511` | ok | GH #477: 1/126/3 (IEEE binary32) |
| nightly/sccz80-zx-mbf32, -mbf40, -mbf64 | `-debug --math-mbf32` / `-Cc-fp-mode=mbf40` / `--math-mbf64` | ok | GH #477: 2/128/3, 3/128/4, 4/128/7 |
| nightly/sccz80-zx-bbc, -z88 | `-debug --math-bbc` / `-Cc-math-z88 -lz88_math` | ok | GH #477: 5/127/4 (z88, with its integer form) |
| nightly/sccz80-zx-dai32 | `-debug --math-dai32` | ok | GH #477: 6/0/3 (AM9511-style) |
| nightly/sccz80-zx-bias129 | `-debug --math48 -Cc-fp-exponent-bias=129 -Cc-fp-mantissa-size=4` | ok | GH #477: 0/129/4, a user format (the bytes decode only with the map's values) |
| nightly/80cc-zx-{math48, math32, mbf32} | `-debug -compiler=80cc --math48` / `--math32` / `--math-mbf32` | ok | GH #477: 80cc writes the same three symbols |
| nightly/sccz80-zxn, sccz80-zxn-math32, 80cc-zxn | `fpfmt.c` for `+zxn`: `-clib=new -debug` [`--math32`] [`-compiler=80cc`] | ok | GH #477: 0/128/5, 1/126/3, 0/128/5 |
| nightly/sccz80-zx-fl-mbf32, -fl-dai32 | `fl.c` (float arithmetic, so the library links): `+zx -debug --math-mbf32` / `--math-dai32` | ok | GH #477: the map also names the library (`__code_fp_mbf32_size`, `__code_fp_dai32_size`); the recorded format beats that guess |
| nightly/sccz80-zx-mix | `fpmix48.c` with `--math48` and `fpmix32.c` with `--math32`, compiled apart, linked with `--math32` | ok | GH #477: two float formats in one map, one `T:` record of `struct sf` per module |
| v2.4-fp/sccz80-zxn | `fpfmt.c`: `-clib=new -debug` | ok | GH #477: the program of `nightly/sccz80-zxn` without any `__fp_*` symbol |

The dot builds (`dot:<subtype>` in `generate.sh`) compile `dot.c dutil.c` with `-subtype=dot|dotx|dotn`
instead of `-subtype=nex`. Classic dot is not built: `-debug` fails (Z1) and without it there are no lines.
They also keep `PROG` / `PROG.X` (the file NextZXOS runs), and their trimmed map keeps `startupoffset`,
`__crt_org_code` and the `__<G>_head/_tail/_size` of every group that has a `prog_<G>.bin` (a dotx's
`__MAIN_head` has size 0 and the old rule dropped it).

`sccz80-tape` is the one `+zx` build (a 48K tape: `prog.tap` holds a BASIC loader and one
CODE block at $8000 that equals `prog_CODE.bin`; link flags without `-subtype=nex`).

Kept per successful build: `prog.map` (trimmed), every user `*.c.lis` / `*.asm.lis`,
`prog.nex` (`prog.tap` for the tape build), every `prog_<GROUP>.bin` (classic: `prog`). `prog.lis` (the crt0
listing, 12-18K lines) is kept for exactly six builds, `v2.4/sdcc-SO2`, `master/sdcc-debug`, both `classic-bank10` and both
`sccz80-banked-crt` (`KEEP_CRT=1` in `generate.sh`, GH #348, GH #426); not kept for the others, nor `.sym`, `.o`.

### Not producible (recorded, `build.log` only)

- `v2.4/sdcc-debug` (G12): `defc Gcounter0_00==.` syntax errors in 2.4's sdcc `-debug`
  (z88dk issue 3172; fixed on master).
- `v2.4/sdcc-cvars` (GH #431): the same z88dk issue 3172 failure as `v2.4/sdcc-debug`, on `vars.c other.c`.
- `v2.4/classic-debug` (Z1): `Unknown option2 --hardware-keyboard`. `master/classic-debug` failed the same way until z88dk v2.4-2883 (GH #445) and is now built (classic +zxn banked `-debug` is producible on master only).

## Findings (evidence in this tree)

- G1 sccz80 emits one `__C_LINE_18_` ($8C9D) though `main.c.lis` has four `C_LINE 18,`
  (`v2.4/sccz80-debug`).
- G2 80 `= $0000 ; addr, local, , <mod>, ,` lines in that map.
- G3 lines 14/15 share $8C92, 16/17 $8C95, 24/25 $8D12 (2.4 sccz80).
- G4 line 20 is $8CBE on 2.4 and $8CBA on master; master 24 $8CFF, 25/26 $8D0F.
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
  m4 line 9207 (with bytes) and again at 9266 on 2.4 (master: 9219 and 9278). The map names the defining line (9207 / master 9219), so a label
  matches only at that line. The m4 file's lines are 1..9689 on 2.4 and 1..9701 on master, none missing.
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
- Z3 (candidate upstream bug, GH #345; v2.4 only for the first half): newlib `+zxn` `__banked` programs built with the
  defaults cannot run on v2.4. `CRT_ORG_PAGE_20` is 16-bit ($C000) there, but `banked_call` reads the page
  from bits 16-23, so `defq _banked_add` is `00 C0 00 00` and it pages in page 0; and the
  default stack ($FF58) is in the banking segment (MMU7), so `banked_call`'s `ret` pops from
  the wrong page and returns to $FFFF. The `-run` builds pass `CRT_ORG_PAGE_20=0x14C000` and
  `REGISTER_SP=0xC000` and run on both installs (`__PAGE_20_head` is then 24-bit, read by its
  low 16 bits, design 9.4). `sccz80-page20`, `sccz80-bank10`, `sccz80-banked`, `sdcc-banked`
  (and `bankin.asm`'s `bank_in`) are map/identity fixtures only and do not run.
  On master (v2.4-2883) the default `CRT_ORG_PAGE_20` is `$14C000` (24-bit; `master/sdcc-banked/prog.map`, built
  with no pragma), so that half is gone; the default stack was not re-measured there.
- Z4 (candidate upstream bug, GH #345): z88dk-gdb (2.4 and master) segfaults loading a map
  with a 24-bit `__C_LINE_` value (the `-run` maps).
  Z3/Z4 are for the owner to decide on; nothing is filed by this work.
- Z1 (candidate upstream bug): `-debug` makes zcc forward ALIAS options to the compiler.
  `zcc +zx --vt100 -c util.c -o u.o` succeeds, `zcc +zx --vt100 -debug -c util.c -o u.o`
  fails with `Unknown option2 --vt100` on v2.4; master v2.4-2883 accepts both (measured with the install named in `generate.sh`).
- Z2 (known upstream TODO): master sdcc labels header code compiled into a second module
  with the wrong line (`util_inc` in `util_c` shows as `util.h:9`, really line 7).

## C variables (GH #431, epic #353)

The `*-cvars`, `*-cvars-banked` and `*-fc` builds exist to give the C variable records
(`__CDBINFO__`) a ground truth. Everything below was read from the committed maps with a
small decoder (undo z80asm's `_hh` escaping of the name; the module is the 4th comma-separated field after the line's ` ; `),
and the `.lis` files; all builds are `-debug`, `TMPDIR=/tmp`.
**Which install produced what:** `v2.4/` = 4d530b6eb, `master/` = 97731e4e29 (v2.4-2883, regenerated by GH #445;
the earlier base 39421a01cd predated upstream PR #3221). 80cc exists only in `master/`.

Records kept per build (F functions, M modules, S variables, T types; total in the map):

| build | total | F / M / S / T |
|-------|-------|---------------|
| v2.4/sccz80-cvars, master/sccz80-cvars | 53 | 3 / 0 / 45 / 5 (the two record sets are identical) |
| master/sdcc-cvars | 63 | 3 / 2 / 48 / 10 |
| master/80cc-cvars | 55 | 3 / 2 / 45 / 5 |
| v2.4/sdcc-cvars | none | does not build (z88dk#3172, `build.log`) |
| banked: v2.4 sccz80, master sccz80 / sdcc / 80cc | 10 / 10 / 13 / 11 | |
| fc: 2.4 sccz80, master sccz80 / sdcc / 80cc | 9 / 9 / 13 / 10 | |

Facts the decoder and the frame rows rely on (each checked in the files named):

- The kind is the first letter: `F:` function, `M:` module (zsdcc and 80cc only), `S:` variable,
  `T:` struct/union type. `S:G$` global, `S:F<scope>$` file static, `S:L<scope>.<func>$` local or
  parameter. The address space follows the type: `E` (data symbol), `B,1,<offset>` (stack),
  `R,0,0,[regs]` (registers; `[]` = optimised out), `C` function, `D` literal (zsdcc `__str_2`).
- The type chain is `({size}...)`: sccz80/80cc `DA3x`, zsdcc `DA3d` for an array of 3; `DG` pointer
  (zsdcc function pointers `DC,DF`); `STpoint:S` struct; `SC/SI/SL/SF:{S,U}`. `char` is signed in
  sccz80/80cc (`g_char` `SC:S`) and unsigned in zsdcc (`SC:U`); `float` is `{6}SF` (sccz80, 80cc) and
  `{4}SF` (zsdcc); an enum variable is `SI:S` (sccz80/80cc, 2 bytes) or `SC:U` (zsdcc, 1 byte).
- `g_float = 1.5` in the data: `00 00 00 00 40 81` (6-byte z88dk format, sccz80 and 80cc, `_g_float`
  $904A on 2.4 / $904C on master sccz80 / $8FC7 80cc) and `00 00 c0 3f` (IEEE single, zsdcc, $8F44).
  Read from `prog_CODE.bin` at (address - `__CODE_head` $8000).
- sccz80 and 80cc write a second, incomplete-array record for `g_str` (zsdcc one record): `({-1}DA-1x,SC:S)` beside `({3}DA3x,SC:S)`.
- sccz80/80cc enumerators are records with an empty type (`S:G$RED$0_0$0({0}),E,0,0`); zsdcc has none.
- Bitfields: zsdcc `T:Fvars$flags[...({1}SB0$3:U)({1}SB3$5:U)...]` carries start and width; sccz80/80cc
  write `({2}SI:U)` members at offsets 0, 0, 1 with no bit layout.
- Types are scoped by the declaring header in sccz80/80cc (`T:Ftypes.h$point`, one copy), by module in
  zsdcc (`T:Fvars$point` and `T:Fother$point`).
- A global function's `F:G$` record is emitted by each declaring module and collapses to one map line
  whose module column is the first declarer's: `F:G$other` has `mod=vars_c` in the sccz80/80cc maps
  (`types.h` declares it) and `mod=other_c` in zsdcc's. Take a function's address from `_<name>`.
- File statics with one name in two files: `_s_dup` is `other_c` $9069 and `vars_c` $909F
  (master sccz80), $8F62 / $8F96 (master sdcc), $8FE4 / $901A (80cc); records
  `S:Fvars.c$s_dup` / `S:Fother.c$s_dup` (zsdcc `S:Fvars$s_dup` / `S:Fother$s_dup`).
- Static local `calls` in `sum`: sccz80/80cc `S:Fvars.c$st_sum_calls$1_0$1`, symbol `_st_sum_calls`
  ($90A0 master sccz80, $901B 80cc); zsdcc `S:Lvars.sum$calls$1_0$10`, symbol `_sum_calls_10000_10`
  ($8F97).
- Stack offsets (`B,1,n`) in `sum(a, b, p, k)`: sccz80 and 80cc `a`=+10, `b`=+8, `p`=+6, `k`=+4 (first
  argument pushed first); zsdcc `a`=+4, `b`=+6, `p`=+8, `k`=+10. Locals are negative. The frame
  layout differs between sccz80 and 80cc: `t` is -2 / -8, `inner` -8 / -2, `main`'s `lp` -5 / -25,
  `arr` -13 / -20.
- zsdcc keeps `t` in `[c,b]` and `inner` in `[l,h]`, `acc`, `big`, `c`, `pp` as `[]` (optimised out)
  and adds compiler temporaries `sum$sloc0`, `sum$sloc1`, `main$sloc0` (`B`, level/sub `0_1`); zsdcc
  also writes `S:G$main`/`S:G$other` and `S:Fvars$sum` as `C`-space function records, and its `F:G$main`
  has frame size 13 in field 4.
- Frame keeping, from the listings: sccz80 `call l_debug_push_frame` in every function; zsdcc
  `push ix` in every function; 80cc mixes the two per function (`vars.c`: `sum` and `main` `push ix`,
  `other.c`: `other` calls `l_debug_push_frame`). Whether `fp = SP at entry - 2` for all three was
  measured by the planner of #353 with z88dk-ticks and is not re-run by this fixture.
- `C_LINE` block scope, `sum()` in `vars.c` (the scope string of the `__C_LINE_<n>_...` map
  lines of module `vars_c`): sccz80 `vars.c::sum::<level>::<block>`: lines 30-36 `sum::1::1`, 37-40 `sum::2::2`,
  41 `sum::1::2` (the level is right, the block is the last one opened, not the current one);
  80cc `vars.c::sum::0::2` for every body line, 33-41 (no level, no block structure); zsdcc
  `vars.c::x::<level*10000>::<block><TAB>`: 33-35 and 41-42 `10000::10`, 36 `20000::11`, 38-39
  `30000::12` (function field `x`, the block is the current one, a TAB ends the field).
  Upstream gap: line 44 is `int main(void)`, and sccz80 and 80cc scope it `sum::0::2`
  (`__C_LINE_44` sits at `_main`: $8D54 sccz80, $8CD9 80cc, the address of `main::0::3` line 45);
  zsdcc gets it right, `x::10000::14`.
- `fc.c`: sccz80 (both installs) records the `__z88dk_fastcall` parameter `x` at `B,1,4`, its
  nominal slot, and that record is WRONG: a fastcall caller pushes nothing, and the listing
  (`master/sccz80-fc/fc.c.lis`: `parameter 'unsigned int x' at sp+4`, then `call l_debug_push_frame`
  / `push hl`) stores `x` at fp-2 (its sibling `y` is correctly `B,1,-4`). The decoder must not
  trust it (plan #353 U1, OD-14: show "location unknown"). 80cc's `B,1,-2` is right
  (`master/80cc-fc/fc.c.lis`: `push ix; ld ix,0; add ix,sp; push hl`); zsdcc records `R,0,0,[l,h]`. `__z88dk_callee`: sccz80/80cc
  `a`=+6, `b`=+4; zsdcc `a`=+4, `b`=+6.
- Banked (`bk.c` in PAGE_20, code, const and data): `_btable` $14C000, `_bdata` $14C004, `_bk_sum`
  $14C005 on sccz80 / 80cc (24-bit values, page 20); zsdcc orders them `_bk_sum` $14C000, `_btable`
  $14C024, `_bdata` $14C028. Records are the same shape as unbanked. zsdcc writes no `S:G$btable`
  (a `const` global), and 80cc writes no record for `main`'s `loc` (sccz80 does:
  `S:Lbkmain.c.main$loc$1_0$2({1}SC:U),B,1,-1`; zsdcc `R,0,0,[]`).
- `extern` (`ext_use.c` declares `extern int g;`, `ext_def.c` defines it; `master/*-extern`): zsdcc writes
  `S:G$g` with `mod=ext_use_c`, the module that declared it, while `_g` is public in `ext_def_c` ($8E46
  in the sdcc build); sccz80 writes the record in `ext_def_c`, the definer. So an `S:G` record joins
  to the PUBLIC symbol of its name, never to the module of its line.

Not producible: `v2.4/sdcc-cvars` (z88dk#3172). Not built here: zsdcc `-O0` (no records), `+zxn`
classic `-debug` on v2.4 (Z1), sdcc_ix (the planner measured records identical to sdcc_iy; not re-run here).

## Debug-record bugs (GH #438)

What each z88dk gives the debugger for the new `blocks.c`, `bits.c`, `fm.c`, `u5.c`, `fl.c` and `xm.c`/`1st.c`
(measured from these files; `v2.4` is the pre-fix record set, as is `master/`):

- **Block scope (z88dk#3227).** sccz80 numbered a block by the last block OPENED, so a local declared after an inner block took
  the inner block's number; `master-fixed/` numbers them in pre-order. `dofor()` raises the nesting level without a block, on
  every sccz80 version and on 80cc (the same front end): a `for (int i ...)` variable gets its enclosing block's number at the
  raised level, and the body has no block at the level between, until z88dk#3258: since it sccz80 opens a block of its own per `for` (every later block number of a function moves up; `master-fixed/sccz80-blocks` and `-cvars`). zsdcc gives the `for` scope a block of its own.
- **Bitfields (z88dk#3231).** sccz80 and 80cc wrote a bitfield as a whole `int` (`{2}SI:U`, overlapping its neighbours), and from
  z88dk#3231 as `SB<start>$<width>:S|U`; zsdcc always writes `SB`. A plain `int s:4` is **unsigned** in sccz80 and 80cc
  (`SB0$4:U`; the code does `and 15`) and signed in zsdcc (`SB0$4:S`); `signed int n:4` is `:S` everywhere.
- **Fastcall parameters (z88dk#3229).** Before it sccz80 recorded a `__z88dk_fastcall` function's parameter at its nominal
  slot, where it is not (it is pushed after `call l_debug_push_frame`: `push hl`); afterwards one register parameter is recorded
  below the frame (`fm`'s `b` at -2) and the others at their nominal slots. 80cc's record of a register-passed parameter names
  another variable's slot before z88dk PR #3244 (`fm`'s `b` is recorded at -2, `y`'s slot; it is at IX-4).
- **Arrays sized by their initialiser (z88dk#3229).** `char ls[] = "loc";` is `{-1}DA-1x,B,1,0` on sccz80 before #3229; static
  and global ones are `{-1}`, the global with a stale twin; 80cc kept that for static and global arrays until z88dk#3257 (`master-fixed/80cc-fm` has four sized records and no twin).
- **80cc frame slots (z88dk#3233).** `u5.c`: `r` stays in a register and its slot is never written.
- **Modules.** z80asm names a module after its source path with `X` prefixed when the path starts with `.`, `/` or a digit
  (`./xm.c` is `X__xm_c`, `1st.c` is `X1st_c`; zsdcc names its modules `xm_c` and `_1st_c`); 80cc writes `M:<file>` with the file as `C_LINE` spells it (`M:./xm.c`), zsdcc
  `M:<module>` against the file `<module>.c`, and sccz80 writes no `M:` into the map. Since z88dk#3265 zsdcc `-debug` names a module by the whole path
  as sccz80 does (`./xm.c` is `__xm_c`, `blocks.c` `M:blocks.c`, its locals' scope `Lblocks.c.f`) and, since #3262, writes the `x` of a `C_LINE` with no TAB after it
  (`master-fixed-zsdcc/`).
- **Enums (z88dk#3241).** sccz80 and 80cc write one `N:F<file>$<tag>({size}<base>)[(NAME$value)...]` per enum and an `E:` record beside the `S:` record of
  each enum-typed object (`SEF<file>$<tag>` type); the empty enumerator records `S:G$RED$0_0$0({0})` are no longer written (`master-fixed/{sccz80,80cc}-cvars`).
- **80cc scope data (z88dk#3243).** 80cc's `C_LINE` carries `level::block` of its own (`main::1::3`, not `main::0::3`), and since #3244 and #3264 (locals and parameters are stored to their frame
  slots) a `-debug` function whose frame the compiler used to elide has an IX frame with its parameters at IX+4 (`master-fixed/80cc-u5`, `-cvars`).
- **Floats (z88dk#3236, #3240).** The record says `{4}SF` for mbf32, dai32 and math32 alike. Before 2026-11-06 `__code_fp_<lib>_size` was the only trace of the
  library, and it is non-zero only when the program calls the library (a program with float constants and no float arithmetic names none). z88dk master from
  2026-11-06 (`nightly/`) writes, into the map of every C module sccz80 and 80cc compile, three LOCAL constants:
  `__fp_exponent_bias`, `__fp_mantissa_size` and `__fp_mode` (0 z80, 1 ieee, 2 mbf32, 3 mbf40, 4 mbf64, 5 z88, 6 am9511), field 4 being the module like a `__CDBINFO__`
  line. Measured on `fpfmt.c` (mode/bias/mantissa, record size): `--math48`, `--genmath`, `-lmz`, `+zxn -clib=new` (sccz80 and 80cc): 0/128/5, 6;
  `--math32`, `-lm`, `--am9511`: 1/126/3, 4; `--math-mbf32`: 2/128/3, 4; `-Cc-fp-mode=mbf40`: 3/128/4, 6 (byte 5 unused); `--math-mbf64`: 4/128/7, 8;
  `--math-bbc` and `-Cc-math-z88 -lz88_math`: 5/127/4, 6 (byte 0 unused; exponent byte 0 is a 32-bit integer); `--math-dai32`: 6/0/3, 4;
  user options `-Cc-fp-exponent-bias=129 -Cc-fp-mantissa-size=4` on `--math48`: 0/129/4, 6. -3.25 is `00 00 00 00 D0 82` (math48), `00 00 50 C0` (IEEE),
  `00 00 D0 82` (mbf32 and dai32), `00 00 00 D0 82 00` (mbf40), `00 00 00 00 00 00 D0 82` (mbf64), `00 00 00 00 D0 81` (z88), `00 00 00 D0 83 00` (bias 129).
  zsdcc and assembly modules write none; neither does z88dk 2.4 (`v2.4-fp/`). A mixed link keeps one set per module and, because the struct's text differs by
  its float size, one `T:` record of a shared struct per module. Not built (the same modes and values as the builds above; JNEXT loads maps of programs for the
  machines it emulates): `+zx81 -lm`, `-lmz` on zx81, lambda and cpc, `--math-cpc`, `+z88 -lmz`.
- **The #439 check.** The whole fixture set regenerated with the nightly standing in for `Z88DK_FIXED` (scratch directory, never committed) and the 12 suites that read
  `JNEXT_Z88DK_FIXTURE_DIR` run against it: see "The nightly check" below.

### Scope-table and extent goldens: `derive-scopes.py`

Row `Z88CDB-40` (`test/debug/z88dk_map_test.cpp`) pins every function's whole scope table on 12 maps
against a golden derived by `derive-scopes.py` from the RAW map lines only (no jnext code). Its header
holds the rules and the command that prints one map's block. Re-derive, never paste the suite's output,
whenever a fixture is regenerated or the equal-line rule changes. `--extents` prints each function's extent (pieces split at the 8 KB boundaries) for rows Z88CDB-41/42 (GH #434). The script is hashed in `MANIFEST.sha256`
(`generate.sh` lists it), because `Z88FIX-02` demands every file here to be in the manifest; nothing runs it
in a gate.

## The nightly check (GH #477, from #439)

2026-11-07: the whole fixture set was regenerated with the nightly `z88dk-20261107-965fd0e628-26852` standing in for `Z88DK_FIXED` (scratch directory, never committed:
`master-fixed*` stay at `0f0a1cf70e`), the three zsdcc trees were kept from the committed ones (the kit has no zsdcc), and the 12 suites that read `JNEXT_Z88DK_FIXTURE_DIR`
(c_value, z88dk_map, source_debug, cdb_record, source_map, z88dk_lis, gdb_rsp, script_eval, script_events, zrcp_adapter, debugger_variables_panel, debugger_source_panel) ran
against it in a scratch copy of the source tree.

What changed in the compilers' output (`master-fixed`, `master-fixed-80cc`, `master-fixed-sccz80`: 32, 18 and 8 files differ besides each `build.log`'s first line and `PROVENANCE`;
`v2.4`, `v2.4-dotn`, `master` and every zsdcc tree are byte-identical; `80cc-bits`, `80cc-u5`, `80cc-x` and `80cc-shadowagg` differ only by the `__fp_*` lines):
- every sccz80 and 80cc map and listing gains the three `__fp_*` constants (the listing five `defc`/`IF`/`ENDIF` lines);
- sccz80 and 80cc now optimise across `C_LINE` boundaries (z88dk master 2026-11-06, copt "C_LINE handling shouldn't disrupt optimisations"): redundant `push`/`pop` pairs are gone,
  the code is shorter, every later address moves, and adjacent source lines can share one address (`master-fixed/sccz80-blocks`: lines 15-17 at `$8D00`, no separate entry for 16 or 17);
- 80cc: a `for` opens a block of its own (block numbers of `80cc-scope`: `f::4::3` became `f::4::5`, `f::2::1` became `f::2::3`; addresses unchanged), and a line that holds code of
  two blocks gets one `C_LINE` for each (shown on a one-line `if/else`: old one record, nightly one per block);
- 80cc loop code changes address in `80cc-ifelse`, `80cc-fm`, `80cc-shadow` and `80cc-static`.

Suites: 7 green with no change (cdb_record 55, source_map 130, z88dk_lis 47, script_eval 76, script_events 153, zrcp_adapter 231, debugger_source_panel 271). Failing rows, each
explained, none a JNEXT defect (every one pins an address, a scope-table entry, a byte or a listing line of the `0f0a1cf70e` build, or says that `master-fixed` has no float format):
- c_value_test CVZB-05, CVZB-06 (the entries of `blocks.c` lines 16 and 17 no longer exist: one address); CVFMT-22 (the scratch `master-fixed*` have `__fp_mode`: D4);
- z88dk_map_test Z88ZB-06 (the fc.c / fm.c entry listings lost their `push`/`pop` pairs), Z88ZB-12 (f3254.c block numbers `4/3` are `4/5`), Z88FP-02 (as CVFMT-22);
- source_debug_test SRCDBG-ZB-03, -05, -06, -07, -09, -11, -14, -17, -18, -19, -25; gdb_rsp_test GDB-ZB-02; debugger_variables_panel_test QVARSH-01, -03, -05: each runs to a hard-coded PC or
  pokes a hard-coded address of an sccz80 or 80cc build whose addresses moved.
Not re-derived row by row; checked instead through the facade on the nightly maps (scratch probe: every `C_LINE` address of `80cc-scope`, `80cc-ifelse`, an 80cc `blocks.c` and
`sccz80-blocks`, `sccz80-shadow` and a one-line two-block 80cc program): the locals listed at each stop match the C programs' scopes and the user guide's description. The `for`
variable of the nightly `80cc-scope` is shown exactly as on `0f0a1cf70e` (listed on the `for` line and in the body, hidden after, `scope unknown`).
Verdict: the nightly changes the guide (user guide "What each z88dk can show": the `for` row, the two-blocks note, the float rows, the first nightly with every fix) and no test
that stays committed. A new `nightly/` row set pins what the nightly writes for floats (CVFMT-01..21, Z88FP-01).

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
