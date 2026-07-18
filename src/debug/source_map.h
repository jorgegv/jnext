#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct SourceLocation {
    std::string file;
    int line = 0;       // one-based
    int column = 0;     // one-based; zero when absent
    std::optional<uint8_t> page;
    uint16_t address = 0;
};

struct SourceAddress {
    std::optional<uint8_t> page;
    uint16_t address = 0;
};

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
    std::optional<SourceLocation> lookup(uint8_t page, uint16_t address) const;

    /// Resolve file:line. A unique basename is accepted as well as the exact
    /// source path. The first emitted instruction for the line is returned.
    std::optional<SourceAddress> resolve(std::string_view expression) const;

    /// Verify optional identity metadata against live logical memory.
    std::optional<bool> verify_program(
        const std::function<uint8_t(uint16_t)>& read) const;

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
