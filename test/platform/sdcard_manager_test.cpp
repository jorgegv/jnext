// The SD card file manager's logic (GH #558 S2): src/platform/sdcard_manager.*, no toolkit.
//
// ORACLES. Nothing is derived by reading the implementation back.
//   - The owner's requirements (issue #558 and its decision comments): opening pauses the machine; a browse-only
//     visit resumes it and leaves a machine that was already paused paused; ANY card change ends in a hard
//     reset with no frame run on the old machine, and the warning before the first change must be accepted;
//     a card jnext cannot write, a write-protected card and a state that forbids a card change are shown
//     read-only; copies are all-or-nothing per item and stop at the first failure; the host side goes to the
//     trash, the card side is permanent; an existing destination asks, and a rename or folder never overwrites;
//     the remembered view reflects the emulator, which wins (owner amendment 2026-11-24): a remembered card that
//     is not mounted gives the mounted one, another card or a vanished directory gives the ROOT.
//   - The debugger backend's contract (debug/debugger.h, CTL-01/02/12 and SES-01): the pause and who owns it
//     (Debugger::state().pause_reason.by), `reset(Hard)` is synchronous and keeps the pause, an observer
//     client arms nothing. Read from the backend's own state and from a LoopDriver that counts the boots and
//     records the pause in force when each one runs.
//   - The card's contents are read back with FatFs and with extract_from_image (an independent reader), and the
//     image is compared by digest where "nothing was written" is the claim. Fixtures are built at run time with
//     FatFs f_mkfs plus byte pokes; never a NextZXOS image, in any form.
//   - The host side is the file system itself (what exists afterwards), with a recording trash hook that really
//     removes what it is given.
//
// Hostile inputs (each class has its rows): card names that arrive through open_name (SDMT-19, SDMT-06), host names
// and paths (SDMT-17, SDMT-18, SDMT-14), a selection mixing good and bad items (SDMT-05, SDMT-08, SDMT-10,
// SDMT-15), the remembered config values (SDMR-01, SDMR-04).
//
// Run: ./build/test/sdcard_manager_test

#include "platform/sdcard_manager.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_file_add.h"
#include "core/sdcard_file_extract.h"
#include "core/sha256.h"
#include "debug/debugger.h"
#include "peripheral/sd_card.h"
#include "platform/emulator_boot.h"
#include "../row_id.h"
#include "../scratch_dir.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdcard::FileAddStatus;
namespace ops = sdcard::cardops;
namespace sm = sdmanager;
using jnext::dbg::ClientId;
using jnext::dbg::ClientInfo;
using jnext::dbg::ClientKind;
using jnext::dbg::Debugger;
using jnext::dbg::PauseReason;
using jnext::dbg::Result;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

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

}  // namespace

#include "../sdcard_file_extract/sdfx_helpers.h"

namespace {

using Tree = std::map<std::string, Bytes>;
using St = FileAddStatus;

const Bytes kWotef = payload(1500, 1);
const Bytes kFileBin = payload(400, 4);
const Bytes kA = payload(700, 5);
const Bytes kB = payload(1, 6);
const Bytes kNewA = payload(900, 31);
const Bytes kNewB = payload(50, 32);
const Bytes kD1 = payload(200, 21), kD2 = payload(210, 22);
const Bytes kQ = payload(90, 12), kOk = payload(95, 13);
// a host name every host file system takes and the card name rules refuse (printable ASCII only)
const char* const kNonAscii = "caf\xC3\xA9.bin";
const Bytes kOne = payload(300, 41), kTwo = payload(310, 42), kX = payload(120, 43);

// The card every row copies: /GAMES/WOTEF.NEX, /GAMES/SUB/DEEP, /MYDIR/{A.BIN,B.BIN}, /FILE.BIN, /EMPTYD, /RO
// (a read-only file), /DATA/D1,D2 with /ODD (the short NAME and a long "NAME." beside it) and /ESC (a long name
// holding an ESC byte).
bool populate(FATFS& fsv, Pokes& pokes) {
    bool ok = f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef) &&
              f_mkdir("0:/GAMES/SUB") == FR_OK && f_mkdir("0:/GAMES/SUB/DEEP") == FR_OK &&
              f_mkdir("0:/MYDIR") == FR_OK && fat_write("0:/MYDIR/A.BIN", kA) && fat_write("0:/MYDIR/B.BIN", kB) &&
              f_mkdir("0:/EMPTYD") == FR_OK && fat_write("0:/FILE.BIN", kFileBin) && f_mkdir("0:/RO") == FR_OK &&
              fat_write("0:/RO/A_RO.BIN", kQ) && f_chmod("0:/RO/A_RO.BIN", AM_RDO, AM_RDO) == FR_OK &&
              fat_write("0:/RO/OK.BIN", kOk);
    ok = ok && f_mkdir("0:/DATA") == FR_OK && f_mkdir("0:/ODD") == FR_OK && f_mkdir("0:/ESC") == FR_OK &&
         fat_write("0:/DATA/D1.BIN", kD1) && fat_write("0:/DATA/D2.BIN", kD2);
    if (!ok) return false;
    const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
    hand_dir(fsv, dir_cluster("0:/ODD"), 2,
             {{u"", "NAME       ", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"NAME.", "NAME~1     ", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    hand_dir(fsv, dir_cluster("0:/ESC"), 2,
             {{u"\u001b[2Jevil", "EVIL~1     ", 0x20, c1, static_cast<uint32_t>(kD1.size())}}, pokes);
    return c1 != 0 && c2 != 0;
}

// A private copy of a master card, removed when the block ends.
struct Img {
    std::string p;
    explicit Img(const std::string& master) {
        p = tmp_name("c") + ".img";
        fs::copy_file(master, p, fs::copy_options::overwrite_existing);
    }
    ~Img() { std::error_code ec; fs::remove(p, ec); }
    Img(const Img&) = delete;
    Img& operator=(const Img&) = delete;
};

// A fresh, empty host directory.
fs::path fresh_dir(const std::string& stem) {
    const fs::path d = g_dir / (stem + "-" + std::to_string(++g_n));
    fs::create_directories(d);
    return d;
}


std::vector<ops::CardEntry> ls(const std::string& img, const std::string& dir) {
    std::vector<ops::CardEntry> e;
    std::string err;
    ops::list_dir(img, dir, e, err);
    return e;
}

bool on_card(const std::string& img, const std::string& path, bool* is_dir = nullptr) {
    bool ex = false, d = false;
    std::string err;
    const St st = ops::stat(img, path, ex, d, err);
    if (is_dir) *is_dir = d;
    return st == St::Ok && ex;
}

// What extract_from_image (an independent reader) returns for a card path.
Tree extracted(const std::string& img, const std::string& card_path) {
    const fs::path d = fresh_dir("x");
    std::string err;
    sdcard::extract_from_image(img, card_path, d.string(), err);
    Tree t = tree_of(d);
    std::error_code ec;
    fs::remove_all(d, ec);
    return t;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) { if (!s.empty()) s += ","; s += x; }
    return s;
}

std::string rep_str(const sm::TransferReport& r) {
    return "done=" + join(r.done) + " skipped=" + join(r.skipped) + " na=" + join(r.not_attempted) + " failed=" +
           r.failed + " err=" + r.error + (r.cancelled ? " CANCELLED" : "") + (r.card_write_attempted ? " WROTE" : "");
}

// Recording hooks: the trash really removes what it is given.
struct Hk {
    std::vector<std::string> log;
    std::vector<sm::OverwriteAnswer> answers;
    size_t ai = 0;
    bool gate_ok = true, trash_ok = true;
    std::function<void()> on_gate;
    std::function<void(const std::string&)> on_trash;
    size_t count(const std::string& prefix) const {
        size_t n = 0;
        for (const auto& l : log) if (l.rfind(prefix, 0) == 0) ++n;
        return n;
    }
    sm::TransferHooks hooks() {
        sm::TransferHooks h;
        h.ask_overwrite = [this](const std::string& dest, bool d) {
            log.push_back("ask:" + dest + (d ? ":dir" : ":file"));
            return ai < answers.size() ? answers[ai++] : sm::OverwriteAnswer::Cancel;
        };
        h.trash_host = [this](const std::string& path, std::string& err) {
            log.push_back("trash:" + path);
            if (on_trash) on_trash(path);
            if (!trash_ok) { err = "no trash here"; return false; }
            std::error_code ec;
            fs::remove_all(path, ec);
            return !ec;
        };
        h.before_card_write = [this]() {
            log.push_back("gate");
            if (on_gate) on_gate();
            return gate_ok;
        };
        return h;
    }
};

sm::TransferRequest host_req(const std::string& img, const std::string& dest, const std::vector<fs::path>& items,
                             bool move = false) {
    sm::TransferRequest r;
    r.from = sm::Side::Host;
    r.image = img;
    r.dest_dir = dest;
    r.move = move;
    for (const auto& p : items) r.items.push_back({p.string(), p.filename().string(), false});
    return r;
}

sm::TransferRequest card_req(const std::string& img, const std::string& dest, const std::vector<std::string>& items,
                             bool move = false) {
    sm::TransferRequest r;
    r.from = sm::Side::Card;
    r.image = img;
    r.dest_dir = dest;
    r.move = move;
    for (const auto& p : items) r.items.push_back({p, p, false});
    return r;
}

// ---- the machine, its backend, and a loop driver that counts boots ----

struct Rig {
    std::unique_ptr<Emulator> emu_owner = std::make_unique<Emulator>();
    Emulator& emu = *emu_owner;
    EmulatorConfig cfg;
    std::unique_ptr<Debugger> dbg;
    int boots = 0;
    bool boot_saw_paused = false;
    uint32_t boot_saw_frame = 0;
    sm::SdManagerSession ss;   // declared after dbg: destroyed (and detached) before it

    explicit Rig(bool with_driver = true) {
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
        if (with_driver) {
            jnext::dbg::LoopDriver d;
            d.cold_boot = [this]() {
                ++boots;
                boot_saw_paused = dbg->state().paused;   // the OLD machine's pause, read before it is replaced
                boot_saw_frame = dbg->state().frame;
                emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
                return true;
            };
            dbg->set_loop_driver(d);
        }
    }
    bool paused() const { return dbg->state().paused; }
    ClientId pause_owner() const { return dbg->state().pause_reason.by; }
};

}  // namespace

int main() {
    g_dir = make_scratch_dir("sdmgr");

    const std::string G = tmp_name("general") + ".img";
    const int gt = make_card(G, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate);
    const std::string F16 = tmp_name("fat16") + ".img";
    const int t16 = make_card(F16, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS&, Pokes&) {
        return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef);
    });
    const std::string sha_g = sha_file(G);
    const std::string sha_16 = sha_file(F16);

    // host files used to put things on a card
    const fs::path host = g_dir / "host";
    fs::create_directories(host / "tree" / "sub");
    write_host_file(host / "one.bin", kOne);
    write_host_file(host / "two.bin", kTwo);
    write_host_file(host / "A.BIN", kNewA);
    write_host_file(host / "B.BIN", kNewB);
    write_host_file(host / "tree" / "t1.bin", kA);
    write_host_file(host / "tree" / "sub" / "x.bin", kX);

    // ======================= the session (SDMS) =======================

    // SDMS-01
    {
        Rig r;
        r.emu.run_frame();
        std::string why;
        const bool was_running = !r.paused();
        const bool ok = r.ss.open(*r.dbg, why);
        check("SDMS-01", "open on a running machine pauses it, and the pause is the session's own client's",
              was_running && ok && r.paused() && r.pause_owner() == r.ss.client() &&
                  r.ss.client() != jnext::dbg::CLIENT_NONE && r.dbg->state().pause_reason.kind == PauseReason::Kind::User,
              why);
        r.ss.close(r.emu);
    }
    // SDMS-02
    {
        Rig r;
        r.emu.run_frame();
        std::string why;
        r.ss.open(*r.dbg, why);
        const auto res = r.ss.close(r.emu);
        check("SDMS-02", "close with no change resumes the machine, with zero cold boots and no reset request",
              res == sm::SdManagerSession::CloseResult::Resumed && !r.paused() && r.boots == 0 &&
                  !r.emu.take_hard_reset_request());
    }
    // SDMS-03
    {
        Rig r;
        const ClientId a = r.dbg->attach(ClientInfo{"A", ClientKind::Test}).value;
        r.dbg->pause(a);
        std::string why;
        const bool ok = r.ss.open(*r.dbg, why);
        const bool still_a = r.paused() && r.pause_owner() == a;
        const auto res = r.ss.close(r.emu);
        check("SDMS-03", "open when another client already paused: still paused and still owned by it; a no-change close leaves it so",
              ok && still_a && res == sm::SdManagerSession::CloseResult::StayedPaused && r.paused() &&
                  r.pause_owner() == a && r.boots == 0);
    }
    // SDMS-04
    {
        Rig r;
        r.emu.run_frame();
        std::string why;
        r.ss.open(*r.dbg, why);
        const bool gate = r.ss.before_card_write([] { return true; });
        const uint32_t frame_at_change = r.dbg->state().frame;
        const auto res = r.ss.close(r.emu);
        check("SDMS-04", "close after a change: exactly one cold boot, run while the machine was still paused and at the frame of the change, then running",
              gate && r.ss.changed() == false && res == sm::SdManagerSession::CloseResult::ResetAndResumed && r.boots == 1 &&
                  r.boot_saw_paused && r.boot_saw_frame == frame_at_change && !r.paused() && !r.emu.take_hard_reset_request(),
              "boots=" + std::to_string(r.boots));
    }
    // SDMS-05
    {
        Rig r;
        const ClientId a = r.dbg->attach(ClientInfo{"A", ClientKind::Test}).value;
        r.dbg->pause(a);
        std::string why;
        r.ss.open(*r.dbg, why);
        r.ss.before_card_write([] { return true; });
        const auto res = r.ss.close(r.emu);
        check("SDMS-05", "a change on an already-paused machine: one cold boot, and the machine is paused afterwards (CTL-12 rule 3)",
              res == sm::SdManagerSession::CloseResult::ResetStayedPaused && r.boots == 1 && r.boot_saw_paused && r.paused());
    }
    // SDMS-06
    {
        Rig r;
        std::string why;
        r.ss.open(*r.dbg, why);
        int asks = 0;
        const bool d1 = r.ss.confirm_first_change([&] { ++asks; return false; });
        const bool d2 = r.ss.confirm_first_change([&] { ++asks; return false; });
        const bool ok1 = r.ss.confirm_first_change([&] { ++asks; return true; });
        const bool ok2 = r.ss.confirm_first_change([&] { ++asks; return true; });
        const bool noask = asks == 3;
        r.ss.close(r.emu);
        r.ss.open(*r.dbg, why);
        const bool again = r.ss.confirm_first_change([&] { ++asks; return true; });
        check("SDMS-06", "confirm_first_change: declined allows nothing and asks again; accepted asks no more this cycle; a re-open asks again",
              !d1 && !d2 && ok1 && ok2 && noask && again && asks == 4, "asks=" + std::to_string(asks));
        r.ss.close(r.emu);
    }
    // SDMS-07
    {
        Rig r(false);
        std::string why;
        r.ss.open(*r.dbg, why);
        r.ss.before_card_write([] { return true; });
        const auto res = r.ss.close(r.emu);
        check("SDMS-07", "a refused reset (no LoopDriver): ResetDeferred, a deferred hard reset requested, the machine left paused",
              res == sm::SdManagerSession::CloseResult::ResetDeferred && r.emu.take_hard_reset_request() && r.paused() &&
                  r.boots == 0);
    }
    // SDMS-08
    {
        Rig r;
        std::string why;
        r.ss.open(*r.dbg, why);
        const ClientId b = r.dbg->attach(ClientInfo{"B", ClientKind::Test}).value;
        r.dbg->run(b);
        const bool ran = !r.paused();
        const bool re = r.ss.ensure_paused();
        // and the per-write gate does it too, before the write is noted
        r.dbg->run(b);
        const bool ran2 = !r.paused();
        const bool gated = r.ss.before_card_write([] { return true; });
        check("SDMS-08", "ensure_paused pauses again after another client's run(), and so does the per-write gate (before_card_write) before it notes the write",
              ran && re && r.paused() && r.pause_owner() == r.ss.client() && ran2 && gated && r.paused() && r.ss.changed());
        r.ss.close(r.emu);
    }
    // SDMS-09
    {
        Rig r;
        const bool a0 = r.dbg->armed(), t0 = r.dbg->attached();
        std::string why;
        r.ss.open(*r.dbg, why);
        const bool a1 = r.dbg->armed(), t1 = r.dbg->attached();
        r.ss.close(r.emu);
        check("SDMS-09", "the session's client is an observer: armed() and attached() are the same before, during and after",
              a0 == a1 && t0 == t1 && r.dbg->armed() == a0 && r.dbg->attached() == t0 && !t1);
    }
    // SDMS-10
    {
        Rig r;
        std::string why;
        const bool before_open = r.ss.confirm_first_change([] { return true; }) || r.ss.ensure_paused();
        const auto closed_unopened = r.ss.close(r.emu);
        r.ss.open(*r.dbg, why);
        const bool second_open = r.ss.open(*r.dbg, why);
        const bool declined = r.ss.before_card_write([] { return false; });
        const bool not_changed = !r.ss.changed();
        const bool accepted = r.ss.before_card_write([] { return true; });
        const auto res = r.ss.close(r.emu);
        check("SDMS-10", "outside an open session nothing is allowed and close is a no-op; open twice is refused; a declined write is not noted as a change",
              !before_open && closed_unopened == sm::SdManagerSession::CloseResult::NotOpen && !second_open && !declined &&
                  not_changed && accepted && res == sm::SdManagerSession::CloseResult::ResetAndResumed);
    }

    // ======================= slots =======================

    // SDMT-11 (also the slot rows)
    {
        // a normal writable card in slot 0, a FAT16 one in slot 1
        Img w(G), f(F16);
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        emu.sd_card().mount(w.p, false);
        emu.sd_card2().mount(f.p, false);
        auto s = sm::slots(emu);
        const bool s0 = s[0].mounted && s[0].writable && s[0].image == w.p && s[0].read_only_reason.empty();
        const bool s1 = s[1].mounted && !s[1].writable && s[1].read_only_reason.find("first FAT32 partition") != std::string::npos;
        // write-protected device
        emu.sd_card().unmount();
        emu.sd_card().mount(w.p, true);
        s = sm::slots(emu);
        const bool wp = s[0].mounted && !s[0].writable && s[0].read_only_reason.find("--sdcard-readonly") != std::string::npos;
        // an empty slot
        emu.sd_card2().unmount();
        s = sm::slots(emu);
        const bool empty = !s[1].mounted && !s[1].writable && s[1].image.empty() && !s[1].read_only_reason.empty();
        // the machine forbids a card change (--no-sdcard)
        Emulator emu2;
        EmulatorConfig cfg2;
        cfg2.type = MachineType::ZX48K;
        cfg2.no_sdcard = true;
        emu2.init(cfg2);
        emu2.sd_card().mount(w.p, false);   // the file is open read-write here too; nothing is written
        const auto s2 = sm::slots(emu2);
        const bool refusal = s2[0].mounted && !s2[0].writable && s2[0].read_only_reason.find("--no-sdcard") != std::string::npos;
        emu.sd_card().unmount();
        emu2.sd_card().unmount();
        // a transfer onto a not-writable card is refused before any hook runs, by the caller's reason and by its own probe
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const std::string before = sha_file(f.p);
        sm::TransferRequest rq = host_req(f.p, "/", {host / "one.bin"});
        const sm::TransferReport own = sm::run_transfer(rq, h);
        sm::TransferRequest rq2 = host_req(w.p, "/", {host / "one.bin"});
        rq2.card_read_only_reason = "the card is write-protected in this session (--sdcard-readonly)";
        const sm::TransferReport given = sm::run_transfer(rq2, h);
        sm::TransferRequest rq3 = card_req(w.p, "/", {"/FILE.BIN"}, true);
        rq3.card_read_only_reason = "read-only";
        const sm::TransferReport mv = sm::run_transfer(rq3, h);
        check("SDMT-11", "slots() marks a FAT16 card, a write-protected device, an empty slot and a --no-sdcard session read-only with the reason; a write onto such a card (or a move off it) is refused before any hook runs",
              s0 && s1 && wp && empty && refusal && hk.log.empty() && !own.error.empty() && own.done.empty() &&
                  sha_file(f.p) == before && !given.error.empty() && given.done.empty() && !mv.error.empty() &&
                  sha_file(w.p) == sha_g,
              rep_str(own) + " | " + rep_str(given));
    }

    // ======================= transfers: host to card =======================

    // SDMT-01
    {
        Img c(G);
        Hk hk;
        std::vector<std::string> shas;
        hk.on_gate = [&] { shas.push_back(sha_file(c.p)); };
        sm::TransferHooks h = hk.hooks();
        const auto rq = host_req(c.p, "/MYDIR", {host / "one.bin", host / "two.bin", host / "tree"});
        const sm::TransferReport r = sm::run_transfer(rq, h);
        const bool first_before_any_write = shas.size() == 3 && shas[0] == sha_g && shas[1] != sha_g;
        const bool content = extracted(c.p, "/MYDIR/one.bin") == Tree{{"one.bin", kOne}} &&
                             extracted(c.p, "/MYDIR/two.bin") == Tree{{"two.bin", kTwo}} &&
                             extracted(c.p, "/MYDIR/tree") ==
                                 Tree{{"tree/", {}}, {"tree/sub/", {}}, {"tree/t1.bin", kA}, {"tree/sub/x.bin", kX}} &&
                             on_card(c.p, "/MYDIR/A.BIN") && on_card(c.p, "/MYDIR/B.BIN");
        check("SDMT-01", "host to card, two files and a folder: all on the card with their bytes, done lists 3, the gate runs once per item and each before that item's write",
              r.done.size() == 3 && r.failed.empty() && r.error.empty() && !r.cancelled && r.card_write_attempted &&
                  first_before_any_write && content && hk.count("gate") == 3 && hk.count("ask") == 0,
              rep_str(r));
    }
    // SDMT-02
    {
        Img c(G);
        Hk hk;
        hk.gate_ok = false;
        sm::TransferHooks h = hk.hooks();
        const auto rq = host_req(c.p, "/MYDIR", {host / "one.bin", host / "two.bin", host / "tree"});
        const sm::TransferReport r = sm::run_transfer(rq, h);
        check("SDMT-02", "the gate declined: the image is byte-identical, the run is cancelled, no card write was attempted, every item is not attempted",
              sha_file(c.p) == sha_g && r.cancelled && !r.card_write_attempted && r.done.empty() &&
                  r.not_attempted.size() == 3 && hk.count("gate") == 1,
              rep_str(r));
    }
    // SDMT-03
    {
        // Skip: A.BIN exists on the card; the rest is copied
        Img c(G);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Skip};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r1 = sm::run_transfer(host_req(c.p, "/MYDIR", {host / "A.BIN", host / "one.bin"}), h);
        const bool skip_ok = r1.skipped.size() == 1 && r1.skipped[0] == "A.BIN" && r1.done.size() == 1 &&
                             extracted(c.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kA}} &&
                             extracted(c.p, "/MYDIR/one.bin") == Tree{{"one.bin", kOne}} && hk.count("ask:/MYDIR/A.BIN:file") == 1;
        // Replace
        Img c2(G);
        Hk hk2;
        hk2.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferHooks h2 = hk2.hooks();
        const sm::TransferReport r2 = sm::run_transfer(host_req(c2.p, "/MYDIR", {host / "A.BIN"}), h2);
        const bool rep_ok = r2.done.size() == 1 && extracted(c2.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kNewA}};
        check("SDMT-03", "an existing card file asks: Skip leaves it and copies the rest; Replace replaces it",
              skip_ok && rep_ok, rep_str(r1) + " | " + rep_str(r2));
    }
    // SDMT-12
    {
        Img c(G);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::ReplaceAll};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r1 = sm::run_transfer(host_req(c.p, "/MYDIR", {host / "A.BIN", host / "B.BIN"}), h);
        const bool ra = r1.done.size() == 2 && hk.count("ask") == 1 && extracted(c.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kNewA}} &&
                        extracted(c.p, "/MYDIR/B.BIN") == Tree{{"B.BIN", kNewB}};
        Img c2(G);
        Hk hk2;
        hk2.answers = {sm::OverwriteAnswer::SkipAll};
        sm::TransferHooks h2 = hk2.hooks();
        const sm::TransferReport r2 = sm::run_transfer(host_req(c2.p, "/MYDIR", {host / "A.BIN", host / "B.BIN"}), h2);
        const bool sa = r2.done.empty() && r2.skipped.size() == 2 && hk2.count("ask") == 1 && sha_file(c2.p) == sha_g &&
                        hk2.count("gate") == 0;
        check("SDMT-12", "ReplaceAll and SkipAll ask once for the whole run; SkipAll with everything skipped writes nothing and never reaches the gate",
              ra && sa, rep_str(r1) + " | " + rep_str(r2));
    }
    // SDMT-13
    {
        Img c(G);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Cancel};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/MYDIR", {host / "one.bin", host / "A.BIN", host / "two.bin"}), h);
        check("SDMT-13", "Cancel at a question stops the run: what was done stays, the asked item and the rest are not attempted, nothing replaced",
              r.cancelled && r.done.size() == 1 && r.done[0] == "one.bin" && r.not_attempted.size() == 2 &&
                  r.not_attempted[0] == "A.BIN" && r.not_attempted[1] == "two.bin" && !on_card(c.p, "/MYDIR/two.bin") &&
                  extracted(c.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kA}},
              rep_str(r));
    }
    // SDMT-04
    {
        // a host folder MYDIR (A.BIN new, NEW.BIN) onto the card's existing /MYDIR (A.BIN, B.BIN): merged
        const fs::path hd = fresh_dir("mydir-parent");
        fs::create_directories(hd / "MYDIR");
        write_host_file(hd / "MYDIR" / "A.BIN", kNewA);
        write_host_file(hd / "MYDIR" / "NEW.BIN", kX);
        Img c(G);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/", {hd / "MYDIR"}), h);
        check("SDMT-04", "a host folder onto an existing card folder, Replace: asked as a folder, merged (clashes replaced, other card files kept, new ones added)",
              r.done.size() == 1 && hk.count("ask:/MYDIR:dir") == 1 && extracted(c.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kNewA}} &&
                  extracted(c.p, "/MYDIR/B.BIN") == Tree{{"B.BIN", kB}} && extracted(c.p, "/MYDIR/NEW.BIN") == Tree{{"NEW.BIN", kX}},
              rep_str(r));
    }
    // SDMT-05
    {
        const fs::path hd = fresh_dir("big");
        write_host_file(hd / "a_ok.bin", kOne);
        {
            std::ofstream f(hd / "b_big.bin", std::ios::binary);
        }
        std::error_code ec;
        fs::resize_file(hd / "b_big.bin", 40u * 1024u * 1024u, ec);   // sparse: more than the card's free space
        write_host_file(hd / "c_ok.bin", kTwo);
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/MYDIR", {hd / "a_ok.bin", hd / "b_big.bin", hd / "c_ok.bin"}), h);
        check("SDMT-05", "partial failure: item 1 is on the card, item 2 (larger than the free space) is absent, item 3 is not attempted; the report names item 2",
              !ec && r.done.size() == 1 && r.failed == "b_big.bin" && !r.error.empty() && r.not_attempted.size() == 1 &&
                  r.not_attempted[0] == "c_ok.bin" && r.card_write_attempted && on_card(c.p, "/MYDIR/a_ok.bin") &&
                  !on_card(c.p, "/MYDIR/b_big.bin") && !on_card(c.p, "/MYDIR/c_ok.bin"),
              rep_str(r));
    }
    // SDMT-08
    {
        // move host to card: item 1 fine, item 2 refused by its name (a non-ASCII byte: the card name rules refuse it), item 3 never runs
        const fs::path hd = fresh_dir("mv");
        write_host_file(hd / "m1.bin", kOne);
        write_host_file(hd / kNonAscii, kTwo);
        write_host_file(hd / "m3.bin", kX);
        Img c(G);
        Hk hk;
        bool card_had_it_at_trash = false;
        hk.on_trash = [&](const std::string& p) {
            if (p == (hd / "m1.bin").string())
                card_had_it_at_trash = extracted(c.p, "/MYDIR/m1.bin") == Tree{{"m1.bin", kOne}};
        };
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/MYDIR", {hd / "m1.bin", hd / kNonAscii, hd / "m3.bin"}, true), h);
        check("SDMT-08", "move host to card: a source goes to the trash only after its add succeeded; the item that failed keeps its source; later items are untouched",
              r.done.size() == 1 && r.failed == kNonAscii && hk.count("trash") == 1 && card_had_it_at_trash &&
                  !fs::exists(hd / "m1.bin") && fs::exists(hd / kNonAscii) && fs::exists(hd / "m3.bin") && !on_card(c.p, "/MYDIR/m3.bin"),
              rep_str(r));
    }

    // ======================= transfers: card to host =======================

    // SDMT-06
    {
        // the hostile one is listed from /ESC and addressed by ITS open_name, as the contract says
        const auto esc = ls(G, "/ESC");
        const std::string esc_path = esc.size() == 1 ? "/ESC/" + esc[0].open_name : std::string();
        const fs::path dd = fresh_dir("dst");
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(card_req(G, dd.string(), {"/GAMES/WOTEF.NEX", "/MYDIR", esc_path}), h);
        const Tree t = tree_of(dd);
        const bool names_ok = t.count("WOTEF.NEX") && t.at("WOTEF.NEX") == kWotef && t.count("MYDIR/A.BIN") && t.at("MYDIR/A.BIN") == kA &&
                              t.count("%1B[2Jevil") && t.at("%1B[2Jevil") == kD1;
        check("SDMT-06", "card to host, three items one with a control byte in its name: created under the escaped host name with the right bytes; the gate is never called; the image is byte-identical",
              esc.size() == 1 && r.done.size() == 3 && r.failed.empty() && names_ok && hk.count("gate") == 0 && !r.card_write_attempted &&
                  sha_file(G) == sha_g,
              rep_str(r));
    }
    // SDMT-07
    {
        const fs::path dd = fresh_dir("rep");
        write_host_file(dd / "WOTEF.NEX", kX);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(card_req(G, dd.string(), {"/GAMES/WOTEF.NEX"}), h);
        const bool first = r.done.size() == 1 && hk.log.size() == 2 && hk.log[1] == "trash:" + (dd / "WOTEF.NEX").string() &&
                           read_host_file(dd / "WOTEF.NEX") == kWotef;
        // a trash failure fails the item and leaves the host entry as it was
        const fs::path dd2 = fresh_dir("rep");
        write_host_file(dd2 / "WOTEF.NEX", kX);
        Hk hk2;
        hk2.answers = {sm::OverwriteAnswer::Replace};
        hk2.trash_ok = false;
        sm::TransferHooks h2 = hk2.hooks();
        const sm::TransferReport r2 = sm::run_transfer(card_req(G, dd2.string(), {"/GAMES/WOTEF.NEX", "/FILE.BIN"}), h2);
        const bool second = r2.done.empty() && r2.failed == "/GAMES/WOTEF.NEX" && read_host_file(dd2 / "WOTEF.NEX") == kX &&
                            r2.not_attempted.size() == 1 && !fs::exists(dd2 / "FILE.BIN");
        // the existing entry is found under the ESCAPED host name the copy would create, not the raw card name
        const auto esc = ls(G, "/ESC");
        const fs::path dd3 = fresh_dir("rep");
        write_host_file(dd3 / "%1B[2Jevil", kX);
        Hk hk3;
        hk3.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferHooks h3 = hk3.hooks();
        const sm::TransferReport r3 = sm::run_transfer(
            card_req(G, dd3.string(), {esc.size() == 1 ? "/ESC/" + esc[0].open_name : std::string()}), h3);
        const bool third = esc.size() == 1 && r3.done.size() == 1 && hk3.log.size() == 2 &&
                           hk3.log[0] == "ask:" + (dd3 / "%1B[2Jevil").string() + ":file" &&
                           hk3.log[1] == "trash:" + (dd3 / "%1B[2Jevil").string() && read_host_file(dd3 / "%1B[2Jevil") == kD1;
        check("SDMT-07", "card to host over an existing host entry, Replace: the existing one (found under the escaped host name the copy creates) goes to the trash first, exactly that path, then the copy; a failed trash fails the item and leaves it",
              first && second && third, rep_str(r) + " | " + rep_str(r2) + " | " + rep_str(r3));
    }
    // SDMT-09
    {
        // move card to host: the host has the file when the gate (before the removal) is asked; the card source is then gone
        Img c(G);
        const fs::path dd = fresh_dir("mvc");
        Hk hk;
        bool host_had_it = false, card_had_it = false;
        hk.on_gate = [&] {
            host_had_it = read_host_file(dd / "WOTEF.NEX") == kWotef;
            card_had_it = on_card(c.p, "/GAMES/WOTEF.NEX");
        };
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(card_req(c.p, dd.string(), {"/GAMES/WOTEF.NEX"}, true), h);
        const bool moved = r.done.size() == 1 && host_had_it && card_had_it && !on_card(c.p, "/GAMES/WOTEF.NEX") &&
                           r.card_write_attempted && hk.count("gate") == 1;
        // a failed copy (the existing host entry cannot be trashed) leaves the card source and never reaches the gate
        Img c2(G);
        const fs::path dd2 = fresh_dir("mvc");
        write_host_file(dd2 / "WOTEF.NEX", kX);
        Hk hk2;
        hk2.answers = {sm::OverwriteAnswer::Replace};
        hk2.trash_ok = false;
        sm::TransferHooks h2 = hk2.hooks();
        const sm::TransferReport r2 = sm::run_transfer(card_req(c2.p, dd2.string(), {"/GAMES/WOTEF.NEX"}, true), h2);
        // a destination that is not a directory
        Img c3(G);
        write_host_file(g_dir / "notadir", kX);
        Hk hk3;
        sm::TransferHooks h3 = hk3.hooks();
        const sm::TransferReport r3 = sm::run_transfer(card_req(c3.p, (g_dir / "notadir").string(), {"/GAMES/WOTEF.NEX"}, true), h3);
        // a read-only card entry: copied, then the removal refuses and the item fails with the card original kept
        Img c4(G);
        const fs::path dd4 = fresh_dir("mvc");
        Hk hk4;
        sm::TransferHooks h4 = hk4.hooks();
        const sm::TransferReport r4 = sm::run_transfer(card_req(c4.p, dd4.string(), {"/RO"}, true), h4);
        check("SDMT-09", "move card to host: the card source is removed after its extract (behind the gate), and kept when the copy failed or the removal refused (a read-only entry)",
              moved && r2.done.empty() && !r2.failed.empty() && on_card(c2.p, "/GAMES/WOTEF.NEX") && hk2.count("gate") == 0 &&
                  sha_file(c2.p) == sha_g && !r3.failed.empty() && on_card(c3.p, "/GAMES/WOTEF.NEX") && hk3.count("gate") == 0 &&
                  !r4.failed.empty() && r4.error.find("A_RO.BIN") != std::string::npos && on_card(c4.p, "/RO/A_RO.BIN") &&
                  read_host_file(dd4 / "RO" / "OK.BIN") == kOk,
              rep_str(r) + " | " + rep_str(r2) + " | " + rep_str(r4));
    }

    // ======================= delete and New Folder =======================

    // SDMT-10
    {
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::delete_items(sm::Side::Card, c.p, {"/MYDIR", "/EMPTYD", "/RO", "/FILE.BIN"}, h);
        const bool card_ok = r.done.size() == 2 && r.failed == "/RO" && r.error.find("A_RO.BIN") != std::string::npos &&
                             r.not_attempted.size() == 1 && r.not_attempted[0] == "/FILE.BIN" && !on_card(c.p, "/MYDIR") &&
                             !on_card(c.p, "/EMPTYD") && on_card(c.p, "/RO/OK.BIN") && on_card(c.p, "/FILE.BIN") &&
                             hk.count("gate") == 3 && r.card_write_attempted;
        // host: the trash per path, stop at the first failure
        const fs::path hd = fresh_dir("del");
        write_host_file(hd / "d1", kOne);
        write_host_file(hd / "d2", kTwo);
        write_host_file(hd / "d3", kX);
        Hk hk2;
        sm::TransferHooks h2 = hk2.hooks();
        hk2.on_trash = [&](const std::string& p) { if (p == (hd / "d2").string()) hk2.trash_ok = false; };
        const sm::TransferReport r2 = sm::delete_items(sm::Side::Host, G, {(hd / "d1").string(), (hd / "d2").string(), (hd / "d3").string()}, h2);
        const bool host_ok = r2.done.size() == 1 && r2.failed == (hd / "d2").string() && r2.not_attempted.size() == 1 &&
                             !fs::exists(hd / "d1") && fs::exists(hd / "d2") && fs::exists(hd / "d3") && hk2.count("trash") == 2 &&
                             hk2.count("gate") == 0 && !r2.card_write_attempted;
        check("SDMT-10", "delete_items: the card removes each path permanently through the gate and stops at the first refusal naming the read-only entry; the host sends each to the trash and stops at a failure",
              card_ok && host_ok, rep_str(r) + " | " + rep_str(r2));
    }
    // SDMT-14
    {
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport ok = sm::make_folder(sm::Side::Card, c.p, "/MYDIR", "NEWDIR", h);
        bool d = false;
        const bool made = ok.done.size() == 1 && on_card(c.p, "/MYDIR/NEWDIR", &d) && d && hk.count("gate") == 1;
        // refusals made before the gate: an existing name in any case, an invalid name, a missing parent
        const size_t gates = hk.count("gate");
        const std::string sha = sha_file(c.p);
        const sm::TransferReport e1 = sm::make_folder(sm::Side::Card, c.p, "/MYDIR", "newdir", h);
        const sm::TransferReport e2 = sm::make_folder(sm::Side::Card, c.p, "/MYDIR", "a:b", h);
        const sm::TransferReport e3 = sm::make_folder(sm::Side::Card, c.p, "/MYDIR", "x.", h);
        const sm::TransferReport e4 = sm::make_folder(sm::Side::Card, c.p, "/NOPE", "x", h);
        const sm::TransferReport e5 = sm::make_folder(sm::Side::Card, c.p, "/MYDIR", "a/b", h);
        const sm::TransferReport e6 = sm::make_folder(sm::Side::Card, c.p, "/MYDIR", "..", h);
        const bool refused = !e1.failed.empty() && !e2.failed.empty() && !e3.failed.empty() && !e4.failed.empty() &&
                             !e5.failed.empty() && !e6.failed.empty() && hk.count("gate") == gates && sha_file(c.p) == sha &&
                             !e1.card_write_attempted;
        // a read-only card
        Hk hk2;
        sm::TransferHooks h2 = hk2.hooks();
        const sm::TransferReport ro = sm::make_folder(sm::Side::Card, F16, "/", "NEWDIR", h2);
        // host side: a plain name works, hostile names and an existing one do not
        const fs::path hd = fresh_dir("mk");
        const sm::TransferReport hok = sm::make_folder(sm::Side::Host, "", hd.string(), "New Folder", h2);
        const bool host_made = hok.done.size() == 1 && fs::is_directory(hd / "New Folder");
        bool host_bad = true;
        for (const char* bad : {"", ".", "..", "../up", "a/b", "a\\b", "x\ny", "New Folder"}) {
            const sm::TransferReport e = sm::make_folder(sm::Side::Host, "", hd.string(), bad, h2);
            host_bad = host_bad && !e.error.empty() && e.done.empty();
        }
        std::error_code ec;
        const bool nothing_escaped = !fs::exists(hd.parent_path() / "up") && !fs::exists(hd / "up") &&
                                     std::distance(fs::directory_iterator(hd, ec), fs::directory_iterator()) == 1;
        const sm::TransferReport nodir = sm::make_folder(sm::Side::Host, "", (hd / "nope").string(), "x", h2);
        check("SDMT-14", "New Folder: on the card through the gate (existing, invalid and missing-parent names refused before it, a read-only card refused); on the host a plain single name only, never an existing one",
              made && refused && !ro.failed.empty() && hk2.count("gate") == 0 && sha_file(F16) == sha_16 && host_made && host_bad &&
                  nothing_escaped && !nodir.failed.empty(),
              "made=" + std::to_string(made) + " refused=" + std::to_string(refused) + " ro=" + std::to_string(!ro.failed.empty()) +
                  " host_made=" + std::to_string(host_made) + " host_bad=" + std::to_string(host_bad) +
                  " esc=" + std::to_string(nothing_escaped) + " nodir=" + std::to_string(!nodir.failed.empty()) + " | " +
                  rep_str(ok) + " | " + rep_str(e6));
    }

    // ======================= hostile selections and paths =======================

    // SDMT-15
    {
        // a repeated item and an item inside another selected one are run once, not twice
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport del = sm::delete_items(sm::Side::Card, c.p, {"/MYDIR", "/mydir/A.BIN", "/MYDIR/", "/FILE.BIN"}, h);
        const bool dd = del.done.size() == 2 && del.skipped.size() == 2 && del.failed.empty() && !on_card(c.p, "/MYDIR") &&
                        !on_card(c.p, "/FILE.BIN") && hk.count("gate") == 2;
        Img c2(G);
        Hk hk2;
        sm::TransferHooks h2 = hk2.hooks();
        const sm::TransferReport cp = sm::run_transfer(host_req(c2.p, "/EMPTYD", {host / "tree", host / "tree" / "t1.bin", host / "tree"}), h2);
        const bool cc = cp.done.size() == 1 && cp.skipped.size() == 2 && cp.failed.empty() && hk2.count("ask") == 0 &&
                        extracted(c2.p, "/EMPTYD/tree/t1.bin") == Tree{{"t1.bin", kA}};
        const fs::path hd = fresh_dir("dup");
        write_host_file(hd / "f", kOne);
        Hk hk3;
        sm::TransferHooks h3 = hk3.hooks();
        const sm::TransferReport hdel = sm::delete_items(sm::Side::Host, G, {hd.string(), (hd / "f").string()}, h3);
        check("SDMT-15", "a selection that repeats an item or holds an item inside another selected one runs each once (the nested or repeated one is skipped, not failed), on the card, onto it, and on the host",
              dd && cc && hdel.done.size() == 1 && hdel.skipped.size() == 1 && hk3.count("trash") == 1,
              rep_str(del) + " | " + rep_str(cp));
    }
    // SDMT-16
    {
        // a destination folder that is gone (or is a file) is refused up front: the engine would create it, and nothing may be written
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport gone = sm::run_transfer(host_req(c.p, "/GONE/DEEP", {host / "one.bin"}), h);
        const sm::TransferReport file = sm::run_transfer(host_req(c.p, "/FILE.BIN", {host / "one.bin"}), h);
        check("SDMT-16", "a card destination folder that no longer exists, or is a file, is refused before any hook runs and nothing is created",
              !gone.error.empty() && gone.done.empty() && !file.error.empty() && file.done.empty() && hk.log.empty() &&
                  sha_file(c.p) == sha_g && !on_card(c.p, "/GONE"),
              rep_str(gone) + " | " + rep_str(file));
    }
    // SDMT-17
    {
        const fs::path hd = fresh_dir("hn");
        write_host_file(hd / kNonAscii, kTwo);       // a name the card rules refuse
        write_host_file(hd / "good.bin", kX);
        fs::create_directories(hd / "FILE.BIN");     // a host FOLDER named like a card FILE
        bool dots_ok = true;
#ifndef _WIN32
        write_host_file(hd / "trail.", kOne);        // a name FAT strips (Windows would strip it on the host too)
        write_host_file(hd / "q?mark.bin", kTwo);    // a character FAT refuses (not a Windows host name)
#endif
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
#ifndef _WIN32
        const sm::TransferReport a = sm::run_transfer(host_req(c.p, "/MYDIR", {hd / "trail."}), h);
        const sm::TransferReport b = sm::run_transfer(host_req(c.p, "/MYDIR", {hd / "q?mark.bin"}), h);
        dots_ok = a.failed == "trail." && b.failed == "q?mark.bin" && !on_card(c.p, "/MYDIR/trail") && !on_card(c.p, "/MYDIR/q");
#endif
        const sm::TransferReport n = sm::run_transfer(host_req(c.p, "/MYDIR", {hd / kNonAscii}), h);
        const sm::TransferReport t = sm::run_transfer(host_req(c.p, "/", {hd / "FILE.BIN"}), h);
        const sm::TransferReport gone = sm::run_transfer(host_req(c.p, "/MYDIR", {hd / "missing.bin"}), h);
        const sm::TransferReport mixed = sm::run_transfer(host_req(c.p, "/MYDIR", {hd / "good.bin", hd / kNonAscii, hd / "good.bin"}), h);
        check("SDMT-17", "host names the card rules refuse (a trailing dot, a refused character, a non-ASCII byte), a type clash and a vanished source each fail their item with nothing written for it, and without asking; a selection mixing good and bad stops at the bad one",
              dots_ok && n.failed == kNonAscii && t.failed == "FILE.BIN" && gone.failed == "missing.bin" && hk.count("ask") == 0 &&
                  mixed.done.size() == 1 && mixed.failed == kNonAscii && mixed.skipped.size() == 1 &&
                  extracted(c.p, "/MYDIR/good.bin") == Tree{{"good.bin", kX}},
              rep_str(n) + " | " + rep_str(t) + " | " + rep_str(mixed));
    }
    // SDMT-18
    {
        // the mounted image must never be copied onto itself, replaced or trashed: it, and any folder holding it
        const fs::path cards = fresh_dir("cards");
        const std::string img = (cards / "mounted.img").string();
        fs::copy_file(G, img, fs::copy_options::overwrite_existing);
        const std::string sha = sha_file(img);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport a = sm::run_transfer(host_req(img, "/MYDIR", {fs::path(img)}), h);
        const sm::TransferReport b = sm::run_transfer(host_req(img, "/MYDIR", {cards}), h);
        const sm::TransferReport mv = sm::run_transfer(host_req(img, "/MYDIR", {cards}, true), h);
        const sm::TransferReport d1 = sm::delete_items(sm::Side::Host, img, {img}, h);
        const sm::TransferReport d2 = sm::delete_items(sm::Side::Host, img, {cards.string()}, h);
        // a relative spelling of the same file is the same file
        const sm::TransferReport d3 = sm::delete_items(sm::Side::Host, img, {(cards / ".." / cards.filename() / "mounted.img").string()}, h);
        // card to host: replacing the image itself is refused
        const fs::path same_name_dir = cards;
        const std::string sha_after = sha_file(img);
        check("SDMT-18", "the mounted image is refused as a copy source, a move source and a delete target, by identity (it, a folder holding it, a relative spelling); it is never trashed or changed",
              !a.failed.empty() && !b.failed.empty() && !mv.failed.empty() && !d1.failed.empty() && !d2.failed.empty() &&
                  !d3.failed.empty() && hk.count("trash") == 0 && hk.count("gate") == 0 && fs::exists(img) && sha_after == sha,
              rep_str(a) + " | " + rep_str(d2));
        (void)same_name_dir;
    }
    // SDMT-19
    {
        // card names arrive through open_name only: the typed long name of an entry whose spelling FatFs would resolve to another entry copies nothing
        const auto odd = ls(G, "/ODD");
        std::string alias, plain;
        for (const auto& e : odd) {
            if (e.name == "NAME.") alias = e.open_name;
            if (e.name == "NAME") plain = e.open_name;
        }
        const fs::path dd = fresh_dir("odd");
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport typed = sm::run_transfer(card_req(G, dd.string(), {"/ODD/NAME."}), h);
        const bool typed_refused = !typed.failed.empty() && typed.done.empty() && tree_of(dd).empty();
        const sm::TransferReport good = sm::run_transfer(card_req(G, dd.string(), {"/ODD/" + alias}), h);
        const Tree t = tree_of(dd);
        // the host name is the one extract creates for THAT entry (trailing dot escaped), with that entry's bytes
        bool bytes_ok = false;
        for (const auto& kv : t) if (kv.first.rfind("NAME", 0) == 0 && kv.second == kD2) bytes_ok = true;
        // stale and traversal spellings
        const sm::TransferReport up = sm::run_transfer(card_req(G, dd.string(), {"/GAMES/../FILE.BIN"}), h);
        const sm::TransferReport back = sm::run_transfer(card_req(G, dd.string(), {"/GAMES\\WOTEF.NEX"}), h);
        const sm::TransferReport root = sm::run_transfer(card_req(G, dd.string(), {"/"}), h);
        const sm::TransferReport missing = sm::run_transfer(card_req(G, dd.string(), {"/GAMES/NOPE.BIN"}), h);
        // a delete by a typed long name is refused the same way and leaves both entries
        Img c(G);
        const sm::TransferReport del = sm::delete_items(sm::Side::Card, c.p, {"/ODD/NAME."}, h);
        const bool del_refused = !del.failed.empty() && ls(c.p, "/ODD").size() == 2;
        check("SDMT-19", "a card item named by its typed long name (trailing dot), a traversal, a backslash, the root or a missing name copies and deletes NOTHING and never reaches the gate; the one addressed by its open_name copies that entry's bytes",
              plain == "NAME" && !alias.empty() && alias != "NAME." && typed_refused && good.done.size() == 1 && bytes_ok &&
                  !up.failed.empty() && !back.failed.empty() && !root.failed.empty() && !missing.failed.empty() && del_refused &&
                  hk.count("gate") == 0,
              "alias=" + alias + " tr=" + std::to_string(typed_refused) + " bytes=" + std::to_string(bytes_ok) + " up=" + rep_str(up) +
                  " back=" + rep_str(back) + " root=" + rep_str(root) + " miss=" + rep_str(missing) + " delr=" + std::to_string(del_refused) +
                  " gates=" + std::to_string(hk.count("gate")) + " tree=" + std::to_string(t.size()));
    }

    // ======================= what the window opens on =======================

    // SDMR-01
    {
        const fs::path base = fresh_dir("rem");
        fs::create_directories(base / "a" / "b" / "c");
        const fs::path fb = fresh_dir("fallback"), home = fresh_dir("home");
        std::array<sm::SlotInfo, 2> none{};
        auto host_of = [&](const std::string& rem, const std::string& fallback, const std::string& h) {
            sm::Remembered r;
            r.host_dir = rem;
            return sm::resolve_remembered(r, none, fallback, h).host_dir;
        };
        const std::string exist = host_of((base / "a" / "b" / "c").string(), fb.string(), home.string());
        fs::remove_all(base / "a" / "b" / "c");
        const std::string leaf_gone = host_of((base / "a" / "b" / "c").string(), fb.string(), home.string());
        fs::remove_all(base / "a" / "b");
        const std::string two_gone = host_of((base / "a" / "b" / "c").string(), fb.string(), home.string());
        const std::string empty = host_of("", fb.string(), home.string());
        // ".." resolves lexically: base/nope/x does not exist, so the nearest existing parent is base
        const std::string nothing = host_of((base / "a" / "b").string() + "/../../nope/x", fb.string(), home.string());
        const std::string fb_gone = host_of("", (g_dir / "no-fb").string(), home.string());
        const std::string all_gone = host_of("", (g_dir / "no-fb").string(), (g_dir / "no-home").string());
        // a tampered value: relative, with a NUL, a file, traversal that resolves lexically
        const std::string rel = host_of("relative/dir", fb.string(), home.string());
        const std::string nul = host_of(std::string("/tmp\0evil", 9), fb.string(), home.string());
        write_host_file(base / "afile", kOne);
        const std::string file = host_of((base / "afile" / "inner").string(), fb.string(), home.string());
        const std::string dots = host_of((base / "a" / "x" / ".." / ".." / "a").string(), fb.string(), home.string());
        const std::string slash = host_of((base / "a").string() + "/", fb.string(), home.string());
        bool unc_ok = true;
#ifdef _WIN32
        unc_ok = host_of("\\\\nonexistent-server\\share\\x", fb.string(), home.string()) == fb.string() &&
                 host_of("\\\\?\\C:\\Windows", fb.string(), home.string()) == fb.string();
#endif
        check("SDMR-01", "host directory: an existing one gives itself; a removed leaf or two give the nearest existing parent; none left gives the Load directory, then home, then \"\"; a relative, NUL, file-inside or UNC value is not trusted",
              exist == (base / "a" / "b" / "c").string() && leaf_gone == (base / "a" / "b").string() &&
                  two_gone == (base / "a").string() && empty == fb.string() && nothing == base.string() &&
                  fb_gone == home.string() && all_gone.empty() && rel == fb.string() && nul == fb.string() &&
                  file == base.string() && dots == (base / "a").string() && slash == (base / "a").string() && unc_ok,
              exist + "|" + leaf_gone + "|" + two_gone + "|" + empty + "|" + nothing + "|" + rel + "|" + nul + "|" + file + "|" + dots + "|" + slash);
    }
    // SDMR-02
    {
        Img c1(G), c2(F16);
        std::array<sm::SlotInfo, 2> s{};
        s[0] = {0, true, c1.p, true, ""};
        s[1] = {1, true, c2.p, false, "x"};
        sm::Remembered r;
        r.card_image = c2.p;
        const int in_slot1 = sm::resolve_remembered(r, s, "", "").slot;
        // another spelling of the same file is the same card
        r.card_image = (fs::path(c2.p).parent_path() / ".." / fs::path(c2.p).parent_path().filename() / fs::path(c2.p).filename()).string();
        const int spelled = sm::resolve_remembered(r, s, "", "").slot;
        // the remembered card is not mounted: the first mounted slot; slot 0 empty: slot 1
        r.card_image = (g_dir / "not-mounted.img").string();
        const int first = sm::resolve_remembered(r, s, "", "").slot;
        std::array<sm::SlotInfo, 2> s1only = s;
        s1only[0].mounted = false;
        const int second = sm::resolve_remembered(r, s1only, "", "").slot;
        std::array<sm::SlotInfo, 2> nonem = s1only;
        nonem[1].mounted = false;
        const int none = sm::resolve_remembered(r, nonem, "", "").slot;
        r.card_image.clear();
        const int unremembered = sm::resolve_remembered(r, s, "", "").slot;
        check("SDMR-02", "active card: the remembered image's slot (any spelling of it); else the first mounted slot; none mounted gives -1",
              in_slot1 == 1 && spelled == 1 && first == 0 && second == 1 && none == -1 && unremembered == 0,
              std::to_string(in_slot1) + std::to_string(spelled) + std::to_string(first) + std::to_string(second) + std::to_string(none));
    }
    // SDMR-03
    {
        Img c1(G), c2(F16);
        std::array<sm::SlotInfo, 2> s{};
        s[0] = {0, true, c1.p, true, ""};
        s[1] = {1, true, c2.p, false, "x"};
        sm::Remembered r;
        r.card_image = c1.p;
        r.card_dir = sm::encode_card_dir("/GAMES/SUB");
        const sm::Resolved same = sm::resolve_remembered(r, s, "", "");
        // the directory is applied only to the card it was remembered on: the other card, or the first mounted instead of an unmounted one
        r.card_image = c2.p;
        const sm::Resolved other_card = sm::resolve_remembered(r, s, "", "");
        r.card_image = (g_dir / "not-mounted.img").string();
        const sm::Resolved unmounted = sm::resolve_remembered(r, s, "", "");
        // a removed leaf gives the ROOT (the emulator wins), not the nearest parent
        r.card_image = c1.p;
        r.card_dir = sm::encode_card_dir("/GAMES/SUB/DEEP");
        const sm::Resolved deep = sm::resolve_remembered(r, s, "", "");
        std::string err;
        ops::remove(c1.p, "/GAMES/SUB/DEEP", err);
        const sm::Resolved leaf_gone = sm::resolve_remembered(r, s, "", "");
        // case: the remembered spelling is matched against open_names and comes back in the card's own spelling
        r.card_dir = sm::encode_card_dir("/games/sub");
        const sm::Resolved cased = sm::resolve_remembered(r, s, "", "");
        check("SDMR-03", "card directory: applied only to the card it was remembered on; a removed leaf gives the root, not the nearest parent; matched by open_name and returned in the card's own spelling",
              same.slot == 0 && same.card_dir == "/GAMES/SUB" && other_card.slot == 1 && other_card.card_dir == "/" &&
                  unmounted.slot == 0 && unmounted.card_dir == "/" && deep.card_dir == "/GAMES/SUB/DEEP" &&
                  leaf_gone.slot == 0 && leaf_gone.card_dir == "/" && cased.card_dir == "/GAMES/SUB",
              same.card_dir + "|" + other_card.card_dir + "|" + leaf_gone.card_dir + "|" + cased.card_dir);
    }
    // SDMR-04
    {
        // the stored form round-trips CP437 bytes and '%'
        const std::string raw = std::string("/D\xE9j\xE0 %41/\x1b""x");
        const std::string enc = sm::encode_card_dir(raw);
        bool ascii = true;
        for (unsigned char ch : enc) if (ch < 0x20 || ch > 0x7E) ascii = false;
        const bool round = sm::decode_card_dir(enc) == raw && ascii && enc.find('%') != std::string::npos;
        // malformed stored values read as the root
        const bool malformed = sm::decode_card_dir("%ZZ") == "/" && sm::decode_card_dir("%1") == "/" && sm::decode_card_dir("") == "/";
        // tampered values never leave the root: traversal, backslash, a name that is not an open_name, a file, a missing directory
        Img c(G);
        std::array<sm::SlotInfo, 2> s{};
        s[0] = {0, true, c.p, true, ""};
        sm::Remembered r;
        r.card_image = c.p;
        bool all_root = true;
        std::string bad;
        for (const char* v : {"/..", "/GAMES/..", "/GAMES/../MYDIR", "\\GAMES", "/GAMES\\SUB", "/ODD/NAME.", "/FILE.BIN", "/FILE.BIN/x",
                              "/NOPE", "/GAMES/NOPE/DEEP", "//..//", "/./GAMES", "%", "%00", "/GAMES/%2E%2E"}) {
            r.card_dir = v;                       // stored form typed straight in: tampered raw text
            const std::string a = sm::resolve_remembered(r, s, "", "").card_dir;
            r.card_dir = sm::encode_card_dir(v);  // and its escaped form
            const std::string b = sm::resolve_remembered(r, s, "", "").card_dir;
            if (a != "/" || b != "/") { all_root = false; bad += std::string(v) + "->" + a + "," + b + " "; }
        }
        // a plain valid one still resolves, with doubled and trailing separators folded
        r.card_dir = sm::encode_card_dir("//GAMES//SUB/");
        const std::string folded = sm::resolve_remembered(r, s, "", "").card_dir;
        check("SDMR-04", "a card directory with CP437 bytes, ESC and '%' round-trips through its stored form; a malformed value reads as the root; tampered values (traversal, backslash, a typed long name, a file, a missing directory) give the root",
              round && malformed && all_root && folded == "/GAMES/SUB", bad + folded);
    }

    std::error_code ec;
    fs::permissions(g_dir, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(g_dir, ec);
    check("SDMT-20", "the fixture cards are real volumes of the types the rows claim (FAT32 partitioned, FAT16 partitioned)",
          gt == FS_FAT32 && t16 == FS_FAT16);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
