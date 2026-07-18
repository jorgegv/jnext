#pragma once

#include <string>

class SymbolTable;

int load_z88dk_map(SymbolTable& symbols, const std::string& path);
int load_simple_map(SymbolTable& symbols, const std::string& path);
int load_nextbuild_memory(SymbolTable& symbols, const std::string& path);
int load_nextbuild_sidecar(SymbolTable& symbols,
                           const std::string& program_path);
