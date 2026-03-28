#include "debug/symbol_table.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

bool SymbolTable::load_z88dk_map(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return false;

    clear();

    // Z88DK .map format: each line is "symbol = $XXXX ; ..."
    // or "symbol = XXXX" (hex address)
    std::string line;
    while (std::getline(f, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == ';')
            continue;

        // Find '=' separator
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos)
            continue;

        // Extract symbol name (trim whitespace)
        std::string name = line.substr(0, eq_pos);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
            name.erase(name.begin());

        if (name.empty())
            continue;

        // Extract hex value after '='
        std::string val_str = line.substr(eq_pos + 1);
        // Trim and remove comment after ';'
        auto semi_pos = val_str.find(';');
        if (semi_pos != std::string::npos)
            val_str = val_str.substr(0, semi_pos);

        // Trim whitespace
        while (!val_str.empty() && std::isspace(static_cast<unsigned char>(val_str.back())))
            val_str.pop_back();
        while (!val_str.empty() && std::isspace(static_cast<unsigned char>(val_str.front())))
            val_str.erase(val_str.begin());

        // Remove '$' prefix if present
        if (!val_str.empty() && val_str[0] == '$')
            val_str.erase(val_str.begin());
        // Remove '0x' prefix if present
        if (val_str.size() >= 2 && val_str[0] == '0' && (val_str[1] == 'x' || val_str[1] == 'X'))
            val_str = val_str.substr(2);

        if (val_str.empty())
            continue;

        // Parse as hex
        unsigned long addr_val = 0;
        try {
            addr_val = std::stoul(val_str, nullptr, 16);
        } catch (...) {
            continue;
        }

        if (addr_val <= 0xFFFF)
            add(static_cast<uint16_t>(addr_val), name);
    }

    return size() > 0;
}

void SymbolTable::clear() {
    by_addr_.clear();
    by_name_.clear();
}

std::optional<std::string> SymbolTable::lookup(uint16_t addr) const {
    auto it = by_addr_.find(addr);
    if (it != by_addr_.end())
        return it->second;
    return std::nullopt;
}

std::optional<uint16_t> SymbolTable::lookup(const std::string& name) const {
    auto it = by_name_.find(name);
    if (it != by_name_.end())
        return it->second;
    return std::nullopt;
}

void SymbolTable::add(uint16_t addr, const std::string& name) {
    by_addr_[addr] = name;
    by_name_[name] = addr;
}
