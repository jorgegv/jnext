#include "core/extended_nex_host.h"

#include <algorithm>
#include <filesystem>
#include <limits>

bool ExtendedNexHost::open(const std::string& path)
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
    return true;
}

bool ExtendedNexHost::reopen()
{
    if (path_.empty()) return false;
    const std::string saved_path = path_;
    return open(saved_path);
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
    const uint64_t available =
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) -
        kSyntheticFirstBlock + 1;
    return static_cast<uint32_t>(std::min<uint64_t>(blocks, available));
}

bool ExtendedNexHost::read_block(uint32_t address, uint8_t* dst)
{
    if (!file_.is_open() || !dst || address < kSyntheticFirstBlock)
        return false;

    const uint64_t block =
        static_cast<uint64_t>(address - kSyntheticFirstBlock);
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
    entries.clear();
    if (!file_.is_open()) return false;

    const uint64_t total_blocks = block_count();
    while (entries.size() < capacity && filemap_next_block_ < total_blocks) {
        const uint64_t remaining = total_blocks - filemap_next_block_;
        const uint16_t blocks = static_cast<uint16_t>(
            std::min<uint64_t>(remaining, std::numeric_limits<uint16_t>::max()));
        entries.push_back({
            kSyntheticFirstBlock +
                static_cast<uint32_t>(filemap_next_block_),
            blocks
        });
        filemap_next_block_ += blocks;
    }
    return true;
}

bool ExtendedNexHost::stream_start(uint32_t address, uint8_t flags)
{
    if (!file_.is_open()) return false;

    if (address < kSyntheticFirstBlock) return false;
    const uint64_t block =
        static_cast<uint64_t>(address - kSyntheticFirstBlock);
    const uint64_t offset = block * kBlockSize;
    const uint64_t rounded_size = ((size_ + kBlockSize - 1) / kBlockSize) * kBlockSize;
    if (offset >= rounded_size)
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
