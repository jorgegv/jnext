#pragma once

#ifdef _WIN32

#include <string>

/// Run `command_line` as a child process and block until it exits, WITHOUT a
/// console window and with the child's stdio pointed at `NUL`.
///
/// Used instead of system() on Windows (GH #56). system() runs the command
/// through `cmd.exe`, and jnext.exe is deliberately linked as a GUI-subsystem
/// binary (`-Wl,--subsystem,windows`) so an Explorer launch shows no console —
/// which means cmd.exe would have no console to inherit and Windows would
/// allocate and SHOW a fresh one. VideoRecorder probes for ffmpeg at every
/// startup, so that is a console window flashing on every launch. Spawning the
/// child directly with CREATE_NO_WINDOW avoids it, and removes the cmd.exe
/// quoting layer entirely.
///
/// Returns the child's exit code, or -1 if the process could not be started
/// (which is how "ffmpeg is not on PATH" is reported).
int win_run_hidden(const std::string& command_line);

#endif  // _WIN32
