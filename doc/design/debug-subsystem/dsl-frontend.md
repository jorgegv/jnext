# Debugger DSL frontend — design (GH #26, carrying #279's vocabulary; #20 verdict)

Part of the GH #276 debug-subsystem design. Sibling of `backend.md` (design-backend),
the Qt, DZRP, ZRCP and GDB-RSP frontend documents. This file is the DSL's; it
records what the language is, what it demands of the backend, where it stops,
and the answer to "is #20 just a use case of #26?".

Status: **v6**, 2026-09-27 — revised after review round 5
(`scratchpad/reviews/dsl-qt-r5.md`, REJECT on R5-1; N5-1 folded; Appendix F).
v5 was revised after review round 4
(`scratchpad/reviews/dsl-qt-r4.md`, REJECT: R-1, R-2 mine; N-1..N-9 folded;
Appendix E). v4 was revised after the OWNER review (comments `;`
and `//`; MUTATION ALLOWED — the no-poke reading of "must not perturb" is
reversed; `on copper` / `on dma` events; demo + script-suite WP; exhaustive
user-guide chapter WP; owner answers recorded in §10). v3 was revised after review round 1
(`scratchpad/reviews/dsl-qt.md`, REJECT: R-1..R-7, N-1..N-15), round 2
(`scratchpad/reviews/dsl-qt-r2.md`, REJECT on R2-1 only; N2-1..N2-6) and
round 3 (`scratchpad/reviews/dsl-qt-r3.md`, **APPROVE**; N3-1, N3-2 folded);
dispositions in Appendices C and D. v1 was written against the code at `main`
v1.0.44 (`974b0ab19`), never against `doc/design/EMULATOR-DESIGN-PLAN.md`.
REQ verdicts in §5 are updated as `design-backend` replies.

---

## Work packages — the tracker for this package

Mirrors this package's row in [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
§10.1, which stays authoritative: if the two ever disagree, §10.1 wins and this
table is stale. It exists because §10.1 states each package's sequence as one
long table cell, which is unreadable as a plan and impossible to track against.

Status values: `todo` · `in progress` · `in review` · **`done`** (independently
reviewed and APPROVED). The whole package lands on **one branch** and merges
whole, so `done` here means the sub-item is approved, not merged.

| WP | Branch `gh26-dsl` (issue #26, carrying #279) | Status |
|---|---|---|
| **WP1** | lexer / parser / `compile_expr` **exported as a library** (Z WP-4 consumes it) — as built: Appendix G | **done** |
| **WP2** | evaluator + the snapshot stacks (`snap` / `unsnap` / `changed()`, which is how #279's span invariants are served with no new event kind) — as built: Appendix H | **done** |
| **WP3** | engine over subscriptions, stop / exit policy. Headless script `stop` with no explicit `exit` is code **3** (never 2, a harness fault) — as built: Appendix I | **done** |
| **WP4** | CLI + man page (`--script`, `--script-key`) — as built: Appendix J | **done** |
| **WP5** | GUI — Script tab, **Alt+1..Alt+8** as the DSL host-key namespace in both windows. **Needs Q** — as built: Appendix K | **done** |
| **WP6** | the recorder — **this is #20**, after its re-scope: recorder + `compare_scr` + INS-16 + the two parked DAPR rows — as built: Appendix L | **done** |
| **WP7** | demos + the `script-*-func` rows — delivered together with WP10, as built: Appendix M | in review |
| **WP8** | developer-guide pages — developer guide 3.12, `script-pipeline` figure, FEATURES.md (§9 WP8b; its man-page part was WP4/WP6) | in review |
| **WP9** | **an exhaustive User Guide chapter for the DSL** (`src/doc/user-guide`, `docs-userguide-check`-gated) — the owner's words: the most powerful feature of jnext — as built: Appendix N | in review |
| **WP10** | **a demo program + script suite** under `demo/dsl_demo/` exercising every event kind and action, with ten `script-*-func` rows — the same deliverable as WP7, done as one package (Appendix M) | in review |

Depends on: B0 (landed), B; WP5 on Q. design-dsl owns WP9 and WP10.

**Mutation is ALLOWED** (owner decision 2026-09-27): Revision 3's "scripts never
poke" rule is withdrawn, and §4.2a defines the contract — `set <target> = expr`
and `out`. The DSL is **the** event primitive rather than one of two, and the
yardstick for the backend's completeness, since it is the only consumer expected
to reach essentially everything.

Every sub-item is reviewed by an agent or person that did NOT write it, and the
branch does not merge until the full §10.3 gate is green on the tip.

---

## 0. Settled decisions (recorded, not re-argued)

From #279 and #277 (owner, 2026-09-26):

| Decision | Consequence here |
|---|---|
| **No plugin API.** No C ABI, no `dlopen`, no scripting binding exposed as a plugin host. | This language is the only user-programmable event consumer. §4 says where it runs out, which is the evidence that would reopen the decision. |
| **The DSL is THE event primitive.** #279 is a set of requirements on this vocabulary. | §3 reproduces #279's acceptance sketch as scripts, verbatim in intent. |
| **Conditions and value predicates live in the backend.** DZRP declines them; the DSL uses them fully. | `when` compiles to a backend predicate (§5.2). The interpreter never sees a non-matching hit. |
| **Hot-path cost belongs to the backend.** | The DSL registers ranges; it does not implement the per-8K-page bitmap. §5.3. |
| **Observation must not perturb the machine** — and (owner, 2026-09-27) **mutation is allowed** when explicit. | Every READ path is side-effect-free by contract (peek paths, InspectionScope). Every WRITE is an explicit verb (`set …`, `out`), logged, and applied at the delivery point so timing stays deterministic. §6.2. |
| **Deterministic time only.** | No wall-clock anywhere in the language. `FRAME`, `CYCLE`, raster counters. |
| **#20 is settled here** (#277: "is #20 a use case of #26?"). | §7: **yes**, with a precise shape. |

---

## 1. Scope, and what this drops from `SCRIPTABLE-DEBUGGER.md`

`doc/design/SCRIPTABLE-DEBUGGER.md` (2026-04-09, 923 lines) predates every
decision above and was designed with no backend: its §4.1 wires the engine
straight into `Z80Cpu::on_m1_cycle`, `Mmu` and a `port_manager.h` that does not
exist. It also predates the GH #219/#225 `DebugState` gating model
(`src/debug/debug_state.h`), the GH #222 port-watch semantics
(`src/debug/breakpoints.h:170-199`) and the raster-counter disambiguation of
GH #22 (`src/debug/raster_state.h`). What survives, what is dropped, and why:

**Kept** (with renamed or tightened semantics): `on <event> [once] [when] do … end`,
`var`, integer/boolean/string values with `${}` interpolation, `print`
(now `log`), `assert`, `exit`, `dump_regs`, `dump_mem`, address ranges,
`if/else`, named rules with `enable`/`disable`, `.MAP` symbols, headless exit
codes.

**Dropped:**

| Old feature | Why |
|---|---|
| `break`, `continue`, `step`, `step_over`, `step_out`, `run_to*` as script actions | A script is an *observer that fires at events*; it is not a REPL driving the machine. Stepping from inside a callback is re-entrant (the callback runs inside `run_frame()`), and "run to X then assert" is `on execute X do assert … end`. `break` becomes `stop`, matching #279's vocabulary. |
| Top-level actions executed at load time, `run_to` at top level | Same reason: nothing runs outside an event. Top level holds declarations only. |
| REPL, script console with input, multi-line REPL | Not in any acceptance case; the GUI gets a read-only log tab (§6.4). |
| `while` loops, `load "other.jds"` | Not needed by the workload; a loop inside a per-write callback is exactly the hot-path risk #26 warns about. Listed as a wall in §4 with the cheap escape hatch. |
| `watch`/`unwatch` actions (dynamic watchpoint creation from a script) | A script's `on write` *is* the watch. Runtime arming is `enable`/`disable` of a named rule. |
| `save_trace`, `trace N`, `disasm`, JUnit XML, `--test-report-xml`, Lua backend, TCP server, hot reload, "visual timeline" | Speculative. The trace log and disassembler stay GUI/DZRP capabilities; the DSL does not need them for any case in #26/#279/#20. |
| `VC` = "visible scanline 0..255", `HC` = "T-state within scanline" | Wrong on the Next: there are four vertical counters with different origins (`raster_state.h:13-48`; GH #16 and #181 were both this confusion). §2.4 names the VHDL counters explicitly. |
| `sym["name"]` | Replaced by `@name` (assembler-familiar, one token). |
| `on interrupt` measured "from INT assertion" | jnext has no assertion-time hook; it has the acceptance seam (`cpu_.on_int_ack`, `emulator.cpp:1114`). `on interrupt` means *accepted*. |
| `on magic_break` | The magic breakpoint already pauses (`emulator.cpp:7880`); a script wanting to act there uses `on execute` at the address or `on stop`. Not added. |

---

## 2. The language

File extension `.jds` (jnext debugger script). UTF-8, line-oriented.
**Comments**: `;` to end of line and `//` to end of line (owner requirement),
plus `#` to end of line (kept; it conflicts with nothing — `#` is not an
operator and `$`/`0x` are the hex prefixes). `//` cannot be confused with
division: there is no unary `/`, so `/` `/` never occurs in a valid
expression. Inside a string literal none of the three starts a comment. Case-sensitive keywords in lower case; built-in state names in upper
case so they cannot collide with MAP symbols (`@name`) or user variables.

### 2.1 Grammar

```
script      ::= { toplevel }
toplevel    ::= var_decl | rule | NEWLINE
var_decl    ::= "var" IDENT "=" expr
rule        ::= [ [ "disabled" ] IDENT ":" ] "on" event [ "once" ] [ "when" expr ] "do" { action } "end"

event       ::= "execute"  addr_spec
              | "read"     addr_spec
              | "write"    addr_spec
              | "io_read"  port_spec
              | "io_write" port_spec
              | "nextreg"  reg_spec                # NextREG write (CPU or Copper)
              | "frame"    [ expr ]                # every frame, or FRAME == expr
              | "scanline" expr                    # CVC == expr (see 2.4)
              | "cycle"    expr                    # first instruction boundary with CYCLE >= expr
              | "interrupt" | "nmi" | "reset"
              | "hostkey"  INT                     # 1..8
              | "stop"                             # any pause, whatever caused it
              | "copper" ( "move" [ reg_spec ] | "wait" | "halt" ) [ "at" expr [ ".." expr ] ]
                                                   #   copper-side view; `at` = copper PC range (0..1023)
              | "dma" ( "start" | "byte" [ addr_spec ] | "end" )
                                                   #   `byte` filter = destination address range;
                                                   #   `end` here is a sub-kind, not the block terminator —
                                                   #   the parser special-cases the token after `dma`
                                                   #   (script_parse_test row PARSE-DMA-END)

addr_spec   ::= expr [ ".." expr ] [ "page" expr ]   # inclusive 16-bit logical range, optionally
              | "page" expr [ ".." expr ]          #   qualified by / replaced with a PHYSICAL 8K page
                                                   #   (set) — the backend's first-class page filter
port_spec   ::= expr [ ".." expr ]                 # GH #222 semantics: 0x00xx = low-byte decode,
              | "mask" expr "value" expr           #   else exact; or explicit mask/value. A range
                                                   #   decodes like a single port, so it lies wholly
                                                   #   in 0x00..0xFF or wholly above it: a range across
                                                   #   0xFF is a load error (Appendix I.2)
reg_spec    ::= expr [ ".." expr ]                 # NextREG number(s)

action      ::= "log" [ "indent" expr ] string
              | "stop" [ string ]
              | "assert" expr string
              | "exit" expr
              | "dump_regs" | "dump_mmu" | "dump_mem" expr expr
              | "snap" IDENT | "unsnap" IDENT | "dump_diff" IDENT
              | "enable" IDENT | "disable" IDENT
              | "screenshot" string                # .png (framebuffer) or .scr (ULA memory)
              | "save_snapshot" string             # .jns/.szx/.sna/.nex by extension
              | "compare_scr" string string        # file, message: assert ULA memory == file
              | "press" key_spec                   # LEVEL: key down until `release` (CAP-IN-02)
              | "press" key_spec "for" expr        # PULSE: down n frames, then up (CAP-IN-01)
              | "release" key_spec                 # LEVEL: key up
              | "joystick" INT expr                # port index (1|2), 12-bit MD6 mask
              | "set" lvalue "=" expr              # var assignment OR machine MUTATION (2.7)
              | "out" expr expr                    # port write (mutation)
              | "if" expr "then" { action } [ "else" { action } ] "end"

lvalue      ::= IDENT                              # a `var`
              | "mem[" expr "]" | "mem16[" expr "]" | "phys[" expr "," expr "]"
              | "nextreg[" expr "]"
              | REG | FLAG | "IFF1" | "IFF2" | "IM" | "AUDIO_MUTE"
                                                   # REG = A B C D E H L F I R AF BC DE HL IX IY SP PC AF2 BC2 DE2 HL2
                                                   # FLAG = CF ZF SF PF HF NF

expr        ::= literal | IDENT | builtin | "@" IDENT | "(" expr ")"   # builtin = the upper-case names of 2.3
              | "mem[" expr "]" | "mem16[" expr "]"
              | "phys[" expr "," expr "]"          # (8K page, offset 0..0x1FFF)
              | "nextreg[" expr "]" | "mmu[" expr "]" | "page[" expr "]" | "stack[" expr "]"
              | IDENT "." field                    # snapshot field, see 2.5
              | "changed(" IDENT "," group ")"     # snapshot comparison
              | "depth(" IDENT ")"
              | unop expr | expr binop expr
literal     ::= INT (dec, 0x hex, $ hex, 0b bin) | "true" | "false" | string
              | "CPU" | "DMA" | "COPPER"           # the SOURCE constants (integers 0, 1, 2)
string      ::= '"' { char | "${" expr [ ":" fmt ] "}" } '"'      # fmt: x2 x4 d
key_spec    ::= string                             # the --delayed-keypress vocabulary, plus
                                                   # "row,col" (one matrix bit, e.g. "0,0" CAPS
                                                   # SHIFT, "7,1" SYMBOL SHIFT) and "ext:<name>",
                                                   # a Next extended key (WP6, Appendix L.2):
                                                   # right left down up dot comma quote semicolon
                                                   # extend capslock graph truevideo invvideo
                                                   # break edit delete; any case
```

Operators, by precedence (low to high): `or`; `and`; `not`; `== != < > <= >=`;
`| ^`; `&`; `<< >>`; `+ -`; `* / %`; unary `- ~`. Integers are 32-bit signed
and wrap; division by zero is a script error (§6.5).

### 2.2 Rules and firing

- A rule is **registered with the backend at load time** as one event
  subscription: event kind, address/port/register range, the compiled `when`
  predicate, and the `once` flag. The interpreter body runs only when the
  backend has already accepted the hit (§5.2).
- `once` disables the rule after its first accepted firing. `enable NAME`
  re-arms it (and resets `once`).
- A rule with a label (`NAME:`) can be `enable`d / `disable`d. Disabled rules
  cost nothing on the hot path (the backend removes the subscription from its
  live set, the `BreakpointSet` live-cache pattern, `breakpoints.h:52-62`).
- Rules with no label start enabled. A labelled rule can start disabled with
  `disabled NAME:` — the #279(e) case, where boot and loading must not trip the
  guards until the user arms them.
- **Delivery point** (backend.md §4.3, adopted): memory, port and NextREG
  events raised *during* an instruction are latched at the site and delivered
  at the **end of that instruction**, with the machine stopped there; execute,
  cycle and host events are delivered *before* the instruction at `PC`; frame
  events at the frame edge; scanline events at the first instruction boundary
  after the line began (≤ 1 instruction late — the payload carries the exact
  cycle). **NextREG writes from the CPU are the same ≤ 1-instruction case**
  (backend v4): they commit in `flush_pending_cpu_nr_writes()`
  (`emulator.cpp:10221`), inside `tick_devices_after_instruction` (`:10193`),
  which runs AFTER the boundary drain, so the event is delivered at the NEXT
  instruction's boundary; `PC`, `CYCLE` and the raster counters are captured
  in the latch at the hook, so the payload still names the writer exactly, and
  a `stop` lands one instruction after it. Copper and DMA NextREG writes are
  delivered at the current instruction's boundary. Scripts and rows therefore
  assert on the payload `PC`, never on the paused machine's PC. No rule body ever runs inside `Mmu::write`, `NextReg::write` or
  `PortDispatch`; that is what makes non-perturbation a property of the design.
- Firing order: events of one instruction in access order (the backend's
  latch ring — **512** entries since backend v7, derived from the Copper's
  per-master-cycle cadence: a 21-T `LDIR` iteration at 3.5 MHz is 168 Copper
  cycles, ~45 contended T × divisor 8 ≈ 360 Copper latches + 50 DMA + CPU;
  a Copper MOVE is ONE entry, fanned out at the drain to both `copper move`
  and `nextreg` with `SOURCE == COPPER`); within one event, rule order in the file, then across
  files in `--script` order. A `stop` does not prevent later rules on the same
  event from running (they may want to log); it takes effect after all of them.
  Overflow is a SPECIFIED, tested behaviour (backend v7): the first N entries
  are kept in order and the boundary's deliveries carry `overflowed{dropped}`;
  the run logs `SCRIPT: event ring overflowed at CYCLE …, <dropped> events
  dropped` once per occurrence — a script cannot see the dropped events, and
  silence would be a lie.
- Actions run to completion synchronously; there is no yielding.

### 2.3 Values and state access (read-only)

All names below are read from the backend's inspection surface under its
non-perturbing discipline (the `DebugState::InspectionScope` rule,
`debug_state.h:126-140`: a script read can never fire a watchpoint or a
destructive NextREG read handler; `NextReg::peek`, `nextreg.h:53-56`).

| Name | Meaning | Source |
|---|---|---|
| `A B C D E H L F I R`, `AF BC DE HL IX IY SP PC`, `AF2 BC2 DE2 HL2` | CPU registers | backend registers |
| `CF ZF SF PF HF NF` | flag bits | from `F` |
| `IFF1 IFF2 IM HALTED` | interrupt state | backend registers |
| `mem[a]`, `mem16[a]` | CPU-view byte / LE word | backend peek (logical) |
| `phys[page, off]` | byte in physical 8K page | backend peek (physical) |
| `stack[n]` | LE word at `SP + 2n` | derived from `mem16` |
| `nextreg[r]` | NextREG value, side-effect-free | `NextReg::peek` |
| `mmu[s]` | NR 0x50+s as the register reads (0xFF for ROM) | `Mmu::nr_mmu_` via backend |
| `page[s]` | effective physical 8K page in slot s | `Mmu::get_effective_page`, `mmu.h:74-77` |
| `FRAME` | frame counter (backend-owned, REQ-dsl-16) | backend |
| `CYCLE` | master 28 MHz cycle since power-on | `Clock` |
| `TFRAME` | master cycles since frame start | `Emulator::current_frame_cycle`, `emulator.h:742`. (`DEBUG-SUBSYSTEM-ARCHITECTURE.md` §7.5 spells this `TSTATES`; this file is the grammar of record and the architecture doc is to be aligned to `TFRAME`) |
| `RAW_HC RAW_VC HC_ULA VC_ULA CVC PHC` | the VHDL raster counters, exactly as `RasterState` names them | `raster_state.h:97-104` |
| `@name` | address of MAP symbol; load error if absent (§6.5) | `SymbolTable::lookup_name`, `symbol_table.h:21` |
| `AUDIO_MUTE` | host-side mute mask (the Audio panel's), readable and settable |
| `MACHINE` | 0=48K 1=128K 2=+3 3=Pentagon 4=Next | backend (REQ-dsl-17: not in backend.md v1's CAP-INS-07/TIME-01) |

Event payload, valid only inside the matching rule body:

| Name | Events | Meaning |
|---|---|---|
| `ADDR` | read/write/execute | logical address hit |
| `VALUE` | read/write/io_*/nextreg | byte read or written (nextreg: the value written; the register is already committed at delivery, so `nextreg[REG] == VALUE`) |
| `PREV` | nextreg | the register's value before the write (backend payload `prev`, peeked at the hook) |
| `PAGE` | read/write/execute | physical 8K page behind `ADDR` at the time of the access — `Mmu::get_effective_page(ADDR >> 13)`. On `execute` it is derived at delivery as `page[PC >> 13]` (the backend's Execute payload carries `pc` only). **Overlay caveat**: `Mmu::write` latches the watch before the Multiface / DivMMC / Layer 2 overlays (`mmu.h:418-450`), so a write that an overlay then captures is seen (good for 3(a)) but `PAGE` is the MMU slot's page, not the overlay's target |
| `PORT` | io_read/io_write | full 16-bit port |
| `REG` | nextreg | register number |
| `SOURCE` | read/write/nextreg, io_read/io_write | one of the constants `CPU` (0), `DMA` (1), `COPPER` (2), declared in the grammar. A DMA byte whose destination is a port raises `io_write` with `SOURCE == DMA` (REQ-dsl-27 ACCEPTED) |
| `PC` | all | **pre-execution PC of the instruction that caused the event** (REQ-dsl-12), not the CPU's live PC |
| `KEY` | hostkey | 1..8 |
| `CPC`, `HC_ULA`, `CVC` | copper move/wait/halt | Copper program counter (0..1023) and the raster position of the Copper step; `REG`/`VALUE` on `move`; `WAIT_V`, `WAIT_H` (the WAIT's vpos and hpos threshold) on `wait`. **Inside a `copper` rule `HC_ULA`/`CVC` are the payload values (the step's position) and shadow the live state names** — delivery is ≤1 instruction late, so the two can differ; use `RAW_HC`/`RAW_VC` for the live beam if needed |
| `PREV` | write | the byte before the write (`Mem{Write}.prev`, REQ-dsl-25 ACCEPTED) |
| `SRC`, `DST`, `LEN`, `DMA_MODE`, `IO_SRC`, `IO_DST` | dma start/byte/end | transfer parameters; on `byte`: `SRC`/`DST` are the addresses of that byte, `VALUE` the byte, `IO_SRC`/`IO_DST` whether each side is a port; on `end`: `LEN` = bytes moved |
| `REASON` | stop | why the machine paused (string) |

### 2.4 Time and raster semantics — which counter is which

`raster_state.h:13-48` is the authority and its warning is adopted: the four
vertical counters have different origins and confusing them is the failure mode
GH #16 and GH #181 both were. The DSL therefore never offers a bare `VC`.

- `on scanline N` compares **`CVC`**, the VHDL `o_vc_cu` — the value NR
  0x1E/0x1F return and the value the line interrupt (NR 0x22/0x23) and Copper
  `WAIT` compare against (`raster_state.h:36-39`). That is the number a Next
  programmer already reasons in. It fires once per frame at the start of that
  line's CPU slot (the `on_scanline` seam, `emulator.cpp:11630`, mapped by the
  backend from the raw line).
- `on frame N` fires at the frame boundary (`Emulator::end_of_frame`,
  `emulator.cpp:9419`) when the backend's frame counter equals N; `on frame`
  fires every frame. `FRAME` is the backend's pre-increment frame tag
  (`time().frame`, the number the rewind slot carries — review N3-1), so
  `FRAME == N` holds throughout the N-th `run_frame()` since load, which is
  what the `--delayed-keypress-frames N` equivalence in §2.6 needs. Frame 0 is the first frame executed after the script is
  loaded, which for a CLI-loaded script is power-on — the same origin
  `--delayed-keypress-frames` uses (§7.3).
- `on cycle N` fires at the first instruction boundary at which `CYCLE >= N`,
  once. Sub-instruction precision does not exist in a per-instruction core; a
  script that needs it is at the wall (§4).

### 2.5 Snapshots — span invariants

The #279(c) case ("at exit, compare against entry") needs a value captured at
one event and compared at another, with nesting. A snapshot is a fixed record,
kept on a named **stack** so nested entries pair with their exits:

```
snap NAME          # push {regs, IFF1, IFF2, IM, SP, stack[0], mmu[0..7], FRAME, CYCLE, PC}
unsnap NAME        # pop
NAME.A  NAME.HL  NAME.SP  NAME.IFF1  NAME.STACK0  NAME.MMU[3]  NAME.PC  NAME.FRAME ...
changed(NAME, regs)   # any of AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP differ from top
changed(NAME, mmu)    # any of the 8 slots differ
changed(NAME, iff1)   # IFF1 differs
changed(NAME, stack0) # word at SP differs from the captured one
depth(NAME)           # stack depth, for indented traces
dump_diff NAME        # log every field that differs, one line each
```

Reading a field of an empty stack is a script error (§6.5). `changed` and
`dump_diff` compare the current machine against the **top** entry. This is the
whole of ChaseTheBug's `FunctionStackEntry` + `EqualRegs` (its `.cs:60-70`,
`:426-441`) as language features rather than as user code — which is what makes
the nesting expressible without loops or data structures.

### 2.6 Actions

| Action | Semantics |
|---|---|
| `log [indent n] "…"` | one line to the script log, prefixed `[jds F:<FRAME> C:<CYCLE>]`, `n` spaces after the prefix. Headless: stderr through the `script` spdlog channel. |
| `stop ["reason"]` | request a pause **at the offending instruction** (§6.3). Headless: log + exit 3 at the end of the current instruction. |
| `assert expr "msg"` | if false: `log "ASSERT FAILED: msg"`, then behaves as `stop "msg"`. |
| `exit n` | headless: exit with code n after the current instruction. GUI: log + pause (a GUI never exits from a script). **A failure at the same boundary wins over `exit 0`** (WP7 review 1, Appendix M.2): a `stop`, a failed `assert` or `compare_scr`, a static stop, or a run-time error at the same event — in this rule or another, before or after the `exit` — makes the run exit 3 (1 for the run-time error), logged `SCRIPT EXIT 0 not taken`; a non-zero `exit n` is kept. An `exit` issued while a `compare_scr` waits for its frame edge waits too, and is taken at that edge after the compare. An `exit` inside an `on stop` rule never changes the status: the pause that ran the rule already decided it (a stop's 3, an `exit`'s own code — row SCRIPT-EV-EXIT-IN-ON-STOP). |
| `dump_regs`, `dump_mmu`, `dump_mem a len` | to the log; `dump_mem` ≤ 4096 bytes, 16 per line. |
| `screenshot "f"` | queued for the **next frame boundary** through `save_screenshot` (`screenshot.h:60`): `.scr` = ULA memory (`Ula::screen_dump`), else PNG. Same path `--delayed-screenshot` uses. Its outcome is the backend's `flush_captures(cid)` (added in B4): the engine calls it before `exit`, and `NoFrame` (a capture still pending) or `RefusedUnavailable` (one that failed to write) makes the run's exit non-zero. |
| `compare_scr "f" "msg"` | at the next frame boundary, `Ula::screen_dump()` byte-compared to file; first differing offset logged; mismatch behaves as `assert` failure. |
| `save_snapshot "f"` | queued for the next frame boundary through the existing savers (the GH #27 `--delayed-snapshot` route). |
| `press "KEY"` | **level**: KEY goes down at the next frame edge and stays down until `release` (backend CAP-IN-02 `set_key`, `Keyboard::set_matrix_bit`, `keyboard.h:185`). Vocabulary of `--delayed-keypress`, plus `row,col` for one matrix bit and `ext:<name>` for one of the 16 Next extended keys (NR 0xB0/0xB1, IN-02 `set_extended_key`; the names are in §2.1's `key_spec`, WP6 / Appendix L.2). This is what the recorder emits (§7.2). |
| `release "KEY"` | **level**: KEY goes up at the next frame edge, `row,col` and `ext:<name>` included. Releasing a key that is not down is a no-op. |
| `press "KEY" for n` | **pulse**: down for n frames then up, with the auto-type 4-frame all-released gap after it (backend CAP-IN-01 over `Keyboard::queue_auto_type`, `keyboard.cpp:541-590`). Today `queue_auto_type` REPLACES the queue, so a second pulse while one is in flight strands the first key down (review R-1); REQ-dsl-18 is ACCEPTED as **append**: CAP-IN-01 queues behind an in-flight pulse (4-frame released gap kept), two pulses due in one frame both happen, and `--delayed-keypress-frames` inherits the fix. `set_matrix_bit` (`keyboard.h:185`, private today) gains a public entry for CAP-IN-02. `--delayed-keypress-frames N KEY` ≡ `on frame N do press "KEY" for 5 end`. An `ext:<name>` key has no pulse form: `press "ext:…" for n` is a run-time error (WP6). |
| `joystick n bits` | set MD6 12-bit state of port n (1\|2) at the next frame boundary (`Joystick::set_joy_left/right`, `joystick.h:116-117`). |
| `enable NAME`, `disable NAME` | arm / disarm a labelled rule. |
| `set v = expr` | assign a `var`. |
| `set <lvalue> = expr` (machine lvalue) | **mutation** (§2.7): write a register / flag / IFF / IM, a byte or word in the CPU view, a byte in a physical page, a NextREG, or the audio mute mask. Applied at the delivery point, logged. |
| `out port value` | **mutation**: write a port through the debugger route (CAP-INS-05). Logged. |

Everything queued "for the next frame edge" is applied by the backend at
`end_of_frame`, in script order, before the next frame's first instruction —
the one place a frame-granular input can be injected without perturbing the
instruction stream mid-frame. That is also why these actions are legal from any
event: a `press` issued from an `on write` handler lands at the same instant a
`press` from `on frame` does.

**Which edge, exactly** (reviews R-1, R2-1). Frame N's edge E_N is the
`end_of_frame` call that closes frame N (`emulator.cpp:9419`, inside
`run_frame(N)`). `on frame N` fires *at* E_N, and an injection issued by any
rule during frame N — including one fired at E_N itself — is applied at E_N,
**before** `end_of_frame`'s own `keyboard_.tick_auto_type()` call
(`emulator.cpp:9592`; REQ-dsl-20, a CAP-IN ordering contract), so:

- a level `press` issued during frame N is visible to every port read of frame
  N+1 and to none of frame N;
- a pulse `press … for n` issued during frame N is pressed by that same
  `tick_auto_type()` at E_N and is likewise visible from N+1 — the frame
  `--delayed-keypress-frames N` lands on today (`headless_app.cpp:560-561`
  queues before `run_frame(N)`), which is what makes the equivalence in the
  table above true. The other order would shift every `--delayed-keypress-frames`
  regression row by one frame.

**How the recorder must stamp** (R2-1). GUI input does not change mid-frame:
Qt/SDL key events reach `Keyboard::set_key` through `host_key_latch::Router`
(`host_key_latch.h:431,445`; `qt_app.cpp:268-280`) between `run_frame()`
calls — a press at once, a release held until a frame has run — so every
change lands after E_N and before frame N+1's first instruction, and the key
is down for all of frame N+1. The recorder samples `input_state()` at
`begin_new_frame(K)` (`emulator.cpp:8450`) and stamps a change first seen
there as frame **K−1**: replay then issues `on frame K−1 do press`, applied at
E_{K−1}, visible from K — exactly as recorded. Stamping K (the naive "same
edge" rule of v2) would be one frame late for every edge. This is exact
because of the latch; an injector that changed the matrix mid-frame would be
recorded to the following edge, which is §4's frame-granularity wall.

### 2.7 Mutation — explicit, logged, deterministic (owner decision 2026-09-27)

The owner reversed the v1-v3 reading of "must not perturb": **observation** is
side-effect-free by contract (every read in §2.3 goes through a peek path),
and **mutation** is allowed when it is explicit. The rule is therefore no
longer "no writes" but three properties every write has:

1. **Explicit.** A write is a `set` on a machine lvalue or an `out`; nothing
   else in the language writes. A reader of a script can grep for them.
2. **Logged.** Every mutation is logged by the *backend* (REQ-dsl-21(d)), as
   `MUTATE <what> <old> -> <new> by script:<rule>` — so a script's writes and a
   DZRP client's writes appear in the same log, and a CI row can grep for
   `MUTATE` to forbid them.
3. **Applied at the delivery point**, with the machine stopped at an
   instruction boundary, through the debugger write paths (CAP-INS-01
   `set_register`, -02 `poke`, -04 `nextreg_write`, -05 `port_out`, -10
   `set_audio_mute_mask`) — the same paths the Qt panels use today. That is
   what keeps a mutating script deterministic: the same run fires the same
   events at the same boundaries and applies the same writes.

**What a mutation means for the rest of the instruction / frame:**

| Delivery point of the rule | A `set`/`out` in its body is seen by |
|---|---|
| `execute`, `cycle`, `hostkey` (pre-instruction) | the instruction at `PC` itself — so `set PC = @skip` redirects before anything runs, `set A = 0xFF` is the value the instruction reads |
| `write`/`read`/`io_*`/`nextreg`/`copper`/`dma`/`interrupt`/`nmi` (post-instruction boundary) | the **next** instruction; the instruction that raised the event has completed with its own memory/port effects (a `set mem[ADDR] = PREV` after a caught write "undoes" it before anyone reads it, but the write did happen and the event was raised). **Exception**: the raising instruction's *deferred CPU NextREG write* has NOT committed yet — `flush_pending_cpu_nr_writes()` runs in `tick_devices_after_instruction` (`emulator.cpp:10221`), after the drain — so `on io_write 0x253B do set nextreg[r] = … end` (or a `Mem`/`Port` rule from the same instruction) is silently overwritten by the guest write it tried to override. To override a NextREG write, use `on nextreg`, which is delivered after the flush. |
| `frame`, `scanline` (frame edge / line boundary) | the next instruction; a frame-edge `set` lands before the injection queue and before `tick_auto_type()` |

Mutations through the debugger route **raise no events and carry no
`SOURCE`** (REQ-dsl-21(c)): a script `set mem[x]` does not fire `on write x`,
a `set nextreg[0x51]` does not fire `on nextreg 0x51`, an `out` does not fire
`on io_write` — no re-entrancy, by construction. A `poke` into 0x0000-0x3FFF
goes where the Memory panel's write goes today (overlay routing per
REQ-dsl-21(c)'s answer).

**Why a script write raises no event — the mechanism, stated once** (review
N5-1): a rule body executes *at a delivery*, i.e. inside `run_frame()`'s
`GuestExecutionScope`, where `wp_live_ = armed_ && guest_access_` is TRUE
(`debug_state.h:275`) — so site gating alone would let a script `set mem[x]`
latch a watch. The engine therefore runs the **whole rule body — reads and
mutations alike — under one `DebugState::InspectionScope`**
(`debug_state.h:104-115`), which drops `guest_access_` and with it
`wp_live_`; the same `guest_access()` gate is what backend B2 adds to the
`NextReg::write` hook. That scope is what §2.3's side-effect-free reads
already rested on; it now carries the mutations too, and the
SCRIPT-EV-MUT-NOEVENT row (§8) pins that an implementation which drops it
turns red.

**Worked examples** (these are the CSpect `Poke`/`SetRegs`/`OutPort` idioms):

```
# patch a byte at a symbol, once, as soon as the program has loaded it
on execute @main once do
    set mem[@debounce_wait] = 0xC9          ; RET — disable the debounce delay
    log "patched debounce_wait"
end

# force a register to reproduce a bug: make the sprite index wrap on entry
on execute @draw_sprite when B == 63 do
    set B = 64
    log "forced B=64 at draw_sprite (frame ${FRAME})"
end

# skip an instruction (3 bytes) that traps in the emulator under test
on execute @bad_out do
    set PC = PC + 3
end

# fault injection: corrupt the byte a DMA just wrote, to exercise the checker
on dma byte 0x8000..0x9FFF once when VALUE == 0x55 do   ; `once` before `when`, as the grammar says
    set mem[DST] = 0xAA
end

# provoke the MMU-inconsistency guard of 3(b) on purpose (a red twin without a rebuild).
# RULE: an injected fault must be UPSTREAM of the guest write the guard watches,
# never the watched write itself — a script `set nextreg[0x51]` raises no event
# (§2.7), so it can never trip `on nextreg 0x51`. Corrupt MMU0 instead — but
# AFTER the guest's own MMU0 write has committed and BEFORE its MMU1 write:
#   @page_in_level:        NEXTREG 0x50, 0x22   ; deferred; commits in this
#                                               ; instruction's device cluster
#   @page_in_level_mmu1:   NEXTREG 0x51, 0x23   ; the watched write
# Hooking @page_in_level would be overwritten: that Execute fires BEFORE its
# instruction, the script sets MMU0 = 0xFF, then the guest's NEXTREG 0x50, 0x22
# commits (emulator.cpp:10221) and the guard sees 0x22 + 1 == 0x23 — green.
# Hooking @page_in_level_mmu1 (a MAP label the WP7 demo exports) fires after
# the previous instruction's cluster committed 0x22, sets MMU0 = 0xFF, and the
# guest's NEXTREG 0x51, 0x23 then trips the guard: 0xFF + 1 != 0x23 and not both 0xFF.
on execute @page_in_level_mmu1 once do
    set nextreg[0x50] = 0xFF               ; MMU0 := ROM one instruction before the guest writes MMU1
end

# silence the AY while a DAC test runs
on frame 0 once do set AUDIO_MUTE = 0b00111 end
```

The last example shows the property that matters most for testing: a red
twin no longer needs a second build of the demo — a script can inject the
fault into the good build. §8's fixture rows use both forms, because a
script-injected fault proves the script, not the demo.

**Three more backend rules a mutating script meets** (backend v7 §4.2a): a
mutation is refused with `RefusedRzx` while an RZX is recording or playing
(the recording would no longer reproduce); the event gate and drain are off
in `replay_mode_`, so no rule fires during a rewind replay; and an `execute`
handler at A that sets `PC = B` bypasses a breakpoint at B for that one
instruction (the step-off arm of GH #221 applies to the redirected PC) — a
script that wants to stop at B after redirecting there says `stop` itself.

**Interaction with rewind** (§4 wall; backend v7 §4.2a): a mutation is inside
the machine state the next frame snapshot captures, but interpreter state
(`once` flags, `var`s, snapshot stacks) is not, and the backend does **not**
re-fire subscriptions during replay. So the backend records each mutation's
cycle and **refuses** `step_back` / `rewind_to_cycle` into a mutated span
(`RefusedUnavailable`; a frame-boundary target is still legal). The engine
additionally logs a warning when a script mutates while `--rewind-buffer-size`
is active.

---

## 3. Worked scripts — the acceptance workload

The five #279 cases are ChaseTheBug (`ChaseTheBugPlugin.cs`, read in full;
`ChaseTheBug.cfg`) expressed in this language. The MAP is loaded with
`--map next-point.map` (§6.6).

### 3(a) Write to a MAP-delimited code range — stop, with the writing PC

ChaseTheBug watches `0x0000..0x3FFF` (MMU0/1: ROM and banked code) and
`0x8000..__data_crt_head` exclusive (`.cs:112-118`), logs `port`/`pc` and
enters the debugger (`.cs:203-204`).

```
# guard.jds — code-area write protection (ChaseTheBug ranges)
var armed = 0

disabled rom_guard: on write 0x0000..0x3FFF do
    log "write to code area ${ADDR:x4} (page ${PAGE:x2}) <- ${VALUE:x2} from PC ${PC:x4} src ${SOURCE}"
    stop "write into MMU0/1 code area"
end

disabled main_guard: on write 0x8000..(@__data_crt_head - 1) do
    log "write to main code ${ADDR:x4} <- ${VALUE:x2} from PC ${PC:x4}"
    stop "write into main code area"
end
```

The two ranges are two backend subscriptions; the DSL never sees a write
outside them. When code is banked, "a write to bank N" is the real invariant
(#279 item 2), and it is a **physical-page filter**, not a predicate: `on write
page 0x22` (or `on write 0xC000..0xFFFF page 0x22` to also require the logical
window) maps onto the backend's first-class page filter (backend.md §4.3 —
"physical page ∈ set", matched at the MMU site whether the page sits at 0x8000
or 0xC000, and tracked by its slot mask when the MMU remaps). The same
qualifier exists on `execute` (`on execute @sym page 0x22`, the backend's
Execute `page` qualifier). A predicate `when PAGE == 0x22` on a full logical
range would instead mark all eight slots in the bitmap and drain every guest
write through the latch — legal, but exactly the hot-path cost §0 says the
DSL must not cause; the parser warns on a `when PAGE ==` over a range wider
than one slot.

### 3(b) NextREG 0x51 inconsistent with MMU0

`.cs:181-193`: on a write to NR 0x51 read NR 0x50; break unless
`mmu0 + 1 == mmu1` or both `0xFF`.

```
disabled mmu_guard: on nextreg 0x51 when not ((nextreg[0x50] + 1 == VALUE) or (nextreg[0x50] == 0xFF and VALUE == 0xFF)) do
    log "MMU0 is ${nextreg[0x50]:x2} whereas MMU1 write is ${VALUE:x2} (src ${SOURCE}, PC ${PC:x4})"
    stop "MMU1 inconsistent with MMU0"
end

on nextreg 0x51 when armed == 1 do
    log "MMU1 write ${VALUE:x2}"
end
```

`VALUE` is the value written to NR 0x51, `nextreg[0x50]` the current MMU0 — a
different register, untouched by this write, so delivery after commit
(backend.md §4.3) changes nothing here; `PREV` gives the old 0x51 if a script
wants to log the transition. The ≤1-instruction-late delivery of CPU writes
(§2.2) preserves ChaseTheBug's semantics in both write orders: for
`NEXTREG 0x50 ; NEXTREG 0x51` the 0x50 commit precedes the 0x51 event's
delivery, so `nextreg[0x50]` is the new MMU0; for `NEXTREG 0x51 ; NEXTREG
0x50` the 0x51 event is delivered at the boundary before the 0x50 commit, so
the script sees the old MMU0 — exactly what the plugin, evaluated at the 0x51
write (`.cs:181-193`), sees. `SOURCE` distinguishes a Copper `MOVE` (`copper.cpp:209`) from the CPU
(`port_dispatch.h` `nextreg_opcode_write_cb` and the 0x253B route both end in
`NextReg::write`, `nextreg.cpp:456`).

### 3(c) Interrupt-handler entry/exit invariants with an indented call trace

`.cs:219-296`: entry pushes {regs, top of stack, 8 MMU regs}; exit breaks if
registers differ, top-of-stack differs, IFF1 is off, or an MMU slot changed;
both log with `depth*2` spaces. Config lines `Int=isr_entry,isr_exit` and
`Fn=f,f_exit1,f_exit2` (`.cs:355-381`).

```
# isr.jds
on execute @isr do
    snap isr
    log indent (depth(isr) * 2) "==> isr at ${PC:x4}"
end

on execute @isr_exit do
    log indent (depth(isr) * 2) "<== isr at ${PC:x4}, Ret=${stack[0]:x4}"
    if depth(isr) == 0 then
        log "Warning: isr exit with empty entry stack"
    else
        if changed(isr, regs) then
            log "Warning: registers differ on exit from isr"
            dump_diff isr
            stop "isr clobbered registers"
        end
        if changed(isr, stack0) then
            log "Warning: top of stack modified ${isr.STACK0:x4} vs ${stack[0]:x4}"
            stop "isr modified return address"
        end
        if not IFF1 then
            stop "isr exit with interrupts disabled"
        end
        if changed(isr, mmu) then
            dump_diff isr
            stop "isr changed an MMU slot"
        end
        unsnap isr
    end
end
```

The plugin's "exit does not match top of stack" check across *different*
functions (`.cs:230-234`) is approximated with one named stack per function,
where a mismatch is `depth(f) == 0` at `f`'s exit; this does **not** catch
non-LIFO interleaving (f exits while g is on top), which the plugin's single
stack does — recorded as a wall in §4. Its "new maximum nesting level" report is a
`var maxdepth` compared against `depth(isr)`. Entry keyed to the hardware
rather than a symbol is `on interrupt` (accepted-INT seam) — the entry PC is
then `PC` and the return address `stack[0]` after acceptance.

### 3(d) `MemPoint=addr,value`

`.cs:190-201`: break only when a specific value is written to a specific
address.

```
mempoint: on write 0x2222 when VALUE == 0xB7 do
    log "MemPoint hit at ${ADDR:x4}: forbidden value ${VALUE:x2} from PC ${PC:x4}"
    stop "MemPoint"
end
```

One address, one predicate; the backend evaluates `VALUE == 0xB7` and the
interpreter is entered only on the hit.

### 3(e) Arm / disarm from host hotkeys

`.cs:150-151, 298-304`: Ctrl+G disables, Ctrl+H enables (`startWatching`).

```
on hostkey 1 do          # Alt+1 in the GUI, --script-key F 1 headless
    enable rom_guard
    enable main_guard
    enable mmu_guard
    enable mempoint
    set armed = 1
    log "MemWatch enabled"
end

on hostkey 2 do
    disable rom_guard
    disable main_guard
    disable mmu_guard
    disable mempoint
    set armed = 0
    log "MemWatch disabled"
end
```

`enable`/`disable` is preferred over `when armed == 1` because a disabled rule
leaves the backend's live set (no hot-path cost); the `armed` variable is kept
only for rules that must stay live but log conditionally.

### 3(f) The #26 originals

```
# palette_init.jds — CI assertion, exit code is the verdict
on execute @palette_init_done once do
    assert mem[0x9000] == 0xAA "sentinel missing in palette buffer"
    assert A == 0 "A must be 0 after palette init"
    log "PASS palette init"
    exit 0
end
on frame 300 do
    log "FAIL: palette_init_done never reached"
    exit 1
end

# sprite_y.jds — value-conditional port watch, reproducer for a raster bug
on io_write 0x57 when VALUE >= 192 do
    log "sprite attr write ${VALUE:x2} at CYCLE ${CYCLE} frame ${FRAME} cvc ${CVC}"
    dump_regs
    dump_mem 0x5C00 64
    stop "sprite Y >= 192"
end

# latency.jds — interrupt acceptance to handler entry, in master cycles
var t_int = 0
on interrupt do
    set t_int = CYCLE
end
on execute 0x0038 do
    log "IM1 handler after ${CYCLE - t_int} master cycles (cvc ${CVC}, hc_ula ${HC_ULA})"
end

# copper.jds — Copper-side view: where along the frame did the palette flip land?
on copper move 0x43 do
    log "Copper MOVE NR43=${VALUE:x2} at copper PC ${CPC} on cvc ${CVC} hc_ula ${HC_ULA}"
end
on copper wait when WAIT_V == 95 do
    log "WAIT(95,${WAIT_H}) satisfied at cvc ${CVC} hc_ula ${HC_ULA}"
    assert CVC == 95 and HC_ULA >= WAIT_H "WAIT for line 95 is satisfied on the Copper's own line 95, at or past its threshold (GH #181)"
end
on copper halt once do log "copper HALT at ${CPC}" end

# dma.jds — DMA-side view: a transfer must stay inside the sprite pattern upload window
on dma start do
    log "DMA ${SRC:x4} -> ${DST:x4} len ${LEN} mode ${DMA_MODE} (io dst ${IO_DST})"
end
on dma byte when not IO_DST and (DST < 0x4000) do
    stop "DMA wrote into ROM/banked code at ${DST:x4} from ${SRC:x4}"
end
on dma end do assert LEN == 256 "sprite upload must move exactly 256 bytes" end

# line.jds — raster position assertions
on scanline 95 do
    assert nextreg[0x43] & 0x70 == 0x10 "palette select wrong at line 95"
end
on cycle 1000000 once do
    screenshot "/tmp/at-1M.png"
end
```

---

## 4. Where expressiveness runs out

Stated plainly, as the owner asked. Each is a real thing a CSpect plugin can do
and this language cannot; the last column says whether a small addition closes
it or whether it is the kind of case that should reopen the plugin decision.

| Wall | Why it is a wall | Cheapest escape, if any |
|---|---|---|
| **No loops, no user functions.** A script cannot scan a memory block, checksum a bank every frame, or walk a linked list in guest RAM. | Deliberate: a loop inside a per-write callback is the hot-path risk #26 names. The acceptance workload never iterates except over the 8 MMU slots and the register set, which `changed()` covers. | `crc32(a..b)` builtin (one backend call, bounded cost) would cover "code unchanged since frame N". **Not in v1**; add when a real script needs it. A general loop would reopen the plugin decision. |
| **Rewind into a mutated span is refused.** A script that mutates and a `step_back`/`rewind_to_cycle` into the mutated frames: the backend refuses (`RefusedUnavailable`) because interpreter state is not snapshotted and subscriptions are not re-fired during replay. | Owner allowed mutation (§2.7); the rewind buffer snapshots the machine, not the interpreter. | Frame-boundary rewind targets stay legal; warning logged; do not mix in a CI row. Snapshotting interpreter state into the rewind stream is a v2 option if a real workflow needs both. |
| **Frame-granular input only.** `press`/`joystick` land at frame boundaries. A test needing a key change at a scanline cannot say so. | Same seam as `--delayed-keypress-frames` and the GUI keyboard (`Keyboard::queue_auto_type`); mid-frame injection would be a new Keyboard capability. | None planned; RZX is IN-granular but replays results, not input (§7.4). |
| **No sub-instruction time.** `on cycle N` resolves to an instruction boundary. | Per-instruction core (`step_one_instruction`, `emulator.h:2255`). | None; this is the accuracy model. |
| **One snapshot stack per name, no cross-name ordering.** ChaseTheBug's single function stack catches an exit of `f` while `g` is on top (non-LIFO interleaving, `.cs:230-234`); per-name stacks cannot see it. | Snapshot stacks are keyed by name so entries pair with their own exits without user bookkeeping. | A shared stack with a name field (`snap calls "f"` / `unsnap calls "f"` asserting the top's name) is a small v2 addition if a real script needs it. |
| **Copper and DMA events are instruction-boundary delivered**, like every latched event: `on copper wait` fires ≤1 instruction after the WAIT was satisfied, `on dma byte` after the burst slot that moved the byte; the 512-entry ring is sized for the Copper's worst case and a full DMA burst with a range armed; overflow is specified (first N kept, dropped count logged). | Per-instruction accuracy model. | The payload carries the exact `CYCLE`/`HC_ULA`/`CVC` of the step, so what is lost is the pause position, not the measurement. |
| **No strings beyond interpolation, no arrays, no maps.** A "call trace with names" is expressible only through the fixed snapshot stack. Counting hits per address needs one `var` per address. | Smallest-language rule. | A `count[addr]` histogram builtin is the likely first request; not in v1. |
| **No file I/O beyond the fixed actions.** No CSV of every hit. | `log` to stderr is greppable; the regression suite already works that way (`magic-port-func.sh`). | None. |
| **No cross-run state.** | Deterministic single run. | None. |
| **No reverse debugging from a script** (`step_back`, "rewind to the frame before this hit"). | The backend has it (owner: capability stays in the backend); the interpreter runs *inside* `run_frame`, and a rewind re-enters it (`emulator.cpp:9252-9254`). Re-entrancy is the problem, not capability. | v2: `stop` then a GUI/DZRP rewind; or a `rewind_to FRAME` action applied at the next frame boundary (needs a prototype). |
| **No interactive REPL.** | Dropped (§1). | The GUI's Script tab shows the log; commands come from the file. |

Nothing in #279's acceptance sketch or in #26's examples hits a wall. The two
items most likely to be asked for first — a bounded `crc32(range)` and a hit
histogram — are both cheap builtins, not plugin-shaped work.

---

## 5. What the DSL requires from the backend

### 5.1 Event vocabulary (REQ ledger)

Sent to `design-backend` as REQ-dsl-1..16. Verdict column updated on reply.

| REQ | Capability | Why (script construct) | Hook point in today's code | Verdict |
|---|---|---|---|---|
| 1 | memory WRITE over a logical range; payload addr, value, PAGE, PC, CYCLE, SOURCE∈{CPU,DMA} | 3(a), 3(d): `on write a..b when VALUE==v` | the 8 `Mmu` sites, `mmu.h:258-412` (write at `:407-412`); per-8K-page bitmap in front | ACCEPTED → CAP-EVT MemWrite (+SOURCE cpu/dma tagged at the boundary drain, `emulator.cpp:9784`) |
| 2 | memory READ over a range, same payload | `on read` | same sites | ACCEPTED → CAP-EVT MemRead |
| 3 | EXECUTE at address/range, before the instruction runs | 3(c), 3(f): `on execute @sym` | the `should_break` slot, `emulator.cpp:9325` | ACCEPTED → CAP-EVT Execute; ALTERNATIVE for the opcode byte: not in the payload, `peek(Cpu, PC)` at delivery (an INT/NMI-accept slot fetches nothing). The DSL exposes no `OPCODE`; nothing lost |
| 4 | NextREG WRITE: reg, new value (pre-commit), SOURCE∈{CPU,COPPER} | 3(b): `on nextreg 0x51 when …` | `NextReg::write`, `nextreg.cpp:456`; Copper `MOVE` `copper.cpp:207-209` (`active_move_hc` ≥ 0 ⇒ Copper) | ALTERNATIVE → CAP-EVT NextRegWrite delivered AFTER commit, payload {reg, value, prev, source cpu/copper/dma}. Adopted: §2.3 `PREV`, `nextreg[REG] == VALUE`; 3(b) unaffected (reads NR 0x50). Whether the deferred-CPU flush path is seen by the same hook: backend §11.4 needs-prototype |
| 5 | PORT read/write: 16-bit port, value (read: value returned), PC; GH #222 matching + mask/value | 3(f) `on io_write 0x57` | `PortDispatch::read/write`, `port_dispatch.cpp:59-65,109-115` | ACCEPTED → CAP-EVT PortRead/PortWrite with (mask, value) as the primitive; GH #222 form is sugar the DSL compiles to. Read value = value returned (latch moves after dispatch) |
| 6 | frame / scanline(CVC) / cycle events; readable FRAME, CYCLE, TFRAME, RasterState | `on frame`, `on scanline`, `${CVC}` | `end_of_frame` `emulator.cpp:9419`; `on_scanline` `:11630`; `raster_state_at` | ACCEPTED → CAP-TIME-02 + CAP-INS-06/07. Scanline delivered ≤1 instruction after the line began; payload carries the exact cycle (§2.2) |
| 7 | RESET (soft/hard), INTERRUPT accepted, NMI accepted | 3(f) latency; span invariants keyed to hardware | `soft_reset` `:10683`, `on_hotkey_f1_hard_reset` `:10864`; `on_int_ack` `:1114`; `request_nmi` `:10386` | ACCEPTED → Reset{hard,soft}, IntAck{vector, mode}, Nmi{source mf/divmmc}, delivered at the boundary after the accept slot |
| 8 | HOST KEY event, named keys 1..8 routed by both GUI frontends; headless `--script-key` | 3(e) | Qt `keyPressEvent` (`main_window.cpp:1351-1522` per TASK-115 §3.3); SDL `host_key_latch::Router::on_host_key` | ACCEPTED (backend half) → CAP-EVT Host, names `script1`..`script8`; key binding per §6.4 (design-qt) |
| 9 | predicate + `once` evaluated in the backend before the subscriber runs | every `when` | new; DZRP declines it | ACCEPTED → predicate closure evaluated at delivery, `once` disables after the first predicate-true firing; DZRP never sets one (§5.2) |
| 10 | read-only inspection from a callback: regs incl. IFF/IM, mem logical + physical, `NextReg::peek`, MMU raw + effective, RasterState, SymbolTable, stack words | every expression in §2.3 | `debug_state.h:126-140` InspectionScope; `nextreg.h:53`; `mmu.h:74` | ACCEPTED → CAP-INS-01/02/03/04/06, CAP-SYM; backend adds `Mmu::peek()` because `Mmu::read()` latches the +3 floating bus (`mmu.h:405-406`, backend finding F1) |
| 11 | actions: STOP(reason), LOG sink, EXIT(code), SCREENSHOT/SCR + SNAPSHOT at frame boundary, INPUT injection at frame boundary (key hold, key edge, joystick bits). (v1 said "no guest mutation" — superseded by REQ-21.) | §2.6 | `save_screenshot` `screenshot.h:60`; `Keyboard::queue_auto_type` `keyboard.h:71-81`, `set_matrix_bit` `:185`; `Joystick::set_joy_left/right` | ACCEPTED → Stop(reason)=pause with `pause_reason Script(id)`; Log=CAP-SES-06; Exit=session event `ExitRequested{code}` (GUI maps to pause+show); screenshot=CAP-CAP-01; snapshot=CAP-CAP-04; input=CAP-IN-01/02/03. The backend offers poke/set_register/nextreg_write/port_out; **the DSL uses them since the owner allowed mutation** (§2.7, 2026-09-27; declined in v1-v3) |
| 12 | STOP from a mid-instruction event takes effect at the end of that instruction; payload PC = pre-execution PC | `stop` in 3(a),(b),(d) landing on the offending instruction | the `data_bp_hit` pattern, `emulator.cpp:9398-9407` | ACCEPTED → pause at the end of the instruction, payload `pc = pc_pre_exec` (`emulator.cpp:9937`) |
| 13 | screen capture as bytes: `Ula::screen_dump`; physical page reads for L2/tilemap | §7 `compare_scr` | `ula.h:608` | ACCEPTED → CAP-CAP-02 `ula_screen_dump()` + `peek(Page{n})` |
| 14 | per-frame input-state observation (matrix rows, joystick ports) from a GUI session | §7 recorder | `Keyboard::read_rows` `keyboard.h:68`, `Joystick::read_port_1f/37` `joystick.h:124-128` | ACCEPTED → CAP-INS-16 `input_state()`: matrix rows, extended keys, joystick 12-bit vectors and the composed 0x1F/0x37 bytes |
| 15 | cost statement measured with `make bench` | owner requirement | `test/bench/bench.sh` | ACCEPTED → backend.md §8; numbers in its v2 |
| 16 | a REAL backend frame counter | `FRAME`, `on frame N` | `Emulator::frame_num_` increments only at `emulator.cpp:8467`, inside `if (rewind_buffer_ && …)` — it is 0 for the whole run without `--rewind-buffer-size` | ACCEPTED, verified by the backend (its finding F2): increment unconditionally at the `emulator.cpp:8467` site, snapshot tag = pre-increment value, so rewind is byte-identical |
| 17 | machine type readable (`MACHINE`) | §7.1 header assert | `EmulatorConfig::type` | ACCEPTED → CAP-INS-19 `machine()`: type + timing constants + video timing variant (absorbs CAP-TIME-01's `machine_timing()`) |
| 18 | CAP-IN-01 pulse queue appends (or refuses) instead of replacing | `press … for n` twice; `--delayed-keypress-frames` twice | `Keyboard::queue_auto_type`, `keyboard.cpp:541` | ACCEPTED → append; 4-frame gap kept; `set_matrix_bit` made public for CAP-IN-02 |
| 19 | CAP-SES-04: SDL = ExitNonZero | `stop` in the SDL frontend (§6.3) | `sdl_app.cpp` has no pause; `frame_sequencer.h:209` | ACCEPTED → "Qt = Pause; SDL and headless = ExitNonZero unless a remote client is connected" |
| 21 | MUTATION: `set_register` covers F/IFF1/IFF2/IM/PC (clears HALTED); pre- vs post-instruction visibility contract; debugger write paths raise no events and carry no SOURCE; backend logs every mutation; rewind stance | §2.7 `set`/`out` | CAP-INS-01/02/04/05/10 exist; Qt panels' write routes | ACCEPTED in full (backend §4.2a): RegId covers every half incl. F/IFF1/IFF2/IM/PC, `set PC` clears halted; Execute delivery = seen by the instruction at PC, every other kind = the next instruction; no events, not CPU-attributed; `poke(Cpu)` = `Mmu::write` outside GuestExecutionScope (the Memory panel's path, overlays honoured), `poke(Page)` hits the physical page, ROM-class page `RefusedReadOnly`; backend logs `MUTATE <what> <old> -> <new> by <client>` for every client; rewind: no re-firing during replay, and `step_back`/`rewind_to_cycle` INTO a mutated span is refused (`RefusedUnavailable`; frame-boundary targets legal) |
| 22 | COPPER events: Move {reg, value, copper_pc, hc_ula, cvc}, Wait {copper_pc, vpos, hpos threshold, hc_ula, cvc}, Halt; filter by copper PC range / reg set; armed only when subscribed | `on copper move/wait/halt` | `copper.cpp:180-195` (WAIT satisfied), `:207-209` (MOVE), `:86-87` (`is_halt`) | ACCEPTED → CAP-EVT `Copper{Move,Wait,Halt}`, copper-PC range and/or NR-set filter, latched at the site, ≤1 instruction late; `Copper.Move` and `NextRegWrite{copper}` both fire |
| 23 | DMA events: Start {src, dst, len, mode, dir}, Byte {src, dst, value, io flags} with dst/src range filter, End {bytes}; armed only when subscribed; overflow policy for long bursts | `on dma start/byte/end` | `dma.cpp:677` (`cmd_load`), `:699` (`execute_burst`), `:785`/`:789` (io/mem write), `:812` (`on_interrupt`) | ACCEPTED → CAP-EVT `Dma{Start,Byte,End}` (Start `:572/:677`, Byte inside `execute_burst` `:783-788`, End `:807-813`; auto-restart = End then Start), Byte range filter, armed only while subscribed; the latch ring is **512 entries** so a full 16-byte burst with a Read\|Write range armed (50 entries) never overflows — corrected from this file's earlier "64" per the architecture doc's Revision 5 sizing, which added the Copper's per-master-cycle cadence (B0 finding F6); overflow is a SPECIFIED, tested path, not a defensive flag |
| 25 | `prev` byte on `Mem{Write}` | `set mem[ADDR] = PREV` (§2.7) | latch site | ACCEPTED (backend v7): `Mem{Write}.prev`, one peek on the hit path only |
| 26 | `Copper.Halt` needs a runtime call of `is_halt` + an edge latch (no caller today, `copper.cpp:87`); `Dma.Start` = the single `state_ = TRANSFERRING` transition (`dma.cpp:423`, `:574`), not also `cmd_load :677` | `on copper halt`, `on dma start` firing once | as cited | ACCEPTED (v7): `Copper.Halt` = new branch on the bare stall path (`copper.cpp:195`) calling `is_halt()` with an edge latch; `Dma.Start` = the one transition "`phase_` enters START_DMA while `state_ == TRANSFERRING`" (covers `:573-575`, `:423-424`, auto-restart `:817`); `cmd_load` cited as a reload only |
| 27 | `source` on `Port{Read,Write}` so a DMA byte to a port destination is attributable | `on io_write … when SOURCE == DMA` | boundary-drain DMA flag | ACCEPTED (v7): `Port{Read,Write}.source ∈ {Cpu, Dma}` |
| 24 | `set_audio_mute_mask` logged as a mutation | `set AUDIO_MUTE` | CAP-INS-10 | ACCEPTED (logged mutation; host-side, never snapshotted) |
| 20 | frame-edge injection queue applied BEFORE `keyboard_.tick_auto_type()` in `end_of_frame` | `--delayed-keypress-frames N ≡ on frame N do press … for 5` (§2.6) | `emulator.cpp:9592`; `headless_app.cpp:560-561` | ACCEPTED → CAP-IN ordering contract (backend.md §4.5, arch §4.5): every IN-01 append / IN-02 level set issued during frame N is applied in `end_of_frame` before `tick_auto_type()` |

### 5.2 Backend concept vs interpreter concept

| Concept | Lives in | Reason |
|---|---|---|
| Range matching, per-8K-page bitmap, port mask matching | backend | hot path; the same code serves the GUI's data breakpoints (a per-address, predicate-less subscription) and DZRP watchpoints |
| `when` predicate | **backend-evaluated**, as a predicate closure `bool(const Event&, const Debugger&)` the DSL compiles (backend.md §4.3, ACCEPTED) | owner decision; a non-matching hit never reaches a rule body. Because the closure is the DSL's own compiled expression, it may read interpreter `var`s and snapshot fields as well as machine state — no split of the clause is needed, and the evaluation point (instruction boundary, machine stopped) makes that safe. The backend only *calls* it; it parses nothing. |
| `once` | backend (auto-disable after the first firing whose predicate was true) | otherwise the interpreter is entered once more than necessary; and DZRP's temporary breakpoints are the same flag |
| enable/disable of a subscription | backend | live-set semantics |
| Snapshots, `var`s, `if`, string interpolation, action sequencing | interpreter | script state |
| Deferred actions (screenshot, snapshot, press, joystick) | interpreter queues, backend applies at `end_of_frame` | the backend owns the frame boundary |
| Exit code, `stop` policy (pause vs exit) | frontend policy (headless app vs GUI) on top of one backend "pause with reason" | §6.3 |

### 5.3 Mapping against backend.md v7 — MAPPED: 35 used, 16 declined, 0 REQs open, 0 reach-arounds

| Backend capability | DSL use |
|---|---|
| CAP-CTL-01 pause | used, only as the `Stop` verdict of a delivery |
| CAP-CTL-02..12 run/step/run_to/step_back/rewind/reset | **declined** — a script observes; it does not drive (§1). `step_back`/`rewind_to_frame` are the §4 wall, revisited in v2 only with a prototype |
| CAP-CTL-13 state()/pause_reason | used (`REASON` in `on stop`) |
| CAP-INS-01 registers | used, read AND write (`set A = …`, `set PC = …`, flags, IFF, IM — §2.7) |
| CAP-INS-02 peek / poke Cpu/Page | used, read and write (`set mem[]`, `set phys[]`) |
| CAP-INS-03 mmu_slots | used (`mmu[]`, `page[]`); `set_mmu_slot` declined — `set nextreg[0x50+s]` is the same write through the register the guest uses |
| CAP-INS-04 nextreg_peek / nextreg_write | used, read and write (`set nextreg[]`) |
| CAP-INS-05 port_in / port_out | `port_out` used (`out`); `port_in` still declined as an *observation* (a port read has side effects; a script that must read a port uses `on io_read`) |
| CAP-INS-06 raster, -07 time | used |
| CAP-INS-08, -09, -11..15 sprites, copper view, disasm, call stack, trace, framebuffer, palette | **declined** — no acceptance case; `screenshot` goes through CAP-CAP-01, not the framebuffer accessor |
| CAP-INS-10 AY registers / audio mute mask | mute mask used (`AUDIO_MUTE`, read and write); AY register read declined |
| CAP-EVT Copper, Dma (REQ-dsl-22/23, ACCEPTED in v6) | used (`on copper`, `on dma`) |
| CAP-INS-16 input_state | used by the recorder (§7.2) |
| CAP-INS-19 machine() | used (`MACHINE`) |
| CAP-INS-17 subscription list | declined (GUI's) |
| CAP-EVT Execute, MemRead, MemWrite, PortRead, PortWrite, NextRegWrite, Frame, Scanline, Cycle, Reset, Host, IntAck, Nmi | used — every kind but one |
| CAP-EVT Magic | declined (§1) |
| CAP-EVT predicate, once, enable, owner | used |
| CAP-TIME-01/02 | used; CAP-TIME-03 run_to declined |
| CAP-IN-01 press_key, -02 set_key, -03 set_joystick | used (`press`, `release`, `joystick`) |
| CAP-IN-04 press_nmi | declined for v1 (no acceptance case; a `press_nmi` action is a one-line add if a recorded session presses the NMI button) |
| CAP-CAP-01 screenshot, -02 screen memory, -04 save_snapshot | used |
| CAP-CAP-03 / CAP-ST-01..04 bookmarks, rewind | declined (§4 wall) |
| CAP-SYM | used (`@name`, `--map`) |
| CAP-SES-01/02 attach + listener | used — the script engine is one client; `Paused` feeds `on stop` |
| CAP-SES-03 pump, -05 active | declined (loop owner / GUI) |
| CAP-SES-04 stop policy | used by the loop owner on the DSL's behalf (§6.3) |
| CAP-SES-06 log | used |

Reach-arounds: none. Every name in §2.3 and every action in §2.6 resolves to a
row above.

### 5.4 The expression compiler as a library (REQ from design-zrcp, ACCEPTED)

backend.md §4.3 puts no expression language in the backend; this grammar is the
grammar of record. So `src/script/` exports two entry points other frontends
may call without a script context:

- `compile_expr(text, EventKind|None) -> std::function<bool(const Event&, const Debugger&)>` — the CAP-EVT predicate. With `None`, payload names (`ADDR VALUE PREV PAGE PORT REG SOURCE KEY REASON`) are compile-time errors, so a predicate compiled for a bare breakpoint can never read a missing payload at run time.
- `eval_expr(text) -> int32` — one-shot evaluation over the inspection surface (ZRCP `evaluate`).

The per-kind payload check covers every payload name of §2.3, including the
Copper/DMA ones (`CPC WAIT_V WAIT_H SRC DST LEN DMA_MODE IO_SRC IO_DST`); a
name used outside a rule of its kind is a compile-time error.

The ZRCP adapter token-translates ZEsarUX's dialect (`A'`→`(AF2>>8)&0xFF`,
`&&`→`and`, `PEEKW(x)`→`mem16[x]`, `SEGn`→`mmu[n]`) into this grammar instead
of owning a second parser; DZRP never calls it. `rom_bank()` is not offered
(the 128K/+3 ROM select is outside this vocabulary; a 128K-view accessor is the
backend's CAP-INS-03 if an adapter needs it). Lands in WP1.

### 5.5 Cost

With no script loaded there must be no new cost: every event site is one
predicated load-and-branch, the pattern `Mmu` already pays for
`watchpoints_live()` (`debug_state.h:98-103`, "a single cached bool"). With a
script loaded, a memory event costs one bitmap test per access plus the precise
check only inside a watched 8K page. The number is the backend's to measure
and state (`make bench`, Task 27 baseline discipline); this document only
records the requirement.

---

## 6. Execution model

### 6.1 Where the interpreter runs

In-process, in `src/script/` (pure C++17, no Qt, no SDL, no `Emulator*`),
invoked **synchronously from backend event callbacks on the emulation thread**.
Every rule body runs inside the backend's callback, which runs inside
`Emulator::run_frame()` (or `execute_single_instruction()` when the GUI is
stepping — the two share `step_one_instruction`, `emulator.h:2255`, so a script
sees the same events either way). There is no script thread and no wall-clock.

### 6.2 Observation is side-effect-free; mutation is explicit

Reads: everything in §2.3, all through the backend's inspection surface, which
is defined to be side-effect-free (InspectionScope; `NextReg::peek`; no
watchpoint can fire on a script read). Observation still cannot perturb the
machine — that half of the constraint is unchanged.

Writes to the machine (owner decision 2026-09-27, §2.7): allowed, through the
explicit verbs `set <machine lvalue> = …` and `out`, over the backend's
existing debugger write paths (CAP-INS-01/02/04/05/10 — what the Qt panels
already write today: registers, memory, NextREGs, the mute mask). Every one is
logged by the backend and applied at the delivery point, so a mutating script
is as deterministic as an observing one. Three other things a script changes:

1. **Debugger state** — pause (`stop`), rule enable/disable, and the process
   exit. Pausing at an instruction boundary changes nothing in the machine;
   the rewind buffer and RZX already rely on that.
2. **Input at a frame boundary** (`press`, `release`, `joystick`). Justified:
   input is not machine state a script is *observing*, it is the stimulus the
   user would otherwise provide by hand; it enters through the same
   `Keyboard`/`Joystick` seam the real frontends use, at the same instant
   (`end_of_frame`) the headless `--delayed-keypress` machinery uses
   (`headless_app.cpp:557-567`), so a run with a script pressing keys is
   deterministic in exactly the way a run with `--delayed-keypress-frames` is.
   Without this the #20 replay could not be a script (§7).

Timing: a rule body executes zero emulated cycles. A script cannot change
*when* anything happens — it changes state at a boundary, and the machine then
runs on from that state.

### 6.3 `stop` semantics

The backend offers one thing: *pause with a reason, at the offending
instruction* — for an execute event that is before the instruction at `PC`
(the `should_break` slot); for a write/port/NextREG event raised mid-instruction
it is at the end of that instruction, with the reported `PC` the instruction's
pre-execution PC (REQ-dsl-12; the `data_bp_hit` shape at `emulator.cpp:9398`)
— except a CPU-sourced NextREG write, which pauses one instruction later
(§2.2) with the payload `PC` still naming the writer.
The frontends decide what a pause means:

| Mode | `stop` | `assert` fail | `exit n` |
|---|---|---|---|
| `--headless` | log `SCRIPT STOP: <reason> at PC=… FRAME=… CYCLE=…`, exit **3** after the instruction | same as stop | exit n |
| GUI (Qt) | pause; open/raise the debugger window if closed (the GH #219 `--persistent-breakpoints` reopen path); disassembly on PC; reason in the Script tab and the status bar | same | log + pause (a GUI never exits from a script) |
| SDL frontend (no debugger) | **same as `--headless`**: log + exit 3. The SDL frontend has no pause: `src/platform/sdl_app.{h,cpp}` contain no pause control (the one `pause` is a FUSE-audio comment, `sdl_app.cpp:422`), and the only pause the sequencer honours is `fx.paused()` reading the debugger's `DebugState` (`frame_sequencer.h:209`) — a stop with no resume path is an exit. REQ-dsl-19 ACCEPTED: CAP-SES-04 now reads "Qt = Pause; SDL and `--headless` = ExitNonZero, unless a remote client is connected", set by the loop owner. Adding an SdlApp pause/resume is deliberately NOT in scope. | same | same as headless |

Exit codes, headless: **0** clean run (the `--delayed-automatic-exit*` bound
fired, or `exit 0`); **1** jnext error — script file missing, parse error,
unresolved `@symbol`, a script runtime error (§6.5); **3** a script `stop` or
failed `assert`; `exit n` as given. **Never 2**: both test harnesses already
use 2 for a HARNESS FAULT (`run-unit-tests.sh` refuses with 2; `regression.sh`
manifest mismatch is 2), so a script verdict must not look like one. The code
for a stop is the loop owner's policy (backend CAP-SES-04 `ExitNonZero`, whose
v1 default is 1): the DSL requires only "non-zero, not 2", and recommends 3 for
`pause_reason == Script(id)` so a row can tell a script verdict from a failed
`--load` without parsing the log — owner question §10.1. A regression row asserts on the code and greps the
log, like `magic-bp-func.sh` does today.

### 6.4 GUI surface (minimal; owned by design-qt)

- Debug menu: **Load Script…**, **Unload Scripts**. Scripts loaded from the
  menu are registered from the *next* frame boundary, so `FRAME` 0 for a
  menu-loaded script is the first full frame after loading — stated in the log
  line the loader emits.
- One read-only **Script** tab in the existing left tab group showing the
  script log and the loaded scripts' rule table (name, event, enabled, hit
  count). No REPL, no editor.
- Host keys 1..8 = `Alt+1`..`Alt+8`, and the routing must work **with the
  debugger window closed** — #279(e) arms the guards after boot/loading with no
  debugger open (`.cs:298-304`). The existing keymap forwarding block
  (`main_window.cpp:2200-2247`) is guarded by `debugger_mgr_->is_enabled()`
  (`:2200`), so it is NOT the place (review R-6). Specified instead:
  1. `MainWindow::keyPressEvent` tests `Alt+1..8` before and independently of
     the `is_enabled()` block, calls the backend's `raise_host_event("scriptN")`
     and **swallows the event**, so the digit never reaches
     `Keyboard::set_key`. **User-visible change, stated and pinned:** today
     `Alt+<digit>` reaches the guest as the bare digit (`keyboard.cpp:361-363`
     uses the Alt variant only for scancodes with an `s_alt_compound` /
     `s_alt_extkey` entry, and digits have none). After this change Alt+1..8
     no longer type 1..8 into the guest; Alt+9/0 and every other key are
     unchanged.
     The key-UP is swallowed the same way, and the chords are consumed even
     when no script is loaded (a host chord's meaning must not depend on
     what is loaded).
  2. In `DebuggerWindow`, eight `Qt::WindowShortcut` `QAction`s (the
     `run_to_cursor_action_` pattern, `src/debugger/debugger_window.cpp:533-537`),
     no menu items — a `QAction` is enumerable by the host-chord gates
     (`findChildren<QAction*>`), a `keyPressEvent` branch is not.
  3. The debugger keymap **refuses** `Alt+1..8` in `validate_combo` by name,
     exactly as it refuses `Alt+letter` (GH1-DEBUGGER-KEYMAP-DESIGN.md) — no
     accept-with-warning: a debugger action on Alt+1 would be ambiguous with
     the script `QAction` in the debugger window (Qt round-robins identical
     sequences, GH #124). A saved config carrying one becomes a `LoadIssue`
     and the action falls back to its default.
  4. The SDL frontend does the same in its `host_key_latch::Router::on_host_key`
     (`host_key_latch.h:306`), before the guest forward.
  5. Rows (agreed with design-qt, qt-frontend.md §5.3): `host_hotkey_test`
     H-SCRIPT-01..08 (debugger CLOSED: Alt+N sets no matrix bit for the digit
     and raises `Host{scriptN}`; key-up leaves the matrix clean) and
     H-SCRIPT-09 (Alt+9 / Alt+0 still reach the guest); `debugger_keymap_test`
     DKSK-01 (fires with the debugger window focused) and DKSK-02
     (`validate_combo` refuses Alt+1, with an `app_config_test` DK twin);
     `host_key_latch_test` HKL-SK-01 (SDL twin). design-qt owns 1-3 and the
     Qt rows; the DSL branch carries 4 and its row, and states the
     user-visible change in the man page (§6.6).

### 6.5 Errors

- **Load time** (parse error, unknown event, `@symbol` not in any loaded MAP,
  label reused, `enable X` naming no rule): the whole script is rejected with
  file:line, headless exits 1 before the machine runs, the GUI shows a dialog.
  Nothing runs partially.
- **Run time** (division by zero, `mem[]` index outside 0..0xFFFF, reading a
  field of an empty snapshot stack): the rule is disabled, the error is logged
  with the rule's file:line, and the run continues; headless additionally
  exits **1** at the next frame boundary. A silently wrong script is worse than
  a stopped one.

### 6.6 Loading — CLI rows for `src/core/cli_options.h`

`make cli-check` diffs the table against the man page both ways, so these are
the rows and the OPTIONS text in one place:

| Flag | Arity | Doc | Help |
|---|---|---|---|
| `--script FILE` | 1 | Documented | Load a debugger script (.jds); repeatable, runs in the order given. In --headless a script `stop` or failed `assert` exits 3. |
| `--script-key FRAME N` | 2 | Documented | Deliver script host key N (1-8) at emulated frame FRAME (headless only, repeatable). The headless form of Alt+N. |
| `--map FILE` | 1 | Documented | Load a z88dk .map symbol table for `@symbol` in scripts and for the debugger (same as Map > Load MAP). |

The man page's "Scripting" section states the user-visible keyboard change:
**Alt+1..Alt+8 are host chords (script keys 1-8) in the Qt and SDL windows and
no longer type the digits into the guest**; Alt+9, Alt+0 and every other key
are unchanged.

`--map` is new to the CLI (the table has no symbol flag today; the debugger
loads MAPs only from its menu, `debugger_window.cpp:528`). It loads into the
backend's single symbol table (CAP-SYM), so the GUI and scripts share one.

---

## 7. The #20 verdict

**Yes: record/replay reduces to (a recorder that emits a script plus screen
dumps) + (the DSL's `press`/`release`/`joystick`, `compare_scr`/`screenshot`,
`exit`). Nothing in replay needs a mechanism the script engine does not already
need for #26/#279, and the recorder needs nothing the backend does not already
have to offer the GUI.** The argument, from the code:

### 7.1 Replay is a script

`06-dapr-keyb` (`demo/dapr-nexlib+tests/interactive/test06keyb`, parked in
`test/interactive/README.md` because it needs typing) replayed:

```
# dapr-keyb.jds — generated by jnext (recorder v1), do not edit
# jds-recorder: 1
# machine=next load=test06keyb.nex rtc=2026-01-01T00:00:00 sd=cspect-next-1gb-fixed.img
# joystick: nr05=0x40 (joy0=kempston1 joy1=sinclair2)
on frame 0 once do assert MACHINE == 4 "recorded on Next" end
on frame 120 do press "q" end               # level: down until the release below
on frame 126 do release "q" end
on frame 131 do press "w" end
on frame 133 do press "caps" end            # overlapping keys are ordinary
on frame 137 do release "w" end
on frame 139 do release "caps" end
on frame 160 do compare_scr "dapr-keyb-0001.scr" "screen after q,w" end
on frame 190 do press "caps+1" end          # EDIT (a compound is two matrix bits)
on frame 197 do release "caps+1" end
on frame 230 do compare_scr "dapr-keyb-0002.scr" "screen after EDIT" end
on frame 231 do exit 0 end
```

Every line parses under §2.1 (the precondition assert is a `once` rule at
frame 0 — there are no top-level actions, §1), and every construct exists for
other reasons: `press`/`release` are the **level** form over the same
`Keyboard` matrix `--delayed-keypress-frames` drives (CAP-IN-02, not the
auto-type pulse — a recorded hold of any length and overlapping keys replay
exactly, which the pulse could not do; review R-1), `compare_scr` is
`Ula::screen_dump()` against a file, `exit` is the CI verdict. The row in
`functional_tests.conf` becomes one `--script` invocation with
`--delayed-automatic-exit-frames` as the hard bound (the suite's existing
contract, §"Headless mode" in `CLAUDE.md`).

### 7.2 What the recorder must observe and emit

A small module (`src/script/recorder.*`, pure C++) attached to the backend's
frame-boundary event in a **GUI session**:

1. **Per frame**: the 8 keyboard matrix rows (`Keyboard::read_rows`, one call
   per row select) and both joystick ports (`Joystick::read_port_1f/37`).
   It emits an event **only on change** (edge encoding, so a 2000-frame session
   is a dozen lines, not 2000). A matrix bit that went down becomes
   `press "<key>"`, up becomes `release "<key>"` — **only the level form, never
   `for`** — using the inverse of the `--delayed-keypress` name table
   (`headless_app.cpp:209-257`; a bit with no single-key name is emitted as
   its `row,col` pair, which `key_spec` also accepts); a joystick change
   becomes `joystick n bits`. The sample is taken at `begin_new_frame(K)` and
   the stamp is **K−1** (§2.6, "How the recorder must stamp"): the change was
   applied by the host-key latch between frames K−1 and K, replay applies
   `on frame K−1` at E_{K−1}, and the guest sees it from K in both runs.
2. **On a host key** (script host key 8 while recording, or a dedicated
   Debug-menu action): `Ula::screen_dump()` to `<base>-NNNN.scr` and a
   `compare_scr` line at the current frame. The index file #20 asks for *is*
   the script.
3. **Header**: a `jds-recorder: <version>` line, machine, loaded file, `--rtc`
   value, SD image identity (`sd_snapshot_identity`) and the **joystick mode**
   (NR 0x05 as read, e.g. `0x40` = joy0 Kempston1 / joy1 Sinclair2, the reset
   default at `joystick.h:184`) — the DSL cannot set the joystick mode, so a
   GUI session that changed it must say so and the replay `assert`s it
   (`nextreg[0x05]`). These are the preconditions under which the replay is
   deterministic, so a mismatch is a loud `assert` rather than a mysterious
   diff.
4. **Recording while paused mid-frame is inexact** (review N3-2). "GUI input
   does not change mid-frame" holds for a running machine. When the debugger
   holds the machine mid-frame (a breakpoint hit, `Emulator::frame_in_progress()`
   true), a key pressed then is applied at once (`host_key_latch.h:431`) and is
   visible to the remainder of that frame after resume — `begin_new_frame` is
   not re-run on resume (`emulator.cpp:9284-9288` guard) — so the recorder
   first samples it at `begin_new_frame(K+1)`, stamps K, and replay shows it
   from K+1 rather than from mid-K. The recorder therefore **warns** in the
   emitted script (`# WARNING: input change recorded while paused mid-frame at
   FRAME K; replay is not exact here`) whenever a change is sampled with
   `frame_in_progress()` set at the moment of the key event, and the replay of
   such a script logs the same warning on load. Refusing to record was
   rejected: pausing to look is a normal part of an interactive session and the
   inexactness is local to that frame. This is §4's frame-granularity wall,
   reached from the other side.
5. **The deterministic observable** (review N-7): a `.scr` taken at frame M
   compares equal iff the guest reached the same state by M — for a program
   that polls input every few frames (test06keyb polls every 4 frames via
   `waitForScanline(255)` ×4, `main.c:64-67`) that means the poll that first
   sees a key must land on the same frame in replay as in the recording, which
   the K−1 stamping rule and the before-`tick_auto_type()` ordering in §2.6
   together guarantee. The regression twin `script-replay-edge-func` (§8) pins
   both with a demo that stores the `FRAME` at which it first saw a key.

The recorder is not a language feature; it is a consumer of REQ-dsl-6/8/14 that
writes text. That is the "one genuinely new piece" #276 predicted, and it is
~200 lines.

### 7.3 Reconciliation with what exists — no third mechanism

| Existing | Relation |
|---|---|
| `--delayed-keypress-frames N KEY` (`headless_app.cpp:557-567`, 5-frame hold via `queue_auto_type`) | ≡ `on frame N do press "KEY" end` **on a run that neither pauses nor cold-boots**: the flag counts loop TICKS, which survive a cold boot and advance while paused, and `on frame N` counts frame tags, which do neither (owner decision 2026-09-28, B4 O2 — the flags keep their tick countdowns in the loop owners). The flag **stays**: it is the regression suite's contract for 20+ rows, and a flag is the right tool for one keypress. A script is the general form; both go through one backend input-injection call (REQ-dsl-11, `press_key` since B4), so the ACTION cannot drift. |
| `--delayed-screenshot FILE` + `-frames N` (+ `.scr` by extension, GH #18) | ≡ `on frame N do screenshot "FILE" end`. Same `save_screenshot` path (`screenshot.h:60`). The flag stays for the same reason. |
| `--delayed-automatic-exit-frames N` | stays as the hard bound; a script's `exit` is the *verdict*, the flag is the *watchdog*. Both are needed (a script that never reaches its `exit 0` must still terminate — non-zero, via the existing "screenshot outstanding" contract for the screenshot case and via the script's own missing `exit` for the general one: a headless run whose scripts declared a `compare_scr`/`exit` that never fired exits 3 with `SCRIPT: N deferred actions never ran`). |
| RZX (`rzx_recorder.h`, `port_dispatch.cpp:194-202`) | **Stays as is; not extended.** RZX records IN *results* per frame and replays them by overriding every IN (`rzx_in_override`), which makes a run reproduce even when the emulator's own input model changes — but it cannot assert anything about the screen, it carries its own SNA snapshot, and its frame is the interchange unit. A jnext script recording records *input state* and lets the emulator compute the INs, which is what a test of the input path needs (the parked dapr rows are tests of the keyboard *reading*, which an RZX would bypass entirely). The two are different tools; neither replaces the other. |
| `--persistent-breakpoints`, magic breakpoint/port | unrelated; a script may coexist with them. |

### 7.4 "Screen memory": decided

- The **byte-diffable assertion unit is the ULA screen** — `Ula::screen_dump()`
  (6912 bytes, 12288 in Timex modes, `ula.h:600-608`), the `.scr` the
  screenshot path already writes. That is what #20's own text asks for
  ("independent of compositing and scaling, diffable as bytes") and it is
  exactly what the two parked rows test.
- For **Layer 2, tilemap and sprites there is no "screen memory"** in that
  sense: Layer 2 is 48-80 KB of banked RAM selected by NR 0x12/0x13, the
  tilemap is a map plus patterns in bank 5/7, sprites are attribute + pattern
  RAM. Dumping those as bytes would assert on state the test author cannot
  reason about and would break on every legitimate change of a bank number.
  The honest observable for those layers is the **composited framebuffer**,
  which the regression suite already compares as PNG with a pixel-diff tool
  (`png-diff-func`). So a recorded test of a Layer 2 program emits
  `screenshot "…png"` lines and the suite's PNG comparison, not a byte compare.
  Scripts get both; the recorder chooses `.scr` when NR 0x15 says only the ULA
  is enabled and `.png` otherwise, and says which in a comment.
- `phys[page, off]` remains available for a hand-written script that *does*
  want to assert on a specific Layer 2 byte.

### 7.5 What record/replay would need that the script engine should NOT carry

Nothing that changes the engine. The two things that sit outside it are already
outside: the recorder (a writer of text, GUI-side) and the PNG comparison
(the suite's). The one wall that matters for #20 is §4's "frame-granular input
only": a game that samples the keyboard mid-frame and reacts differently to a
key that changes at scanline 100 versus scanline 0 cannot be recorded exactly.
The parked dapr rows do not need that; a future test that does would need
scanline-stamped injection in `Keyboard`, which is a backend/input change, not
a language one.

**Proposed re-scope of #20**: "Recorder that emits a `.jds` script + `.scr`/PNG
captures from a GUI session; `press`/`release`/`joystick`/`compare_scr` actions
in the #26 DSL; the two parked DAPR rows converted to `script-*-func`
regression rows." Blocked on #26's WP1-WP4 (§9).

---

## 8. Testing

All headless, no GUI; scripts are the fixtures.

1. **`script_parse_test`** (unit, `test/script/`): lexer + parser against a
   fake backend that records subscriptions. Rows per grammar production, every
   §6.5 load-time error, and one row per event kind proving the rule became
   exactly one subscription with the right kind, filter, `once` and a non-null
   predicate iff `when` was written.
   Named rows: PARSE-COMMENTS (`;`, `//`, `#`; `a / b // c` is a comment after
   a division), PARSE-ONCE-WHEN-ORDER (`when … once` is rejected),
   PARSE-DMA-END (`on dma end do … end`), PARSE-PAYLOAD-KIND (`CPC` outside a
   copper rule is a compile error), PARSE-LVALUE (`set A = 1`, `set mem[x] = 1`,
   `set 3 = 1` rejected).
2. **`script_eval_test`**: evaluator against a fake inspection surface —
   every expression form in §2.3, `${:x2}` formatting, snapshot stack
   semantics (push/pop/depth, `changed()` per group, empty-stack error), 32-bit
   wrap, division by zero disabling the rule.
3. **`script_events_test`**: the real `Emulator` in headless mode with a
   tiny injected Z80 program built in the test (the `--inject` route), one row
   per event kind proving the payload: `ADDR`/`VALUE`/`PAGE`/`PC`/`SOURCE` on
   a write; on a NextREG write from the CPU and from a Copper `MOVE` the
   **post-commit contract** — `nextreg[REG] == VALUE` at delivery, `PREV` ==
   the value before the write, `SOURCE` correct; a `page` filter matching a
   write to a bank mapped at 0x8000 and, after an MMU remap, at 0xC000; `once`;
   enable/disable; `stop` landing PC on the offending instruction after a
   mid-instruction write; a level `press` issued in frame N visible to the
   first port read of frame N+1 and to no read of frame N; a `for` pulse
   issued at E_N pressed at E_N (before `tick_auto_type()`) and released after
   n frames, landing on the same frame `--delayed-keypress-frames N` lands on
   (row SCRIPT-EV-INJ-ORDER, mutation: apply the queue after `tick_auto_type()`);
   `FRAME` advancing without a rewind buffer.
   Each of the three suites is a `test/unit-tests.conf` line with its pinned
   count (and the SDL-only manifest's, since all three are Qt-free) in the
   same WP that adds the suite — `run-unit-tests.sh` refuses (exit 2)
   otherwise. The counts cannot be pinned before implementation and are not
   guessed here.
4. **Regression rows** — one per feature of the script suite (§9 WP7):
   `script-guard-func` (range watch), `script-mempoint-func` (value
   predicate), `script-mmu-func` (NextREG), `script-isr-func` (span
   invariants), `script-copper-func`, `script-dma-func`, `script-mutation-func`,
   `script-hostkey-func`, `script-replay-keyb-func` and
   `script-replay-edge-func` (record/replay) in `functional_tests.conf`
   (`# expect:` count bumped), each running one script of the suite against
   the `dsl_demo` NEX — with a **red twin**: the same script against a
   deliberately buggy build of the demo (a stray write, an ISR that clobbers
   HL) OR, where §2.7 allows it, the good build with the fault injected by a
   script mutation **upstream of the guest write the guard watches** (a
   script write raises no event, so it can never be the watched write
   itself), must exit 3 with the expected reason in the log. A row
   that only has the green half is fixture-blind; a red twin injected by the
   script proves the script, one built into the demo proves the detector
   against real code — the suite has at least one of each.

Mutations a reviewer must run (each must turn the named row red):

| Script | Mutation in the emulator/backend | Row that must go red |
|---|---|---|
| 3(a) guard | stop passing `PAGE`/`PC`; or make the range check exclusive at the top end | `script-guard-func` (asserts on the logged PC and the last byte of the range) |
| 3(b) mmu | deliver the NextREG event *before* commit (so `nextreg[REG] != VALUE` at delivery) | `script_events_test` post-commit row |
| 3(b) mmu | drop `prev` from the payload (so `PREV == VALUE`) | `script_events_test` `PREV` row |
| 3(b) mmu | capture `PC` at the drain instead of at the hook for a CPU NextREG write (payload PC = the instruction AFTER the writer) | `script-mmu-func` (asserts the logged PC == the `NEXTREG` instruction's address from the MAP) |
| 3(c) isr | `changed(isr, mmu)` compares 7 slots instead of 8; or `stack0` captured after the push | `script-isr-func` red twin (clobbers slot 7 / the return address) |
| 3(d) mempoint | register the rule with no predicate and test `VALUE` inside the body instead | `script_parse_test` (subscription must carry a predicate) + `script_events_test` row counting rule-body entries on N non-matching writes (must be 0) |
| 3(b) mmu | deliver a Copper `MOVE` to NR 0x51 with `SOURCE == CPU` | `script_events_test` Copper row (the demo's copper list writes NR 0x51 once) |
| 3(e) hostkey | route Alt+N to the guest instead of the backend | `script-hostkey-func` (headless `--script-key`) + a Qt unit row on the keymap |
| replay | apply a level `press` one frame late (or immediately, mid-frame) | `script-replay-edge-func`: the demo stores the `FRAME` of its first key-down sighting at a fixed address; the script asserts `mem[addr] == 121` for a `press` at frame 120 — deterministic, unlike a `.scr` of a program whose "just pressed" line is transient (review N-7) |
| recorder | stamp a change first seen at `begin_new_frame(K)` as K instead of K−1 | `script-replay-edge-func` second half: record a GUI-driven press (the row drives it through `host_key_latch::Router`, as `sdl-keypress-func` does) whose first sighting the demo latched at frame 121; the emitted script must say `on frame 120`, and replaying it must latch 121 again |
| injection order | apply the injection queue after `tick_auto_type()` | `script_events_test` SCRIPT-EV-INJ-ORDER + every existing `--delayed-keypress-frames` screenshot row (shifted by one frame, the dapr-tilemap and game rows diff) |
| replay | implement `press` (no `for`) as the auto-type pulse | `script-replay-keyb-func` (the recording holds `w` across `caps`; the pulse strands or drops one of them and the second `.scr` differs) |
| 3(a) guard | implement `on write page N` as a full-range subscription with a `PAGE` predicate | `script_events_test` page row counting latched events on writes outside page N (must be 0) |
| all | remove `once` auto-disable | `script_events_test` `once` row |
| mutation | apply a script `set mem[x]` inside `GuestExecutionScope` (so it fires `on write x`) | `script_events_test` SCRIPT-EV-MUT-NOEVENT (a rule that pokes an address it also watches must fire once, not loop) |
| mutation | apply a `set PC` at an execute delivery AFTER the instruction instead of before | `script_events_test` SCRIPT-EV-MUT-PRE (the demo's trap instruction must NOT execute) |
| mutation | drop the backend `MUTATE` log line | `script-mutation-func` (greps the line) |
| mutation red twin | a script-injected fault UPSTREAM of the watched guest write: `set nextreg[0x50] = 0xFF` at `on execute @page_in_level_mmu1` — the `NEXTREG 0x51` instruction itself, so the guest's preceding `NEXTREG 0x50` has already committed — against the GOOD demo build | `script-mmu-func` second half: exit 3 with the guard's reason — the GUEST's `NEXTREG 0x51, 0x23` trips `mmu_guard` (0xFF + 1 != 0x23). Two mutations of the mutation, both must stay GREEN: (1) inject at the watched register `set nextreg[0x51]` instead (no event, §2.7); (2) hook `@page_in_level` one instruction earlier (the guest's deferred `NEXTREG 0x50, 0x22` overwrites the injection, `emulator.cpp:10221`) |
| copper | deliver `on copper move` without `CPC` (or with the CPU's raster position instead of the step's `HC_ULA`) | `script-copper-func` (asserts `CPC` and the GH #181 `CVC == 95 and HC_ULA >= WAIT_H` line) |
| dma | fire `on dma byte` only for bytes inside a Mem range subscription | `script-dma-func` (no range subscribed; the byte count must equal `LEN`) |
| all | remove the `InspectionScope` around script reads | `script_events_test`: a `read` watchpoint on an address the script peeks must NOT fire |

---

## 9. Work packages — one issue branch, reviewed per WP, merged once

Owner rule 2026-09-24 and `DEBUG-SUBSYSTEM-ARCHITECTURE.md` §10.1/§10.3: a
multi-stage issue lives on **one** branch until the whole issue is done. All
DSL work happens on **`gh26-dsl`** (worktree `~/tmp/worktrees/gh26-dsl`); each
WP is a commit series — or a short-lived sub-branch off `gh26-dsl` for two WPs
in flight in parallel, merged back into `gh26-dsl`, never into `main` — with
its own independent review before it lands on `gh26-dsl`. `gh26-dsl` merges to
`main` **once**, after the full triplet + `make unit-test-sdl` on the branch
and a final review, followed by a single `make bump-patch`. The recorder (#20)
rides the same branch and #20 closes with #26. WP0 is the backend's and lands
on its own branch first (`gh26-dsl` rebases onto it).

| WP | Content | Depends on |
|---|---|---|
| WP0 | Backend event surface per §5 (REQ-dsl-1..16) — **design-backend's**, not this file's | — |
| WP1 | `src/script/lexer.*` (comments `;`, `//`, `#` — parser rows for all three and for `//` vs division), `parser.*`, `ast.h` (machine lvalues, `out`, `copper`/`dma` events), the `compile_expr`/`eval_expr` library entry points (§5.4); `script_parse_test` + its `unit-tests.conf` and SDL-manifest lines | WP0's predicate signature |
| WP2 | `evaluator.*`, `value.h`, snapshot stacks, interpolation; `script_eval_test` + manifest lines | WP1 |
| WP3 | `script_engine.*`: rule registration onto backend subscriptions (incl. the page filter, Copper and DMA kinds), deferred-action queue with the §2.6 edge rule, mutation dispatch onto the debugger write paths (§2.7) with the rewind warning, `stop`/`exit` policy hooks, log sink; `script_events_test` + manifest lines | WP0 (incl. REQ-dsl-21..23), WP2 |
| WP4 | CLI rows (`--script`, `--script-key`, `--map`) in `cli_options.h`, man page `jnext.1.md` OPTIONS + a "Scripting" section, `make docs-man`, headless and SDL exit-code wiring | WP3 |
| WP5 | GUI: Debug menu items, Script tab, Alt+1..8 routing per §6.4 (incl. the `host_hotkey_test` / `debugger_keymap_test` rows) — design-qt owns; the SDL `host_key_latch` twin + row is this branch's | WP3 |
| WP6 | Recorder (`src/script/recorder.*`), Debug menu "Record Script…", capture hotkey, header emission (§7.2); converts the two parked DAPR rows | WP3, WP5 |
| WP7 | **The DSL demo and script suite** (owner requirement). `demo/dsl_demo/` — ONE NEX built with z88dk like the other demos, exercising in one program: writes to a data area and (in its buggy build) a stray write into code, NextREG writes incl. MMU0/MMU1 paging, an IM2 interrupt handler with a (buggy-build) register clobber, a Copper list with a `WAIT`/`MOVE` palette split, a DMA sprite-pattern upload, and a keyboard poll that latches the `FRAME` of the first key seen; `--map` output shipped next to it. `test/scripts/dsl/` — one script per feature: `range_watch.jds`, `value_predicate.jds`, `nextreg.jds`, `span_invariants.jds`, `copper.jds`, `dma.jds`, `mutation.jds`, `record_replay.jds` (+ the recorder's emitted twin), each with a header comment stating the exact command line, the expected exit code and the expected log line — runnable by hand. Harness contract (N-9): each row is a thin `test/00regression/scripts/script-<feature>-func.sh` wrapper (the driver refuses a declared row with no script) that runs `$JNEXT --headless --script test/scripts/dsl/<feature>.jds …` and asserts the exit code + log line; `dsl_demo.nex`, its buggy twin `dsl_demo_buggy.nex` and `dsl_demo.map` are committed under `test/00regression/nex/` like every other fixture. Scripts and rows are 1:1 — ten of each: `range_watch`, `value_predicate`, `nextreg`, `span_invariants`, `copper`, `dma`, `mutation`, `hostkey` (driven with `--script-key`), `replay_keyb` (the recorder's emitted script, committed) and `replay_edge`; `functional_tests.conf` `# expect:` count +10 | WP4 |
| WP8a | **User-guide chapter for the Debug DSL** under `src/doc/user-guide` (owner: "the most powerful feature of jnext"), EXHAUSTIVE: the language reference (every event kind with its payload, every condition form, every action, every builtin and state accessor from §2.3, every mutation verb with its visibility rule, comments, literals, precedence), the `dsl_demo` scripts walked through one by one with their output, record/replay end to end (record in the GUI, replay headless, what "inexact" means), headless/CI usage (`--script`, `--script-key`, `--map`, exit codes 0/1/3, the watchdog flag), and the GUI (Script tab, Alt+1..8). Re-rendered with `make docs-userguide` and committed in the same change — `docs-check` (a prerequisite of `make unit-test` and `make regression`) fails otherwise. The chapter is written against the RUNNING product, per the project rule that found the man-page defects. | WP4-WP7 |
| WP8b | Developer guide chapter (`src/doc/developer-guide`, `make docs-devguide` + `docs-devguide-check`); man page "Scripting" section; FEATURES.md | WP4-WP7 |

Each WP gets an independent reviewer per `CLAUDE.md` before it lands on
`gh26-dsl`; WP7's reviewer runs the §8 mutation table; the final pre-merge
review re-runs it on the whole branch.

---

## 10. Owner answers (2026-09-27) — no open questions remain

1. **Exit code for a headless script stop with no explicit `exit` = 3.**
   Decided. (1 = jnext could not run; 2 = harness fault, reserved; 3 = the
   script caught something; `exit n` overrides.) §6.3 stands.
2. **`Alt+1..Alt+8` as script host keys = yes.** §6.4 stands.
3. **`--map` feeds the one symbol table = yes.** §6.6 stands.
4. **Mutation = allowed**, explicit and logged — §2.7 replaces the v1-v3
   no-poke reading; the "no mutation" wall is gone from §4.
5. **#20 re-scope** as in §7.5 — carried on `gh26-dsl` (§9).
6. Added by the owner: comments `;` and `//` (§2), `on copper` / `on dma`
   (§2.1, §3, REQ-dsl-22/23), the demo + script suite (WP7), the exhaustive
   user-guide chapter (WP8a).

---

## Appendix A — CSpect vocabulary mapping (reference, not binding)

From `iPlugin.cs` (`eAccess`) and `iCSpect.cs`:

| CSpect | DSL |
|---|---|
| `Memory_Write` / `Memory_Read` per address (registered one address at a time — ChaseTheBug registers 16K+ `sIO`s in a loop, `.cs:127-131`) | `on write a..b` / `on read a..b` (one subscription per range) |
| `Memory_EXE` | `on execute` |
| `Port_Read` / `Port_Write` | `on io_read` / `on io_write` |
| `NextReg_Write` / `NextReg_Read` | `on nextreg`; no read event (no case needs it; `NextReg::peek` side-effects were the reason the GUI got `peek`, and a read *event* would re-open that) |
| `KeyPress` `"<ctrl>g"` | `on hostkey n` |
| `Tick()` (per frame), `OSTick()` (wall-clock UI) | `on frame`; nothing (wall-clock is out) |
| `Reset()` | `on reset` |
| `GetRegs`, `Peek`, `PeekPhysical`, `GetNextRegister`, `LookUpSymbol` | §2.3 |
| `Debugger(Enter)` | `stop` |
| `Poke`, `PokePhysical`, `SetRegs`, `OutPort`, `SetNextRegister` | `set mem[]`, `set phys[]`, `set <REG>`, `out`, `set nextreg[]` (§2.7 — owner 2026-09-27) |
| `SetSprite`, `CopperWrite`, `PokeSprite`, `LoadNex`, `SetColour` | not offered: sprite attributes/pattern RAM and the Copper program are reachable through their ports (`out 0x303B …`, `out 0x57 …`, `out 0x5B …`) and the palette through NextREGs, so no dedicated verb; `LoadNex` is a control verb (§1) |
| `Debugger(Step/StepOver/UnStep/Run)` | not offered from a script (§1) |

## Appendix B — Coordination log

- 2026-09-26: REQ-dsl-1..15 sent to `design-backend`; amendments 16 (frame
  counter) and 1-SOURCE sent after reading `emulator.cpp:8467`.
- 2026-09-26: `design-qt` asked to make GUI data breakpoints the predicate-less
  case of the same subscription, to confirm the Script tab / `stop` display,
  and the `Alt+1..8` namespace.
- 2026-09-26: `design-dzrp` asked to confirm the predicate slot is optional in
  the shared subscription and that a script `stop` surfaces as a plain pause
  notification.
- 2026-09-26: backend.md v1 verdicts received and mapped (§5.1, §5.3). One
  model change adopted: events latched at the site, delivered at the
  instruction boundary (§2.2); NextREG delivery after commit (`PREV`). REQ-17
  (machine type) sent. Backend §11.5 answered: the indented call trace and the
  entry/exit span need no general variables or lists — the snapshot stack
  (§2.5) is the one data structure, deliberately fixed, and it is enough for
  3(c). Backend §9's #20 verdict and §7 here agree.
- 2026-09-26: backend accepted REQ-17 (CAP-INS-19). Exit code for a script
  stop changed from 2 to 3 on the backend's point that 2 already means harness
  fault in both test harnesses; owner question §10.1 rephrased.
- 2026-09-26: design-dzrp confirmed: the predicate slot is optional, DZRP
  breakpoint ids are adapter-side over backend handles, and a script `stop`
  reaches DeZog as `NTF_PAUSE` reason 255 + the reason string
  (DeZogProtocol.md:843-846). Caveat recorded: DeZog consumes a pause
  notification only while it has a CONTINUE outstanding, so a script stop while
  DeZog already believes the machine paused is dropped by the client — DeZog's
  model, not ours.
- 2026-09-26: design-qt agreed (its qt-frontend.md §3.5, §9.3, §10): a GUI
  data breakpoint is the degenerate subscription (lo==hi, no predicate, Stop,
  owner=gui) — with four properties it keeps (listable model + per-entry
  enable + exact master round-trip GH #225; kind-typed change notification
  GH #220; unchanged single-address hot-path pre-gate; the GH #222 port rule),
  sent to the backend as REQ-qt-13b/c/d. GUI Watches stay a peek, not an
  event. `stop` in GUI = the GH #219 path (`Paused{reason=Script(id)}` →
  window auto-enabled next tick, follow-PC, reason in the debugger status bar,
  `debugger_window.cpp:774-829`). A Script tab is the DSL PR's change, not
  #278's, and must add rows to `debugger_window_size`/`grow` and
  `debugger_accel_test`, which pin the tab group and menu shape. `Alt+1..8`:
  no collision (window Alt compounds are E/G/C only, `main_window.cpp:2181-2183`);
  caveat that Alt+digit is a legal user debugger-key binding, so the existing
  accept-with-warning rule (GH1-DEBUGGER-KEYMAP-DESIGN.md §4b) applies; route
  through the keymap forwarding block (`main_window.cpp:2200-2247`), not a new
  switch. Menu rows Load Script… / Unload Script: design-qt wires them and
  they go into `debugger_menu_test` with a DACC-*-acceptable mnemonic.
- 2026-09-26: design-zrcp asked for the expression compiler as a library call
  so ZRCP conditions and `evaluate` translate into this grammar rather than a
  second parser: ACCEPTED, §5.4; spellings confirmed to it (AF2/BC2/DE2/HL2,
  `and/or/not`, `mem[]/mem16[]`, `mmu[]/page[]`, no `rom_bank()`).
- 2026-09-27 (review round 4): REQ-dsl-25/26/27 ACCEPTED (backend v7);
  adopted the 512-entry ring with specified overflow (dropped count logged),
  MOVE as one latch entry fanned out at the drain, and the three §4.2a rules
  (RefusedRzx, no firing in replay, PC redirect bypasses a breakpoint at B for
  one instruction). v7 confirmed: 35 / 16 / 0 / 0.
- 2026-09-27 (owner review): mutation allowed (§2.7), comments `;`/`//`,
  `on copper`/`on dma`, WP7 demo + script suite, WP8a user-guide chapter,
  owner answers in §10. REQ-dsl-21..24 ACCEPTED by design-backend (its v6,
  §4.2a): adopted the rewind refusal (`RefusedUnavailable` into a mutated
  span) in place of my warning-only stance, and the 64-entry latch ring.
  v6 confirmed: 35 used / 16 declined / 0 open / 0 reach-arounds.
- 2026-09-26 (review round 2): REQ-dsl-20 (injection queue applied before
  `tick_auto_type()`) ACCEPTED; recorder stamping rule corrected to K−1 (R2-1).
- 2026-09-26 (review round 1): backend v4 — REQ-dsl-18 ACCEPTED (append),
  REQ-dsl-19 ACCEPTED (CAP-SES-04), arch §7.5 aligned to `TFRAME`. One CAP I
  use changed shape and is adopted: CPU-sourced NextREG writes commit inside
  `tick_devices_after_instruction` (`emulator.cpp:10221`), after the boundary
  drain, so `on nextreg` from the CPU is delivered ≤1 instruction late with
  the payload `PC`/`CYCLE` captured at the hook (§2.2, §6.3); a mutation row
  pins that the payload PC is the writer's. v4 confirmed (28/21/0).
- 2026-09-26 (review round 1): design-qt verified R-6 and specified the Qt
  delivery (qt-frontend.md §5.3); §6.4 aligned to it. REQ-dsl-18 (CAP-IN-01
  append vs refuse) and REQ-dsl-19 (CAP-SES-04: SDL = ExitNonZero) sent to
  design-backend.
- 2026-09-26: backend.md v3 confirmed (28 used / 21 declined / 0 open). Its
  additions — CAP-INS-20 executed-PC coverage, per-client subscription switch,
  `probe_execute`, CAP-CTL-15 `load`, named bookmarks, richer TraceEntry,
  pump budget — are all declined for v1 (no acceptance case; an
  `executed(addr)` builtin over coverage is a cheap later add). `Paused.matched
  Hit{}` feeds `REASON`/payload in `on stop`. CAP-SES-04 as decided (headless
  Stop pauses while a remote client is connected, exits non-zero otherwise)
  matches §6.3.
- 2026-09-26: design-gdb confirmed the range-watch primitive (hit address +
  value at the stop) is the same shape for RSP `Z2/Z3/Z4`.

## Appendix C — Review round 1 dispositions (2026-09-26, `scratchpad/reviews/dsl-qt.md`)

Every finding was verified against the source before the text changed.

| Finding | Verified | Disposition |
|---|---|---|
| R-1 `press`/`release` cannot replay a session | `keyboard.cpp:541` replaces the queue; `:557-590` press at 0, release at `frames`, 4-frame gap; `headless_app.cpp:559-561` inherits it | FIXED: `press`/`release` are level ops on CAP-IN-02; `press … for n` is the CAP-IN-01 pulse; recorder emits level only; the edge rule is stated (§2.6); REQ-dsl-18 ACCEPTED → append |
| R-2 replay example does not parse | §2.1 has no top-level actions | FIXED: `on frame 0 once do assert … end`; no `require` added (one mechanism) |
| R-3 physical-page filter has no grammar | backend.md v3 §4.3 `Mem` page set + `Execute.page` | FIXED: `page` form in `addr_spec` (§2.1), §3(a) rewritten, parser warning on wide `when PAGE ==`, mutation added |
| R-4 §8 test 3 contradicts post-commit delivery | §2.3/§5.1 REQ-4 | FIXED: test 3 and the 3(b) mutations rewritten to the post-commit contract (`nextreg[REG] == VALUE`, `PREV`) |
| R-5 SDL "has a pause" is false | `sdl_app.cpp:422` only; `frame_sequencer.h:209` | FIXED: SDL = ExitNonZero (§6.3); REQ-dsl-19 ACCEPTED → CAP-SES-04 updated |
| R-6 host keys need the debugger open; guest digit swallow unstated | `main_window.cpp:2200` guard; `keyboard.cpp:361-363`; `debugger_window.cpp:533-537` | FIXED and agreed with design-qt (qt-frontend.md §5.3): main-window block independent of `is_enabled()`, debugger-window `WindowShortcut` QActions, `validate_combo` refuses Alt+1..8, SDL router twin, digit swallow stated in §6.4 and the man page, rows H-SCRIPT-01..09 / DKSK-01..02 / HKL-SK-01 |
| R-7 per-WP merges to `main` | owner rule 2026-09-24; arch §10.1/§10.3 name `gh26-dsl` | FIXED: §9 — one branch `gh26-dsl`, per-WP review, one merge, one bump |
| N-1 stale "pending" cell | — | FIXED (§5.1 row 8) |
| N-2 `SOURCE` constants undeclared | — | FIXED: `CPU`/`DMA`/`COPPER` literals in the grammar |
| N-3 `PAGE` on execute undefined | backend Execute payload is `pc` only | FIXED: derived as `page[PC >> 13]` at delivery (§2.3) |
| N-4 `TSTATES`/`TFRAME`, `--script-explain`, v1/v3 | arch §7.5 line 754 | FIXED: `--script-explain` dropped from WP4 (it was already out of §6.6); §5.3 header says v3; `TFRAME` kept as the grammar of record; design-backend aligned `DEBUG-SUBSYSTEM-ARCHITECTURE.md` §7.5 to it |
| N-5 per-function stacks miss non-LIFO interleaving | `.cs:230-234` | FIXED: moved to §4 as a wall; §3(c) says so |
| N-6 manifest lines | `run-unit-tests.sh` exit 2 | FIXED: §8 and every WP row |
| N-7 replay mutation not deterministic | `test06keyb/main.c:64-67` polls every 4 frames | FIXED: `script-replay-edge-func` with a frame-latching demo; `.scr` twin kept for the level-vs-pulse mutation |
| N-8 `PAGE` on overlay hits | `mmu.h:418-450` | FIXED: caveat in §2.3 |
| N-12 exit code 3, N-13 `--map` | — | kept as recommended |
| N-14 recorder header records joystick mode | `joystick.h:184` default 0x40 | FIXED (§7.2 item 3) |
| N-9, N-10, N-11, N-15 | Qt/arch-side or confirmations | not this file's |

CONTESTED: none.

## Appendix D — Review round 2 dispositions (2026-09-26, `scratchpad/reviews/dsl-qt-r2.md`)

| Finding | Verified | Disposition |
|---|---|---|
| R2-1 recorder stamp one frame late; injection vs `tick_auto_type()` order unspecified | `host_key_latch.h:431,445` and `qt_app.cpp:268-280` apply host keys between `run_frame()` calls; `end_of_frame` calls `tick_auto_type()` at `emulator.cpp:9592`; `headless_app.cpp:560-561` queues before `run_frame(N)` | FIXED as proposed: (a) recorder samples at `begin_new_frame(K)` and stamps K−1, stated exact because of the latch, §4 wall kept (§2.6, §7.2); (b) injections applied BEFORE `tick_auto_type()` — REQ-dsl-20 ACCEPTED by design-backend as a CAP-IN ordering contract (backend.md §4.5); (c) pinned by `script-replay-edge-func` (both halves) and `script_events_test` SCRIPT-EV-INJ-ORDER, plus the mutation rows |
| N2-1 man-page paragraph inside the table | — | FIXED (moved below the table) |
| N2-2 row 17 after 18/19 | — | FIXED |
| N2-3 both NR write orders | `emulator.cpp:4749-4770`, `:10221` | FIXED: one sentence in §3(b) |
| N2-4, N2-5, N2-6 | confirmations | no change |

CONTESTED: none.

Round 3 (`scratchpad/reviews/dsl-qt-r3.md`, APPROVE): N3-1 — `FRAME` defined
in §2.4 as the backend's pre-increment tag (backend CAP-INS-07 updated on its
side); N3-2 — verified (`host_key_latch.h:431`; `emulator.cpp:9284-9288`
guard) and folded into §7.2 item 4: recording while paused mid-frame is
inexact, the recorder warns.

## Appendix E — Review round 4 dispositions (2026-09-27, `scratchpad/reviews/dsl-qt-r4.md`)

| Finding | Verified | Disposition |
|---|---|---|
| R-1 script-injected red twin cannot trip an event guard | §2.7 / REQ-21(c): debugger-route writes raise no events | FIXED: rule stated ("inject UPSTREAM of the guest write the guard watches, never the watched write"); example 5 and the §8 row corrupt MMU0 so the demo's own `NEXTREG 0x51` trips `mmu_guard`; the row also pins that injecting at the watched register stays green. (Round 5 R5-1 moved the hook from `@page_in_level` to `@page_in_level_mmu1` — see Appendix F.) |
| R-2 example 4 violates `once`-before-`when` | §2.1 | FIXED: reordered; grammar unchanged (one order); `script_parse_test` row PARSE-ONCE-WHEN-ORDER rejects the other order |
| N-1 deferred CPU NR write commits after the drain | `emulator.cpp:10221` | FIXED: exception stated in the §2.7 visibility table; `on nextreg` is the override point |
| N-2 `PREV_BYTE` does not exist | — | FIXED: REQ-dsl-25 (`prev` on `Mem{Write}`) ACCEPTED (backend v7) |
| N-4 new payload names in `compile_expr`; `HC_ULA`/`CVC` shadowing on copper events | — | FIXED (§2.3, §5.4) |
| N-5 `on dma end` vs the `end` terminator | — | FIXED: parser special case + row PARSE-DMA-END (§2.1) |
| N-6 `is_halt` has no runtime caller; two `Start` sites | `copper.cpp:87` only definition; `dma.cpp:423`, `:574` | FIXED: REQ-dsl-26 ACCEPTED (v7: Halt branch on the stall path with an edge latch; Start = the single START_DMA-while-TRANSFERRING transition) |
| N-7 DMA byte to a port has no `source` | — | FIXED: REQ-dsl-27 ACCEPTED (v7: `Port{Read,Write}.source`) |
| N-9 rows are `.sh` wrappers, fixtures under `nex/`, 1:1 script list | `functional_tests.conf` contract; `scripts/magic-bp-func.sh` | FIXED (WP7: ten wrappers, ten scripts incl. `hostkey` and the two replay ones, fixtures + `.map` under `test/00regression/nex/`) |
| N-3, N-8, N-10, N-11 | confirmations / Qt-side | no change here |

CONTESTED: none.

## Appendix F — Review round 5 dispositions (2026-09-27, `scratchpad/reviews/dsl-qt-r5.md`)

| Finding | Verified | Disposition |
|---|---|---|
| R5-1 injection at `@page_in_level` is overwritten by the guest's own deferred `NEXTREG 0x50` before the watched `NEXTREG 0x51` | `emulator.cpp:10221` (flush after the drain); Execute delivery is pre-instruction (§2.2) | FIXED: hook moved to `@page_in_level_mmu1`, the `NEXTREG 0x51` instruction itself (a MAP label the WP7 demo exports), where the previous instruction's cluster has committed 0x22; example 5 re-walked instruction by instruction in its comment; §8 row adds the second must-stay-green mutation (hooking one instruction early) |
| N5-1 "existing `GuestExecutionScope` gating covers the sites" is wrong at a delivery | `debug_state.h:275` | FIXED: §2.7 states the whole rule body runs under one `InspectionScope` (`debug_state.h:104-115`), the same `guest_access()` gate B2 adds to the `NextReg::write` hook; design-backend aligned §4.2a in both docs (confirmed 2026-09-27) |

CONTESTED: none.

## Appendix G — WP1 as built (2026-09-29)

`src/script/` (target `jnext_script`): `lexer.*`, `parser.*` over `ast.h`,
`names.*` (the one table of reserved words, state names and payload names),
`check.*` (load-time checks and name binding), `evaluator.*`, and the public
`expr_compiler.h`. Suite `script_parse_test` (`gate: none`). Every decision
below is one §2.1 left open; none extends the language.

### G.1 Grammar decisions

| # | Decision | Why |
|---|---|---|
| G1 | **Line ends are whitespace.** §2.1's `NEWLINE` alternative is satisfied by treating every line end as a blank. | No construct needs one: §3 puts several actions on one line (`on frame 0 once do set AUDIO_MUTE = 0b00111 end`) and one action per line elsewhere, and no action or top-level item begins with a token that could continue an expression, so a line end never decides a parse. |
| G2 | **The bracketed accessors are single tokens, spelled as §2.1 quotes them** — `mem[` `mem16[` `phys[` `nextreg[` `mmu[` `page[` `stack[` `changed(` `depth(` — with no space before the bracket. | It is what separates the `addr_spec` keyword `page` from the accessor `page[s]`, and the event `nextreg` from `nextreg[r]`: `on execute page[3]` is an address, `on execute page 3` a page filter. `mem [x]` is a name followed by a bracket, an error. |
| G3 | **Every lower-case word of §2.1 is reserved** (plus the accessor stems): it, the upper-case built-in and payload names, and `CPU`/`DMA`/`COPPER` can be no variable, rule label or snapshot name. `@name` accepts any identifier, keywords included. | Case already separates built-ins from user names (§2); reserving the grammar words keeps `on io_write mask m value v` and friends unambiguous. A MAP symbol is not a keyword. |
| G4 | **All binary operators are left-associative, comparisons included** (`a < b < c` is `(a < b) < c`, not a chain). `not` sits below the comparisons, `&` above them — exactly §2.1's list — so `x & 0x70 == 0x10` is `(x & 0x70) == 0x10`, unlike C. | §2.1 gives levels, not associativity; left is the only reading that makes `10 - 3 - 2` equal 5. §3(f) `line.jds` relies on the `&`/`==` order. |
| G5 | **Juxtaposed expressions** (`dump_mem a n`, `out p v`, `joystick N e`, `log indent n "…"`, `assert e "…"`): the first ends at the first token that cannot continue it. `dump_mem a -1` is therefore `dump_mem (a - 1)` with its length missing; write `dump_mem a (-1)`. | The grammar puts two `expr`s side by side; only a leading `-` can be read both ways, and the greedy binary reading is the conventional one. |
| G6 | **Integer literals**: the prefixes are lower case as §2.1 writes them (`0x`, `0b`, `$`); at most 32 bits, the value being the 32-bit pattern (`0xFFFFFFFF` is -1); a literal running into a letter or digit it cannot use (`0X10`, `0b102`) is an error, not two tokens. | §2.1: integers are 32-bit signed. |
| G7 | **Strings have no escape sequences**: a backslash, a `$` not before `{` and a lone `}` are ordinary characters (a Windows path is written as it is); so a string cannot contain `"`. Inside `${…}` a `"` is an error, and so are `;` and `#` — §2 says no comment form is a comment inside a string. `fmt` is exactly `x2`, `x4` or `d`. | §2.1 says `char` and nothing more; escapes would be an extension. |
| G8 | `hostkey` and `joystick` take an integer **literal** (§2.1 writes `INT`), range-checked at parse time: 1..8 and 1 or 2. | |
| G9 | **Arithmetic**: 32-bit wrapping `+ - * << -x`; `/` truncates toward zero and `%` takes the dividend's sign (INT_MIN / -1 wraps, `%` gives 0); a shift uses the low five bits of its count; `>>` is arithmetic; `and`/`or`/`not` and comparisons yield 1 or 0, `and`/`or` short-circuit. | §2.1 fixes the width and the wrap, not these. |
| G10 | **Snapshot fields** are the §2.5 record: `A B C D E H L F I R AF BC DE HL IX IY SP PC AF2 BC2 DE2 HL2 IFF1 IFF2 IM STACK0 MMU[n] FRAME CYCLE`. Flags and `HALTED` are not fields (not in the record). | §2.5 lists the record and a few examples ending in "…". |
| G11 | **Errors**: `line:column: message`, both 1-based, the column counting characters (UTF-8 code points; a tab is one). The parser stops at the first syntax error; `check_script` reports every load-time error. The loader prefixes the file name. | §6.5 rejects a script with any load-time error whole. |
| G12 | `check_script` checks **one text**. How several `--script` files share labels, variables and snapshot names is WP3's; it can check a merged tree. A snapshot name is not checked against a `snap`: an unsnapped stack is empty at run time (§2.5). | |
| G13 | **Nesting is bounded, and refused past the bound** (review round 1, B1): an expression tree at most **200** nodes tall and the parser's own recursion at most 200 levels (`(`, index, interpolation, `not`, unary), `if` nesting at most **64**. Enforced at parse time by the only producer of trees (`parser.h` `MAX_EXPR_DEPTH`, `MAX_IF_DEPTH`), so the binder, the checker, the evaluator and the destructors — the only recursive passes — are bounded by construction. Past a bound the input is refused, positioned at the operator / `if` / opening token: "expression too long or too deeply nested (limit 200 levels; each chained operator, bracket or nesting counts one)", or "`if` nested too deeply (limit 64)". Siblings are not nesting: a flat script, many rules, many actions and `if`s in a row are unaffected. **A chain of operators is not a list of siblings**: the tree is left-deep, so every chained operator is one level, parentheses or not, and `a or b or …` stops at 199 operators (200 terms) — split a longer condition across rules. (Review round 2 corrected this sentence, which first claimed the opposite.) | Without it a 100 KB `1+1+…+1` (a loop in the parser but a left-deep tree) or 20000 nested `if`s overflowed the stack. Measured cost of the deepest accepted shapes: ~300 KB of stack at -O2, ~450 KB at -O0 (the precedence cascade is ~12 frames per parenthesis level); row DEPTH-STACK runs them all on a 1 MB thread stack. The engine runs on the emulation (main) thread: 8 MB on Linux/macOS, 16 MB reserved on Windows. |

### G.2 The library as built (§5.4)

- `compile_expr(text, PayloadScope, CompileOptions)` → `{dbg::Condition predicate, errors}`. `PayloadScope` converts implicitly from `dbg::EventKind` — §5.4's spelling compiles as written — and defaults to `None`; it refines by the access of a `Mem`/`Port` rule and the sub-kind of a `Copper`/`Dma` rule. A bare kind admits only the names **every** event of that kind carries (a bare `Mem` scope refuses `PREV`, which a read does not carry).
- `eval_expr(text, const Debugger&)` — §5.4 writes `eval_expr(text)`; it needs the facade to read anything.
- `@symbol` resolves **once, at compile time**, through `CompileOptions::symbols` (`eval_expr` uses the backend's table); with no resolver every `@symbol` is refused. §6.5 makes an unknown symbol a load-time error, which a lazy lookup could not be.
- A run-time failure (division by zero, an accessor out of range, a refused `phys[]` page) makes the predicate **false** and is reported to `CompileOptions::on_runtime_error` with the failing node's position; WP3 passes a handler that disables the rule and logs it (§6.5).
- Strings and the script-only forms (a `var`, `NAME.field`, `changed()`, `depth()`) are compile errors in `compile_expr`: a condition is an integer expression. WP2/WP3 compile `when` clauses that read interpreter state with that state in scope.
- The `stop` scope exists for the script checker only; `compile_expr` refuses it (a stop is not a backend event).

### G.3 Name binding

- `PC` is the payload PC in every event rule (the causing instruction, REQ-dsl-12) and the CPU's PC with no event; a `set PC` target is always the register. `HC_ULA`/`CVC` are the Copper step's in `copper` rules only (§2.3) and live elsewhere, a `scanline` rule included. `FRAME` and `CYCLE` are always live.
- Filter bounds and `var` initializers bind with **no event** — they are evaluated at registration — so a payload name there is an error.
- `SOURCE`: the backend's `EventSource` order is Cpu, Copper, Dma; §2.1's constants are CPU 0, DMA 1, COPPER 2. Mapped explicitly (row PAYV-SOURCE).
- `KEY` is N of the host name `scriptN`. `LEN` is the programmed length on `dma start` and the bytes moved on `dma end`. `WAIT_H` is the threshold `(hpos << 3) + 12` the backend carries, as §2.3 says ("hpos threshold") — so §3(f)'s `WAIT(95,${WAIT_H})` prints 428 for `hpos` 52.
- `MACHINE` is 0/1/2/4 for 48K/128K/+3/Next; 3 (Pentagon) is never produced — jnext has no Pentagon machine type any more, and `MachineInfo` carries no timing variant.
- `CYCLE`, `TFRAME`, `FRAME` wrap to 32 bits like every value: `CYCLE` passes 2^31 after about 76.7 s of emulated time (~3800 frames). A difference (`CYCLE - t_int`) stays right across the wrap; an ordered comparison with an absolute `CYCLE` does not. The `on cycle N` filter itself is 64-bit.

### G.4 Findings against the design (not worked around)

| # | Finding | Consequence in WP1 | Resolution needed |
|---|---|---|---|
| F1 | §2.3 lists `SRC DST LEN DMA_MODE IO_SRC IO_DST` for "dma start/byte/end", but the backend fills `dma_is_io_src/dst` on **Byte only** (`Dma::latch_start_` / `latch_end_` set no I/O flag; REQ-dsl-23's Start payload is `{src, dst, len, mode, dir}`), and the Byte payload has no length or mode. | Each name is admitted only where the backend carries it (§5.4: a predicate "can never read a missing payload"). §3(f) `dma.jds`'s `on dma start … ${IO_DST}` therefore **parses but fails the load-time check**; row WORK-3F-DMA pins that exactly this one error is reported. | Either the backend latches the I/O flags on `Start` (a REQ to design-backend), or §3(f) and §2.3 drop `IO_DST` from `dma start`. |
| F2 | §2.1 says the `dma byte` filter is the **destination** range; the backend's `Dma{Byte}` filter matches **either** endpoint (`events.h`, `EventFilter::lo`). The Dma filter also has no page qualifier, though `addr_spec` lets `on dma byte page P` parse. | None (registration is WP3). | WP3: AND a `DST` range condition into the rule's predicate; refuse `dma byte … page` at load time. |
| F3 | The page-only `addr_spec` allows a page **range** (`on execute page 0x20..0x23`), but the backend's `Execute` filter has one `page` qualifier; only `Mem` has a page set. | None. | WP3: refuse a multi-page `execute` filter at load time (one rule stays one subscription). |
| F4 | §3(a): "the parser warns on a `when PAGE ==` over a range wider than one slot". The range is known only after `@symbol` resolution. | WP1 does not warn. | WP3, at registration, where the resolved range is known. |
| F5 | §8.1 asks `script_parse_test` for "one row per event kind proving the rule became exactly one subscription". | Registration is WP3; those rows land with it. | WP3. |
| F6 | An `on stop` rule's `PC` binds to the payload PC, but a stop is not an `Event`. | None. | WP3 supplies it from `PausedInfo`. |

## Appendix H — WP2 as built (2026-09-29)

`src/script/`: `value.h` (the value model), `state.*` (variables and the
snapshot stacks), the evaluator (`evaluator.*`) extended to every form, and the
checker (`check.*`) extended with types and slots. Suite `script_eval_test`
(`gate: none`). Every decision below is one §2 left open; none extends the
language.

### H.1 The value model

| # | Decision | Why |
|---|---|---|
| H1 | **Two types, fixed at load time**: INTEGER (32-bit, wrapping; booleans are 0/1) and STRING. A string is a string literal (its `${…}` evaluated) or `REASON`; it can be compared with `==` / `!=` against another string and interpolated, and nothing else. | §2.1 lists string literals as expressions and `REASON` is a string (§2.3), but no action or builtin consumes a string value, and every expression slot of the grammar (a condition, a `set`, an index, a filter bound, a `var` initializer, `exit`, `dump_mem`, `out`, `joystick`, `press … for`, `log indent`) is an integer. |
| H2 | **Variables are integers.** `var v = "x"` and `set v = "x"` are load-time errors. | No worked script stores a string, and a static type makes every type error a load-time error (§6.5: nothing runs partially) instead of a run-time one. |
| H3 | **`check_script` types every expression** and refuses: a string in arithmetic or under `not`; a string compared with an integer; a string where an integer is needed; a format (`x2`, `x4`, `d`) on a string interpolation. `compile_expr` keeps refusing strings outright (Appendix G, G.2). | |

### H.2 Variables

- A variable's slot is its declaration index; `check_script` binds every reference and `set` target to it.
- **Initializers run once, at load, in declaration order**, in the `None` scope (machine state allowed, payload not), and may read only the variables declared **before** them: a later one is a load-time error "used before its declaration". Rules read any variable, wherever it is declared.
- Values wrap like every integer: `var big = 0x7FFFFFFF + 1` is INT_MIN.

### H.3 Snapshot stacks (§2.5)

- **The record**: the registers (`AF..HL2 IX IY SP PC I R IFF1 IFF2 IM`), the word at SP, the eight MMU slots as `mmu[s]` reads them (NR 0x50+s), FRAME and CYCLE (64-bit, read wrapped to 32) — captured through the inspection surface only.
- **One stack per name**, with a slot in order of first appearance (`Script::snapshots`).
- **Bounded at 4096 entries per name.** `snap` on a full stack, and `unsnap`, a field, `changed()` or `dump_diff` on an empty one, are **run-time errors at the action or expression, leaving the stack unchanged** (§6.5: the engine disables the rule). Dropping the oldest entry was rejected: every later exit would compare against the wrong entry, a silently wrong script. `depth()` of an empty stack is 0, not an error — it is how a script asks (§3(c) does).
- **`changed()` compares exactly §2.5's groups**:
  - `regs`: AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP.
  - `mmu`: all 8 slots.
  - `iff1`: IFF1 only.
  - `stack0`: the word at the *current* SP against the captured word. A moved SP over the same word is unchanged.
- **`dump_diff`** returns one line per differing field, `NAME old -> new`, in the order AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP IFF1 IFF2 IM STACK0 MMU0..MMU7. The values are upper-case hex, 4 digits for 16-bit, 2 for MMU, 1 for IFF/IM. **PC, I, R, FRAME and CYCLE are not compared**: between an entry and its exit they always differ, so they would bury the line that matters.

### H.4 Interpolation

- With no format, or with `d`, a value prints in signed decimal.
- `x2` and `x4` print upper-case hex of the 32-bit pattern, zero-padded to **at least** 2 or 4 digits. A wider value keeps all its digits (`${0x1234:x2}` is `1234`, never a silently truncated `34`), and a negative one prints its pattern (`${-1:x4}` is `FFFFFFFF`).
- A string piece (`${REASON}`) is inserted as it is.

### H.5 Run-time errors (§6.5)

- Every run-time failure throws `EvalError` with the failing node's position. That covers:
  - division or modulo by zero;
  - an accessor out of range;
  - a refused `phys[]` page;
  - a snapshot stack that is empty or full;
  - `MMU[n]` outside 0..7;
  - an `@symbol` left **unresolved** (the script was checked with no MAP): this is an error, not 0;
  - `REASON` evaluated outside a stop delivery: this is an error, not "".
- `make_condition`'s predicate turns a failure into *false* plus a report to its handler. The engine (WP3) catches a failure around a rule body and disables the rule.

### H.6 Recursion

- The new paths are the string comparison, interpolation, snapshot fields with an index, and variable initializers.
- They recurse over the tree only, whose height the parser bounds (Appendix G13). A string node's height includes its interpolations', so the bound holds through them.
- Row SEV-STACK runs each path at the bound on a 1 MB stack.

### H.7 What WP3 consumes

- `ScriptState`: variables (`set_var` is what `set v = …` calls), and `snap` / `unsnap` / `diff` for the three actions.
- `init_vars`.
- `eval_int`, `eval_str`, `evaluate` and `interpolate` for action operands and messages.
- `make_condition` for each rule's `when`.
- An `on stop` rule evaluates with `EvalContext::reason`, plus a synthetic `Event` carrying the paused PC (Appendix G.4 F6).

### H.8 Deviations from the §8 test plan

- §8 plans `script_eval_test` "against a fake inspection surface". `Debugger` is a concrete class in the frozen backend headers, so the suite runs a real `Debugger` over a 48K machine instead.
- "Division by zero disabling the rule" is the engine's behaviour (WP3). WP2 pins that the error is raised and reported, positioned.

## Appendix I — WP3 as built (2026-10-01)

`src/script/script_engine.*` (`ScriptEngine`, `EngineHost`, `LoadResult`), one
evaluator change (I.5), two backend fixes (I.4 F1, F8). Suite
`script_events_test` (`gate: none`, both configurations), which drives the
engine directly on real 48K and Next machines; the CLI that loads scripts is
WP4.

### I.1 The engine

- **One engine is one backend client** (`ClientKind::Script`) and is its own
  `Listener`. Every loaded script is a unit with its own variables, labels and
  snapshot stacks; scripts load in order, and rules on one event run in file
  order, then across files in load order (§2.2).
- **`load()` is atomic** (§6.5): parse, check (symbols from the backend's
  table), run the `var` initializers, evaluate every filter bound — a bound may
  use a `var` or an `@symbol` and is range-checked here — and only then
  subscribe. Any error, including a backend refusal, registers nothing.
- **A rule is ONE subscription** (F5), carrying the rule's `once` and enable
  flag, a `Handler` that runs the body and returns the verdict, and a
  `Condition` exactly when the rule has a `when` or the engine refines the
  filter (I.2). The one exception is F3's page range.
- **What runs where**: event rules in the backend's delivery (machine stopped
  at the boundary, under its `InspectionScope`); `on stop` rules in
  `on_paused()`, i.e. from `pump()`; deferred actions per I.3.
- **`EngineHost::exit`** is the loop owner's: `exit n` and the run-time-error
  exit 1 go there. Empty means the GUI, where `exit` logs and pauses.
- **The log sink is `Debugger::log`** — the debugger channel plus every
  listener's `on_log` (§2.6 names a `script` spdlog channel; the backend appends
  ` [client N]`). WP4 may route it.

### I.2 Registration

| Rule | Subscription |
|---|---|
| `execute A..B [page P]` | `Execute`, `lo..hi`, optional `page` |
| `execute page P1..P2` | **one `Execute` per page** over 0..0xFFFF, at most 16 (F3) |
| `read` / `write A..B [page P]` | `Mem`, access Read / Write, range, optional `page` (the AND form) |
| `read` / `write page P1..P2` | ONE `Mem` whose page SET is P1..P2 — the backend's filter, never a `PAGE ==` predicate |
| `io_read` / `io_write P` | `Port`: mask `0x00FF` for P ≤ 0xFF (GH #222), else `0xFFFF` |
| `io_* mask M value V` | `Port`, as written |
| `io_* P1..P2` | `Port` matching every port, plus the engine's inclusive range condition — on the LOW byte when the range lies in 0x00..0xFF, exactly otherwise, as a single port decodes (GH #222). A range straddling 0xFF is a load error |
| `nextreg R1..R2` | `NextRegWrite`, register set |
| `frame [N]` / `scanline N` / `cycle N` | `Frame` (N or every) / `Scanline` (0..1023) / `Cycle` |
| `interrupt` / `nmi` / `reset` | `IntAck` / `Nmi` / `Reset{Any}` |
| `hostkey N` | `Host`, name `scriptN` |
| `copper move [R1..R2] / wait / halt [at A..B]` | `Copper`, one sub-kind, register set, Copper-PC range 0..1023 |
| `dma start / byte [A..B] / end` | `Dma`, one sub-kind; a `byte` range pre-selects in the backend (either endpoint) and the engine's condition keeps the DESTINATION (F2); `dma byte … page` is a load error (no page filter) |
| `stop` | none |

The §3(a) warning (F4) is raised here, where the bounds are resolved: a `PAGE ==`
or `PAGE !=` test in the `when` of a `read`/`write` range that spans more than
one 8K slot and has no page qualifier. It is positioned at the comparison and
logged as `SCRIPT WARNING file:L:C: …`; the script still loads.

### I.3 Actions

- **The frame edge (§2.6).** `press` / `release` / `press … for` go straight to
  the backend (IN-01/IN-02), which queues them for the edge itself; `screenshot`
  is the backend's (CAP-01, next rendered frame). `joystick` (IN-03 is
  immediate) and `compare_scr` are **queued by the engine** and applied by its
  own `Frame` subscription, made on first need — or at once when issued from a
  `frame` rule, which already runs at the edge. `compare_scr` logs the first
  differing offset (or the size), then `ASSERT FAILED: msg`, and stops.
- **`save_snapshot` waits for `on_frame_ended()`**: the backend refuses a save
  inside a delivery that would have to run the frame out, so the engine writes
  it from the first `pump()` that finds the machine AT a frame boundary — not
  while a stop holds it inside a frame, which would move the user's machine. A
  failed write is a run-time error.
- **`press … for n`** is the backend's auto-type pulse, the one
  `--delayed-keypress-frames` uses: pressed by the tick at the issuing edge,
  released by the n-th tick, so the guest sees it for n−1 frames (backend row
  IN-01-07 pins the same span). §2.6's "down for n frames" counts ticks.
- **`log indent n`** clamps n to 0..255. **`dump_mem`** refuses a length over
  4096 at run time.
- **`once` is the rule's**: its first firing spends every subscription of the
  rule (an execute page range has several). **`enable`** re-arms a spent
  `once` by registering the rule afresh — the backend never re-arms one
  (EVT-EXEC-32). A rule a run-time error disabled
  stays disabled; `enable` does not revive it.

### I.4 Stop, exit, errors

- `stop` / failed `assert` return `Stop`; the backend pauses at the boundary and
  applies the loop owner's SES-04 policy, so headless is exit **3** with no
  engine involvement. The engine logs `SCRIPT STOP: <reason> at PC=<the event's
  PC> FRAME=… CYCLE=<the event's cycle>`.
- **`exit n`** calls `EngineHost::exit(n)` during the delivery, BEFORE the
  backend's stop requests 3 — the loop owner keeps the first code it is given
  (row SCRIPT-EV-EXIT pins the order). **Superseded by WP7 review 1
  (Appendix M.2):** the `exit` is no longer handed over during the delivery but
  at the pause, in `on_paused()`, once every stop of the boundary is known, and
  the loop owner's listener leaves the backend's 3 to that hand-over while an
  exit is pending. A failure at the same boundary turns `exit 0` into its code. An `exit`
  inside an `on stop` rule never changes the status: the pause that ran it already
  decided it (a stop's 3, an `exit`'s own code; SCRIPT-EV-EXIT-IN-ON-STOP). Before that it calls
  `flush_captures()`: a screenshot still pending or failed, or a
  `save_snapshot` still queued, turns `exit 0` into exit 1, logged.
- **`REASON`** is the rule's own text for the engine's own stop (the backend
  names it Breakpoint / Watch / Script by event kind, with empty text), and
  `user`, `breakpoint`, … plus the backend's text otherwise. An `on stop`
  rule's `PC` is the paused PC (F6), carried on a synthetic `Event`.
- **Run-time errors** disable the rule's subscriptions, log `SCRIPT ERROR
  file:L:C: msg — rule X disabled`, and call `EngineHost::exit(1)` once, at the
  next frame edge.
- A mutation that does not wholly land is a run-time error, never silent:
  `poke(Cpu)` returns `RefusedReadOnly` with the count that landed (GH #281),
  reported as `N of M byte(s) landed`; the bytes that did land stay.
- The ring overflow (§2.2) is logged once per boundary.

### I.5 Name binding — supersedes G.3 for `CYCLE`

**`CYCLE` in an event rule is the event's own cycle**, captured at the hook;
outside one (a `var` initializer) it is the live clock. G.3 made it always
live, and the §3(f) `latency.jds` acceptance script then always printed 0: the
IntAck and the `execute 0x0038` it measures are delivered at the same
boundary. With the payload cycle it prints the acknowledge's 104 master cycles
(13 T at 3.5 MHz), and the design already says so (§2.2 "the payload carries
the exact cycle", §5.1 rows 1 and 6). The `[jds F: C:]` log stamp uses the same
value. `FRAME` stays live.

### I.6 Findings

| # | Finding | Resolution |
|---|---|---|
| F1 | §2.3's `IO_SRC`/`IO_DST` on `dma start` had no backend payload. | **Fixed in the backend** (manager decision): `Dma::latch_start_` sets both flags in the block's direction. Rows PL-DMA-IO-01..03, SCRIPT-EV-DMA-START-IO. `events.h` still documents the flags as `Dma{Byte}` only — a frozen-header comment, reported, not edited. |
| F2 | The `dma byte` filter is the destination (§2.1); the backend's matches either endpoint. | The design is the contract: the engine ANDs a destination condition (row SCRIPT-EV-DMA-DST); `dma byte page` is a load error. |
| F3 | `execute page P1..P2` against a one-page Execute filter. | One subscription per page, at most 16, all owned by the one rule. |
| F4 | The §3(a) warning needs resolved bounds. | Raised at registration (I.2). |
| F5 | One subscription per rule, pinned per kind. | The SCRIPT-EV-REG-* rows, in `script_events_test` rather than `script_parse_test` (registration needs a backend). |
| F6 | `on stop`'s `PC`. | The paused PC (I.4). |
| F7 | §3(f) `copper.jds` asserted `CVC == 96` for a WAIT on line 95. A WAIT is satisfied when the Copper's OWN line counter equals its vpos (`copper.vhd:94`; `Copper::execute`, `copper.cpp:199-201`), so its payload `CVC` is 95 by construction; GH #181's "the following line" is the RAW line. As written the script always stopped. | **§3(f) corrected** (review round 1) to `assert CVC == 95 and HC_ULA >= WAIT_H`; row SCRIPT-EV-WORK-COPPER runs the corrected script verbatim and pins that it passes, and that the old assert would stop. |
| F8 | The `NextRegWrite` a Copper MOVE fans out to had `prev` = 0 (the site never peeked), so §8's post-commit contract failed for the Copper. | **Fixed in the backend**: the Copper site peeks the register before the write and the drain carries it. Rows PL-NR-COPPER-PREV, SCRIPT-EV-COPPER. |
| F9 | `CYCLE` always live (G.3) defeats `latency.jds`. | I.5. |
| F10 | §2.7's MUTATE line reads `by script:<rule>`; the backend writes `by <client id>` and knows no rule. | Reported to the backend owner; the engine does not re-log mutations. |

### I.7 For WP4

- The loop owner implements `EngineHost::exit`, keeping the FIRST code.
- §7.3's "a headless run whose scripts declared a `compare_scr`/`exit` that
  never fired exits 3 with `SCRIPT: N deferred actions never ran`" is the loop
  owner's check at the `--delayed-automatic-exit*` bound.
- A menu-loaded script (§6.4) registers from the next frame boundary; `load()`
  itself registers at once.

### I.8 Deviations from the §8 test plan

- §8.3 plans the program "through the `--inject` route"; the rows write it into
  RAM and drive the engine directly, as the CLI is WP4.
- The per-kind registration rows §8.1 assigns to `script_parse_test` are
  `script_events_test`'s SCRIPT-EV-REG-* (F5).

## Appendix J — WP4 as built (2026-10-01)

The CLI rows of §6.6, the loop owners' script host, the man page. Suites:
`script_events_test` (SCRIPT-HOST-*), `cli_options_test` (CLI-SKEY-01/02), and
six regression rows `script-{pass,assert,stop,load-error,key,frontends}-func`.

### J.1 The options

| Flag | As built |
|---|---|
| `--script FILE` | repeatable, loaded in the order given into ONE engine (one backend client) |
| `--script-key FRAME N` | FRAME 0..2^31-1, N 1..8, both whole (`cli::parse_script_key`); `--headless` only — the windowed frontends refuse it at startup as they refuse `--delayed-keypress`; needs a `--script` |
| `--map FILE` | a z88dk `.map` into the backend's one symbol table (CAP-SYM), loaded BEFORE the scripts so `@symbol` resolves; a file that cannot be loaded or holds no symbols is a startup error |

### J.2 `ScriptHost` (`src/script/script_host.*`)

- **Where it lives.** In `src/script`, not `src/platform`: it needs only the
  `Debugger` and the engine, so its rows run in `script_events_test` with no
  frontend. Each loop owner holds one beside its `DebugServers`, declared after
  `debugger_`, and starts it right after the servers.
- **Startup.** Any failure — an unreadable file, a load-time error in any
  script (§6.5), a bad `--map`, `--script-key` with no `--script` — is logged
  (`SCRIPT ERROR file:line:column: message`, then `--script FILE: not loaded`
  and `--script: not starting; no script is loaded (exit 1)`) and fails the
  start; the loop owner exits 1 before the machine runs. Nothing of any script
  stays registered, a good one loaded before the bad one included.
- **No scripts, no client.** With nothing to load it attaches nothing and arms
  nothing (row SCRIPT-HOST-NONE).
- **The exit code (§6.3).** The FIRST code a run reaches is kept: the engine's
  `exit n` and run-time-error 1 through `EngineHost::exit`, and the backend's
  `ExitRequested` 3 through the host's own listener — a NON-ARMING client
  (`ClientInfo::observer`), because the listener only listens. The headless and
  SDL loops check it after every pump; a 0 never replaces an earlier failure.
- **Qt.** Started with `exits = false`: no exit hook and no listener, so
  `exit` and `stop` pause (the stop policy there is `Pause`). A load error is
  still a startup failure, logged; the dialog §6.5 describes is WP5's.

### J.3 `--script-key` timing

A scheduled key is raised at the EDGE of frame FRAME (`E_FRAME`, where
`on frame FRAME` fires), from the engine's own frame-edge subscription, BEFORE
the edge's deferred queue, so a `hostkey` rule runs with `FRAME == FRAME` and
what it queues for the edge lands at that same edge. Raising it from the loop
between frames was rejected: `Debugger::time().frame` there is the frame just
completed, so the rule would have seen FRAME − 1, and the timing would have
depended on which loop owner raised it.

### J.4 The watchdog (§7.3)

At the `--delayed-automatic-exit*` bound the headless and SDL loops ask
`unreached_verdicts()`: rules holding `exit` or `compare_scr` (in any `if`
branch) that never fired, deferred actions still queued, and scheduled keys
not yet delivered. A non-zero count logs `SCRIPT: N deferred actions never ran`
and exits 3 unless the run had already failed. A rule with no verdict (a guard)
never counts: a guard that never trips is a pass.

### J.5 Deviations

- The man page's SCRIPTING section does NOT state the Alt+1..Alt+8 keyboard
  change §6.6 names: that change is WP5's and is not in the product yet. It
  lands with WP5.
- The user guide links the man page's SCRIPTING section until WP8a writes the
  guide's own chapter (`tools/gen-userguide-cli.pl`).
- The GH #26 ChangeLog line is reworded now that scripts load from the CLI; it
  is to be checked again at the final merge.

## Appendix K — WP5 as built (2026-10-01)

The GUI half of §6.4 and qt-frontend.md §5.3: the Script tab and menu, and
Alt+1..Alt+8 as the script host keys in both windows. Rows: QSCR-01..07
(`debugger_panels_test`), H-SCRIPT-01..09 (`host_hotkey_test`), DKSK-01/02
(`debugger_keymap_test`), DK-32..35 and DK-68 (`app_config_test`), HKL-SK-01..04
(`host_key_latch_test`), SCRIPT-HOST-GUI-* (`script_events_test`), and DACC-05's
pinned menu shape (`debugger_accel_test`).

### K.1 The host keys

- **One implementation for both windowed frontends: the key `Router`**
  (`host_key_latch.h`), which the Qt emulator window and the SDL window both
  feed. §5.3 placed the Qt half in `MainWindow::keyPressEvent` and the SDL
  half in the Router; the Router alone serves both, with the same properties —
  independent of the debugger being open (the Router is upstream of nothing
  the debugger gates), swallowed press AND release, consumed with no script
  loaded — and the two frontends cannot drift. `wire_script_keys()`
  (`host_key_wiring.h`) binds it to `Debugger::raise_host_event("scriptN")`
  in `QtApp` and `SdlApp`, one spelling of the name.
- **The chord is exact**: Alt (left or right) and no Ctrl / Shift / GUI held.
  Ctrl+Alt+N, Shift+Alt+N, Alt+9, Alt+0 and the bare digits are unchanged.
  The digit's release is swallowed even if Alt went up first; an autorepeat of
  a held chord raises nothing; `release_all()` forgets a chord whose key-up
  went to another window.
- **The debugger window**: eight `Qt::WindowShortcut` `QAction`s, not menu
  items, each raising the same event, with auto-repeat off (a held chord
  raises once there too — review round 1 found it raising per autorepeat); `validate_combo` refuses `Alt+1..Alt+8`
  by name ("Alt+1..Alt+8 are the script host keys"), so a saved binding there
  is a `LoadIssue` and keeps its default.
- **SDL parity**: full — the SDL window's key callback forwards Alt+digit to
  the same Router (it filters only F-keys and the pointer release).

### K.2 The Script tab and menu

- `ScriptPanel` (`src/debugger/script_panel.*`) drives the loop owner's
  `ScriptHost` (`QtApp` → `MainWindow::set_script_host` →
  `DebuggerManager::set_script_host`) — the one `--script` loads into, so CLI
  scripts are listed too. It includes no core header (QTF-09..13 stay green).
- **Script menu** (Alt+S on the debugger window's menu bar): Load Script…,
  Reload Scripts, Unload Scripts. §6.4 named "Load Script…, Unload Scripts"
  in a Debug menu; a menu of its own keeps the Debug menu's mnemonics as they
  are, and Reload was asked for by the WP5 brief.
- **What it shows**: per rule — file, label (or `line:column`), the event with
  the filter as registered, the state (`armed` / `disabled` / `spent (once)` /
  `error (disabled)`, plus `verdict not reached`), hits; the verdict line from
  `ScriptEngine::status()` — `PASS: exit 0` / `FAIL: exit n` (the machine
  paused, the GUI never exits), stops with the last reason, run-time errors,
  verdicts not reached; and the script log, the last 2000 lines the engine's
  client logged (the backend's `[client N]` tag removed), the `MUTATE` lines
  included, plus the host's own load errors.
- **ScriptHost gained a GUI API**: `load_file()` (registered at once; a
  script with an error registers nothing and the loaded ones stay),
  `unload_all()` (which destroys the engine: its client arms the machine, and
  a GUI with nothing loaded must not keep every instruction paying for it),
  `reload()`, `files()`, `log_since()`. Its listener client (non-arming) now
  exists in the GUI too, to collect the log.
- **`ScriptEngine`** gained `status()` and three `RuleView` fields (`spent`,
  `verdict`, `event`).

### K.3 Deviations

- **FRAME stays absolute for a menu-loaded script.** §6.4 says a menu-loaded
  script's `FRAME` 0 is "the first full frame after loading"; §2.4 says the
  same for any script ("Frame 0 is the first frame executed after the script
  is loaded"). WP3 made `FRAME` the backend's frame number (Appendix I), which
  for a CLI script at power-on is the same thing. Making it relative per load
  would split `FRAME`, `on frame N`, `--script-key`, the recorder's stamps and
  the `[jds F:]` log stamp across two origins, so it stays the machine's frame
  number, and the load note says so (`SCRIPT … loaded at FRAME n (FRAME and
  \`on frame N\` count the machine's frames)`), as do the man page and the
  user guide. A script that wants a relative frame captures `FRAME` into a
  `var` in a `once` rule.
- **Registered at once, not "from the next frame boundary".** The Qt tick
  loads between frames (or with the machine paused), which is a boundary.
- **No regression row for the GUI path**: §8's `script-hostkey-func` is WP7's,
  headless. The GUI path is pinned in the Qt unit suites above (offscreen,
  real windows, the real key Router and the real backend).

### K.4 Breakpoints other clients can see (Z review finding, 2026-10-01)

Every rule used to be registered as a static `Continue` plus a handler, the
handler deciding `stop`. A client's own stepping loop — ZRCP `run n`, or any
like it — asks which subscriptions would stop at a PC (`probe_execute` +
`subscriptions()`), and a probe cannot run a handler (it might mutate), so it
ran past every script breakpoint.

- **A stop-only `execute` rule** (body exactly `stop ["msg"]`) is now
  registered as a static `Stop`, its `when` as the subscription's condition,
  no handler — the shape every frontend breakpoint has. `probe_execute` lists
  it and `subscriptions()` reports `action Stop`, no handler.
- **Its bookkeeping moved to `on_paused()`**, driven by `PausedInfo::matched`
  (every stop of the boundary, whoever's was first): the hit, `once` (every
  subscription of the rule spent — a page range has several), the
  `SCRIPT STOP: <msg> at PC=… FRAME=… CYCLE=…` line, the stop count and last
  reason (`status()`), and `REASON` for the `on stop` rules. The message is
  interpolated on an Execute event at the paused PC — the boundary the rule
  fired at, nothing executed since — so it reads what the handler read.
  Exit 3 under `StopPolicy::ExitNonZero` is the backend's, unchanged.
- **Still invisible to a probe**: every rule that does more than `stop` (a
  `log` before the stop, a mutation, `snap`, a conditional `stop` inside
  `if`, `assert`, `exit`, `compare_scr`) and every non-`execute` rule. Those
  keep a handler, because only running the body knows the verdict. A script
  author who wants a breakpoint another client honours writes it stop-only and
  puts the condition in `when`. Mem / Port stops need no probe: they are
  delivered after the instruction, which no stepping loop pre-empts.
- Rows: SCRIPT-EV-STATIC-STOP, -WHEN, -ONCE, -PAGES, -TWO, -MIXED, SCRIPT-HOST-STATIC-EXIT3.
  `probe_execute` is still the `bool` form on this branch (it ignores
  conditions); the condition-evaluating `std::vector<EventId>` form arrives
  with Z, whose `other_breakpoint_at()` already reads `action == Stop &&
  !has_handler` — the shape these rows pin.

## Appendix L — WP6 as built: the recorder, #20 (2026-10-01)

Where this differs from §7 it says so; §7 is the design, this is the code.
Code: `src/script/recorder.*`, `src/script/key_names.*`, the recorder half of
`ScriptHost`, `src/platform/recording_info.h`, `ScriptPanel` /
`DebuggerWindow`'s Script menu. Rows: `script_record_test` (REC-*),
QSCR-11..13, DKSK-05, and the functional rows `script-replay-keyb-func`,
`script-replay-joystick-func`, `script-record-replay-func`.

### L.1 What it records, and when

- **One backend client** (`ClientKind::Script`, arming): a `Frame`
  subscription (every frame) and a `Host` one (`script8`), plus a listener
  for `Paused` and `Reset{Hard}`. It only observes: nothing it does changes
  the machine.
- **At every frame edge E_K** it reads INS-16 `input_state()` and writes one
  edge per change since E_K-1 — a matrix bit (`press` / `release`), an
  extended key (`ext:<name>`, L.2), a connector's 12-bit state (`joystick 1|2
  0x…`, the whole new state) — stamped **`on frame K-1`**, as §7.2 item 1 says.
  Level form only; there is no `for`. The sample is taken at E_K rather than
  at `begin_new_frame(K)`: the two read the same input, because a running
  machine takes host input only between frames, and E_K is a backend event
  where `begin_new_frame` is not. E_K's `Frame` delivery comes before the
  edge's injection drain and `tick_auto_type()` (`emulator.cpp`, B4), so a
  change applied AT E_K-1 (a script `press`, an auto-type step) is first seen
  at E_K and stamped K-1 like a host key.
- **A capture** (host key 8 — Alt+8 in either window, `--script-key F 8` —
  or `capture()` from the menu / tab) is taken at the NEXT frame edge E_K, and
  stamped K: a replay's `on frame K` rule runs at that same point, so what is
  compared is, by construction, what was captured. Taking it at once instead
  was rejected: between frames, and above all while paused mid-frame, there is
  no frame a replay could take it at. A capture asked for while the machine is
  paused at a frame boundary is therefore taken one frame after resuming.
- **`.scr` or PNG** (§7.4): `.scr` (`ula_screen_dump()` written by the
  recorder at E_K, and `compare_scr` in the script) when only the ULA is on —
  NR 0x68 b7 clear, NR 0x15 b0 (sprites) and b7 (LoRes) clear, NR 0x69 b7
  (Layer 2) clear, NR 0x6B b7 (tilemap) clear, read with `nextreg_peek()`,
  i.e. the live values. Otherwise a PNG through CAP-01 `screenshot()` (deferred
  to the next rendered frame, as a replay's `screenshot` is) and, in the
  script, `screenshot "<base>-NNNN-replay.png"` with a comment naming the
  reference. The DSL has no PNG comparison (§7.4 puts it in the suite), so a
  PNG replay's verdict is the caller's: the functional rows `png_diff` them.
- **The header**: `jds-recorder: 1`, machine, program (`config().load_file`
  of the CURRENT boot), `--rtc`, the SD image's base name and `.jns` Tier-1
  identity, NR 0x05, the frame range and counts, and the replay command line.
  The preconditions are `once` asserts at the first recorded frame: `MACHINE`
  and `nextreg[0x05]`. The script ends `on frame <last>+2 do exit 0 end` — two
  frames, so a PNG asked for at the last edge has been rendered and written.
- **Inexact, said in the script** — a `# WARNING:` comment and the same text
  as a `log` at the first frame, so a replay prints it: a change sampled at the
  edge of a frame the machine was paused in mid-frame (§7.2 item 4; flagged
  per FRAME, from the `Paused` push with `at_frame_boundary()` false — the
  backend does not say whether the key moved during the pause or before it,
  so this errs towards warning); input held when recording began; input before
  frame 0; a frame tag going backwards (a rewind).
- **A cold boot** (`Reset{Hard}` — a hard reset, a menu load, which is one)
  restarts the recording at FRAME 0 and re-reads the header's facts: a replay
  starts at power-on too, so nothing recorded before it could be replayed.
  This is what makes "Record, then File > Load" work. Not in §7.
- **`stop()`** calls `flush_captures()`: a PNG still pending (asked for at the
  last edge before the stop) is dropped by the backend, and its line goes with
  it, with a warning. The script is written whole at the stop (not streamed),
  so the warnings can lead it.

### L.2 Findings against the design (not worked around)

- **§7.2 named the matrix and the joysticks but not the 16 Next EXTENDED keys**
  (NR 0xB0/0xB1, `InputState::ext_keys`). The host's arrows, Backspace, Esc,
  the Alt-letter keys drive those, not the matrix (issue #33), so a recorder
  that ignored them would drop real input silently. The language gained the
  key names `ext:right … ext:delete` for `press` / `release` (backend IN-02's
  `set_extended_key`, already there); `press "ext:…" for n` is a run-time
  error (there is no extended-key pulse). §2.1's `key_spec` and §2.6's
  `press` / `release` rows carry the names. The names live in
  `src/script/key_names.*`, with the inverse matrix table (§7.2's "`row,col`
  for a bit with no single-key name" — CAPS SHIFT `0,0`, SYMBOL SHIFT `7,1`).
- **Keys jnext types by itself are input like any other.** A tape `--load`'s
  `LOAD ""` and `--delayed-keypress` pulses change the matrix, and the
  recorder records them; a replay given the same options types them twice.
  INS-16 cannot tell them apart and the backend has no "auto-type active"
  query, so it is documented (man page) rather than filtered.
- **Not recorded at all**: the Kempston mouse (not in INS-16), media changes
  (a tape inserted, an SD card swapped, GH #93), a soft reset from the GUI
  (`Reset{Soft}` is also what a guest NR 0x02 write raises, so it cannot be
  warned about without false alarms), and a loaded script's mutations.

### L.3 Where it is reached (deviations)

- **`ScriptHost`** owns the recorder: `start_recording()` /
  `capture_screen()` / `stop_recording()` / `recording()` / `recorder()`, and
  `set_recording_info()`, a provider each loop owner sets to
  `recording_info_of(emulator_)`. The host's destructor stops and writes, so
  any exit writes the script. Its log lines reach the Script tab like the
  engine's.
- **`--record-script FILE`** — NOT in §6.6 or §9. Added because the SDL
  frontend has no menus, so it is the only way to record there, and because it
  makes the recorder testable end to end through the binary
  (`script-record-replay-func`) and from a real GUI session driven by xdotool
  (`test/scripts/dsl/record-dapr.sh`). With it, `--script-key` no longer needs
  a `--script` (key 8 is the capture).
- **The GUI affordance is the Script menu and tab, not the Debug menu** (§9
  said Debug menu "Record Script…"): Script > Record Script… / Capture Screen /
  Stop Recording, enabled by state when the menu opens, and the same three as a
  row of buttons in the Script tab with a status line — beside the Load /
  Reload / Unload of K.2, which is where a user who records a script looks for
  it.
- **Alt+8 is the capture while recording**, as §7.2 says; it is still raised
  as host key 8, so a loaded `on hostkey 8` rule runs too.

### L.4 The parked DAPR rows, and what else records

- **`06-dapr-keyb` and `07-dapr-joystick`** are recorded once from the Qt GUI
  under Xvfb with `--record-script`, driven by xdotool
  (`test/scripts/dsl/record-dapr.sh`, which re-records), and committed as
  `test/scripts/dsl/dapr-{keyb,joystick}.jds` with their PNG captures. Both
  programs draw on the tilemap, so the captures are PNGs. The rows
  `script-replay-keyb-func` / `script-replay-joystick-func` replay them
  headless and require `exit 0` and every capture pixel-identical. The keyb
  recording holds W across CAPS SHIFT, presses EDIT (CAPS + 1) and holds E
  with R (the §8 "press as a pulse" mutation strands or drops one of those);
  the joystick one uses `--joy1-source keys` (diagonals, fire, a direction held
  across another's release). `test/interactive/README.md` keeps them listed
  for a manual run and points at the rows. **`script-replay-keyb-func` is
  therefore WP6's, not WP7's**: WP7's ten rows become nine plus this one.
- **RZX** (§7.3) stays as it is: it records the IN *results* and replays them
  by overriding every IN, inside its own snapshot; the recorder records the
  input *state* and lets the emulator compute the INs, which is what a test of
  the input path needs. They can run in the same session; neither replaces the
  other.
- **`--tape-save`** records what the guest SAVEs (the ROM's SA-BYTES), not
  what the user does: a recorded session that SAVEs, replayed with the same
  `--tape-save`, writes the same `.tap` again — a comparison the caller can
  make. The recorder neither replaces it nor needs it.

### L.5 Deviations from the §8 test plan

- `script-replay-edge-func` (a demo that latches the FRAME of its first key)
  needs WP7's `dsl_demo`; it stays WP7's. The K-1 stamping it pins is pinned
  here by `REC-EDGE-*` (exact frame numbers) and by the round trip
  `REC-RT-GUEST`, whose guest counts loop passes before Q is first seen down —
  a number that moves with the frame the press lands on.

## Appendix M — WP7 (and WP10) as built: the DSL demo and script suite (2026-10-01)

WP7 (§9) and the overview's WP10 are one deliverable, built as one package.
`script-replay-keyb-func` is WP6's (L.4), so WP7 has **nine** rows; together
with it they are §9's ten.

### M.1 What was built

- **`demo/dsl_demo/dsl_demo.asm`** — one z88dk `+zxn` NEX, written in
  assembly so every address a script names is a MAP label. `make` builds
  `dsl_demo.nex`, `dsl_demo_buggy.nex` (the same source with `BUGGY`) and
  `dsl_demo.map`. `make install` copies the three to `test/00regression/nex/`.
  **One MAP serves both builds:** each `BUGGY` difference is an operand of the
  same size, or an instruction of the same length writing a different target.
  The Makefile refuses to build if the two maps disagree on any `addr` line.
  Every frame, under IM 2, the program:

  | Feature | Good build | `BUGGY` build |
  |---|---|---|
  | Keyboard poll | latches the frame counter at the first Q (`first_key`) | same |
  | Data area | writes `data_area` | same |
  | `mempoint_addr` | written, never 0xB7 | 0xB7 at its frame 20 |
  | `patch_byte` → `patch_copy` | copied | same |
  | `trap_insn` | runs | runs, plus a second, unskipped write after it |
  | Stray write | none | one into `__data_crt_head - 1`, the last byte of the guarded range |
  | MMU0/MMU1 paging (every 8 frames) | `page_in_level` / `page_in_level_mmu1` | MMU1 = 0x24 |
  | DMA sprite-pattern upload to port 0x5B (every 16 frames) | 256 bytes | 128 bytes |

  The IM 2 handler (`isr` .. `isr_exit`) counts `frames`. It carries five
  faults, one per invariant of the interrupt-exit audit, picked by the byte
  `isr_fault`: 0 in the good build, 1 in the `BUGGY` one, any other value set by
  a script (WP7 review 1). 1 returns IY incremented (a constant clobber would
  be invisible after the first entry); 2 leaves MMU slot 7 on page 0x0F; 3
  rewrites the return address; 4 returns without EI; 5 leaves SP two bytes
  deeper. The stack and the IM 2 table live in slot 5, so slot 7 is free for
  fault 2. The Copper runs `MOVE NR 0x43,0x00; WAIT 95;
  MOVE NR 0x43,0x02; HALT`; in the `BUGGY` build the WAIT is for line 96.
- **`test/scripts/dsl/`** holds the nine scripts. Each header gives the exact
  command line, the exit code and the log line. The rows are thin
  `test/00regression/scripts/script-*-func.sh` wrappers. They take the §8 names
  (the mutation table's), not `script-<script name>-func`:

  | Script | Row | Red twin |
  |---|---|---|
  | `range_watch.jds` | `script-guard-func` — **#279** code-area guard, `0x8000..(@__data_crt_head - 1)` as ChaseTheBug | buggy build: exit 3, the write into 0x8317 from PC 0x8172 |
  | `value_predicate.jds` | `script-mempoint-func` | buggy build: exit 3 |
  | `nextreg.jds` | `script-mmu-func` — **#279** MMU0/MMU1 | buggy build: exit 3, PC 0x818B. Also the good build with a script fault upstream of the watched write (3), and the two "must stay green" variants (watched register, one instruction early; both 0) |
  | `span_invariants.jds` | `script-isr-func` — **#279** interrupt-exit audit | each of the five handler faults: exit 3 with its own line — IY named; MMU7 named and "isr changed an MMU slot"; "top of stack modified"; "isr exit with interrupts disabled"; SP named |
  | `copper.jds` | `script-copper-func` | buggy build: exit 3, split on line 96 |
  | `dma.jds` | `script-dma-func` | buggy build: exit 3, a 128-byte upload |
  | `mutation.jds` | `script-mutation-func` (requires the MUTATE lines) | buggy build: exit 3, its `exit 0` not taken |
  | `hostkey.jds` | `script-hostkey-func` (`--script-key 560 1`) | buggy with the key: 3; buggy with no key: 0 (nothing armed); good with the key: 0 |
  | `replay_edge.jds` | `script-replay-edge-func` | script: Q pressed in the program's frame 30 is first seen in 31. A real SDL window under Xvfb records a held Q (through the key Router, xdotool); the recording must press it at F-1 (F = the FRAME the program latched it in) and replay to the same latch. Control: the press a frame later latches a frame later |

### M.2 Findings

- **A defect: `assert …; exit 0` passed a failed assert.** `exit` handed its
  code to the loop owner during the delivery, before the backend's stop asked
  for 3, so the first code — 0 — won. §3(f)'s own `palette_init.jds` would have
  gone green on a failed assert. The same held for `compare_scr …; exit 0`, for
  a stop in ANOTHER rule at the same event, and, in an event rule, a deferred
  `compare_scr` was never made because the `exit` came first (review round 1).
  Fixed by class in `ScriptEngine`:
  - an `exit` is no longer handed over during its delivery: it is recorded
    (`take_exit`), the machine pauses, and `on_paused()` hands it over once
    every stop of the boundary is known (`hand_over_exit`), the loop owner's
    listener leaving the backend's 3 to it while an exit is pending. The
    backend's first `pump()` only takes a baseline and pushes no pause, so
    `ScriptHost::start()` pumps once before the loop's first tick, or an
    `exit` at frame 0 would never be handed over (SCRIPT-EV-EXIT-FIRST-TICK);
  - every failure marks the boundary (`mark_failure`, by master cycle): a
    `stop`, a failed `assert` or `compare_scr`, a static stop (counted in
    `account_static_stops`), and a run-time error (code 1). At the hand-over
    an `exit 0` at a failed boundary becomes the failure's code, logged
    `SCRIPT EXIT 0 not taken: "reason" failed at the same boundary (exit 3)`;
    a non-zero `exit n` is kept. The rest of a body still runs, so a span
    script's `unsnap` after a `stop` keeps its stack balanced;
  - an `exit` issued while any `compare_scr` waits for its frame edge waits too
    (`Deferred::Kind::Exit`), and is taken at that edge after the compare.
  Pinned by SCRIPT-EV-ASSERT-EXIT, -EXIT-COMPARE-FRAME, -EXIT-COMPARE-HELD,
  -EXIT-OTHER-RULE (both orders, a static stop, `exit 7` kept), -EXIT-RUNTIME,
  -EXIT-FIRST-TICK, -EXIT-FIRST-OF-BOUNDARY, -EXIT-LATER-BOUNDARY, and end to end by `script-mutation-func`'s red twin. §2.6's `exit` row, I.4
  and the man page say so. Other clients' breakpoints at the same boundary are
  not script failures and do not count.
- **`@__data_crt_head` did not resolve** (review round 1, blocking for #279):
  jnext's MAP loader kept only the `; addr` lines of a z88dk map, and the crt's
  section bounds are `; const`. Fixed in `SymbolTable`: `; const` lines are kept
  in a separate NAME-ONLY table that `lookup_name()` consults after the
  addresses — so `@__data_crt_head`, ZRCP and GDB names resolve — and that
  `lookup()`, `symbols()` and the disassembler never see, so a size or a bound
  never names an address (rows SYM-11, SYM-12). A name defined twice resolves to
  its FIRST definition, a const as an address already did; a name that is both
  is the address (review round 2, C5; SYM-13). `range_watch.jds` and
  `hostkey.jds` now guard `0x8000..(@__data_crt_head - 1)`, as §3(a) and
  ChaseTheBug do.
- **A script must arm after the program is loaded.** Before the NEX loads,
  NextZXOS and the loader run code at these same addresses, and the loader
  writes the code range. Every script therefore arms its watches at the first
  `main_loop` where the program's `magic` word reads 0xD5D5 (`once when
  mem16[@magic] == 0xD5D5`). §3's sketches assumed watches live from power-on.
- **The keyboard poll is in the main loop, not the handler**, after
  `main_loop`, where the script presses. This way "applied at once" (seen in
  the same frame) and "a frame late" (two frames on) both differ from the
  frame-edge rule (seen in the next frame), and both of §8's replay mutations
  are visible.
- **Absolute frames**: `--script-key 560 1` and the recording half rely on the
  NEX running from about FRAME 500, as with a warm start. The DAPR recordings
  (L.4) rely on the same thing. The verdicts themselves count the program's own
  frames.

### M.3 Deviations

- `replay_edge.jds` presses at the program's frame 30 and expects 31, rather
  than §8's absolute FRAME 120 and 121: the program's start frame depends on
  the boot path.
- The recording half uses the SDL frontend under Xvfb with xdotool, as
  `sdl-keypress-func` does. It SKIPS, and never fails, when the X server
  delivers no key — the recording then holds no press.

## Appendix N — WP9 (§9's WP8a) as built: the user-guide chapter (2026-10-02)

`src/doc/user-guide/06-debugger/scripting/`, fourteen pages, rendered into the
committed `doc/user-guide/` and linked from the debugger chapter's index, the
Script panel and function pages, and the generated option page (whose
SCRIPTING link now points at the chapter instead of the man page). Pages: an
index; a first-script tour; the language; events (filter, delivery point,
payload for every kind); reading the machine; actions; snapshots and span
checks; keys, joysticks and host keys; changing the machine (the §2.7
visibility rule); running scripts (CLI, GUI, exit codes and the
exit-after-failure rule, the watchdog, CI); recording and replaying (recorded
in the Qt GUI under Xvfb, replayed headless, the "inexact" warnings);
catching memory-corruption bugs (#279's three cases and MemPoint and the host
keys, as a ChaseTheBug user's path); the nine demo scripts with their output;
a quick reference. Every example was run against the product and its output
pasted (time stamps and `[client N]` trimmed).

### N.1 Product defects found while writing it, fixed in-branch with rows

- **The raster names read a constant while running.** `Debugger::raster()` /
  `time()` returned the LAST PAUSE's snapshot unless the machine was paused,
  so `RAW_VC`, `CVC`, `HC_ULA` … in a `scanline` or `execute` rule never
  moved (`on scanline 100` printed `CVC=247 RAW_VC=0` on every frame). They are
  now derived from the clock at the query; the paused snapshot is still left
  alone. INS-06-04 re-pinned (its "raster() returns the kept snapshot while
  running" clause was the defect), SCRIPT-EV-RASTER-LIVE added. In a `scanline
  N` rule the live `CVC` reads N-1: the rule runs as the raw line begins and
  CVC steps a few pixels into it — exactly what NR 0x1E/0x1F read then (GH
  #257); the guide says so.
- **`exit 256` reached the shell as 0.** An `exit` outside 0..255 is now a
  run-time error (exit 1). SCRIPT-EV-EXIT-RANGE.
- **The joystick and sprite-pattern `MUTATE` lines printed decimal after
  `0x`** (`joystick 1 0x10` logged `= 0x16`). MUT-HEX-01.
- **The man page's `# REMOTE DEBUGGING (ZRCP)` heading had no blank line
  before it**, so pandoc swallowed the whole ZRCP section into SCRIPTING (a
  literal `# REMOTE…` line in the roff).
- **The Script tab elided its Event and State cells** (`write …`, `armed, …`)
  at the window's default width. The columns are now sized to their text when
  it changes. QSCR-14.
- **`docshot` did not build** (GH #278 WP7 changed `DebuggerManager`'s
  constructor and the Video panel's raster helper); fixed, and it now also
  captures the Script tab (`debugger-script.png`, used by the guide).

### N.2 Found, documented, not changed (owner's call)

- `page[s]` (and the `PAGE` payload, and the `page` filters) give a ROM slot its
  ROM page number, 0..7 — the same numbers as RAM pages 0..7, so `on execute
  page 0` also matches ROM code. Documented, with `mmu[s] != 0xFF` as the RAM
  test.
- The backend logs `STOP under StopPolicy::ExitNonZero — requesting exit 3`
  for every headless stop, a script `exit 0`'s included; the verdict is the
  host's `script requested exit N` line. Documented.
- `save_snapshot` (like `--delayed-snapshot`) writes `.sna` data for any
  extension but `.jns`/`.szx`/`.nex`, `.z80` included. Documented.
- `screenshot` / `save_snapshot` and an `exit` in the same frame rule: the run
  exits 1 (§2.6, fail loud), unlike `compare_scr`, which the exit waits for.
  Documented, with the one-frame-later idiom.
- A `log "PASS …"` after a failed `assert` in the same body is still printed
  (the body runs to the end, §6.3); the status is right. Documented.

