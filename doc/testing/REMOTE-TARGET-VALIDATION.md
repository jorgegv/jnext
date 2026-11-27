# Remote target validation on a real Next (epic #486)

Test record for the remote-debugging epic: what the DZRP client was observed to do against a real
ZX Spectrum Next running dezogif_ng. Plan: `doc/design/DEBUGGER-REMOTE-REAL-NEXT-PLAN.md`
(R0.5, R4.6). Section 1 is R0.5, the early checkpoint of the plan's §2.4. Section 2 is R4.6's.

## 1. R0.5 early hardware checkpoint (GH #512)

### 1.1 Setup

| Item | Value | Source |
|---|---|---|
| Board | KS2 (Kickstarter 2), board issue 4 | owner |
| Core | 3.02.01 on the boot screen; NR 0x01 = 0x32, NR 0x0E = 0x01 agree | owner; probe |
| Stub | dezogif_ng WiFi build 00.26 (banner "build 00.26"), ROM magic `DeZoGiFnG_WIFI_0026`, repo `eb58524`, installed with `.mfinstall --load wifi` | owner |
| Transport | dezogif_ng WiFi ROM over the ESP-01, TCP port 11000, from the development host over the LAN | owner; probe |
| State | NMI pressed first: the stub's screen up, no program loaded or run on the stub | owner |
| Client | `build/test/dzrp_client_probe` built from `epic486-remote-target` b241483ba | |
| Date | 2026-11-27, about 08:11 to 08:12 local time | probe logs |

Commands, run 1 then run 2 (run 2 last and once, Q10); the Next's LAN address is masked as `<next-ip>` throughout this record:

```
dzrp_client_probe --hex tcp:<next-ip>:11000
dzrp_client_probe --nextreg 0E --send-unsupported 24 tcp:<next-ip>:11000
```

Content check: the outputs below hold only 16-byte reads at 0x0000 (stub Alt ROM patch and 48K
ROM bytes), at 0x2000 (48K ROM) and zeros; no SD-card image content.

### 1.2 Observed values against the plan's §2.4

| Fact | Observed | §2.4 says | Verdict |
|---|---|---|---|
| `CMD_INIT` reply | `00 02 01 00 04 "dezogif v2.2.1"`: error 0, DZRP 2.1.0, machine 4 (ZXNEXT) | DZRP 2.1.0, machine 4, `"dezogif v2.2.1"` (`commands.asm:26-35`) | AGREE |
| Slot bytes of `CMD_GET_REGISTERS` | `FF FF 0A 0B 04 05 00 01` (slots 0-7); NR 0x50-0x57 read afterwards: `FF FF 0A 0B 04 05 00 5E` | slot 7 from the stub's backup, slot 0 as ROM | AGREE, and refined: see 1.3 point A (slots 1-6 and the slot 7 backup are written by `CMD_INIT` itself; the reported slot 7 = 1 is the client-visible one memory reads use, NR 0x57 = 0x5E = 94 = `MAIN_BANK` is the stub's own) |
| `CMD_READ_MEM` 0x0000 | `18 64 FB ED 92 57 F1 C9` then `2A 5D 5C 22 5F 5C 18 43` (48K ROM 0x0008..) | the 48K ROM patched into the Alt ROM (bytes 0x0000-0x0007) | AGREE |
| `CMD_READ_MEM` 0x2000 | `0D CD 79 1C CD C3 1F CD 07 23 3E 16 18 10 FE AD` | ROM slots show the patched Alt ROM | AGREE: equals offset 0x2000 of the Sinclair 48K ROM (cross-check below) |
| `CMD_READ_MEM` 0x4000-0xFFFF | 16 bytes of zeros in each slot, under the mapping `CMD_INIT` set (pages 10 and 11 at 0x4000 are the ULA screen bank) | not stated | NOT A CHECK: 16 zero bytes say nothing; not compared with the screen |
| `CMD_READ_MEM` returning the debuggee's bytes | not checkable: there was no debuggee (1.3 B) | the plan's acceptance item | NOT CHECKED at R0.5; R4.6 checks it after an upload |
| Registers | all zero (PC, SP, AF..., R, I, IM) on a stub freshly entered from NextZXOS | not stated | NEW FACT: see 1.3 point B (zero holds for a fresh entry only); not compared with the stub screen, the source explains them |
| Unsupported command 24 | no reply within 2000 ms; owner saw the stub screen reset to its idle screen; reconnect + `CMD_INIT` served in 214.1 ms | no reply, `drain_main` into `main`, session reset | AGREE |
| Round trip | median 7.5 ms (run 1), 7.4 ms (run 2), 6.4 to 6.5 ms floor, max 28.9 ms (a `CMD_INIT`) | "about 6.6 ms" (README) | AGREE (about 1 ms above the README figure at the median, 0.1 ms at the floor) |
| Identity rule | 2.1.0 `"dezogif ..."` reached over TCP | only dezogif_ng has a TCP transport (§2.4a, D19) | AGREE (the profile line says `dezogif_ng`) |

Cross-check of the ROM bytes: the 0x0008 and 0x2000 reads equal bytes 0x0008 and 0x2000 of
`/usr/share/fuse/48.rom` (package `fuse-emulator-roms-0.9.0-25.fc45`, sha256
`d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42`), a copy of the Sinclair 48K
ROM that is not from any SD image. Bytes 0x0000-0x0007 of that ROM are `F3 AF 11 FF FF C3 CB 11`,
which the stub overwrites (`copy_rom_start_0000h_code`, `breakpoints.asm:48-69`: `JR dbg_enter`,
`EI`, `NEXTREG 0x57,A` (restore slot 7), `POP AF`, `RET`), so the first 8 bytes read differ from the ROM, as
`altrom.asm` `modify_bank` says they must.

### 1.3 Findings not in §2.4 (folded into the plan, see 1.5)

A. **`CMD_INIT` has side effects on the machine.** `cmd_init` (`commands.asm:150-166`) sets
`slot_backup.slot0 = ROM_BANK`, `slot_backup.slot7 = 1`, and writes NR 0x51-0x56 to the ZX128
defaults (ROM, 10, 11, 4, 5, 0), also on a re-attach to a live debuggee. So after `CMD_INIT`
the slots `CMD_GET_REGISTERS` reports and NR 0x50-0x56 show are those defaults, not what the
interrupted program had mapped; the observed `FF FF 0A 0B 04 05 00 01` is exactly that table.
The reported slot 7 (1) is the one `CMD_READ_MEM` pages in for 0xE000-0xFFFF
(`commands.asm:712-720`), so it is the mapping the client sees. NR 0x57 reads 0x5E because the
stub itself runs from `MAIN_BANK` in slot 7 while stopped (`constants.asm:15,17`); that is the
stub's own slot, not the client's.

B. **A session starts with no debuggee; zero registers hold only after a fresh entry.**
Registers are written to `backup.*` (`data.asm:68-91`) by `save_registers`, which an NMI reaches
only over a RUNNING debuggee (`mf_rom.asm` `.break_into_debuggee`, after the
`prgm_state == PRGM_RUNNING` test at `:386-388`, then `mf.asm:105-112`) and `dbg_enter` (an RST 0
breakpoint, `breakpoints.asm:144-146`) reaches too, and by `CMD_SET_REGISTER`. With
`prgm_state == PRGM_IDLE` (set by `main`, `main.asm:146`, `constants.asm:518`) the NMI either
re-initialises the stub (`init_main_bank`, when NextZXOS was executing, which copies a fresh
`data.asm` image, so `backup.*` is zero) or returns at once; neither saves registers. That is the
observed state: the program NextZXOS was running cannot be inspected at all. It is not the state of
every session: `main` (entered by `CMD_CLOSE`, or through `drain_main` after an error or an
unsupported command) resets only the clock speed, interrupt state, layer 2 port, slot 0 backup
and `prgm_state` (`main.asm:142-156`), so a session after an earlier one reads that session's
stale registers, and a re-attach to a live debuggee reads real ones while `CMD_INIT` has just
rewritten its slots (A). Hence "no debuggee" is a client-side per-session state, set by every
`CMD_INIT`, cleared when the client has set SP, PC and the slots in this session, and never
inferred from register values (plan D24). `CMD_INIT` also sets `PRGM_LOADING`
(`commands.asm:179`, "After CMD_INIT until the first CMD_CONTINUE"), the DeZog upload model; a
`CMD_CONTINUE` before SP and PC are set would resume whatever `backup.*` holds
(`restore_registers`, `backup.asm:58-145`), read from the source, not tried on hardware.

C. The probe's second connection came 3.4 s after the first (log lines 08:11:57 and 08:12:01):
the 2000 ms wait for the reply that never comes, the probe's default 1000 ms
`--reconnect-delay-ms`, and the connect.

### 1.4 Round trips (ms)

| Command | Run 1 | Run 2 |
|---|---|---|
| TCP connect | 364.4 | 83.5 |
| `CMD_INIT` | 28.9 | 26.6 |
| `CMD_GET_REGISTERS` | 8.5 | 7.5 |
| `CMD_READ_MEM` PC 0x0000 | 7.5 | 7.4 |
| `CMD_READ_MEM` SP 0x0000 | 7.1 | 7.4 |
| `CMD_READ_MEM` slot 0x0000 | 7.0 | 7.7 |
| `CMD_READ_MEM` slot 0x2000 | 10.3 | 6.6 |
| `CMD_READ_MEM` slot 0x4000 | 7.5 | 7.5 |
| `CMD_READ_MEM` slot 0x6000 | 8.0 | 7.5 |
| `CMD_READ_MEM` slot 0x8000 | 13.0 | 7.4 |
| `CMD_READ_MEM` slot 0xA000 | 7.5 | 7.4 |
| `CMD_READ_MEM` slot 0xC000 | 7.5 | 13.9 |
| `CMD_READ_MEM` slot 0xE000 | 7.6 | 7.5 |
| `CMD_GET_TBBLUE_REG` NR 0x00 | 8.7 | 6.4 |
| NR 0x01 | 7.0 | 6.4 |
| NR 0x50 | 6.5 | 6.4 |
| NR 0x51 | 6.5 | 6.4 |
| NR 0x52 | 6.6 | 6.4 |
| NR 0x53 | 7.1 | 6.4 |
| NR 0x54 | 8.9 | 6.4 |
| NR 0x55 | 6.5 | 6.4 |
| NR 0x56 | 6.5 | 6.4 |
| NR 0x57 | 15.4 | 7.4 |
| NR 0x0E | not sent | 6.4 |
| unsupported 24 | not sent | no reply in 2000 ms |
| reconnect + `CMD_INIT` | not sent | 214.1 (connect included) |
| `CMD_CLOSE` | 6.5 | 6.5 |
| Summary | 23 commands, min 6.5 / median 7.5 / max 28.9 | 25 commands, min 6.4 / median 7.4 / max 47.0 |

The run 2 maximum (47.0 ms) is the reconnect `CMD_INIT` round trip: its listed 214.1 ms is the
connect (167.1 ms) plus that round trip (`test/remote/dzrp_probe.cpp`, the reconnect line prints
connect + `init_rtt`, the summary records `init_rtt` alone). Window 1 throughout (no pipelining tried).

### 1.5 Where the plan changed

Findings A and B are folded into `DEBUGGER-REMOTE-REAL-NEXT-PLAN.md`: §2.4 (two rows, and the
`main` row sharpened), §11 (the Run, Registers, MMU slots and Build match rows of the Remote Next
column), R1.2, R2.1, R2.5, R3.2, R4.1, R4.3, R4.5, R4.6, §12 (one risk), §13 (Q11) and D24. Design
change: a dezogif session is "no debuggee" from every `CMD_INIT` until its client has set SP, PC
and the slots: Run unavailable, registers invalid, slots available and labelled `CMD_INIT`
defaults.

### 1.6 Raw output

Run 1:

```
2026-11-27 08:11:13
[08:11:13.666] [debugger] [info] dzrp client: connected to "dezogif v2.2.1" (DZRP 2.1.0) over tcp:<next-ip>:11000: profile dezogif_ng, window 1
dzrp_client_probe: tcp:<next-ip>:11000
connect                      ok               364.4 ms
CMD_INIT            seq  1   ok      28.9 ms  reply 00 02 01 00 04 64 65 7A 6F 67 69 66 ...
  remote "dezogif v2.2.1"  DZRP 2.1.0  machine 4 (ZXNEXT)  profile dezogif_ng  window 1
CMD_GET_REGISTERS   seq  2   ok       8.5 ms  
  PC=0000 SP=0000 AF=0000 BC=0000 DE=0000 HL=0000 IX=0000 IY=0000
  AF'=0000 BC'=0000 DE'=0000 HL'=0000 R=00 I=00 IM=0
  slots 0:FF 1:FF 2:0A 3:0B 4:04 5:05 6:00 7:01
  raw: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 08 FF FF 0A 0B 04 05 00 01
CMD_READ_MEM        seq  3   ok       7.5 ms  PC 0000+16
  18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
  raw: 18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
CMD_READ_MEM        seq  4   ok       7.1 ms  SP 0000+16
  18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
  raw: 18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
CMD_READ_MEM        seq  5   ok       7.0 ms  slot 0000+16
  18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
  raw: 18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
CMD_READ_MEM        seq  6   ok      10.3 ms  slot 2000+16
  0D CD 79 1C CD C3 1F CD 07 23 3E 16 18 10 FE AD
  raw: 0D CD 79 1C CD C3 1F CD 07 23 3E 16 18 10 FE AD
CMD_READ_MEM        seq  7   ok       7.5 ms  slot 4000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  raw: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq  8   ok       8.0 ms  slot 6000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  raw: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq  9   ok      13.0 ms  slot 8000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  raw: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq 10   ok       7.5 ms  slot A000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  raw: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq 11   ok       7.5 ms  slot C000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  raw: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq 12   ok       7.6 ms  slot E000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  raw: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_GET_TBBLUE_REG  seq 13   ok       8.7 ms  NR 0x00
  NR 0x00 = 0x0A
  raw: 0A
CMD_GET_TBBLUE_REG  seq 14   ok       7.0 ms  NR 0x01
  NR 0x01 = 0x32
  raw: 32
CMD_GET_TBBLUE_REG  seq 15   ok       6.5 ms  NR 0x50
  NR 0x50 = 0xFF
  raw: FF
CMD_GET_TBBLUE_REG  seq  1   ok       6.5 ms  NR 0x51
  NR 0x51 = 0xFF
  raw: FF
CMD_GET_TBBLUE_REG  seq  2   ok       6.6 ms  NR 0x52
  NR 0x52 = 0x0A
  raw: 0A
CMD_GET_TBBLUE_REG  seq  3   ok       7.1 ms  NR 0x53
  NR 0x53 = 0x0B
  raw: 0B
CMD_GET_TBBLUE_REG  seq  4   ok       8.9 ms  NR 0x54
  NR 0x54 = 0x04
  raw: 04
CMD_GET_TBBLUE_REG  seq  5   ok       6.5 ms  NR 0x55
  NR 0x55 = 0x05
  raw: 05
CMD_GET_TBBLUE_REG  seq  6   ok       6.5 ms  NR 0x56
  NR 0x56 = 0x00
  raw: 00
CMD_GET_TBBLUE_REG  seq  7   ok      15.4 ms  NR 0x57
  NR 0x57 = 0x5E
  raw: 5E
CMD_CLOSE           seq  8   ok       6.5 ms
summary: 23 commands, 0 failed; round trip min 6.5 / median 7.5 / max 28.9 ms
exit=0
```

Run 2:

```
2026-11-27 08:11:57
[08:11:57.973] [debugger] [info] dzrp client: connected to "dezogif v2.2.1" (DZRP 2.1.0) over tcp:<next-ip>:11000: profile dezogif_ng, window 1
[08:12:01.346] [debugger] [info] dzrp client: connected to "dezogif v2.2.1" (DZRP 2.1.0) over tcp:<next-ip>:11000: profile dezogif_ng, window 1
dzrp_client_probe: tcp:<next-ip>:11000
connect                      ok                83.5 ms
CMD_INIT            seq  1   ok      26.6 ms  reply 00 02 01 00 04 64 65 7A 6F 67 69 66 ...
  remote "dezogif v2.2.1"  DZRP 2.1.0  machine 4 (ZXNEXT)  profile dezogif_ng  window 1
CMD_GET_REGISTERS   seq  2   ok       7.5 ms  
  PC=0000 SP=0000 AF=0000 BC=0000 DE=0000 HL=0000 IX=0000 IY=0000
  AF'=0000 BC'=0000 DE'=0000 HL'=0000 R=00 I=00 IM=0
  slots 0:FF 1:FF 2:0A 3:0B 4:04 5:05 6:00 7:01
CMD_READ_MEM        seq  3   ok       7.4 ms  PC 0000+16
  18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
CMD_READ_MEM        seq  4   ok       7.4 ms  SP 0000+16
  18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
CMD_READ_MEM        seq  5   ok       7.7 ms  slot 0000+16
  18 64 FB ED 92 57 F1 C9 2A 5D 5C 22 5F 5C 18 43
CMD_READ_MEM        seq  6   ok       6.6 ms  slot 2000+16
  0D CD 79 1C CD C3 1F CD 07 23 3E 16 18 10 FE AD
CMD_READ_MEM        seq  7   ok       7.5 ms  slot 4000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq  8   ok       7.5 ms  slot 6000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq  9   ok       7.4 ms  slot 8000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq 10   ok       7.4 ms  slot A000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq 11   ok      13.9 ms  slot C000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_READ_MEM        seq 12   ok       7.5 ms  slot E000+16
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CMD_GET_TBBLUE_REG  seq 13   ok       6.4 ms  NR 0x00
  NR 0x00 = 0x0A
CMD_GET_TBBLUE_REG  seq 14   ok       6.4 ms  NR 0x01
  NR 0x01 = 0x32
CMD_GET_TBBLUE_REG  seq 15   ok       6.4 ms  NR 0x50
  NR 0x50 = 0xFF
CMD_GET_TBBLUE_REG  seq  1   ok       6.4 ms  NR 0x51
  NR 0x51 = 0xFF
CMD_GET_TBBLUE_REG  seq  2   ok       6.4 ms  NR 0x52
  NR 0x52 = 0x0A
CMD_GET_TBBLUE_REG  seq  3   ok       6.4 ms  NR 0x53
  NR 0x53 = 0x0B
CMD_GET_TBBLUE_REG  seq  4   ok       6.4 ms  NR 0x54
  NR 0x54 = 0x04
CMD_GET_TBBLUE_REG  seq  5   ok       6.4 ms  NR 0x55
  NR 0x55 = 0x05
CMD_GET_TBBLUE_REG  seq  6   ok       6.4 ms  NR 0x56
  NR 0x56 = 0x00
CMD_GET_TBBLUE_REG  seq  7   ok       7.4 ms  NR 0x57
  NR 0x57 = 0x5E
CMD_GET_TBBLUE_REG  seq  8   ok       6.4 ms  NR 0x0E
  NR 0x0E = 0x01
unsupported 24      seq  1   no reply within 2000 ms
reconnect + CMD_INIT         ok               214.1 ms  "dezogif v2.2.1" DZRP 2.1.0
CMD_CLOSE           seq  2   ok       6.5 ms
summary: 25 commands, 0 failed; round trip min 6.4 / median 7.4 / max 47.0 ms
exit=0
```

## 2. R4.6 hardware validation (owner)

Not run yet. R4.6 fills this section: the R4.5 scenario on the KS2 (board issue 4) over WiFi,
with an uploaded program (section 1.3 B: there is no debuggee before an upload), each record
naming the board, the core and the dezogif_ng build.
