#pragma once

// Windows-only console reattachment. Isolated in its own translation unit so
// <windows.h> (which #defines OUT, DELETE, etc. — clashing with project enums)
// never pollutes any other source file. Declaration only; no <windows.h> here.
void win_attach_parent_console();

// Disarm the GH #212 CLI completion signal (an Enter posted to the parent
// console's input buffer at exit, so cmd redraws its prompt after --help and
// friends). Call once, right before the long-running app phase (GUI/headless)
// starts: past that point an exit is not a quick CLI result, and the injected
// Enter could submit a command the user typed into the waiting shell meanwhile.
void win_console_note_app_running();
