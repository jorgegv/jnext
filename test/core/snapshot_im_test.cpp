// A loaded snapshot's interrupt mode reaches the IM latch behind NR 0xC0.
//
// Oracle: VHDL. NR 0xC0 bits 2:1 read the current Z80 interrupt mode
// (zxnext.vhd:6230 `... & z80_im_mode & ...`, fed by im2_control's o_im_mode,
// :1905), and im2_control.vhd latches that mode by decoding the IM
// instructions the CPU executes (:218-229). A snapshot
// (.sna, .z80, .szx) restores the CPU straight into its interrupt mode with no
// IM instruction executed, so the latch must be seeded from it — otherwise NR
// 0xC0 read IM 0 for a machine running in IM 1 or 2, and the hardware-IM2
// gates (im2_control's im_mode = "10") disagreed with the CPU.
//
// The IM each file carries: .sna header byte 25; .z80 byte 29 bits 1:0; .szx
// the Z80R chunk (written here by jnext's own SzxSaver, the round trip the
// SNAPSAVE-SZX rows use).
//
// Run: ./build/test/snapshot_im_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/szx_saver.h"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition,
           const std::string& detail = {}) {
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

std::string write_file(const char* name, const std::vector<uint8_t>& bytes) {
    const auto path = g_root / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

std::unique_ptr<Emulator> make_emu() {
    auto emu = std::make_unique<Emulator>();
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = 0;
    emu->init(cfg);
    return emu;
}

// 48K .sna: 27-byte header (IM at byte 25), then 48 KB. SP points at a
// return address inside the dump (the .sna convention for PC).
std::vector<uint8_t> sna(uint8_t im) {
    std::vector<uint8_t> f(27 + 49152, 0x00);
    f[23] = 0x00; f[24] = 0x80;     // SP = $8000
    f[25] = im;
    f[27 + 0x4000] = 0x00;          // [$8000] = $8000 return address
    f[27 + 0x4001] = 0x80;
    return f;
}

// .z80 version 1, uncompressed: 30-byte header (IM in byte 29 bits 1:0).
std::vector<uint8_t> z80v1(uint8_t im) {
    std::vector<uint8_t> f(30 + 49152, 0x00);
    f[6] = 0x00; f[7] = 0x80;       // PC = $8000 (non-zero: version 1)
    f[8] = 0x00; f[9] = 0xFF;       // SP
    f[12] = 0x00;                   // uncompressed
    f[29] = im;
    return f;
}

struct Result { bool ok = false; uint8_t cpu_im = 0xFF; uint8_t nr_im = 0xFF; };

Result result(Emulator& emu, bool ok) {
    Result r;
    r.ok = ok;
    r.cpu_im = emu.cpu().get_registers().IM;
    r.nr_im = static_cast<uint8_t>((emu.nextreg().read(0xC0) >> 1) & 0x03);
    return r;
}

std::string detail(const Result& r) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "ok=%d cpu IM=%u NR 0xC0 IM=%u",
                  r.ok ? 1 : 0, r.cpu_im, r.nr_im);
    return buf;
}

} // namespace

int main() {
    std::printf("Snapshot interrupt-mode latch tests\n");
    std::printf("===============================================\n\n");

    g_root = std::filesystem::temp_directory_path() /
             ("jnext-snapshot-im-test-" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);

    {
        auto emu = make_emu();
        const Result r = result(*emu, emu->load_sna(write_file("im2.sna", sna(2))));
        check("SNAPIM-01", "a .sna in IM 2 loads with NR 0xC0 bits 2:1 = 2, as the CPU",
              r.ok && r.cpu_im == 2 && r.nr_im == 2, detail(r));
    }
    {
        auto emu = make_emu();
        const Result r = result(*emu, emu->load_sna(write_file("im1.sna", sna(1))));
        check("SNAPIM-02", "a .sna in IM 1 loads with NR 0xC0 bits 2:1 = 1",
              r.ok && r.cpu_im == 1 && r.nr_im == 1, detail(r));
    }
    {
        auto emu = make_emu();
        const Result r = result(*emu, emu->load_z80(write_file("im2.z80", z80v1(2))));
        check("SNAPIM-03", "a .z80 in IM 2 loads with NR 0xC0 bits 2:1 = 2",
              r.ok && r.cpu_im == 2 && r.nr_im == 2, detail(r));
    }
    {
        // An .szx written by SzxSaver from a 48K machine put in IM 2.
        auto src = make_emu();
        Z80Registers regs = src->cpu().get_registers();
        regs.IM = 2;
        regs.PC = 0x8000;
        src->cpu().set_registers(regs);
        const SzxSaver::SaveResult saved = SzxSaver::save(*src);
        auto emu = make_emu();
        const bool ok = saved.ok && emu->load_szx(write_file("im2.szx", saved.data));
        const Result r = result(*emu, ok);
        check("SNAPIM-04", "a .szx in IM 2 loads with NR 0xC0 bits 2:1 = 2",
              r.ok && r.cpu_im == 2 && r.nr_im == 2, detail(r) + " saved=" +
              std::to_string(saved.ok ? 1 : 0));
    }

    std::filesystem::remove_all(g_root, ec);
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
