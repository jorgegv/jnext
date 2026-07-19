# Run, pause, and the step family

| Action | Key | Toolbar |
|---|---|---|
| Run / Continue | **F5** | `F5: Continue` |
| Pause / Break | **F9** | `F9: Break` |
| Single Step (into) | **F6** | `F6: Single Step` |
| Step Over | **F7** | `F7: Step Over` |
| Step Out | **F8** | `F8: Step Out` |
| Source Step Into | **Ctrl+F6** | Source Step menu |
| Source Step Over | **Ctrl+F7** | Source Step menu |
| Source Step Out | **Ctrl+F8** | Source Step menu |
| Source Step Back | **Ctrl+Shift+F7** | Source Step menu |
| Reverse Continue to Source Breakpoint | **Ctrl+Shift+F5** | Source Step menu |

The same actions are in the **Debug** menu.

**Single Step** executes exactly one instruction, entering calls, traps and
interrupt handlers.

**Step Over** sets a one-shot breakpoint after the current instruction and
resumes, so a subroutine runs at full speed and you land on the following
instruction. It only does this for instructions that can come back —
`CALL nn`, `CALL cc,nn`, `RST n` and `DJNZ`. For anything else it is identical
to Single Step. Note the consequence for `DJNZ`: stepping over it runs the
whole loop.

**Step Out** resumes until a `RET`, `RETI` or `RETN` executes with `SP` at or
above its value when you pressed it — that is, until the current subroutine
returns rather than a nested one.

If a subroutine never returns (it jumps away, or is waiting on input that never
arrives) Step Over and Step Out simply keep running; press **F9** to take back
control.

The Source Step actions use the loaded SLD map. They run across instructions
that belong to the same source position and stop at the next different
file/line/column. Unmapped compiler or runtime code is crossed automatically;
the normal disassembly remains available when no source position exists.

Source Step Back and Reverse Continue require rewind to be enabled. Repeated
loop iterations at the same source position count as one location rather than
one step each.
