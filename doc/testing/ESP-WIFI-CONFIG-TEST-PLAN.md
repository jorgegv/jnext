# ESP-01 Wi-Fi configuration — test plan (GH #154)

> **Scope: the GH #154 class-A command set** — the Wi-Fi configuration group
> (`AT+CWMODE`, `AT+CWJAP=`, `AT+CWLAP`, `AT+CWQAP`, `AT+CIPSTATUS`,
> `AT+CIPMODE=0`/`?`, and the query forms of `AT+CIPMUX` / `AT+CIPSERVER` /
> `AT+UART*`), plus **`AT+CIPDOMAIN`**. The designs are
> [ESP01-EMULATOR-DESIGN.md §18](../design/ESP01-EMULATOR-DESIGN.md#18-the-wi-fi-configuration-category-gh-154)
> and [§19](../design/ESP01-EMULATOR-DESIGN.md#19-atcipdomain-gh-154); the
> evaluation that scoped both is
> [ESP-AT-SURFACE.md](../design/ESP-AT-SURFACE.md).

**This file is deliberately NOT a `*-TEST-PLAN-DESIGN.md`, and it is not indexed
by the traceability generator.** `esp_at_test` has no entry in
`refresh-traceability-matrix.pl`'s `%PLAN_DOC` map and never has: the ESP module
is self-contained and *"tests ship with the module"*
([design §7.6](../design/ESP01-EMULATOR-DESIGN.md#76-tests-ship-with-the-module)),
so its rows reach the matrix from their own `check()` calls rather than from a
plan document. Adding the suite to that map would demand plan rows for all 401
of its rows, not the 57 this change added — a far larger piece of work than this
issue, and one that would put a hand-written side back next to a generated one.
The matrix already carries every row below, cited `(ESP-AT firmware)`.

---

## 1. The oracle

Three sources, in this order of authority, and **not the implementation**:

| Oracle | Used for |
|---|---|
| `tbblue/docs/extra-hw/wifi/WIFIand UARTReadME1st.txt` — the **Next's own shipped WiFi documentation** | Which commands must work at all, and in what order. It is the reason this group exists ([§18.1](../design/ESP01-EMULATOR-DESIGN.md#181-the-consumer-is-a-document-and-that-is-stronger-than-usual)) |
| ESP-AT v2.3.0.0 `AT_Command_Set/*.rst` + the shipped `libesp8266_at_core.a` | Reply **formats** and byte-exact result strings |
| ESP8266 Non-OS AT Instruction Set v3.0.5 (↔ AT_V1.7.x) + `ESP8266_NONOS_SDK`'s decoded `at_fun[]` | Which spellings exist in the 1.x firmware jnext claims to be |

Where the sources disagree — and for `AT+CWMODE` they do — the **firmware
binary** wins over the manual, and the Next's own documentation decides what
jnext must answer regardless. That disagreement is recorded in
[ESP-AT-SURFACE.md Q1](../design/ESP-AT-SURFACE.md#q1--which-firmware-is-jnext-emulating).

## 2. Unit coverage — 91 rows in `esp_at_test` (437 total)

| Group | IDs | What it pins |
|---|---|---|
| `AT+CWMODE` | `CWM-01..14` | All three modes accepted; **mode 2 modelled** (no address, connect refused, `No AP`); `0`/`4`/non-numeric/`=?` refused; the query form; `AT+RST` restoring mode 1 |
| `AT+CWJAP=` | `CWJ-01..10` | URC order then `OK`; the SSID echoed by the query; **any SSID accepted**, but **both arguments required** as on hardware; unquoted/empty SSID, missing and unquoted password all refused; the optional third (BSSID) argument accepted; `AT+RST` forgetting it |
| `AT+CWLAP` | `CWL-01..04` | The one synthetic entry byte-exact; its BSSID/channel/RSSI **agreeing with `AT+CWJAP?`**; exact-entry matching |
| `AT+CWQAP` | `CWQ-01..08` | `WIFI DISCONNECT` then `OK`; the address, join query and connect all following; rejoin and `AT+RST` restoring; **and both halves of the GH #246 boundary** |
| Query forms | `QRY-01..08` | `AT+CIPMUX?`, `AT+UART_CUR?`/`_DEF?`/`AT+UART?` each under its **own** prefix, `AT+CIPSERVER?` with and without a listener |
| `AT+CIPMODE` | `CPM-01..05` | `=0` accepted, `=1` refused, the query, out-of-range, `=?` |
| `AT+CIPSTATUS` | `CSTAT-01..11` | Status 2/3/5; an outbound link's fields; **an inbound link's `tetype` and `<local port>`**; a closed link disappearing; the host-outage status; no shadowing of `AT+CIPSTA?` |
| `AT+CIPDOMAIN` | `DOM-01..25` | The deferred reply and the 1.x unquoted byte form; the 63/64-byte boundary and a 600-byte name refused whole; a **forged `+CIPDOMAIN` reply inside the hostname** that cannot inject; a NUL- and 8-bit-bearing name; deferral, in-order replay, and a deferred line that is itself a lookup; the deadline; the **station gate** (`DOM-23/24`) and the predicate that must back it (`DOM-25`); and — as an **equality** — that a policy refusal is byte-identical to a DNS miss |

### 2.1 The two suites below the engine

`AT+CIPDOMAIN` is the one command in this set that reaches outside the engine,
so it carries coverage in two more suites:

| Suite | Rows | What only it can prove |
|---|---|---|
| `esp_socket_test` | `RSLV-01..12` (201 total) | The real `SocketResolver`: the address policy on a path nobody dials, the literal fast path and that it **never consults the injected resolver**, one-at-a-time, a resolver that throws, one that reports success with no addresses, and that **destroying a resolver mid-lookup returns immediately** |
| `esp_wiring_test` | `RGATE-01..09` (111 total) | `EspGatedResolver`: the `--esp-allow` list over lookups, that a blocked name **never reaches the wrapped resolver**, and that a refusal is **accepted-then-failed rather than rejected** — the property that stops the command being an allowlist oracle |

Every reply is asserted **byte-exact** via `check_eq`, not by substring, except
where a row deliberately asserts a relationship between two replies
(`CWL-02/03`) or an ordering (`CSTAT-08..10`).

## 3. Functional coverage

### 3.1 `esp-wifi-setup-func`

The Next's own documented session, executed against the real binary through the
whole path (Z80 → port `0x133B` → `UartChannel` → `EspUartAdapter` → `AtEngine`).
Hermetic: **no socket, no peer, no port** — this session never leaves the
emulator, which is what makes it both the cheapest ESP row and the most direct.

Its headline assertion is a **negative**: the session must contain no `ERROR` at
all. Three things stop that passing vacuously — a pinned 21-line denominator, the
individual replies that carry state, and an **ordering** check that the station
address is present before `AT+CWQAP` and gone after.

### 3.2 `esp-cipdomain-func`

Three IP **literals** through the real binary — no DNS server, no peer, nothing
to race, because a literal takes the synchronous fast path while the address
policy still applies to it exactly as it would to a resolved name. One RFC1918
address (answered), loopback and cloud-metadata (both refused).

Its strongest assertion is also a negative: **neither denied address may appear
anywhere in what the guest was told.** A resolver without the policy would have
answered `+CIPDOMAIN:127.0.0.1` quite happily. It additionally asserts the
*asymmetry* — the operator's log names both refusals while the guest's wire
cannot tell them apart.

**This row earned itself immediately.** The first version of `AT+CIPDOMAIN`
passed all 744 unit rows while the product did nothing: the service hook had
been added to `AtEngine::poll()`, and `ThreadedEsp`'s worker does not call
`poll()` — it calls `advance_transports()` and `service_transports()` directly
so the transport pass can run unlocked. Every unit suite drives the passive
core, so none of them could see it. Both guest scripts come from the same
generator and the **same** 76-byte Z80 walker; only the table differs.

## 4. What is deliberately NOT tested

| Not tested | Why |
|---|---|
| `=?` test forms beyond "they answer `ERROR`" | No version documents their replies, so there is nothing to assert against ([§18.5](../design/ESP01-EMULATOR-DESIGN.md#185-three-stated-deviations)) |
| SoftAP behaviour beyond "the station is gone" | jnext is not an access point and does not pretend to be |
| `AT+CWJAP` failure | Every join succeeds by policy ([§18.2](../design/ESP01-EMULATOR-DESIGN.md#182-the-join-policy)); there is no failure to test until a flag creates one |
| Persistence across a process restart | Nothing persists; `AT+RST` is the boundary the rows assert |

## 5. Mutation results — the Wi-Fi configuration group

36 mutations derived **from the diff**, not from the row list. The harness was
validated against a known answer first (a pinned reply prefix must be caught; a
comment-only edit must survive) — a harness that has not been shown to report a
*known* failure cannot be trusted to report an unknown one.

**Four survived the first pass, and every one was in a path with no row:**

| Survivor | Fix |
|---|---|
| `AT+CIPSTATUS` `tetype` hardcoded to 0 | `CSTAT-08/09` — there was no **inbound** row at all |
| `AT+CIPSTATUS` `<local port>` hardcoded to 0 | `CSTAT-10`, same cause |
| The remote-address `"0.0.0.0"` fallback | **Deleted.** No path reaches it, and a fallback that turns a known-unknown into an empty string for an inbound link is worse than the address the transport has |
| `AT+CIPSTATUS` gating on the guest's state instead of the full address test | `CSTAT-11` — the host-outage status had no row |

All four are caught now. Two further defects were found the same way and are
worth recording because neither was in the product:

- **The functional row aborted instead of failing.** Under `set -euo pipefail` a
  `grep` that finds nothing returns 1 and takes the command substitution — and
  the whole row — down with it, so the row printed its name and died **without a
  verdict**. Removing one command from the dispatch table reproduced it exactly.
  Fixed with `|| true` on the three ordering lookups.
- **Its failure message was unreadable**, because it dumped the entire wire and a
  blocked guest makes that arbitrarily long. Bounded to 30 lines. A failure
  nobody can diagnose is barely better than no failure.
- **Its headline assertion could silently pass when it should fail**, and the
  suite's own `harness-selftest` (HS-30) caught it. The check was written
  `printf '%s\n' "${wire[@]}" | grep -qx 'ERROR'`, which is unsound under
  `set -o pipefail`: `grep -q` exits on the match, `printf` dies of SIGPIPE, and
  pipefail promotes 141 to the pipeline's status — so a **found** ERROR reports
  as **not found**. On a negative assertion that is the worst possible failure
  mode. Replaced with a herestring. HS-30 refuses the idiom across every suite
  source precisely so a new row cannot reintroduce it, and it worked.

## 6. Mutation results — `AT+CIPDOMAIN`

24 further mutations, again derived from the diff, across all three suites.

**The harness itself was the first thing caught.** A mutation removing the
null-resolver guard made `esp_at_test` **crash**, and the harness reported that
as `SURVIVED` — a crashed suite prints no `Total:` line, and "no failures
counted" looked identical to "nothing failed". It now pins each suite's expected
row count, so an absent or short denominator is a CAUGHT rather than a pass, and
the known-answer validation was extended to cover a crash as well as a real
mutation and a no-op comment edit. A harness that has not been shown to report a
*known* failure cannot be trusted to report an unknown one.

**Two survivors:**

| Survivor | Disposition |
|---|---|
| `addrs.empty()` removed from `SocketResolver::poll()` | **Equivalent mutant.** `select_candidate` already refuses an empty candidate list, so `finish()` fails anyway. Kept as defence in depth — the reason the transport keeps its `out.clear()` handlers — and **documented in place** as not being the barrier. `RSLV-12` pins the outcome, which is the right thing to pin. |
| `blocked_ = false` removed from `EspGatedResolver::reset()` | **A real gap.** `begin()` clears the flag itself, so the next lookup worked either way — but `state()` stayed stuck on `Failed` in between, breaking the `reset()` contract `EspResolver` documents. Closed by strengthening `RGATE-08` to assert the contract (`state() == Idle`) rather than only its consequence. |

The functional row was mutation-tested against the **product**: removing the
address policy from `SocketResolver::finish()` makes it fail loudly, naming the
addresses that leaked.

## 7. The gate that four suites missed (review round 2)

`AT+CIPDOMAIN` shipped its first version **without the station gate**: a lookup
succeeded after `AT+CWMODE=2` or `AT+CWQAP`, where a connect had refused since
the first increment. Worth recording, because of what did *not* catch it:

- all 432 `DOM-*` rows,
- every `RSLV-*` row against the real resolver,
- every `RGATE-*` row against the allowlist gate,
- and `esp-cipdomain-func` against the real binary.

**None of them turns the station off before looking a name up.** The coverage
was wide and uniformly on one side of the missing condition — which is the
shape a row count cannot show, and the reason a reviewer reading the *other*
command's gates found it when four suites did not.

It was also contradicted inside the same change: `FEATURES.md` said two lines
above the new bullet that `AT+CWMODE=2` "really does remove the station… and a
connect fails until station mode comes back", while the new bullet claimed no
such thing for the lookup. **Two statements of one rule, and only one of them
was true** — the same defect class as the `AT+SAVETRANSLINK` disagreement the
sweep script now checks for, in a place no script was looking.

The framing helped hide it. The design said *"one rule, two commands"* and
listed the two policy layers, which invites a reader to check that the
**policies** match and stop. `AT+CIPSTART` has more gates than its policies.
[§19.3](../design/ESP01-EMULATOR-DESIGN.md#193-which-gates-apply-command-by-command)
is now a per-command table of **every** gate, with the ones that deliberately
have no analogue named as such, so the next command added to this surface is
checked against a list rather than against a slogan.

### Rows and mutations

| Row | Pins |
|---|---|
| `DOM-23` / `DOM-23b` | `AT+CWMODE=2` then a lookup → `ERROR`, and the resolver is **never asked** |
| `DOM-24` / `DOM-24b` | the same after `AT+CWQAP` |
| `DOM-25` | a **host** outage does NOT refuse a lookup — the GH #246 boundary, from this side |

Four further mutations, all caught:

| Mutation | Result |
|---|---|
| the station gate removed entirely | 4 rows fail |
| `station_enabled_by_guest()` → `station_has_ip()` (the wrong predicate) | **`DOM-25` alone** fails — which is precisely what that row exists to discriminate |

The known-answer validation was re-run first, including the **crash** case the
harness now pins, so the four results above come from a harness shown to report
a known failure before it was trusted with an unknown one.
