# Scriptable Debugger — Design Document

## 1. Overview

The Scriptable Debugger adds a small Domain Specific Language (DSL) to the JNext emulator that
allows users to automate debugging sessions, write automated tests, reproduce bugs deterministically,
and extract fine-grained timing information — all without modifying emulator source code.

Scripts can be used both from the command line (headless) and from the GUI debugger, making them
suitable for CI pipelines, regression tests, and interactive investigation alike.

---

## 2. Goals and Use Cases

### 2.1 Automated Testing

Write scripts that load a program, run it to a known point, assert register/memory state, and exit
with a pass/fail code. Integrates naturally with `--headless` mode for CI.

```
# test_palette_init.dbg
# Assert palette registers are initialised correctly after boot

on execute sym["palette_init_done"] once do
    assert mem[0x9000] == 0xAA "Expected sentinel value in palette buffer"
    assert A == 0x00 "Expected A=0 after palette init"
    print "PASS: palette init test"
    exit 0
end

on frame 300 do
    print "FAIL: palette_init_done never reached within 300 frames"
    exit 1
end
```

### 2.2 Bug Documentation and Reproduction

Encode the exact conditions that trigger a bug — register values, memory state, precise timing —
so that any developer can reproduce and debug it without guesswork.

```
# bug_123_sprite_glitch.dbg
# Bug: sprite Y coordinate wraps incorrectly when crossing scanline 192
# Reproducer: break when sprite 0 Y register is written with value >= 192

on io_write 0x0057 when VALUE >= 192 do
    print "Sprite 0 Y write: ${VALUE} at cycle ${CYCLE} (frame ${FRAME}, scanline ${VC})"
    dump_regs
    dump_mem 0x5C00 64
    break
end
```

### 2.3 Fine-Grained Time Traceability

Log execution events at T-state, scanline, or frame granularity to understand timing-sensitive
behaviour (interrupt latency, raster effects, audio synchronisation).

```
# timing_trace.dbg
# Measure interrupt handler latency (cycle from INT assertion to handler first opcode)

var int_cycle = 0

on interrupt do
    int_cycle = CYCLE
    print "INT asserted at cycle ${CYCLE} (scanline ${VC})"
end

on execute 0x0038 once do
    print "IM1 handler reached: cycle ${CYCLE}, latency ${CYCLE - int_cycle} T-states"
end
```

### 2.4 Event-Triggered Breakpoints

Break or log on memory accesses, I/O port activity, specific instructions, interrupts, NMIs, or
magic breakpoints — with optional conditions on register/memory state.

```
# Break when a specific memory region is modified with a specific value
on write 0x4000..0x57FF when VALUE == 0xFF do
    print "Full-byte pixel write at ${ADDR}: frame ${FRAME}"
end

# Break on OUTI instruction touching a port
on io_write 0x253B do
    print "NextREG data write: ${VALUE} at PC=${PC}"
end
```

### 2.5 Detailed Output

Capture structured output: register dumps, memory hex dumps, disassembly, trace logs, and
screenshots — either to the console or to files.

```
on frame 60 do
    save_screenshot "/tmp/frame60.png"
    save_trace "/tmp/trace_frame60.txt"
    dump_regs
    dump_mem 0x4000 256 "/tmp/screen_frame60.bin"
end
```

---

## 3. DSL Language Specification

### 3.1 Lexical Structure

- Line comments: `#` to end-of-line
- Identifiers: `[a-zA-Z_][a-zA-Z0-9_]*`
- Integer literals: decimal (`255`), hex (`0xFF`, `0x5800`), binary (`0b10110011`)
- Range literals: `0x4000..0x57FF` (inclusive)
- String literals: `"text"` with `${expr}` interpolation
- Keywords: `on`, `do`, `end`, `var`, `if`, `then`, `else`, `when`, `once`, `and`, `or`, `not`,
  `true`, `false`

### 3.2 Types

| Type     | Examples                     | Notes                              |
|----------|------------------------------|------------------------------------|
| integer  | `255`, `0xFF`, `0b11110000`  | 32-bit signed                      |
| boolean  | `true`, `false`              | From comparisons and logic ops     |
| string   | `"hello ${A}"`               | Interpolated at evaluation time    |
| range    | `0x4000..0x57FF`             | Inclusive address range            |

### 3.3 Variables

```
var name = expr         # Declare and initialise
name = expr             # Assign (must already be declared)
```

Variables are global to the script. There are no scopes or closures.

### 3.4 Built-in Variables (Read-Only)

#### CPU Registers

| Name | Description         | Name | Description              |
|------|---------------------|------|--------------------------|
| `A`  | Accumulator         | `F`  | Flags register           |
| `B`  | B register          | `C`  | C register               |
| `D`  | D register          | `E`  | E register               |
| `H`  | H register          | `L`  | L register               |
| `BC` | BC pair             | `DE` | DE pair                  |
| `HL` | HL pair             | `AF` | AF pair                  |
| `IX` | Index register IX   | `IY` | Index register IY        |
| `SP` | Stack pointer       | `PC` | Program counter          |
| `I`  | Interrupt vector    | `R`  | Refresh register         |
| `A'` | Shadow accumulator  | `F'` | Shadow flags             |
| `BC'`| Shadow BC           | `DE'`| Shadow DE                |
| `HL'`| Shadow HL           | `AF'`| Shadow AF                |
| `IFF`| Interrupt flip-flop | `IM` | Interrupt mode (0/1/2)   |

#### CPU Flags (individual bits)

| Name | Flag       | Name | Flag            |
|------|------------|------|-----------------|
| `CF` | Carry      | `ZF` | Zero            |
| `SF` | Sign       | `PF` | Parity/Overflow |
| `HF` | Half-carry | `NF` | Subtract        |

#### Timing

| Name     | Description                                      |
|----------|--------------------------------------------------|
| `CYCLE`  | Master cycle count since emulator start          |
| `FRAME`  | Frame counter (increments at VSYNC)              |
| `VC`     | Vertical counter — scanline 0..255 (visible)     |
| `HC`     | Horizontal counter — T-state within scanline     |
| `TFRAME` | T-states elapsed since frame start               |

#### Event Context (only valid inside event handlers)

| Name    | Description                                  |
|---------|----------------------------------------------|
| `ADDR`  | Address that triggered a memory/I/O event    |
| `VALUE` | Value read or written in a memory/I/O event  |

#### Memory Access

| Expression       | Description                       |
|------------------|-----------------------------------|
| `mem[addr]`      | Read byte at address `addr`       |
| `mem16[addr]`    | Read little-endian word at `addr` |
| `nextreg[reg]`   | Read NextREG register value       |

#### Symbol Table

| Expression       | Description                                        |
|------------------|----------------------------------------------------|
| `sym["name"]`    | Address of symbol from loaded .MAP file            |

### 3.5 Expressions

```
expr ::= integer_lit
       | boolean_lit
       | string_lit
       | variable
       | builtin_var
       | mem[expr]
       | mem16[expr]
       | nextreg[expr]
       | sym[string_lit]
       | expr op expr
       | not expr
       | ( expr )

op   ::= + | - | * | / | % | & | | | ^ | << | >>
       | == | != | < | > | <= | >= | and | or
```

Integer results are always 32-bit signed. Overflow wraps. Division by zero triggers a runtime error
and pauses execution.

### 3.6 Event Triggers

```
trigger_stmt ::= on event_spec [once] [when expr] do
                     action_list
                 end

event_spec   ::= execute addr_spec
               | read    addr_spec
               | write   addr_spec
               | io_read  addr_spec
               | io_write addr_spec
               | frame   integer_lit
               | scanline integer_lit
               | cycle   integer_lit
               | interrupt
               | nmi
               | magic_break
               | reset
```

`addr_spec` is either a single address expression or a range (`0x4000..0x57FF`). Both hex and
decimal literals are accepted. Symbol names via `sym["name"]` are also valid.

`once` causes the trigger to auto-remove itself after firing once.

`when expr` adds a boolean guard — the body executes only if the guard evaluates to `true`.

Named triggers (for enable/disable):

```
trigger "name" on event_spec ... do
    action_list
end
```

### 3.7 Actions

#### Execution Control

| Action                   | Description                                                  |
|--------------------------|--------------------------------------------------------------|
| `break`                  | Pause emulator execution (open debugger if GUI available)    |
| `continue`               | Resume execution (useful at end of conditional blocks)       |
| `step [N]`               | Execute N instructions then pause (default 1)                |
| `step_over [N]`          | Step over N instructions (treat CALL as atomic)              |
| `step_out`               | Run until RET returns to caller's frame                      |
| `run_to ADDR`            | Run until PC == ADDR                                         |
| `run_to_frame N`         | Run until frame N                                            |
| `run_to_cycle N`         | Run until master cycle N                                     |
| `run_to_scanline N`      | Run until VC == N at start of frame                          |

#### Output

| Action                             | Description                                                |
|------------------------------------|------------------------------------------------------------|
| `print "msg"`                      | Print interpolated message to script console               |
| `dump_regs`                        | Print all CPU registers                                    |
| `dump_mem ADDR SIZE`               | Hex dump SIZE bytes from ADDR to console                   |
| `dump_mem ADDR SIZE "file"`        | Hex dump to file                                           |
| `disasm ADDR [LEN]`                | Disassemble LEN bytes (default 16) from ADDR               |
| `trace [N]`                        | Print last N instructions from trace log (default 20)      |
| `save_trace "file"`                | Save full trace log to file                                |
| `save_screenshot "file"`           | Save PNG screenshot                                        |
| `save_snapshot "file"`             | Save SZX snapshot                                          |

#### Watchpoints

| Action               | Description                                              |
|----------------------|----------------------------------------------------------|
| `watch ADDR`         | Add data watchpoint (read+write) at ADDR                 |
| `watch_read ADDR`    | Add read watchpoint                                      |
| `watch_write ADDR`   | Add write watchpoint                                     |
| `unwatch ADDR`       | Remove watchpoint at ADDR                                |

#### Trigger Management

| Action                    | Description                                          |
|---------------------------|------------------------------------------------------|
| `enable "name"`           | Enable a previously disabled named trigger           |
| `disable "name"`          | Disable a named trigger (does not delete it)         |
| `remove "name"`           | Delete a named trigger permanently                   |

#### Variables

| Action             | Description                              |
|--------------------|------------------------------------------|
| `var NAME = expr`  | Declare a new script variable            |
| `NAME = expr`      | Assign to existing variable              |

#### Testing

| Action                       | Description                                                  |
|------------------------------|--------------------------------------------------------------|
| `assert expr "message"`      | Fail with message if expr is false; increments fail counter  |
| `assert_eq A B "message"`    | Fail if A != B; shows both values in error                   |
| `assert_mem ADDR SIZE "file"`| Fail if memory block differs from binary file content        |
| `exit [CODE]`                | Exit emulator; CODE defaults to 0 (pass) or 1 (fail)        |

#### Control Flow

```
if expr then
    action_list
[else
    action_list]
end
```

### 3.8 Top-Level Statements

```
script ::= (trigger_stmt | var_decl | action)*

var_decl ::= var NAME = expr

action   ::= (all action forms from §3.7)
```

Top-level actions (outside any event handler) execute once at script load time. This is useful for
setting up initial state, enabling features, or running to a starting point:

```
# Set up initial conditions before any triggers fire
var pass_count = 0
var fail_count = 0

run_to sym["main_entry"]
print "Reached main entry at cycle ${CYCLE}"
```

### 3.9 Loading Other Scripts

```
load "path/to/script.dbg"
```

Executes another script file inline at that point. Useful for splitting large test suites into
reusable modules.

---

## 4. Integration Points

### 4.1 Existing Infrastructure

The emulator already provides the hooks needed for script execution:

| Emulator Hook                      | DSL Event        | Location                  |
|------------------------------------|------------------|---------------------------|
| `on_m1_cycle` callback on `Z80Cpu` | `on execute`     | `src/cpu/z80_cpu.h`       |
| MMU read hook                      | `on read`        | `src/memory/mmu.h`        |
| MMU write hook                     | `on write`       | `src/memory/mmu.h`        |
| Port read dispatch                 | `on io_read`     | `src/port/port_manager.h` |
| Port write dispatch                | `on io_write`    | `src/port/port_manager.h` |
| `on_magic_breakpoint` callback     | `on magic_break` | `src/cpu/z80_cpu.h`       |
| Scheduler `VSYNC` event            | `on frame`       | `src/core/emulator.h`     |
| Scheduler `SCANLINE` event         | `on scanline`    | `src/core/emulator.h`     |
| Master cycle counter               | `on cycle`       | `src/core/clock.h`        |
| CPU interrupt request              | `on interrupt`   | `src/cpu/z80_cpu.h`       |
| CPU NMI request                    | `on nmi`         | `src/cpu/z80_cpu.h`       |

### 4.2 New Infrastructure Required

- **Script engine**: `src/script/` — lexer, parser, AST, evaluator
- **Script event dispatcher**: bridge between emulator callbacks and active triggers
- **Script console panel**: GUI tab for REPL and output (`src/debugger/script_panel.*`)
- **CLI option**: `--script FILE` to load a script at startup

---

## 5. Architecture

### 5.1 Component Overview

```
┌─────────────────────────────────────────────────────────┐
│                     Script Engine                       │
│                                                         │
│  ┌──────────┐  ┌──────────┐  ┌───────────────────────┐  │
│  │  Lexer   │→ │  Parser  │→ │  AST / IR             │  │
│  └──────────┘  └──────────┘  └───────────────────────┘  │
│                                      │                  │
│                               ┌──────▼──────┐           │
│                               │  Evaluator  │           │
│                               └──────┬──────┘           │
└──────────────────────────────────────┼──────────────────┘
                                       │
                    ┌──────────────────▼──────────────────┐
                    │           ScriptContext             │
                    │  - variables map                    │
                    │  - trigger registry                 │
                    │  - pass/fail counters               │
                    │  - output stream                    │
                    └──────────────────┬──────────────────┘
                                       │
               ┌───────────────────────┼───────────────────┐
               │                       │                   │
        ┌──────▼──────┐    ┌───────────▼───────┐   ┌───────▼───────┐
        │  Emulator   │    │   DebugState      │   │  TraceLog /   │
        │  API façade │    │   (break/step/    │   │  BreakpointSet│
        │             │    │    continue)      │   │               │
        └─────────────┘    └───────────────────┘   └───────────────┘
```

### 5.2 ScriptEngine Class

```cpp
// src/script/script_engine.h
class ScriptEngine {
public:
    // Load and parse a script file (does not execute yet)
    bool load_file(const std::string& path, std::string& error_out);

    // Load and parse a script string (for REPL)
    bool load_string(const std::string& source, std::string& error_out);

    // Execute top-level statements (called once after load)
    void execute_toplevel();

    // Called from emulator event hooks — dispatch to matching triggers
    void fire_execute(uint16_t pc);
    void fire_read(uint16_t addr, uint8_t value);
    void fire_write(uint16_t addr, uint8_t value);
    void fire_io_read(uint16_t port, uint8_t value);
    void fire_io_write(uint16_t port, uint8_t value);
    void fire_frame(uint32_t frame_num);
    void fire_scanline(uint16_t vc);
    void fire_cycle(uint64_t cycle);
    void fire_interrupt();
    void fire_nmi();
    void fire_magic_break(uint16_t pc);
    void fire_reset();

    // REPL: execute a single statement and return output
    std::string exec_statement(const std::string& stmt);

    // Output stream (GUI panel or stdout)
    void set_output(std::ostream* out);

    // Connect to emulator subsystems
    void set_emulator(Emulator* emu);
    void set_debug_state(DebugState* ds);
    void set_trace_log(TraceLog* trace);
    void set_symbol_table(SymbolTable* syms);

    int get_fail_count() const;
    int get_pass_count() const;
};
```

### 5.3 Trigger Registry

```cpp
struct Trigger {
    std::string name;            // optional, for enable/disable
    EventType   event_type;
    uint16_t    addr_lo;         // for address-based events
    uint16_t    addr_hi;         // == addr_lo for single address
    bool        once;            // auto-remove after first fire
    bool        enabled;
    ASTNode*    condition;       // optional `when` guard
    ASTNode*    body;            // action list
};

class TriggerRegistry {
    std::vector<Trigger> triggers_;
    // Fast path: per-PC set for `on execute` triggers
    std::unordered_map<uint16_t, std::vector<size_t>> execute_index_;
    // Range triggers stored separately, scanned linearly
    std::vector<size_t> execute_range_triggers_;
    // ...
};
```

**Performance consideration**: `on execute` triggers for a single address use a hash set lookup
(O(1)). Range triggers and other event types are scanned linearly since they fire infrequently
relative to per-instruction overhead. When no execute triggers are registered, the `fire_execute`
path is a single null-check and returns immediately — zero overhead for unscripted sessions.

### 5.4 Evaluator

The evaluator is a simple tree-walking interpreter:

```cpp
class Evaluator {
public:
    Value eval_expr(const ASTNode* node, const EventContext& ctx);
    void  exec_action(const ASTNode* node, const EventContext& ctx);
    void  exec_action_list(const ASTNode* node, const EventContext& ctx);
};

struct EventContext {
    uint16_t addr;   // ADDR
    uint8_t  value;  // VALUE
};

struct Value {
    enum class Type { Integer, Boolean, String };
    Type type;
    int32_t     i;
    bool        b;
    std::string s;
};
```

No heap allocation for small values (integers, booleans). Strings are only created for `print`
and `assert` actions.

### 5.5 Emulator API Façade

The façade exposes only the operations that scripts need, keeping the engine decoupled from the
full emulator interface:

```cpp
// src/script/emulator_api.h
class EmulatorApi {
public:
    // Execution control
    void do_break();
    void do_continue();
    void do_step(int n = 1);
    void do_step_over(int n = 1);
    void do_step_out();
    void do_run_to(uint16_t addr);
    void do_run_to_frame(uint32_t frame);
    void do_run_to_cycle(uint64_t cycle);
    void do_run_to_scanline(uint16_t vc);

    // Inspection
    Z80Registers get_registers() const;
    uint8_t  read_byte(uint16_t addr) const;
    uint16_t read_word(uint16_t addr) const;
    uint8_t  read_nextreg(uint8_t reg) const;
    uint32_t get_frame() const;
    uint16_t get_vc() const;
    uint16_t get_hc() const;
    uint64_t get_cycle() const;

    // Output
    void save_screenshot(const std::string& path);
    void save_snapshot(const std::string& path);
    void save_trace(const std::string& path);
    std::string dump_mem(uint16_t addr, uint16_t size);
    std::string disassemble(uint16_t addr, int len);
    std::string dump_regs();

    // Watchpoints
    void add_watchpoint(uint16_t addr, WatchType type);
    void remove_watchpoint(uint16_t addr);

    // Teardown
    void do_exit(int code);

private:
    Emulator*   emu_;
    DebugState* debug_state_;
    TraceLog*   trace_;
    SymbolTable* syms_;
};
```

### 5.6 GUI Integration

A new **Script Panel** tab is added to the debugger window:

```
┌──────────────────────────────────────────────────────┐
│ Script                                               │
├──────────────────────────────────────────────────────┤
│ [Load Script...] [Clear Output] [□ Echo commands]    │
├──────────────────────────────────────────────────────┤
│ Output:                                              │
│ ┌────────────────────────────────────────────────┐   │
│ │ Script loaded: test_palette.dbg                │   │
│ │ Reached main entry at cycle 12345              │   │
│ │ PASS: palette init test                        │   │
│ └────────────────────────────────────────────────┘   │
├──────────────────────────────────────────────────────┤
│ REPL:                                                │
│ > dump_regs                        [Run]             │
└──────────────────────────────────────────────────────┘
```

- Output area: scrolling text view, monospaced font, auto-scrolls to bottom
- REPL input: single-line text field; Enter submits; history with Up/Down arrows
- Multi-line REPL input: Shift+Enter adds newline, Enter submits

---

## 6. Implementation Plan

### Phase 1 — Core DSL Engine (PoC)

Goal: A working but minimal script engine that can break on `on execute`, print registers, and
exit with a code. Enough to replace manual debugging sessions.

- [ ] `src/script/lexer.h/.cpp` — tokenizer for the DSL
- [ ] `src/script/parser.h/.cpp` — recursive-descent parser producing AST
- [ ] `src/script/ast.h` — AST node types
- [ ] `src/script/evaluator.h/.cpp` — tree-walking evaluator
- [ ] `src/script/script_context.h/.cpp` — variable map, trigger registry, pass/fail counters
- [ ] `src/script/emulator_api.h/.cpp` — façade over emulator subsystems
- [ ] `src/script/script_engine.h/.cpp` — public interface, event dispatch
- [ ] Wire `fire_execute` into `Emulator::run_frame` loop (checked when debug is active)
- [ ] CLI option `--script FILE` in `src/main.cpp`
- [ ] Actions: `break`, `continue`, `print`, `dump_regs`, `assert`, `exit`
- [ ] Events: `on execute addr`, `on frame N`
- [ ] Variables: `var`, assignment, integer arithmetic, comparisons
- [ ] Tests: two or three `.dbg` scripts run under `--headless` in regression suite

**Milestone**: A script can run the emulator to a specific PC, assert register values, and exit
with pass/fail — suitable for CI use.

### Phase 2 — Memory and I/O Events

- [ ] `on read`, `on write` (single address and range)
- [ ] `on io_read`, `on io_write`
- [ ] `mem[addr]` and `mem16[addr]` expressions
- [ ] `nextreg[reg]` expression
- [ ] `ADDR` and `VALUE` context variables
- [ ] Actions: `dump_mem`, `watch`, `watch_read`, `watch_write`, `unwatch`
- [ ] Wire into MMU read/write hooks and port dispatch
- [ ] Range-based trigger matching

**Milestone**: Memory corruption and I/O tracing scripts work end-to-end.

### Phase 3 — Timing Events

- [ ] `on scanline N`, `on cycle N`
- [ ] `VC`, `HC`, `CYCLE`, `TFRAME` built-in variables
- [ ] `on interrupt`, `on nmi`
- [ ] `IFF`, `IM` CPU state variables
- [ ] `run_to_scanline`, `run_to_cycle`, `run_to_frame` actions
- [ ] `on magic_break`, `on reset`

**Milestone**: Timing analysis scripts (interrupt latency, raster timing) work correctly.

### Phase 4 — Output and Inspection

- [ ] `disasm ADDR [LEN]` action
- [ ] `trace [N]` action (from `TraceLog`)
- [ ] `save_trace "file"` action
- [ ] `save_screenshot "file"` action
- [ ] `save_snapshot "file"` action
- [ ] `dump_mem ADDR SIZE "file"` (file output variant)
- [ ] `assert_mem ADDR SIZE "file"` (compare block to binary file)
- [ ] Symbol table integration: `sym["name"]` expression
- [ ] `.MAP` file auto-load alongside `--script` (if present)

**Milestone**: Full inspection capability; screenshot comparison in scripts works.

### Phase 5 — GUI Integration

- [ ] `ScriptPanel` Qt widget in `src/debugger/`
- [ ] Output text area wired to script engine output stream
- [ ] REPL input field with history
- [ ] "Load Script" file dialog
- [ ] Script output panel auto-opens when a script is loaded
- [ ] Named trigger management: `enable`, `disable`, `remove` actions
- [ ] `load "file"` action for script composition

**Milestone**: Full interactive scripting from GUI; users can type commands and see results live.

### Phase 6 — Testing Infrastructure

- [ ] Test report: structured output (pass/fail counts, messages)
- [ ] JUnit-compatible XML output (`--test-report-xml FILE`)
- [ ] `assert_eq A B "message"` action
- [ ] Batch script runner (`--script` accepts multiple `--script` options)
- [ ] Integration with `test/regression.sh`
- [ ] Example test scripts in `test/scripts/`

**Milestone**: Script-based tests run in CI alongside FUSE and screenshot regression tests.

### Phase 7 — Advanced Features (Post-v1.0)

- [ ] Backward execution integration: `step_back [N]`, `on rewind_to addr`
- [ ] Source-level debugging: `on source "file.asm" line 42`
- [ ] Conditional watchpoints with expressions on `VALUE`
- [ ] Script profiling output: how many times each trigger fired, total cycles spent
- [ ] Lua embedding as an alternative evaluation backend (for users needing full language power)
- [ ] Remote script execution via TCP socket (headless server mode)
- [ ] Hot-reload: modify script file while emulator is paused, reload without restart

---

## 7. File and Script Conventions

### 7.1 File Extension

Scripts use the `.dbg` extension by convention.

### 7.2 Script Header Comment Block

Recommended convention for reproducibility:

```
# Script:  bug_sprite_y_wrap.dbg
# Author:  your-name
# Date:    2026-04-09
# Machine: next
# Load:    demo/sprites.nex
# Purpose: Reproducer for sprite Y coordinate wrap bug at scanline 192
```

### 7.3 Directory Layout

```
test/scripts/
├── unit/
│   ├── test_palette_init.dbg
│   ├── test_mmu_banking.dbg
│   └── test_copper_wait.dbg
├── regression/
│   ├── bug_123_sprite_glitch.dbg
│   └── bug_456_layer2_scroll.dbg
└── timing/
    ├── int_latency.dbg
    └── raster_timings.dbg
```

### 7.4 Headless Usage Pattern

```bash
# Run a single test
./build/jnext --headless --machine-type next \
    --load test/programs/palette_test.nex \
    --script test/scripts/unit/test_palette_init.dbg

# Run with test report
./build/jnext --headless --machine-type next \
    --load test/programs/palette_test.nex \
    --script test/scripts/unit/test_palette_init.dbg \
    --test-report-xml /tmp/results.xml

# Exit code: 0 = all asserts passed, 1 = one or more asserts failed
```

---

## 8. Example Scripts

### 8.1 Unit Test: Verify Memory Initialisation

```
# test_ram_clear.dbg
# After boot, ROM clears 0x5B00..0xFF7F. Verify it is zeroed.

var fail = 0

on execute sym["rom_clear_done"] once do
    var addr = 0x5B00
    while addr <= 0xFF7F do
        if mem[addr] != 0 then
            print "FAIL: ${addr} = ${mem[addr]} (expected 0)"
            fail = fail + 1
        end
        addr = addr + 1
    end
    if fail == 0 then
        print "PASS: RAM clear verified (0x5B00..0xFF7F)"
        exit 0
    else
        print "FAIL: ${fail} non-zero bytes found"
        exit 1
    end
end

on frame 500 do
    print "FAIL: rom_clear_done never reached"
    exit 1
end
```

### 8.2 Timing: Measure Frame Render Time

```
# frame_timing.dbg
# Measure CPU cycles consumed during first 10 visible frames

var frame_start_cycle = 0
var frame_count = 0

on scanline 0 when frame_count < 10 do
    if frame_count > 0 then
        var elapsed = CYCLE - frame_start_cycle
        print "Frame ${frame_count}: ${elapsed} T-states"
    end
    frame_start_cycle = CYCLE
    frame_count = frame_count + 1
end

on frame 11 do
    print "Timing complete"
    exit 0
end
```

### 8.3 Bug Reproducer: Attribute Clash Detection

```
# detect_attr_clash.dbg
# Log every write to the attribute area to track unexpected updates

var write_count = 0

on write 0x5800..0x5AFF do
    write_count = write_count + 1
    print "[${write_count}] Attr write: ${ADDR} <- ${VALUE} (PC=${PC}, frame=${FRAME}, VC=${VC})"
    if write_count >= 50 then
        print "--- 50 attr writes recorded, stopping ---"
        save_trace "/tmp/attr_trace.txt"
        break
    end
end
```

### 8.4 Integration Test: Screenshot Comparison

```
# integration_boot.dbg
# Boot the emulator and compare the screen after 3 seconds (150 frames)

on frame 150 once do
    save_screenshot "/tmp/actual_boot.png"
    assert_mem 0x4000 6912 "test/references/boot_screen.bin"
    print "PASS: Boot screen matches reference"
    exit 0
end
```

---

## 9. Performance Considerations

### 9.1 Zero Overhead When Not Scripting

When no script is loaded, `ScriptEngine::fire_execute` is a single check of a boolean flag and
returns immediately. The flag is stored in a field adjacent to the existing debug state check, so
the branch predictor will treat it the same as the existing `debug_state_.active()` check.

### 9.2 Execute Trigger Performance

When `on execute` triggers are registered:
- Single-address triggers: O(1) lookup in `std::unordered_set<uint16_t>`
- Range triggers: O(n) linear scan, but expected to be rare (< 5 active)
- The combined check adds approximately 10–20 ns per instruction on modern hardware, which is
  negligible compared to the emulated Z80 instruction (several microseconds of wall time at
  normal emulation speed)

At 100% emulation speed (3.5 MHz Z80), the host CPU is already executing ~3.5 million iterations
of the main loop per second; the script dispatch overhead is well within budget.

### 9.3 Condition Evaluation

Guard expressions (`when ...`) are only evaluated when the trigger address matches. Complex
conditions with `mem[addr]` reads add one host memory access per check — typically < 1 ns.

---

## 10. Future Directions

### 10.1 Lua Backend

Once the DSL is established and its usage patterns are clear, embedding a Lua 5.4 interpreter
as an alternative backend would give users the full power of a scripting language while keeping
the simple DSL as the default. The `EmulatorApi` façade maps cleanly to Lua C bindings.

### 10.2 Remote Debugging Protocol

A TCP socket server (`--debug-port 5555`) that accepts DSL commands and returns JSON responses
would enable IDE integration (VS Code extension, DeZog-compatible protocol) and remote debugging
of the emulator from a separate machine.

### 10.3 Record and Replay Integration

Combined with the backward execution feature (Phase 8), scripts could replay a deterministic
recording and extract timing information from the recorded stream — enabling offline analysis
without re-running the emulator.

### 10.4 Visual Timeline

A GUI panel showing a timeline of trigger firings plotted against frame/scanline/cycle coordinates
would make raster timing visualisation intuitive — similar to a logic analyser display.

---

## 11. Dependencies and Risks

| Risk                                               | Mitigation                                                                           |
|----------------------------------------------------|--------------------------------------------------------------------------------------|
| DSL complexity creep                               | Start with PoC (Phase 1); add features only as needed by real use cases              |
| Performance regression from per-instruction checks | Benchmark before and after; keep fast-path as branch with single flag check          |
| Script engine bugs introducing non-determinism     | Script engine is read-only w.r.t. emulator state except for explicit control actions |
| Symbol table availability                          | `sym["x"]` returns 0xFFFF if symbol not found; scripts should handle gracefully      |
| MMU hook contention with existing debug hooks      | Extend existing hook points rather than adding parallel ones                         |

---

*Document created: 2026-04-09*
*Status: Design — awaiting implementation approval*
