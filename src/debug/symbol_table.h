#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <optional>

/// Simple symbol table: maps addresses to names and vice versa.
/// Supports loading Z88DK .map files.
class SymbolTable {
public:
    SymbolTable() = default;

    /// Load a Z88DK-format .map file.
    /// Returns true on success.
    bool load_z88dk_map(const std::string& path);

    /// Clear all symbols.
    void clear();

    /// Number of symbols loaded.
    size_t size() const { return by_addr_.size(); }

    /// Look up a symbol name by address.
    std::optional<std::string> lookup(uint16_t addr) const;

    /// Look up an address by symbol name.
    std::optional<uint16_t> lookup(const std::string& name) const;

    /// Add a symbol manually.
    void add(uint16_t addr, const std::string& name);

private:
    std::unordered_map<uint16_t, std::string> by_addr_;
    std::unordered_map<std::string, uint16_t> by_name_;
};
