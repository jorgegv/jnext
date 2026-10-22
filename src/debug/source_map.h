#pragma once

#include "debug/mem_space.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// A position in the source. No address and no page: where it was emitted is
/// the store's business (SourceRange).
struct SourceLocation {
    std::string file;   // normalised: '/' separators, no leading "./"
    int line = 0;       // one-based
    int column = 0;     // one-based; zero when absent
};

/// What a format adapter emits (design 9.1): the bytes [lo, hi_excl) of one
/// memory space came from `location`. `hi_excl == lo` is a zero-width line.
struct SourceRange {
    jnext::dbg::FetchSource space;
    uint16_t lo = 0;
    uint32_t hi_excl = 0;   // lo..0x10000
    SourceLocation location;
};

/// A zero-width line that resolves through a (non-zero-width) range.
struct SourceAlias {
    std::string file;
    int line = 0;
    size_t range = 0;   // index into the vector given to replace()
};

/// One start of a resolved line.
struct SourceAddress {
    jnext::dbg::FetchSource space;
    uint16_t address = 0;
};

struct SourceResolution {
    std::vector<SourceAddress> starts;   // sorted by (space, address), unique
    std::string error;   // "no code at <expression>" when the text names a file of
                         // the map but no start; empty otherwise
};

/// Optional binary identity a source map may carry: the SHA-256 of `size`
/// bytes of logical memory from `org`, as they were right after the load.
struct SourceProgramIdentity {
    std::string sha256;
    uint16_t org = 0;
    uint32_t size = 0;
};

/// Compiler-neutral source ranges indexed by the memory space that supplied
/// the fetch. Format adapters replace the contents transactionally.
///
/// Every `const SourceLocation*` this class returns points into the store and
/// is valid until the next replace() / clear(): copy what you keep.
class SourceMap {
public:
    /// `ranges` in emission order (the tie-break between equal-length ranges).
    void replace(std::vector<SourceRange> ranges,
                 std::vector<SourceAlias> aliases,
                 std::optional<SourceProgramIdentity> identity,
                 std::string loaded_file);

    /// The primary location of the ranges containing `address` in `space`: the
    /// shortest, on a tie the later emitted. Null when none. No allocation.
    const SourceLocation* lookup(jnext::dbg::FetchSource space, uint16_t address) const;

    /// Every location there: primary first, then ascending length, then
    /// later-emitted first.
    std::vector<const SourceLocation*> lookup_all(jnext::dbg::FetchSource space,
                                                  uint16_t address) const;

    /// Resolve `file:line`. The file is an exact path, else the unique file whose
    /// path ends in the text on a component boundary. The starts are the line's
    /// entries: a range that continues another of the same line is not one.
    SourceResolution resolve(std::string_view expression) const;

    /// Verify optional identity metadata against live logical memory: nullopt
    /// when the map carries none, else whether the bytes still match.
    std::optional<bool> verify_program(
        const std::function<uint8_t(uint16_t)>& read) const;

    /// Same file, line and column (one statement can emit several ranges).
    static bool same_position(const SourceLocation& a, const SourceLocation& b) {
        return a.file == b.file && a.line == b.line && a.column == b.column;
    }

    void clear();
    bool empty() const { return ranges_.empty(); }
    size_t size() const { return ranges_.size(); }   // ranges, zero-width included
    const std::string& loaded_file() const { return loaded_file_; }

private:
    struct Range {
        uint32_t space_key = 0;   // kind << 16 | index
        uint32_t lo = 0;
        uint32_t hi = 0;
        size_t location = 0;      // into locations_
    };
    struct Segment {              // an elementary interval with its covering ranges
        uint64_t start = 0;       // space_key << 20 | lo
        uint32_t end = 0;         // exclusive, same space
        size_t first = 0;         // into cover_
        size_t count = 0;
    };
    using FileLine = std::pair<std::string, int>;

    const Segment* segment_at(jnext::dbg::FetchSource space, uint16_t address) const;

    std::vector<SourceLocation> locations_;
    std::vector<Range> ranges_;
    std::vector<Segment> segments_;
    std::vector<size_t> cover_;                    // range indices, primary first
    std::map<FileLine, std::vector<size_t>> by_line_;   // non-zero-width ranges
    std::map<FileLine, size_t> aliases_;
    std::set<std::string> files_;
    std::string loaded_file_;
    std::optional<SourceProgramIdentity> identity_;
};
