# Debugger DSL frontend — design (GH #26, carrying #279's vocabulary; #20 verdict)

Part of the GH #276 debug-subsystem design. Sibling of `backend.md` (design-backend),
the Qt, DZRP, ZRCP and GDB-RSP frontend documents. This file is the DSL's; it
records what the language is, what it demands of the backend, where it stops,
and the answer to "is #20 just a use case of #26?".

Status: v1 draft, 2026-09-26. Written against the code at `main` v1.0.44
(`974b0ab19`), never against `doc/design/EMULATOR-DESIGN-PLAN.md`. REQ verdicts
in §5 are updated as `design-backend` replies.

---

## 0. Settled decisions (recorded, not re-argued)

From #279 and #277 (owner, 2026-09-26):

| Decision | Consequence here |
|---|---|
| **No plugin API.** No C ABI, no `dlopen`, no scripting binding exposed as a plugin host. | This language is the only user-programmable event consumer. §4 says where it runs out, which is the evidence that would reopen the decision. |
| **The DSL is THE event primitive.** #279 is a set of requirements on this vocabulary. | §3 reproduces #279's acceptance sketch as scripts, verbatim in intent. |
| **Conditions and value predicates live in the backend.** DZRP declines them; the DSL uses them fully. | `when` compiles to a backend predicate (§5.2). The interpreter never sees a non-matching hit. |
| **Hot-path cost belongs to the backend.** | The DSL registers ranges; it does not implement the per-8K-page bitmap. §5.3. |
| **Observation must not perturb the machine.** | Scripts cannot write guest memory, registers, NextREGs or ports. §6.2 argues the one deliberate exception (input injection). |
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

File extension `.jds` (jnext debugger script). UTF-8, line-oriented, `#`
comments. Case-sensitive keywords in lower case; built-in state names in upper
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

addr_spec   ::= expr [ ".." expr ]                 # inclusive 16-bit logical range
port_spec   ::= expr [ ".." expr ]                 # GH #222 semantics: 0x00xx = low-byte decode,
              | "mask" expr "value" expr           #   else exact; or explicit mask/value
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
              | "press" key_spec [ "for" expr ]    # frame-boundary input; see 2.6, 7
              | "release" key_spec
              | "joystick" INT expr                # port index (1|2), 12-bit MD6 mask
              | "set" IDENT "=" expr
              | "if" expr "then" { action } [ "else" { action } ] "end"

expr        ::= literal | IDENT | builtin | "@" IDENT | "(" expr ")"   # builtin = the upper-case names of 2.3
              | "mem[" expr "]" | "mem16[" expr "]"
              | "phys[" expr "," expr "]"          # (8K page, offset 0..0x1FFF)
              | "nextreg[" expr "]" | "mmu[" expr "]" | "page[" expr "]" | "stack[" expr "]"
              | IDENT "." field                    # snapshot field, see 2.5
              | "changed(" IDENT "," group ")"     # snapshot comparison
              | "depth(" IDENT ")"
              | unop expr | expr binop expr
literal     ::= INT (dec, 0x hex, $ hex, 0b bin) | "true" | "false" | string
string      ::= '"' { char | "${" expr [ ":" fmt ] "}" } '"'      # fmt: x2 x4 d
key_spec    ::= string                             # same vocabulary as --delayed-keypress
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
  cycle). No rule body ever runs inside `Mmu::write`, `NextReg::write` or
  `PortDispatch`; that is what makes non-perturbation a property of the design.
- Firing order: events of one instruction in access order (the backend's
  16-entry latch ring); within one event, rule order in the file, then across
  files in `--script` order. A `stop` does not prevent later rules on the same
  event from running (they may want to log); it takes effect after all of them.
  If the ring overflowed (a DMA burst slot can), the run logs
  `SCRIPT: event ring overflowed at CYCLE …` once per occurrence — a script
  cannot see the dropped events, and silence would be a lie.
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
| `TFRAME` | master cycles since frame start | `Emulator::current_frame_cycle`, `emulator.h:742` |
| `RAW_HC RAW_VC HC_ULA VC_ULA CVC PHC` | the VHDL raster counters, exactly as `RasterState` names them | `raster_state.h:97-104` |
| `@name` | address of MAP symbol; load error if absent (§6.5) | `SymbolTable::lookup_name`, `symbol_table.h:21` |
| `MACHINE` | 0=48K 1=128K 2=+3 3=Pentagon 4=Next | backend (REQ-dsl-17: not in backend.md v1's CAP-INS-07/TIME-01) |

Event payload, valid only inside the matching rule body:

| Name | Events | Meaning |
|---|---|---|
| `ADDR` | read/write/execute | logical address hit |
| `VALUE` | read/write/io_*/nextreg | byte read or written (nextreg: the value written; the register is already committed at delivery, so `nextreg[REG] == VALUE`) |
| `PREV` | nextreg | the register's value before the write (backend payload `prev`, peeked at the hook) |
| `PAGE` | read/write/execute | physical 8K page behind `ADDR` at the time of the access |
| `PORT` | io_read/io_write | full 16-bit port |
| `REG` | nextreg | register number |
| `SOURCE` | read/write/nextreg | `CPU`, `DMA`, `COPPER` |
| `PC` | all | **pre-execution PC of the instruction that caused the event** (REQ-dsl-12), not the CPU's live PC |
| `KEY` | hostkey | 1..8 |
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
  fires every frame. Frame 0 is the first frame executed after the script is
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
| `exit n` | headless: exit with code n after the current instruction. GUI: log + pause (a GUI never exits from a script). |
| `dump_regs`, `dump_mmu`, `dump_mem a len` | to the log; `dump_mem` ≤ 4096 bytes, 16 per line. |
| `screenshot "f"` | queued for the **next frame boundary** through `save_screenshot` (`screenshot.h:60`): `.scr` = ULA memory (`Ula::screen_dump`), else PNG. Same path `--delayed-screenshot` uses. |
| `compare_scr "f" "msg"` | at the next frame boundary, `Ula::screen_dump()` byte-compared to file; first differing offset logged; mismatch behaves as `assert` failure. |
| `save_snapshot "f"` | queued for the next frame boundary through the existing savers (the GH #27 `--delayed-snapshot` route). |
| `press "KEY" [for n]` | queued for the next frame boundary: hold KEY down for n frames (default 5, the `HeadlessApp` value at `headless_app.cpp:559-561`); vocabulary of `--delayed-keypress`. |
| `release "KEY"` | for the recorder's edge-based replay (§7.2): release now rather than after a hold. |
| `joystick n bits` | set MD6 12-bit state of port n (1\|2) at the next frame boundary (`Joystick::set_joy_left/right`, `joystick.h:116-117`). |
| `enable NAME`, `disable NAME` | arm / disarm a labelled rule. |
| `set v = expr` | assign a `var`. |

Everything queued "for the next frame boundary" is applied by the backend at
`end_of_frame`, in script order, before the next frame's first instruction —
the one place a frame-granular input can be injected without perturbing the
instruction stream mid-frame. That is also why these actions are legal from any
event: a `press` issued from an `on write` handler lands at the same instant a
`press` from `on frame` does.

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
outside them. `PAGE` is what makes "a write to bank N" the real invariant when
code is banked — a guard on a *physical* page is `on write 0x0000..0xFFFF when
PAGE == 0x22`, which the backend's bitmap still prunes by logical page and the
predicate finishes.

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
wants to log the transition. `SOURCE` distinguishes a Copper `MOVE` (`copper.cpp:209`) from the CPU
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
functions (`.cs:230-234`) is one named stack per function; a mismatch is
`depth(f) == 0` at `f`'s exit. Its "new maximum nesting level" report is a
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
| **No mutation of the machine.** No poke, no register write, no NextREG write, no OUT. CSpect's `Poke`/`SetRegs`/`OutPort` are gone. | Owner constraint: observation must not perturb. A script that patches a value to *provoke* a path is a legitimate technique and is unavailable. | `--inject`, snapshots, or the guest program itself. If a user needs in-run patching this is the case to report. |
| **Frame-granular input only.** `press`/`joystick` land at frame boundaries. A test needing a key change at a scanline cannot say so. | Same seam as `--delayed-keypress-frames` and the GUI keyboard (`Keyboard::queue_auto_type`); mid-frame injection would be a new Keyboard capability. | None planned; RZX is IN-granular but replays results, not input (§7.4). |
| **No sub-instruction time.** `on cycle N` resolves to an instruction boundary. | Per-instruction core (`step_one_instruction`, `emulator.h:2255`). | None; this is the accuracy model. |
| **No DMA-cycle events, no Copper instruction events.** A script sees a DMA *write* (as `SOURCE == DMA`) and a Copper *NextREG write*, not the DMA burst or the Copper `WAIT`. | Nothing observable is lost for the acceptance cases; timing analysis of the Copper itself is the Copper panel's job. | Add `on copper_move` only if a script needs `WAIT` positions. |
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
| 8 | HOST KEY event, named keys 1..8 routed by both GUI frontends; headless `--script-key` | 3(e) | Qt `keyPressEvent` (`main_window.cpp:1351-1522` per TASK-115 §3.3); SDL `host_key_latch::Router::on_host_key` | pending (design-qt consulted) |
| 9 | predicate + `once` evaluated in the backend before the subscriber runs | every `when` | new; DZRP declines it | ACCEPTED → predicate closure evaluated at delivery, `once` disables after the first predicate-true firing; DZRP never sets one (§5.2) |
| 10 | read-only inspection from a callback: regs incl. IFF/IM, mem logical + physical, `NextReg::peek`, MMU raw + effective, RasterState, SymbolTable, stack words | every expression in §2.3 | `debug_state.h:126-140` InspectionScope; `nextreg.h:53`; `mmu.h:74` | ACCEPTED → CAP-INS-01/02/03/04/06, CAP-SYM; backend adds `Mmu::peek()` because `Mmu::read()` latches the +3 floating bus (`mmu.h:405-406`, backend finding F1) |
| 11 | actions: STOP(reason), LOG sink, EXIT(code), SCREENSHOT/SCR + SNAPSHOT at frame boundary, INPUT injection at frame boundary (key hold, key edge, joystick bits). No guest mutation. | §2.6 | `save_screenshot` `screenshot.h:60`; `Keyboard::queue_auto_type` `keyboard.h:71-81`, `set_matrix_bit` `:185`; `Joystick::set_joy_left/right` | ACCEPTED → Stop(reason)=pause with `pause_reason Script(id)`; Log=CAP-SES-06; Exit=session event `ExitRequested{code}` (GUI maps to pause+show); screenshot=CAP-CAP-01; snapshot=CAP-CAP-04; input=CAP-IN-01/02/03. The backend still offers poke/set_register/nextreg_write for DZRP/RSP; **the DSL declines them** (§6.2) |
| 12 | STOP from a mid-instruction event takes effect at the end of that instruction; payload PC = pre-execution PC | `stop` in 3(a),(b),(d) landing on the offending instruction | the `data_bp_hit` pattern, `emulator.cpp:9398-9407` | ACCEPTED → pause at the end of the instruction, payload `pc = pc_pre_exec` (`emulator.cpp:9937`) |
| 13 | screen capture as bytes: `Ula::screen_dump`; physical page reads for L2/tilemap | §7 `compare_scr` | `ula.h:608` | ACCEPTED → CAP-CAP-02 `ula_screen_dump()` + `peek(Page{n})` |
| 14 | per-frame input-state observation (matrix rows, joystick ports) from a GUI session | §7 recorder | `Keyboard::read_rows` `keyboard.h:68`, `Joystick::read_port_1f/37` `joystick.h:124-128` | ACCEPTED → CAP-INS-16 `input_state()`: matrix rows, extended keys, joystick 12-bit vectors and the composed 0x1F/0x37 bytes |
| 15 | cost statement measured with `make bench` | owner requirement | `test/bench/bench.sh` | ACCEPTED → backend.md §8; numbers in its v2 |
| 16 | a REAL backend frame counter | `FRAME`, `on frame N` | `Emulator::frame_num_` increments only at `emulator.cpp:8467`, inside `if (rewind_buffer_ && …)` — it is 0 for the whole run without `--rewind-buffer-size` | ACCEPTED, verified by the backend (its finding F2): increment unconditionally at the `emulator.cpp:8467` site, snapshot tag = pre-increment value, so rewind is byte-identical |
| 17 | machine type readable (`MACHINE`) | §7.1 header assert | `EmulatorConfig::type` | ACCEPTED → CAP-INS-19 `machine()`: type + timing constants + video timing variant (absorbs CAP-TIME-01's `machine_timing()`) |

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

### 5.3 Mapping against backend.md v1 — MAPPED: 28 used, 21 declined, 0 REQs open, 0 reach-arounds

| Backend capability | DSL use |
|---|---|
| CAP-CTL-01 pause | used, only as the `Stop` verdict of a delivery |
| CAP-CTL-02..12 run/step/run_to/step_back/rewind/reset | **declined** — a script observes; it does not drive (§1). `step_back`/`rewind_to_frame` are the §4 wall, revisited in v2 only with a prototype |
| CAP-CTL-13 state()/pause_reason | used (`REASON` in `on stop`) |
| CAP-INS-01 registers | used, read half only; `set_register` declined |
| CAP-INS-02 peek Cpu/Page | used; `poke` declined |
| CAP-INS-03 mmu_slots | used (`mmu[]`, `page[]`); `set_mmu_slot` declined |
| CAP-INS-04 nextreg_peek | used; `nextreg_write` declined |
| CAP-INS-05 port_in/out | **declined** (perturbing; the backend itself says so) |
| CAP-INS-06 raster, -07 time | used |
| CAP-INS-08..15 sprites, copper, AY, disasm, call stack, trace, framebuffer, palette | **declined** — no acceptance case; `screenshot` goes through CAP-CAP-01, not the framebuffer accessor |
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

### 6.2 Non-perturbation — what a script may legally change

Reads: everything in §2.3, all through the backend's inspection surface, which
is defined to be side-effect-free (InspectionScope; `NextReg::peek`; no
watchpoint can fire on a script read).

Writes to the machine: **none** — no memory, register, NextREG or port writes
exist in the grammar, so the constraint is enforced by the language rather than
by policy. The backend does offer `poke`, `set_register`, `nextreg_write` and
`port_out` (CAP-INS-02/01/04/05) because DZRP and RSP need them; the DSL is
the one consumer that declines them, and the reason is the owner's constraint,
not a missing capability. The two things a script *can* change:

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

Timing: a rule body executes zero emulated cycles. A script cannot change when
anything happens, only whether the run pauses or ends.

### 6.3 `stop` semantics

The backend offers one thing: *pause with a reason, at the offending
instruction* — for an execute event that is before the instruction at `PC`
(the `should_break` slot); for a write/port/NextREG event raised mid-instruction
it is at the end of that instruction, with the reported `PC` the instruction's
pre-execution PC (REQ-dsl-12; the `data_bp_hit` shape at `emulator.cpp:9398`).
The frontends decide what a pause means:

| Mode | `stop` | `assert` fail | `exit n` |
|---|---|---|---|
| `--headless` | log `SCRIPT STOP: <reason> at PC=… FRAME=… CYCLE=…`, exit **3** after the instruction | same as stop | exit n |
| GUI (Qt) | pause; open/raise the debugger window if closed (the GH #219 `--persistent-breakpoints` reopen path); disassembly on PC; reason in the Script tab and the status bar | same | log + pause (a GUI never exits from a script) |
| SDL frontend (no debugger) | log + pause the machine (the SDL app has a pause); reason on stderr | same | same as headless |

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
- Host keys 1..8 = `Alt+1`..`Alt+8` (agreed with design-qt: no collision;
  routed through the existing keymap forwarding block,
  `main_window.cpp:2200-2247`; a user binding a debugger key to Alt+digit gets
  the accept-with-warning rule of GH1-DEBUGGER-KEYMAP-DESIGN.md §4b). The SDL
  frontend routes the same chords through its `host_key_latch` seam.

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
# dapr-keyb.jds — generated by `jnext --record-script`, do not edit
# machine=next load=test06keyb.nex rtc=2026-01-01T00:00:00 sd=cspect-next-1gb-fixed.img
assert MACHINE == 4 "recorded on Next"
on frame 120 do press "q" end
on frame 126 do release "q" end
on frame 131 do press "w" end
on frame 137 do release "w" end
on frame 160 do compare_scr "dapr-keyb-0001.scr" "screen after q,w" end
on frame 190 do press "caps+1" end          # EDIT
on frame 197 do release "caps+1" end
on frame 230 do compare_scr "dapr-keyb-0002.scr" "screen after EDIT" end
on frame 231 do exit 0 end
```

Every line is a construct §2 already has for other reasons: `press`/`release`
are the same `Keyboard` seam `--delayed-keypress-frames` uses, `compare_scr` is
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
   `press "<key>"`, up becomes `release "<key>"`, using the inverse of the
   `--delayed-keypress` name table (`headless_app.cpp:209-257`); a joystick
   change becomes `joystick n bits`. The stamp is the backend frame counter —
   deterministic, and the same origin `--delayed-keypress-frames` uses.
2. **On a host key** (script host key 8 while recording, or a dedicated
   Debug-menu action): `Ula::screen_dump()` to `<base>-NNNN.scr` and a
   `compare_scr` line at the current frame. The index file #20 asks for *is*
   the script.
3. **Header**: machine, loaded file, `--rtc` value, SD image identity
   (`sd_snapshot_identity`) — the preconditions under which the replay is
   deterministic, so a mismatch is a loud `assert` rather than a mysterious
   diff.

The recorder is not a language feature; it is a consumer of REQ-dsl-6/8/14 that
writes text. That is the "one genuinely new piece" #276 predicted, and it is
~200 lines.

### 7.3 Reconciliation with what exists — no third mechanism

| Existing | Relation |
|---|---|
| `--delayed-keypress-frames N KEY` (`headless_app.cpp:557-567`, 5-frame hold via `queue_auto_type`) | ≡ `on frame N do press "KEY" end`. The flag **stays**: it is the regression suite's contract for 20+ rows, and a flag is the right tool for one keypress. A script is the general form; both go through one backend input-injection call (REQ-dsl-11), so they cannot drift. |
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
2. **`script_eval_test`**: evaluator against a fake inspection surface —
   every expression form in §2.3, `${:x2}` formatting, snapshot stack
   semantics (push/pop/depth, `changed()` per group, empty-stack error), 32-bit
   wrap, division by zero disabling the rule.
3. **`script_events_test`**: the real `Emulator` in headless mode with a
   tiny injected Z80 program built in the test (the `--inject` route), one row
   per event kind proving the payload: `ADDR`/`VALUE`/`PAGE`/`PC`/`SOURCE` on
   a write, pre-commit `VALUE` vs `nextreg[]` on a NextREG write from the CPU
   and from a Copper `MOVE`, `once`, enable/disable, `stop` landing PC on the
   offending instruction after a mid-instruction write, deferred `press`
   arriving at the frame boundary, `FRAME` advancing without a rewind buffer.
4. **Regression rows** `script-guard-func`, `script-mmu-func`,
   `script-isr-func`, `script-mempoint-func`, `script-hostkey-func`,
   `script-replay-keyb-func` in `functional_tests.conf` (count bumped), each
   running one §3 script against a demo NEX built for it in `demo/` — with a
   **red twin**: the same script against a deliberately buggy build of the demo
   (a stray write, an ISR that clobbers HL) must exit 3 with the expected
   reason in the log. A row that only has the green half is fixture-blind.

Mutations a reviewer must run (each must turn the named row red):

| Script | Mutation in the emulator/backend | Row that must go red |
|---|---|---|
| 3(a) guard | stop passing `PAGE`/`PC`; or make the range check exclusive at the top end | `script-guard-func` (asserts on the logged PC and the last byte of the range) |
| 3(b) mmu | fire the NextREG event *after* commit (so `nextreg[0x50]` and `VALUE` are both new) | `script-mmu-func` red twin passes wrongly → row asserts the red twin exits 3 |
| 3(c) isr | `changed(isr, mmu)` compares 7 slots instead of 8; or `stack0` captured after the push | `script-isr-func` red twin (clobbers slot 7 / the return address) |
| 3(d) mempoint | register the rule with no predicate and test `VALUE` inside the body instead | `script_parse_test` (subscription must carry a predicate) + `script_events_test` row counting rule-body entries on N non-matching writes (must be 0) |
| 3(b) mmu | deliver a Copper `MOVE` to NR 0x51 with `SOURCE == CPU` | `script_events_test` Copper row (the demo's copper list writes NR 0x51 once) |
| 3(e) hostkey | route Alt+N to the guest instead of the backend | `script-hostkey-func` (headless `--script-key`) + a Qt unit row on the keymap |
| replay | apply `press` immediately instead of at the frame boundary | `script-replay-keyb-func` (the `.scr` differs when the key lands mid-frame) |
| all | remove `once` auto-disable | `script_events_test` `once` row |
| all | remove the `InspectionScope` around script reads | `script_events_test`: a `read` watchpoint on an address the script peeks must NOT fire |

---

## 9. Work packages (parallelisable; one branch each off `main`)

| WP | Content | Depends on |
|---|---|---|
| WP0 | Backend event surface per §5 (REQ-dsl-1..16) — **design-backend's**, not this file's | — |
| WP1 | `src/script/lexer.*`, `parser.*`, `ast.h`, the `compile_expr`/`eval_expr` library entry points (§5.4); `script_parse_test` | WP0's predicate signature |
| WP2 | `evaluator.*`, `value.h`, snapshot stacks, interpolation; `script_eval_test` | WP1 |
| WP3 | `script_engine.*`: rule registration onto backend subscriptions, deferred-action queue, `stop`/`exit` policy hooks, log sink; `script_events_test` | WP0, WP2 |
| WP4 | CLI rows (`--script`, `--script-key`, `--map`, `--script-explain`) in `cli_options.h`, man page `jnext.1.md` OPTIONS + a "Scripting" section, `make docs-man`, headless exit-code wiring | WP3 |
| WP5 | GUI: Debug menu items, Script tab, Alt+1..8 routing, pause-with-reason display (design-qt owns) | WP3 |
| WP6 | Recorder (`src/script/recorder.*`), Debug menu "Record Script…", capture hotkey, header emission; converts the two parked DAPR rows | WP3, WP5 |
| WP7 | Demo programs for the §3 scripts (`demo/script_guard`, buggy twins), six `script-*-func` rows, `functional_tests.conf` count | WP4 |
| WP8 | Developer guide chapter (`src/doc/developer-guide`) + user guide page for scripting; FEATURES.md | WP4-WP7 |

Each WP gets an independent reviewer per `CLAUDE.md`; WP7's reviewer runs the
§8 mutation table.

---

## 10. Open questions for the owner

1. **Exit code for a headless script stop with no explicit `exit`**: the
   backend's default is 1 (same as a failed `--load`); this file recommends a
   dedicated 3 so a row can tell "the script caught something" from "jnext
   could not run" by exit code alone. Never 2 (harness fault in both
   harnesses). `assert` and `stop` share the code; the reason string carries
   the distinction.
2. **Host key namespace** `Alt+1..Alt+8`: acceptable in the Qt window and the
   SDL frontend? (design-qt to confirm no collision.)
3. **`--map` on the CLI**: it is needed for any headless script that uses
   `@symbol`. Any objection to making it also load into the debugger's symbol
   table (one table, not two)?
4. **Mutation ban**: the language has no poke. Confirm this is the intended
   reading of "must not perturb", or allow an explicit, loudly-logged
   `poke`/`set_reg` that a CI row could still forbid with a flag.
5. **#20 re-scope** as in §7.5.

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
| `Poke*`, `SetRegs`, `OutPort`, `SetNextRegister`, `SetSprite`, `CopperWrite`, `LoadNex` | **not offered** (§6.2, §4) |
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
