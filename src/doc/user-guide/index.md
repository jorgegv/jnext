# JNEXT User Guide

JNEXT is a ZX Spectrum Next emulator built from the official FPGA core sources.
This guide shows you how to use it.

Chapters 1 to 5 are for running programs, and assume no development
background. Chapters 6 and 7 are for developers: the debugger, and automating
JNEXT the way its own test suite does. Chapters 8 and 9 are for everyone —
what to do when something is wrong, and where everything else lives.

The complete list of command-line options is in the manual page — `man jnext`,
or [`USAGE.md`](https://github.com/jorgegv/jnext/blob/main/USAGE.md). This guide links there rather than repeating
it.

## Contents

| | Chapter | |
|---|---|---|
| 1 | [Introduction](01-introduction/index.md) | What JNEXT is and what it emulates |
| 2 | [Installing](02-installing/index.md) | Packages for Linux, Windows and macOS |
| 3 | [First run](03-first-run/index.md) | The SD card image and your first boot |
| 4 | [Configuration](04-configuration/index.md) | Preferences and the config file |
| 5 | [Running programs](05-running-programs/index.md) | Machines, input, display, sound, recording |
| 6 | [The debugger](06-debugger/index.md) | Every panel and every function |
| 7 | [Automation and CI](07-automation-and-ci/index.md) | Screenshot-testing your own programs |
| 8 | [Known issues](08-known-issues/index.md) | What to expect when something is wrong |
| 9 | [Reference](09-reference/index.md) | Where everything else lives |

## Getting help

Bugs and questions go to the
[issue tracker](https://github.com/jorgegv/jnext/issues). If the documentation
and the program disagree, the program is right — please report it.
