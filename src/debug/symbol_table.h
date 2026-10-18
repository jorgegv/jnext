#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <map>
#include <optional>
#include <string_view>

/// Symbol table loaded from a MAP file (e.g. Z88DK linker output).
/// Pure C++ — no GUI dependency.
class SymbolTable {
public:
    /// Load symbols from a Z88DK .map file. "; addr" lines are the symbols
    /// (named by address and by name); "; const" lines go to a separate
    /// NAME-ONLY table (GH #26 WP7 / #279): `lookup_name()` resolves them —
    /// `@__data_crt_head` in a script, a GDB name - but `lookup()`,
    /// `symbols()` and therefore the disassembler never see them, so a value
    /// that is a size or a section bound never names an address.
    /// The `-debug` records (names starting `__C_LINE_`, `__ASM_LINE_` or
    /// `__CDBINFO__`) are skipped entirely: they are not symbols, and would
    /// otherwise name the function labels' addresses (GH #352).
    /// Returns the number of "; addr" symbols loaded, or -1 on error.
    int load_z88dk_map(const std::string& path);

    /// Load symbols from a simple map file (SYMBOL = $ADDR format).
    /// Lines starting with ';' are comments. No metadata filtering.
    /// Returns number of symbols loaded, or -1 on error.
    int load_simple_map(const std::string& path);

    /// Load symbols from a NextBuild / Boriel ZX Basic `Memory.txt` (lines of
    /// the form `8000: ._Main`, hexadecimal address then label). The label is
    /// shown without its leading `.` and, for a Boriel user name, without the
    /// single `_` the compiler adds (`._Main` names `Main`); the raw label and
    /// the label without its `.` resolve by name as well. Returns the number
    /// of labels loaded, or -1 if the file cannot be read; `why`, when given,
    /// then says why (over the size cap, not a regular file, cannot be opened).
    int load_nextbuild_memory(const std::string& path, std::string* why = nullptr);

    /// Load labels from an sjasmplus `--sym` or `--exp` file (GH #390): lines
    /// of the form `NAME: EQU 0x0000ABCD`, exactly as sjasmplus writes them
    /// (a `--sym` of sjasmplus 1.17.0 or earlier spells the keyword `equ`; a
    /// trailing CR, from its Windows build, is dropped); any other line is
    /// skipped. The file carries no type and no page, so every entry is a
    /// label (named by address and by name) whatever the source declared it
    /// with: a label, an EQU, a DEFL, a struct size or a field offset. A value
    /// above 0xFFFF, or a negative one (sjasmplus writes it as 0x80000000 and
    /// up), does not fit the 16-bit table and is skipped. The first occurrence
    /// wins for an address and, apart, for a name; the count is the entries
    /// accepted, duplicates included. Returns it, or -1 if the file cannot be
    /// opened (then the table is untouched).
    int load_sjasmplus_equ(const std::string& path);

    /// Load a z88dk `.map` or an sjasmplus `--sym` / `--exp` file, whichever it
    /// is, told by the file's FIRST line: an sjasmplus EQU line (whatever its
    /// value: a real `--sym` may open with a negative one) makes it
    /// `load_sjasmplus_equ()`'s, anything else, an empty file included,
    /// `load_z88dk_map()`'s. `--map` uses it, because the file's name cannot
    /// say: sjasmplus's are free (`--sym=<filename>`), and z88dk's assembler
    /// writes a `.sym` of its own, in the map format. The file is opened and
    /// read ONCE: `--map <(cmd)` is a pipe, which cannot be opened a second
    /// time, and a first look that read the first line would take what it
    /// buffered with it. The result is that of the loader it picks, -1
    /// included (the file cannot be opened; the table is then untouched).
    int load_z88dk_or_sjasmplus(const std::string& path);

    /// Resolve the text of an address field: a hexadecimal address first
    /// (`4000`, `$4000`, `0x4000`), else a loaded symbol name. A typed `FE`
    /// or `B` is the number even when a program has a variable of that name;
    /// `@name` always means the symbol, so that variable is still reachable.
    std::optional<uint16_t> resolve(std::string_view text) const;

    /// Look up a symbol name by its address. Returns nullopt if not found.
    std::optional<std::string> lookup(uint16_t addr) const;

    /// Look up an address by symbol name — an "; addr" symbol first, else a
    /// Z88DK "; const" one. Returns nullopt if not found.
    std::optional<uint16_t> lookup_name(const std::string& name) const;

    /// Get all symbols (address -> name).
    const std::map<uint16_t, std::string>& symbols() const { return addr_to_name_; }

    /// Clear all loaded symbols.
    void clear();

    /// Returns true if no symbols are loaded.
    bool empty() const { return addr_to_name_.empty(); }

    /// Number of symbols loaded.
    size_t size() const { return addr_to_name_.size(); }

    /// Get the loaded file path (empty if none loaded).
    const std::string& loaded_file() const { return loaded_file_; }

private:
    /// The line loops of the z88dk and sjasmplus loaders, on a stream that is
    /// already open: they add to the table and return the number of entries.
    /// Opening, `clear()` and `loaded_file_` are the callers'.
    int read_z88dk_map(std::istream& file);
    int read_sjasmplus_equ(std::istream& file);

    std::map<uint16_t, std::string> addr_to_name_;
    std::map<std::string, uint16_t> name_to_addr_;
    /// Z88DK "; const" values, consulted by `lookup_name()` only.
    std::map<std::string, uint16_t> const_by_name_;
    std::string loaded_file_;
};
