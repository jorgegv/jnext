# Quick reference

## A script

```
; comments: ;  //  #   (never inside a string)
var NAME = EXPR                          ; an integer, set once when the script loads

[disabled] [LABEL:] on EVENT [once] [when EXPR] do
    ACTION ...
end
```

## Events

| Event | Filter | Payload (besides `PC`) |
|---|---|---|
| `execute` | `A[..B] [page P]`, `page P[..Q]` (≤ 16 pages) | `ADDR PAGE` |
| `read` | `A[..B] [page P]`, `page P[..Q]` | `ADDR VALUE PAGE SOURCE` |
| `write` | `A[..B] [page P]`, `page P[..Q]` | `ADDR VALUE PREV PAGE SOURCE` |
| `io_read`, `io_write` | `P[..Q]` (≤ 0xFF: low byte), `mask M value V` | `PORT VALUE SOURCE` |
| `nextreg` | `R[..S]` | `REG VALUE PREV SOURCE` |
| `frame` | `[N]` | — |
| `scanline` | `N` (`CVC` line, 0..1023) | — |
| `cycle` | `N` (master cycles) | — |
| `interrupt`, `nmi`, `reset` | — | — |
| `hostkey` | `N` (1..8) | `KEY` |
| `copper move` | `[R[..S]] [at A[..B]]` | `CPC HC_ULA CVC REG VALUE` |
| `copper wait` | `[at A[..B]]` | `CPC HC_ULA CVC WAIT_V WAIT_H` |
| `copper halt` | `[at A[..B]]` | `CPC HC_ULA CVC` |
| `dma start` | — | `SRC DST LEN DMA_MODE IO_SRC IO_DST` |
| `dma byte` | `[A[..B]]` (destination) | `SRC DST VALUE IO_SRC IO_DST` |
| `dma end` | — | `SRC DST LEN DMA_MODE` |
| `stop` | — | `REASON` (a string) |

`SOURCE` is `CPU` (0), `DMA` (1) or `COPPER` (2).

## State

| Names | |
|---|---|
| `A B C D E H L F I R AF BC DE HL IX IY SP PC AF2 BC2 DE2 HL2` | registers |
| `SF ZF HF PF NF CF`, `IFF1 IFF2 IM HALTED` | flags, interrupt state |
| `mem[a] mem16[a] phys[p, o] stack[n]` | memory |
| `mmu[s] page[s] nextreg[r]` | MMU, NextREGs |
| `FRAME CYCLE TFRAME` | time |
| `RAW_HC RAW_VC HC_ULA VC_ULA CVC PHC` | the raster |
| `MACHINE AUDIO_MUTE` | 0 48K, 1 128K, 2 +3, 4 Next; the mute mask |
| `@name` | a MAP symbol (`; addr` or `; const`) |
| `NAME.FIELD`, `changed(NAME, GROUP)`, `depth(NAME)` | snapshots; GROUP is `regs`, `mmu`, `iff1` or `stack0` |

## Actions

| Action | |
|---|---|
| `log [indent N] "…"` | a log line; `${expr}`, `${expr:FMT}` with FMT `x2`, `x4` or `d` |
| `stop ["…"]`, `assert EXPR "…"` | fail: stop here (exit 3 headless) |
| `exit N` | end the run with status N (0..255) |
| `dump_regs`, `dump_mmu`, `dump_mem A LEN` | dumps (`LEN` ≤ 4096) |
| `snap N`, `unsnap N`, `dump_diff N` | snapshots |
| `enable L`, `disable L` | switch a labelled rule |
| `screenshot "f.png"` or `"f.scr"`, `save_snapshot "f"` | at the next frame edge |
| `compare_scr "f.scr" "…"` | at the next frame edge; mismatch = failed assert |
| `press "K" [for N]`, `release "K"` | keys, at the next frame edge |
| `joystick N BITS` | connector N (1 or 2), 12 bits, at the next frame edge |
| `set VAR = EXPR` | a variable |
| `set TARGET = EXPR` | change the machine (logged `MUTATE`); TARGET is a register, a flag, `IFF1`, `IFF2`, `IM`, `mem[]`, `mem16[]`, `phys[]`, `nextreg[]` or `AUDIO_MUTE` |
| `out PORT VALUE` | change the machine (logged `MUTATE`) |
| `if EXPR then … [else …] end` | |

## Operators, loosest first

`or` · `and` · `not` · `== != < > <= >=` · `| ^` · `&` · `<< >>` · `+ -` ·
`* / %` · unary `- ~`. 32-bit signed, wrapping; all left-associative.

## Numbers and keys

Numbers: `255`, `0xFF`, `$FF`, `0b11111111`, `true`, `false`.

Keys: `a`..`z`, `0`..`9`, `enter`, `space`, `.` `,` `;` `:`, `sym+X`, `caps+X`,
`up` `down` `left` `right` (CAPS + 7 6 5 8), `"row,col"`, `ext:right left down
up dot comma quote semicolon extend capslock graph truevideo invvideo break edit
delete`.

Joystick bits: 0 right, 1 left, 2 down, 3 up, 4 B (fire 1), 5 C (fire 2), 6 A,
7 START, 8 Y, 9 Z, 10 X, 11 MODE.

## Command line

| Option | |
|---|---|
| `--script FILE` | load a script (repeatable) |
| `--map FILE` | load a z88dk MAP for `@name` |
| `--script-key FRAME N` | host key N at frame FRAME (headless) |
| `--record-script FILE` | record the session |
| `--delayed-automatic-exit-frames N` | the bound — and the watchdog |

## Exit status (headless and SDL)

| | |
|---|---|
| 0 | clean, or `exit 0` |
| N | `exit N` |
| 3 | `stop`, failed `assert` / `compare_scr`, a declared verdict never reached |
| 1 | a script that does not load, a run-time error, a capture or snapshot not written |

A failure at the same event as `exit 0` wins. In the GUI a script never exits:
it pauses.
