# cspect_dzrp — minimal Python DZRP client for CSpect

A small, dependency-free Python 3 module that speaks the **DZRP (DeZog
Remote Protocol)** to a running CSpect with **DezogPlugin.dll** loaded.
Built to ground-truth jnext's behaviour against CSpect for the G46(b)
investigation.

* Single file: `cspect_dzrp.py` — pure stdlib (`socket`, `struct`).
* Tests:      `test_cspect_dzrp.py` — runs offline against an in-process
              fake server.
* Example:    `example_g46b_diff.py` — runs CSpect to a PC, dumps regs +
              slot map + memory at HL as markdown.

## Launching CSpect with DezogPlugin

DezogPlugin is bundled with modern CSpect builds (it's "now integrated
into CSpect" per the upstream readme) — the DLL ships in CSpect's
install dir alongside `CSpect.exe`. The TCP listener starts
automatically on launch (default port **11000**, configurable in
`DeZogPlugin.dll.config`).

### Booting NextZXOS for the G46(b) investigation

Run from the **jnext repo root**:

```bash
mono ../CSpect3_1_0_0/CSpect.exe -mmc $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img
```

This launches CSpect against the same SD image jnext uses, so
boot-state comparisons are apples-to-apples. The DZRP listener comes
up at boot — verify with:

```bash
nc -z 127.0.0.1 11000 && echo OK
```

### Breaking at CPU reset ($0000)

Add `-debug` to halt CSpect's CPU at PC=$0000 right after reset:

```bash
mono ../CSpect3_1_0_0/CSpect.exe -mmc $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img -debug
```

CSpect opens its debugger window at $0000. From there you can:
- Connect with our DZRP client to inspect the reset state.
- Set a breakpoint at any address (e.g., $01ED for the supervisor's
  RST $28 to bank-1 $1661) and `Run` from the debugger; the BP fires
  and you can dump regs/memory via DZRP at exactly the boot phase you
  care about.

### Cross-host setup

If CSpect runs on a Windows VM and your tools run on Linux, expose the
port via VM port-forwarding and pass `--host` to the scripts.

## What this client supports

| DZRP cmd                  | id  | Method                              |
|---------------------------|-----|-------------------------------------|
| `CMD_INIT`                | 1   | `client.init()`                     |
| `CMD_CLOSE`               | 2   | `client.close()` (auto on exit)     |
| `CMD_GET_REGISTERS`       | 3   | `client.get_registers()`            |
| `CMD_SET_REGISTER`        | 4   | `client.set_register(name, value)`  |
| `CMD_CONTINUE`            | 6   | `client.cont(tmp_bp1, tmp_bp2)`     |
| `CMD_PAUSE`               | 7   | `client.pause()`                    |
| `CMD_READ_MEM`            | 8   | `client.read_mem(addr, size)`       |
| `CMD_WRITE_MEM`           | 9   | `client.write_mem(addr, data)`      |
| `CMD_SET_SLOT`            | 10  | `client.set_slot(slot, bank)`       |
| `CMD_GET_TBBLUE_REG`      | 11  | `client.get_tbblue_reg(reg)`        |
| `CMD_READ_PORT`           | 20  | `client.read_port(addr)`            |
| `CMD_WRITE_PORT`          | 21  | `client.write_port(addr, value)`    |
| `CMD_INTERRUPT_ON_OFF`    | 23  | `client.interrupt_on_off(bool)`     |
| `CMD_ADD_BREAKPOINT`      | 40  | `client.add_breakpoint(addr, bank)` |
| `CMD_REMOVE_BREAKPOINT`   | 41  | `client.remove_breakpoint(id)`      |
| `NTF_PAUSE`               | 1   | `client.wait_for_pause(timeout)`    |

NextReg slot mapping (NR `$50..$57`) is **not** a separate command — it
is appended to every `GET_REGISTERS` response. Use
`client.get_slots()` (a thin wrapper) or read `regs.slots` directly.

## Wire format (verified against `Server.cs`)

Both directions use a length-prefixed, little-endian frame:

```
Request  : [len:u32-LE] [seqno:u8] [cmd:u8]   [payload...]
Response : [len:u32-LE] [seqno:u8]            [payload...]   ; cmd not echoed
Notify   : [len:u32-LE] [seqno=0]  [ntf_id:u8] [payload...]
```

* `len` is the byte count *after* the length field
  (so `seqno + cmd + payload`, or `seqno + ntf + payload`).
* `seqno = 0` is reserved for asynchronous notifications. Clients pick
  any non-zero value; the server echoes it on responses.
* CSpect's `CMD_CONTINUE` returns an empty response *immediately*; the
  actual stop arrives later as a `NTF_PAUSE` notification.

DZRP version reported by the plugin: **2.0.0**.

## Subtleties that bite you if you guess

* **No single-step command.** Use `CMD_CONTINUE` with one or two 64K-only
  *temporary* breakpoints. To "step one instruction" from PC, set
  `tmp_bp1 = PC + instruction_length`. We expose
  `step_over_byte(n_bytes)` as a convenience.
* **No CMD_GET_SLOTS.** The 8 slot bank IDs are tacked onto the end of
  `GET_REGISTERS` (Commands.cs:GetRegisters):
  `12 LE words + R + I + IM + 0 + count(=8) + 8 slot bytes` = 37 bytes.
* **`CMD_ADD_BREAKPOINT` takes a *long address*** (3 bytes:
  `addr_lo, addr_hi, bank`). `bank=0` means a 64K-address breakpoint;
  `bank > 0` selects a physical bank (the plugin offsets by `-1`
  internally). Returns a `u16` ID.
* **`CMD_READ_MEM` payload starts with a reserved byte**, then
  `addr:u16, size:u16`. The reserved byte is mandatory.
* **`SET_REGISTER` register numbering** is custom (not the Z80 encoding):
  see `SET_REG_NUMS` in `cspect_dzrp.py`. PC=0, SP=1, AF=2, etc.

## Running the tests (offline, no CSpect required)

```bash
cd tools/cspect_dzrp
python3 -m unittest test_cspect_dzrp.py -v
```

Spins up an in-process fake plugin on a random localhost port and
round-trips every supported command. Verifies: frame encoding, register
parsing, breakpoint lifecycle, async pause notification, set-register
payload shape, seqno wraparound (skipping 0).

## Demo CLI

```bash
# Connect, dump current state.
python3 cspect_dzrp.py

# Run CSpect to PC=0x01ED, then dump regs + 64 bytes at HL.
python3 cspect_dzrp.py --run-to 0x01ED

# Dump 256 bytes at a specific address.
python3 cspect_dzrp.py --mem 0x3EAF --mem-size 256
```

## G46(b) ground-truth example

```bash
# Reproduce the exact comparison points jnext is failing.
python3 example_g46b_diff.py --pc 0x01ED --size 64 > cspect-g46b.md
# ... then in jnext, dump the same and diff:
diff cspect-g46b.md jnext-g46b.md
```

The script:

1. Runs CSpect until PC = `$01ED` (just before RST `$28`).
2. Dumps regs, NR `$50..$57`, 64 bytes at HL, and a handful of
   informative NextRegs (`$03`, `$07`, `$43`, `$8C`, `$8E`).
3. Continues to `$0028` (RST `$28` entry), dumps again — slot mapping
   often changes here.
4. Emits markdown so a `diff` against jnext's matching dump is clean.

## Known limitations

* **No watchpoints.** The plugin supports them
  (`CMD_ADD/REMOVE_WATCHPOINT`); we haven't exposed them yet — easy to
  add (`addr:u16, size:u16, access:u8` where bit 0 = read, bit 1 = write).
* **No bank write.** `CMD_WRITE_BANK` (8 KiB at a time) is unimplemented.
* **No sprite/border/state commands** — these are useful only for full
  debugger UIs, not for ground-truth diffing.
* **Step-into/step-out aren't real.** As noted above, the plugin
  doesn't expose them; we approximate step-over by `CONTINUE
  tmp_bp1=PC+n`. For instructions whose length you don't know
  statically (e.g. prefixed opcodes, `RST`s that may bank-flip), prefer
  setting two temp breakpoints flanking a region.
* **64K-only temp breakpoints.** `CMD_CONTINUE`'s in-payload temp BPs
  are 16-bit addresses with no bank selector. For bank-aware BPs use
  `add_breakpoint(addr, bank)` and clean up afterwards.
* **No multi-client support.** The plugin accepts one client at a
  time; if you re-connect, the previous breakpoint map is wiped.
* **`PAUSE` notifications can race the `CONTINUE` response.** Our
  `_request()` drains stray notifications while waiting for a response;
  this is correct but means a hyperactive CPU can queue several pauses
  before you call `wait_for_pause()`. Notifications are kept in
  `client.notifications`.
