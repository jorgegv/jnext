# REVIEW.md — Empirical Rebuttal of Finding 2

The independent reviewer's REVIEW.md (commit `c0a0ff1b`) raised four findings.
Per project memory rule `feedback_findings_require_independent_review`,
this rebuttal re-verifies each. Three findings are legitimate and will be
addressed; **Finding 2 is empirically falsified** — see below.

## Finding 2 (MAJOR): "`enNextZX.rom` filename almost certainly does not exist on the canonical SD image"

The reviewer reasoned from CLAUDE.md's "ROMs" section, which enumerates
only the LEGACY-BASIC ROMs (`48.rom` / `128.rom` / `plus3.rom`) plus
DivMMC / MF / AltROM. CLAUDE.md does not claim those are the only files
on the SD image; it documents what jnext currently extracts in the
firmware-boot path.

The PLAN-AUDIT.md §1 (lines 36-43) flagged a naming-reconciliation
concern: "the plan's reference to `enNextZX.rom` does NOT match jnext's
current `/MACHINES/NEXT/*.rom` filename scheme." This is what the
reviewer cited. But PLAN-AUDIT was a preview audit; it did not check
SD-image contents.

### Empirical falsification

```
$ python3 tools/cspect_dzrp/task18_*.py  # FAT32 walk of nextzxos-1gb-fat32fix.img

  ENNEXTZX.ROM: cluster=10321, size=65536 bytes (64 KB)
  ENNXTMMC.ROM: cluster=10320, size=8192 bytes (8 KB)
  ENNEXTMF.ROM: cluster=10329, size=8192 bytes (8 KB)
  ENALTZX.ROM:  cluster=10338, size=32768 bytes (32 KB)
  PLUS3.ROM:    cluster=10301, size=65536 bytes (64 KB)
  48.ROM:       cluster=10342, size=16384 bytes (16 KB)
  128.ROM:      cluster=10348, size=32768 bytes (32 KB)
  MENU.DEF:     cluster=10309, size=750 bytes
  CONFIG.INI:   cluster=12165, size=283 bytes
  TBBLUE.FW:    NOT FOUND
```

Both `enNextZX.rom` AND `plus3.rom` exist on the canonical SD image,
each 64 KB. They are DIFFERENT files with DIFFERENT contents:

```
enNextZX.rom $0000-$000F: F3 C3 EF 00 45 44 09 02 C3 3B 10 2A 2E 2A FF 00
plus3.rom    $0000-$000F: F3 01 03 6C 0B 78 B1 20 FB C3 0F 01 45 44 00 00
```

`enNextZX.rom` is the NextZXOS combined ROM blob (matches tbblue
source's RAMPAGE_ROMSPECCY load); `plus3.rom` is the legacy Amstrad +3
BASIC ROM.

### Runtime confirmation

The bypass code's smoke-test produces this log line:

```
[memory] [info] ROM slot 0: loaded from byte buffer (16384 bytes)
[memory] [info] ROM slot 1: loaded from byte buffer (16384 bytes)
[memory] [info] ROM slot 2: loaded from byte buffer (16384 bytes)
[memory] [info] ROM slot 3: loaded from byte buffer (16384 bytes)
[emulator] [info] Machine ROM loaded from SD '/MACHINES/NEXT/enNextZX.rom'
                  (65536 bytes -> 4 banks): NextZXOS (bypass-tbblue-fw)
```

`extract_sd_rom()` succeeded — 65536 bytes loaded into 4 banks.

Furthermore, the Z80 CPU trace shows execution at PC=$00EF doing
`ED 91 07 03 ED 91 03 B0 ...`, which matches `enNextZX.rom` byte $00EF
exactly:

```
enNextZX.rom $00EF: ED 91 07 03 ED 91 03 B0 ED 91 C0 08 3E FF ED 92
plus3.rom    $00EF: 10 5B E5 2A 5A 5B E3 C9 F3 3E 10 01 FD 1F ED 79
```

The executing code is NextZXOS, not legacy +3 BASIC. The file load,
SRAM seed, and rom_in_sram routing are all working correctly.

### Conclusion

Finding 2 is REJECTED. The bypass implementation correctly loads
`enNextZX.rom` (NextZXOS, 64 KB) into SRAM pages 0..7. The reviewer
reasoned from secondary documentation (CLAUDE.md + PLAN-AUDIT.md flag)
without checking the actual SD image, and got an incorrect conclusion.

The lesson is the same one captured in [[feedback_findings_require_independent_review]]:
even an independent reviewer can be wrong; empirical verification
against the actual artifact is the ultimate arbiter.

---

## Findings 1 (MINOR), 3 (MINOR), 4 (NIT) — accepted and being addressed

- **Finding 1 (config_mode comment)**: latent hazard, no functional bug
  today. Will update comment at `emulator.cpp:5243-5250` to document
  that bypass mode intentionally skips config_mode activation.
- **Finding 3 (no tests)**: will add a discriminative unit test under
  `test/task18_baseline/` that asserts (a) SRAM page 0 first 4 bytes =
  `F3 C3 EF 00`, (b) `boot_rom_enabled()=false`, (c) `config_mode()=false`,
  (d) `machine_type()=ZX_PLUS3`.
- **Finding 4 (NR $07 omission rationale)**: will add comment block
  explaining that NR $07 cold-reset default is already $00 (3.5 MHz),
  matching what NextZXOS expects at handoff; the firmware-boot path's
  $03 (28 MHz) is for tbblue.fw's own use during firmware execution and
  is irrelevant to the post-firmware contract.

These three nit-fixes will land in a follow-up commit on this branch.
