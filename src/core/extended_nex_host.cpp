#include "core/extended_nex_host.h"

#include <algorithm>
#include <filesystem>
#include <limits>

bool ExtendedNexHost::open(const std::string& path, uint32_t first_block,
                           uint64_t max_blocks)
{
    close();

    std::error_code ec;
    const uint64_t file_size = std::filesystem::file_size(path, ec);
    if (ec) return false;

    file_.open(path, std::ios::binary);
    if (!file_) return false;

    path_ = std::filesystem::absolute(path).lexically_normal().string();
    size_ = file_size;
    position_ = 0;
    filemap_next_block_ = 0;
    first_block_ = first_block;
    max_blocks_ = max_blocks;
    return true;
}

bool ExtendedNexHost::reopen()
{
    if (path_.empty()) return false;
    const std::string saved_path = path_;
    return open(saved_path, first_block_, max_blocks_);
}

bool ExtendedNexHost::open_sibling(const std::string& path)
{
    close_sibling();

    std::error_code ec;
    const uint64_t file_size = std::filesystem::file_size(path, ec);
    if (ec) return false;

    sibling_file_.open(path, std::ios::binary);
    if (!sibling_file_) return false;

    sibling_size_ = file_size;
    sibling_position_ = 0;
    return true;
}

void ExtendedNexHost::close()
{
    if (file_.is_open()) file_.close();
    file_.clear();
    if (stream_file_.is_open()) stream_file_.close();
    stream_file_.clear();
    position_ = 0;
    filemap_next_block_ = 0;
    stream_active_ = false;
    initial_token_pending_ = false;
    stream_offset_ = 0;
    stream_block_pos_ = 0;
    stream_phase_ = StreamPhase::Data;
}

void ExtendedNexHost::close_sibling()
{
    if (sibling_file_.is_open()) sibling_file_.close();
    sibling_file_.clear();
    sibling_size_ = 0;
    sibling_position_ = 0;
}

void ExtendedNexHost::clear()
{
    close();
    close_sibling();
    path_.clear();
    size_ = 0;
}

bool ExtendedNexHost::read(uint8_t* dst, std::size_t requested, std::size_t& actual)
{
    return read_file(file_, size_, position_, dst, requested, actual);
}

bool ExtendedNexHost::read_sibling(uint8_t* dst, std::size_t requested,
                                   std::size_t& actual)
{
    return read_file(sibling_file_, sibling_size_, sibling_position_,
                     dst, requested, actual);
}

bool ExtendedNexHost::read_file(std::ifstream& file, uint64_t size,
                                uint64_t& position, uint8_t* dst,
                                std::size_t requested, std::size_t& actual)
{
    actual = 0;
    if (!file.is_open() || !dst) return false;
    if (position >= size || requested == 0) return true;

    const uint64_t available = size - position;
    const std::size_t count = static_cast<std::size_t>(
        std::min<uint64_t>(available, requested));
    file.clear();
    file.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    if (!file) return false;

    file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(count));
    actual = static_cast<std::size_t>(file.gcount());
    position += actual;
    return actual == count;
}

bool ExtendedNexHost::seek(uint8_t mode, uint32_t offset)
{
    const bool reset_filemap = (mode & 0x03) == 0 && offset == 0;
    if (!seek_open_file(file_, size_, position_, mode, offset)) return false;
    if (reset_filemap) filemap_next_block_ = 0;
    return true;
}

bool ExtendedNexHost::seek_sibling(uint8_t mode, uint32_t offset)
{
    return seek_open_file(sibling_file_, sibling_size_, sibling_position_,
                          mode, offset);
}

uint32_t ExtendedNexHost::block_count() const
{
    const uint64_t blocks = (size_ + kBlockSize - 1) / kBlockSize;
    uint64_t available =
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) -
        first_block_ + 1;
    if (max_blocks_ != 0) available = std::min(available, max_blocks_);
    return static_cast<uint32_t>(std::min<uint64_t>(blocks, available));
}

bool ExtendedNexHost::read_block(uint32_t address, uint8_t* dst)
{
    if (!file_.is_open() || !dst || address < first_block_)
        return false;

    const uint64_t block =
        static_cast<uint64_t>(address - first_block_);
    if (block >= block_count()) return false;

    const uint64_t offset = block * kBlockSize;
    const std::size_t available = static_cast<std::size_t>(
        std::min<uint64_t>(kBlockSize, size_ - offset));
    std::fill(dst, dst + kBlockSize, 0);
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) return false;
    file_.read(reinterpret_cast<char*>(dst),
               static_cast<std::streamsize>(available));
    return static_cast<std::size_t>(file_.gcount()) == available;
}

bool ExtendedNexHost::seek_open_file(std::ifstream& file, uint64_t size,
                                     uint64_t& position, uint8_t mode,
                                     uint32_t offset)
{
    if (!file.is_open()) return false;

    uint64_t target = position;
    switch (mode & 0x03) {
        case 0:  // absolute
            target = offset;
            break;
        case 1:  // forward
            target = std::min<uint64_t>(
                size, position + static_cast<uint64_t>(offset));
            break;
        case 2:  // backward
            target = offset > position ? 0 : position - offset;
            break;
        default:
            return false;
    }

    position = std::min<uint64_t>(target, size);
    file.clear();
    file.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    return static_cast<bool>(file);
}

bool ExtendedNexHost::file_map(std::size_t capacity,
                               std::vector<FileMapEntry>& entries)
{
    return file_map(capacity, entries, filemap_next_block_);
}

bool ExtendedNexHost::file_map(std::size_t capacity,
                               std::vector<FileMapEntry>& entries,
                               uint64_t& next_block)
{
    entries.clear();
    if (!file_.is_open()) return false;

    const uint64_t total_blocks = block_count();
    while (entries.size() < capacity && next_block < total_blocks) {
        const uint64_t remaining = total_blocks - next_block;
        const uint16_t blocks = static_cast<uint16_t>(
            std::min<uint64_t>(remaining, std::numeric_limits<uint16_t>::max()));
        entries.push_back({
            first_block_ + static_cast<uint32_t>(next_block),
            blocks
        });
        next_block += blocks;
    }
    return true;
}

bool ExtendedNexHost::stream_start(uint32_t address, uint8_t flags)
{
    if (!file_.is_open()) return false;

    if (address < first_block_) return false;
    const uint64_t block =
        static_cast<uint64_t>(address - first_block_);
    const uint64_t offset = block * kBlockSize;
    const uint64_t rounded_size = ((size_ + kBlockSize - 1) / kBlockSize) * kBlockSize;
    if (offset >= rounded_size || block >= block_count())
        return false;

    if (stream_file_.is_open()) stream_file_.close();
    stream_file_.clear();
    stream_file_.open(path_, std::ios::binary);
    if (!stream_file_) return false;
    stream_file_.seekg(
        static_cast<std::streamoff>(std::min<uint64_t>(offset, size_)),
        std::ios::beg);
    if (!stream_file_) {
        stream_file_.close();
        stream_file_.clear();
        return false;
    }

    stream_active_ = true;
    initial_token_pending_ = (flags & 0x80) != 0;
    stream_offset_ = offset;
    stream_block_pos_ = 0;
    stream_phase_ = StreamPhase::Data;
    return true;
}

std::optional<uint8_t> ExtendedNexHost::read_stream_byte()
{
    if (!stream_active_) return std::nullopt;

    if (initial_token_pending_) {
        initial_token_pending_ = false;
        return 0xFE;
    }

    switch (stream_phase_) {
        case StreamPhase::Data: {
            uint8_t value = 0x00;  // zero-pad the final allocated sector
            if (stream_offset_ < size_) {
                char byte = 0;
                stream_file_.read(&byte, 1);
                if (!stream_file_) return std::nullopt;
                value = static_cast<uint8_t>(byte);
            }
            ++stream_offset_;
            ++stream_block_pos_;
            if (stream_block_pos_ == kBlockSize) {
                stream_block_pos_ = 0;
                stream_phase_ = StreamPhase::CrcHigh;
            }
            return value;
        }
        case StreamPhase::CrcHigh:
            stream_phase_ = StreamPhase::CrcLow;
            return 0x00;
        case StreamPhase::CrcLow:
            stream_phase_ = StreamPhase::Token;
            return 0x00;
        case StreamPhase::Token:
            stream_phase_ = StreamPhase::Data;
            return 0xFE;
    }
    return std::nullopt;
}

bool ExtendedNexHost::stream_end()
{
    if (!stream_active_) return false;
    stream_active_ = false;
    initial_token_pending_ = false;
    stream_phase_ = StreamPhase::Data;
    if (stream_file_.is_open()) stream_file_.close();
    stream_file_.clear();
    return true;
}

// ── HostFileStreams (GH #556) ────────────────────────────────────────────

bool HostFileStreams::region_for(const std::string& host_path,
                                 ExtendedNexHost*& region, bool& full)
{
    full = false;
    if (host_path.empty()) return false;
    const std::string key =
        std::filesystem::absolute(host_path).lexically_normal().string();
    for (auto& r : regions_) {
        if (r->path() == key) { region = r.get(); return true; }
    }
    if (regions_.size() >= kRegions) { full = true; return false; }
    auto fresh = std::make_unique<ExtendedNexHost>();
    const uint32_t base = kFirstBlock +
        static_cast<uint32_t>(regions_.size()) * kRegionBlocks;
    if (!fresh->open(key, base, kRegionBlocks)) return false;
    regions_.push_back(std::move(fresh));
    region = regions_.back().get();
    return true;
}

bool HostFileStreams::find(uint32_t address, ExtendedNexHost*& region)
{
    if (address < kFirstBlock) return false;
    const std::size_t index = (address - kFirstBlock) / kRegionBlocks;
    if (index >= regions_.size()) return false;
    region = regions_[index].get();
    return true;
}

bool HostFileStreams::stream_start(uint32_t address, uint8_t flags)
{
    ExtendedNexHost* region = nullptr;
    if (!find(address, region)) return false;
    // One card, one CMD18: starting a stream ends any other.
    for (auto& r : regions_) r->stream_end();
    return region->stream_start(address, flags);
}

bool HostFileStreams::stream_end()
{
    bool ended = false;
    for (auto& r : regions_) ended = r->stream_end() || ended;
    return ended;
}

bool HostFileStreams::stream_active() const
{
    for (const auto& r : regions_)
        if (r->stream_active()) return true;
    return false;
}

std::optional<uint8_t> HostFileStreams::read_stream_byte()
{
    for (auto& r : regions_)
        if (r->stream_active()) return r->read_stream_byte();
    return std::nullopt;
}

void HostFileStreams::clear()
{
    regions_.clear();
}
