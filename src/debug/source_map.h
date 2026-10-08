#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// One source position an executable address was emitted from.
struct SourceLocation {
    std::string file;
    int line = 0;       // one-based
    int column = 0;     // one-based; zero when absent
    std::optional<uint8_t> page;
    uint16_t address = 0;
};

/// Where `file:line` resolves to: a logical address, and the physical 8K page
/// when the record named one.
struct SourceAddress {
    std::optional<uint8_t> page;
    uint16_t address = 0;
};

/// Optional binary identity a source map may carry: the SHA-256 of `size`
/// bytes of logical memory from `org`, as they were right after the load.
struct SourceProgramIdentity {
    std::string sha256;
    uint16_t org = 0;
    uint32_t size = 0;
};

/// Compiler-neutral source locations indexed by logical address and optional
/// physical 8K page. Format adapters replace the contents transactionally.
class SourceMap {
public:
    void replace(std::vector<SourceLocation> locations,
                 std::optional<SourceProgramIdentity> identity,
                 std::string loaded_file);

    /// Resolve an exact physical-page record, then an unqualified logical one.
    /// `page` NOT_RAM_PAGE (debug/ram_page.h): only an unqualified record.
    std::optional<SourceLocation> lookup(uint8_t page, uint16_t address) const;

    /// Resolve file:line. A unique basename is accepted as well as the exact
    /// source path. The first emitted instruction for the line is returned.
    std::optional<SourceAddress> resolve(std::string_view expression) const;

    /// Verify optional identity metadata against live logical memory.
    /// Verify optional identity metadata against live logical memory: nullopt
    /// when the map carries none, else whether the bytes still match.
    std::optional<bool> verify_program(
        const std::function<uint8_t(uint16_t)>& read) const;

    /// Same file, line and column (the address and page may differ: one
    /// statement can emit several records).
    static bool same_position(const SourceLocation& a, const SourceLocation& b) {
        return a.file == b.file && a.line == b.line && a.column == b.column;
    }

    void clear();
    bool empty() const { return by_address_.empty(); }
    size_t size() const { return by_address_.size(); }
    const std::string& loaded_file() const { return loaded_file_; }

private:
    using FileLine = std::pair<std::string, int>;

    static uint32_t address_key(std::optional<uint8_t> page, uint16_t address) {
        constexpr uint32_t UNQUALIFIED_PAGE = 0x100;
        return ((page ? static_cast<uint32_t>(*page) : UNQUALIFIED_PAGE) << 16)
               | address;
    }

    std::vector<SourceAddress> addresses(const std::string& file, int line) const;

    std::map<uint32_t, SourceLocation> by_address_;
    std::map<FileLine, std::vector<SourceAddress>> by_line_;
    std::string loaded_file_;
    std::optional<SourceProgramIdentity> identity_;
};
