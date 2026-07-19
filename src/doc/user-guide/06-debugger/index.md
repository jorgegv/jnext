# 6. The debugger

JNEXT ships with a full debugger for ZX Spectrum Next software. It works at
instruction level on its own, and becomes source-aware when compiler symbols
and an sjasmplus SLD source map are available. It shows you the CPU, memory
map, source or disassembly, every video layer, Copper program, NextREG file and
AY registers as the hardware sees them at that instant.

This chapter is a reference. Each panel and each function has its own section,
so you can look one up while you are debugging.

Open the debugger with **Ctrl+D**, with **View ▸ Debugger**, or with the bug
button on the emulator toolbar. The same actions close it. While it is closed
the debugger costs nothing — no breakpoint checking, no call-stack tracking, no
panel refreshes. Closing it also resumes the machine if it was paused.

The debugger opens in its own window, positioned to the right of the emulator
window. It has its own menu bar — **Debug**, **Map**, **Breakpoints**,
**Watches** and **Window** — and a control toolbar along the bottom.

**Window ▸ Attach to emulator window** keeps it pinned to the emulator's
right-hand edge as you drag or resize that window; the setting is remembered
between sessions. This needs a window system that lets an application position
its own windows, so it works on **X11** and is unavailable on **Wayland**,
where the menu item is greyed out — see
[8.3](../08-known-issues/02-the-debugger-window-does-not-follow-the-emulator-window.md)
for why, and how to run under X11 if you want it.

![The debugger window](../img/debugger-window.png)

The layout is fixed: video-related tabs on the top left, disassembly and source
in the centre, CPU registers and MMU down the right, and memory/stack tabs and
watch/breakpoint tabs along the bottom. The splitters between the areas can be
dragged.

Four panels — CPU Registers, Disassembly, Stack and Call Stack — only show data
while the machine is **paused**, and are greyed out while it runs. That is
deliberate: reading them every frame would slow the emulation down for no
benefit, since the values would be a blur. The rest refresh about four times a
second while the machine is running.

---
