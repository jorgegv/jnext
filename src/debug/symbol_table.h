#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <optional>

/// Symbol table loaded from a MAP file (e.g. Z88DK linker output).
/// Pure C++ — no GUI dependency.
class SymbolTable {
public:
    /// Load symbols from a Z88DK .map file (only "; addr" lines).
    /// Returns number of symbols loaded, or -1 on error.
    int load_z88dk_map(const std::string& path);

    /// Load symbols from a simple map file (SYMBOL = $ADDR format).
    /// Lines starting with ';' are comments. No metadata filtering.
    /// Returns number of symbols loaded, or -1 on error.
    int load_simple_map(const std::string& path);

    /// Look up a symbol name by its address. Returns nullopt if not found.
    std::optional<std::string> lookup(uint16_t addr) const;

    /// Look up an address by symbol name. Returns nullopt if not found.
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
    std::string loaded_file_;
};
