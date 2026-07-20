# Call stack

The chain of calls that led to the current instruction: depth number, the type
(`CALL`, `RST`, `INT` or `NMI`), the caller address, and the target. Most
recent call first. With a MAP file loaded the target column shows the symbol
name instead of the address.

Call tracking only runs while the debugger is open, so the stack is built from
the moment you open it — not retroactively.
