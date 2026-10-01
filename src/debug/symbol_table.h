#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <map>
#include <optional>

/// Symbol table loaded from a MAP file (e.g. Z88DK linker output).
/// Pure C++ — no GUI dependency.
class SymbolTable {
public:
    /// Load symbols from a Z88DK .map file. "; addr" lines are the symbols
    /// (named by address and by name); "; const" lines go to a separate
    /// NAME-ONLY table (GH #26 WP7 / #279): `lookup_name()` resolves them —
    /// `@__data_crt_head` in a script, a ZRCP or GDB name — but `lookup()`,
    /// `symbols()` and therefore the disassembler never see them, so a value
    /// that is a size or a section bound never names an address.
    /// Returns the number of "; addr" symbols loaded, or -1 on error.
    int load_z88dk_map(const std::string& path);

    /// Load symbols from a simple map file (SYMBOL = $ADDR format).
    /// Lines starting with ';' are comments. No metadata filtering.
    /// Returns number of symbols loaded, or -1 on error.
    int load_simple_map(const std::string& path);

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
    std::map<uint16_t, std::string> addr_to_name_;
    std::map<std::string, uint16_t> name_to_addr_;
    /// Z88DK "; const" values, consulted by `lookup_name()` only.
    std::map<std::string, uint16_t> const_by_name_;
    std::string loaded_file_;
};
