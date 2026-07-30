# NextSync end-to-end verification against the emulated ESP-01

Record of the [issue #167](https://github.com/jorgegv/jnext/issues/167)
verification: does **NextSync** — the other headline consumer of the ESP-01
hardware besides NXtel — work end-to-end against jnext's emulated module?

**Verdict: yes.** The canonical NextSync 1.2 dot command completes a sync at all
three of its baud settings, and every transferred file is byte-identical by
`sha256sum`. Verified 2026-07-30 on jnext v0.99.82.

It is also **under test**: the `nextsync-func` regression row runs the vendored
NextSync 1.2 against a live server on every regression run and fails if a single
byte differs. The manual runs recorded here are the wider evidence — three baud
settings, five corpora, up to 768 KB — that the row samples one point of.

Downloads and sources are listed in [REFERENCES.md](../REFERENCES.md) — this
document does not repeat the URLs.

## What was exercised, and what that proves

NextSync is a different traffic shape from NXtel: sustained bulk transfer of
binary payloads from a PC on the LAN, over a UART the guest reprograms mid-run
to 1.152 or 2 Mbaud. Four things had to hold and all four did.

1. **The AT command set is sufficient.** The canonical 1.2 binaries use only
   `ATE0`, `AT+UART_CUR=`, `AT+CIPSTART=`, `AT+CIPSENDEX=`, `AT+CIPCLOSE` — every
   one implemented. Extracted from the shipped binaries with `strings`, so this
   is what the released dot actually sends, not what a fork's source suggests:

   | dot         | baud switch                    |
   |-------------|--------------------------------|
   | `sync`      | `AT+UART_CUR=1152000,8,1,0,0`  |
   | `syncfast`  | `AT+UART_CUR=2000000,8,1,0,0`  |
   | `syncslow`  | none (stays at 115200)         |

   **`AT+CIPMODE` is NOT used by any of the three.** jnext does not implement
   `AT+CIPMODE` passthrough (`esp_at.h`), and that is not a blocker for NextSync.
   The `AT+CIPMODE=1` path exists only in `nextsync_raw_io.c` in the ZX-Next-Unite
   fork, which that fork's `build.ps1` does not compile.

2. **Bulk transfer is reliable.** Zero retries, zero restarts and zero of the
   server's `Gee`/`Neex` mistransmit counters across every run below, including
   the server's `-u` mode whose 1455-byte payloads are nearly three times the
   Next's 512-byte UART RX FIFO. Nothing overran.

3. **Binary payloads survive.** The corpus deliberately contains every byte
   value, runs of `0x00` (the byte `AT+CIPSENDEX` terminates on), runs of `0xFF`,
   `CR`/`LF`/`0x1A`, and the literal strings `OK`, `ERROR`, `SEND OK`, `> ` and
   `+IPD,` — anything mishandled as a control character or a response frame would
   corrupt the file rather than garble a screen.

4. **A LAN-address server is reachable with no extra flags.** `AddressPolicy`
   refuses loopback but deliberately permits RFC1918, and an empty `--esp-allow`
   list means allow-all, so `--esp` alone is enough. Binding the server to
   `127.0.0.1` is refused by design; bind it to the host's LAN address.

## The working invocation

Followable top to bottom against a pristine image. **Do not skip step 2** — a
run without it reaches the NextZXOS command line and answers
`No such command, 0:1`.

**1. Prepare a private SD image.** jnext opens the image read-write and the sync
mutates it, so never point this at your master:

```
cp --reflink=auto ~/.jnext/sdcard/cspect-next-1gb-fixed.img /path/to/sd.img
```

**2. Install the dot command onto that image, at `/dot/sync`.** The dots are
vendored at `test/00regression/nextsync/dot/`; the three variants are separate
programs and all three were verified, so install whichever you intend to run.
`sdfile_tool` (built by `make unit-test-build`) does this with the project's own
FAT32 writer — no mtools, no external tool:

```
build/test/sdfile_tool put /path/to/sd.img \
    dot/sync     test/00regression/nextsync/dot/sync
build/test/sdfile_tool put /path/to/sd.img \
    dot/syncfast test/00regression/nextsync/dot/syncfast     # optional
```

Pass every `<path> <file>` pair to a single invocation when installing more than
one: each call reformats and re-emits the whole partition.

**3. Start the server** on the host, in the directory that mirrors the root of
the Next's SD card — the vendored copy is at
`test/00regression/nextsync/server/nextsync.py`:

```
python3 nextsync.py -a          # -a = ignore timestamps, always sync
```

> **It binds `0.0.0.0`**, i.e. every interface including any public one, and it
> has no option to narrow that. It is reachable at your LAN address, which is
> what the Next needs — but stop it when you are done rather than leaving it
> running. The `nextsync-func` regression row does not use this server for
> exactly this reason; it binds only the discovered private address.

**4. On the Next, once,** save the server address — it must be the host's LAN
address, because the ESP address policy refuses loopback. This writes
`c:/sys/config/nextsync.cfg`, which holds the address as bare ASCII with no
terminator:

```
.sync 192.168.100.238
```

**5. On the Next, thereafter:**

```
.sync                           # or .syncfast / .syncslow
```

Steps 4 and 5 driven headlessly — `--esp` is required, the ESP is off by
default. This one types `.sync` (step 5); step 4 is the same shape with
` 192.168.100.238` typed before the `enter`:

```
jnext --headless --machine next --esp --sdcard sd.img \
    --rtc 2026-07-30T12:00:00 --log-level esp01=debug \
    --delayed-keypress-frames 400 space \
    --delayed-keypress-frames 470 down \
    --delayed-keypress-frames 500 enter \
    --delayed-keypress-frames 560 . \
    --delayed-keypress-frames 575 s \
    --delayed-keypress-frames 590 y \
    --delayed-keypress-frames 605 n \
    --delayed-keypress-frames 620 c \
    --delayed-keypress-frames 635 enter \
    --delayed-screenshot sync.png --delayed-screenshot-frames 5900 \
    --delayed-automatic-exit-frames 6000
```

The three boot keypresses are the same ones the `boot-nextzxos-dotls` regression
row uses to reach the NextZXOS command line.

**6. Read the results back** off the image and checksum them against the
originals — same tool, other direction:

```
build/test/sdfile_tool get /path/to/sd.img HELLO.TXT ./extracted/HELLO.TXT
sha256sum -c originals.sha256
```

## Results

Every run: `.sync`-family dot from `nextsync12.zip`, server on
`192.168.100.238:2048` (the port is hard-coded in the dot), corpus re-sent from
scratch, files deleted from the SD image beforehand so the run demonstrably wrote
them. Load average is `/proc/loadavg` 1-minute, before → after, on a 12-core host.

| dot         | server payload | corpus              | packets | retries / restarts / mistransmits | sha256 | load        |
|-------------|----------------|---------------------|---------|-----------------------------------|--------|-------------|
| `.sync`     | 1024           | 3 files, 36.05 KB   | 45      | 0 / 0 / 0                         | 3/3 OK | 0.86 → 1.32 |
| `.syncfast` | 1024           | 3 files, 36.05 KB   | 45      | 0 / 0 / 0                         | 3/3 OK | 2.35 → 2.41 |
| `.syncslow` | 1024           | 3 files, 36.05 KB   | 45      | 0 / 0 / 0                         | 3/3 OK | 1.85 → 1.94 |
| `.syncfast` | 1455 (`-u`)    | 10 files, 263.90 KB | 214     | 0 / 0 / 0                         | 10/10 OK | 0.78 → 1.08 |
| `.syncfast` | 1455 (`-u`)    | 1 file, 768.00 KB   | 545     | 0 / 0 / 0                         | 1/1 OK | 4.04 → 2.78 |

The 10-file corpus spans 56 B to 131 072 B and includes the adversarial binary
described above, an all-zero file, an all-`0xFF` file, and sizes of 1023 / 1455 /
1456 bytes to land either side of a whole `-u` payload. The 768 KB run is the
single-largest-file case: 545 consecutive packets on one connection, still zero
retries.

The last row ran at a visibly higher load than the others (other work on the
box). It is a pass/fail row, not a timing row, so that does not weaken it — but
the throughput figures below were all taken at load 0.5–0.9, and are the only
numbers here that load could distort.

## Emulated-time throughput — a measured fidelity gap, not a failure

Transfers *complete*, but they are slower in **emulated** time than the author's
real-hardware benchmark table, and the gap widens with baud. Measured on a single
768 KB file with `-u` payloads, by truncating the run at two frame counts 2000
frames (40 s emulated) apart and differencing the server's reported byte offset —
which cancels out boot and connect time and needs no wall-clock inference:

| dot         | emulated | real hardware (author's table) | ratio |
|-------------|----------|--------------------------------|-------|
| `.syncslow` | 6.7 kB/s | 9.19 kB/s                      | 1.4× slower |
| `.sync`     | 11.7 kB/s | 39.94 kB/s                    | 3.4× slower |
| `.syncfast` | 13.4 kB/s | 48.22 kB/s                    | 3.6× slower |

So the emulated 115200 → 1.152 Mbaud step buys 1.75× where real hardware buys
4.3×. Three observations bound where the time goes, and **none of them is a
pacing failure**:

- **RX pacing itself tracks the programmed prescaler.** `byte_transfer_ticks()` is
  recomputed on every service pass (`uart.h`), the pacer releases at most one byte
  per byte-time and banks no credit while idle (`esp_at.cpp`), and throughput does
  move in the right direction when the guest reprograms the baud. Nothing in these
  runs contradicts the pacing measurement from #25 — the workload that was
  supposed to be able to falsify it did not: zero FIFO overruns at 1455-byte
  payloads.
- **~15% of the transfer's wall time is host poll latency** — measured as the sum
  of the `buffered … from the peer` → `+IPD framing` intervals over the 214-packet
  run (0.656 s of 4.461 s). The ESP worker's condition variable wakes early for
  *guest* input but waits out its 1 ms interval for *socket* input
  (`esp_threaded.cpp`). That cost is charged in wall time, so it inflates
  emulated time in proportion to how fast the emulator is running: this headless
  run averaged 208 fps (416% of real time, from `--benchmark`), so a 100%-speed
  windowed run should lose about a quarter as much emulated time to it. **The
  numbers above are therefore a pessimistic bound**, by roughly 10%.
- **The remainder is the guest-side per-byte path** — esxdos SD writes plus
  NextSync's own checksum and polling loop. A per-packet timeline shows ~9 ms
  emulated of UART delivery against ~33 ms emulated of guest-side work per
  1029-byte packet at 1.152 Mbaud.

Root cause of the residual is **not localised** and the comparison baseline is
the author's own single-run, explicitly unscientific table. Tracked as
[issue #177](https://github.com/jorgegv/jnext/issues/177).

One other defect surfaced and is tracked separately: the server sets
`SO_LINGER(1,0)`, so its close sends RST rather than FIN and jnext logs the
resulting `ECONNRESET` at **error** level on a completely normal NextSync
teardown — [issue #176](https://github.com/jorgegv/jnext/issues/176). Cosmetic:
the buffered bytes are still delivered, `CLOSED` is still emitted, and the guest
still prints "All done".

## The functional regression row

`nextsync-func`, declared in `test/00regression/functional_tests.conf`. It runs
the vendored NextSync 1.2 against a live server on every regression run.

An earlier draft of this document argued no row was possible, on three grounds.
**Two of the three were false** — both capabilities already existed in-tree and
the claim was made without searching for them. Corrected in full, because an
inaccurate justification for missing coverage is worse than the missing
coverage:

- *"jnext ships only a FAT32 reader, so this needs mtools."* Wrong.
  `src/core/fat32_image.h` has the writer — `fat32_read_tree` →
  `fat32_tree_upsert` → `fat32_format_and_populate` — linked into `jnext_core`,
  and `sdcard_provisioner.cpp:382-392` already uses that exact sequence to
  inject `MACHINES/NEXT/config.ini`. The row uses it too, via `sdfile_tool`. No
  mtools anywhere.
- *"the row would have to discover a routable RFC1918 address."* It does not
  have to invent that: `test/00regression/esp-loopback-peer.py` already does it
  (connected-UDP-socket trick, nothing sent, works with the cable out) and
  already exits 3 → SKIP when the host has none. `nextsync-peer.py` follows it.
- *"a third-party binary dot command must be vendored."* True, and that was the
  only real blocker. The owner approved it on 2026-07-30: NextSync is
  **Unlicense** (public domain, explicitly covering redistribution "as a
  compiled binary"), so it is licence-clean under the PR protocol, and the
  no-new-dependency exception was granted for a binary nothing in-tree can
  rebuild. Provenance and hashes: `test/00regression/nextsync/README.md`.

### What it does

1. `nextsync-peer.py` picks the host's RFC1918 address (SKIP if there is none),
   generates the corpus, and binds **that address** on port 2048 (SKIP if the
   port is taken — it is compiled into the dot, so the row cannot pick another).
2. `sdfile_tool put` injects `dot/sync` and `sys/config/nextsync.cfg` (holding
   that address) into the run's private SD clone.
3. jnext boots NextZXOS headless with `--esp --esp-allow <peer>` and types
   `.sync`, using the same three boot keypresses as `boot-nextzxos-dotls`.
4. `sdfile_tool get` reads the two synced files back out and the row compares
   **sha256** against what the peer served.

Assertions: the emulator logged `connection OPENED` to the peer; the peer
reported a completed sync with `retries: 0, restarts: 0`; and both files are
byte-identical. The corpus is 4 KB of adversarial binary (every byte value,
embedded NULs, and the literal `OK` / `ERROR` / `SEND OK` / `> ` / `+IPD,`
frames) plus 16 KB spanning 16 payload packets.

The **dot command** is the vendored release binary, unmodified — that is the
software under test. The peer speaks the protocol itself rather than running the
vendored `server/nextsync.py`, for two reasons: that server binds the wildcard
address with no way to narrow it, which would expose a listener on every
interface of the machine; and running it as a subprocess would give the row a
child process to orphan, which is precisely how an earlier draft left strays on
port 2048. The vendored server is still what the manual recipe above runs.

That split is safe because the dot **validates** what the peer sends — it checks
a checksum and a packet number on every packet — so a mis-framed peer cannot be
silently accepted. Mutation M4 below demonstrates exactly that.

### Lifecycle — the peer is reaped on every path

| Path | What reaps it |
|---|---|
| Row passes | the row's `kill`/`wait` block, which runs after the verdict |
| Row fails | the same block — it is outside the pass/fail branch |
| Early SKIP/FAIL before the run | the peer has already exited; `peer_pid` is cleared |
| Shell dies without cleanup | the peer's own orphan detection: `getppid()` changes, it logs `parent went away` and exits within ~1 s |
| Anything else | a 180 s watchdog in the peer |

It is a **single process with no child**, which is what makes that list short.
No EXIT trap is installed — the suite library owns the single handler
([issue #153](https://github.com/jorgegv/jnext/issues/153)).

### Why it does not flake

No screenshot, no pixel compare, no wall-clock threshold — the verdict is a pair
of hashes. The 4000-frame budget is **emulated** time against a transfer
measured at ~1.5 s emulated, and CPU contention pushes it the safe way: a loaded
box runs fewer frames per second, so the same budget buys *more* wall time, never
less. The only wall-clock value is a `timeout` crash guard set well over
measured. The row does not install an EXIT trap — the suite library owns the
single handler ([issue #153](https://github.com/jorgegv/jnext/issues/153)).

### Mutation-tested before review

| # | Mutation | Result |
|---|---|---|
| M1 | `--esp` replaced with `--no-esp` | **FAIL** — no connection, neither file arrives |
| M2 | dot command not injected into the image | **FAIL** — `No such command`, no sync |
| M3 | one payload byte flipped in transit, manifest unchanged | **FAIL** — `NSBIG.BIN is CORRUPT`, and *only* that assertion fires |
| M4 | peer emits a wrong running-sum checksum on data packets | **FAIL** — `retries: 5`, file empty |
| — | restored | **PASS** |

M3 is the one that matters most: it fires the checksum assertion **alone**, with
the connection and no-retry assertions still passing, so the byte-identity claim
is independently discriminative rather than riding on the other two.

M4 earns its place because the peer's framing is hand-written. It shows the real
dot command *validating* what the peer sends — five retries and a zero-length
file, rather than silent acceptance — which is what makes the passing run
evidence that the framing is right, and proves the `retries: 0` assertion bites.
