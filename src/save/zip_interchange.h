#pragma once
//
// A read-only ZIP reader for archives other people wrote (GH #392): the
// `game.tap.zip` a user downloads. It is NOT the container layer of the `.jns`
// format and it must not become one.
//
// WHY A SECOND READER. zip_archive.h's `Reader` accepts only the exact subset
// the `.jns` writer emits (flag 0, version 20, no extra field, no data
// descriptor, lower-case grammar paths) and refuses everything else on purpose:
// two readers, both believing they succeeded, disagreeing about the bytes.
// Real archives break every one of those rules (Info-ZIP sets flag 0x0002 and a
// 24-byte extra field on every entry and writes directory entries; streaming
// writers set flag 0x0008 and put zero sizes in the local header). Relaxing the
// strict reader would weaken it, so this one is separate. The only thing the two
// share is `inflate_exact()` (raw inflate to an exactly declared length).
//
// Oracle: PKWARE APPNOTE.TXT (doc/REFERENCES.md): 4.3.7 local header, 4.3.12
// central header, 4.3.14-4.3.16 ZIP64 / EOCD, 4.4.4 flags, 4.4.5 methods.
//
// ACCEPTED: stored (0) and deflate (8) members; any flag bit except encryption
// (bit 0, bit 6); extra fields and comments of any length (skipped by length);
// an EOCD comment; sizes and CRC taken from the CENTRAL directory (a data
// descriptor member has zero sizes in its local header); the data offset taken
// from the LOCAL header's own name and extra lengths (they differ from the
// central ones in real archives).
//
// REFUSED, each naming the thing: not an archive (no EOCD ending at the end of
// the buffer), ZIP64 in any form, multi-disk, a central directory outside the
// buffer or disagreeing with its entry count (archive level, from open());
// encryption, a method other than 0/8, a declared size over the cap (checked
// BEFORE anything is allocated), a bad local header, data past the end of the
// buffer, a stored member whose sizes differ, a CRC-32 mismatch (member level,
// from read()).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jnext {
namespace zip {

/// 256 MiB: a 10-minute CD-quality stereo WAV tape is ~106 MB, which the
/// `.jns` 64 MiB cap would refuse. Bounds a DECLARED size (a zip bomb costs a
/// refusal, not an allocation).
constexpr uint64_t kMaxInterchangeMemberBytes = 256ull * 1024 * 1024;

struct InterchangeEntry {
    std::string name;            ///< raw bytes from the central directory
    uint16_t    flags = 0;
    uint16_t    method = 0;
    uint32_t    crc32 = 0;
    uint32_t    comp_size = 0;
    uint32_t    uncomp_size = 0;
    uint32_t    local_offset = 0;
    uint16_t    central_extra_len = 0;
    bool is_directory() const { return !name.empty() && name.back() == '/'; }
};

class InterchangeReader {
public:
    explicit InterchangeReader(uint64_t max_member = kMaxInterchangeMemberBytes)
        : max_member_(max_member) {}

    /// Parse the central directory of `data[0..len)`. The buffer must outlive
    /// the reader. On failure `why` says why and the reader is left empty.
    bool open(const uint8_t* data, size_t len, std::string& why);

    const std::vector<InterchangeEntry>& entries() const { return entries_; }

    /// Read one member's bytes, CRC-checked. A refusal leaves `out` empty and
    /// names the member's problem (the caller prefixes the member name).
    bool read(const InterchangeEntry& e, std::vector<uint8_t>& out,
              std::string& why) const;

private:
    uint64_t                       max_member_;
    const uint8_t*                 data_ = nullptr;
    size_t                         len_ = 0;
    std::vector<InterchangeEntry>  entries_;
};

}  // namespace zip
}  // namespace jnext
