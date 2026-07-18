#include "debug/symbol_loaders.h"

#include "debug/symbol_table.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

std::string trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::optional<uint16_t> hex_address(std::string_view text)
{
    if (text.empty()
        || !std::all_of(text.begin(), text.end(), [](unsigned char c) {
               return std::isxdigit(c) != 0;
           })) {
        return std::nullopt;
    }
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()
        || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(value);
}

std::optional<uint16_t> dollar_address(std::string_view text)
{
    const size_t dollar = text.find('$');
    if (dollar == std::string_view::npos) return std::nullopt;
    size_t end = dollar + 1;
    while (end < text.size()
           && std::isxdigit(static_cast<unsigned char>(text[end]))) ++end;
    return hex_address(text.substr(dollar + 1, end - dollar - 1));
}

std::string nextbuild_display_name(std::string name)
{
    if (!name.empty() && name.front() == '.') name.erase(name.begin());
    if (name.size() > 1 && name[0] == '_' && name[1] != '_') name.erase(name.begin());
    return name;
}

} // namespace

int load_z88dk_map(SymbolTable& symbols, const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open()) return -1;

    std::vector<SymbolDefinition> definitions;
    std::string line;
    int count = 0;
    while (std::getline(input, line)) {
        const size_t semicolon = line.find(';');
        if (semicolon == std::string::npos) continue;
        const std::string metadata = trim(std::string_view(line).substr(semicolon + 1));
        if (metadata.compare(0, 4, "addr") != 0) continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos || equals > semicolon) continue;
        const std::string name = trim(std::string_view(line).substr(0, equals));
        const auto address = dollar_address(
            std::string_view(line).substr(equals + 1, semicolon - equals - 1));
        if (name.empty() || !address) continue;
        definitions.push_back({*address, name, {}});
        ++count;
    }
    symbols.replace(std::move(definitions), path);
    return count;
}

int load_simple_map(SymbolTable& symbols, const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open()) return -1;

    std::vector<SymbolDefinition> definitions;
    std::string line;
    int count = 0;
    while (std::getline(input, line)) {
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == ';') continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string name = trim(std::string_view(line).substr(0, equals));
        const auto address = dollar_address(std::string_view(line).substr(equals + 1));
        if (name.empty() || !address) continue;
        definitions.push_back({*address, name, {}});
        ++count;
    }
    symbols.replace(std::move(definitions), path);
    return count;
}

int load_nextbuild_memory(SymbolTable& symbols, const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open()) return -1;

    std::vector<SymbolDefinition> definitions;
    std::string line;
    int count = 0;
    while (std::getline(input, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto address = hex_address(trim(std::string_view(line).substr(0, colon)));
        const std::string raw_name = trim(std::string_view(line).substr(colon + 1));
        const std::string display_name = nextbuild_display_name(raw_name);
        if (!address || raw_name.empty() || display_name.empty()) continue;

        std::vector<std::string> aliases{raw_name};
        if (raw_name.front() == '.') aliases.push_back(raw_name.substr(1));
        definitions.push_back({*address, display_name, std::move(aliases)});
        ++count;
    }
    symbols.replace(std::move(definitions), path);
    return count;
}

int load_nextbuild_sidecar(SymbolTable& symbols,
                           const std::string& program_path)
{
    namespace fs = std::filesystem;
    const fs::path program(program_path);
    std::string extension = program.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".nex") return -1;

    const fs::path named = program.parent_path()
        / (program.stem().string() + ".Memory.txt");
    if (fs::is_regular_file(named)) return load_nextbuild_memory(symbols, named.string());
    const fs::path legacy = program.parent_path() / "Memory.txt";
    if (fs::is_regular_file(legacy)) return load_nextbuild_memory(symbols, legacy.string());
    return -1;
}
