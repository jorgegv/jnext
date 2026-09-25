# ESP-01 AT surface — what to widen, and what not to (GH #154)

> **This document is the ANALYSIS AND JUDGEMENT that scoped GH #154** — the
> A/B/C evaluation of all 99 ESP8266-reachable AT commands, and the seven worth
> building. It ends in [§7 Questions for the owner](#7-questions-for-the-owner).
>
> **STATUS: ALL SEVEN class-A commands are now BUILT.**
> ([ESP01-EMULATOR-DESIGN.md §18](ESP01-EMULATOR-DESIGN.md#18-the-wi-fi-configuration-category-gh-154),
> tests in [ESP-WIFI-CONFIG-TEST-PLAN.md](../testing/ESP-WIFI-CONFIG-TEST-PLAN.md)):
> `AT+CWMODE`, `AT+CWJAP=`, `AT+CWLAP`, `AT+CWQAP`, `AT+CIPSTATUS`, and
> `AT+CIPMODE=0`/`?`, plus the query forms of [§5.1 A2](#a2-the-symmetry-gaps--query-and-test-forms).
> The seventh, **`AT+CIPDOMAIN`**, followed in
> [§19](ESP01-EMULATOR-DESIGN.md#19-atcipdomain-gh-154) once the owner
> authorised the seam change it needed. This document priced it as a table row
> and it was not one: the async resolver exists but is **bound to a
> connection**, so a standalone lookup took a new interface (`EspResolver`,
> beside `EspListener`). The estimate's *shape* held, though — it was a new
> interface, not a new ownership model and not new threading, because the
> resolver thread already captured no owner of any kind.
>
> The **test (`=?`) forms** were likewise dropped from A during phase 1 itself,
> when the reference turned out to document none — recorded in place at
> [§5.5](#55-what-could-not-be-verified-and-one-thing-this-changed).
>
> **THE OWNER HAS NOW ANSWERED ALL SEVEN QUESTIONS.** Nothing below is a
> provisional default any more. Q1-Q5 were ratified exactly as recommended and
> as built. **Q6 and Q7 were answered BUILD — the opposite of the
> recommendation in each case** — and both are built:
> [§20](ESP01-EMULATOR-DESIGN.md#20-atping-gh-154) and
> [§21](ESP01-EMULATOR-DESIGN.md#21-sntp-gh-154).
>
> The recommendations that were overruled are **kept, not deleted**, with what
> changed recorded under each. In Q6's case the recommendation rested on a
> factual claim that turned out to be wrong, and saying so is more useful than
> quietly rewriting it.

---

## Table of contents

- [1. The goal, restated — and the two readings it excludes](#1-the-goal-restated--and-the-two-readings-it-excludes)
- [2. What the shipped module actually answers — measured, not read](#2-what-the-shipped-module-actually-answers--measured-not-read)
- [3. The reference names a firmware jnext does not claim to be](#3-the-reference-names-a-firmware-jnext-does-not-claim-to-be)
- [4. The Next's own shipped documentation is the sharpest evidence there is](#4-the-nexts-own-shipped-documentation-is-the-sharpest-evidence-there-is)
- [5. The evaluation — A, B, C](#5-the-evaluation--a-b-c)
- [6. Proposed implementation order](#6-proposed-implementation-order)
- [7. Questions for the owner](#7-questions-for-the-owner)
- [8. Evidence index](#8-evidence-index)

---

## 1. The goal, restated — and the two readings it excludes

The owner's framing, which governs everything below:

> *"the goal is to emulate the practical AT interface **as any program would see
> it when running on the Next**. The goal is NOT a full ESP01 emulator."*

And the issue's own:

> *"it makes no sense to emulate the complete command set, but only certain
> categories that are deemed useful. Other more arcane commands will be emulated
> on a request basis, with specific use cases."*

Two readings are therefore **out of scope by instruction**, and naming them is
what keeps the list below honest:

- **Not "implement the datasheet".** A list that ends in "and the rest" is a
  failed analysis. The issue title says *"to datasheet spec"*; the issue body
  and the owner's brief both narrow it, and the narrower reading wins.
- **Not "emulate an ESP8266".** The ESP-01 on a Next is reachable only through
  a 4-wire UART header. **Anything a Z80 cannot observe through that UART is
  not emulable, it is merely storable** — and storing a value so that a query
  can read it back is a lie with a round trip, not an emulation. That single
  test does most of the C-class sorting in [§5](#5-the-evaluation--a-b-c).

The useful question is therefore not *"is this command in the manual?"* but:

> **If a Next program sends this, what does it do differently depending on the
> answer — and can jnext produce that difference honestly?**

Three answers, and they are the three classes:

| | Meaning |
|---|---|
| **A** | The program behaves differently, and jnext can produce the difference honestly. **Implement now.** |
| **B** | The program could behave differently, but nothing on the Next asks, or the honest answer needs a decision jnext has not been given. **On request, with a use case.** |
| **C** | The program cannot tell, or the difference is a property of silicon or a radio jnext does not have. **Do not build.** |

---

## 2. What the shipped module actually answers — measured, not read

This section was produced by **driving the shipped `AtEngine`**, not by reading
[§5.1 of the design doc](ESP01-EMULATOR-DESIGN.md#51-the-command-set-as-shipped).
That matters: the design doc's own §5.1 preamble records that the table once went
on claiming `AT+CIPMUX=1` answered `ERROR` for two releases after GH #210 made it
`OK`, because *"a reader scanning for a command sees a row, never a scope
heading"*. A document is not evidence about a product.

**Method.** A throwaway program (scratchpad, not committed) linked
`src/esp01/src/{esp_at,esp_log,esp_socket,esp_address_policy,esp_socket_posix}.cpp`
against a null transport and a null listener, fed each command line in byte by
byte followed by `CR LF`, drained the engine, and printed the guest-bound bytes
escaped. Each line ran against a **fresh engine**, so every answer below is the
power-on answer with no prior state.

### 2.1 The dispatch table, verbatim

`AtEngine::kCommands` (`src/esp01/src/esp_at.cpp:119-144`) is **21 rows**
covering **19 distinct commands** — 21 minus the two commands that occupy two
rows each (`AT+CIPCLOSE` bare + `=<id>`, `AT+CIPSTO` `?` + `=`), counting `ATE0`
and `ATE1` as the separate literals they are. It is matched by case-insensitive
prefix, first match wins, and an entry marked non-prefix is skipped when trailing
text remains:

```
AT   ATE0   ATE1   AT+RST   AT+GMR   AT+CIFSR
AT+CWJAP?   AT+CIPSTA?   AT+CIPDNS_CUR?
AT+CIPSTART=   AT+CIPSEND=   AT+CIPSENDEX=
AT+CIPCLOSE   AT+CIPCLOSE=   AT+CIPMUX=
AT+CIPSERVER=   AT+CIPSTO?   AT+CIPSTO=
AT+UART_CUR=   AT+UART_DEF=   AT+UART=
```

Everything else — **every** command not in that list, in any form — falls through
to `queue_error()` and answers `\r\nERROR\r\n`
(`esp_at.cpp:251-252`).

### 2.2 The measured baseline

Answers abbreviated; `ERROR` means the literal `\r\nERROR\r\n` and nothing else.

| Command sent | Answer today | |
|---|---|---|
| `AT` / `ATE0` / `ATE1` | `OK` | ✅ |
| `AT+RST` | `OK` + `WIFI CONNECTED` + `WIFI GOT IP` | ✅ |
| `AT+GMR` | canned 4-line version block + `OK` | ✅ |
| `AT+CIFSR` | `+CIFSR:STAIP,…` / `STAMAC,…` + `OK` | ✅ |
| `AT+CWJAP?` | `+CWJAP:"JNextWifiHost",…` + `OK` | ✅ |
| `AT+CIPSTA?` | `+CIPSTA:ip/gateway/netmask` + `OK` | ✅ |
| `AT+CIPDNS_CUR?` | two `+CIPDNS_CUR:` lines + `OK` | ✅ |
| `AT+CIPMUX=0` / `=1` | `OK` | ✅ |
| `AT+CIPSTO?` | `+CIPSTO:180` + `OK` | ✅ |
| `AT+CIPSTO=180` | **`ERROR`** | ✅ *deliberate* — measured on hardware, GH #249: refused with no server running |
| `AT+CIPSERVER=0` | **`ERROR`** | ✅ *deliberate* — documented divergence, [§13.7](ESP01-EMULATOR-DESIGN.md#137-what-implementation-decided-that-13-did-not) |
| **`AT+CWMODE?`** | **`ERROR`** | ❌ |
| **`AT+CWMODE=1`** / `=3` | **`ERROR`** | ❌ |
| **`AT+CWJAP="ssid","pass"`** | **`ERROR`** | ❌ — only the **query** form exists |
| **`AT+CWLAP`** | **`ERROR`** | ❌ |
| **`AT+CWQAP`** | **`ERROR`** | ❌ |
| **`AT+UART_CUR?`** | **`ERROR`** | ❌ — only the **set** form exists |
| **`AT+CIPMUX?`** | **`ERROR`** | ❌ — only the **set** form exists |
| **`AT+CIPSTATUS`** | **`ERROR`** | ❌ |
| **`AT+CIPDOMAIN="…"`** | **`ERROR`** | ❌ |
| **`AT+CIPMODE=0`** | **`ERROR`** | ❌ — even the *no-op* setting is refused |
| `AT+CIPMODE=1` | `ERROR` | — deliberate (no passthrough) |
| `AT+PING`, `AT+CIPSNTPCFG`, `AT+CIPSNTPTIME?` | `ERROR` | — |
| `AT+CIPRECVMODE`, `AT+CIPBUFSTATUS`, `AT+SAVETRANSLINK` | `ERROR` | — |
| `ATI`, `AT+RESTORE`, `AT+GSLP`, `AT+SLEEP`, `AT+RFPOWER`, `AT+SYSRAM?` | `ERROR` | — |
| `AT+CWDHCP_CUR`, `AT+CWAUTOCONN`, `AT+CWHOSTNAME?`, `AT+CIPSTAMAC?` | `ERROR` | — |
| **`AT+CIPSTART=?`, `AT+CWMODE=?`, `AT+GMR=?`** (test forms) | **`ERROR`** | ❌ — **no command implements the `=?` test form** |

### 2.3 Four facts this measurement establishes that the prose does not

1. **The set/query asymmetry is invisible in every existing document.** §5.1
   lists `AT+UART_CUR=<baud>,…` and `AT+CIPMUX=0` as implemented. Both are — in
   the **set** direction only. `AT+UART_CUR?` and `AT+CIPMUX?` answer `ERROR`,
   and nothing anywhere says so. The Next's own UART source comments cite
   `AT+UART_CUR?` by name ([§4](#4-the-nexts-own-shipped-documentation-is-the-sharpest-evidence-there-is)).
2. **`AT+CWJAP` is half a command.** The query form answers; the **set** form —
   the one the Next's own documentation instructs a user to type — answers
   `ERROR`. §5.1 lists the row as `AT+CWJAP?`, which is accurate, but every
   summary downstream of it says "CWJAP" unqualified.
3. **No test (`=?`) form exists at all**, for any command, and that is not
   recorded anywhere either. It looked like the cheapest item in this whole
   document until the reference was actually read — v2.3.0.0 does not document
   the test form of **any** command, so there is nothing to copy
   ([§5.5](#55-what-could-not-be-verified-and-one-thing-this-changed)). It is a
   real gap with no verified answer, which is a different thing from a cheap
   one.
4. **`AT+CIPMODE=0` is refused.** The user guide and the design doc both say
   "no passthrough", which reads as "`=1` is refused". In fact the command does
   not exist, so the setting that means *"stay in the mode you are already in"*
   is an error too. A client that defensively asserts `AT+CIPMODE=0` during init
   — which is the normal, careful thing to do, and what `AT+CIPMUX=0` is already
   doing in NXtel — is refused for asking for the status quo.

**What the user guide gets wrong by omission.**
`src/doc/user-guide/05-running-programs/06-networking.md:353-370` says the
unemulated set is *"a couple of things"* — transparent mode and TLS — and then
*"None of the rest has a consumer in current Next software."* The first half is
an undercount (the Wi-Fi configuration category is absent entirely, not a couple
of things), and the second half is contradicted by §4 below: the Next's own
shipped WiFi documentation is a consumer, and it is the one a *user* meets first.
Correcting that page is part of phase 2 regardless of which options are chosen.

---

## 3. The reference names a firmware jnext does not claim to be

The issue names **esp-at `release/v2.3.0.0_esp8266`** as the authoritative
source. jnext does not emulate that firmware, and says so on the wire:

| | Version |
|---|---|
| `AT+GMR` answers (`esp_at.cpp:849-854`) | `AT version:1.7.4.0` / `SDK version:3.0.4` |
| The hardware the `AT+CIPSTO` model was **measured** on ([§15.1](ESP01-EMULATOR-DESIGN.md#151-the-measurement)) | a real Ai-Thinker ESP-01, **AT 1.2.0.0 / SDK 1.5.4.1** |
| The document that model was written against | **ESP8266 AT Instruction Set v1.5.4** §5.17 |

So every behavioural oracle this module has ever used is the **1.x NONOS AT
firmware** — which is what the ESP-01 modules people actually plug into a Next
ship with — while the issue points at the **2.x esp-at** command set.

**They are not the same surface, and jnext has already chosen.** Three findings,
all from the fetched reference:

1. **jnext ships a command that exists only in 1.x.** `AT+CIPDNS_CUR?` is
   implemented today. In v2.3.0.0 there is no `_CUR` variant of anything except
   the two UART commands: the comparison page lists `AT+CIPDNS_CUR` / `_DEF` —
   along with twelve other `_CUR` / `_DEF` pairs — as ESP-AT `❌` with the note
   *"This command will not be added to the ESP-AT version."* They were never
   deprecated in 2.x; **they were never in it**, and Espressif says they never
   will be. So the `_CUR` / `_DEF` variants the issue's scope sketch asks for are
   only coherent under 1.x.
2. **The `> ` prompt already differs, and 1.x is the one that works.** jnext
   emits `"\r\nOK\r\n> "` — one CRLF, trailing space. v2.3.0.0's result-code
   table holds `"\r\nOK\r\n\r\n>"` — **two** CRLFs and **no** trailing space
   (byte-exact, from the shipped `libesp8266_at_core.a`). The NextZXOS `.UART`
   dot command waits for the exact sequence `OK`,13,10,`>`
   ([§5.2](ESP01-EMULATOR-DESIGN.md#52-the-framing-constraints-that-actually-bite)),
   which **the 2.x form does not contain** — the extra CRLF breaks it. Moving to
   2.x byte fidelity would break a NextZXOS dot command.
3. **`FAIL` does not exist in 2.x.** Its `ESP_AT_RESULT_CODE_FAIL` enumerator
   maps to the string `"ERROR"`, and `AT+CWJAP` failure answers
   `+CWJAP:<error code>` then `ERROR`, not `FAIL`. jnext's *"`AT+CIPSTART`
   failure answers `ERROR` only; `FAIL` is never emitted"*
   ([simplification 4](ESP01-EMULATOR-DESIGN.md#5-the-command-set)) is therefore
   **already 2.x-correct and 1.x-divergent** — the one place the module leans the
   other way, which is worth knowing before anyone "fixes" it in either
   direction.

**This is a real fork in the road and it is [Q1](#q1--which-firmware-is-jnext-emulating).**
It has to be answered before the A list is built, because it decides:

- whether `AT+CWMODE_CUR` / `_DEF` are commands to add or commands to refuse;
- what `AT+GMR` should say;
- whether `AT+SYSSTORE` (2.x's replacement for `_CUR`/`_DEF`) is in scope at all;
- whether the MQTT / HTTP / web-server command categories — which exist in 2.x
  and do **not** exist in 1.x — are even candidates.

Answering it does not mean abandoning the issue's reference. The esp-at source is
still the best *available* statement of exact response strings for the commands
the two versions share, and it is Apache-2.0 and therefore usable. It is the
*scope* the version question settles, not the reading material.

---

## 4. The Next's own shipped documentation is the sharpest evidence there is

The design doc's §1.1 derived the v1.0 surface from **software that runs on a
Next**, and explicitly *not* from the Espressif manual. That method is right and
this analysis keeps it — but applying it again turns up a source §1.1 did not
use, and it is the best one yet:

> `tbblue/docs/extra-hw/wifi/WIFIand UARTReadME1st.txt`
> — the **official ZX Spectrum Next WiFi documentation, shipped in the
> distribution**.

It is not a program; it is a **script for a human at a terminal**, which is
strictly better evidence for this issue than any single client. A client sends
what its author needed. This file tells *every* Next owner what to type, and it
was written by the Next project itself.

What it instructs the user to type, in order (`:232-280`, `:553-566`, `:737`):

| Step | Line | jnext today |
|---|---|---|
| Check station mode | `AT+CWMODE?` | **`ERROR`** |
| Set station mode | `AT+CWMODE=1` | **`ERROR`** |
| List access points | `AT+CWLAP` | **`ERROR`** |
| Join an access point | `AT+CWJAP="wifinetwork","password"` | **`ERROR`** |
| Reset | `AT+RST` | `OK` ✅ |
| Get IP | `AT+CIFSR` | `OK` ✅ |
| Versions | `AT+GMR` | `OK` ✅ |
| Connect | `AT+CIPSTART="TCP","www.google.com",80` | `OK` ✅ |
| Send | `AT+CIPSEND=7` | `> ` ✅ |
| Close | `AT+CIPCLOSE` | `OK` ✅ |
| Leave the AP | `AT+CWQAP` (`:372`) | **`ERROR`** |
| Firmware update | `AT+CIUPDATE` (`:566`) | **`ERROR`** |
| Query the baud | `AT+UART_CUR?` (`:737`) | **`ERROR`** |

**A user following the Next's own WiFi instructions inside jnext hits `ERROR` on
the very first line**, and on five of the first six. Everything that actually
moves bytes works; everything that *configures the radio* — which is the entire
first half of the documented workflow — does not exist.

That is the finding this analysis turns on, and it reframes the issue. GH #154
reads as a completeness exercise ("go the rest of the way to the datasheet"). It
is not. The gap that bites is **the Wi-Fi configuration category**, and the
reason it bites is not that new software might use it one day — it is that the
Next's shipped documentation already tells users to use it today, and jnext
already fails it.

It also satisfies the project's own filing test (CLAUDE.md, *"favour
user-facing work"*): a user, and a shipped artifact, behave differently.

### 4.1 A corpus check, for scale

A survey of every local ZX Spectrum source tree (`grep -rhoE 'AT\+[A-Z0-9_]+'`
across `/home/jorgegv/src/spectrum`, excluding jnext itself) ranks the commands
real Next-adjacent code mentions. Every command mentioned **more than once** is
implemented except these:

| Command | Mentions | Where |
|---|---|---|
| `AT+CIPMODE` | 31 | `dezogif_ng` — **all of them explaining why it does NOT use it** (`transport_esp.asm:24-30`, `:4107-4109`: `CIPSERVER` needs `CIPMUX=1`, which forbids `CIPMODE=1`) |
| `AT+CWMODE` | 8 | the Next WiFi readme; `dezogif_ng/doc/WIFI-SETUP.md` |
| `AT+CWLAP` / `AT+CWQAP` / `AT+CIUPDATE` | 2 each | the Next WiFi readme; `dezogif_ng/doc/WIFI-SETUP.md` |
| `AT+CWDHCP` | 1 | `dezogif_ng/doc/WIFI-SETUP.md` |
| `AT+CIPSTATUS` | 1 | — |
| `AT+CIPDOMAIN` | 2 | — |

Two things follow, and they point in opposite directions, which is why both are
worth stating:

- **`AT+CIPMODE` has the highest mention count of any unimplemented command and
  the weakest case of any of them.** Every mention is a comment explaining that
  it is unusable in the mode the mentioning program runs in. Counting mentions
  without reading them would have put it top of the A list; reading them puts it
  in B. (`nextsync`'s `AT+CIPMODE=1` path is likewise real but
  **not compiled** — `nextsync_raw_io.c` in the ZX-Next-Unite fork, which that
  fork's `build.ps1` does not build; `doc/testing/NEXTSYNC-VERIFICATION.md:36-38`.)
- **The Wi-Fi configuration commands are mentioned by the Next's own docs and by
  a live consumer's setup guide**, which is exactly the evidence class §1.1 was
  built on.

---

## 5. The evaluation — A, B, C

### 5.1 Class A — implement now

Four groups, in the order they should be built. **Group A1 is the issue**; the
rest is proportionate work around it.

---

#### A1. The Wi-Fi configuration category — the §4 gap

This is the whole finding. Four commands, and they are what the Next's own
documentation opens with.

| Command | Forms | What "correct" means here |
|---|---|---|
| `AT+CWMODE` | set + query | Station-only (1), AP (2), both (3). jnext **is** a station, so 1 and 3 are its natural state. Mode 2 is emulable and observable rather than refusable: it means *no station*, which the module already has a representation for — the GH #246 `associated_` flag. `AT+CWMODE=2` therefore drives `associated_` false, and `AT+CIFSR` reports `STAIP,"0.0.0.0"` and `AT+CIPSTART` fails, exactly as on hardware. **Reusing shipped, tested state is the point** — it is the difference between modelling the mode and storing a number. |
| `AT+CWJAP` | **set** (query exists) | Accept any SSID/password and answer `WIFI CONNECTED` / `WIFI GOT IP` / `OK`, recording the SSID so `AT+CWJAP?` reports back what the guest asked for. This is cosmetic in exactly the way [§5.6](ESP01-EMULATOR-DESIGN.md#56-synthetic-identity) already establishes — nothing routes through it — and echoing the **guest's own** string is not a host-information leak. Whether a *wrong* SSID should fail is [Q4](#q4--should-a-wi-fi-join-ever-fail). |
| `AT+CWLAP` | execute | One `+CWLAP:` entry: the module's own synthetic AP. **Never a host scan** — §8.3 forbids it, and a real scan would put the user's neighbours' SSIDs inside the guest. One honest synthetic entry beats three invented ones. |
| `AT+CWQAP` | execute | Leave the AP: drives `associated_` false, the same state `--esp-delayed-disassociate-frames` already drives. Answers `OK`. Whether it also emits `WIFI DISCONNECT` is a sub-case of [Q5](#q5--the-never-emit-list). |

**Why this group is A and not B**, stated against the project's own bar: it is
not "a program might one day use it". A user following the shipped Next WiFi
instructions fails on line one today ([§4](#4-the-nexts-own-shipped-documentation-is-the-sharpest-evidence-there-is)).
That is a user-visible defect in a shipped artifact.

**How it is tested.** `esp_at_test` rows (new prefix `CWM`, `CWJ`, `CWL`,
`CWQ`), byte-exact against the reply strings, in the style of the existing 344
rows; plus the `associated_` interaction proved against the existing `ASSOC`
rows so the two entry points into that state cannot diverge. One functional
regression row driving the §4 workflow end to end as a script — the transcript
in the Next's readme is the test case, which is the strongest form this suite
has: the oracle is a document the Next project shipped.

---

#### A2. The symmetry gaps — query and test forms

The cheapest work in this document and the least visible. Every one of these is
a **readback of state the engine already holds**; none needs a new concept.

| Missing | Why it matters |
|---|---|
| `AT+CIPMUX?` | The set form is implemented and the query is not. A client that checks before setting is refused. |
| `AT+UART_CUR?` / `AT+UART_DEF?` / `AT+UART?` | Same asymmetry — and the Next's **own UART source comments cite `AT+UART_CUR?` by name** (`WIFIand UARTReadME1st.txt:737`). |
| `AT+CWMODE?`, `AT+CWJAP?` (exists), `AT+CIPSTO?` (exists) | Completes the set. |
| `AT+CIPSERVER?` | Set form implemented; query not. Documented reply `+CIPSERVER:<mode>[,<port>,…]`. |
| ~~The `=?` test form~~ | **DEFERRED — see the correction below.** |

**The test (`=?`) form is deliberately NOT in this group, and an earlier draft of
this document was wrong about it.** That draft asserted that real firmware
answers a parameter template (`AT+CWMODE=?` → `+CWMODE:(1-3)`). Checking the
named reference does not support it: **there is not one `Test Command` heading in
any of the twelve v2.3.0.0 category pages, and the literal `=?` does not appear
in the TCP/IP page at all** ([§5.5](#55-what-could-not-be-verified-and-one-thing-this-changed)).
The template strings are therefore **unverified**, and emitting a guessed one is
exactly the plausible-but-wrong this project holds is worse than nothing. Test
forms move to B until their replies come from the 1.x instruction set or from
hardware. `AT+CIPSTART=?` keeps answering `ERROR` — today by accident (the line
matches the `AT+CIPSTART=` prefix and the handler rejects `?` as a malformed
argument list), and now by decision as well.

**Effort/benefit is the argument for what remains.** The query forms are
documented, byte-exact and few: a handful of constant strings and one dispatch
rule.

---

#### A3. `AT+CIPSTATUS` — and a correction to the record

[§14.6](ESP01-EMULATOR-DESIGN.md#146-what-this-does-not-add) declined it on two
grounds. **One has expired and the other was never right:**

- *"no consumer has made it"* — still largely true, and on its own would keep
  this in B.
- *"Adding it would mean **inventing a status format** nothing parses"* — **this
  is incorrect.** The format is specified by the AT instruction set
  (`STATUS:<stat>` followed by one `+CIPSTATUS:<id>,<type>,<remote>,<rport>,<lport>,<tetype>`
  line per live link). It is not invented; it is the one thing in this whole
  document that is unambiguously *documented rather than inferred*. Whatever
  else keeps a command out, "we would have to make it up" does not apply here.

It promotes to A because it is the **natural companion to server mode** ([§13](ESP01-EMULATOR-DESIGN.md#13-server-mode-gh-210)):
with up to four inbound slots the guest can now hold connections it did not
open, and `AT+CIPCLOSE=<id>` (GH #211) exists precisely because a wedged peer
must be nameable. **A command to close link `<id>` with no command to ask which
`<id>`s exist is an incomplete pair**, and GH #211's own failure story — four
wedged peers exhausting the slots "with nothing the guest can say about it" — is
exactly the state `AT+CIPSTATUS` reports.

One sub-decision, flagged rather than assumed: the `<lport>` field is the
module's local port, which on jnext is a **host** socket's port. Reporting it
discloses a host detail into the guest for no benefit. Recommend reporting `0`
for `<lport>` on outbound links and the listener's own (guest-chosen) port for
inbound ones — the guest already knows both, so nothing is lost.

---

#### A4. `AT+CIPDOMAIN` — DNS without a connection

Resolve a name and answer `+CIPDOMAIN:<ip>`. In A because the **machinery already
exists and is already asynchronous**: the socket transport resolves on its own
thread (`make_socket_transport`, and `esp_socket_test` ASYNC-09 pins that
`poll()` returns while a lookup is outstanding). This is a new *command* over an
existing capability, not a new capability.

It must go through `AddressPolicy` like everything else, and a **denied** address
must answer `ERROR` without disclosing the address — otherwise `AT+CIPDOMAIN`
becomes a way to read addresses the policy exists to refuse, which would make it
a quiet hole in [§8.2](ESP01-EMULATOR-DESIGN.md#82-what-the-address-policy-actually-enforces).
That is a real requirement and its own test row.

---

### 5.2 Class B — on request only, with a use case

Implementable and observable, but nothing on the Next asks today, or the honest
answer needs a decision the project has not been given. The issue's own policy
covers these: *"emulated on a request basis, with specific use cases."*

| Command / group | Why it is B and not A | Why it is B and not C |
|---|---|---|
| **`AT+CIPMODE=1`** (passthrough only — `=0` and `?` are class **A**, two rows down) | Highest mention count of any unimplemented command (31) and **every mention is a comment explaining it cannot be used**: `CIPSERVER` needs `CIPMUX=1`, which forbids `CIPMODE=1`. The one real code path (`nextsync_raw_io.c`, ZX-Next-Unite fork) **is not compiled**. | Genuinely observable and genuinely useful — it removes all AT framing from the data path. But it **changes the whole UART contract** and every parser assumption in [§5.2 of the design doc](ESP01-EMULATOR-DESIGN.md#52-the-framing-constraints-that-actually-bite). See [Q3](#q3--passthrough-atcipmode1). |
| **`AT+CIPMODE=0` and `AT+CIPMODE?`** | — | **Recommend promoting just these two to A.** `=0` asks for the mode jnext is permanently in; refusing a request for the status quo fails a defensive client for no reason, and answering `OK` promises nothing. `=1` stays `ERROR`. This is the `AT+CIPMUX` precedent exactly ([§13.7c](ESP01-EMULATOR-DESIGN.md#137-what-implementation-decided-that-13-did-not)): *refuse a **change**, not the command.* |
| **`AT+SAVETRANSLINK`** | No consumer, and it is meaningless while passthrough itself is unbuilt: it *"set[s] whether to enter Wi-Fi passthrough mode on power-up"*, so it persists a mode jnext does not have. | Not an OTA/flash command despite being flash-backed — an earlier draft of this document filed it with `AT+CIUPDATE` under *"there is no firmware image"*, which is simply the wrong reason for it. It belongs here, gated on [Q3](#q3--passthrough-atcipmode1) (the mode) and [Q2](#q2--persistence) (the persistence), and it is the one command that needs **both** answered before it could be built. |
| **`AT+PING`** | No consumer. | Emulable, but not as ICMP — raw sockets need `CAP_NET_RAW`/admin on every platform jnext ships to. See [Q6](#q6--ping). |
| **SNTP** (`AT+CIPSNTPCFG`, `AT+CIPSNTPTIME?`) | No consumer **for the AT form** — `newt` gets the time by doing UDP NTP itself over `AT+CIPSTART="UDP"`, which already works (GH #198) and is regression-covered (`esp-udp-sntp-func`). | Emulable, but the answer is a *time*, and jnext already has two of those. See [Q7](#q7--sntp). |
| **`_CUR` / `_DEF` persistence variants** | The issue asks for them; whether they exist at all depends on [Q1](#q1--which-firmware-is-jnext-emulating). | Needs somewhere to persist **to**, and jnext has a config file. See [Q2](#q2--persistence). |
| **`AT+CIPRECVMODE` / `AT+CIPRECVDATA`** (passive receive) | No consumer. | Interesting on its merits: passive mode replaces the unsolicited `+IPD` push with a `+IPD,<id>,<len>` *notification* the guest then pulls with `AT+CIPRECVDATA`. That **dissolves the interleaving problem** the issue lists as a real-hardware behaviour jnext simplifies — no serialisation rule is needed if nothing is pushed. Worth remembering if that problem is ever reopened. **Confirmed present on ESP8266 in 2.x** (passive buffer 5760 bytes), and it brings `AT+CIPRECVLEN` and `AT+CIPDINFO` with it. |
| **MQTT / HTTP / web-server command categories** | No consumer, each is a protocol client in its own right, and **none exists in 1.x at all** — so under [Q1](#q1--which-firmware-is-jnext-emulating)(a) they are out of scope by construction. | Fully observable and genuinely valuable to a Z80 (an MQTT client the guest does not have to write). Also the **largest** item here by an order of magnitude. Now established per category: **MQTT is enabled in the shipped ESP8266 2.x firmware**; **HTTP is compiled out** of it (so a real module answers `ERROR`, as jnext already does); the web server is off by default. |
| **`AT+GSLP`** (deep sleep) | No consumer. | Observable — the module stops answering and then reboots. Not C: the guest can tell. |
| **`AT+RESTORE`** | No consumer; with no persistence it is `AT+RST` with extra steps. | Becomes meaningful the moment [Q2](#q2--persistence) is answered yes. |
| **`ATI`, `AT+SYSRAM?`, `AT+CWHOSTNAME`, `AT+CIPSTAMAC?`, `AT+CWDHCP`, `AT+CWAUTOCONN`, `AT+CIPSTA` set form** | No consumer; canned-identity readbacks in the [§5.6](ESP01-EMULATOR-DESIGN.md#56-synthetic-identity) family. | Trivial to add if asked. Deliberately **not** bundled into A2: A2 completes commands that already exist, and these are new surface. Adding them "while we are here" is how a narrow issue becomes a broad one. |
| **The faithful error strings** — `busy p...`, `ALREADY CONNECTED`, `SEND FAIL`, `link is not valid`, `no ip` | The issue explicitly asks for these. They are on the design doc's **never-emit list** ([§5.4](ESP01-EMULATOR-DESIGN.md#54-strings-that-must-never-be-emitted)) for an evidenced reason. | See [Q5](#q5--the-never-emit-list) — this is a decision, not an implementation. |

---

### 5.3 Class C — do not build, and why each one

The test from [§1](#1-the-goal-restated--and-the-two-readings-it-excludes): **a
Z80 reaches the module through four wires and an AT parser. If the command's
whole effect is on the far side of that, there is nothing to emulate** — only a
number to store and hand back, which is a lie with a round trip.

| Command / group | Why it cannot be meaningful here |
|---|---|
| **GPIO / ADC / PWM / I²C / SPI driver commands** (`AT+DRV*` in 2.x; the `AT+SYSGPIO*` family in 1.x) | Two independent reasons, either sufficient. (i) The ESP-01's GPIO pins terminate on the Next's WiFi header and **nothing on the Next reads them**: a write changes no signal any Z80 instruction can sample, and a read has no source. Storing the value so a read returns it emulates a variable, not a pin. (ii) In 2.x the whole category is **hard-excluded from the ESP8266 target** (`depends on AT_ENABLE && !IDF_TARGET_ESP8266`), so a real module answers `ERROR` — which is what jnext already does. |
| **RF power and tuning.** The family is **exactly three** commands in 1.x and one in 2.x: `AT+RFPOWER` (both), `AT+RFVDD` (1.x only — *"Sets the RF TX Power according to VDD33"*, NONOS manual §3.2.12; esp-at comparison marks it NONOS ✅ / ESP-AT ❌), and `AT+RFAUTOTRACE` (1.x only, and **undocumented**: the NONOS manual's V3.0 release notes say *"Remove AT+RFAUTOTRACE command"*, yet it is still bound to handlers in the shipped `libat.a` dispatch table and present in the 1.7.6 firmware image — the doc and the binary disagree, and the binary is what a guest talks to). | jnext has **no radio**. These parameterise a transmitter that does not exist and whose only observable — signal quality — is already synthetic and constant (`,1,-55` in `AT+CWJAP?`, [§5.6](ESP01-EMULATOR-DESIGN.md#56-synthetic-identity)). There is no quantity for them to change. |
| **Sleep modes** (`AT+SLEEP` modem/light sleep) | These trade power for latency. jnext models **no power domain** and its latency is the host's. The guest cannot distinguish any setting from any other. (`AT+GSLP` deep sleep is **not** here — it reboots the module, which is observable, so it is B.) |
| **Flash / OTA** (`AT+CIUPDATE`, `AT+SYSFLASH`, `AT+SYSROLLBACK`) | There is **no firmware image** — jnext *is* the firmware, and the version `AT+GMR` reports is a constant chosen to be honestly synthetic. `AT+CIUPDATE` cannot update anything, and emitting its `+CIPUPDATE:1..4` progress URCs and then `OK` would claim work that did not happen. **Today's `ERROR` is already a documented-correct answer**: the Next's own readme says *"If there are mistakes in the updating, then [the ESP will] break update and print ERROR"* (`WIFIand UARTReadME1st.txt:~570`). This is the one C-class command that the Next's docs mention, and it is already right. |
| **Bluetooth / BLE** | The ESP8266 **has no Bluetooth radio at all**. These are ESP32-only commands; on an ESP8266 target they are not a simplification, they are a category error. |
| **Signalling / factory / RF-test commands** | Production-line instrumentation for physical silicon. No guest-observable effect whatsoever. |
| **`AT+SYSMSG` message-format switches** | Real value, real risk, no consumer — but the deciding argument is different from the rest of this table: changing the *framing* of system messages is precisely what [§5.2](ESP01-EMULATOR-DESIGN.md#52-the-framing-constraints-that-actually-bite) says must not move, because three guest parsers busy-wait on the exact bytes **with no timeout**. A command whose purpose is to change those bytes is a hazard aimed at the one part of this module that hangs the emulated machine when it is wrong. |

**On the ones that are "only a stored value":** the alternative to `ERROR` is
answering `OK` and ignoring the setting. The project has already rejected that
pattern twice, on stated grounds — *"silently accepting promises a behaviour the
guest cannot ask back"* ([§1.4](ESP01-EMULATOR-DESIGN.md#14-what-was-deliberately-not-built),
and UDP `<mode>` 1/2 in [§5.7](ESP01-EMULATOR-DESIGN.md#57-udp-gh-198)). C-class
commands keep answering `ERROR`, which is a true statement about a module that
does not do that thing.

---

## 6. Proposed implementation order

Dependency-ordered. Effort is in **agent-sessions**, and each figure is
implementation only — the project's mandatory independent review is separate and
adds roughly half an item each.

| # | Item | Depends on | Sessions |
|---|---|---|---|
| **0** | **Answers to [§7](#7-questions-for-the-owner).** [Q1](#q1--which-firmware-is-jnext-emulating) gates the scope of every row below it; Q2-Q7 gate their own rows only. | — | *owner* |
| **1** | **Query dispatch plumbing.** One pre-argument-parse rule in `dispatch_line` so `AT+X?` reaches a per-command constant instead of each handler re-deriving it. Infrastructure for items 2 and 3, and the reason they are cheap. Shaped so a `=?` arm can be added later if its strings are ever established. | 0 (Q1) | **0.5** |
| **2** | **A1 — the Wi-Fi configuration category.** `AT+CWMODE` (set + query), `AT+CWJAP` set form, `AT+CWLAP`, `AT+CWQAP`. Includes wiring `CWMODE=2` / `CWQAP` to the existing `associated_` state, and one functional regression row driving the Next readme's documented workflow end to end. **This is the issue.** | 1; Q4, Q5 | **2** |
| **3** | **A2 — fill in the QUERY forms** for every command that already exists (`AT+CIPMUX?`, `AT+UART_CUR?` / `_DEF?` / `AT+UART?`, `AT+CIPSERVER?`). Mechanical once item 1 is in. **Test (`=?`) forms are excluded** — their reply strings are not documented in the named reference ([§5.5](#55-what-could-not-be-verified-and-one-thing-this-changed)). | 1 | **0.75** |
| **4** | **`AT+CIPMODE=0` / `AT+CIPMODE?` only** (the "refuse a change, not the command" promotion). Tiny, and independent of whether passthrough is ever built. | 1; Q3 | **0.25** |
| **5** | **A3 — `AT+CIPSTATUS`.** Reports the `conn_` table. Natural completion of server mode + `AT+CIPCLOSE=<id>`. | 1 | **1** |
| **6** | **A4 — `AT+CIPDOMAIN`.** DNS over the existing async resolver, with the `AddressPolicy` non-disclosure rule and its own row. | 1 | **1** |
| **7** | **Documentation.** Append a numbered section to `ESP01-EMULATOR-DESIGN.md` in its established per-issue style; **correct** the `§5.1` table and the user guide's "what is not emulated yet" ([§2.3](#23-four-facts-this-measurement-establishes-that-the-prose-does-not)); new `doc/testing/` test-plan doc; `FEATURES.md`. | 2-6 | **1** |

**Total for the A list: ≈ 6.5 agent-sessions** of implementation, plus review.

Two properties of this list worth stating up front:

- **It adds no CLI surface.** Nothing in items 1-6 needs a flag, so there is no
  man-page change, no `cli-check` table edit and no `docs-check` churn. The one
  new behaviour a *test* needs — taking the module off its network — already has
  its flags (`--esp-delayed-disassociate-frames`).
- **It adds no new architecture.** Every item is rows in `kCommands` plus
  constant strings, which is exactly what design-doc
  [§3 choice C](ESP01-EMULATOR-DESIGN.md#3-the-three-v11-shape-choices) predicted
  v1.1 would be: *"adding the ~40 commands v1.1 wants is adding rows, not
  extending an if/else chain."* Items 2-6 test that prediction; item 1 is the
  only place it is not literally true, and it is half a session.

**Not in this list, deliberately:** everything in [§5.2](#52-class-b--on-request-only-with-a-use-case)
and [§5.3](#53-class-c--do-not-build-and-why-each-one). If the owner wants any
B-class item, it is a separate sized row, not a bolt-on — MQTT/HTTP alone would
exceed the whole table above.

---

## 7. Questions for the owner

Each is a specific either/or with a recommendation. **Nothing below has been
decided and nothing has been built.**

### Q1 — Which firmware is jnext emulating?

The issue names **esp-at v2.3.0.0_esp8266**. Every oracle this module has used is
the **1.x NONOS AT firmware**: `AT+GMR` answers `1.7.4.0`, and the `AT+CIPSTO`
model was measured on a real ESP-01 running **AT 1.2.0.0** against the *ESP8266
AT Instruction Set v1.5.4* ([§3](#3-the-reference-names-a-firmware-jnext-does-not-claim-to-be)).

- **(a)** Stay **1.x**. `_CUR`/`_DEF` are real commands; MQTT/HTTP/web-server are
  out of scope by construction; `AT+GMR` keeps saying 1.7.4.0.
- **(b)** Move to **2.x** (esp-at v2.3.0.0). `_CUR`/`_DEF` become legacy,
  `AT+SYSSTORE` appears, several categories become candidates — and `AT+GMR`
  must change, which changes bytes an evidenced client already parses.

**Recommendation: (a), stay 1.x — and note the decision has effectively already
been made in code.** It is what the module claims to be, what the one hardware
measurement was taken against, what the ESP-01 modules people actually plug into
a Next run, and — decisively — what the shipped code already implements:
`AT+CIPDNS_CUR?` does not exist in 2.x at all, and jnext's `> ` prompt is the
1.x-era form that the NextZXOS `.UART` dot command requires and that the 2.x form
would **break** ([§3](#3-the-reference-names-a-firmware-jnext-does-not-claim-to-be)).
Moving to 2.x would also make the `AT+CIPSTO` model — the only hardware-measured
thing in the module — a claim about a firmware it was not measured on, and would
pull MQTT, `AT+SYSSTORE` and a dozen other categories into scope.

Keep reading the esp-at source regardless: it is Apache-2.0, it is the best text
available, and for commands the two versions share it is byte-exact where the 1.x
manual is prose. What this answer does is settle a large part of class B as **out
of scope by construction** rather than merely unasked-for.

**All seven class-A commands exist in 1.x — verified per command, and one of them
was not trivial.** An earlier draft of this document asserted "all seven exist in
both versions" as a throwaway line, and for `AT+CWMODE` two 1.x sources
contradict each other:

| Source | Bare `AT+CWMODE` in NONOS-AT 1.x? |
|---|---|
| esp-at's own `AT_Command_Set_Comparison.rst` — the page this document cites for the `_CUR`/`_DEF` Note-3 argument | **❌**, listed as an ESP-AT-only addition; only `AT+CWMODE_CUR` / `_DEF` are ✅ |
| ESP8266 Non-OS AT Instruction Set v3.0.5 (↔ AT_V1.7.x) | **Absent from the §4.1 reference table and §4.2 sections** — yet used, bare, in **eight** of the manual's own worked examples |
| **The shipped firmware's dispatch table** — `at_fun[]` decoded from `ESP8266_NONOS_SDK`'s `libat.a(at_cmd.o)`, relocations resolved | **Present**, slot 11, bound to `at_testCmdCwmode` / `at_queryCmdCwmode` / `at_setupCmdCwmodeDef` |
| The shipped 1.7.6 AT binary | `+CWMODE` present as its own NUL-delimited pool entry, not a prefix of `+CWMODE_CUR` |

**The binary settles it: bare `AT+CWMODE` works on a real 1.x module.** The
comparison page's `❌` is defensible as a claim about what the NONOS *manual
documents* and false as a claim about what the NONOS *firmware accepts* — and
that page is internally inconsistent about it, since `AT+CWJAP` is the
structurally identical case (manual: `_CUR`/`_DEF` only, bare in examples only)
and is marked ✅. The other six were checked the same way and are unambiguous in
every source.

**So the recommendation does not rest on "exists in both".** It rests on
[§4](#4-the-nexts-own-shipped-documentation-is-the-sharpest-evidence-there-is):
the Next's own shipped WiFi documentation instructs users to type the **bare**
spellings, so the bare spellings are what jnext must answer whatever a comparison
table says. The version evidence supports the recommendation; the Next's own
documentation is what decides it.

**One consequence for implementation, found by the same check.** Bare
`AT+CWMODE` is **not** an alias of `_CUR`: its setup handler is
`at_setupCmdCwmodeDef`, i.e. the bare form has **`_DEF` (flash-persisted)**
semantics, so on real 1.x hardware a mode set with the bare command *survives
`AT+RST`*. jnext persists nothing ([Q2](#q2--persistence)), and it deliberately
resets the mode on `AT+RST` instead — a **stated deviation**, not an oversight,
because `AT+RST`'s fixed reply announces `WIFI CONNECTED` / `WIFI GOT IP` and
that reply becomes a lie if a SoftAP-only mode survives the reset.

### Q2 — Persistence

Narrower than it looks, because the fetch settled the scope: **`AT+UART_CUR` /
`AT+UART_DEF` are the only `_CUR` / `_DEF` pair that survives into 2.x** — every
other pair is ESP-AT `❌` — and they are exactly the pair jnext already
implements. So this question is *only* about those two, whichever way
[Q1](#q1--which-firmware-is-jnext-emulating) goes.

jnext routes `AT+UART_CUR=`, `AT+UART_DEF=` and `AT+UART=` to the **same
handler**, so `_DEF` already lies slightly: it promises flash persistence and
delivers none. (In 2.x `AT+UART_DEF` is one of only three commands written to
flash **unconditionally**, regardless of `AT+SYSSTORE`.) Real persistence needs a
store, and jnext has one (`~/.jnext/jnext.conf`).

- **(a)** Keep them aliases. `_DEF` behaves as `_CUR`; nothing survives `AT+RST`. Document the deviation instead of leaving the code silent about it.
- **(b)** Persist the `_DEF` baud to the jnext config file.

**Recommendation: (a), keep them aliases and document the deviation.**
Persisting a guest-chosen baud rate into the user's config file lets a guest
program durably change jnext's behaviour for **later runs** — a surprising power
for very little gain — and it makes test runs order-dependent, which the
regression suite's determinism is worth more than. There is also no consumer:
nextsync sends `AT+UART_CUR`, the *non*-persistent one, and re-sends it every
run. If (b) is ever wanted it should be gated on a flag, off by default.

Under [Q1](#q1--which-firmware-is-jnext-emulating)(b) this question would grow a
second half — `AT+SYSSTORE`, 2.x's global replacement for the whole
`_CUR` / `_DEF` mechanism — which is one more reason (a) on Q1 keeps this small.

### Q3 — Passthrough (`AT+CIPMODE=1`)

It changes the whole UART contract: in passthrough the module stops parsing AT
lines entirely and every byte is payload until `+++`. Every framing guarantee in
design-doc §5.2 is suspended while it is on.

- **(a)** Do not build passthrough. Promote **only** `AT+CIPMODE=0` and
  `AT+CIPMODE?` so a defensive client asking for the status quo is not refused.
- **(b)** Build it.

**Recommendation: (a).** The evidence actively argues against (b): all 31 local
mentions are comments explaining it **cannot be used** alongside `CIPMUX=1`/
`CIPSERVER`, and the one real code path is in a fork that does not compile it.
(a) is item 4 in [§6](#6-proposed-implementation-order) and costs a quarter of a
session.

### Q4 — Should a Wi-Fi join ever fail?

`AT+CWJAP="ssid","pass"` against a module whose network is synthetic.

- **(a)** Always succeed, echoing the SSID back to `AT+CWJAP?`.
- **(b)** Succeed only for `JNextWifiHost`; anything else answers the documented
  failure — which in 2.x is `+CWJAP:<error code>` then **`ERROR`**, *not* `FAIL`
  (`ESP_AT_RESULT_CODE_FAIL`'s string is literally `"ERROR"`). 1.x is the version
  that answers `FAIL`, so the exact bytes depend on
  [Q1](#q1--which-firmware-is-jnext-emulating).

**Recommendation: (a).** The Next's own documentation tells the user to type
their *real* SSID, so (b) fails the exact workflow this work exists to fix — the
one SSID guaranteed *not* to be typed is `JNextWifiHost`. Under 1.x, (b) also
needs `FAIL`, which is on the never-emit list ([Q5](#q5--the-never-emit-list)).
If failure injection is wanted later it belongs on a **flag**, like the existing
outage scheduling — a testable failure the *host* schedules, not one a guest can
trip by typing its own network's name correctly.

### Q5 — The never-emit list

`busy p...`, `ALREADY CONNECTED`, `SEND FAIL`, `link is not valid`, `no ip`,
`ready` are deliberately never emitted ([§5.4](ESP01-EMULATOR-DESIGN.md#54-strings-that-must-never-be-emitted)),
because `ESPATreadme.TXT:92` records that an unexpected URC leaves the NextZXOS
driver in an unknown state. The issue explicitly asks for them.

- **(a)** Keep the list intact. Faithful strings stay unemitted.
- **(b)** Emit them where firmware would.
- **(c)** Keep the list, with the single exception of a URC the guest's **own
  command** asked for — specifically `WIFI DISCONNECT` in reply to `AT+CWQAP`.

**Recommendation: (c).** (b) puts the one live-traffic-proven path (NXtel against
the real nx.nxtel.org BBS) at risk to satisfy a datasheet, and the never-emit
list is about **unexpected** URCs — a `WIFI DISCONNECT` the guest just asked for
by sending `AT+CWQAP` is not unexpected, by the same reasoning that lets `AT+RST`
drop connections without `CLOSED`. If you prefer the narrowest possible change,
(a) is safe and `AT+CWQAP` simply answers `OK` silently; say which and item 2
follows it.

### Q6 — Ping

`AT+PING="host"` answers `+<time>` or `+timeout`. ICMP needs `CAP_NET_RAW` or
admin rights on every platform jnext ships to, and jnext must not need
privileges.

- **(a)** Do not implement (stays `ERROR`).
- **(b)** Implement as a **TCP connect-time** measurement to a port, reported as
  a ping time.
- **(c)** Implement as a synthetic constant.

**Recommendation was (a).** (c) is fiction. (b) measures something real but not
the thing it reports — a host that black-holes the port reads as "down" when
ICMP would say "up", so a guest using it as a reachability test gets a wrong
answer that looks authoritative.

> **OWNER ANSWER: BUILD IT — and the recommendation above was WRONG on a point
> of fact, not of taste.**
>
> Its premise was the first sentence of this section: *"ICMP needs
> `CAP_NET_RAW` or admin rights."* That is false on a modern Linux, and
> measurably false on this project's own host. `/usr/bin/ping` there is plain
> `0755` with no setuid bit and no file capabilities; it works because
> `net.ipv4.ping_group_range` is `0 2147483647`, i.e. the kernel permits any
> group to open a `SOCK_DGRAM`/`IPPROTO_ICMP` socket. **jnext has that
> capability on identical terms**, so there was never a privilege to acquire
> and option (d) — do it in process, properly — was available all along and
> went unconsidered.
>
> The route was found by the owner asking for something else: *"can you
> implement ping by calling each platform's ping command?"* Checking what
> privilege that binary actually holds is what revealed that it holds none, and
> the implementation became an in-process ICMP socket instead — which also
> works in the Flatpak (whose runtimes ship no `ping` at all), parses no output,
> and exposes no argv to a guest-supplied hostname.
>
> Built in [§20](ESP01-EMULATOR-DESIGN.md#20-atping-gh-154). The lesson worth
> keeping: **a recommendation to decline should state the fact it rests on
> plainly enough to be checked.** This one did, and the check overturned it.

### Q7 — SNTP

`AT+CIPSNTPCFG` / `AT+CIPSNTPTIME?` would have the module report a time. jnext
has two clocks: the host's, and the emulated RTC that `--rtc` pins for
deterministic screenshots.

- **(a)** Do not implement (stays `ERROR`). `newt` already gets the time by
  doing NTP itself over `AT+CIPSTART="UDP"`, which works and is regression-covered.
- **(b)** Answer from the **emulated RTC**, so `--rtc` keeps a test deterministic.
- **(c)** Answer from the host clock, or by really querying an NTP server.

**Recommendation was (a), and (b) if it is wanted at all.** (c) breaks
determinism and puts host state into the guest.

> **OWNER ANSWER: BUILD IT, AND BUILD (c)** — *"implement SNTP cfg and time by
> configuring the server and querying the real NTP server"*.
>
> The determinism objection was real and is **not** dismissed: it is met by
> documenting the divergence instead of hiding it. `--rtc` pins the emulated
> clock so boot screenshots are reproducible; a guest that asks SNTP for the
> time gets wall-clock regardless, because that is what a real time server
> returns. The two clocks disagree **on purpose**, it is stated in
> [§21](ESP01-EMULATOR-DESIGN.md#21-sntp-gh-154) and in the user guide, and no
> regression row screenshots an SNTP-derived date.
>
> The "no consumer" argument also survives unchanged and is simply overruled:
> `newt` still does NTP itself over `AT+CIPSTART="UDP"`. Building the AT form
> serves software that has not been written yet, which is the whole premise of
> this issue.

---

### 5.4 The complete table

**Source.** The enumeration below is the full ESP-AT `release/v2.3.0.0_esp8266`
command set, taken from the **raw** `docs/en/AT_Command_Set/*.rst` on
`raw.githubusercontent.com`, cross-checked against `main/Kconfig` target gates,
the per-module firmware table, and `strings` over the shipped
`components/at/lib/libesp8266_at_core.a`. Per-chip applicability is from the
Kconfig gates, which are authoritative where the prose is silent.

**The `Not on ESP8266` column is not an opinion.** 84 of the 183 documented
commands are gated out of the ESP8266 target by `Kconfig` — BLE (45), Classic
Bluetooth (22), Driver/GPIO/ADC/PWM/I²C/SPI (12, `depends on … && !IDF_TARGET_ESP8266`),
Ethernet (2), `AT+FS`, `AT+SYSTEMP`, `AT+CWJEAP`. They are not "not worth
emulating"; **they are not on the chip**, and a Next program cannot send them to
a module that does not implement them.

That leaves **99 commands available on an ESP8266**, classified:

| | Count | |
|---|---|---|
| **Already implemented** (fully) | **14** | |
| **A — implement now** | **7** | `AT+CWMODE`, `AT+CWJAP` (set form), `AT+CWLAP`, `AT+CWQAP`, `AT+CIPSTATUS`, `AT+CIPDOMAIN`, `AT+CIPMODE` (`=0` / `?` only) — plus query-form completion on 4 commands that already exist |
| **B — on request** | **53** | |
| **C — do not build** | **25** | |
| *(Not on ESP8266 at all)* | *84* | *out of 183 documented* |

**Seven of ninety-nine.** That is the judgement this document exists to make.

#### Basic — 20 on ESP8266 (`AT+FS`, `AT+SYSTEMP` are ESP32/S2-only)

| Command | Class | Note |
|---|---|---|
| `AT`, `ATE`, `AT+RST`, `AT+GMR` | **done** | |
| `AT+UART_CUR`, `AT+UART_DEF` | **done (set)** | **query form missing** → item 3 |
| `AT+CMD` | B | Lists which commands and forms the firmware supports. 2.x only; genuinely useful for capability discovery. |
| `AT+GSLP` | B | Deep sleep — observable (module goes away, then reboots). |
| `AT+RESTORE` | B | Factory reset; meaningful only with persistence ([Q2](#q2--persistence)). |
| `AT+SYSRAM` | B | Free-heap query; answerable with a synthetic constant. |
| `AT+SYSTIMESTAMP` | B | A time; same decision as [Q7](#q7--sntp). |
| `AT+SYSLOG` | B | Toggles the AT error-code prompt. |
| `AT+SYSSTORE` | B | 2.x's replacement for `_CUR`/`_DEF` ([Q1](#q1--which-firmware-is-jnext-emulating), [Q2](#q2--persistence)). |
| `AT+SLEEP` | C | Power/latency trade-off; no power domain to model. |
| `AT+SYSMSG` | C | **Changes the framing of the exact bytes three guest parsers busy-wait on with no timeout.** A hazard aimed at the one thing that hangs the machine when wrong. |
| `AT+SYSFLASH`, `AT+SYSROLLBACK` | C | Flash partitions / firmware rollback — no firmware image exists. |
| `AT+RFPOWER` | C | No radio. |
| `AT+SLEEPWKCFG` | C | Light-sleep wake GPIO — no GPIO, no sleep. |
| `AT+SYSREG` | C | Raw read/write of ESP silicon registers. |

#### Wi-Fi — 25 on ESP8266 (`AT+CWJEAP` is ESP32-only)

| Command | Class | Note |
|---|---|---|
| **`AT+CWMODE`** | **A** | [§5.1 A1](#a1-the-wi-fi-configuration-category--the-4-gap) |
| **`AT+CWJAP`** | **A** | query done; **set form missing** |
| **`AT+CWLAP`** | **A** | one synthetic AP; never a host scan |
| **`AT+CWQAP`** | **A** | drives the existing `associated_` state |
| `AT+CWSTATE`, `AT+CWRECONNCFG`, `AT+CWLAPOPT`, `AT+CWDHCP`, `AT+CWAUTOCONN`, `AT+CWHOSTNAME`, `AT+CIPSTAMAC`, `AT+CIPSTA` (set form) | B | Synthetic-identity readbacks and knobs; trivial individually, new surface collectively. |
| `AT+MDNS` | B | A real network service — but multicast is denied by `AddressPolicy` default ([§8.2](ESP01-EMULATOR-DESIGN.md#82-what-the-address-policy-actually-enforces)), so it needs a policy decision first. |
| `AT+CWSAP`, `AT+CWLIF`, `AT+CWQIF`, `AT+CWDHCPS`, `AT+CIPAPMAC`, `AT+CIPAP` | C | **SoftAP.** An access point is a radio function: there is nothing for a station to associate *over*, `AT+CWLIF` would list clients that cannot exist, and turning "be an AP" into "open a host listening socket" is a different feature that `AT+CIPSERVER` already provides — with a security review ([§13.4](ESP01-EMULATOR-DESIGN.md#134-security-review--the-inbound-surface)) this would bypass. |
| `AT+CWSTARTSMART`, `AT+CWSTOPSMART`, `AT+WPS` | C | SmartConfig and WPS are **over-the-air credential handshakes** with a phone or a router button. No radio, no counterparty, nothing to observe. |
| `AT+CWAPPROTO`, `AT+CWSTAPROTO`, `AT+CWCOUNTRY` | C | 802.11 b/g/n selection and regulatory domain — radio parameters with no emulated quantity to change. |

#### TCP/IP — 34

| Command | Class | Note |
|---|---|---|
| `AT+CIPSTART`, `AT+CIPSEND`, `AT+CIPSENDEX`, `AT+CIPCLOSE`, `AT+CIFSR`, `AT+CIPSTO` | **done** | `AT+CIFSR` emits 2 of the 12 lines 2.x documents (the rest are IPv6 and Ethernet). |
| `AT+CIPMUX`, `AT+CIPSERVER` | **done (set)** | **query form missing** → item 3 |
| **`AT+CIPSTATUS`** | **A** | [§5.1 A3](#a3-atcipstatus--and-a-correction-to-the-record). Documented format: `STATUS:<stat>` + one `+CIPSTATUS:<link ID>,<"type">,<"remote IP">,<remote port>,<local port>,<tetype>` per link. **Deprecated in 2.x in favour of `AT+CIPSTATE`** — under [Q1](#q1--which-firmware-is-jnext-emulating)(a) `AT+CIPSTATUS` is the right spelling. |
| **`AT+CIPDOMAIN`** | **A** | `+CIPDOMAIN:<"IP address">` then `OK`. |
| **`AT+CIPMODE`** | **A** *(partial)* | `=0` and `?` only; `=1` passthrough is B ([Q3](#q3--passthrough-atcipmode1)). |
| `AT+CIPV6`, `AT+CIPSTATE`, `AT+CIPSTARTEX`, `+++`, `AT+CIPSERVERMAXCONN`, `AT+SAVETRANSLINK`, `AT+CIPDINFO`, `AT+CIPRECONNINTV`, `AT+CIPTCPOPT` | B | `AT+CIPDINFO` unlocks 2 of the 6 documented `+IPD` formats (the ones carrying remote IP/port). `AT+CIPSTARTEX`'s syntax is **undocumented** in this release. |
| `AT+CIPRECVMODE`, `AT+CIPRECVDATA`, `AT+CIPRECVLEN` | B | Passive receive — see [§5.2](#52-class-b--on-request-only-with-a-use-case). |
| `AT+CIPSNTPCFG`, `AT+CIPSNTPTIME` | B | [Q7](#q7--sntp) |
| `AT+PING` | B | [Q6](#q6--ping). Documented: `+PING:<time>`+`OK`, or `+PING:TIMEOUT`+`ERROR`. |
| `AT+CIPDNS` | B | 2.x spelling; jnext ships the **1.x** `AT+CIPDNS_CUR?` ([Q1](#q1--which-firmware-is-jnext-emulating)). |
| `AT+CIPSSLCCONF`, `AT+CIPSSLCCIPHER`, `AT+CIPSSLCCN`, `AT+CIPSSLCSNI`, `AT+CIPSSLCALPN`, `AT+CIPSSLCPSK` | B | Six TLS-configuration commands, all blocked on TLS itself, which has no consumer. Configuring a transport that does not exist is worse than refusing it. |
| `AT+CIUPDATE` | **C** | OTA. jnext *is* the firmware. **Today's `ERROR` is already a documented-correct answer** — the Next's own readme records that a failed update prints `ERROR` (`WIFIand UARTReadME1st.txt:574`). |

#### MQTT (11) · HTTP (3) · Web server (1) · Signaling (1) · User (4)

| Group | Class | Note |
|---|---|---|
| **MQTT** — `AT+MQTTUSERCFG`, `AT+MQTTLONGCLIENTID`, `AT+MQTTLONGUSERNAME`, `AT+MQTTLONGPASSWORD`, `AT+MQTTCONNCFG`, `AT+MQTTCONN`, `AT+MQTTPUB`, `AT+MQTTPUBRAW`, `AT+MQTTSUB`, `AT+MQTTUNSUB`, `AT+MQTTCLEAN` | B | **Enabled in the shipped ESP8266 2.x firmware**, so on 2.x this is a real capability, not a theoretical one — and a genuinely attractive one, since it hands a Z80 an MQTT client it would otherwise have to write. It is also by far the **largest** item in this document: eleven commands, a broker connection, a session and a subscription model. Under [Q1](#q1--which-firmware-is-jnext-emulating)(a) it is out of scope by construction — MQTT does not exist in 1.x. |
| **HTTP** — `AT+HTTPCLIENT`, `AT+HTTPGETSIZE`, `AT+HTTPCPOST` | B | **Not in the shipped ESP8266 firmware** (`default n`, disabled in both ESP8266 module configs, marked `×` in the per-module table) — although the code is present in the library. So even on 2.x a real ESP-01 would answer `ERROR`, which is what jnext already does. |
| **Web server** — `AT+WEBSERVER` | C | Off by default; and its purpose is a captive-portal Wi-Fi-setup page served over the module's **own SoftAP**, which is C for the SoftAP reason above. |
| **Signaling test** — `AT+FACTPLCP` | C | ESP8266-exclusive and on by default, and pure RF production instrumentation (long/short PLCP preamble). No guest-observable effect. |
| **User** — `AT+USERRAM` | B | Scratch RAM in the module; emulable as a buffer if anything ever wants it. |
| **User** — `AT+USEROTA`, `AT+USERWKMCUCFG`, `AT+USERMCUSLEEP` | C | OTA again, plus two commands about the module waking/being woken by the **host MCU over a GPIO line** — the Next has no such line to the ESP header. |

---

### 5.5 What could not be verified, and one thing this changed

**Everything in [§5.4](#54-the-complete-table) was fetched.** No documentation
page failed to load; the enumeration is from raw sources, not from memory. The
gaps below are silences **in the documentation itself**, and they are listed
because two of them change the plan.

1. **THE TEST FORM `AT+X=?` IS NOT DOCUMENTED ANYWHERE IN v2.3.0.0.** There is
   not one `Test Command` heading in any of the twelve category pages, and the
   literal `=?` does not appear in the TCP/IP page at all. The index defines four
   command types and then documents three.
   **This directly contradicts an earlier draft of this document**, which asserted
   that "real firmware answers a parameter template (`AT+CWMODE=?` →
   `+CWMODE:(1-3)`)". That is **unverified** against the named reference, and it
   is corrected in [§5.1 A2](#a2-the-symmetry-gaps--query-and-test-forms) and in
   item 3 of [§6](#6-proposed-implementation-order): the **query** (`?`) forms are
   documented and safe to build; the **test** (`=?`) forms are not, and are
   deferred until their reply strings come from the 1.x NONOS AT instruction set
   or from hardware. Emitting a guessed template is precisely the
   plausible-but-wrong this project treats as worse than nothing.
2. **`AT+CIPSTARTEX` has no syntax or response block**, only a sentence deferring
   to `AT+CIPSTART`. It stays B on those grounds alone.
3. **Line endings on three of the legacy status strings are unresolvable.**
   `ALREADY CONNECTED`, `link is not valid` and `no ip` are stored in
   `libesp8266_at_core.a` with a **lone CR**, not CRLF, unlike every other
   message; whether the emitting code appends the `\n` is in the closed-source
   core (`AT+GMR`'s own documentation: *"Code is closed source, no plan to
   open"*). If [Q5](#q5--the-never-emit-list) is ever answered (b), those three
   need hardware verification before a byte is emitted.
4. **The pre-`ready` ESP8266 boot output is undocumented** in this release; the
   only boot-log sample given is an ESP32 one. The banner itself **is** settled:
   `"\r\nready\r\n"`, emitted from open source (`main/interface/uart/at_uart_task.c:748`)
   as the last statement of `app_main()`, preceded by a firmware-version line.
5. **Four command names exist in the ESP8266 library with no documentation page**
   (`+CIPSSLCPSKHEX`, `+MQTTCLIENTID`, `+MQTTUSERNAME`, `+MQTTPASSWORD`).
   Existence only; no syntax, so no classification is offered.
6. **`AT+CIPSTART` SSL shows `OK` with no `CONNECT` line** while TCP and UDP both
   show `CONNECT` then `OK`. Whether that is a real asymmetry or a doc omission
   cannot be told from the page. Moot while TLS is unbuilt, and noted so it is
   not "discovered" again later.
7. **1.x command *names* referenced in [§5.3](#53-class-c--do-not-build-and-why-each-one)
   were not fetched.** The GPIO family is `AT+DRV*` in 2.x (ESP32-only) and was
   spelled `AT+SYSGPIO*` in 1.x. The classification is unaffected — it rests on
   "the Next reads no ESP GPIO pin", which is true under either spelling — but
   the 1.x spellings are stated from general knowledge, not from a fetched page,
   and are flagged as such. They should be confirmed if [Q1](#q1--which-firmware-is-jnext-emulating)
   is answered (a) and anyone proposes to revisit them.

---

## 8. Evidence index

Every claim in this document traces to one of these. Where something is
**inferred** or **unverified**, it says so at the point of use rather than here.

### Measured in this analysis

| Claim | How |
|---|---|
| The whole of [§2.2](#22-the-measured-baseline) — what the engine answers to 54 command lines | A throwaway probe (scratchpad, **not committed**) linking `src/esp01/src/{esp_at,esp_log,esp_socket,esp_address_policy,esp_socket_posix}.cpp` against a null transport and null listener, one fresh engine per line |
| The dispatch table is **21 rows** / 19 commands, prefix-matched, first match wins | `src/esp01/src/esp_at.cpp:119-144`, `:244-252` — counted from the source, not from the cited line span |
| Everything unmatched answers `\r\nERROR\r\n` | `esp_at.cpp:251-252`; `queue_error()` at `esp_at.h:921` |
| `AT+CWJAP?`, `AT+CIPSTA?`, `AT+CIFSR`, `AT+CIPDNS_CUR?`, `AT+GMR` reply bodies | `esp_at.cpp:840-880` |
| Association state is host-driven and guest-invisible except through `AT+CIFSR` | `esp_at.h:734-735,1034`; `esp_at.cpp:342,870` |
| `esp_at_test` carries 344 rows under 18 ID prefixes | `test/unit-tests.conf`; prefix census over `src/esp01/test/esp_at_test.cpp` |

### The Next's own software and documentation

| Claim | Citation |
|---|---|
| The documented user workflow, and the five commands in it that fail | `tbblue/docs/extra-hw/wifi/WIFIand UARTReadME1st.txt:232-280` |
| `AT+CWQAP` in the documented workflow | *ibid.* `:372` |
| `AT+CWMODE=3` + `AT+CIUPDATE` OTA sequence | *ibid.* `:553-566` |
| **A failed `AT+CIUPDATE` prints `ERROR`** — i.e. jnext's current answer is already in spec | *ibid.* `:574` |
| `AT+UART_CUR?` cited by the Next's own UART prescaler table | *ibid.* `:737` |
| `AT+CIPMODE` is unusable beside `CIPMUX=1` / `CIPSERVER`, stated by a live consumer | `dezogif_ng/src/transport_esp.asm:24-30`, `:4107-4109` |
| nextsync's `AT+CIPMODE=1` path exists only in an uncompiled fork file | `doc/testing/NEXTSYNC-VERIFICATION.md:36-38` |
| Command-mention census across every local ZX source tree | `grep -rhoE 'AT\+[A-Z0-9_]+'` over `/home/jorgegv/src/spectrum`, jnext excluded |

### The Espressif reference (fetched, not remembered)

Fetched from `raw.githubusercontent.com` on branch `release/v2.3.0.0_esp8266`:
`docs/en/AT_Command_Set/*.rst` (14 files), `docs/en/AT_Command_Examples/TCP-IP_AT_Examples.md`,
`main/Kconfig`, `main/app_main.c`, `main/interface/uart/at_uart_task.c`,
`components/at/include/esp_at_core.h`, the two ESP8266 `sdkconfig.defaults`, and
`components/at/lib/libesp8266_at_core.a` (639,650 bytes, core rev `2522d50`)
inspected with `strings` and byte-level search.

| Claim | Where |
|---|---|
| 183 documented commands; 84 gated off the ESP8266 target | `main/Kconfig` target gates + the per-module firmware table |
| Driver (GPIO/ADC/PWM/I²C/SPI) is `depends on AT_ENABLE && !IDF_TARGET_ESP8266` | `main/Kconfig:240` |
| BLE (45) and Classic BT (22) depend on `IDF_TARGET_ESP32*` | `main/Kconfig:146,163` |
| MQTT **enabled** in both ESP8266 module configs; HTTP `default n` and disabled | `module_config/module_esp8266_*/sdkconfig.defaults`; `main/Kconfig:135` |
| The five result-code literals, byte-exact, incl. `"\r\nOK\r\n\r\n>"` | result-code table at offset `0x7c2b0` of `libesp8266_at_core.a`, cross-checked against `esp_at_core.h:182-192` |
| `ESP_AT_RESULT_CODE_FAIL`'s string is `"ERROR"` — **there is no bare `FAIL`** | `esp_at_core.h:182-192` |
| `AT+CWJAP` failure is `+CWJAP:<error code>` then `ERROR` | Wi-Fi `.rst` |
| All thirteen `_CUR` / `_DEF` pairs are ESP-AT `❌` + *"will not be added to the ESP-AT version"*; `AT+UART_CUR` / `_DEF` survive | `AT_Command_Set_Comparison` page, Note 3 |
| Six `+IPD` format strings (active/passive × mux × `CIPDINFO`) | format strings in `libesp8266_at_core.a`, corroborated by TCP-IP `.rst:1904-1909` |
| `ready` is `"\r\nready\r\n"`, last statement of `app_main()` | `main/interface/uart/at_uart_task.c:748`; `main/app_main.c` |
| `AT+CIPSTATUS` / `AT+CIPDOMAIN` / `AT+PING` response formats | TCP-IP `.rst` |
| **No `Test Command` heading exists in any of the twelve category pages** | absence over all fetched `.rst` — the basis for deferring `=?` |

### Not verified, and flagged as such at the point of use

- The 1.x spellings of the GPIO family (`AT+SYSGPIO*`) — [§5.5 item 7](#55-what-could-not-be-verified-and-one-thing-this-changed).
- Line endings on `ALREADY CONNECTED` / `link is not valid` / `no ip` — [§5.5 item 3](#55-what-could-not-be-verified-and-one-thing-this-changed).
- `AT+CIPSTARTEX` syntax; the SSL `CONNECT` asymmetry; the pre-`ready` ESP8266 boot output — [§5.5](#55-what-could-not-be-verified-and-one-thing-this-changed) items 2, 6, 4.
- The `.UART` / `.ESPBAUD` / NXtel line citations inherited from
  [ESP01-EMULATOR-DESIGN.md §12](ESP01-EMULATOR-DESIGN.md#12-evidence-index),
  whose own provenance note records that **no NXtel or NextZXOS dot-command
  source exists on this machine**. This document adds no new claim resting on
  them; the `.UART` `OK`,13,10,`>` sequence used in
  [§3](#3-the-reference-names-a-firmware-jnext-does-not-claim-to-be) is one of
  those inherited, unre-verified citations, and the Q1 argument it supports has
  two other legs that do not depend on it.

---

*Phase 1 ends here. Nothing above has been implemented, and the test triplet was
not run — this change adds one document and touches no code.*
