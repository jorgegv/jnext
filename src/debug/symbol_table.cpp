#include "symbol_table.h"

#include <fstream>
#include <sstream>
#include <algorithm>

int SymbolTable::load_z88dk_map(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return -1;

    clear();

    int count = 0;
    std::string line;

    while (std::getline(file, line)) {
        // "; addr" lines are memory addresses: they name addresses (the
        // disassembler's immediates) and resolve by name. "; const" lines are
        // compile-time values — a size, or a section bound such as
        // `__data_crt_head` — so they must never name an address, but a script
        // or a remote client may still ask for one by name (#279).
        auto semi = line.find(';');
        if (semi == std::string::npos)
            continue;
        std::string metadata = line.substr(semi + 1);
        // Trim leading whitespace from metadata
        auto md_start = metadata.find_first_not_of(" \t");
        if (md_start == std::string::npos)
            continue;
        metadata = metadata.substr(md_start);
        // "; const" values (section bounds, sizes) are kept by NAME only:
        // `lookup_name()` resolves them, `lookup()` never sees them.
        const bool is_const = metadata.substr(0, 5) == "const";
        if (metadata.substr(0, 4) != "addr" && !is_const)
            continue;

        // Strip metadata for value parsing
        std::string def_part = line.substr(0, semi);

        // Find '=' separator
        auto eq = def_part.find('=');
        if (eq == std::string::npos)
            continue;

        // Extract symbol name (first non-whitespace token before '=')
        std::string name_part = def_part.substr(0, eq);
        // Trim trailing whitespace
        auto end = name_part.find_last_not_of(" \t\r\n");
        if (end == std::string::npos)
            continue;
        // Trim leading whitespace
        auto start = name_part.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        std::string name = name_part.substr(start, end - start + 1);
        if (name.empty())
            continue;

        // Extract hex value after '=' — look for '$' prefix
        std::string val_part = def_part.substr(eq + 1);
        auto dollar = val_part.find('$');
        if (dollar == std::string::npos)
            continue;

        std::string hex_str;
        for (size_t i = dollar + 1; i < val_part.size(); ++i) {
            char c = val_part[i];
            if (std::isxdigit(static_cast<unsigned char>(c)))
                hex_str += c;
            else
                break;
        }

        if (hex_str.empty())
            continue;

        // Parse hex value — skip addresses that don't fit in 16 bits
        unsigned long addr_val = 0;
        try {
            addr_val = std::stoul(hex_str, nullptr, 16);
        } catch (...) {
            continue;
        }

        if (addr_val > 0xFFFF)
            continue;

        auto addr = static_cast<uint16_t>(addr_val);

        if (is_const) {
            const_by_name_.emplace(name, addr);   // first occurrence wins
            continue;
        }

        // First occurrence wins for both maps
        if (addr_to_name_.find(addr) == addr_to_name_.end())
            addr_to_name_[addr] = name;

        if (name_to_addr_.find(name) == name_to_addr_.end())
            name_to_addr_[name] = addr;

        ++count;
    }

    loaded_file_ = path;
    return count;
}

int SymbolTable::load_simple_map(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return -1;

    clear();

    int count = 0;
    std::string line;

    while (std::getline(file, line)) {
        // Skip comment lines
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == ';')
            continue;

        // Find '=' separator
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        // Extract symbol name
        std::string name_part = line.substr(0, eq);
        auto end = name_part.find_last_not_of(" \t\r\n");
        if (end == std::string::npos) continue;
        auto start = name_part.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        std::string name = name_part.substr(start, end - start + 1);
        if (name.empty()) continue;

        // Extract hex value after '=' — look for '$' prefix
        std::string val_part = line.substr(eq + 1);
        auto dollar = val_part.find('$');
        if (dollar == std::string::npos) continue;

        std::string hex_str;
        for (size_t i = dollar + 1; i < val_part.size(); ++i) {
            char c = val_part[i];
            if (std::isxdigit(static_cast<unsigned char>(c)))
                hex_str += c;
            else
                break;
        }
        if (hex_str.empty()) continue;

        unsigned long addr_val = 0;
        try {
            addr_val = std::stoul(hex_str, nullptr, 16);
        } catch (...) {
            continue;
        }
        if (addr_val > 0xFFFF) continue;

        auto addr = static_cast<uint16_t>(addr_val);

        if (addr_to_name_.find(addr) == addr_to_name_.end())
            addr_to_name_[addr] = name;
        if (name_to_addr_.find(name) == name_to_addr_.end())
            name_to_addr_[name] = addr;

        ++count;
    }

    loaded_file_ = path;
    return count;
}

std::optional<std::string> SymbolTable::lookup(uint16_t addr) const
{
    auto it = addr_to_name_.find(addr);
    if (it != addr_to_name_.end())
        return it->second;
    return std::nullopt;
}

std::optional<uint16_t> SymbolTable::lookup_name(const std::string& name) const
{
    auto it = name_to_addr_.find(name);
    if (it != name_to_addr_.end())
        return it->second;
    auto c = const_by_name_.find(name);
    if (c != const_by_name_.end())
        return c->second;
    return std::nullopt;
}

void SymbolTable::clear()
{
    addr_to_name_.clear();
    name_to_addr_.clear();
    const_by_name_.clear();
    loaded_file_.clear();
}
