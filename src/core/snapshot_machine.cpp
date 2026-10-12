#include "core/snapshot_machine.h"

#include "core/sna_loader.h"
#include "core/szx_loader.h"
#include "core/z80_loader.h"


#include <fstream>
#include <iterator>

namespace snapshot_machine_detail {

inline SnapshotMachine accept(MachineType m) { SnapshotMachine r; r.ok = true; r.machine = m; return r; }
inline SnapshotMachine refuse(std::string why) { SnapshotMachine r; r.why = std::move(why); return r; }

inline uint16_t u16(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

inline SnapshotMachine sna(const std::vector<uint8_t>& b) {
    constexpr size_t k48 = 49179;   // 27-byte header + 48K (sna.c:80-84)
    const size_t n = b.size();
    if (n < k48)
        return refuse("a .sna must be at least 49179 bytes (this one is " + std::to_string(n) + ")");
    if (n == k48) return accept(MachineType::ZX48K);
    // Longer: the 128K form (sna.c:85-88; libspectrum calls it a Pentagon 128,
    // jnext's own saver writes it from the 128K and the +3, D3).
    if (n < k48 + 4)
        return refuse("the 128K .sna is truncated (" + std::to_string(n) + " bytes)");
    if (b[k48 + 3] != 0)
        return refuse("it was saved with the TR-DOS ROM paged in (Beta 128 disk interface), "
                      "which jnext does not emulate");
    return accept(MachineType::ZX128K);
}

inline SnapshotMachine szx(const std::vector<uint8_t>& b) {
    if (b.size() < 8 || std::string(b.begin(), b.begin() + 4) != "ZXST")
        return refuse("not an SZX file");
    const uint8_t id = b[6];
    switch (id) {   // ZXSTMID_* (szx.c:41-58, :2613-2680)
        case 0: case 1: case 15: return accept(MachineType::ZX48K);    // 16K, 48K, NTSC 48K
        case 2: case 3: case 16: return accept(MachineType::ZX128K);   // 128K, +2, 128Ke
        case 4: case 5: case 6:  return accept(MachineType::ZX_PLUS3); // +2A, +3, +3e
        case 7:                  return accept(MachineType::PENTAGON); // Pentagon 128
        case 8:  return refuse("it was saved on a Timex TC2048, which jnext does not emulate");
        case 9:  return refuse("it was saved on a Timex TC2068, which jnext does not emulate");
        case 10: return refuse("it was saved on a Scorpion ZS-256, which jnext does not emulate");
        case 11: return refuse("it was saved on a ZX Spectrum SE, which jnext does not emulate");
        case 12: return refuse("it was saved on a Timex TS2068, which jnext does not emulate");
        case 13: return refuse("it was saved on a Pentagon 512, which jnext does not emulate "
                               "(only the Pentagon 128)");
        case 14: return refuse("it was saved on a Pentagon 1024, which jnext does not emulate "
                               "(only the Pentagon 128)");
        default: return refuse("unknown SZX machine ID " + std::to_string(id));
    }
}

inline SnapshotMachine z80(const std::vector<uint8_t>& b) {
    if (b.size() < 30) return refuse("a .z80 must be at least 30 bytes");
    if (u16(b, 6) != 0) return accept(MachineType::ZX48K);   // version 1 (z80.c:405-414)
    if (b.size() < 32) return refuse("the .z80 header is truncated");
    const uint16_t len = u16(b, 30);
    if (len != 23 && len != 54 && len != 55)   // z80.c:270-285
        return refuse("unknown .z80 header length " + std::to_string(len));
    if (b.size() < 32u + len) return refuse("the .z80 header is truncated");
    const bool    v2 = len == 23;
    const uint8_t hw = b[34];
    // Peripheral ROMs paged in when the snapshot was taken: jnext emulates
    // none of them, so the program would run against a different ROM (D4).
    // byte 36 = 0xFF "Interface I ROM paged"; v3 byte 59 = 0xFF MGT ROM paged
    // (z80.c:297-298); v3 byte 60 = 0xFF Multiface ROM paged.
    const bool if1 = v2 ? (hw == 1 || hw == 4) : (hw == 1 || hw == 5);
    const bool mgt = !v2 && (hw == 3 || hw == 6);
    if (if1 && b[36] == 0xFF)
        return refuse("it was saved with the Interface 1 ROM paged in, which jnext does not emulate");
    if (mgt && b[59] == 0xFF)
        return refuse("it was saved with the MGT DISCiPLE/+D ROM paged in, which jnext does not emulate");
    if (!v2 && b[60] == 0xFF)
        return refuse("it was saved with the Multiface ROM paged in, which jnext cannot reconstruct");
    // Byte 37 bit 7 ("modify hardware", z80.c:320-352) changes the machine to
    // its sibling (48K->16K, 128K->+2, +3->+2A): all run as the same jnext machine.
    if (v2) {
        switch (hw) {   // z80.c:449-465
            case 0: case 1: case 2: return accept(MachineType::ZX48K);
            case 3: case 4:         return accept(MachineType::ZX128K);
            case 5: case 6:         return refuse("unknown v2 .z80 hardware mode " + std::to_string(hw));
            default: break;
        }
    } else {
        switch (hw) {   // z80.c:501-520
            case 0: case 1: case 2: case 3: return accept(MachineType::ZX48K);
            case 4: case 5: case 6:         return accept(MachineType::ZX128K);
            default: break;
        }
    }
    switch (hw) {   // z80.c:537-560
        case 7: case 8: case 13: return accept(MachineType::ZX_PLUS3);
        case 9:                  return accept(MachineType::PENTAGON);
        case 12:                 return accept(MachineType::ZX128K);   // +2
        case 10: return refuse("it was saved on a Scorpion ZS-256, which jnext does not emulate");
        case 11: return refuse("it was saved on a Didaktik Kompakt, which jnext does not emulate");
        case 14: return refuse("it was saved on a Timex TC2048, which jnext does not emulate");
        case 15: return refuse("it was saved on a Timex TC2068, which jnext does not emulate");
        case 128: return refuse("it was saved on a Timex TS2068, which jnext does not emulate");
        default: return refuse("unknown .z80 hardware mode " + std::to_string(hw));
    }
}

}   // namespace snapshot_machine_detail

SnapshotMachine snapshot_machine(const std::vector<uint8_t>& bytes, const std::string& ext) {
    using namespace snapshot_machine_detail;
    if (ext == "sna") return sna(bytes);
    if (ext == "szx") return szx(bytes);
    if (ext == "z80") return z80(bytes);
    return refuse("'." + ext + "' is not a snapshot type");
}

SnapshotMachine snapshot_file_machine(const std::string& path, const std::string& ext) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return snapshot_machine_detail::refuse("cannot read '" + path + "'");
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    return snapshot_machine(bytes, ext.empty() ? snapshot_file_ext(path) : ext);
}


bool snapshot_file_loadable(const std::string& path, std::string& why)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { why = "cannot read the file"; return false; }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    const std::string ext = snapshot_file_ext(path);
    const SnapshotMachine m = snapshot_machine(bytes, ext);
    if (!m.ok) { why = m.why; return false; }
    bool parsed = false;
    if (ext == "sna")      { SnaLoader l; parsed = l.load_from_buffer(bytes, path); }
    else if (ext == "szx") { SzxLoader l; parsed = l.load_from_buffer(bytes, path); }
    else                   { Z80Loader l; parsed = l.load_from_buffer(bytes); }
    if (!parsed) { why = "the file is damaged (see the log)"; return false; }
    why.clear();
    return true;
}
