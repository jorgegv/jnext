#pragma once

// Windows-only console reattachment. Isolated in its own translation unit so
// <windows.h> (which #defines OUT, DELETE, etc. — clashing with project enums)
// never pollutes any other source file. Declaration only; no <windows.h> here.
void win_attach_parent_console();
