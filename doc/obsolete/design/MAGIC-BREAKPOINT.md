# MAGIC BREAKPOINT

This is an extremely useful feature for Z80 assembly development, as it allows you to insert "stop points" directly into your source code without relying on the emulator's manual interface.

The emulator you are referring to is **ZEsarUX** (created by César Hernández Bañó), though this feature is also supported by **Spectaculator** and **CSpect**.

### The Opcode: `ED FF`

The exact operation code is **`ED FF`**.

### How does it work?
In the Z80 processor architecture, the `ED` prefix opens a table of extended instructions. However, not all combinations in that table are documented or implemented in the original silicon.

* **On a real machine:** The processor interprets `ED FF` as a **`NOP`** (No Operation). It simply consumes a few clock cycles (8 T-states) and moves to the next instruction without altering registers or flags.
* **In the emulator:** The emulator monitors the execution flow. As soon as the Program Counter (PC) hits the `ED FF` sequence, the emulator pauses execution and automatically opens the **Debugger** window.

---

### Use in Source Code
Developers typically define a macro or an alias to make it easier to use in assemblers like Pasmo or SjASMPlus:

```assembly
BREAKPOINT: MACRO
            db $ED, $FF
            ENDM

; Usage example:
LD A, 10
CALL CalculateRoute
BREAKPOINT         ; The emulator will stop here so you can check the registers
```

### Other Emulators and Variants
While ZEsarUX is the most well-known for this implementation in the Spectrum development scene, there are other similar cases:

| Emulator | Breakpoint Opcode |
| :--- | :--- |
| **ZEsarUX / Spectaculator** | `ED FF` |
| **CSpect** | `DD 01` (Often used for ZX Spectrum Next development) |
| **Fuse** | Allows setting events, but doesn't have a "hardcoded" opcode in the same way. |

Essentially, it is the equivalent of the famous `INT 3` (`CC`) in x86 architecture, but adapted "unofficially" to make life easier for retro-scene programmers.
