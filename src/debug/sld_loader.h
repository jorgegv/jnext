#pragma once

#include <string>

class SourceMap;

struct SourceMapLoadResult {
    int count = -1;
    std::string error;
    /// The sidecar a `load_sld_sidecar()` result is about, also when that file
    /// was refused; empty when no sidecar was found, and for `load_sld()`.
    std::string file;

    explicit operator bool() const { return count >= 0; }
};

/// Load sjasmplus SLD v1 source traces into a compiler-neutral SourceMap.
SourceMapLoadResult load_sld(SourceMap& map, const std::string& path);

/// Load <program>.sld or <program>.sld.txt beside a program image.
SourceMapLoadResult load_sld_sidecar(SourceMap& map,
                                     const std::string& program_path);
