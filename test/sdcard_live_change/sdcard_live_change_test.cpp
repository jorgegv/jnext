// A running NextZXOS finds a file the SD card manager added to its card mid-run (GH #558, SDCOH-06).
//
// ORACLE: real NextZXOS's esxDOS answer, in jnext. A probe NEX run under NextZXOS (started with `.nexload`, as
// test/00regression/esxdos-write-probe-measure.sh does) calls F_OPEN on C:/COHDATA.TXT twice. The file does not exist
// at the first call (carry set: an error). While the machine keeps running, between two frames and never paused, the
// manager's engine adds the file (add_to_image, the call run_transfer makes). At the second call the file must be
// there (carry clear) and F_READ must return the host file's bytes. Nothing in jnext is computed from the other side:
// the answers are the guest's, written by the probe into a mailbox the test reads.
//
// The card is a PRIVATE copy of the test master (the suites' own card, ~/.jnext/sdcard/test-suite/, never the user's),
// deleted at the end. No NextZXOS image is ever committed, and none is kept: the copy lives in the scratch directory.
// The boot timeline is the regression suite's (the keys of esxdos-write-probe-measure.sh), shifted by the default
// master's boot delay: 4 frames (BOOT_DEFAULT_COMMIT 476 against BOOT_REF_COMMIT 472, test-functions.inc).
//
// The probe (Z80, org 0x8000):
//   di; F_OPEN("C:/COHDATA.TXT", read); mailbox[1]=carry, [2]=A; mailbox[0]=0xA5;
//   wait; F_OPEN + F_CLOSE of a file in ANOTHER folder (C:/nextzxos/48k.scr); F_OPEN again; mailbox[0x11]=carry,
//   [0x12]=A; if it opened: F_READ 16 bytes to 0x8220; mailbox[0x10]=0xA6; spin.
// The read in the other folder is the rule the guide gives the user: NextZXOS's FAT code keeps the sector window it
// last read (as every FatFs does), so a change is seen when the guest next reads the folder AFTER having read
// another sector ("enter a folder and come back", `dir` again). Measured here: with no other read between the two
// opens, the second F_OPEN of the same folder answers from that window and does not see the file.
//
// Run: ./build/test/sdcard_live_change_test
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/sdcard_file_add.h"
#include "debug/debugger.h"
#include "debug/inspect.h"
#include "../row_id.h"
#include "../test_sd_image.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

// The probe, assembled by hand (esxDOS: RST 8 then the call byte; F_OPEN $9A: A='*', IX=name, B=mode; F_READ $9D:
// A=handle, IX=buffer, BC=length). Mailbox at 0x8200, name at 0x8060.
const Bytes kProbe = {
    0xF3, 0xDD, 0x21, 0x60, 0x80, 0x3E, 0x2A, 0x06, 0x01, 0xCF, 0x9A, 0xF5,
    0xC1, 0x79, 0xE6, 0x01, 0x32, 0x01, 0x82, 0x78, 0x32, 0x02, 0x82, 0x3E,
    0xA5, 0x32, 0x00, 0x82, 0x01, 0x00, 0x00, 0x0B, 0x78, 0xB1, 0x20, 0xFB,
    0xDD, 0x21, 0x80, 0x80, 0x3E, 0x2A, 0x06, 0x01, 0xCF, 0x9A, 0x38, 0x02,
    0xCF, 0x9B, 0xDD, 0x21, 0x60, 0x80, 0x3E, 0x2A, 0x06, 0x01, 0xCF, 0x9A,
    0xF5, 0xC1, 0x79, 0xE6, 0x01, 0x32, 0x11, 0x82, 0x78, 0x32, 0x12, 0x82,
    0x79, 0xE6, 0x01, 0x20, 0x0A, 0x78, 0xDD, 0x21, 0x20, 0x82, 0x01, 0x10,
    0x00, 0xCF, 0x9D, 0x3E, 0xA6, 0x32, 0x10, 0x82, 0x18, 0xFE,
};
const std::string kName = "C:/COHDATA.TXT";
const std::string kName2 = "C:/nextzxos/48k.scr";   // any file in ANOTHER folder (it only has to be read)

// A NEX V1.2 with banks 2 and 0 (the layout of the regression probe): header, bank 2 (the code at 0x8000 and the
// name at 0x8060), bank 0.
Bytes make_nex() {
    Bytes h(512, 0);
    std::memcpy(h.data(), "Next", 4);
    std::memcpy(h.data() + 4, "V1.2", 4);
    h[9] = 2;                                  // banks stored
    h[12] = 0xF0; h[13] = 0xBF;                // SP = 0xBFF0
    h[14] = 0x00; h[15] = 0x80;                // PC = 0x8000
    h[18 + 0] = 1;                             // bank 0 present
    h[18 + 2] = 1;                             // bank 2 present
    Bytes bank2(16384, 0), bank0(16384, 0);
    std::memcpy(bank2.data(), kProbe.data(), kProbe.size());
    std::memcpy(bank2.data() + 0x60, kName.c_str(), kName.size() + 1);
    std::memcpy(bank2.data() + 0x80, kName2.c_str(), kName2.size() + 1);
    Bytes out = h;
    out.insert(out.end(), bank2.begin(), bank2.end());   // NEX file order: 5, 2, 0, ...
    out.insert(out.end(), bank0.begin(), bank0.end());
    return out;
}

void write_file(const fs::path& p, const Bytes& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

}  // namespace

int main() {
    // The private copy of the card goes where the harness puts its own clones ($HOME/.jnext/runs, never the RAM-backed
    // temp directory, never beside the master): removed at the end.
    const char* home = std::getenv("HOME");
    if (!(home && *home)) home = std::getenv("USERPROFILE");
    const fs::path scratch = fs::path(home ? home : ".") / ".jnext" / "runs" / ("sdcoh-" + std::to_string(static_cast<long>(::getpid())));
    {
        std::error_code mk;
        fs::create_directories(scratch, mk);
    }
    const std::string master = test_sd_image_path();
    std::error_code ec;
    const fs::path clone = scratch / "card.img";
    const bool copied = fs::exists(master, ec) && fs::copy_file(master, clone, fs::copy_options::overwrite_existing, ec) && !ec;

    const Bytes data = {'J', 'N', 'E', 'X', 'T', '-', 'C', 'O', 'H', 'E', 'R', 'E', 'N', 'C', 'E', '!'};
    const fs::path host_nex = scratch / "COH.NEX", host_data = scratch / "COHDATA.TXT";
    write_file(host_nex, make_nex());
    write_file(host_data, data);

    std::string err;
    const bool nex_on_card = copied && sdcard::add_to_image(clone.string(), host_nex.string(), "/COH.NEX", false, err) == sdcard::FileAddStatus::Ok;

    bool booted = false, first_seen = false, second_seen = false, never_paused = true, add_ok = false;
    int first_frame = -1, second_frame = -1;
    uint8_t c1 = 9, a1 = 0, c2 = 9, a2 = 0;
    Bytes got(16, 0);
    bool reset_requested = false;
    if (nex_on_card) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.sd_card_image = clone.string();
        cfg.rewind_buffer_frames = 0;
        cfg.rtc_fixed = true;
        std::tm tm{};
        tm.tm_year = 2026 - 1900; tm.tm_mon = 6; tm.tm_mday = 10; tm.tm_hour = 8; tm.tm_min = 55; tm.tm_sec = 0;
        cfg.rtc_fixed_tm = tm;
        auto emu = std::make_unique<Emulator>();
        booted = emu->init(cfg);
        jnext::dbg::Debugger dbg(*emu);

        // the regression suite's boot timeline (esxdos-write-probe-measure.sh), shifted by the default master's +4
        constexpr int kShift = 4;
        struct Key { int frame; std::string name; };
        std::vector<Key> keys = {{577 + kShift, "space"}, {647 + kShift, "down"}, {677 + kShift, "enter"}};
        int f = 737 + kShift;
        for (char ch : std::string(".nexload coh.nex")) {
            keys.push_back({f, ch == ' ' ? std::string("space") : std::string(1, ch)});
            f += 15;
        }
        keys.push_back({f, "enter"});

        for (int frame = 0; booted && frame < 4200 && !second_seen; ++frame) {
            for (const Key& k : keys) {
                if (k.frame != frame) continue;
                jnext::dbg::MatrixKey mk;
                if (jnext::dbg::key_name_to_matrix(k.name, mk))
                    emu->keyboard().queue_auto_type({{mk.row1, mk.col1, mk.row2, mk.col2, 5}});
            }
            emu->run_frame();
            if (dbg.state().paused) never_paused = false;
            if (!first_seen && emu->mmu().read(0x8200) == 0xA5 && emu->mmu().read(0x8000) == 0xF3) {
                first_seen = true;
                first_frame = frame;
                c1 = emu->mmu().read(0x8201);
                a1 = emu->mmu().read(0x8202);
                // the manager adds the file NOW: between two frames, the machine running
                add_ok = sdcard::add_to_image(clone.string(), host_data.string(), "/COHDATA.TXT", false, err, nullptr, true) ==
                         sdcard::FileAddStatus::Ok;
            }
            if (first_seen && emu->mmu().read(0x8210) == 0xA6) {
                second_seen = true;
                second_frame = frame;
                c2 = emu->mmu().read(0x8211);
                a2 = emu->mmu().read(0x8212);
                for (int i = 0; i < 16; ++i) got[static_cast<size_t>(i)] = emu->mmu().read(static_cast<uint16_t>(0x8220 + i));
            }
        }
        reset_requested = emu->take_hard_reset_request();
    }
    std::string detail = "master=" + master + " copied=" + std::to_string(copied) + " nex=" + std::to_string(nex_on_card) +
                         " booted=" + std::to_string(booted) + " first@" + std::to_string(first_frame) + " c1=" + std::to_string(c1) +
                         " a1=" + std::to_string(a1) + " add=" + std::to_string(add_ok) + " " + err + " second@" + std::to_string(second_frame) +
                         " c2=" + std::to_string(c2) + " a2=" + std::to_string(a2);
    check("SDCOH-06",
          "real NextZXOS: a probe's first F_OPEN of C:/COHDATA.TXT fails (carry set); the manager's engine adds the file while the machine runs, never "
          "paused and with no reset requested; the probe's second F_OPEN succeeds (carry clear) and F_READ returns the host file's 16 bytes",
          copied && nex_on_card && first_seen && c1 == 1 && add_ok && second_seen && c2 == 0 && got == data && never_paused && !reset_requested,
          detail);
    fs::remove_all(scratch, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
