#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SymbolDefinition {
    uint16_t address = 0;
    std::string display_name;
    std::vector<std::string> aliases;
};

/// Compiler-neutral symbol names indexed by logical address.
class SymbolTable {
public:
    void replace(std::vector<SymbolDefinition> definitions,
                 std::string loaded_file);

    std::optional<std::string> lookup(uint16_t address) const;
    std::optional<uint16_t> lookup_name(const std::string& name) const;

    /// Resolve a loaded symbol or a hexadecimal address. Numeric forms are
    /// 4000, $4000 and 0x4000.
    std::optional<uint16_t> resolve(std::string_view text) const;

    const std::map<uint16_t, std::string>& symbols() const { return addr_to_name_; }
    void clear();
    bool empty() const { return addr_to_name_.empty(); }
    size_t size() const { return addr_to_name_.size(); }
    const std::string& loaded_file() const { return loaded_file_; }

private:
    std::map<uint16_t, std::string> addr_to_name_;
    std::map<std::string, uint16_t> name_to_addr_;
    std::string loaded_file_;
};
