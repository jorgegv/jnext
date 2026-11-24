#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Read-only host-file bridge used by directly-loaded NEX files that keep
/// their own file handle and stream appended data through NextZXOS APIs.
///
/// The guest sees one synthetic, contiguous, block-addressed SD extent. The
/// card addresses are opaque cookies whose value is the host-file 512-byte
/// block number. DISK_STRMSTART turns that cookie back into a byte offset and
/// read_stream_byte() supplies the SD/MMC wire sequence expected at port $EB.
class ExtendedNexHost {
public:
    struct FileMapEntry {
        uint32_t address = 0;  // block address (card flags bit 1 is set)
        uint16_t blocks = 0;
    };

    // Keep distinct from --esxdos-stub's in-memory handle (1), so an
    // extended NEX may still use that compatibility file alongside itself.
    static constexpr uint8_t kHandle = 2;
    static constexpr uint8_t kSiblingHandle = 3;
    static constexpr uint8_t kCardFlags = 0x02;  // card 0, block addressing
    static constexpr uint16_t kBlockSize = 512;
    // Keep host-file sectors disjoint from realistic SD-card image offsets.
    // DISK_FILEMAP returns these opaque cookies; both DISK_STRMSTART and the
    // SD-card read overlay translate them back to host-file blocks.
    static constexpr uint32_t kSyntheticFirstBlock = 0x80000000U;

    /// `first_block` is the synthetic card address of the file's block 0 and
    /// `max_blocks` (0 = up to the end of the 32-bit address space) caps how
    /// many blocks it may span. The defaults are the extended NEX's own range.
    bool open(const std::string& path, uint32_t first_block = kSyntheticFirstBlock,
              uint64_t max_blocks = 0);
    bool reopen();
    bool open_sibling(const std::string& path);
    void close();
    void close_sibling();
    void clear();

    bool is_open() const { return file_.is_open(); }
    bool sibling_is_open() const { return sibling_file_.is_open(); }
    const std::string& path() const { return path_; }
    uint64_t size() const { return size_; }
    uint64_t position() const { return position_; }
    uint64_t sibling_size() const { return sibling_size_; }
    uint64_t sibling_position() const { return sibling_position_; }
    uint32_t block_count() const;

    bool read(uint8_t* dst, std::size_t requested, std::size_t& actual);
    bool read_sibling(uint8_t* dst, std::size_t requested, std::size_t& actual);
    bool seek(uint8_t mode, uint32_t offset);
    bool seek_sibling(uint8_t mode, uint32_t offset);
    bool read_block(uint32_t address, uint8_t* dst);

    /// Return up to capacity contiguous extents, continuing where the previous
    /// call stopped. A host file is contiguous, but the 16-bit sector count
    /// limits each returned entry to 65535 blocks.
    bool file_map(std::size_t capacity, std::vector<FileMapEntry>& entries);
    /// Same, with the continuation cursor (next block to map) owned by the
    /// caller: esxDOS keeps it per open handle, not per file.
    bool file_map(std::size_t capacity, std::vector<FileMapEntry>& entries,
                  uint64_t& next_block);
    uint32_t first_block() const { return first_block_; }

    /// Start the SD/MMC wire stream at a synthetic block address. Bit 7 of
    /// flags requests the NextZXOS 2.01 no-wait form, for which the first port
    /// byte is the ready token ($FE).
    bool stream_start(uint32_t address, uint8_t flags);
    std::optional<uint8_t> read_stream_byte();
    bool stream_end();
    bool stream_active() const { return stream_active_; }

private:
    enum class StreamPhase : uint8_t {
        Data,
        CrcHigh,
        CrcLow,
        Token
    };

    static bool read_file(std::ifstream& file, uint64_t size, uint64_t& position,
                          uint8_t* dst, std::size_t requested, std::size_t& actual);
    static bool seek_open_file(std::ifstream& file, uint64_t size, uint64_t& position,
                               uint8_t mode, uint32_t offset);

    std::ifstream file_;
    std::ifstream sibling_file_;
    // Keep the raw port-$EB stream independent from F_READ and the synthetic
    // SD overlay, both of which legitimately seek the primary host handle.
    std::ifstream stream_file_;
    std::string path_;
    uint64_t size_ = 0;
    uint64_t position_ = 0;
    uint64_t sibling_size_ = 0;
    uint64_t sibling_position_ = 0;
    uint64_t filemap_next_block_ = 0;
    uint32_t first_block_ = kSyntheticFirstBlock;
    uint64_t max_blocks_ = 0;

    bool stream_active_ = false;
    bool initial_token_pending_ = false;
    uint64_t stream_offset_ = 0;
    uint16_t stream_block_pos_ = 0;
    StreamPhase stream_phase_ = StreamPhase::Data;
};

/// GH #556: block streaming (DISK_FILEMAP / DISK_STRMSTART / DISK_STRMEND and
/// port $EB) for the files of the host directory a directly loaded program is
/// served. Each mapped host file gets a region of synthetic card addresses
/// (an ExtendedNexHost opened at the region's base), keyed by its host path.
/// A region outlives F_CLOSE (card addresses outlive a handle on hardware) and
/// is dropped only by clear(). One stream at a time, as on one real card.
class HostFileStreams {
public:
    static constexpr uint32_t kFirstBlock = 0x90000000U;
    static constexpr uint32_t kRegionBlocks = 0x00800000U;  // 4 GB, the FAT32 file limit
    static constexpr std::size_t kRegions =
        (0x100000000ULL - kFirstBlock) / kRegionBlocks;      // 224

    /// Region of `host_path`, created on first use. False when the file cannot
    /// be opened or all regions are taken (`full` tells which).
    bool region_for(const std::string& host_path, ExtendedNexHost*& region,
                    bool& full);
    /// True and `region` set when `address` lies in a region already created.
    bool find(uint32_t address, ExtendedNexHost*& region);
    /// Start the stream at `address` (in a region), ending any other.
    bool stream_start(uint32_t address, uint8_t flags);
    /// End the active stream; false when none is active.
    bool stream_end();
    bool stream_active() const;
    std::optional<uint8_t> read_stream_byte();
    void clear();

private:
    std::vector<std::unique_ptr<ExtendedNexHost>> regions_;
};
