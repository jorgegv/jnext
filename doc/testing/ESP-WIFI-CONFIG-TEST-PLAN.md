# ESP-01 Wi-Fi configuration — test plan (GH #154)

> **Scope: the GH #154 Wi-Fi configuration group only** — `AT+CWMODE`,
> `AT+CWJAP=`, `AT+CWLAP`, `AT+CWQAP`, `AT+CIPSTATUS`, `AT+CIPMODE=0`/`?`, and
> the query forms of `AT+CIPMUX` / `AT+CIPSERVER` / `AT+UART*`. The design is
> [ESP01-EMULATOR-DESIGN.md §18](../design/ESP01-EMULATOR-DESIGN.md#18-the-wi-fi-configuration-category-gh-154);
> the evaluation that scoped it is
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

## 2. Unit coverage — 60 rows in `esp_at_test` (404 total)

| Group | IDs | What it pins |
|---|---|---|
| `AT+CWMODE` | `CWM-01..14` | All three modes accepted; **mode 2 modelled** (no address, connect refused, `No AP`); `0`/`4`/non-numeric/`=?` refused; the query form; `AT+RST` restoring mode 1 |
| `AT+CWJAP=` | `CWJ-01..10` | URC order then `OK`; the SSID echoed by the query; **any SSID accepted**, but **both arguments required** as on hardware; unquoted/empty SSID, missing and unquoted password all refused; the optional third (BSSID) argument accepted; `AT+RST` forgetting it |
| `AT+CWLAP` | `CWL-01..04` | The one synthetic entry byte-exact; its BSSID/channel/RSSI **agreeing with `AT+CWJAP?`**; exact-entry matching |
| `AT+CWQAP` | `CWQ-01..08` | `WIFI DISCONNECT` then `OK`; the address, join query and connect all following; rejoin and `AT+RST` restoring; **and both halves of the GH #246 boundary** |
| Query forms | `QRY-01..08` | `AT+CIPMUX?`, `AT+UART_CUR?`/`_DEF?`/`AT+UART?` each under its **own** prefix, `AT+CIPSERVER?` with and without a listener |
| `AT+CIPMODE` | `CPM-01..05` | `=0` accepted, `=1` refused, the query, out-of-range, `=?` |
| `AT+CIPSTATUS` | `CSTAT-01..11` | Status 2/3/5; an outbound link's fields; **an inbound link's `tetype` and `<local port>`**; a closed link disappearing; the host-outage status; no shadowing of `AT+CIPSTA?` |

Every reply is asserted **byte-exact** via `check_eq`, not by substring, except
where a row deliberately asserts a relationship between two replies
(`CWL-02/03`) or an ordering (`CSTAT-08..10`).

## 3. Functional coverage — `esp-wifi-setup-func`

The Next's own documented session, executed against the real binary through the
whole path (Z80 → port `0x133B` → `UartChannel` → `EspUartAdapter` → `AtEngine`).
Hermetic: **no socket, no peer, no port** — this session never leaves the
emulator, which is what makes it both the cheapest ESP row and the most direct.

Its headline assertion is a **negative**: the session must contain no `ERROR` at
all. Three things stop that passing vacuously — a pinned 21-line denominator, the
individual replies that carry state, and an **ordering** check that the station
address is present before `AT+CWQAP` and gone after.

## 4. What is deliberately NOT tested

| Not tested | Why |
|---|---|
| `=?` test forms beyond "they answer `ERROR`" | No version documents their replies, so there is nothing to assert against ([§18.5](../design/ESP01-EMULATOR-DESIGN.md#185-three-stated-deviations)) |
| SoftAP behaviour beyond "the station is gone" | jnext is not an access point and does not pretend to be |
| `AT+CWJAP` failure | Every join succeeds by policy ([§18.2](../design/ESP01-EMULATOR-DESIGN.md#182-the-join-policy)); there is no failure to test until a flag creates one |
| Persistence across a process restart | Nothing persists; `AT+RST` is the boundary the rows assert |

## 5. Mutation results

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
