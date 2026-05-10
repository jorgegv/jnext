# Pass-18 DivMMC + SD + SPI — Fix-of-Reviewer Review (V18-DIVMMC-NIT-01)

- **Date**: 2026-05-10
- **Branch**: `task2/verify18-divmmc-sd-spi-fix-review`
- **Fix commit under review**: `a15bfc8` (off reviewer HEAD `91e55c9`)
- **Reviewer**: independent Pass-18 fix-reviewer (this document)
- **Verdict**: **APPROVE**

---

## 1. Mandate

The Pass-18 reviewer at `91e55c9` issued an APPROVE-WITH-NITS verdict with
one promoted nit (V18-DIVMMC-NIT-01): the `default:` branch of
`SdCardDevice::receive()` (states RESPONDING / SENDING_DATA / WRITE_RESP,
non-CMD-start byte) was returning `0xFF` without advancing the response
stream, diverging from the VHDL full-duplex SPI semantics. The
fix-of-reviewer commit `a15bfc8` claims to:

1. Modify `src/peripheral/sd_card.cpp` to delegate to `send()` in the
   non-CMD-start sub-case of the default branch.
2. Add 2 discriminative regression tests (SD-30, SD-31) in
   `test/sdcard/sdcard_test.cpp`.
3. Verify the discriminative-test sandwich.

This document independently re-verifies all of the above.

---

## 2. Source-code diff verification

`git -C <wt> show a15bfc8 -- src/peripheral/sd_card.cpp` shows the
single behavioural edit (around `sd_card.cpp:286-311`):

- The CMD-start branch now `return 0xFF;` explicitly (was a `break;`).
- A new tail in the default branch:
  ```cpp
  // ...full VHDL/comment block referencing spi_master.vhd:104-117 +
  // :148-168 + zxnext.vhd:3270-3298...
  return send();
  ```

This is exactly the surgical fix the Pass-18 reviewer requested. No
collateral changes elsewhere in `sd_card.cpp`.

---

## 3. VHDL-faithfulness audit

### 3.1 `serial/spi_master.vhd:104-117` (output shift register)

```vhdl
process (i_CLK)
begin
   if rising_edge(i_CLK) then
      if i_reset = '1' then
         oshift_r <= (others => '1');
      elsif spi_begin = '1' and i_spi_rd = '1' then
         oshift_r <= (others => '1');
      elsif spi_begin = '1' and i_spi_wr = '1' then
         oshift_r <= i_spi_mosi_dat;
      elsif state_r(0) = '1' then
         oshift_r <= oshift_r(6 downto 0) & '1';
      end if;
   end if;
end process;
```

A `spi_begin` pulse on EITHER `i_spi_rd` (port_EB read) OR `i_spi_wr`
(port_EB write) starts a transfer. The only difference is what MOSI
shifts out (all-ones for reads, the CPU's byte for writes). The state
machine clocks 8 SCK edges in both cases.

### 3.2 `serial/spi_master.vhd:148-168` (input shift register + miso_dat)

```vhdl
process (i_CLK)
begin
   if rising_edge(i_CLK) then
      ...
      elsif state_r0_d = '1' then
         ishift_r <= ishift_r(5 downto 0) & i_spi_miso;
      end if;
   end if;
end process;

process (i_CLK)
begin
   if rising_edge(i_CLK) then
      ...
      elsif state_last_d = '1' then
         miso_dat <= ishift_r & i_spi_miso;
      end if;
   end if;
end process;
```

The input shift register samples MISO on every rising-edge clock where
`state_r0_d='1'` — i.e. on every SCK toggle regardless of whether the
transfer was kicked by `i_spi_rd` or `i_spi_wr`. `miso_dat` captures the
final 8-bit value at `state_last_d='1'` (transfer complete).

Consequence: SPI in this core is **strictly full-duplex**. Every
`OUT (0xEB),val` causes a byte to be **clocked out on MOSI AND clocked
in on MISO**. The slave's response stream advances by one byte regardless
of whether the host issues `i_spi_wr` or `i_spi_rd`.

### 3.3 `zxnext.vhd:3270-3298` (master instantiation)

```vhdl
spi_master_mod: entity work.spi_master
port map (
   i_CLK          => i_CLK_CPU,
   i_reset        => '0',
   i_spi_rd       => port_eb_rd,
   i_spi_wr       => port_eb_wr,
   i_spi_mosi_dat => cpu_do,
   o_spi_miso_dat => port_eb_dat,
   ...
);
```

Confirms `port_eb_wr` (= CPU `OUT (0xEB),val`) drives `i_spi_wr`, and
`port_eb_rd` (= CPU `IN A,(0xEB)`) drives `i_spi_rd`. Both kick the
master into a transfer cycle, both advance MISO.

### 3.4 Conclusion

Pre-fix `SdCardDevice::receive(0xFF)` returning `0xFF` while in
RESPONDING/SENDING_DATA/WRITE_RESP **violated VHDL full-duplex
semantics**: the next response byte would not be observable on MISO,
and `resp_idx_` / `data_idx_` would stay frozen. The post-fix
`return send();` perfectly mirrors what the VHDL master would clock out
(the byte from the slave's MISO line) AND advances the response-stream
pointer one position. **VHDL-faithful.**

---

## 4. Discriminative-test review

### 4.1 SD-30: CMD17 SENDING_DATA full-duplex

The test:
1. Mounts a 4-sector image.
2. Runs `init_card`, issues CMD17 sector=1 via `send_cmd_r1` (returns
   R1 via repeated `read_data()`-style polls).
3. Polls 16 `receive(0xFF)` looking for the `0xFE` data token.
4. Calls `send()` to read sector-1 byte 0 (= `0x01` per `make_image`).
5. Calls `receive(0xFF)` (the discriminative line) — expects byte 1 of
   sector 1 (= `0x00`).
6. Calls `send()` again — expects byte 2 of sector 1 (= `0x00`).

Pre-fix expected behaviour: step 3 returns 0xFF every iteration
(state frozen), so b_token never finds 0xFE → b_token=0xFF. Then step 4
returns the queued 0xFE token instead of byte 0 (data0=0xFE). Step 5
returns 0xFF default. Step 6 returns byte 0 = 0x01. So pre-fix
expected: `r1=0 b_token=0xFF data0=0xFE rx_during_send=0xFF after=0x01`.

Post-fix expected: step 3 advances through `{0xFE}` (resp_buf_) and
returns 0xFE on first or later iteration. Step 4 returns data byte 0
(0x01). Step 5 returns data byte 1 (0x00). Step 6 returns data byte 2
(0x00).

**Independent sandwich run confirms exactly this.** See § 5.

### 4.2 SD-31: CMD13 RESPONDING full-duplex

The test:
1. Issues CMD13 raw via 6 `receive()` calls (cmd byte + 4 arg + CRC).
2. After the 6th byte, `process_command` puts state into RESPONDING
   with resp_buf_ = `{0xFF, R1=0x00, R2=0x00}`.
3. Polls 16 `receive(0xFF)` looking for the first non-0xFF.
4. Calls `send()` to read the R2 status byte.

Pre-fix expected: step 3 returns 0xFF unconditionally → r1_via_receive
stays 0xFF. Then step 4 returns the first byte of resp_buf_ = 0xFF
(NCR), then 0x00. So `r1_via_receive=0xFF r2_status=0xFF` (the first
poll-via-send leaves resp_idx_ at 1 with R1=0x00 still pending; the next
`send()` returns R1, not R2 — actually step 4 directly returns R1=0x00,
since send() advances from resp_idx_=0 → 1 here). Test fails on
r1_via_receive=0xFF check.

Post-fix expected: step 3 — first iteration delegates to send(), returns
0xFF (NCR) at resp_idx_=0→1. Second iteration: returns R1=0x00 at
resp_idx_=1→2. Loop breaks with r1_via_receive=0x00. Step 4: send()
returns R2=0x00 at resp_idx_=2→3. Test passes.

**Independent sandwich run confirms.** See § 5.

### 4.3 Discriminative quality

Both tests fail with at least one assertion violated when the fix is
absent. Specifically:
- SD-30 fails on `b_token != 0xFE` (255 != 254), `data0 != 0x01`
  (254 != 1), `rx_during_send != 0x00` (255 != 0). Three independent
  observation points → strong discrimination.
- SD-31 fails on `r1_via_receive != 0x00` (255 != 0), `r2_status != 0x00`
  (255 != 0). Two observation points → adequate discrimination.

Both tests are anchored to make_image's deterministic sector-1 pattern
and CMD13's hardcoded R2=0x00 response, so the expected post-fix values
are unambiguous.

**Tests are genuinely discriminative.** No risk of false-positive PASS
under the pre-fix code.

---

## 5. Independent sandwich verification

### 5.1 Baseline (post-fix, as committed)

```text
ctest --output-on-failure:       38/38 PASS
./build/test/sdcard_test:        32/32 PASS  (incl. SD-30, SD-31)
./build/test/fuse_z80_test ...:  1356/1356 PASS
```

### 5.2 Pre-fix (sd_card.cpp only reverted)

I manually reverted the `return send();` plus the CMD-start branch's
`return 0xFF;` (restoring the pre-fix shape with `break;`), rebuilt
the sdcard_test target, and ran:

```text
FAIL SD-30: ... r1=0 b_token=255 data0=254 rx_during_send=255 after=1
FAIL SD-31: ... r1_via_receive=255 r2_status=255
Total: 32  Passed: 30  Failed: 2  Skipped: 0
```

Matches the predictions in §§ 4.1 and 4.2 exactly.

### 5.3 Post-fix (restored via `git checkout`)

```text
Total: 32  Passed: 32  Failed: 0  Skipped: 0
```

**Sandwich verified independently.** Tests are discriminative and the
fix resolves both rows.

---

## 6. Side-effect analysis

The default branch of `receive()` is only entered for states
RESPONDING, SENDING_DATA, WRITE_RESP (the explicit cases cover IDLE,
RECEIVING_CMD, RECEIVING_DATA). In all three default-branch states the
card is **actively driving its response stream on MISO**, so delegating
to `send()` is semantically correct.

Potentially-affected transitions in `send()`:

1. **RESPONDING → IDLE** (resp_buf_ drained, no pending_write_after_r1_).
   Triggering this via the write path is correct: a real SPI cycle would
   advance the slave state regardless of whether the host port-read or
   port-wrote.

2. **RESPONDING → RECEIVING_DATA** (resp_buf_ drained,
   pending_write_after_r1_ set). This is the CMD24 R1 → data-phase
   bridge. Pre-fix, a host that issued CMD24 then sent a poll-write
   (rather than a poll-read) would leave the bridge armed and the state
   stuck. Post-fix, the bridge fires correctly when the R1 byte has been
   clocked out via any port-EB access. **VHDL-faithful.**

3. **SENDING_DATA mid-block** → no state transition, just `data_idx_++`.
   Symmetric.

4. **SENDING_DATA past-EOF CMD18** → emits 0x08 data-error token and
   transitions to IDLE, clearing multi_block_. Triggering via the write
   path is harmless (host sees 0x08 on a port read of port_EB_dat AFTER
   the write, which is what the VHDL pipeline would deliver).

5. **WRITE_RESP → IDLE** (resp_buf_ drained). Symmetric.

The `exchange()` function (legacy fallback) already calls
`receive() + send()` in sequence (lines 113-120 of sd_card.cpp). Post-fix,
calling `receive(non-CMD)` standalone now produces the same MISO byte
that `exchange()` would return (since `exchange()`'s `receive()` returns
0xFF discarded, then `send()` produces the response). The only
behavioural difference is that post-fix `receive(non-CMD)` advances the
stream ONCE, whereas pre-fix `exchange(non-CMD)` advanced ONCE too (via
`send()`). So `receive()` and `exchange()` are now consistent for the
default branch.

**SpiMaster wiring check** (`spi.cpp:188-195`):

```cpp
void SpiMaster::write_data(uint8_t val) {
    SpiDevice* dev = active_device();
    if (dev) {
        rx_data_ = dev->receive(val);
        ...
    } else { rx_data_ = 0xFF; }
}
```

Post-fix, `write_data(0xFF)` while the SD card is in RESPONDING/SENDING_DATA/
WRITE_RESP returns the actual next response byte (advances stream).
The subsequent `read_data()` returns `prev = rx_data_` (which is now
the real next response byte) and calls `send()` to capture the byte
AFTER that. Pre-fix the same sequence returned `0xFF` from
`write_data()` and only the `read_data()`-triggered `send()` produced
useful response bytes.

For the boot path (TBBlue + FatFs + esxdos), the polling idiom is a tight
read loop (`IN A,($EB) / CP $FF / JR Z,$-4`) with no interleaved writes,
so the behavioural difference is invisible — both pre-fix and post-fix
deliver the same R1 byte after enough read iterations. Confirmed by the
commit message ("Boot-path impact zero").

For NON-boot-path callers (custom assembly that interleaves writes mid-
response stream, or future host code), the post-fix behaviour is the
only VHDL-faithful one.

**No regressions detected.** The change is strictly more correct, and
the only known callers (boot firmware) are unaffected.

---

## 7. Possible-side-effect probe matrix

| Concern | Pre-fix | Post-fix | Verdict |
|---|---|---|---|
| Quiescent IDLE/RECEIVING_CMD/RECEIVING_DATA tx=non-CMD | unchanged (explicit case) | unchanged | ✓ no change |
| CMD-start byte mid-response | resets to RECEIVING_CMD, returns 0xFF | identical (explicit return 0xFF added — equivalent to old break) | ✓ no change |
| RESPONDING tx=0xFF | returns 0xFF, no advance | returns next byte, advances | VHDL-faithful |
| SENDING_DATA tx=0xFF mid-data | returns 0xFF, no advance | returns next byte, advances | VHDL-faithful |
| WRITE_RESP tx=0xFF | returns 0xFF, no advance | returns response token, advances | VHDL-faithful |
| CMD24 R1→data bridge via write-poll | armed but stuck if no read follows | fires correctly | bug fix |
| CMD18 past-EOF via write-poll | data-error token never observed via write side | observable | bug fix |
| Boot path (TBBlue/FatFs/esxdos) | OK (read-only poll) | OK (read-only poll) | unchanged |
| `exchange()` legacy fallback | OK | OK (still works, slightly faster state advance) | unchanged |

---

## 8. Test-invariant report

| Suite | Result |
|---|---|
| `ctest --output-on-failure` | 38/38 PASS |
| `./build/test/fuse_z80_test build/test/fuse` | 1356/1356 PASS |
| `./build/test/sdcard_test` | 32/32 PASS |
| Sandwich pre-fix sdcard_test | 30/32 PASS (SD-30, SD-31 FAIL — discriminative confirmed) |

---

## 9. Final verdict

**APPROVE** — the fix is VHDL-faithful, the discriminative tests are
genuinely discriminative (sandwich-verified independently), and no side
effects on the boot path or any other state transition were identified.

The Pass-18 subsystem may legitimately be declared converged with this
fix landed.
