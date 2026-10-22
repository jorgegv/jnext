// Builds ZIP archives in memory with knobs per member and per EOCD, for the
// rows that need a shape no foreign writer produces on demand (GH #392):
// declared sizes and CRC that lie, a local extra field unlike the central one,
// ZIP64 sentinels, a multi-disk EOCD. Oracle for the layout: PKWARE APPNOTE.TXT
// 4.3.7 (local header), 4.3.12 (central header), 4.3.16 (EOCD).
#pragma once

#include <zlib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace zipfx {

using Bytes = std::vector<uint8_t>;

struct Member {
    std::string name;
    Bytes       data;                 ///< the uncompressed bytes
    uint16_t    method = 8;           ///< 0 stored, 8 deflate, anything else: raw bytes
    uint16_t    flags = 0;
    size_t      local_extra = 0;      ///< length of the local extra field (zeros)
    size_t      central_extra = 0;    ///< length of the central extra field (zeros)
    int64_t     crc_override = -1;
    int64_t     csize_override = -1;  ///< central directory only
    int64_t     usize_override = -1;  ///< central directory only
    int64_t     local_offset_override = -1;   ///< central directory only
    int         disk_start = 0;               ///< central directory only
    int         central_name_len_override = -1;
    bool        corrupt_local_sig = false;
    bool        use_raw = false;      ///< on-disk bytes are `raw`, not `data` compressed
    Bytes       raw;
};

struct Eocd {
    std::string comment;
    uint16_t    disk = 0;
    uint16_t    cd_disk = 0;
    int         total_override = -1;  ///< both entry counts
    int         here_override = -1;   ///< this-disk entry count only
    int         central_extra_entries = 0;  ///< entries the EOCD claims beyond the real ones (total only)
    int64_t     cd_size_override = -1;
    int64_t     cd_off_override = -1;
    bool        zip64_locator = false;
    Bytes       central_tail;         ///< bytes appended inside the central directory (counted in its size)
};

inline void p16(Bytes& b, uint32_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
inline void p32(Bytes& b, uint32_t v) { p16(b, v & 0xFFFF); p16(b, v >> 16); }

inline Bytes deflate_raw(const Bytes& in) {
    z_stream s{};
    deflateInit2(&s, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    Bytes out(deflateBound(&s, static_cast<uLong>(in.size())) + 64);
    s.next_in = const_cast<Bytef*>(in.data());
    s.avail_in = static_cast<uInt>(in.size());
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    deflate(&s, Z_FINISH);
    out.resize(s.total_out);
    deflateEnd(&s);
    return out;
}

inline Bytes build(const std::vector<Member>& members, const Eocd& e = {}) {
    Bytes out, central;
    for (const Member& m : members) {
        Bytes disk;
        if (m.use_raw) disk = m.raw;
        else if (m.method == 8) disk = deflate_raw(m.data);
        else disk = m.data;
        const uint32_t crc = static_cast<uint32_t>(
            crc32(0, m.data.empty() ? Z_NULL : m.data.data(), static_cast<uInt>(m.data.size())));
        const uint32_t use_crc = m.crc_override >= 0 ? static_cast<uint32_t>(m.crc_override) : crc;
        const uint32_t csize = m.csize_override >= 0 ? static_cast<uint32_t>(m.csize_override)
                                                      : static_cast<uint32_t>(disk.size());
        const uint32_t usize = m.usize_override >= 0 ? static_cast<uint32_t>(m.usize_override)
                                                      : static_cast<uint32_t>(m.data.size());
        const uint32_t off = static_cast<uint32_t>(out.size());

        p32(out, m.corrupt_local_sig ? 0x04034b51 : 0x04034b50);
        p16(out, 20); p16(out, m.flags); p16(out, m.method);
        p16(out, 0); p16(out, 0x5A21);
        p32(out, use_crc); p32(out, static_cast<uint32_t>(disk.size()));
        p32(out, static_cast<uint32_t>(m.data.size()));
        p16(out, static_cast<uint32_t>(m.name.size())); p16(out, static_cast<uint32_t>(m.local_extra));
        out.insert(out.end(), m.name.begin(), m.name.end());
        out.insert(out.end(), m.local_extra, 0);
        out.insert(out.end(), disk.begin(), disk.end());

        p32(central, 0x02014b50);
        p16(central, 20); p16(central, 20); p16(central, m.flags); p16(central, m.method);
        p16(central, 0); p16(central, 0x5A21);
        p32(central, use_crc); p32(central, csize); p32(central, usize);
        p16(central, m.central_name_len_override >= 0 ? static_cast<uint32_t>(m.central_name_len_override)
                                                      : static_cast<uint32_t>(m.name.size()));
        p16(central, static_cast<uint32_t>(m.central_extra));
        p16(central, 0); p16(central, static_cast<uint32_t>(m.disk_start)); p16(central, 0); p32(central, 0);
        p32(central, m.local_offset_override >= 0 ? static_cast<uint32_t>(m.local_offset_override) : off);
        central.insert(central.end(), m.name.begin(), m.name.end());
        central.insert(central.end(), m.central_extra, 0);
    }
    central.insert(central.end(), e.central_tail.begin(), e.central_tail.end());
    const uint32_t cd_off = static_cast<uint32_t>(out.size());
    out.insert(out.end(), central.begin(), central.end());
    if (e.zip64_locator) { p32(out, 0x07064b50); out.insert(out.end(), 16, 0); }
    const uint32_t n = e.total_override >= 0 ? static_cast<uint32_t>(e.total_override)
                                             : static_cast<uint32_t>(members.size() + e.central_extra_entries);
    p32(out, 0x06054b50);
    const uint32_t nh = e.here_override >= 0 ? static_cast<uint32_t>(e.here_override) : n;
    p16(out, e.disk); p16(out, e.cd_disk); p16(out, nh); p16(out, n);
    p32(out, e.cd_size_override >= 0 ? static_cast<uint32_t>(e.cd_size_override)
                                      : static_cast<uint32_t>(central.size()));
    p32(out, e.cd_off_override >= 0 ? static_cast<uint32_t>(e.cd_off_override) : cd_off);
    p16(out, static_cast<uint32_t>(e.comment.size()));
    out.insert(out.end(), e.comment.begin(), e.comment.end());
    return out;
}

}  // namespace zipfx
