#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <functional>
#include <string>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

#include "debug/mem_space.h"

/// WHICH memory supplies an address NOW: what `Debugger::source_page()` answers.
using SymbolSpaceOf = std::function<jnext::dbg::FetchSource(uint16_t)>;

/// One named address. A PAGED label (`space` set) names its address only while
/// that memory supplies it (a sjasmplus SLD label assembled for page 20); an
/// UNPAGED one (`space` nullopt) names it in every memory (GH #375).
struct SymbolLabel {
    std::string name;
    std::optional<jnext::dbg::FetchSource> space;
    bool operator==(const SymbolLabel& o) const { return name == o.name && space == o.space; }
};

/// The address a name resolves to, and the memory it was assembled for (nullopt:
/// any).
struct SymbolNameKey {
    uint16_t addr = 0;
    std::optional<jnext::dbg::FetchSource> space;
    bool operator==(const SymbolNameKey& o) const { return addr == o.addr && space == o.space; }
};

/// Symbol table loaded from a MAP file (e.g. Z88DK linker output), a
/// Memory.txt, an sjasmplus --sym/--exp file or the labels of an SLD.
/// Labels are keyed by (memory, address); there is no address-only lookup.
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
    /// Returns the number of "; addr" symbols loaded, or -1 on error. A file that
    /// is not a regular file or is over the size cap is refused before it is
    /// opened, the table untouched, and `why`, when given, says so; a file that
    /// simply cannot be opened leaves `why` empty.
    int load_z88dk_map(const std::string& path, std::string* why = nullptr);

    /// Load symbols from a simple map file (SYMBOL = $ADDR format).
    /// Lines starting with ';' are comments. No metadata filtering.
    /// Returns number of symbols loaded, or -1 on error; `why` as for
    /// `load_z88dk_map()`.
    int load_simple_map(const std::string& path, std::string* why = nullptr);

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

    /// Resolve like `resolve()`, keeping the memory a symbol name was assembled
    /// for. A number, and an unpaged name, carry no memory.
    std::optional<SymbolNameKey> resolve_key(std::string_view text) const;

    /// Add a label. A paged label (`space` set) keys on (space, addr), an
    /// unpaged one on addr; the first label of a key wins and, apart, the
    /// first of a name. Returns whether the key was free (the entry stored).
    bool add_label(const std::string& name, uint16_t addr,
                   std::optional<jnext::dbg::FetchSource> space);

    /// Add a NAME-ONLY value (a size, an offset, a section bound): `find_name()`
    /// resolves it, no address is ever named by it. The first of a name wins.
    void add_value(const std::string& name, uint16_t value);

    /// Record the file the table was loaded from (a loader that fills the table
    /// through `add_label()`).
    void set_loaded_file(std::string path) { loaded_file_ = std::move(path); }

    /// Add another table's entries after this one's: its labels whose key (memory,
    /// address) is still free, its names (labels' and aliases') not yet taken, its
    /// name-only values not yet taken. First wins throughout, as within one table.
    /// An empty `loaded_file()` takes the other's.
    void merge(const SymbolTable& other);

    /// The name of `addr` in the memory `space`: the paged label with exactly
    /// that key, else the unpaged label at `addr`. `MemSpace::cpu()` is "no
    /// memory known": it reaches unpaged labels only.
    std::optional<std::string> lookup(jnext::dbg::FetchSource space, uint16_t addr) const;

    /// The visible label with the greatest address <= `addr` (address and name):
    /// an unpaged one, or a paged one whose space is what `space_of` says
    /// supplies its address.
    std::optional<std::pair<uint16_t, std::string>>
    nearest(uint16_t addr, const SymbolSpaceOf& space_of) const;

    /// The address, and memory, of a name — a label first, else a name-only
    /// value (no memory). Returns nullopt if not found.
    std::optional<SymbolNameKey> find_name(const std::string& name) const;

    /// Look up an address by symbol name (`find_name()`'s address).
    std::optional<uint16_t> lookup_name(const std::string& name) const;

    /// Every label, by address (equal addresses in the order they were added).
    const std::multimap<uint16_t, SymbolLabel>& symbols() const { return labels_; }

    /// Clear all loaded symbols.
    void clear();

    /// Returns true if no symbols are loaded.
    bool empty() const { return labels_.empty(); }

    /// Number of labels loaded: distinct (memory, address) keys.
    size_t size() const { return labels_.size(); }

    /// Get the loaded file path (empty if none loaded).
    const std::string& loaded_file() const { return loaded_file_; }

private:
    /// The line loops of the z88dk and sjasmplus loaders, on a stream that is
    /// already open: they add to the table and return the number of entries.
    /// Opening, `clear()` and `loaded_file_` are the callers'.
    int read_z88dk_map(std::istream& file);
    int read_sjasmplus_equ(std::istream& file);

    /// Name `name` (first wins) without storing a label: the aliases a
    /// Memory.txt label also answers to.
    void add_name(const std::string& name, uint16_t addr,
                  std::optional<jnext::dbg::FetchSource> space);

    std::multimap<uint16_t, SymbolLabel> labels_;
    std::map<std::string, SymbolNameKey> name_to_key_;
    /// Name-only values, consulted by `find_name()` after the labels.
    std::map<std::string, uint16_t> const_by_name_;
    std::string loaded_file_;
};
