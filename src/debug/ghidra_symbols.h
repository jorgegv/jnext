#pragma once

// ---------------------------------------------------------------------------
// The Ghidra symbol export (GH #389, E23-26): the files Ghidra's own
// `ImportSymbolsScript.py` reads, one label per line, `NAME ADDRESS l`.
//
// The script splits a line on whitespace (name, address, then `f` for a
// function or anything else for a label), has no comment syntax and raises on a
// blank line, and it STOPS at the first line whose address space the program
// lacks (a NullPointerException; Ghidra still exits 0). That is why the export is
// one file per space: a space the user has not created as an overlay then costs
// only its own file. Measured against Ghidra 12.2 (design 11.2).
//
// Files:
//   `<TAG>.ghidra.txt`  the labels of one space, `NAME <TAG>:0xHHHH l`; Ghidra
//                       reads `<TAG>` as the name of an address space (an overlay
//                       the user creates over the page's bytes)
//   `CPU.ghidra.txt`    the unpaged labels, `NAME 0xHHHH l`
//   `view.ghidra.txt`   the labels the disassembly shows NOW, `NAME 0xHHHH l`, for
//                       flat memory with no overlay at all (on a 48K or 128K machine
//                       every RAM label is paged, so the other files need overlays)
// A file is written only when it has a line. Sorted by address, then name.
//
// Pure: std, `mem_space.h`, `long_address.h`. No machine.
// ---------------------------------------------------------------------------

#include "debug/reexport.h"

namespace jnext {
namespace dbg {
namespace ghidra {

/// A label of the view: the name the disassembly shows at `addr` for the memory
/// that supplies it now (`Debugger::lookup()`).
struct ViewLabel {
    uint16_t addr = 0;
    std::string name;
};

reexport::Output build(const std::vector<reexport::Label>& labels,
                       const std::vector<ViewLabel>& view);

}  // namespace ghidra
}  // namespace dbg
}  // namespace jnext
