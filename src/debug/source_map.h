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

/// `bytes` as they must read from `space` at the 16-bit logical addresses
/// `lo`, `lo + 1`, ... (a z88dk section binary, design 9.7).
struct SourceByteCheck {
    jnext::dbg::FetchSource space;
    uint16_t lo = 0;
    std::vector<uint8_t> bytes;
};

/// Optional binary identity a source map may carry: the SHA-256 of `size`
/// bytes of logical memory from `org`, as they were right after the load
/// (SLD), and/or bytes of named memory spaces (z88dk). Present when `sha256`
/// or `bytes` is not empty.
struct SourceProgramIdentity {
    std::string sha256;
    uint16_t org = 0;
    uint32_t size = 0;
    std::vector<SourceByteCheck> bytes;
};

/// Compiler-neutral source ranges indexed by the memory space that supplied
/// the fetch. Format adapters replace the contents transactionally.
///
/// Every `const SourceLocation*` this class returns points into the store and
/// is valid until the next replace() / clear(): copy what you keep.
class SourceMap {
public:
    /// `ranges` in emission order (the tie-break between equal-length ranges).
    /// `format` names the file format the map came from, for messages
    /// ("SLD", "z88dk").
    void replace(std::vector<SourceRange> ranges,
                 std::vector<SourceAlias> aliases,
                 std::optional<SourceProgramIdentity> identity,
                 std::string loaded_file,
                 std::string format = "SLD");

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

    /// One byte of a memory space at a 16-bit logical address; nullopt when the
    /// space cannot be read.
    using SpaceReader = std::function<std::optional<uint8_t>(jnext::dbg::FetchSource, uint16_t)>;

    /// Verify optional identity metadata against live memory: nullopt when the
    /// map carries none, else whether it all still matches. The SHA-256 reads
    /// logical memory through `read`; each byte check reads its own space through
    /// `read_space`, and a missing reader, an unreadable byte or a different
    /// one fails.
    std::optional<bool> verify_program(
        const std::function<uint8_t(uint16_t)>& read,
        const SpaceReader& read_space = {}) const;

    /// Whether the map carries a binary identity (a SHA-256 or byte checks).
    bool has_identity() const;

    /// GH #344 stage 3 - the memory a tape program's map is judged against, split into
    /// the UNITS the identity is verified in: each memory space a z88dk byte check lies
    /// in, and, for an SLD's SHA-256 (which hashes logical memory as one), the single
    /// unit `MemSpace::cpu()`. A span is the code the CPU is about to run: `[lo,
    /// hi_excl)` of `space` (adjacent or overlapping spans of one unit and space
    /// merged), the trigger for verifying `unit`. Empty when the map has no identity.
    struct VerifySpan {
        jnext::dbg::FetchSource space;
        uint16_t lo = 0;
        uint32_t hi_excl = 0;
        jnext::dbg::FetchSource unit;
    };
    std::vector<VerifySpan> verify_spans() const;

    /// The identity of ONE unit (see `verify_spans()`) against live memory: the SHA-256
    /// for `MemSpace::cpu()`, the byte checks of that space otherwise. nullopt when the
    /// map has nothing to check in it.
    std::optional<bool> verify_unit(jnext::dbg::FetchSource unit,
                                    const std::function<uint8_t(uint16_t)>& read,
                                    const SpaceReader& read_space) const;

    /// Same file, line and column (one statement can emit several ranges).
    static bool same_position(const SourceLocation& a, const SourceLocation& b) {
        return a.file == b.file && a.line == b.line && a.column == b.column;
    }

    void clear();
    bool empty() const { return ranges_.empty(); }
    size_t size() const { return ranges_.size(); }   // ranges, zero-width included
    const std::string& loaded_file() const { return loaded_file_; }
    /// The format the contents came from ("SLD", "z88dk").
    const std::string& format() const { return format_; }

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
    std::string format_ = "SLD";
    std::optional<SourceProgramIdentity> identity_;
};
