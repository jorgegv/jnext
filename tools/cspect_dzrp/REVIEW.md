# Code review of cspect_dzrp/

**Reviewer**: independent agent
**Date**: 2026-05-08
**Commit reviewed**: 4f0c9b2
**Verdict**: APPROVE-WITH-NITS

## Summary

The DZRP client is small, focused, well-documented, and matches the upstream DeZogPlugin
wire format on every command I cross-checked against `Server.cs`/`Commands.cs`. The
asymmetric framing fix is consistent and correctly applied. Tests are clean and exercise
all happy paths through an in-process fake. Found two concurrency bugs (one with non-zero
real-world consequence: `step_over_byte` consuming a stale `PAUSE`; one latent: `close()`
racing in-flight requests), one ergonomic footgun in `step_over_byte` already documented
but worth surfacing more loudly, and several test/error-handling gaps. No CRITICAL bugs.

## Bugs found (severity ordered)

### CRITICAL

None. All command payload layouts match upstream.

### HIGH

**H1. `step_over_byte` consumes a stale notification and returns without stepping**
`cspect_dzrp.py:505-515` calls `cont(tmp_bp1=PC+n)` then `wait_for_pause()`. But
`wait_for_pause()` (line 526-527) returns the **first queued** notification immediately
without checking that it is the one we just produced. If `self.notifications` already
contains a stale `PauseNtf` (for example, from a previous `cont()` whose
`wait_for_pause` was skipped, or a manual break that arrived during a previous
`_request` and was stashed by `_handle_notification`), `step_over_byte` returns that
*stale* `PauseNtf` and the CPU has not actually stopped at `PC+n` yet. The caller
believes the step happened. Worse, the next `_request()` will collide with the
*real* pause notification when it eventually arrives.

Reproduction: queue a stale notification on `self.notifications` (any `_request` after
a manual break does this), then call `step_over_byte`. It returns instantly, before
the CPU has run.

Fix: `step_over_byte` should drain `self.notifications` *before* calling `cont()`, and
`wait_for_pause` should accept a "wait until *new* notification" semantics, OR the
client should clear `self.notifications` whenever a new `cont()` is issued. Same
hazard applies anywhere the public `wait_for_pause()` is called: callers must
remember to drain first. Not documented.

**H2. `wait_for_pause()` does not hold `self._lock`**
`cspect_dzrp.py:522-547` reads from `self._sock` and mutates `self.notifications`
without acquiring `self._lock`. If any other thread calls `_request()` concurrently
(e.g., a watchdog thread polling `read_mem`), both threads will read from the same
socket; bytes from the response and from a notification can interleave, producing
either a parser exception or — worse — silent corruption (a stray response is
*silently dropped* per `cspect_dzrp.py:542`, so a wrongly-attributed frame can
disappear without trace).

The README claims at line 184-185 "PAUSE notifications can race the CONTINUE
response. Our `_request()` drains stray notifications while waiting for a response;
this is correct." That claim is true *only* under serial use. Multithreaded use is
undocumented and broken.

Fix: either (a) acquire `self._lock` in `wait_for_pause`, with a re-entrant
"already locked" path for the common single-thread case, or (b) document explicitly
that the client is single-threaded and assert it (e.g., refuse to enter
`wait_for_pause` if `_lock` is held by another thread). Option (b) is simpler and
matches actual usage in the four scripts under review.

**H3. `close()` is unsafe under concurrent `_request()`**
`cspect_dzrp.py:328-338` calls `self._send_only(Cmd.CLOSE)` which is **not** under
`self._lock`. If thread A is mid-`_request()` (lock held, waiting on response), and
thread B calls `close()`, B's CLOSE bytes interleave with A's request payload on the
TCP send side, then B closes the socket while A is still trying to read. A gets
`socket closed by peer` — but its caller cannot tell whether the operation
succeeded server-side or not.

Fix: `close()` should acquire `self._lock` before sending CMD_CLOSE, and should
*not* send CMD_CLOSE if `self._sock is None` (idempotency for double-close).
Currently double-close is half-safe (the `is not None` check at line 329 handles
the trivial double-close, but only because `close()` zeroes `self._sock` last).

### MEDIUM

**M1. `set_slot` discards a possibly non-zero error byte**
`cspect_dzrp.py:452-455` calls `_request(Cmd.SET_SLOT, ...)` and discards the return.
But Commands.cs:1069-1072 returns `[error:u8]` (currently always 0, but the byte is
defined). Real protocol surface area: any non-zero value is silently swallowed. If
the plugin author later wires this to "slot=8 invalid" or similar, jnext's diff
runs would report no error and produce silently bogus snapshots. Cheap fix:
```python
err = self._request(Cmd.SET_SLOT, ...)
if err and err[0] != 0:
    raise DZRPError(f"SET_SLOT returned error {err[0]}")
```

**M2. `decode_request` is too loose on length-field validation**
`cspect_dzrp.py:218-229` compares `length + 6 != len(frame)`. It does NOT bound-check
`length` against, say, 16 MiB or against `len(frame) - 6`. An attacker (or buggy
peer) sending `length=0xFFFFFFFF` would underflow on a real receive path that does
`recv_n(length)`. The actual receive path (`_recv_frame` at line 374) is similarly
unbounded: a malicious or corrupt server can pin the client into a 4 GiB read.
Practical risk is low (loopback only) but a one-line cap (`if length > 0x100000:
raise DZRPError(...)`) is cheap defense in depth.

**M3. `parse_registers` silently rewrites a non-8 slot count to 8**
`cspect_dzrp.py:277-279` sees `slot_count != 8` and "be defensive but don't fail".
But if the plugin ever genuinely changes the slot count, the client will continue
to read 8 bytes and produce wrong results without any error. A protocol change
should fail loudly. Either honor the count (read `slot_count` bytes) or raise.

**M4. CMD_INIT response: no length sanity check beyond `>= 5`**
`cspect_dzrp.py:411-420` accepts any string after the machine byte. If the response
is malformed and `name_bytes` contains non-ASCII garbage, the `errors="replace"`
hides the corruption. For a debug-tool client this is OK, but pair it with a debug
log entry on suspicious bytes.

**M5. `_recv_frame` does not guard against `length == 0`**
If server ever sends a frame with `length=0`, line 378 calls `_recv_exact(0)` which
returns `b""`, then line 379 raises `DZRPError("empty body")`. That error is
correct, but the *real* server has no path that produces this — except a malformed
peer. Worth handling more explicitly so the error message points at the corrupt
peer rather than at our own client.

### NITS

**N1. `set_register` value is masked but no warning on overflow**
`cspect_dzrp.py:428` — `value & 0xFFFF` silently truncates 32-bit ints. For an 8-bit
register (e.g., `R`, `IM`, `A`), the upper 8 bits are discarded by Commands.cs
(`(byte)value`), so the high byte is silently lost. Add a `if value & ~0xFFFF:`
warning, or document.

**N2. `add_breakpoint(bank=0)` magic-value semantics are documented in docstring
but not in type system**
The docstring at line 473-478 explains that `bank=0` means a 64K-address BP. But the
Python signature is `bank: int = 0`. Not a bug; just inviting the user to write
`bank=1` thinking "physical bank 1" when DeZog actually means "physical bank 0
plus 1". The ergonomic fix is two methods:
`add_breakpoint_64k(addr)` and `add_breakpoint_bank(addr, bank)`.

**N3. `Cmd.CLOSE` numerical value is sent as part of `_send_only(Cmd.CLOSE)`
without confirming the response is read**
The polite close at line 333 sends CMD_CLOSE but doesn't read the response. The
server *will* call `SendResponse()` (Commands.cs:578), which queues bytes, then we
close before draining them. On the server side this triggers a benign "Disconnected"
log line. Not a bug, but the `try / except: pass` at lines 332-335 swallows any
error including `BrokenPipe` from a server that closed first. Worth at least
logging.

**N4. `Registers.format()` does not show I' / R' or IFF**
Cosmetic: nothing in the protocol exposes IFF state from `GET_REGISTERS` (it isn't
in the layout), so it's correct. The README could note this gap.

**N5. README claim: "DZRP version reported by the plugin: 2.0.0"**
Verified against Commands.cs:29 `DZRP_VERSION = { 2, 0, 0 }` — accurate.

**N6. `dzrp_check.py:33` prints `DZRP v{v}` where `v` is an `InitInfo` dataclass
(prints the whole repr, not just the version)**
Cosmetic, but the f-string `f"DZRP v{v}"` will print
`DZRP vInitInfo(error=0, dzrp_version=(2,0,0), ...)` — ugly. Should be
`f"DZRP v{'.'.join(map(str, v.dzrp_version))} machine={v.machine_type} program={v.program_name!r}"`.

**N7. `g46b_at_1661.py:36` calls `c.cont()` with no `tmp_bp1`**
This is intentional — the persistent BP at `$1661` was just added — but it
relies on the persistent BP being effective even when `tmp_bp1=0` is sent.
Verified against Commands.cs:812-816: the server sets `bp1Enable = (byte != 0)`,
so `tmp_bp1=None` correctly translates to `bp1Enable=0`. No bug; the script
works only by virtue of the explicit `add_breakpoint($1661)` two lines earlier.
Worth a comment.

**N8. `_handle_notification` silently drops empty seqno=0 frames**
Line 401-402: `if not payload: return`. Is this ever a legal server frame? No
— `SendPauseNotification` always emits at least 5 payload bytes. So an empty
seqno=0 frame is a bug somewhere. Should at minimum log it.

**N9. `parse_pause_ntf` strips trailing NUL but only one**
Line 262-263: `if s and s[-1] == 0: s = s[:-1]`. Server always emits exactly one
NUL (Commands.cs:1497 `reasonString+"\0"`). OK. But if peer-side ever emits a
double-NUL, the second gets decoded as `\x00`. Minor.

**N10. No test for `wait_for_pause` timeout**
The async-pause test fires within 20 ms. There is no test for the timeout path
where the server *never* sends a pause. Should add: pass `timeout=0.1`, never
fire pause, expect `TimeoutError`.

**N11. No test for `seqno mismatch`**
`_request` line 394-397 raises `DZRPError("seqno mismatch")` if response seqno
disagrees with sent seqno. There is no test that drives this path. A
single-frame fake server response with the wrong seqno would cover it.

**N12. No test for `socket closed by peer` mid-request**
`_recv_exact` line 369-370 raises `DZRPError("socket closed by peer")`. Untested.
Easy: stop the fake server mid-request, call `_request`, expect `DZRPError`.

**N13. No test for partial-read recovery**
TCP can deliver any chunk size. `_recv_exact` already handles this correctly via
the loop, but there is no test that explicitly slow-feeds the client one byte at
a time to confirm.

**N14. No test for `read_mem(size > received_size)`**
Line 438-439 raises `DZRPError` if response size mismatches. A fake server
returning 3 bytes when 8 are requested would test this — currently untested.

## Wire-format audit

| Cmd | Direction | Server.cs / Commands.cs reference | Python ref | Match? |
|-----|-----------|-----------------------------------|-----------|--------|
| Frame request len | host→plugin | Server.cs:255-271 (`totalLength = 4 + 2 + MsgLength`, `MsgLength = data[0..3]`) | `cspect_dzrp.py:213` (`len(payload)`) | ✓ |
| Frame response len | plugin→host | Server.cs:551-557 (`length = byteData.Length + 1`) | `cspect_dzrp.py:235` (`len(seqno + payload)`) | ✓ |
| Frame notify len | plugin→host | Commands.cs:1501-1519 (`length = 6 + stringLen` covers seqno + ntf + reason + 3-byte addr + string) | `cspect_dzrp.py:248-252` (seqno + ntf_id + reason + 3 addr + str+NUL) | ✓ |
| `INIT` request | host→plugin | empty payload | empty | ✓ |
| `INIT` response | plugin→host | Commands.cs:562-568: `err(1) ver(3) machine(1) name+\0` | line 411-420 parses err, 3-byte ver tuple, machine, NUL-stripped string | ✓ |
| `GET_REGISTERS` request | host→plugin | empty | empty | ✓ |
| `GET_REGISTERS` response | plugin→host | Commands.cs:589-616: `12 LE words(24) + R(1) I(1) IM(1) reserved(1) + slot_count(1) + 8 slots(8) = 37` | `cspect_dzrp.py:267-286` parses exactly 37 bytes | ✓ |
| `SET_REGISTER` request | host→plugin | Commands.cs:627-680: `regnum(1) value:u16-LE(2)` | `cspect_dzrp.py:425-429` — `bytes([n]) + struct.pack("<H", v)` | ✓ |
| `READ_MEM` request | host→plugin | Commands.cs:1009-1014: `reserved(1) addr:u16(2) size:u16(2)` | `cspect_dzrp.py:436` — `bytes([0]) + pack("<HH", addr, size)` | ✓ |
| `READ_MEM` response | plugin→host | Commands.cs:1019-1024: `size` raw bytes | line 437-440 reads `size` bytes, validates length | ✓ |
| `WRITE_MEM` request | host→plugin | Commands.cs:1033-1043: `reserved(1) addr:u16(2) data:bytes` | line 442-446 — `bytes([0]) + pack("<H", addr) + data` | ✓ |
| `WRITE_MEM` response | plugin→host | empty | line 446 ignored | ✓ |
| `CONTINUE` request | host→plugin | Commands.cs:812-816: `bp1Enable:u8 bp1Addr:u16 bp2Enable:u8 bp2Addr:u16` (5 bytes) | line 488-500 — `pack("<BHBH", ...)` (5 bytes) | ✓ |
| `CONTINUE` response | plugin→host | Commands.cs:869: empty | line 500 ignored | ✓ |
| `NTF_PAUSE` | plugin→host | Commands.cs:1500-1519: `seqno=0 ntf=1 reason(1) addr_lo(1) addr_mid(1) addr_bank(1) reasonStr+NUL` | `cspect_dzrp.py:255-264` parses seqno-stripped body: ntf_id(1) reason(1) addr(3 LE incl. bank) str | ✓ |
| `PAUSE` request | host→plugin | empty | empty | ✓ |
| `PAUSE` response | plugin→host | Commands.cs:909: empty | empty | ✓ |
| `SET_SLOT` request | host→plugin | Commands.cs:1056-1058: `slot(1) bank(1)` | line 452-455 — `bytes([slot, bank])` | ✓ |
| `SET_SLOT` response | plugin→host | Commands.cs:1069-1072: `error(1)` | line 455 ignores it (see M1) | ⚠ payload ignored |
| `GET_TBBLUE_REG` request | host→plugin | Commands.cs:1322-1323: `reg(1)` | line 459 — `bytes([reg])` | ✓ |
| `GET_TBBLUE_REG` response | plugin→host | Commands.cs:1328-1335: `value(1)` | line 460-462 reads 1 byte | ✓ |
| `READ_PORT` request | host→plugin | Commands.cs:1082: `addr:u16(2)` | line 464-465 — `pack("<H", addr)` | ✓ |
| `READ_PORT` response | plugin→host | Commands.cs:1089-1093: `value(1)` | line 466-468 reads 1 byte | ✓ |
| `WRITE_PORT` request | host→plugin | Commands.cs:1106-1108: `addr:u16(2) value:u8(1)` | line 470-471 — `pack("<HB", ...)` | ✓ |
| `WRITE_PORT` response | plugin→host | empty | ignored | ✓ |
| `INTERRUPT_ON_OFF` request | host→plugin | Commands.cs:1280: `enable:u8(1)` | line 517-518 — `bytes([0/1])` | ✓ |
| `INTERRUPT_ON_OFF` response | plugin→host | Commands.cs:1291: empty | ignored | ✓ |
| `ADD_BREAKPOINT` request | host→plugin | Commands.cs:919 + Server.cs:507-524 (`GetLongAddress`): `addr_lo(1) addr_hi(1) bank(1)` (3 bytes) | line 479 — `pack("<HB", addr, bank)` (3 bytes; the `H` is `addr_lo,addr_hi` LE-packed exactly equivalent) | ✓ |
| `ADD_BREAKPOINT` response | plugin→host | Commands.cs:925-927: `id:u16(2)` | line 480-483 | ✓ |
| `REMOVE_BREAKPOINT` request | host→plugin | Commands.cs:937: `id:u16(2)` | line 485-486 — `pack("<H", id)` | ✓ |
| `REMOVE_BREAKPOINT` response | plugin→host | Commands.cs:941: empty | ignored | ✓ |
| `CLOSE` request | host→plugin | empty | empty | ✓ |
| `CLOSE` response | plugin→host | Commands.cs:578: empty | not read in `close()` (best-effort) | ✓ but see N3 |

**Two semantic notes (not bugs):**
1. `Cmd.READ_MEM` reserved byte: server explicitly skips it (Commands.cs:1010
   `CSpectSocket.GetDataByte();` discarded). Python sends `0x00`. Either value
   would work. Documented in Python.
2. `ADD_BREAKPOINT` long-address encoding: `bank=0` means 64K addr; `bank>=1`
   means physical bank `bank-1`. Server.cs:507-524 confirms. Python comment
   on line 478 is correct.

## Tests review

### Covered (good)

- Frame round-trip (encode → decode) for request, pause notification.
- Length-field correctness on both directions (the asymmetric framing fix).
- Seqno=0 rejection on requests.
- `parse_registers` happy path and too-short error path.
- Live socket round-trip via in-process `FakeCSpectServer`:
  `INIT`, `GET_REGISTERS`, `READ_MEM`, `GET_TBBLUE_REG`, `ADD_BREAKPOINT` +
  `REMOVE_BREAKPOINT`, `CONTINUE` + delayed `PAUSE` notification, `SET_REGISTER`
  payload shape inspection, seqno wraparound (260 calls across 1..255).

### Missing

- **No timeout test** (N10) — the most likely error mode for jnext use.
- **No seqno-mismatch test** (N11).
- **No peer-closed-mid-request test** (N12).
- **No partial-read test** (N13). TCP loopback usually delivers in one chunk
  but production code should handle dribbled bytes; the loop in `_recv_exact`
  is correct, but it's untested.
- **No malformed/short response test** (N14): fake server returns wrong-size
  `READ_MEM` payload, expect `DZRPError`.
- **No `set_register` test for each named register**. Only PC tested. Easy to
  parameterize.
- **No async-pause-during-request test**: the README claims `_request` "drains
  stray notifications while waiting for a response" — untested. Should
  inject a notification mid-request and confirm it lands in
  `self.notifications`.
- **No `__exit__` exception test**: ensure that an exception inside the `with`
  block still runs `close()`.
- **No double-close test**: `client.close(); client.close()` should not raise.
- **No re-init test**: `client.close(); client.connect(); client.init()` —
  does the seqno reset? (It doesn't; `self._seqno` persists across reconnect.
  Probably fine, but untested.)

### Test stub fidelity to real plugin

The `FakeCSpectServer` is a faithful happy-path mirror but does NOT mimic these
real-server behaviors that could mask bugs:

1. **Receive-while-pending rejection** (Server.cs:212-215): server throws if a
   second request arrives before a response has been sent. The fake never
   does this; concurrency tests would silently pass against the fake but fail
   against the real server.
2. **Async send semantics** (Server.cs:581 `BeginSend`): real server may
   batch multiple sends. Fake sends immediately.
3. **Connection drop on error** (Server.cs:444-460): real server `Shutdown`s
   the socket on protocol errors; fake just stops dispatching.

These are not bugs in the test — but the README should warn that these tests
do not certify multi-thread or error-path behavior against a real plugin.

## Documentation review

The README is generally accurate. Specific cross-checks against `Server.cs`
and `Commands.cs`:

| Claim | Source-of-truth check | Verdict |
|-------|----------------------|---------|
| "DZRP version reported by the plugin: 2.0.0" | Commands.cs:29 | ✓ accurate |
| "No CMD_GET_SLOTS … `12 LE words + R + I + IM + 0 + count(=8) + 8 slot bytes` = 37 bytes" | Commands.cs:589-616 | ✓ accurate (29+8 InitData call) |
| "ADD_BREAKPOINT takes a *long address* (3 bytes: addr_lo, addr_hi, bank)" | Server.cs:507-524 GetLongAddress | ✓ accurate |
| "bank=0 means a 64K-address breakpoint; bank>0 selects a physical bank (the plugin offsets by -1 internally)" | Commands.cs:741-756 SetBreakpointRaw | ✓ accurate (`if (bank > 0)` branch with `(bank-1) << 13`) |
| "READ_MEM payload starts with a reserved byte" | Commands.cs:1010 `GetDataByte()` discard | ✓ accurate |
| "SET_REGISTER register numbering … PC=0, SP=1, AF=2, etc." | Commands.cs:637-674 | ✓ accurate |
| "CSpect's CMD_CONTINUE returns an empty response immediately; the actual stop arrives later as a NTF_PAUSE" | Commands.cs:868-873 (response sent before `StartCpu(true)`) | ✓ accurate |
| "PAUSE notifications can race the CONTINUE response. Our `_request()` drains stray notifications while waiting for a response; this is correct" | Verified by reading code; but only correct *single-threaded* | ⚠ should note the threading caveat (see H2) |
| "No multi-client support. The plugin accepts one client at a time; if you re-connect, the previous breakpoint map is wiped" | `BreakpointMap = new Dictionary<...>()` in AcceptCallback (Commands.cs:Init via Server.cs:180); listener accepts ONE then `listener.Close()` (Server.cs:188) | ✓ accurate |
| Step-over-byte: "set tmp_bp1 = PC + instruction_length" | Implementation correct, but does NOT warn about taken-jumps and queued-stale-notifications (H1). | ⚠ incomplete warning |
| Frame format ASCII diagram on lines 87-91 | Bytes-on-the-wire | ✓ matches Server.cs/Commands.cs |
| `len = byte count after the length field (so seqno + cmd + payload, or seqno + ntf + payload)` | This describes the response/notification side. The request side `len = payload only`, NOT seqno+cmd. | ⚠ **The README is partially WRONG here.** The `len` semantics are *asymmetric* between request and response; the docstring on `encode_request` (line 202-210) gets this right, but the README ASCII diagram + the bullet "len is the byte count *after* the length field (so seqno + cmd + payload, or seqno + ntf + payload)" describes only response/notification, omitting that **on requests, len = payload only**. Fix the README. |

### Doc fix priority

1. **HIGH**: README §"Wire format" — clarify that `len` is asymmetric. The
   docstring in cspect_dzrp.py:8-15 *does* note this, but the README does not.
2. **MEDIUM**: README §"Subtleties" — add a bullet warning that
   `step_over_byte` is unreliable across taken jumps and stale
   notifications.
3. **LOW**: README §"Known limitations" — note that the test stub does not
   cover error-path or multi-client behaviors, so the fake-server-passing
   tests are *necessary* but *not sufficient* against real CSpect.
4. **LOW**: `Registers.format()` doc — note IFF1/IFF2 are NOT in
   `GET_REGISTERS` (server reads them via separate `cspect.GetRegs()` for
   `INTERRUPT_ON_OFF` only, not exposed). For ground-truth diffing this
   matters: jnext must NOT include IFF state in its matching dump or the
   diff will spuriously differ.

## Recommended fixes

### Required (blocks fully-trustworthy ground-truth runs)

1. **Fix H1** (`step_over_byte` stale-notification): drain `self.notifications`
   before `cont()` and assert that exactly one new notification arrived.
   Or expose `wait_for_new_pause(since=token)` semantics. File:
   `cspect_dzrp.py:505-515`.
2. **Fix the README's wire-format claim** to clarify the asymmetric `len`.
   File: `README.md:93-94`.

### Recommended (low-cost, high-value)

3. **Add timeout test, seqno-mismatch test, peer-closed test, partial-read
   test, malformed-response test** (N10–N14). These cover the failure modes
   most likely to bite jnext-vs-CSpect diff runs in practice. File:
   `test_cspect_dzrp.py`.
4. **Acquire `self._lock` in `wait_for_pause`** (H2) OR document explicitly
   single-thread-only. File: `cspect_dzrp.py:522-547`.
5. **Acquire `self._lock` in `close()`** (H3) and make it idempotent. File:
   `cspect_dzrp.py:328-338`.
6. **Validate `set_slot` response error byte** (M1). File:
   `cspect_dzrp.py:452-455`.
7. **Bound-check `length` on receive** (M2). File: `cspect_dzrp.py:374-382`.

### Nice-to-have

8. Fix `dzrp_check.py:33` to print version cleanly (N6).
9. Add comment in `g46b_at_1661.py:36` clarifying that the persistent BP
   is what stops execution, not `cont()` (N7).
10. Honor or fail on non-8 slot count (M3) — defensive code that hides bugs
    is worse than no defensive code.
11. Provide a typed `add_breakpoint_64k(addr) / add_breakpoint_bank(addr,
    bank)` pair (N2) — cheap ergonomic win, removes the `bank=0` magic value.

## Verdict justification

**APPROVE-WITH-NITS.** The wire-format implementation is correct on every
command, length accounting is right on both sides of the asymmetric framing,
and the unit tests cover every supported command's happy path. The two HIGH
issues (stale-notification consumption in `step_over_byte`; missing lock in
`wait_for_pause`/`close`) are concurrency hazards that **do not affect the
current G46(b) usage pattern** (single-threaded, dedicated CSpect instance),
which is why I'm not requesting changes — but they should be fixed before
this client is reused for a multi-threaded debugger UI or for any flow where
manual breaks are mixed with `step_over_byte`. The README has one inaccuracy
(asymmetric `len` not documented in the ASCII diagram) and one missing
warning (`step_over_byte` taken-jump caveat). Fixing those two doc issues
plus adding the H1 fix would warrant a clean APPROVE.

For the immediate G46(b) ground-truthing purpose: **trust the data this
client returns from CSpect**, with the single caveat that `step_over_byte`
should not be used until H1 is fixed. Stick to `cont(tmp_bp1=...)` +
`wait_for_pause()` + drain-notifications-between-runs idiom (which all four
example scripts do). Memory reads, register dumps, and NextReg snapshots are
trustworthy.
