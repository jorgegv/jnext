# Run, pause, and the step family

| Action | Key | Toolbar |
|---|---|---|
| Run / Continue | **F5** | `F5: Continue` |
| Pause / Break | **F9** | `F9: Break` |
| Single Step (into) | **F6** | `F6: Single Step` |
| Step Over | **F7** | `F7: Step Over` |
| Step Out | **F8** | `F8: Step Out` |

The same actions are in the **Debug** menu.

**Run / Continue** resumes free execution. When the machine is stopped *on* a
breakpoint, that breakpoint does not stop it again for the instruction you are
standing on — otherwise F5 would put you straight back where you were, having
executed nothing. The suppression is exactly one instruction wide: a breakpoint
on the *next* instruction still stops you, and coming back round a loop to the
same breakpoint stops you again. Step Over, Step Out, Run to Here and Run to
EOF/EOSL all resume through the same mechanism, so they step off the breakpoint
under the PC too. One consequence worth knowing: **Run to Here** on the line you
are already stopped at runs a full lap and stops when control comes back.

None of that applies when the machine is *not* stopped. Pressing F5 while a
program is running — the emulator window takes F5 too, whenever the debugger is
enabled — does not skip anything: your breakpoints all stay live and stop the
machine as normal. It does clear a pending **Run to Here** or step, which is
what asking to run freely means.

**Single Step** executes exactly one instruction, entering calls, traps and
interrupt handlers.

**Step Over** sets a one-shot breakpoint after the current instruction and
resumes, so a subroutine runs at full speed and you land on the following
instruction. It only does this for instructions that can come back —
`CALL nn`, `CALL cc,nn`, `RST n` and `DJNZ`. For anything else it is identical
to Single Step. Note the consequence for `DJNZ`: stepping over it runs the
whole loop.

**Step Out** resumes until a return instruction — `RET`, a taken `RET cc`,
`RETI` or `RETN` — pops the stack back *past* its level when you pressed it;
that is, until the current subroutine returns rather than a nested one. A
subroutine called after you press F8 returns onto that level, not past it, so
its own `RET` does not end the step; neither does an interrupt handler's
`RETI`, nor a bare `POP` that happens to move `SP` the same way.

If a subroutine never returns (it jumps away, or is waiting on input that never
arrives) Step Over and Step Out simply keep running; press **F9** to take back
control.
