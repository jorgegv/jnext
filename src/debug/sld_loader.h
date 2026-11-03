#pragma once

#include <string>
#include <vector>

class SourceMap;
class SymbolTable;

struct SourceMapLoadResult {
    int count = -1;
    std::string error;
    /// The sidecar a `load_sld_sidecar()` result is about, also when that file
    /// was refused; empty when no sidecar was found, and for `load_sld()`.
    std::string file;
    /// `L` label records accepted into the label table; -1 when no label table
    /// was given or the file was refused as a whole.
    int labels = -1;
    /// z88dk only: the non-data line ranges stored that no section binary covers,
    /// so the program identity (if any) does not check them.
    int unchecked = 0;
    /// z88dk only (GH #348): the `--list` listings beside the map that were used,
    /// the map line records they were cross-checked against, how many of those
    /// they place elsewhere, and a sentence for each listing that was ignored or
    /// dropped and for each other thing worth telling the user.
    int listings = 0;
    int listing_checks = 0;
    int listing_disagreements = 0;
    std::vector<std::string> listing_notes;

    explicit operator bool() const { return count >= 0; }
};

/// Load sjasmplus SLD v1 source traces into a compiler-neutral SourceMap, and,
/// when `labels` is given, its `L` records into that table (GH #375). Two
/// transactions over one file (design 9.1): a file refused as a whole (missing
/// header, bad device or identity record) loads neither and leaves both
/// untouched; a bad trace refuses the source half (`count == -1`, `error`) and
/// the labels still load (`labels >= 0`). The label half is lenient: a record
/// it cannot place is skipped. `*labels` is cleared and refilled on a load.
SourceMapLoadResult load_sld(SourceMap& map, const std::string& path,
                             SymbolTable* labels = nullptr);

/// Load <program>.sld or <program>.sld.txt beside a program image.
SourceMapLoadResult load_sld_sidecar(SourceMap& map,
                                     const std::string& program_path,
                                     SymbolTable* labels = nullptr);
