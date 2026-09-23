#include "save/zip_archive.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace jnext {
namespace zip {

namespace {

// ── Signatures and fixed header widths ───────────────────────────────────
constexpr uint32_t kSigLocal      = 0x04034b50u;
constexpr uint32_t kSigCentral    = 0x02014b50u;
constexpr uint32_t kSigEocd       = 0x06054b50u;
constexpr uint32_t kSigZip64Loc   = 0x07064b50u;  ///< EOCD64 locator
constexpr uint32_t kSigZip64Eocd  = 0x06064b50u;  ///< EOCD64 record

constexpr size_t kLocalFixed   = 30;
constexpr size_t kCentralFixed = 46;
constexpr size_t kEocdFixed    = 22;

/// Version-needed-to-extract. 20 = "2.0", which is what DEFLATE requires and
/// is the only value this subset accepts in either header.
constexpr uint16_t kVersionNeeded = 20;

/// Fixed DOS timestamp: 1980-01-01 00:00:00, the DOS epoch. See the header's
/// DETERMINISM note. Date is `((year-1980) << 9) | (month << 5) | day`, so
/// 1980-01-01 is `(0 << 9) | (1 << 5) | 1` = 0x0021. Month and day are
/// 1-based, so 0 is not a legal encoding and 0x0000 would be a malformed date
/// some tools flag.
constexpr uint16_t kDosDate = 0x0021;
constexpr uint16_t kDosTime = 0x0000;

/// The largest value any 32-bit ZIP size or offset field may carry here.
/// 0xFFFFFFFF is the ZIP64 sentinel, so it is not a legal magnitude.
constexpr uint64_t kMaxU32Field = 0xFFFFFFFEull;

/// 0xFFFF in the EOCD entry count is the ZIP64 sentinel.
constexpr size_t kMaxEntries = 0xFFFEu;

/// The EOCD comment field is a `u16` length.
constexpr size_t kMaxComment = 0xFFFFu;

// ── Little-endian primitives ─────────────────────────────────────────────

void put_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

uint16_t get_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::string quoted(const std::string& s) { return "'" + s + "'"; }

std::string u64s(uint64_t v) { return std::to_string(v); }

/// Lower-case hex, for CRC values in refusal messages. A CRC printed in
/// decimal is unrecognisable next to the hex every other tool prints.
std::string hex8(uint32_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(8, '0');
    for (int i = 7; i >= 0; --i) {
        s[static_cast<size_t>(i)] = d[v & 0xF];
        v >>= 4;
    }
    return s;
}

/// Raw DEFLATE (windowBits -15 — no zlib wrapper, which is what ZIP method 8
/// is). Returns false only on an allocation or stream failure.
bool deflate_raw(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    z_stream s{};
    // Level 9: a snapshot is written once and read often, and the input is
    // dominated by mostly-zero guest RAM, which deflate handles very well.
    if (deflateInit2(&s, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }
    out.resize(deflateBound(&s, static_cast<uLong>(len)) + 64);
    s.next_in   = const_cast<Bytef*>(src);
    s.avail_in  = static_cast<uInt>(len);
    s.next_out  = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&s, Z_FINISH);
    const uLong produced = s.total_out;
    deflateEnd(&s);
    if (rc != Z_STREAM_END) {
        out.clear();
        return false;
    }
    out.resize(produced);
    return true;
}

/// Raw INFLATE to an EXACTLY known length. `expect` comes from the header, so
/// the output buffer is sized up front and the stream is refused if it wants
/// more (`avail_out` exhausted with input left) or produces fewer bytes.
///
/// Both directions matter. zlib's own error covers the first; the explicit
/// length comparison afterwards covers the second, which is the one a
/// truncated or hand-edited member produces and which zlib reports as success
/// if the stream happens to end cleanly.
bool inflate_exact(const uint8_t* src, size_t src_len, uint32_t expect,
                   std::vector<uint8_t>& out, std::string& why) {
    out.assign(expect, 0);
    z_stream s{};
    if (inflateInit2(&s, -15) != Z_OK) {
        why = "zlib could not start an inflate stream";
        return false;
    }
    s.next_in   = const_cast<Bytef*>(src);
    s.avail_in  = static_cast<uInt>(src_len);
    s.next_out  = out.empty() ? nullptr : out.data();
    s.avail_out = static_cast<uInt>(expect);
    const int rc = inflate(&s, Z_FINISH);
    const uLong produced = s.total_out;
    inflateEnd(&s);

    if (rc != Z_STREAM_END) {
        why = "the deflate stream is corrupt or longer than its declared " +
              u64s(expect) + " bytes (zlib error " + std::to_string(rc) + ")";
        out.clear();
        return false;
    }
    if (produced != expect) {
        why = "the deflate stream inflates to " + u64s(produced) +
              " bytes, not the declared " + u64s(expect);
        out.clear();
        return false;
    }
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────
// Path grammar
// ─────────────────────────────────────────────────────────────────────────

bool valid_member_path(const std::string& path, std::string& why) {
    if (path.empty()) {
        why = "an empty member path";
        return false;
    }
    if (path.size() > kMaxPathLen) {
        why = "member path " + quoted(path.substr(0, 40)) + "… is " +
              u64s(path.size()) + " bytes, over the " + u64s(kMaxPathLen) +
              "-byte limit";
        return false;
    }

    // First character. This one rule rejects an absolute path, a leading dot
    // and a leading dash, and it is checked before the character class so the
    // message can say which it was.
    const unsigned char first = static_cast<unsigned char>(path[0]);
    const bool first_ok = (first >= 'a' && first <= 'z') ||
                          (first >= '0' && first <= '9');
    if (!first_ok) {
        if (first == '/') {
            why = "member path " + quoted(path) +
                  " is absolute; paths are relative with no leading '/'";
        } else {
            why = "member path " + quoted(path) + " starts with '" +
                  std::string(1, path[0]) +
                  "'; it must start with a lower-case letter or a digit";
        }
        return false;
    }

    // Character class. Rejects backslash, upper case, spaces, control bytes
    // and every non-ASCII byte.
    for (char c : path) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') ||
                        u == '.' || u == '_' || u == '/' || u == '-';
        if (!ok) {
            if (u == '\\') {
                why = "member path " + quoted(path) +
                      " contains a backslash; the separator is '/'";
            } else if (u >= 'A' && u <= 'Z') {
                why = "member path " + quoted(path) +
                      " contains an upper-case letter; paths are lower-case";
            } else {
                why = "member path " + quoted(path) + " contains byte 0x" +
                      hex8(u).substr(6) + ", outside [a-z0-9._/-]";
            }
            return false;
        }
    }

    // Components. The character class alone cannot express these: both '.'
    // and '/' are legal characters, so "a/../b" passes every check above and
    // is still a traversal.
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        if (slash == std::string::npos) slash = path.size();
        const std::string comp = path.substr(start, slash - start);
        if (comp.empty()) {
            why = "member path " + quoted(path) +
                  " has an empty component (a leading, trailing or doubled "
                  "'/')";
            return false;
        }
        if (comp == "." || comp == "..") {
            why = "member path " + quoted(path) + " contains a " +
                  quoted(comp) + " component";
            return false;
        }
        if (slash == path.size()) break;
        start = slash + 1;
    }

    why.clear();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// Writer
// ─────────────────────────────────────────────────────────────────────────

Writer::Writer(std::string comment) : comment_(std::move(comment)) {}

bool Writer::add(const std::string& name, const uint8_t* data, size_t len,
                 Method method, std::string& why) {
    if (!valid_member_path(name, why)) return false;

    for (const Member& m : members_) {
        if (m.name == name) {
            why = "member " + quoted(name) + " was already added";
            return false;
        }
    }
    if (members_.size() >= kMaxEntries) {
        why = "the archive already holds " + u64s(members_.size()) +
              " members, the limit without ZIP64";
        return false;
    }
    if (static_cast<uint64_t>(len) > kMaxU32Field) {
        why = "member " + quoted(name) + " is " + u64s(len) +
              " bytes, over the " + u64s(kMaxU32Field) +
              "-byte limit (ZIP64 is not written)";
        return false;
    }

    Member m;
    m.name        = name;
    m.uncomp_size = static_cast<uint32_t>(len);
    m.crc32       = static_cast<uint32_t>(
        ::crc32(0L, data, static_cast<uInt>(len)));

    if (method == Method::Deflate && len > 0) {
        std::vector<uint8_t> packed;
        if (!deflate_raw(data, len, packed)) {
            why = "member " + quoted(name) + " could not be deflated";
            return false;
        }
        // A "compressed" member larger than its input is a pure loss. Store
        // it instead; the reader handles both methods, so nothing downstream
        // has to know which one a member used.
        if (packed.size() < len) {
            m.method  = Method::Deflate;
            m.payload = std::move(packed);
        } else {
            m.method = Method::Stored;
            m.payload.assign(data, data + len);
        }
    } else {
        m.method = Method::Stored;
        m.payload.assign(data, data + len);
    }

    if (static_cast<uint64_t>(m.payload.size()) > kMaxU32Field) {
        why = "member " + quoted(name) + " compresses to " +
              u64s(m.payload.size()) + " bytes, over the " +
              u64s(kMaxU32Field) + "-byte limit (ZIP64 is not written)";
        return false;
    }

    members_.push_back(std::move(m));
    why.clear();
    return true;
}

bool Writer::add(const std::string& name, const std::string& text,
                 Method method, std::string& why) {
    return add(name, reinterpret_cast<const uint8_t*>(text.data()),
               text.size(), method, why);
}

bool Writer::finish(std::vector<uint8_t>& out, std::string& why) const {
    if (members_.empty()) {
        why = "the archive has no members";
        return false;
    }
    if (comment_.size() > kMaxComment) {
        why = "the archive comment is " + u64s(comment_.size()) +
              " bytes, over the " + u64s(kMaxComment) + "-byte limit";
        return false;
    }

    out.clear();
    std::vector<uint32_t> offsets;
    offsets.reserve(members_.size());

    // Local headers + data, in add order.
    for (const Member& m : members_) {
        if (static_cast<uint64_t>(out.size()) > kMaxU32Field) {
            why = "the archive exceeds " + u64s(kMaxU32Field) +
                  " bytes (ZIP64 is not written)";
            return false;
        }
        offsets.push_back(static_cast<uint32_t>(out.size()));

        put_u32(out, kSigLocal);
        put_u16(out, kVersionNeeded);
        put_u16(out, 0);  // general purpose bit flag — always 0 in this subset
        put_u16(out, static_cast<uint16_t>(m.method));
        put_u16(out, kDosTime);
        put_u16(out, kDosDate);
        put_u32(out, m.crc32);
        put_u32(out, static_cast<uint32_t>(m.payload.size()));
        put_u32(out, m.uncomp_size);
        put_u16(out, static_cast<uint16_t>(m.name.size()));
        put_u16(out, 0);  // extra field length — always 0
        out.insert(out.end(), m.name.begin(), m.name.end());
        out.insert(out.end(), m.payload.begin(), m.payload.end());
    }

    if (static_cast<uint64_t>(out.size()) > kMaxU32Field) {
        why = "the archive exceeds " + u64s(kMaxU32Field) +
              " bytes (ZIP64 is not written)";
        return false;
    }
    const uint32_t cd_offset = static_cast<uint32_t>(out.size());

    // Central directory, in the same order.
    for (size_t i = 0; i < members_.size(); ++i) {
        const Member& m = members_[i];
        put_u32(out, kSigCentral);
        // Version made by: 20 with a host-system byte of 0 (MS-DOS/FAT). The
        // host byte governs how external attributes are read; 0 with zero
        // attributes is the portable "no attributes" spelling, and it is what
        // keeps the archive identical across the platforms jnext ships on.
        put_u16(out, kVersionNeeded);
        put_u16(out, kVersionNeeded);
        put_u16(out, 0);  // general purpose bit flag
        put_u16(out, static_cast<uint16_t>(m.method));
        put_u16(out, kDosTime);
        put_u16(out, kDosDate);
        put_u32(out, m.crc32);
        put_u32(out, static_cast<uint32_t>(m.payload.size()));
        put_u32(out, m.uncomp_size);
        put_u16(out, static_cast<uint16_t>(m.name.size()));
        put_u16(out, 0);  // extra field length
        put_u16(out, 0);  // file comment length
        put_u16(out, 0);  // disk number start
        put_u16(out, 0);  // internal file attributes
        put_u32(out, 0);  // external file attributes
        put_u32(out, offsets[i]);
        out.insert(out.end(), m.name.begin(), m.name.end());
    }

    const uint64_t cd_size = out.size() - cd_offset;
    if (cd_size > kMaxU32Field) {
        why = "the central directory exceeds " + u64s(kMaxU32Field) +
              " bytes (ZIP64 is not written)";
        return false;
    }

    put_u32(out, kSigEocd);
    put_u16(out, 0);  // this disk
    put_u16(out, 0);  // disk with the central directory
    put_u16(out, static_cast<uint16_t>(members_.size()));
    put_u16(out, static_cast<uint16_t>(members_.size()));
    put_u32(out, static_cast<uint32_t>(cd_size));
    put_u32(out, cd_offset);
    put_u16(out, static_cast<uint16_t>(comment_.size()));
    out.insert(out.end(), comment_.begin(), comment_.end());

    why.clear();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────────

bool Reader::open(const uint8_t* data, size_t len, std::string& why) {
    data_ = nullptr;
    len_  = 0;
    entries_.clear();
    comment_.clear();

    if (data == nullptr || len < kEocdFixed) {
        why = "the file is " + u64s(len) +
              " bytes, too short to hold a ZIP end-of-central-directory "
              "record (" + u64s(kEocdFixed) + " bytes)";
        return false;
    }

    // ── Find the EOCD ────────────────────────────────────────────────────
    //
    // Scan backwards from the last position an EOCD could start at. The
    // comment-length cross-check is what disambiguates a false signature
    // occurring inside member data: a real EOCD's declared comment length
    // accounts for EXACTLY the bytes remaining after it.
    const size_t scan_floor =
        (len > kEocdFixed + kMaxComment) ? len - kEocdFixed - kMaxComment : 0;
    size_t eocd = SIZE_MAX;
    for (size_t i = len - kEocdFixed + 1; i-- > scan_floor;) {
        if (get_u32(data + i) != kSigEocd) continue;
        const uint16_t clen = get_u16(data + i + 20);
        if (i + kEocdFixed + clen == len) {
            eocd = i;
            break;
        }
    }
    if (eocd == SIZE_MAX) {
        why = "no ZIP end-of-central-directory record found; this is not a "
              "ZIP archive";
        return false;
    }

    // ── ZIP64 refusals ───────────────────────────────────────────────────
    //
    // Two independent tells, and both are checked because either can appear
    // without the other: a genuine ZIP64 archive carries the locator record
    // immediately before the EOCD, and it ALSO sets one or more EOCD fields
    // to their all-ones sentinel.
    if (eocd >= 20 && get_u32(data + eocd - 20) == kSigZip64Loc) {
        why = "the archive carries a ZIP64 end-of-central-directory locator; "
              "ZIP64 is neither written nor read";
        return false;
    }

    const uint16_t disk_no      = get_u16(data + eocd + 4);
    const uint16_t cd_disk      = get_u16(data + eocd + 6);
    const uint16_t here_count   = get_u16(data + eocd + 8);
    const uint16_t total_count  = get_u16(data + eocd + 10);
    const uint32_t cd_size      = get_u32(data + eocd + 12);
    const uint32_t cd_offset    = get_u32(data + eocd + 16);
    const uint16_t comment_len  = get_u16(data + eocd + 20);

    if (total_count == 0xFFFFu || cd_size == 0xFFFFFFFFu ||
        cd_offset == 0xFFFFFFFFu) {
        why = "the end-of-central-directory record uses a ZIP64 sentinel "
              "value; ZIP64 is neither written nor read";
        return false;
    }
    if (disk_no != 0 || cd_disk != 0) {
        why = "the archive is spanned across disks (this disk " +
              u64s(disk_no) + ", central directory on disk " + u64s(cd_disk) +
              "); spanned archives are not read";
        return false;
    }
    if (here_count != total_count) {
        why = "the end-of-central-directory record claims " +
              u64s(here_count) + " entries on this disk but " +
              u64s(total_count) + " in total; spanned archives are not read";
        return false;
    }
    if (total_count == 0) {
        why = "the archive holds no members";
        return false;
    }
    if (static_cast<uint64_t>(cd_offset) + cd_size > eocd) {
        why = "the central directory (offset " + u64s(cd_offset) + ", " +
              u64s(cd_size) + " bytes) runs past the end-of-central-directory "
              "record at offset " + u64s(eocd) + "; the archive is truncated "
              "or corrupt";
        return false;
    }
    if (cd_offset < kLocalFixed && total_count > 0) {
        why = "the central directory starts at offset " + u64s(cd_offset) +
              ", before any local file header could fit";
        return false;
    }

    comment_.assign(reinterpret_cast<const char*>(data + eocd + kEocdFixed),
                    comment_len);

    // ── Walk the central directory ───────────────────────────────────────
    size_t p = cd_offset;
    const size_t cd_end = static_cast<size_t>(cd_offset) + cd_size;
    for (uint16_t i = 0; i < total_count; ++i) {
        if (p + kCentralFixed > cd_end) {
            why = "central-directory entry " + u64s(i) +
                  " is truncated; the directory declares " + u64s(total_count) +
                  " entries in " + u64s(cd_size) + " bytes";
            return false;
        }
        if (get_u32(data + p) == kSigZip64Eocd) {
            why = "a ZIP64 end-of-central-directory record appears inside the "
                  "central directory; ZIP64 is neither written nor read";
            return false;
        }
        if (get_u32(data + p) != kSigCentral) {
            why = "central-directory entry " + u64s(i) +
                  " has signature 0x" + hex8(get_u32(data + p)) +
                  ", not the expected 0x" + hex8(kSigCentral);
            return false;
        }

        const uint16_t flags    = get_u16(data + p + 8);
        const uint16_t method   = get_u16(data + p + 10);
        const uint32_t crc      = get_u32(data + p + 16);
        const uint32_t csize    = get_u32(data + p + 20);
        const uint32_t usize    = get_u32(data + p + 24);
        const uint16_t name_len = get_u16(data + p + 28);
        const uint16_t xtra_len = get_u16(data + p + 30);
        const uint16_t cmnt_len = get_u16(data + p + 32);
        const uint16_t disk_st  = get_u16(data + p + 34);
        const uint32_t loc_off  = get_u32(data + p + 42);
        const uint16_t vneed    = get_u16(data + p + 6);

        if (p + kCentralFixed + name_len + xtra_len + cmnt_len > cd_end) {
            why = "central-directory entry " + u64s(i) +
                  " declares a name/extra/comment longer than the directory";
            return false;
        }
        const std::string name(
            reinterpret_cast<const char*>(data + p + kCentralFixed), name_len);

        std::string path_why;
        if (!valid_member_path(name, path_why)) {
            why = path_why;
            return false;
        }
        for (const Entry& e : entries_) {
            if (e.name == name) {
                // §6: ZIP permits duplicates and readers disagree about which
                // one wins. Refused outright rather than picked.
                why = "the archive contains member " + quoted(name) +
                      " more than once; duplicate member names are refused "
                      "because ZIP readers disagree about which one wins";
                return false;
            }
        }

        if (flags != 0) {
            if (flags & 0x0008u) {
                why = "member " + quoted(name) +
                      " uses a data descriptor (general-purpose flag bit 3); "
                      "its sizes are not known at the local header, which is "
                      "a framing ambiguity this reader refuses";
            } else if (flags & 0x0041u) {
                why = "member " + quoted(name) +
                      " is encrypted (general-purpose flag 0x" + hex8(flags) +
                      "); encrypted archives are not read";
            } else {
                why = "member " + quoted(name) +
                      " sets general-purpose flags 0x" + hex8(flags) +
                      "; only 0 is accepted";
            }
            return false;
        }
        if (method != static_cast<uint16_t>(Method::Stored) &&
            method != static_cast<uint16_t>(Method::Deflate)) {
            why = "member " + quoted(name) + " uses compression method " +
                  u64s(method) + "; only 0 (stored) and 8 (deflate) are read";
            return false;
        }
        if (vneed > kVersionNeeded) {
            why = "member " + quoted(name) + " needs ZIP version " +
                  u64s(vneed) + " to extract; this reader implements " +
                  u64s(kVersionNeeded);
            return false;
        }
        if (xtra_len != 0 || cmnt_len != 0) {
            why = "member " + quoted(name) + " carries a " +
                  (xtra_len != 0 ? std::string("central-directory extra field")
                                 : std::string("file comment")) +
                  "; neither is written and neither is read";
            return false;
        }
        if (disk_st != 0) {
            why = "member " + quoted(name) + " starts on disk " +
                  u64s(disk_st) + "; spanned archives are not read";
            return false;
        }
        if (method == static_cast<uint16_t>(Method::Stored) && csize != usize) {
            why = "member " + quoted(name) + " is stored but declares " +
                  u64s(csize) + " compressed bytes against " + u64s(usize) +
                  " uncompressed";
            return false;
        }

        Entry e;
        e.name         = name;
        e.method       = static_cast<Method>(method);
        e.crc32        = crc;
        e.comp_size    = csize;
        e.uncomp_size  = usize;
        e.local_offset = loc_off;
        entries_.push_back(std::move(e));

        p += kCentralFixed + name_len + xtra_len + cmnt_len;
    }
    if (p != cd_end) {
        why = "the central directory declares " + u64s(cd_size) +
              " bytes but its " + u64s(total_count) + " entries occupy " +
              u64s(p - cd_offset);
        return false;
    }

    // ── Cross-check every local file header ──────────────────────────────
    //
    // Eagerly, not lazily at read() time. A member nobody reads can still
    // make the archive one that a different implementation resolves
    // differently, and the point of the accepted subset is that there is
    // exactly one reading.
    for (const Entry& e : entries_) {
        const size_t off = e.local_offset;
        if (off + kLocalFixed > cd_offset) {
            why = "member " + quoted(e.name) +
                  " declares a local header at offset " + u64s(off) +
                  ", which does not fit before the central directory at " +
                  u64s(cd_offset);
            return false;
        }
        const uint8_t* lh = data + off;
        if (get_u32(lh) != kSigLocal) {
            why = "member " + quoted(e.name) + " has local header signature "
                  "0x" + hex8(get_u32(lh)) + " at offset " + u64s(off) +
                  ", not the expected 0x" + hex8(kSigLocal);
            return false;
        }

        const uint16_t l_vneed  = get_u16(lh + 4);
        const uint16_t l_flags  = get_u16(lh + 6);
        const uint16_t l_method = get_u16(lh + 8);
        const uint32_t l_crc    = get_u32(lh + 14);
        const uint32_t l_csize  = get_u32(lh + 18);
        const uint32_t l_usize  = get_u32(lh + 22);
        const uint16_t l_nlen   = get_u16(lh + 26);
        const uint16_t l_xlen   = get_u16(lh + 28);

        if (l_flags != 0) {
            why = "member " + quoted(e.name) +
                  " sets general-purpose flags 0x" + hex8(l_flags) +
                  " in its local header; only 0 is accepted";
            return false;
        }
        if (l_xlen != 0) {
            why = "member " + quoted(e.name) +
                  " carries a local extra field; extra fields are neither "
                  "written nor read";
            return false;
        }
        if (l_vneed > kVersionNeeded) {
            why = "member " + quoted(e.name) + " needs ZIP version " +
                  u64s(l_vneed) + " in its local header; this reader "
                  "implements " + u64s(kVersionNeeded);
            return false;
        }
        if (off + kLocalFixed + l_nlen > cd_offset) {
            why = "member " + quoted(e.name) +
                  " has a local header name running past the central "
                  "directory";
            return false;
        }
        const std::string l_name(
            reinterpret_cast<const char*>(lh + kLocalFixed), l_nlen);

        // The local-vs-central disagreement refusal. This is the specific
        // ambiguity where two conforming readers can legitimately extract
        // different bytes for the same member, depending on which header each
        // trusts — the `.szx` failure shape in container form.
        if (l_name != e.name) {
            why = "member " + quoted(e.name) +
                  " is named " + quoted(l_name) +
                  " in its local header; the local header and the central "
                  "directory disagree";
            return false;
        }
        if (l_method != static_cast<uint16_t>(e.method)) {
            why = "member " + quoted(e.name) + " uses method " +
                  u64s(l_method) + " in its local header and " +
                  u64s(static_cast<uint16_t>(e.method)) +
                  " in the central directory; the two disagree";
            return false;
        }
        if (l_crc != e.crc32) {
            why = "member " + quoted(e.name) + " declares CRC-32 0x" +
                  hex8(l_crc) + " in its local header and 0x" +
                  hex8(e.crc32) + " in the central directory; the two "
                  "disagree";
            return false;
        }
        if (l_csize != e.comp_size || l_usize != e.uncomp_size) {
            why = "member " + quoted(e.name) + " declares " + u64s(l_csize) +
                  "/" + u64s(l_usize) +
                  " compressed/uncompressed bytes in its local header and " +
                  u64s(e.comp_size) + "/" + u64s(e.uncomp_size) +
                  " in the central directory; the two disagree";
            return false;
        }

        const size_t data_off = off + kLocalFixed + l_nlen;
        if (static_cast<uint64_t>(data_off) + e.comp_size > cd_offset) {
            why = "member " + quoted(e.name) + " has " + u64s(e.comp_size) +
                  " bytes of data at offset " + u64s(data_off) +
                  " running into the central directory at " + u64s(cd_offset);
            return false;
        }
    }

    data_ = data;
    len_  = len;
    why.clear();
    return true;
}

bool Reader::has(const std::string& name) const { return find(name) != nullptr; }

const Entry* Reader::find(const std::string& name) const {
    for (const Entry& e : entries_) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

bool Reader::read(const std::string& name, std::vector<uint8_t>& out,
                  std::string& why) const {
    out.clear();
    const Entry* e = find(name);
    if (e == nullptr) {
        why = "the archive has no member " + quoted(name);
        return false;
    }
    // open() already bounds-checked every local header and data extent, so
    // the offsets below cannot leave the buffer.
    const uint16_t nlen = get_u16(data_ + e->local_offset + 26);
    const uint8_t* src  = data_ + e->local_offset + kLocalFixed + nlen;

    if (e->method == Method::Stored) {
        out.assign(src, src + e->comp_size);
    } else {
        std::string inflate_why;
        if (!inflate_exact(src, e->comp_size, e->uncomp_size, out,
                           inflate_why)) {
            why = "member " + quoted(name) + ": " + inflate_why;
            return false;
        }
    }

    // No length re-check here: for STORED, open() enforced csize == usize and
    // `out` is exactly csize bytes; for DEFLATE, inflate_exact refuses unless
    // it produced exactly uncomp_size. A third comparison would be a guard no
    // input can trip — which the mutation battery finds by surviving, and
    // which reads as coverage that is not there.
    const uint32_t actual = static_cast<uint32_t>(
        ::crc32(0L, out.empty() ? nullptr : out.data(),
                static_cast<uInt>(out.size())));
    if (actual != e->crc32) {
        why = "member " + quoted(name) + " has CRC-32 0x" + hex8(actual) +
              " but declares 0x" + hex8(e->crc32);
        out.clear();
        return false;
    }
    why.clear();
    return true;
}

bool Reader::read_text(const std::string& name, std::string& out,
                       std::string& why) const {
    std::vector<uint8_t> bytes;
    if (!read(name, bytes, why)) {
        out.clear();
        return false;
    }
    out.assign(bytes.begin(), bytes.end());
    return true;
}

}  // namespace zip
}  // namespace jnext
