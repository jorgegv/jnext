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

/// A label read from an import file. `space` unset: the file does not say which memory (an
/// UNTAGGED entry); the importer then places it by `source_page(addr)`.
struct ImportLabel {
    std::optional<MemSpace> space;
    uint16_t addr = 0;
    std::string name;
    int line = 0;
};

struct ImportComment {
    uint16_t addr = 0;
    std::string text;   ///< lines joined with LF
    int line = 0;
};

/// What a reader found in one file (skoolkit_ctl.h `parse_ctl()`, ghidra_symbols.h
/// `parse_symbols()`), and what it did not take in.
struct Imported {
    std::optional<MemSpace> space;       ///< a control file's memory (header, else file name); unset: untagged
    std::vector<ImportLabel> labels;
    std::vector<ImportComment> comments;
    size_t titles = 0;                   ///< block titles (sna2skool writes them itself): not imported
    size_t descriptions = 0;             ///< `D` descriptions: not imported
    size_t other = 0;                    ///< R N E M L > and `@` directives other than label=: not imported
    size_t continued = 0;                ///< `.` / `:` lines that continued one of those
    std::vector<std::string> notes;
};

/// Whether `name` can stand in a control-file or symbol-file line: non-empty, and
/// neither whitespace nor a control character (both formats split on whitespace).
bool name_exportable(const std::string& name);

/// GH #349 (R2): one name, one key. Both import formats refuse a name at two addresses, and the
/// debugger's own labels hold one key per name (`UserAnnotations`); z88dk's module-local labels
/// repeat (`end_x` in two library routines). Of the labels sharing an exportable name at
/// different (memory, address) keys, the writer keeps the FIRST in a fixed order (unpaged
/// labels, then memories by kind and index, then address) and drops the rest, each with a note
/// naming the key that kept the name. Names that are not exportable pass through (the writers
/// report them); the order of the kept labels is the input's.
std::vector<Label> unique_names(const std::vector<Label>& labels, std::vector<std::string>& notes);

}  // namespace reexport
}  // namespace dbg
}  // namespace jnext
