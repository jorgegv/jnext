# The language

## Files

A script is a text file, by convention with the extension `.jds`, in UTF-8.
Line ends are only white space: a rule can sit on one line or spread over
many, and several actions can share a line —

```
on frame 0 once do set AUDIO_MUTE = 0b00111 end
```

is as good as the same rule written over five lines.

A file holds **variable declarations** (`var`) and **rules** (`on … end`), in
any order.

## Comments

Three forms, each running to the end of the line: `;` (the assembler's), `//`
and `#`.

```
; a comment
// another
# and another
on frame 10 do log "a ; is not a comment inside a string" end   ; but this is
```

None of them is a comment inside a string. `//` is never confused with
division, because the language has no unary `/`.

## Names and case

- **Keywords** are lower case: `on`, `do`, `end`, `when`, `log`, `write`, …
- **Built-in state names** are upper case: `PC`, `HL`, `FRAME`, `CVC`, `VALUE`, …
- **Your own names** — variables, rule labels, snapshot names — are anything
  else: letters, digits and `_`, not starting with a digit. Case matters.

Every lower-case word of the language is reserved and cannot be one of your
names, nor can a built-in name:

```
var start = 0
```

```
[debugger] [error] SCRIPT ERROR reserved.jds:1:5: `start` is a reserved name and cannot be a variable name
```

The reserved words are: `var disabled on once when do end execute read write
io_read io_write nextreg frame scanline cycle interrupt nmi reset hostkey stop
copper move wait halt at dma start byte page mask value log indent assert exit
dump_regs dump_mmu dump_mem snap unsnap dump_diff enable disable screenshot
save_snapshot compare_scr press for release joystick set out if then else and
or not true false mem mem16 phys mmu stack changed depth`.

A MAP symbol is written `@name` and can be any name at all, keywords included
(`@start` is fine): see [Symbols](04-reading-the-machine.md#symbols-name).

## Numbers

| Form | Example | Value |
|---|---|---|
| decimal | `255` | 255 |
| hex, C style | `0xFF` | 255 |
| hex, assembler style | `$FF` | 255 |
| binary | `0b11111111` | 255 |
| truth | `true`, `false` | 1, 0 |

The prefixes are lower case (`0x`, `0b`). A number running into a letter or
digit it cannot use — `0X10`, `12ab`, `0b102` — is an error, not two tokens.

All arithmetic is **32-bit signed** and **wraps**: `0xFFFFFFFF` is -1, and
`0x7FFFFFFF + 1` is -2147483648. Integers are also the booleans: a comparison
gives 1 or 0, and a condition is true when it is not 0.

`CPU`, `DMA` and `COPPER` are the constants 0, 1 and 2, for comparing with an
event's `SOURCE`.

## Strings and interpolation

A string is `"…"` on one line. There are no escape sequences: a backslash is an
ordinary character (a Windows path is written as it is), and a string cannot
contain `"`.

Inside a string, `${expr}` is replaced by the value of `expr`, and
`${expr:fmt}` formats it:

| Format | Prints |
|---|---|
| (none), `d` | signed decimal |
| `x2` | upper-case hex, at least 2 digits |
| `x4` | upper-case hex, at least 4 digits |

A wider value keeps all its digits — `${0x1234:x2}` is `1234`, never a silently
cut `34` — and a negative one prints its 32-bit pattern (`${-1:x4}` is
`FFFFFFFF`). An interpolation may not hold a string literal, nor a comment
character.

```
var x = 0x35
on frame 1 do
    log "literals: ${255} ${0xFF} ${$FF} ${0b11111111} ${true} ${false} ${0xFFFFFFFF}"
    log "formats: ${42} ${42:d} ${42:x2} ${42:x4} ${0x1234:x2} ${-1:x4}"
    log "x & 0x70 == 0x30 is ${x & 0x70 == 0x30}; 10 - 3 - 2 is ${10 - 3 - 2}; 1 + 2 * 3 is ${1 + 2 * 3}"
    log "-7 / 2 is ${-7 / 2}; -7 % 2 is ${-7 % 2}; 1 << 33 is ${1 << 33}; -16 >> 2 is ${-16 >> 2}"
    log "not 5 is ${not 5}; 3 and 0 is ${3 and 0}; 0 or 7 is ${0 or 7}; ~0 is ${~0}"
    log "0x7FFFFFFF + 1 is ${0x7FFFFFFF + 1}"
    exit 0
end
```

```
[debugger] [info] [jds F:1 C:1118224] literals: 255 255 255 255 1 0 -1
[debugger] [info] [jds F:1 C:1118224] formats: 42 42 2A 002A 1234 FFFFFFFF
[debugger] [info] [jds F:1 C:1118224] x & 0x70 == 0x30 is 1; 10 - 3 - 2 is 5; 1 + 2 * 3 is 7
[debugger] [info] [jds F:1 C:1118224] -7 / 2 is -3; -7 % 2 is -1; 1 << 33 is 2; -16 >> 2 is -4
[debugger] [info] [jds F:1 C:1118224] not 5 is 0; 3 and 0 is 0; 0 or 7 is 1; ~0 is -1
[debugger] [info] [jds F:1 C:1118224] 0x7FFFFFFF + 1 is -2147483648
[debugger] [info] [jds F:1 C:1118224] SCRIPT EXIT 0
```

There are two types of value, fixed when the script loads: **integers** and
**strings**. A string is a string literal, or `REASON` in an
[`on stop`](03-events.md#stop) rule. Strings can be compared with `==` and
`!=` and printed with `${}`; nothing else. Every other place takes an integer,
and a string there is an error when the script loads:

```
[debugger] [error] SCRIPT ERROR strvar.jds:1:9: an integer is needed here, not a string
```

## Operators

From the loosest to the tightest:

| Level | Operators | Meaning |
|---|---|---|
| 1 | `or` | logical or (1 or 0; the right side is not evaluated when the left is true) |
| 2 | `and` | logical and (1 or 0; the right side is not evaluated when the left is false) |
| 3 | `not` | logical not |
| 4 | `==` `!=` `<` `>` `<=` `>=` | comparisons, 1 or 0 |
| 5 | <code>&#124;</code> `^` | bitwise or, exclusive or |
| 6 | `&` | bitwise and |
| 7 | `<<` `>>` | shifts |
| 8 | `+` `-` | add, subtract |
| 9 | `*` `/` `%` | multiply, divide, remainder |
| 10 | unary `-` `~` | negate, bitwise not |

Two things differ from C:

- **`&` binds tighter than `==`**, so `x & 0x70 == 0x30` means
  `(x & 0x70) == 0x30`, which is what you mean when you test bits.
- **Every binary operator is left-associative, comparisons included**:
  `a < b < c` is `(a < b) < c`, not a range test. Write `a < b and b < c`.

And the details: `/` truncates toward zero and `%` takes the dividend's sign
(`-7 / 2` is -3, `-7 % 2` is -1); a shift uses the low five bits of its count
(`1 << 33` is 2); `>>` is arithmetic (`-16 >> 2` is -4). Dividing by zero is a
run-time error.

Two expressions can sit side by side in an action (`dump_mem addr len`, `out
port value`, `assert cond "…"`). The first one ends at the first token that
cannot continue it, so `dump_mem a -1` reads as `dump_mem (a - 1)` with its
length missing: write `dump_mem a (-1)`.

## Variables

```
var hits = 0
var armed_at = FRAME
```

A variable holds an integer. It is declared once per file with `var`, and
assigned with `set` inside a rule:

```
set hits = hits + 1
```

The initial values are computed **once, when the script loads**, in the order
they are written. An initial value may read the machine (`var armed_at =
FRAME`), and the variables declared before it, but not one declared after it:

```
var a = b + 1
var b = 2
```

```
[debugger] [error] SCRIPT ERROR order.jds:1:9: variable `b` is used before its declaration
```

A variable can not hold a string. Each script file has its own variables.

## Rules

```
[disabled] [label:] on EVENT [once] [when CONDITION] do
    ACTIONS
end
```

- **`EVENT`** is one of the [events](03-events.md), with its filter: `on write
  0x4000..0x57FF`, `on frame 100`, `on nextreg 0x51`.
- **`once`** turns the rule off after it has fired once.
- **`when CONDITION`** is an integer expression; the body runs only when it is
  true. The debugger evaluates it at the event, before the body, so a rule
  whose condition is false costs a comparison and nothing else. It may use the
  event's payload, the machine's state, variables and snapshots. `once` comes
  before `when`.
- **`label:`** names the rule, so `enable label` and `disable label` can switch
  it on and off. Labels are per file.
- **`disabled label:`** starts the rule switched off. A rule that is off costs
  nothing at all: the debugger takes it out of its live set. This is how a
  guard waits until you arm it — booting and loading a program write all over
  memory, and should not trip it.
- **`ACTIONS`** are the [actions](05-actions.md), run in order.

`enable` re-arms a rule whose `once` has fired. A rule that a run-time error
switched off stays off; `enable` does not bring it back.

**Order.** When several rules fire on the same event they run in the order they
are written, and across files in the order the files were loaded. A `stop` in
one rule does not keep the later rules from running — they may want to log — it
takes effect after all of them. Actions run to the end; nothing in a rule waits.

**Several files.** You can load several scripts. Each one has its own
variables, labels and snapshots, so a rule can only `enable` a rule of its own
file:

```
[debugger] [error] SCRIPT ERROR b.jds:1:22: `guard` names no labelled rule
```

## Errors

**Load-time errors** — a syntax error, an unknown name, a payload name the
event does not carry, a type error, a filter out of range — are reported as
*file*:*line*:*column*: *message*, and **nothing of the script runs**. From the
command line JNEXT exits 1 before the machine starts; from the Script menu a
message box shows the error and the script is not loaded. Syntax errors stop at
the first one; the checks after parsing report every error:

```
on write 0x4000 when PREV == 0 do
    log "x"
end
on read 0x4000 do log "prev ${PREV}" end
on frame 1 do log "${ADDR}" end
```

```
[debugger] [error] SCRIPT ERROR err2.jds:4:31: `PREV` is event payload and is not available in a `read` event
[debugger] [error] SCRIPT ERROR err2.jds:5:22: `ADDR` is event payload and is not available in a `frame` event
[debugger] [error] --script err2.jds: not loaded (2 errors)
[debugger] [error] --script: not starting; no script is loaded (exit 1)
```

More of them, as JNEXT reports them:

```
[debugger] [error] SCRIPT ERROR err1.jds:2:17: unknown variable `X` (declare it with `var`)
[debugger] [error] SCRIPT ERROR err3.jds:1:12: unknown symbol `@no_such_symbol` (not in any loaded MAP)
[debugger] [error] SCRIPT ERROR err5.jds:2:9: unterminated string
[debugger] [error] SCRIPT ERROR err6.jds:1:24: expected an action or `end`, found `en`
[debugger] [error] SCRIPT ERROR fmt.jds:1:28: unknown format 'x8' (expected x2, x4 or d)
[debugger] [error] SCRIPT ERROR strerr.jds:1:22: a string literal cannot appear inside `${…}`
```

**Run-time errors** — a division by zero, an accessor out of range, a page that
cannot be read, a refused change to the machine — **switch the rule off**,
log where, and end the run with status 1 at the next frame edge. Actions before
the error have happened; the ones after it in that rule have not:

```
var z = 0
on frame 10 do
    log "about to divide"
    log "${100 / z}"
    log "never printed"
end
on frame 20 do log "frame 20 still runs" end
```

```
[debugger] [info] SCRIPT loaded err4.jds: 2 rules
[debugger] [info] [jds F:10 C:6150160] about to divide
[debugger] [error] SCRIPT ERROR err4.jds:4:16: division by zero — rule at 2:1 disabled
[debugger] [error] SCRIPT: a run-time error disabled a rule; exiting 1
[platform] [info] script requested exit 1
```

The run ended before frame 20. In the GUI the rule shows `error (disabled)` in
the Script tab and the machine carries on.

## Limits

| What | Limit |
|---|---|
| an expression | 200 levels of nesting; every chained operator counts one, so `a or b or …` takes at most 200 terms |
| `if` inside `if` | 64 levels |
| a snapshot stack | 4096 entries per name |
| `dump_mem` | 4096 bytes |
| an `execute page` range | 16 pages |
| `log indent` | 0 to 255 spaces |
