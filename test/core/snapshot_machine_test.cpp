// GH #323 — loading a .sna, .szx or .z80 runs it on the machine it was saved on.
//
// Oracles (every expected value comes from one of these, never from the code):
//   * libspectrum 1.5.0, how a snapshot says which machine it holds:
//     sna.c:80-88 (size), szx.c:41-58 + :2613-2680 (machine ID), z80.c:270-285
//     (header lengths), :320-352 (modify-hardware flag), :405-565 (hardware
//     modes), and szx.c:471-497 (AY chunk);
//   * the .z80 format description for bytes 36-38 (IF1 ROM paged, AY), 39-54
//     (AY registers), 59-60 (MGT / Multiface paged) and 86 (port 0x1FFD);
//   * the VHDL: zxnext.vhd:3718 (port 0x7FFD lock bit closes 0x1FFD),
//     :5741-5757 (NR 0x03 type decode), zxula_timing.vhd:282-298 (48K at 60 Hz
//     is the 224 T x 264 line NTSC frame), ports.txt:90-100 (port 0xFF).
// Fixtures are built here from bytes: nothing is checked in.
//
// Run: ./build/test/snapshot_machine_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/rzx.h"
#include "core/saveable.h"
#include "core/snapshot_machine.h"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

using Bytes = std::vector<uint8_t>;

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition,
           const std::string& detail = {}) {
    report_row_id(id);
    ++total;
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::filesystem::path g_root;

std::string write_file(const std::string& name, const Bytes& b) {
    const auto path = g_root / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return path.string();
}

// ---- fixtures --------------------------------------------------------------

// .sna: 49179 bytes = 48K; longer = 128K form (extended header after the 48K
// dump, then the five remaining banks). `trdos` is ext byte 3.
Bytes sna_bytes(size_t n, uint8_t trdos = 0) {
    Bytes f(n, 0);
    if (n > 49179 + 3) f[49179 + 3] = trdos;
    return f;
}

// 48K .sna the loader can run: SP = $8000, [$8000] = $9000 (PC).
Bytes sna48(uint8_t im = 1) {
    Bytes f = sna_bytes(49179);
    f[23] = 0x00; f[24] = 0x80;
    f[25] = im;
    f[27 + 0x4000] = 0x00; f[27 + 0x4001] = 0x90;
    return f;
}

// 128K .sna: bank 7 paged at $C000 (7FFD = 0x07) with first byte 0xB7, PC $8123.
Bytes sna128(uint8_t im = 0) {
    Bytes f = sna_bytes(131103);
    f[25] = im;
    f[27 + 32768] = 0xB7;                          // first byte of the paged bank
    f[49179 + 0] = 0x23; f[49179 + 1] = 0x81;      // PC
    f[49179 + 2] = 0x07;                           // 7FFD
    return f;
}

// .z80 version 2/3. `len` = 23 / 54 / 55; `hw` byte 34. Memory pages are
// uncompressed; a 128K-class page N (3..10) starts with 0x60+N, a 48K file
// carries pages 4, 5 and 8 the same way.
Bytes z80_bytes(uint16_t len, uint8_t hw, bool pages = true) {
    Bytes f(32 + len, 0);
    f[30] = static_cast<uint8_t>(len);
    f[31] = static_cast<uint8_t>(len >> 8);
    f[32] = 0x00; f[33] = 0x80;                     // PC $8000
    f[34] = hw;
    if (pages) {
        const bool k128 = (len == 23) ? hw >= 3 : hw >= 4;
        const std::vector<int> ids = k128 ? std::vector<int>{3, 4, 5, 6, 7, 8, 9, 10}
                                          : std::vector<int>{4, 5, 8};
        for (int p : ids) {
            f.insert(f.end(), {0xFF, 0xFF, static_cast<uint8_t>(p)});
            Bytes page(16384, 0);
            page[0] = static_cast<uint8_t>(0x60 + p);
            f.insert(f.end(), page.begin(), page.end());
        }
    }
    return f;
}

// .z80 version 1, uncompressed, PC $8000; the first RAM byte ($4000) is 0x11.
Bytes z80_v1() {
    Bytes f(30 + 49152, 0);
    f[6] = 0x00; f[7] = 0x80;
    f[8] = 0x00; f[9] = 0xFF;
    f[30] = 0x11;
    return f;
}

// .szx: header + Z80R + (optional) SPCR + RAMP pages + extra chunks.
void chunk(Bytes& f, const char id[4], const Bytes& data) {
    f.insert(f.end(), id, id + 4);
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(data.size() >> (8 * i)));
    f.insert(f.end(), data.begin(), data.end());
}

struct SzxOpts {
    uint8_t p7ffd = 0, p1ffd = 0;
    bool    spcr = true;
    std::vector<int> pages;       // RAM bank numbers; first byte = 0x40+bank
    bool    ay = false;  uint8_t ay_sel = 0; uint8_t ay_regs[16] = {};
};

Bytes szx_bytes(uint8_t id, const SzxOpts& o = {}) {
    Bytes f = {'Z', 'X', 'S', 'T', 1, 4, id, 0};
    Bytes z(37, 0);
    z[20] = 0x00; z[21] = 0xF0;      // SP
    z[22] = 0x00; z[23] = 0x80;      // PC
    chunk(f, "Z80R", z);
    if (o.spcr) {
        Bytes s(8, 0);
        s[1] = o.p7ffd; s[2] = o.p1ffd;
        chunk(f, "SPCR", s);
    }
    for (int b : o.pages) {
        Bytes r = {0x00, 0x00, static_cast<uint8_t>(b)};
        Bytes page(16384, 0);
        page[0] = static_cast<uint8_t>(0x40 + b);
        r.insert(r.end(), page.begin(), page.end());
        chunk(f, "RAMP", r);
    }
    if (o.ay) {
        Bytes a = {0x00, o.ay_sel};
        a.insert(a.end(), o.ay_regs, o.ay_regs + 16);
        chunk(f, "AY\0\0", a);
    }
    return f;
}

// ---- emulators ---------------------------------------------------------------

std::unique_ptr<Emulator> make_emu(MachineType t) {
    auto emu = std::make_unique<Emulator>();
    EmulatorConfig cfg;
    cfg.type = t;
    cfg.rewind_buffer_frames = 0;
    emu->init(cfg);
    return emu;
}

std::vector<uint8_t> state_of(const Emulator& emu) {
    StateWriter measure;
    emu.save_state(measure);
    std::vector<uint8_t> buf(measure.position());
    StateWriter w(buf.data(), buf.size());
    emu.save_state(w);
    return buf;
}

SnapshotMachine sm(const Bytes& b, const char* ext) { return snapshot_machine(b, ext); }

bool is(const SnapshotMachine& r, MachineType m) { return r.ok && r.machine == m; }

const char* ts(MachineType t) { return machine_type_str(t); }

uint8_t nr03_type(Emulator& e) { return e.nextreg().read(0x03) & 0x07; }

}  // namespace

int main() {
    std::printf("Snapshot machine selection tests (GH #323)\n");
    std::printf("===============================================\n\n");

    g_root = std::filesystem::temp_directory_path() /
             ("jnext-snapshot-machine-test-" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);

    // ============================ the mapping ================================

    check("SNM-01", ".sna: 49179 bytes = 48K; 131103 and 147487 (the 128K forms) = 128K",
          is(sm(sna_bytes(49179), "sna"), MachineType::ZX48K) &&
          is(sm(sna_bytes(131103), "sna"), MachineType::ZX128K) &&
          is(sm(sna_bytes(147487), "sna"), MachineType::ZX128K) &&
          !sm(sna_bytes(100), "sna").ok);

    {
        const SnapshotMachine r = sm(sna_bytes(131103, 1), "sna");
        check("SNM-02", ".sna 128K saved with TR-DOS paged in is refused naming TR-DOS; the same file "
                        "without it is a 128K",
              !r.ok && r.why.find("TR-DOS") != std::string::npos &&
              is(sm(sna_bytes(131103, 0), "sna"), MachineType::ZX128K), r.why);
    }

    check("SNM-03", ".z80 version 1 (PC != 0) = 48K",
          is(sm(z80_v1(), "z80"), MachineType::ZX48K));

    {
        bool ok = true;
        for (int hw : {0, 1, 2}) ok = ok && is(sm(z80_bytes(23, hw, false), "z80"), MachineType::ZX48K);
        for (int hw : {3, 4}) ok = ok && is(sm(z80_bytes(23, hw, false), "z80"), MachineType::ZX128K);
        for (int hw : {5, 6}) ok = ok && !sm(z80_bytes(23, hw, false), "z80").ok;
        check("SNM-04", ".z80 v2: hardware 0,1,2 = 48K; 3,4 = 128K; 5,6 refused (z80.c:449-465)", ok);
    }

    {
        bool ok = true;
        for (int len : {54, 55}) {
            for (int hw : {0, 1, 2, 3}) ok = ok && is(sm(z80_bytes(len, hw, false), "z80"), MachineType::ZX48K);
            for (int hw : {4, 5, 6})    ok = ok && is(sm(z80_bytes(len, hw, false), "z80"), MachineType::ZX128K);
        }
        check("SNM-05", ".z80 v3 (header 54 and 55): hardware 0-3 = 48K; 4-6 = 128K (z80.c:501-520)", ok);
    }

    {
        bool ok = true;
        for (int len : {23, 54, 55}) {
            for (int hw : {7, 8, 13}) ok = ok && is(sm(z80_bytes(len, hw, false), "z80"), MachineType::ZX_PLUS3);
            ok = ok && is(sm(z80_bytes(len, 12, false), "z80"), MachineType::ZX128K);
            ok = ok && is(sm(z80_bytes(len, 9, false), "z80"), MachineType::PENTAGON);
        }
        check("SNM-06", ".z80 v2/v3: hardware 7,8,13 = +3; 12 (+2) = 128K; 9 = Pentagon (z80.c:537-547)", ok);
    }

    {
        struct C { int hw; const char* word; } cs[] = {
            {10, "Scorpion"}, {11, "Didaktik"}, {14, "TC2048"}, {15, "TC2068"},
            {128, "TS2068"}, {16, "16"}, {200, "200"}};
        bool ok = true;
        std::vector<std::string> reasons;
        for (const C& c : cs) {
            const SnapshotMachine r = sm(z80_bytes(54, c.hw, false), "z80");
            ok = ok && !r.ok && !r.why.empty() && r.why.find(c.word) != std::string::npos;
            for (const auto& o : reasons) ok = ok && o != r.why;
            reasons.push_back(r.why);
        }
        check("SNM-07", ".z80 hardware 10,11,14,15,128 and unknown 16,200 are refused, each reason "
                        "non-empty, distinct and naming the machine or the number", ok);
    }

    {
        bool ok = true;
        for (int len : {30, 22}) {
            Bytes b(32 + 60, 0);
            b[30] = static_cast<uint8_t>(len);
            const SnapshotMachine r = sm(b, "z80");
            ok = ok && !r.ok && r.why.find("header length") != std::string::npos;
        }
        for (int len : {23, 54, 55}) ok = ok && sm(z80_bytes(len, 0, false), "z80").ok;
        check("SNM-08", ".z80 header lengths other than 23/54/55 are refused; those three are accepted "
                        "(z80.c:270-285)", ok);
    }

    {
        bool ok = true;
        auto with37 = [](int hw) { Bytes b = z80_bytes(54, hw, false); b[37] |= 0x80; return b; };
        ok = ok && is(sm(with37(0), "z80"), MachineType::ZX48K);
        ok = ok && is(sm(with37(4), "z80"), MachineType::ZX128K);
        ok = ok && is(sm(with37(7), "z80"), MachineType::ZX_PLUS3);
        ok = ok && is(sm(with37(9), "z80"), MachineType::PENTAGON);
        check("SNM-09", ".z80 byte 37 bit 7 (modify hardware) does not change the machine (z80.c:320-352)", ok);
    }

    {
        Bytes if1 = z80_bytes(54, 1, false);  if1[36] = 0xFF;
        Bytes mgt = z80_bytes(54, 3, false);  mgt[59] = 0xFF;
        Bytes mf  = z80_bytes(54, 4, false);  mf[60]  = 0xFF;
        Bytes if1b = z80_bytes(54, 1, false);
        Bytes mgtb = z80_bytes(54, 3, false);
        Bytes mfb  = z80_bytes(54, 4, false);
        Bytes samram = z80_bytes(54, 2, false); samram[36] = 0xFF;
        Bytes if1_128 = z80_bytes(54, 5, false); if1_128[36] = 0xFF;
        const bool refused = !sm(if1, "z80").ok && !sm(mgt, "z80").ok && !sm(mf, "z80").ok &&
                             !sm(if1_128, "z80").ok;
        const bool accepted = sm(if1b, "z80").ok && sm(mgtb, "z80").ok && sm(mfb, "z80").ok;
        // SamRam (hardware 2) uses byte 36 for something else: not an IF1 flag.
        const bool samram_ok = is(sm(samram, "z80"), MachineType::ZX48K);
        check("SNM-10", ".z80 saved with the Interface 1 / MGT / Multiface ROM paged in is refused; the "
                        "same files with it clear load; SamRam with byte 36 = 0xFF is a 48K",
              refused && accepted && samram_ok);
    }

    {
        bool ok = true;
        for (int id : {0, 1, 15}) ok = ok && is(sm(szx_bytes(id), "szx"), MachineType::ZX48K);
        for (int id : {2, 3, 16}) ok = ok && is(sm(szx_bytes(id), "szx"), MachineType::ZX128K);
        for (int id : {4, 5, 6})  ok = ok && is(sm(szx_bytes(id), "szx"), MachineType::ZX_PLUS3);
        ok = ok && is(sm(szx_bytes(7), "szx"), MachineType::PENTAGON);
        check("SNM-11", ".szx machine IDs 0,1,15 = 48K; 2,3,16 = 128K; 4,5,6 = +3; 7 = Pentagon "
                        "(szx.c:41-58, :2613-2680)", ok);
    }

    {
        struct C { int id; const char* word; } cs[] = {
            {8, "TC2048"}, {9, "TC2068"}, {10, "Scorpion"}, {11, "SE"}, {12, "TS2068"},
            {13, "Pentagon 512"}, {14, "Pentagon 1024"}, {17, "17"}, {255, "255"}};
        bool ok = true;
        std::vector<std::string> reasons;
        for (const C& c : cs) {
            const SnapshotMachine r = sm(szx_bytes(c.id), "szx");
            ok = ok && !r.ok && r.why.find(c.word) != std::string::npos;
            for (const auto& o : reasons) ok = ok && o != r.why;
            reasons.push_back(r.why);
        }
        Bytes bad = szx_bytes(1); bad[0] = 'X';
        ok = ok && !sm(bad, "szx").ok;
        check("SNM-12", ".szx IDs 8-14 (Timex, Scorpion, SE, Pentagon 512/1024), unknown 17,255 and a "
                        "bad magic are refused, each reason distinct and naming the machine", ok);
    }

    {
        // The single table: an RZX embedding the file is judged the same way.
        struct C { Bytes b; const char* ext; };
        std::vector<C> cs;
        cs.push_back({sna_bytes(49179), "sna"});
        cs.push_back({sna_bytes(131103), "sna"});
        cs.push_back({sna_bytes(131103, 1), "sna"});
        cs.push_back({sna_bytes(100), "sna"});
        cs.push_back({z80_v1(), "z80"});
        for (int hw : {0, 1, 3, 4, 5, 7, 9, 10, 12, 13, 14, 15, 128}) {
            cs.push_back({z80_bytes(54, hw, false), "z80"});
            cs.push_back({z80_bytes(23, hw, false), "z80"});
        }
        cs.push_back({Bytes(29, 0), "z80"});
        Bytes z80b = z80_bytes(22, 0, false); z80b[30] = 30;
        cs.push_back({z80b, "z80"});
        for (int id = 0; id <= 17; ++id) cs.push_back({szx_bytes(id), "szx"});
        Bytes bad = szx_bytes(1); bad[1] = 'Q';
        cs.push_back({bad, "szx"});
        bool ok = true;
        std::string why;
        for (const C& c : cs) {
            RzxRecording rec;
            rec.creator       = "Fuse";
            rec.snapshot_data = c.b;
            rec.snapshot_ext  = c.ext;
            MachineType got = MachineType::ZXN_ISSUE2;
            const bool known = rzx::recorded_machine(rec, got);
            const SnapshotMachine r = snapshot_machine(c.b, c.ext);
            if (known != r.ok || (known && got != r.machine)) {
                ok = false;
                why += std::string(" ") + c.ext + "/" + std::to_string(c.b.size());
            }
        }
        check("SNM-13", "rzx::recorded_machine agrees with snapshot_machine() for every input (one table)",
              ok, "differs:" + why);
    }

    // ============================ the loaders ================================

    {   // SNM-20
        auto e = make_emu(MachineType::ZX48K);
        const bool a = e->load_sna(write_file("a128.sna", sna128()));
        const bool a_ok = a && e->config().type == MachineType::ZX128K &&
                          e->mmu().machine_type() == MachineType::ZX128K &&
                          nr03_type(*e) == 2 &&
                          e->mmu().read(0xC000) == 0xB7 &&
                          e->cpu().get_registers().PC == 0x8123;
        const std::string d1 = std::string("128K on 48K: type=") + ts(e->config().type) +
                               " nr03=" + std::to_string(nr03_type(*e));
        auto e2 = make_emu(MachineType::ZX128K);
        const bool b = e2->load_sna(write_file("b48.sna", sna48()));
        const bool b_ok = b && e2->config().type == MachineType::ZX48K &&
                          e2->mmu().machine_type() == MachineType::ZX48K && nr03_type(*e2) == 1 &&
                          e2->cpu().get_registers().PC == 0x9000;
        auto e3 = make_emu(MachineType::ZXN_ISSUE2);
        const bool c = e3->load_sna(write_file("c48.sna", sna48()));
        const bool c_ok = c && e3->config().type == MachineType::ZX48K && nr03_type(*e3) == 1;
        check("SNM-20", ".sna: a 128K file on a 48K makes it a 128K with the file's paging and PC; a 48K "
                        "file on a 128K and on a Next makes it a 48K",
              a_ok && b_ok && c_ok, d1);
    }

    {   // SNM-21
        auto e = make_emu(MachineType::ZX48K);
        bool ok = e->load_z80(write_file("v3h4.z80", z80_bytes(54, 4))) &&
                  e->config().type == MachineType::ZX128K && nr03_type(*e) == 2 &&
                  e->mmu().read(0xC000) == 0x60 + 3 && e->mmu().read(0x4000) == 0x60 + 8;
        auto e2 = make_emu(MachineType::ZX_PLUS3);
        ok = ok && e2->load_z80(write_file("v1.z80", z80_v1())) &&
             e2->config().type == MachineType::ZX48K && nr03_type(*e2) == 1 &&
             e2->mmu().read(0x4000) == 0x11;
        auto e3 = make_emu(MachineType::ZX48K);
        ok = ok && e3->load_z80(write_file("v3h7.z80", z80_bytes(54, 7))) &&
             e3->config().type == MachineType::ZX_PLUS3 && nr03_type(*e3) == 3 &&
             e3->mmu().read(0x4000) == 0x60 + 8;
        auto e4 = make_emu(MachineType::ZXN_ISSUE2);
        ok = ok && e4->load_z80(write_file("v3h4n.z80", z80_bytes(54, 4))) &&
             e4->config().type == MachineType::ZX128K && nr03_type(*e4) == 2;
        check("SNM-21",
              ".z80: v3 hardware 4 on a 48K and on a Next = 128K; v1 on a +3 = 48K; hardware 7 on a 48K = +3, "
              "each with its RAM in the right banks", ok);
    }

    {   // SNM-22
        SzxOpts o128; o128.pages = {0, 5};
        auto e = make_emu(MachineType::ZX48K);
        bool ok = e->load_szx(write_file("id2.szx", szx_bytes(2, o128))) &&
                  e->config().type == MachineType::ZX128K && nr03_type(*e) == 2 &&
                  e->mmu().read(0xC000) == 0x40 + 0 && e->mmu().read(0x4000) == 0x40 + 5;
        SzxOpts o48; o48.spcr = true; o48.pages = {5};
        auto e2 = make_emu(MachineType::ZX_PLUS3);
        ok = ok && e2->load_szx(write_file("id1.szx", szx_bytes(1, o48))) &&
             e2->config().type == MachineType::ZX48K && nr03_type(*e2) == 1;
        auto e3 = make_emu(MachineType::ZX128K);
        ok = ok && e3->load_szx(write_file("id5.szx", szx_bytes(5, o128))) &&
             e3->config().type == MachineType::ZX_PLUS3 && nr03_type(*e3) == 3;
        check("SNM-22", ".szx: ID 2 on a 48K = 128K; ID 1 on a +3 = 48K; ID 5 on a 128K = +3", ok);
    }

    {   // SNM-23: control
        bool same = true;
        auto a = make_emu(MachineType::ZX48K);
        same = same && a->load_sna(write_file("s48.sna", sna48())) && a->config().type == MachineType::ZX48K;
        auto b = make_emu(MachineType::ZX128K);
        same = same && b->load_z80(write_file("s128.z80", z80_bytes(54, 4))) &&
               b->config().type == MachineType::ZX128K;
        auto c = make_emu(MachineType::ZX_PLUS3);
        same = same && c->load_szx(write_file("s3.szx", szx_bytes(5))) &&
               c->config().type == MachineType::ZX_PLUS3;
        auto d = make_emu(MachineType::ZX48K);
        const std::string rzx_path = (g_root / "same.rzx").string();
        const bool started = d->start_rzx_recording(rzx_path);
        d->run_frame();
        const bool loaded = d->load_szx(write_file("s48.szx", szx_bytes(1)));
        const bool still = d->rzx_recorder().is_recording();
        d->stop_rzx_recording();
        check("SNM-23", "a snapshot saved on the running machine changes nothing new: the machine stays, "
                        "and a running RZX recording is not ended",
              same && started && loaded && still);
    }

    {   // SNM-24
        auto e = make_emu(MachineType::ZX128K);
        for (int i = 0; i < 3; ++i) e->run_frame();
        for (uint16_t a = 0x8000; a < 0x8100; ++a) e->mmu().write(a, static_cast<uint8_t>(0xA5 ^ a));
        const std::vector<uint8_t> before = state_of(*e);
        const bool r1 = e->load_szx(write_file("id9.szx", szx_bytes(9)));
        const bool r2 = e->load_z80(write_file("hw14.z80", z80_bytes(54, 14)));
        const bool r3 = e->load_sna(write_file("bad.sna", sna_bytes(100)));
        const bool r4 = e->load_sna(write_file("trdos.sna", sna_bytes(131103, 1)));
        const bool same = e->config().type == MachineType::ZX128K && state_of(*e) == before;
        check("SNM-24", "a refused or corrupt snapshot (.szx ID 9, .z80 hardware 14, a 100-byte .sna, a "
                        "TR-DOS .sna) returns false and leaves the machine byte-identical",
              !r1 && !r2 && !r3 && !r4 && same);
    }

    auto marker_of = [](const std::string& path, MachineType& m) {
        RzxRecording rec;
        return rzx::parse(path, rec) && rzx::recorded_machine(rec, m);
    };

    {   // SNM-25
        auto e = make_emu(MachineType::ZX48K);
        const std::string path = (g_root / "switch.rzx").string();
        const bool started = e->start_rzx_recording(path);
        e->run_frame();
        SzxOpts o; o.pages = {0};
        const bool loaded = e->load_szx(write_file("sw128.szx", szx_bytes(2, o)));
        MachineType m = MachineType::ZXN_ISSUE2;
        const bool parsed = std::filesystem::exists(path) && marker_of(path, m);
        check("SNM-25", "an in-place machine switch ends a running RZX recording first: it is written, "
                        "parses, and names the machine it was made on (48K)",
              started && loaded && !e->rzx_recorder().is_recording() && parsed && m == MachineType::ZX48K,
              std::string("started=") + (started ? "1" : "0") + " loaded=" + (loaded ? "1" : "0") +
              " recording=" + (e->rzx_recorder().is_recording() ? "1" : "0") +
              " parsed=" + (parsed ? "1" : "0"));
    }

    {   // SNM-26
        auto src = make_emu(MachineType::ZX48K);
        src->run_frame();
        const std::string jns = (g_root / "m48.jns").string();
        const bool saved = src->save_jns_file(jns);
        auto e = make_emu(MachineType::ZX128K);
        const std::string path = (g_root / "switch-jns.rzx").string();
        const bool started = e->start_rzx_recording(path);
        e->run_frame();
        const bool loaded = e->load_jns_file(jns);
        MachineType m = MachineType::ZXN_ISSUE2;
        const bool parsed = std::filesystem::exists(path) && marker_of(path, m);
        check("SNM-26", "the same through a .jns: a 48K .jns on a 128K with a recording ends it, and the "
                        "file names the 128K",
              saved && started && loaded && e->config().type == MachineType::ZX48K &&
              !e->rzx_recorder().is_recording() && parsed && m == MachineType::ZX128K);
    }

    {   // SNM-27
        SzxOpts o; o.p7ffd = 0x30; o.p1ffd = 0x04;
        auto e = make_emu(MachineType::ZX_PLUS3);
        const bool loaded = e->load_szx(write_file("lock.szx", szx_bytes(5, o)));
        // +3 ROM = port_1ffd bit 2 : port_7ffd bit 4 (zxnext.vhd:2981-3008): 0x04 + 0x30 = ROM 2 | 1 = 3.
        check("SNM-27", ".szx +3 with 7FFD = 0x30 (locked) and 1FFD = 0x04: both ports land (1FFD is "
                        "written before the lock closes it) and ROM 3 is selected (zxnext.vhd:3718)",
              loaded && e->mmu().port_1ffd() == 0x04 && e->mmu().port_7ffd() == 0x30 &&
              e->mmu().current_rom_bank() == 3,
              "1ffd=" + std::to_string(e->mmu().port_1ffd()) + " 7ffd=" + std::to_string(e->mmu().port_7ffd()) +
              " rom=" + std::to_string(e->mmu().current_rom_bank()));
    }

    {   // SNM-28
        Bytes f55 = z80_bytes(55, 7); f55[35] = 0x30; f55[86] = 0x04;
        Bytes f54 = z80_bytes(54, 7); f54[35] = 0x30;
        auto e = make_emu(MachineType::ZX_PLUS3);
        const bool a = e->load_z80(write_file("p3-55.z80", f55));
        const bool a_ok = a && e->mmu().port_1ffd() == 0x04 && e->mmu().port_7ffd() == 0x30;
        auto e2 = make_emu(MachineType::ZX_PLUS3);
        e2->port().out(0x1FFD, 0x04);
        const bool b = e2->load_z80(write_file("p3-54.z80", f54));
        const bool b_ok = b && e2->mmu().port_1ffd() == 0x00 && e2->mmu().port_7ffd() == 0x30;
        // A 128K (hardware 4) file with a 55-byte header and byte 86 != 0: that byte is
        // not a +3 port 0x1FFD, so nothing may be written to it.
        Bytes f128 = z80_bytes(55, 4); f128[86] = 0x01;
        auto e3 = make_emu(MachineType::ZX48K);
        const bool c = e3->load_z80(write_file("p128-55.z80", f128));
        const bool c_ok = c && e3->config().type == MachineType::ZX128K && e3->mmu().port_1ffd() == 0x00;
        check("SNM-28", ".z80 +3, header 55: byte 86 reaches port 0x1FFD before the 0x7FFD lock; with "
                        "header 54 there is no 1FFD in the file and it stays 0; a 128K file's byte 86 is "
                        "not written to 0x1FFD", a_ok && b_ok && c_ok);
    }

    {   // SNM-30
        auto e = make_emu(MachineType::ZX128K);
        const bool a = e->load_szx(write_file("ntsc.szx", szx_bytes(15)));
        const bool bit = (e->nextreg().cached(0x05) & 0x04) != 0;
        e->run_frame(); e->run_frame();
        const bool ntsc = e->config().type == MachineType::ZX48K && e->video_timing().refresh_60hz() &&
                          e->video_timing().hc_max() == 447 && e->video_timing().vc_max() == 263;
        auto e2 = make_emu(MachineType::ZX128K);
        const bool b = e2->load_szx(write_file("pal.szx", szx_bytes(1)));
        const bool bit2 = (e2->nextreg().cached(0x05) & 0x04) != 0;
        e2->run_frame(); e2->run_frame();
        const bool pal = !e2->video_timing().refresh_60hz() && e2->video_timing().vc_max() == 311;
        // A 48K has no 0x7FFD paging: an NTSC file's SPCR 7FFD byte (here 7) must not repage 0xC000.
        SzxOpts n7; n7.p7ffd = 0x07; n7.pages = {0, 7};
        auto e3 = make_emu(MachineType::ZX128K);
        const bool c = e3->load_szx(write_file("ntsc7.szx", szx_bytes(15, n7)));
        const bool c_ok = c && e3->mmu().port_7ffd() == 0x00 && e3->mmu().read(0xC000) == 0x40 + 0;
        check("SNM-30", ".szx NTSC 48K (ID 15) is a 48K at 60 Hz: NR 0x05 bit 2 set and the frame is 448 x "
                        "264 (zxula_timing.vhd:282-298); ID 1 is the 50 Hz 48K; a 48K file does not write "
                        "port 0x7FFD",
              a && b && bit && ntsc && !bit2 && pal && c_ok);
    }

    {   // SNM-31
        uint8_t regs[16] = {0x11, 0x02, 0x33, 0x04, 0x55, 0x06, 0x07, 0x38,
                            0x09, 0x0A, 0x0B, 0x77, 0x88, 0x0C, 0x00, 0x00};
        auto readback = [&](Emulator& em, bool& ok) {
            // The selected register first (no select in between), then each register.
            ok = ok && em.port().in(0xFFFD) == regs[7];
            for (int r = 0; r <= 13; ++r) {
                em.port().out(0xFFFD, static_cast<uint8_t>(r));
                ok = ok && em.port().in(0xFFFD) == regs[r];
            }
        };
        bool z_ok = true;
        Bytes z = z80_bytes(54, 4);
        z[38] = 7;
        for (int r = 0; r < 16; ++r) z[39 + r] = regs[r];
        auto e = make_emu(MachineType::ZX48K);
        z_ok = z_ok && e->load_z80(write_file("ay.z80", z));
        readback(*e, z_ok);

        bool s_ok = true;
        SzxOpts o; o.ay = true; o.ay_sel = 7; std::copy(regs, regs + 16, o.ay_regs);
        auto e2 = make_emu(MachineType::ZX48K);
        s_ok = s_ok && e2->load_szx(write_file("ay.szx", szx_bytes(2, o)));
        readback(*e2, s_ok);

        // Straddle: a 48K file has no sound chip to restore.
        Bytes z48 = z80_bytes(54, 0);
        z48[38] = 7;
        for (int r = 0; r < 16; ++r) z48[39 + r] = regs[r];
        auto e3 = make_emu(MachineType::ZX128K);
        const bool l48 = e3->load_z80(write_file("ay48.z80", z48));
        e3->port().out(0xFFFD, 7);
        const bool none = l48 && e3->port().in(0xFFFD) != regs[7];
        // A v2 (header 23) 128K .z80 carries the same AY bytes.
        bool v2_ok = true;
        Bytes z2 = z80_bytes(23, 4);
        z2[38] = 7;
        for (int r = 0; r < 16; ++r) z2[39 + r] = regs[r];
        auto e4 = make_emu(MachineType::ZX48K);
        v2_ok = v2_ok && e4->load_z80(write_file("ay-v2.z80", z2));
        readback(*e4, v2_ok);

        // Straddle: a 48K .szx (ID 1) with an AY chunk has no sound chip to restore.
        SzxOpts o48; o48.ay = true; o48.ay_sel = 7; std::copy(regs, regs + 16, o48.ay_regs);
        auto e5 = make_emu(MachineType::ZX128K);
        const bool l48s = e5->load_szx(write_file("ay48.szx", szx_bytes(1, o48)));
        e5->port().out(0xFFFD, 7);
        const bool none_s = l48s && e5->port().in(0xFFFD) != regs[7];
        check("SNM-31", "the AY registers and the selected register are restored from a 128K .z80 (v2 and "
                        "v3, bytes 38-54) and an .szx AY chunk; a 48K .z80 and a 48K .szx restore none",
              z_ok && s_ok && none && v2_ok && none_s);
    }

    {   // SNM-32
        auto e = make_emu(MachineType::ZX48K);
        Bytes z = z80_bytes(54, 9); z[35] = 0x07;
        bool ok = e->load_z80(write_file("pent.z80", z)) && e->config().type == MachineType::PENTAGON &&
                  nr03_type(*e) == 4 && e->nextreg().nr_03_machine_timing() == 4 &&
                  e->mmu().port_7ffd() == 0x07 && e->mmu().read(0xC000) == 0x60 + 10;
        auto e2 = make_emu(MachineType::ZX48K);
        SzxOpts o; o.p7ffd = 0x07; o.pages = {7};
        ok = ok && e2->load_szx(write_file("pent.szx", szx_bytes(7, o))) &&
             e2->config().type == MachineType::PENTAGON && nr03_type(*e2) == 4 &&
             e2->nextreg().nr_03_machine_timing() == 4 && e2->mmu().port_7ffd() == 0x07 &&
             e2->mmu().read(0xC000) == 0x40 + 7;
        check("SNM-32", "a Pentagon 128 .z80 (hardware 9) and .szx (ID 7) on a 48K run on the Pentagon with "
                        "its NR 0x03 type/timing 100 and the file's 0x7FFD", ok);
    }

    {   // SNM-33
        auto e = make_emu(MachineType::ZX48K);
        const bool a = e->load_sna(write_file("im2.sna", sna128(2)));
        const uint8_t im_a = static_cast<uint8_t>((e->nextreg().read(0xC0) >> 1) & 3);
        auto e2 = make_emu(MachineType::ZX48K);
        const bool b = e2->load_snapshot_from_memory(sna48(2), "sna", "mem");
        const uint8_t im_b = static_cast<uint8_t>((e2->nextreg().read(0xC0) >> 1) & 3);
        // The other two arms of the RZX in-memory path: .szx (Z80R chIM at file offset 44) and .z80 (byte 29).
        Bytes sz = szx_bytes(1); sz[44] = 2;
        Bytes z1 = z80_v1(); z1[29] = 2;
        auto e3 = make_emu(MachineType::ZX48K);
        const bool c = e3->load_snapshot_from_memory(sz, "szx", "mem");
        const uint8_t im_c = static_cast<uint8_t>((e3->nextreg().read(0xC0) >> 1) & 3);
        auto e4 = make_emu(MachineType::ZX48K);
        const bool d = e4->load_snapshot_from_memory(z1, "z80", "mem");
        const uint8_t im_d = static_cast<uint8_t>((e4->nextreg().read(0xC0) >> 1) & 3);
        check("SNM-33", "a snapshot that switches machine, and one loaded from memory (RZX; .sna, .szx and "
                        ".z80), leaves NR 0xC0 bits 2:1 at the snapshot's IM (zxnext.vhd:6230)",
              a && b && c && d && im_a == 2 && im_b == 2 && im_c == 2 && im_d == 2 &&
              e->config().type == MachineType::ZX128K,
              "im_a=" + std::to_string(im_a) + " im_b=" + std::to_string(im_b));
    }

    std::filesystem::remove_all(g_root, ec);
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
