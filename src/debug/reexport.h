#pragma once

// ---------------------------------------------------------------------------
// The reverse-engineering exports (GH #389, E23-26): what the pure writers of
// `skoolkit_ctl.h` and `ghidra_symbols.h` take and give. std headers and
// `mem_space.h` only: no machine, no Qt (`Debugger::export_skoolkit()` and
// `export_ghidra()` gather the inputs from the backend and write the files).
//
// Design: doc/design/SOURCE-LEVEL-DEBUGGING.md section 11.2.
// ---------------------------------------------------------------------------

#include "debug/mem_space.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jnext {
namespace dbg {
namespace reexport {

/// One label as the disassembly shows it: the name `SymbolTable::lookup()` gives
/// for the key. `space` unset is an UNPAGED label (names its address in every memory).
struct Label {
    std::optional<MemSpace> space;
    uint16_t addr = 0;
    std::string name;
};

/// A comment on the instruction or byte at (space, addr); always paged.
struct Comment {
    MemSpace space;
    uint16_t addr = 0;
    std::string text;
};

/// One file of an export: its name inside the export directory, and its bytes.
struct File {
    std::string name;
    std::string content;
};

/// A writer's result. `labels` / `comments` count the entries written; `notes` are
/// the things the user should know about (dropped whitespace, a name skipped, ...).
struct Output {
    std::vector<File> files;
    std::vector<std::string> notes;
    size_t labels = 0;
    size_t comments = 0;
};

/// Whether `name` can stand in a control-file or symbol-file line: non-empty, and
/// neither whitespace nor a control character (both formats split on whitespace).
bool name_exportable(const std::string& name);

}  // namespace reexport
}  // namespace dbg
}  // namespace jnext
