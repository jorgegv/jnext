# Task 18 — Late-Session Experiments (2026-05-17, follow-up to POST-BOOT-DIAGNOSIS)

After the [POST-BOOT-DIAGNOSIS](POST-BOOT-DIAGNOSIS-2026-05-17.md) and
the parallel CSpect-DZRP capture (see [CSPECT-POST-BOOT-DIFF](CSPECT-POST-BOOT-DIFF.md)),
I attempted two empirical fixes against the welcome-banner-missing
issue. Neither worked. This doc captures the negative results so they
aren't re-attempted next session.

## Experiment 1 — `nmi_source_.strobe_soft_reset()` after NR 0x03=0xB3

**Hypothesis**: NextZXOS reads NR 0x02 readback (which surfaces the
`reset_type` FSM) to decide between "cold-boot, trust firmware
painted" vs "post-soft-reset, paint banner myself". Power-on
`reset_type` = `100` ($04); after one RESET_SOFT it should be `010`
($02). Adding `nmi_source_.strobe_soft_reset()` to the bypass advances
the FSM to `010`, mirroring what tbblue.fw's final NR 0x02=0x01 write
would have done.

**Result**: NEGATIVE. Screenshot at t=10s identical to baseline
(`md5: afc4f6a42c74c7c7333787d0e028bd14`). Probe state at frame 600
identical: pixels all-zero, attributes all-$38, sysvars at $5C42-$5C5F
still zero. NextZXOS does NOT key the welcome banner off `reset_type`.

**Verdict**: reverted. Code clean.

## Experiment 2 — Mirror CSpect's divergent NR values pre-handoff

**Hypothesis**: NextZXOS keys the banner-draw path off a specific NR
register state that CSpect's tbblue.fw sets but our bypass leaves at
default. Per the CSpect post-boot diff, the diverging NRs are NR 0x14
(global transparency colour = $E3), NR 0x4B ($E3), NR 0x4C ($0F),
NR 0x42 ($0F), and NR 0xC0 ($08 — bit 3 = ULA INT NMI return).

**Edit**: pre-wrote all 5 NR values in the bypass init block, AFTER
the existing NR 0x03=0xB3 write, so NextZXOS sees those values from
its first instruction.

**Result**: NEGATIVE. Screenshot identical, screen state identical,
sysvars still zero past $5C40. NextZXOS overwrites or ignores these
NR values.

**Verdict**: reverted. Code clean.

## Why these experiments failed (analysis)

The CSpect post-boot capture caught CSpect's NextZXOS at PC=$0C90,
inside the KEYBOARD SCAN ROUTINE — well after the banner draw. Our
jnext-bypass NextZXOS is at PC=$0000 in the IDLE LOOP. Both are
post-init states; both have IM=1 + IFF1=1 active. The DIFFERENCE is
that CSpect's NextZXOS ALREADY drew the banner (we see it because
CSpect's screen is non-empty), whereas jnext's NextZXOS reached idle
WITHOUT drawing.

The banner draw is a single, traceable code path. We don't know
where in `enNextZX.rom` it lives, but it must be a function called
between the CLS (which jnext DID perform) and the entry to the idle
loop. Disassembling `enNextZX.rom` to locate the banner-print routine
+ tracing why jnext's NextZXOS skips it is the next-session task.

## Key conclusion (carrying forward)

The bypass-firmware-exploration-v2 branch achieves its primary goal:
**NextZXOS boots in jnext** for the first time. The OS is fully alive
in its idle loop with proper sysvar pointer at IY, IM-1 mode, ISR
running, ROM banks switched correctly, screen CLS'd.

The remaining gap (no welcome banner) is downstream of the bypass and
requires NextZXOS reverse engineering, which is out of scope for this
investigation pass. The bypass works as a "boot-NextZXOS-and-get-it-
running" mechanism — usable for any task that doesn't need the
welcome screen text (programmatic load of TAP/TZX/NEX, SD-card-based
loaders, etc.). Drawing the banner requires either finding the
specific NextZXOS condition that gates it, or running tbblue.fw
properly (the Task 1 / G46(b) work).
