#include "debug/source_map.h"

#include "core/sha256.h"

#include <algorithm>
#include <charconv>
#include <tuple>

namespace {

using jnext::dbg::FetchSource;
using jnext::dbg::MemSpace;

std::string normalized_file(std::string file)
{
    std::replace(file.begin(), file.end(), '\\', '/');
    while (file.size() >= 2 && file[0] == '.' && file[1] == '/')
        file.erase(0, 2);
    return file;
}

uint32_t space_key(FetchSource s)
{
    // Cpu compares equal whatever its index.
    const uint32_t index = s.kind == MemSpace::Kind::Cpu ? 0u : s.index;
    return (static_cast<uint32_t>(s.kind) << 16) | index;
}

FetchSource key_space(uint32_t key)
{
    return FetchSource{static_cast<MemSpace::Kind>(key >> 16),
                       static_cast<uint16_t>(key & 0xFFFF)};
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

void SourceMap::replace(std::vector<SourceRange> ranges,
                        std::vector<SourceAlias> aliases,
                        std::optional<SourceProgramIdentity> identity,
                        std::string loaded_file)
{
    SourceMap next;
    std::map<std::tuple<std::string, int, int>, size_t> location_ids;
    for (auto& r : ranges) {
        r.location.file = normalized_file(std::move(r.location.file));
        const auto key = std::make_tuple(r.location.file, r.location.line, r.location.column);
        auto [it, added] = location_ids.try_emplace(key, next.locations_.size());
        if (added) next.locations_.push_back(r.location);
        next.files_.insert(r.location.file);
        const uint32_t hi = std::max<uint32_t>(r.hi_excl, r.lo);
        next.ranges_.push_back({space_key(r.space), r.lo, std::min<uint32_t>(hi, 0x10000),
                                it->second});
    }

    // Line index: non-zero-width ranges, in emission order.
    for (size_t i = 0; i < next.ranges_.size(); ++i) {
        const Range& r = next.ranges_[i];
        if (r.hi == r.lo) continue;
        const SourceLocation& l = next.locations_[r.location];
        next.by_line_[{l.file, l.line}].push_back(i);
    }
    for (auto& a : aliases) {
        if (a.range >= next.ranges_.size()) continue;
        a.file = normalized_file(std::move(a.file));
        next.files_.insert(a.file);
        next.aliases_[{a.file, a.line}] = a.range;
    }

    // Address index: elementary segments between all range boundaries.
    std::vector<uint64_t> bounds;
    for (const Range& r : next.ranges_) {
        if (r.hi == r.lo) continue;
        bounds.push_back((static_cast<uint64_t>(r.space_key) << 20) | r.lo);
        bounds.push_back((static_cast<uint64_t>(r.space_key) << 20) | r.hi);
    }
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    // lists[i] covers [bounds[i], bounds[i+1]) when both are in one space.
    std::vector<std::vector<size_t>> lists(bounds.size());
    for (size_t i = 0; i < next.ranges_.size(); ++i) {
        const Range& r = next.ranges_[i];
        if (r.hi == r.lo) continue;
        const uint64_t base = static_cast<uint64_t>(r.space_key) << 20;
        const uint64_t hi = base | r.hi;
        size_t s = std::lower_bound(bounds.begin(), bounds.end(), base | r.lo) - bounds.begin();
        for (; s < bounds.size() && bounds[s] < hi; ++s) lists[s].push_back(i);
    }
    auto primary_first = [&next](size_t a, size_t b) {
        const Range& x = next.ranges_[a];
        const Range& y = next.ranges_[b];
        const uint32_t lx = x.hi - x.lo;
        const uint32_t ly = y.hi - y.lo;
        return lx != ly ? lx < ly : a > b;   // shorter, then later-emitted
    };
    for (size_t s = 0; s < bounds.size(); ++s) {
        if (lists[s].empty()) continue;
        std::sort(lists[s].begin(), lists[s].end(), primary_first);
        Segment seg;
        seg.start = bounds[s];
        seg.end = static_cast<uint32_t>(bounds[s + 1] & 0xFFFFF);
        seg.first = next.cover_.size();
        seg.count = lists[s].size();
        next.cover_.insert(next.cover_.end(), lists[s].begin(), lists[s].end());
        next.segments_.push_back(seg);
    }

    next.identity_ = std::move(identity);
    next.loaded_file_ = std::move(loaded_file);
    *this = std::move(next);
}

const SourceMap::Segment* SourceMap::segment_at(FetchSource space, uint16_t address) const
{
    const uint64_t key = (static_cast<uint64_t>(space_key(space)) << 20) | address;
    auto it = std::upper_bound(segments_.begin(), segments_.end(), key,
                               [](uint64_t k, const Segment& s) { return k < s.start; });
    if (it == segments_.begin()) return nullptr;
    --it;
    if ((it->start >> 20) != (key >> 20) || address >= it->end) return nullptr;
    return &*it;
}

const SourceLocation* SourceMap::lookup(FetchSource space, uint16_t address) const
{
    const Segment* seg = segment_at(space, address);
    return seg ? &locations_[ranges_[cover_[seg->first]].location] : nullptr;
}

std::vector<const SourceLocation*> SourceMap::lookup_all(FetchSource space,
                                                         uint16_t address) const
{
    std::vector<const SourceLocation*> all;
    if (const Segment* seg = segment_at(space, address)) {
        for (size_t i = 0; i < seg->count; ++i)
            all.push_back(&locations_[ranges_[cover_[seg->first + i]].location]);
    }
    return all;
}

SourceResolution SourceMap::resolve(std::string_view expression) const
{
    const size_t colon = expression.find_last_of(':');
    if (colon == std::string_view::npos) return {};
    const std::string query = normalized_file(std::string(expression.substr(0, colon)));
    const auto line = decimal(expression.substr(colon + 1));
    if (query.empty() || !line || *line < 1) return {};

    // File: exact, else the unique file ending in "/" + query.
    const std::string* file = nullptr;
    if (const auto exact = files_.find(query); exact != files_.end()) {
        file = &*exact;
    } else {
        const std::string tail = "/" + query;
        for (const std::string& f : files_) {
            if (f.size() <= tail.size()
                || f.compare(f.size() - tail.size(), tail.size(), tail) != 0)
                continue;
            if (file) return {};   // ambiguous
            file = &f;
        }
    }
    if (!file) return {};

    SourceResolution out;
    const FileLine fl{*file, *line};
    if (const auto it = by_line_.find(fl); it != by_line_.end()) {
        for (const size_t i : it->second) {
            const Range& r = ranges_[i];
            const bool continuation = std::any_of(
                it->second.begin(), it->second.end(), [&](size_t j) {
                    return ranges_[j].space_key == r.space_key && ranges_[j].hi == r.lo;
                });
            if (!continuation)
                out.starts.push_back({key_space(r.space_key), static_cast<uint16_t>(r.lo)});
        }
        std::sort(out.starts.begin(), out.starts.end(),
                  [](const SourceAddress& a, const SourceAddress& b) {
                      return std::make_tuple(space_key(a.space), a.address)
                           < std::make_tuple(space_key(b.space), b.address);
                  });
        out.starts.erase(std::unique(out.starts.begin(), out.starts.end(),
                             [](const SourceAddress& a, const SourceAddress& b) {
                                 return space_key(a.space) == space_key(b.space)
                                     && a.address == b.address;
                             }),
                         out.starts.end());
    }
    if (out.starts.empty()) {
        if (const auto al = aliases_.find(fl); al != aliases_.end()) {
            const Range& r = ranges_[al->second];
            out.starts.push_back({key_space(r.space_key), static_cast<uint16_t>(r.lo)});
        } else {
            out.error = "no code at " + std::string(expression);
        }
    }
    return out;
}

std::optional<bool> SourceMap::verify_program(
    const std::function<uint8_t(uint16_t)>& read) const
{
    if (!identity_) return std::nullopt;
    if (identity_->size > 0x10000u - identity_->org) return false;
    std::vector<uint8_t> bytes(identity_->size);
    for (uint32_t i = 0; i < identity_->size; ++i)
        bytes[i] = read(static_cast<uint16_t>(identity_->org + i));
    const std::string actual = sdcard::sha256_hex(bytes);
    return !actual.empty() && actual == identity_->sha256;
}

void SourceMap::clear()
{
    locations_.clear();
    ranges_.clear();
    segments_.clear();
    cover_.clear();
    by_line_.clear();
    aliases_.clear();
    files_.clear();
    loaded_file_.clear();
    identity_.reset();
}
