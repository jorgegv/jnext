#pragma once

// ---------------------------------------------------------------------------
// save_snapshot_file — write the machine as a snapshot FILE, the format chosen
// by the path's extension (GH #276 B4, §4.5 CAP-04).
//
// `.jns` → `Emulator::save_jns_file()`; `.szx` → `SzxSaver`; `.nex` →
// `NexSaver`; anything else → `.sna` (`SnaSaver`, the 48K form). The debugger
// backend's `save_snapshot()` is its caller. The same table is spelled out
// twice more today — `HeadlessApp`'s `--delayed-snapshot` and
// `MainWindow::on_save_snapshot()` — and this is the copy they are meant to
// call instead (the B4 frontend half and package Q respectively), so that the
// extension table exists once.
//
// PRECONDITION: the machine is at a frame boundary. This function does not
// advance it — who waits for the boundary is the caller's decision (the
// backend refuses or advances by ST-01's rule), and a saver that advanced on
// its own would hide that decision.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>

class Emulator;

/// Write the snapshot. True iff the whole file was written; on false `error`
/// says why (the saver's own reason where it has one — a `.szx` or `.sna` of a
/// machine the format cannot represent, a `.jns` refusal — else the I/O
/// failure). `bytes` receives the file's size on success.
bool save_snapshot_file(Emulator& emu, const std::string& path, std::string& error,
                        size_t& bytes);
