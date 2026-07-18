#include "debug/source_map.h"

#include <algorithm>
#include <charconv>

#include <openssl/evp.h>

namespace {

std::string normalized_file(std::string file)
{
    std::replace(file.begin(), file.end(), '\\', '/');
    while (file.size() >= 2 && file[0] == '.' && file[1] == '/')
        file.erase(0, 2);
    return file;
}

std::string basename(const std::string& file)
{
    const size_t slash = file.find_last_of('/');
    return slash == std::string::npos ? file : file.substr(slash + 1);
}

std::optional<int> decimal(std::string_view text)
{
    int value = 0;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, 10);
    if (error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

} // namespace

void SourceMap::replace(std::vector<SourceLocation> locations,
                        std::optional<SourceProgramIdentity> identity,
                        std::string loaded_file)
{
    std::map<uint32_t, SourceLocation> by_address;
    std::map<FileLine, std::vector<SourceAddress>> by_line;
    for (auto& location : locations) {
        location.file = normalized_file(std::move(location.file));
        SourceAddress address{location.page, location.address};

        // Later records win at a duplicate execution address. SLD uses this
        // for statement markers that replace a zero-width function marker.
        by_address[address_key(location.page, location.address)] = location;
        auto& line_addresses = by_line[{location.file, location.line}];
        const auto duplicate = std::find_if(
            line_addresses.begin(), line_addresses.end(),
            [&](const SourceAddress& candidate) {
                return candidate.page == address.page
                       && candidate.address == address.address;
            });
        if (duplicate == line_addresses.end())
            line_addresses.push_back(address);
    }

    by_address_ = std::move(by_address);
    by_line_ = std::move(by_line);
    identity_ = std::move(identity);
    loaded_file_ = std::move(loaded_file);
}

std::optional<SourceLocation> SourceMap::lookup(uint8_t page, uint16_t address) const
{
    if (const auto exact = by_address_.find(address_key(page, address));
        exact != by_address_.end()) {
        return exact->second;
    }
    const auto logical = by_address_.find(address_key(std::nullopt, address));
    return logical == by_address_.end()
        ? std::nullopt : std::optional<SourceLocation>(logical->second);
}

std::vector<SourceAddress> SourceMap::addresses(const std::string& file, int line) const
{
    const std::string normalized = normalized_file(file);
    if (const auto exact = by_line_.find({normalized, line}); exact != by_line_.end())
        return exact->second;

    std::vector<SourceAddress> result;
    bool matched = false;
    for (const auto& [key, values] : by_line_) {
        if (key.second != line || basename(key.first) != normalized) continue;
        if (matched) return {};
        result = values;
        matched = true;
    }
    return result;
}

std::optional<SourceAddress> SourceMap::resolve(std::string_view expression) const
{
    const size_t colon = expression.find_last_of(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string file(expression.substr(0, colon));
    const auto line = decimal(expression.substr(colon + 1));
    if (file.empty() || !line || *line < 1) return std::nullopt;
    const auto candidates = addresses(file, *line);
    return candidates.empty() ? std::nullopt
                              : std::optional<SourceAddress>(candidates.front());
}

std::optional<bool> SourceMap::verify_program(
    const std::function<uint8_t(uint16_t)>& read) const
{
    if (!identity_) return std::nullopt;
    if (identity_->size > 0x10000u - identity_->org) return false;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) return false;
    bool ok = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
    uint8_t chunk[1024];
    uint32_t offset = 0;
    while (ok && offset < identity_->size) {
        const uint32_t count = std::min<uint32_t>(
            sizeof(chunk), identity_->size - offset);
        for (uint32_t i = 0; i < count; ++i) {
            chunk[i] = read(static_cast<uint16_t>(identity_->org + offset + i));
        }
        ok = EVP_DigestUpdate(context, chunk, count) == 1;
        offset += count;
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    ok = ok && EVP_DigestFinal_ex(context, digest, &digest_size) == 1;
    EVP_MD_CTX_free(context);
    if (!ok || digest_size != 32) return false;

    static constexpr char HEX[] = "0123456789abcdef";
    std::string actual;
    actual.reserve(64);
    for (unsigned int i = 0; i < digest_size; ++i) {
        actual.push_back(HEX[digest[i] >> 4]);
        actual.push_back(HEX[digest[i] & 0x0F]);
    }
    return actual == identity_->sha256;
}

void SourceMap::clear()
{
    by_address_.clear();
    by_line_.clear();
    loaded_file_.clear();
    identity_.reset();
}
