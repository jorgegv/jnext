# Run, pause, and the step family

| Action | Key | Toolbar |
|---|---|---|
| Run / Continue | **F5** | `F5: Continue` |
| Pause / Break | **F9** | `F9: Break` |
| Single Step (into) | **F6** | `F6: Single Step` |
| Step Over | **F7** | `F7: Step Over` |
| Step Out | **F8** | `F8: Step Out` |

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
