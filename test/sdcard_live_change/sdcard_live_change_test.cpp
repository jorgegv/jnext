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
#include "core/image_lock.h"
#include "platform/debug_servers.h"
#include "platform/sdcard_forward.h"
#include "platform/sdcard_manager.h"
#include "../row_id.h"
#include "../test_sd_image.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <thread>
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

struct Probe {
    bool copied = false, nex_on_card = false, booted = false, first_seen = false, second_seen = false;
    bool never_paused = true, add_ok = false, reset_requested = false;
    int first_frame = -1, second_frame = -1;
    uint8_t c1 = 9, a1 = 0, c2 = 9, a2 = 0;
    Bytes got = Bytes(16, 0);
    std::string err, master, extra;
};

// One run of the probe on a fresh private clone. `through_server`: the file is added not by calling the engine but by
// a client of the running jnext, as `jnext --sdcard-file-add` is when the card is held (SDCOH-R01): the lock file names the
// ZRCP server of a real DebugServers, a std::thread runs the client, and this thread pumps the debugger.
Probe run_probe(bool through_server, const fs::path& scratch, const Bytes& data) {
    Probe r;
    std::error_code ec;
    r.master = test_sd_image_path();
    const fs::path clone = scratch / (through_server ? "card7.img" : "card.img");
    r.copied = fs::exists(r.master, ec) && fs::copy_file(r.master, clone, fs::copy_options::overwrite_existing, ec) && !ec;
    const fs::path host_nex = scratch / "COH.NEX", host_data = scratch / "COHDATA.TXT";
    write_file(host_nex, make_nex());
    write_file(host_data, data);
    r.nex_on_card = r.copied && sdcard::add_to_image(clone.string(), host_nex.string(), "/COH.NEX", false, r.err) == sdcard::FileAddStatus::Ok;
    if (!r.nex_on_card) return r;

    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image = clone.string();
    cfg.rewind_buffer_frames = 0;
    cfg.rtc_fixed = true;
    std::tm tm{};
    tm.tm_year = 2026 - 1900; tm.tm_mon = 6; tm.tm_mday = 10; tm.tm_hour = 8; tm.tm_min = 55; tm.tm_sec = 0;
    cfg.rtc_fixed_tm = tm;
    auto emu = std::make_unique<Emulator>();
    r.booted = emu->init(cfg);
    jnext::dbg::Debugger dbg(*emu);

    std::unique_ptr<DebugServers> servers;
    if (through_server) {
        // this process holds the card the way a running jnext does, and serves ZRCP on a free loopback port
        sdcard::sd_locks::set_enabled(true);
        std::string why;
        if (!sdcard::sd_locks::hold(0, clone.string(), "", why)) r.extra += " hold:" + why;
        servers = std::make_unique<DebugServers>();
        EmulatorConfig scfg = cfg;
        scfg.zrcp_port = 0;
        if (!servers->start(dbg, scfg, [&](const jnext::remote::zrcp::SdFileAddRequest& q) { return sdmanager::remote_add(*emu, q, {}); }))
            r.extra += " DebugServers did not start";
    }

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

    for (int frame = 0; r.booted && frame < 4200 && !r.second_seen; ++frame) {
        for (const Key& k : keys) {
            if (k.frame != frame) continue;
            jnext::dbg::MatrixKey mk;
            if (jnext::dbg::key_name_to_matrix(k.name, mk))
                emu->keyboard().queue_auto_type({{mk.row1, mk.col1, mk.row2, mk.col2, 5}});
        }
        emu->run_frame();
        if (dbg.state().paused) r.never_paused = false;
        if (!r.first_seen && emu->mmu().read(0x8200) == 0xA5 && emu->mmu().read(0x8000) == 0xF3) {
            r.first_seen = true;
            r.first_frame = frame;
            r.c1 = emu->mmu().read(0x8201);
            r.a1 = emu->mmu().read(0x8202);
            if (!through_server) {
                // the manager adds the file NOW: between two frames, the machine running
                r.add_ok = sdcard::add_to_image(clone.string(), host_data.string(), "/COHDATA.TXT", false, r.err, nullptr, true) ==
                           sdcard::FileAddStatus::Ok;
            } else {
                // a second jnext, refused the held card, forwards the copy: the lock names the holder and its server
                sdcard::LockHolder holder;
                std::string why;
                const bool refused = !sdcard::ImageLock::acquire(clone.string(), sdcard::ImageLock::Kind::Image, why, {}, &holder).has_value();
                sdforward::ForwardRequest req;
                req.image = clone.string();
                req.host = host_data.string();
                req.card_path = "/COHDATA.TXT";
                req.slot = 0;
                int status = -1;
                std::string text;
                std::atomic<bool> done{false};
                std::thread client([&] { status = sdforward::forward_file_add(holder, req, text); done = true; });
                const auto stop = std::chrono::steady_clock::now() + std::chrono::seconds(60);
                while (!done && std::chrono::steady_clock::now() < stop) {
                    dbg.pump(jnext::dbg::PumpBudget{});
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                client.join();
                r.add_ok = refused && holder.has_endpoint && status == 0;
                r.extra += " refused=" + std::to_string(refused) + " port=" + std::to_string(holder.port) + " status=" + std::to_string(status) + " " + text;
                if (dbg.state().paused) r.never_paused = false;
            }
        }
        if (r.first_seen && emu->mmu().read(0x8210) == 0xA6) {
            r.second_seen = true;
            r.second_frame = frame;
            r.c2 = emu->mmu().read(0x8211);
            r.a2 = emu->mmu().read(0x8212);
            for (int i = 0; i < 16; ++i) r.got[static_cast<size_t>(i)] = emu->mmu().read(static_cast<uint16_t>(0x8220 + i));
        }
    }
    r.reset_requested = emu->take_hard_reset_request();
    if (through_server) {
        servers.reset();                       // before the Debugger it registered on
        sdcard::sd_locks::release(0);
        sdcard::sd_locks::set_enabled(false);
    }
    return r;
}

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
    // the locks of SDCOH-R01 live in the scratch directory, never in the user's state dir
#ifdef _WIN32
    _putenv_s("JNEXT_CONFIG_DIR", (scratch / "state").string().c_str());
#else
    setenv("JNEXT_CONFIG_DIR", (scratch / "state").string().c_str(), 1);
#endif
    std::error_code ec;
    const Bytes data = {'J', 'N', 'E', 'X', 'T', '-', 'C', 'O', 'H', 'E', 'R', 'E', 'N', 'C', 'E', '!'};

    {
        const Probe p = run_probe(false, scratch, data);
        std::string detail = "master=" + p.master + " copied=" + std::to_string(p.copied) + " nex=" + std::to_string(p.nex_on_card) +
                             " booted=" + std::to_string(p.booted) + " first@" + std::to_string(p.first_frame) + " c1=" + std::to_string(p.c1) +
                             " a1=" + std::to_string(p.a1) + " add=" + std::to_string(p.add_ok) + " " + p.err + " second@" + std::to_string(p.second_frame) +
                             " c2=" + std::to_string(p.c2) + " a2=" + std::to_string(p.a2);
        check("SDCOH-06",
              "real NextZXOS: a probe's first F_OPEN of C:/COHDATA.TXT fails (carry set); the manager's engine adds the file while the machine runs, never "
              "paused and with no reset requested; the probe's second F_OPEN succeeds (carry clear) and F_READ returns the host file's 16 bytes",
              p.copied && p.nex_on_card && p.first_seen && p.c1 == 1 && p.add_ok && p.second_seen && p.c2 == 0 && p.got == data && p.never_paused && !p.reset_requested,
              detail);
        fs::remove(scratch / "card.img", ec);
    }
    {
        const Probe p = run_probe(true, scratch, data);
        std::string detail = "copied=" + std::to_string(p.copied) + " nex=" + std::to_string(p.nex_on_card) + " booted=" + std::to_string(p.booted) +
                             " first@" + std::to_string(p.first_frame) + " c1=" + std::to_string(p.c1) + " add=" + std::to_string(p.add_ok) +
                             p.extra + " second@" + std::to_string(p.second_frame) + " c2=" + std::to_string(p.c2) + " a2=" + std::to_string(p.a2);
        check("SDCOH-R01",
              "real NextZXOS: the same probe, with the file added by a client of the running jnext (the lock file names its ZRCP server, "
              "the client sends sd-file-add over the socket, the server runs the manager's logic) while the machine runs: the first F_OPEN fails, "
              "the machine is never paused and no reset is requested, the second F_OPEN succeeds and F_READ returns the 16 bytes",
              p.copied && p.nex_on_card && p.first_seen && p.c1 == 1 && p.add_ok && p.second_seen && p.c2 == 0 && p.got == data && p.never_paused && !p.reset_requested,
              detail);
    }
    fs::remove_all(scratch, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
