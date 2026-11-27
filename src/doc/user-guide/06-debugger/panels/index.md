# Panels

## Greyed actions and panels

When the debugger is attached to a machine that cannot do something, the matching menu items and buttons
are greyed, and their tooltip says why (`Not available on <machine>: <reason>`). A panel whose data the
machine cannot give shows that reason instead of its content. In the [CPU registers](01-cpu-registers.md) a
`?` means the machine does not report that value. A greyed control comes back by itself when the machine
can do it again.
