#include "debug/symbol_table.h"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace {

std::string trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

} // namespace

void SymbolTable::replace(std::vector<SymbolDefinition> definitions,
                          std::string loaded_file)
{
    std::map<uint16_t, std::string> by_address;
    std::map<std::string, uint16_t> by_name;
    for (auto& definition : definitions) {
        if (definition.display_name.empty()) continue;
        by_address.try_emplace(definition.address, definition.display_name);
        by_name.try_emplace(definition.display_name, definition.address);
        for (auto& alias : definition.aliases) {
            if (!alias.empty()) by_name.try_emplace(std::move(alias), definition.address);
        }
    }
    addr_to_name_ = std::move(by_address);
    name_to_addr_ = std::move(by_name);
    loaded_file_ = std::move(loaded_file);
}

std::optional<std::string> SymbolTable::lookup(uint16_t address) const
{
    const auto found = addr_to_name_.find(address);
    return found == addr_to_name_.end()
        ? std::nullopt : std::optional<std::string>(found->second);
}

std::optional<uint16_t> SymbolTable::lookup_name(const std::string& name) const
{
    const auto found = name_to_addr_.find(name);
    return found == name_to_addr_.end()
        ? std::nullopt : std::optional<uint16_t>(found->second);
}

std::optional<uint16_t> SymbolTable::resolve(std::string_view text) const
{
    std::string value = trim(text);
    if (value.empty()) return std::nullopt;
    if (const auto symbol = lookup_name(value)) return symbol;

    if (value.front() == '$') {
        value.erase(value.begin());
    } else if (value.size() > 2 && value[0] == '0'
               && (value[1] == 'x' || value[1] == 'X')) {
        value.erase(0, 2);
    }
    if (value.empty()
        || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isxdigit(c) != 0;
           })) {
        return std::nullopt;
    }

    unsigned int address = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), address, 16);
    if (error != std::errc{} || end != value.data() + value.size()
        || address > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(address);
}

void SymbolTable::clear()
{
    addr_to_name_.clear();
    name_to_addr_.clear();
    loaded_file_.clear();
}
