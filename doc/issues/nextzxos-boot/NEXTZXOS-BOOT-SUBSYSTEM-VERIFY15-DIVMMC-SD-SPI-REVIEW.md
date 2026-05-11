# Pass-15 Verify-Audit Review — DivMMC + SD + SPI Subsystem

**Date:** 2026-05-10
**Reviewer branch:** `task2/verify15-divmmc-sd-spi-reviewer`
**Worktree:** `.claude/worktrees/task2-verify15-divmmc-sd-spi-reviewer`
**Audit branch under review:** `task2/verify15-divmmc-sd-spi` HEAD `b7ab00a`
**Audit fix commit under review:** `d0b2cb2`
**Reviewer:** Pass-15 independent reviewer (no access to prior Pass-1..14
report files, VHDL + SD spec as oracle).

## Verdict

**APPROVE.**

The single Pass-15 finding (V15-DIVMMC-01) has been independently
verified against the SD Physical Layer Simplified Spec § 7.3.3.3, the
fix is correctly placed and minimal, and the discriminative test SD-28
fires exactly on the pre-fix shape.

## 1. Spec correctness — SD § 7.3.3.3

The audit's spec citation is correct.

  * SD Physical Layer Simplified Spec § 7.3.3.3 *Data Response Token
    format* defines a 1-byte token with layout `xxx0_sss1` where the
    high three bits are reserved/don't-care, bit 4 is fixed `0`, bit 0
    is fixed `1`, and bits [3:1] = `sss` carry the status:
    - `010` = data accepted              → token byte `xxx0_0101` = 0x05
    - `101` = data rejected (CRC error)  → token byte `xxx0_1011` = 0x0B
    - `110` = data rejected (write error) → token byte `xxx0_1101` = 0x0D
  * 0x05/0x0B/0x0D are exactly the three values the audit names. The
    fix's choice of 0x0D for "host fstream rejected the write" is the
    spec-correct mapping for the canonical real-card cause (write-
    protect tab engaged, or media commit failure).

The same three values were already used by V12-DIVMMC-02 (past-EOF
0x0D), so the encoding is consistent across the file.

## 2. Fix correctness

Reviewed `d0b2cb2^` → `d0b2cb2` for `src/peripheral/sd_card.cpp`. All
three audit-claimed properties are verified:

  * **`file_.good()` after write+flush** — `file_.good()` is checked
    on line 228, immediately after the `file_.write()` (line 204) and
    `file_.flush()` (line 205) pair. `good()` reports `(eofbit |
    failbit | badbit) == 0`, so it captures *both* the silent-fail
    case (`failbit` set by the standard library when the underlying
    file lacks write permission) and a `badbit` real I/O error. Order
    is correct: flush forces any buffered failure to surface before
    the check.

  * **`file_.clear()` resets sticky failbit** — line 242. Without the
    `clear()`, the next CMD17/CMD18 `seekg`/`read` on the same stream
    would short-circuit on the persisted bit and silently return
    nothing, producing further downstream divergence. With `clear()`,
    each command attempts the I/O fresh and re-fails symmetrically if
    permissions are still bad. Correct.

  * **`write_ok` flag drives the response token** — line 234 sets
    `write_ok = false` inside the failure branch; line 254 emits the
    token via `static_cast<uint8_t>(write_ok ? 0x05 : 0x0D)`. The
    token correctly maps to 0x0D when either (a) the past-EOF guard
    triggers (V12-DIVMMC-02 pre-existing) or (b) the new fstream
    failure path triggers (V15-DIVMMC-01). One consistent code path.

The fix is minimal, correctly placed, well-commented, and consistent
with the V12/V14 past-EOF family already in the file.

## 3. Discriminative test SD-28

The test (`test_sd_28_cmd24_ro_image_write_error`,
`test/sdcard/sdcard_test.cpp:1424-1525`) is genuinely discriminative.
Verified by reverting the fix and running:

  * **Pre-fix (revert)**: SD-28 reports
    `r1=0 resp=5 r1_rw=0 resp_rw=5` →
    **FAIL** (RO-mounted image returns 0x05 instead of 0x0D).
    Total: `29 Passed: 28 Failed: 1`.
  * **Post-fix (restore)**: Total: `29 Passed: 29 Failed: 0`.

The test also includes a symmetric RW-remount guard (lines 1488-1510):
the same image, re-mounted with `chmod(0644)`, must accept the same
CMD24 with R1=0x00 + 0x05. This rules out a false-positive from an
unrelated CMD24 regression (e.g. an accidental break of the past-EOF
or pre-token state machine). The dual-arm structure mirrors the prior
SD-22..SD-27 tests and is robust against false positives.

A minor practicality note: the test relies on POSIX `chmod` and gives
up via `skip()` if the chmod call itself fails. That is acceptable
defensive coding. On platforms where `chmod(0444)` does not actually
prevent `fstream::open` for write (some non-POSIX hosts; not our
canonical Linux build), the test would skip instead of false-pass —
which is the right disposition.

## 4. Full Release-mode test suite

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
cmake --build build -j$(nproc)             # clean, no audited-subsystem warnings
ctest --test-dir build --output-on-failure # 38/38 PASS
./build/test/fuse_z80_test build/test/fuse # 1356/1356 PASS
./build/test/sdcard_test                   # 29/29 PASS, 0 fail, 0 skip
```

Zero FAILs, zero regressions. Worktree was missing a `roms` directory
(common across worktrees); created a symlink to `/home/jorgegv/src/
spectrum/jnext/roms` so the `nextboot.rom` link target resolved. The
symlink is a workspace artefact and is not committed (working tree
shows only `?? roms`).

## 5. Hunt for missed cases

### CMD25 multi-block write — N/A

CMD25 is **not implemented** in `SdCardDevice::process_command()`. The
command-dispatch switch (lines 464-493) covers CMD0/1/8/9/10/12/13/16/
17/18/23/24/55/58 and falls through to the illegal-command path
otherwise. CMD25 lands on the `default:` branch and queues an R1 with
bit 2 (illegal command) set, never entering a data phase. Boot-path
firmware (TBBlue/FatFs/esxdos) does not exercise CMD25. No analogous
write-success-without-failbit-check site exists in the absence of
CMD25, so V15-DIVMMC-01 closes the family.

### Read paths (CMD17/CMD18) — fstream failbit not checked

The read paths in `cmd17_read_single_block` (lines 666-667),
`cmd18_read_multiple_block` (lines 708-709), and CMD18 between-blocks
re-load (lines 402-404) all use `seekg + read` without a subsequent
`file_.good()` check. If the underlying host file genuinely failed
mid-read (rare on a regular file: stat'd size matches, `seekg` succeeds,
but `read` returns short — would require external truncation or a
hardware I/O error during the run), the card would emit stale or
partial `data_block_` content as if valid.

The audit's symmetric-with-past-EOF reasoning still holds — the past-
EOF guard (`byte_addr + 512 > file_size_` covered by V12-DIVMMC-04 +
V14-DIVMMC-01) catches the canonical case. A genuine fstream-read
failure of an in-bounds sector is class-(b) hypothetical (vanishingly
rare in practice for a held-open file backed by a regular fs), and
adding a `good()` check on the read paths would emit the data error
token 0x08 (out-of-range/generic-error mnemonic, see V14-DIVMMC-01
comment) — but that token-mnemonic mismatch (it's labelled OECR with
sss bits semantically "out of range") is exactly why this is not a
clean Pass-15 ask: the cleanest spec-faithful response to a read
fstream-fail is still 0x08, just with the **generic-error** bit
(LSB = 0x01) rather than the **out-of-range** bit. Surfacing that as a
follow-up class-(b) is reasonable — but it is not a blocker for this
review and does not invalidate V15-DIVMMC-01's narrow scope.

I am NOT promoting this to a class-(c) reviewer-finding for the
following reasons:

  1. The class-(b) probability bar (real-world reproducibility) is not
     met: a held-open `std::fstream` to a regular file on a working
     filesystem essentially never short-reads in-bounds.
  2. The audit explicitly documents the scope as "CMD24 post-CRC
     commit" — extending to read paths would broaden the audit beyond
     its declared scope.
  3. The fix-by-symmetry argument cuts both ways: the past-EOF guard
     already catches the canonical real-world failure mode for read
     paths (truncated image, host hands jnext a too-short image). The
     residual fstream-fail-mid-read window is a pure tail-of-tail.

I am noting it here as a **deferred class-(b)** for any future pass
that explicitly walks the SD read-error space.

### `mount()` / `unmount()` / seek paths — clean

  * `mount()` already detects open failure (line 31, fall-through to
    RO at line 33; full failure at line 34 returns false).
  * `unmount()` close+reset is unconditional.
  * `seekg(0, std::ios::end)` at line 41 to read file_size_ — if this
    fails, `tellg()` returns -1 which casts to a huge unsigned, but
    every downstream guard uses `byte_addr + 512 > file_size_` which
    would either always-trigger (real corruption) or always-pass
    (impossibly large size). Defensive against the realistic failure
    modes.
  * `seekp` in CMD24 (line 203) is followed immediately by the write
    + good() check, so a seek failure also surfaces as `!good()` and
    correctly produces 0x0D.

No additional missed sites.

## 6. Conformance to project guidance

  * Comment style: VHDL/spec-citation header, class label, symmetric-
    fix cross-references — consistent with V12/V13/V14 style.
  * Test naming: `test_sd_28_*` continues the SD-NN series; comment
    block follows the same format as SD-22..SD-27.
  * No `ChangeLog` / `FEATURES.md` / `TODO.md` updates needed — the
    project's CLAUDE.md says these only get updates for "significant"
    user-visible features; a class-(c) latent fix in an RO-mount edge
    case does not meet that bar.

## 7. Risk surface

Class-(c) latent. Boot-path firmware writes succeed because the
canonical NextZXOS image is opened RW. The fix surfaces correctly on:

  * RO-mounted user image (forensic / system-image inspection).
  * Disk-full or filesystem-quota exhaustion mid-write.
  * Real I/O error mid-write.

The fix is purely additive (failure case) and cannot regress the
happy path: RW-image writes still emit 0x05, all 28 pre-existing
sdcard tests pass unchanged.

## 8. Summary

| ID            | Class | Verdict  | Rationale                                  |
|---------------|-------|----------|--------------------------------------------|
| V15-DIVMMC-01 | (c)   | APPROVE  | Spec-correct; fix minimal; test discriminates pre-fix; no regressions |

Pass-15 introduced 1 finding, the audit fixed it correctly with a
discriminative regression test, and the full Release test suite is
green (38 ctest + 1356 FUSE + 29 sdcard, 0 FAIL, 0 SKIP regressions).

A deferred class-(b) note for the read-path fstream-failbit window
is recorded above for any future audit pass explicitly exploring the
read-error space; it is NOT a blocker for this review.

## Final return

```json
{
  "verdict": "APPROVE",
  "findings_verified": 1,
  "discriminative": ["SD-28"],
  "issues": [],
  "tests_passed": true,
  "head_sha": "b7ab00a5db0863cdf263cf975717c8690471c229"
}
```
