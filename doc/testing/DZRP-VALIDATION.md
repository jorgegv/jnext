# DZRP server — validation protocol (GH #12)

How jnext's DeZog Remote Protocol server (`--dzrp-port`, `src/remote/dzrp/`) is
validated. Design: [dzrp-frontend.md](../design/debug-subsystem/dzrp-frontend.md)
§7. Three tiers:

1. **The fake-transport unit suite** — `dzrp_adapter_test` (§7.3), bytes on the
   wire and machine state for every command, run by `make unit-test`.
2. **Live regression rows** (§7.2) — a real jnext, headless, on a real socket,
   driven by two clients that were written independently of the server. Run by
   `make regression`. Automated; listed in [§1](#1-the-automated-rows).
3. **A real DeZog in VS Code** (§7.1) — needs a human at the editor. It is the
   **owner checklist** in [§3](#3-owner-checklist-dezog-in-vs-code), not yet run.

## 1. The automated rows

Every row starts `jnext --headless --machine 48k --dzrp-port 0`, waits (bounded)
for the `dzrp: listening on 127.0.0.1:<port>` line, runs its client under
`timeout --kill-after`, and stops jnext. The per-run SD clone is the harness's.

### Through jnext's own client (`tools/cspect_dzrp/cspect_dzrp.py`)

Written for the CSpect plugin before jnext had a server — an independent reading
of the wire. Its REVIEW.md H1-H3 were fixed first (a harness with a known race
is not a harness), and `cspect-dzrp-selftest-func` runs its own suite.

| Row | Proves |
|---|---|
| `dzrp-loopback-func` | framing: LOOPBACK 1..8192 bytes, the response length counted from the seq byte, 8193 declined in sync, the seq wrap 255 → 1 |
| `dzrp-init-regs-func` | INIT as 2.0.0 and 2.2.0 → 2.2.0 / ZXNEXT / "jnext v…"; the supported-commands bitfield; the 37-byte register block and 48K slot map; 270 seqs across the wrap; a version-less INIT refused in its error field |
| `dzrp-bp-continue-func` | NTF_PAUSE reason 2 with the bank byte; a temporary at the same address reported as a step (F8); a banked breakpoint fires only with its page mapped |
| `dzrp-watch-func` | a 256-byte write watch stops once, at its last byte; the bytes before and after, and a read, do not stop it; a read watch → reason 3 |
| `dzrp-state-func` | the JNXB state token saved and restored exactly; refused mid-frame; empty, garbage and unissued tokens → NTF 255 "no state to restore", nothing changed, no latch |
| `dzrp-bank-func` | WRITE_BANK / WRITE_BANK_MEM read back through SET_SLOT + READ_MEM; READ_BANK_MEM bounded to the page; bank 255 is the ROM the CPU sees |
| `dzrp-brkint-func` | break-on-interrupt stops at 0x0038 with "Break on interrupt."; off, frames run without a stop |
| `dzrp-unsupported-func` | ids 13/22/99 seq-only with a warn line, in sync; the 2.2.0-removed SET_BORDER and WRITE_BANK still served |
| `dzrp-close-resume-func` | INIT holds the machine; CLOSE releases the client's own pause; INIT again on the connection; reconnect served |

### The owner's dezogif_ng DZRP clients (`test/dzrp/`)

**Copied, not a submodule**, from dezogif_ng (https://github.com/jorgegv/dezogif_ng),
`test/dzrp/`, at commit `709ae7d77d444e0e14d88d5c6114b2a7c18e2be6` (the files'
last change there: `4eb9359cce05b3e3e95234cedbebfe75997f9ada`). GPLv3, as jnext
is. Each file's header says what, if anything, changed.

| File | As copied | Row |
|---|---|---|
| `dzrp.py` | the client; `init_payload()`'s version made a parameter (`DZRP_VERSION`, default 2.2.0 — dezogif_ng pins 2.1.0) | all below |
| `conformance.py`, `screen.py` | unchanged | `dzrp-conformance-func`: C1-C17, C24, C25, C15 must all PASS, every command required. C19-C23 are the dezogif_ng stub's own RST-patching mechanism (SET_BREAKPOINTS / RESTORE_MEM, which a normal-mode remote does not serve); C18 takes its verdict on a second concurrent connection, which jnext refuses by design |
| `queued-commands.py` | adapted: one connection instead of three colliding ones | `dzrp-queued-commands-func` — the drain-while-paused rule (SES-03) |
| `split-command.py` | adapted: the second client is refused rather than multiplexed | `dzrp-split-command-func` — frame reassembly from the command's own bytes |
| `orphan-notify.py` | adapted: a counter loop and a breakpoint instead of a crashed stub | `dzrp-orphan-notify-func` — SES-01 |
| `abandoned-send-client.py` | adapted: a reply lost on a closed socket instead of an abandoned `AT+CIPSEND` | `dzrp-abandoned-client-func` — SES-01 |

The conformance suite found a real defect on its first run: every other check
was refused as "a second client", because the transport accepted a reconnecting
client's new connection before reading the old one's hang-up. Fixed in
`src/remote/transport.cpp`, rows XPT-SRV-30/31/32.

### The loop owners and the command line

| Row | Proves |
|---|---|
| `dzrp-paused-headless-func` | a `--headless` run held paused by a client uses < 25 % of a core and executes nothing (the old loop spun), and its `--delayed-automatic-exit-frames` bound still fires, charged in wall time — exit status 0, ~2 s after the attach for 100 frames |
| `dzrp-sdl-func`, `dzrp-qt-func` | the SDL and Qt loop owners register the server; eight commands queued while paused are answered within one tick (T's draining budget — the running one answers a command a tick); CONTINUE runs the program under that loop and PAUSE notifies |
| `dzrp-cli-func` | `--dzrp-port` values outside 0..65535 refused by name; a port in use is a named startup error; no flag, nothing listens |
| `debug-listen-address-func` | `--debug-listen-address` refused without a server port; with `--dzrp-port 0`, the default and each given address reach the listener |
| `cspect-dzrp-selftest-func` | the harness client's own suite, H1-H3 included, at its pinned size |

## 2. What the automated tiers cannot show

- That **DeZog** — not a client written from the specification — interoperates:
  its session start, its step and step-out sequences, its use of the bank
  byte, conditional breakpoints evaluated client-side.
- **Latency as a user feels it** (V-LAT): the paused cadence under DeZog's own
  throttle.
- The **Qt window and DeZog together** (V-GUI).

Those are the checklist below.

## 3. Owner checklist: DeZog in VS Code

Not yet run. Record the DeZog version, the jnext version (`jnext --version`),
the date and the result on each row. A 3.8 result is provisional until 3.8.0 is
released.

**Setup.** Build a demo with its map (`make -C demo`), then
`jnext --dzrp-port 11000 --load demo/<x>.nex`. DeZog 3.7.4 (marketplace):

```json
"remoteType": "cspect",
"cspect": { "hostname": "localhost", "port": 11000 }
```

DeZog 3.8 (upstream `main`, in the Extension Development Host):
`"remoteType": "dzrp", "dzrp": { "port": 11000 }`, and repeat V-INIT with
`cspect`.

| ✓ | Row | Exercise | Proves (design ref) | DeZog | Result |
|---|---|---|---|---|---|
| ☐ | V-INIT | Connect: VS Code shows *stopped/entry*, Registers pane populated, MMU slots shown; 3.8: no "Unsupported command" in the DeZog log for anything jnext advertises | rows 1, 3, 24; F1, F4 | | |
| ☐ | V-SUPP | 3.8: `-dbg cmd_exec_asm 0` and a `.p` load → named "not supported by the remote" errors, no timeout; 3.7.4: `-state save` refused client-side | row 24; bits 13/14/22 clear | | |
| ☐ | V-BANKMEM | 3.8, `remoteType "dzrp"`: `-md <addr> bank=<n>` on an unmapped bank shows it; `bank=255` at 0x0000 and 0x2000 shows the two ROM halves; `-md 0x3000 bank=<n>` on an 8 KB bank — record what the view shows; repeat `bank=255` with RAM paged at 0x0000 (port 0xEFF7 bit 3) | rows 25/26; N-1..N-3 | | |
| ☐ | V-BRKINT | Not executable against 3.8.0-rc7 (no code path sends 39): record that, and rely on `dzrp-brkint-func`; re-run when a release wires it | row 39 | | |
| ☐ | V-WP | 3.8: a WPMEM watchpoint on a variable, Continue → stops with the write address; 3.7.4: not reachable (F3) | rows 42/43 | | |
| ☐ | V-BP | An editor breakpoint from the map, Continue, hits; Continue again, hits again; remove it, no hit | rows 40, 41, 6; §3.1 | | |
| ☐ | V-BANK | A breakpoint in a banked page hits only while that bank is mapped | §3.1, REQ-dzrp-7 | | |
| ☐ | V-STEP | Step Into through `CALL`, `RET`, `JR cc` taken and not taken, `RST 08`; Step Over a `CALL`; Step Out of a routine | §3.2, F8 | | |
| ☐ | V-COND | A conditional breakpoint `A == 3` in a loop stops exactly once | decision 2 | | |
| ☐ | V-LOG | A LOGPOINT prints its text and the machine does not stay stopped | row 40 | | |
| ☐ | V-PAUSE | Continue, then Pause: stops, registers refresh | row 7 | | |
| ☐ | V-MEM | Memory view, disassembly view, edit a byte, edit a register | rows 8, 9, 4 | | |
| ☐ | V-NR/SPR | `-nextreg`, the sprite viewer, sprite palette, pattern viewer | rows 11, 16-19 | | |
| ☐ | V-LOAD | `launch.json` `load` of a `.sna` and of a `.nex`, on 3.7.4 (rows 5, 12) and 3.8 (rows 26, 21 with port 0xFE) | rows 5, 10, 12, 21, 23, 26 | | |
| ☐ | V-STATE | 3.8: `-state save` after Pause, change a register, `-state restore` → machine back, registers refreshed. Then after a breakpoint hit: `-state save` writes an empty file (check its size), `-state restore` → registers unchanged, jnext logs "no state to restore", and Continue still works | §6, R-1 | | |
| ☐ | V-GUI | With jnext's Qt debugger open: a DZRP pause opens/refreshes it; GUI Run, then DeZog Continue | §4.4 | | |
| ☐ | V-LAT | Step Out of a 1000-iteration `DJNZ` routine, wall-clock timed, `build/gui-release/jnext` at 100 %; record the per-step cost (expect ≈ 1 tick + ≤ 10 ms). Measure the first second apart from the rest: after it, DeZog's own `TimeWait(1000, 200, 100)` sleeps 100 ms in every 200 ms, so a flat result past 1 s says nothing about jnext's cadence. The result decides REQ-dzrp-9 (a shorter paused cadence) | REQ-dzrp-9; R-4; N-5 | | |
| ☐ | V-CLOSE | Stop the session: the machine resumes; reconnecting works without restarting jnext | row 2, §4.1 | | |
