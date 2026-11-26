// The SD card file manager's logic (GH #558 S2): src/platform/sdcard_manager.*, no toolkit.
//
// ORACLES. Nothing is derived by reading the implementation back.
//   - The owner's requirements (issue #558 and its decision comments, amended 2026-11-25): the manager does not touch
//     the machine (no pause, no reset, no question before a change): the guest sees a change when it next reads the
//     folder, and the manager sees the guest's writes (SDCOH-01..04); a card item that changed since it was listed is
//     not acted on (SDMT-37);
//     a card jnext cannot write, a write-protected card and a state that forbids a card change are shown
//     read-only; copies are all-or-nothing per item and stop at the first failure; the host side goes to the
//     trash, the card side is permanent; an existing destination asks, and a rename or folder never overwrites;
//     the remembered view reflects the emulator, which wins (owner amendment 2026-11-24): a remembered card that
//     is not mounted gives the mounted one, another card or a vanished directory gives the ROOT.
//   - The guest's view: the SPI port pair driven the way the guest drives it (OUT 0xE7 / OUT 0xEB / IN 0xEB, the SD
//     Physical Layer commands CMD0/8/55/41/58 init, CMD17 read, CMD24 write), and the FAT32 on-disk layout (fatgen103
//     section 3.1 BPB fields, section 6 directory entries, section 7 LFN entries) read RAW from what the SPI returns.
//     The machine's pause state and boots are read from the debugger backend and a LoopDriver that counts boots.
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
// Not driven by a row (stated): the video recorder's raw temp files (VideoRecorder::start needs ffmpeg in PATH)
// and NextPi's overlay (needs QEMU). Both register through the same held_files::Hold as the members SDMT-32 drives.
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
#include "audio/audio_recorder.h"
#include "audio/dac_trace_recorder.h"
#include "core/held_files.h"
#include "core/image_lock.h"
#include "core/output_files.h"
#include "peripheral/joy_uart_link.h"
#include "peripheral/sd_card.h"
#include "peripheral/sd_write_journal.h"
#include "platform/emulator_boot.h"
#include "remote/zrcp/zrcp_server.h"
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
const Bytes kLongF = payload(310, 51);
const Bytes kReadme = payload(77, 52);
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
         fat_write("0:/DATA/D1.BIN", kD1) && fat_write("0:/DATA/D2.BIN", kD2) &&
         fat_write("0:/DATA/Long File Name.txt", kLongF) && fat_write("0:/DATA/Readme.txt", kReadme) && f_mkdir("0:/SHD") == FR_OK;
    if (!ok) return false;
    const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
    hand_dir(fsv, dir_cluster("0:/ODD"), 2,
             {{u"", "NAME       ", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"NAME.", "NAME~1     ", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    // /SHD: an earlier long entry "foo" (alias FOO~1) and a later 8.3-only "FOO": two entries of one name
    hand_dir(fsv, dir_cluster("0:/SHD"), 2,
             {{u"foo", "FOO~1      ", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"", "FOO        ", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    hand_dir(fsv, dir_cluster("0:/ESC"), 2,
             {{u"\u001b[2Jevil", "EVIL~1     ", 0x20, c1, static_cast<uint32_t>(kD1.size())}}, pokes);
    return c1 != 0 && c2 != 0;
}

// A tiny valid TAP: a CODE header and a 2-byte data block (SDMT-23 loads it so the emulator holds a tape file).
Bytes tap_blk(uint8_t flag, const Bytes& pl) {
    Bytes b;
    const unsigned n = static_cast<unsigned>(pl.size()) + 2;
    b.push_back(static_cast<uint8_t>(n & 0xFF));
    b.push_back(static_cast<uint8_t>(n >> 8));
    b.push_back(flag);
    uint8_t x = flag;
    for (uint8_t v : pl) { b.push_back(v); x ^= v; }
    b.push_back(x);
    return b;
}
Bytes tap_pair() {
    Bytes h{3, 't', 'a', 'p', 'e', 't', 'e', 's', 't', ' ', ' ', 2, 0, 0, 0x80, 0, 0};
    Bytes a = tap_blk(0, h), d = tap_blk(0xFF, Bytes{1, 2});
    a.insert(a.end(), d.begin(), d.end());
    return a;
}

// A minimal WAV (8-bit mono 44100 Hz, silence) and a minimal TZX (one pause block): loadable tape files.
Bytes tiny_wav() {
    Bytes w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { w.push_back(static_cast<uint8_t>(v)); w.push_back(static_cast<uint8_t>(v >> 8)); };
    for (char c : std::string("RIFF")) w.push_back(static_cast<uint8_t>(c));
    u32(36 + 400);
    for (char c : std::string("WAVEfmt ")) w.push_back(static_cast<uint8_t>(c));
    u32(16); u16(1); u16(1); u32(44100); u32(44100); u16(1); u16(8);
    for (char c : std::string("data")) w.push_back(static_cast<uint8_t>(c));
    u32(400);
    for (int i = 0; i < 400; ++i) w.push_back(0x80);
    return w;
}
Bytes tiny_tzx() { return Bytes{'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20, 0x20, 0xE8, 0x03}; }

// A private copy of a master card, removed when the block ends.
// GH #558 (S3 review): names with code page 437 bytes and the damaged-directory cases that go with them. /DUPS: a short-only
// DER.BIN (D1) and a LONG "D<0x82> a long name.bin" (D2) whose 8.3 name is the SAME; /DUPD: the same with folders /DA and /DB;
// /SFNO: a short-only X<0x90>Y.BIN (no long name, a high byte in the 8.3 name); /D<0x82>R: a folder.
bool populate_bytes(FATFS& fsv, Pokes& pokes)
{
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/DUPS") != FR_OK || f_mkdir("0:/SFNO") != FR_OK || f_mkdir("0:/DUPD") != FR_OK ||
        f_mkdir("0:/DA") != FR_OK || f_mkdir("0:/DB") != FR_OK || f_mkdir("0:/D\x82R") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) ||
        !fat_write("0:/DATA/D2.BIN", kD2))
        return false;
    const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
    hand_dir(fsv, dir_cluster("0:/DUPS"), 2,
             {{u"", "DER     BIN", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"D\u00E9 a long name.bin", "DER     BIN", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    hand_dir(fsv, dir_cluster("0:/SFNO"), 2, {{u"", std::string("X\x90Y     BIN"), 0x20, c2, static_cast<uint32_t>(kD2.size())}}, pokes);
    hand_dir(fsv, dir_cluster("0:/DUPD"), 2,
             {{u"", "DER        ", 0x10, dir_cluster("0:/DA"), 0}, {u"D\u00E9 a long folder", "DER        ", 0x10, dir_cluster("0:/DB"), 0}},
             pokes);
    return c1 != 0 && c2 != 0;
}

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
    for (const auto& p : items) r.items.push_back({p.string(), p.filename().string(), false, std::string()});
    return r;
}

sm::TransferRequest card_req(const std::string& img, const std::string& dest, const std::vector<std::string>& items,
                             bool move = false) {
    sm::TransferRequest r;
    r.from = sm::Side::Card;
    r.image = img;
    r.dest_dir = dest;
    r.move = move;
    for (const auto& p : items) r.items.push_back({p, p, false, std::string()});
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

    explicit Rig(bool with_driver = true, MachineType type = MachineType::ZX48K, const std::string& sd = {},
                 const std::string& sd2 = {}) {
        cfg.type = type;
        cfg.sd_card_image = sd;
        cfg.sd_card2_image = sd2;
        cfg.rewind_buffer_frames = 0;
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
    sd_write_journal::forget_all_for_test();

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
        auto s = sm::card_slots(emu);
        const bool s0 = s[0].mounted && s[0].writable && s[0].image == w.p && s[0].read_only_reason.empty();
        const bool s1 = s[1].mounted && !s[1].writable && s[1].read_only_reason.find("first FAT32 partition") != std::string::npos;
        // write-protected device
        emu.sd_card().unmount();
        emu.sd_card().mount(w.p, true);
        s = sm::card_slots(emu);
        const bool wp = s[0].mounted && !s[0].writable && s[0].read_only_reason.find("--sdcard-readonly") != std::string::npos;
        // an empty slot
        emu.sd_card2().unmount();
        s = sm::card_slots(emu);
        const bool empty = !s[1].mounted && !s[1].writable && s[1].image.empty() && !s[1].read_only_reason.empty();
        // the machine forbids a card change (--no-sdcard)
        Emulator emu2;
        EmulatorConfig cfg2;
        cfg2.type = MachineType::ZX48K;
        cfg2.no_sdcard = true;
        emu2.init(cfg2);
        emu2.sd_card().mount(w.p, false);   // the file is open read-write here too; nothing is written
        const auto s2 = sm::card_slots(emu2);
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
        check("SDMT-11", "card_slots() marks a FAT16 card, a write-protected device, an empty slot and a --no-sdcard session read-only with the reason; a write onto such a card (or a move off it) is refused before any hook runs",
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
        const sm::TransferReport r = sm::delete_items(sm::Side::Card, c.p, {"/MYDIR", "/EMPTYD", "/RO", "/FILE.BIN"}, {}, h);
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
        const sm::TransferReport r2 = sm::delete_items(sm::Side::Host, G, {(hd / "d1").string(), (hd / "d2").string(), (hd / "d3").string()}, {}, h2);
        const bool host_ok = r2.done.size() == 1 && r2.failed == (hd / "d2").string() && r2.not_attempted.size() == 1 &&
                             !fs::exists(hd / "d1") && fs::exists(hd / "d2") && fs::exists(hd / "d3") && hk2.count("trash") == 2 &&
                             hk2.count("gate") == 0 && !r2.card_write_attempted;
        check("SDMT-10", "delete_items: the card removes each path permanently through the gate and stops at the first refusal naming the read-only entry; the host sends each to the trash and stops at a failure",
              card_ok && host_ok, rep_str(r) + " | " + rep_str(r2));
    }
    // SDMT-36
    {
        // F1a / F1b through the manager: the entry NAMED is the one acted on (a duplicate 8.3 name on the card, a short-only high-byte
        // name), copying off the card and onto it
        const std::string H = tmp_name("bytes") + ".img";
        const int ht = make_card(H, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_bytes);
        Img c(H);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const fs::path out = fresh_dir("bytes-out");
        const sm::TransferReport r1 = sm::run_transfer(card_req(c.p, out.string(), {"/DUPS/D\x82 a long name.bin"}), h);
        const sm::TransferReport r2 = sm::run_transfer(card_req(c.p, out.string(), {"/SFNO/X\x90Y.BIN", "/DUPS/DER.BIN"}), h);
        const Tree got = tree_of(out);
        const bool off = r1.done.size() == 1 && r2.done.size() == 2 && got.size() == 3 && got.count("D%82 a long name.bin") &&
                         got.at("D%82 a long name.bin") == kD2 && got.at("X%90Y.BIN") == kD2 && got.at("DER.BIN") == kD1;
        const fs::path hf = fresh_dir("bytes-host");
        write_host_file(hf / "one.bin", kOne);
        write_host_file(hf / "two.bin", kTwo);
        const sm::TransferReport r3 = sm::run_transfer(host_req(c.p, "/DUPD/D\x82 a long folder", {hf / "one.bin"}), h);
        const sm::TransferReport r4 = sm::run_transfer(host_req(c.p, "/D\x82R", {hf / "two.bin"}), h);
        auto names_in = [&](const std::string& dir) {
            std::string n;
            for (const auto& e : ls(c.p, dir)) n += (n.empty() ? "" : ",") + e.name;
            return n;
        };
        const bool on = r3.done.size() == 1 && r4.done.size() == 1 && names_in("/DB") == "one.bin" && names_in("/DA").empty() &&
                        names_in("/D\x82R") == "two.bin";
        check("SDMT-36", "a card entry whose 8.3 name duplicates an earlier one's, and a short-only high-byte name, are copied off the card as THEMSELVES (the other entry keeps its bytes); a copy into a long-named folder with a duplicate 8.3 name lands in THAT folder, and into a CP437-named folder",
              ht == FS_FAT32 && off && on, rep_str(r1) + " | " + rep_str(r2) + " | " + rep_str(r3) + " | " + rep_str(r4));
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
        const sm::TransferReport del = sm::delete_items(sm::Side::Card, c.p, {"/MYDIR", "/mydir/A.BIN", "/MYDIR/", "/FILE.BIN"}, {}, h);
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
        const sm::TransferReport hdel = sm::delete_items(sm::Side::Host, G, {hd.string(), (hd / "f").string()}, {}, h3);
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
        const sm::TransferReport d1 = sm::delete_items(sm::Side::Host, img, {img}, {}, h);
        const sm::TransferReport d2 = sm::delete_items(sm::Side::Host, img, {cards.string()}, {}, h);
        // a relative spelling of the same file is the same file
        const sm::TransferReport d3 = sm::delete_items(sm::Side::Host, img, {(cards / ".." / cards.filename() / "mounted.img").string()}, {}, h);
        const std::string sha_after = sha_file(img);
        check("SDMT-18", "the mounted image is refused as a copy source, a move source and a delete target, by identity (it, a folder holding it, a relative spelling); it is never trashed or changed",
              !a.failed.empty() && !b.failed.empty() && !mv.failed.empty() && !d1.failed.empty() && !d2.failed.empty() &&
                  !d3.failed.empty() && hk.count("trash") == 0 && hk.count("gate") == 0 && fs::exists(img) && sha_after == sha,
              rep_str(a) + " | " + rep_str(d2));
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
        const sm::TransferReport del = sm::delete_items(sm::Side::Card, c.p, {"/ODD/NAME."}, {}, h);
        const bool del_refused = !del.failed.empty() && ls(c.p, "/ODD").size() == 2;
        check("SDMT-19", "a card item named by its typed long name (trailing dot), a traversal, a backslash, the root or a missing name copies and deletes NOTHING and never reaches the gate; the one addressed by its open_name copies that entry's bytes",
              plain == "NAME" && !alias.empty() && alias != "NAME." && typed_refused && good.done.size() == 1 && bytes_ok &&
                  !up.failed.empty() && !back.failed.empty() && !root.failed.empty() && !missing.failed.empty() && del_refused &&
                  hk.count("gate") == 0,
              "alias=" + alias + " tr=" + std::to_string(typed_refused) + " bytes=" + std::to_string(bytes_ok) + " up=" + rep_str(up) +
                  " back=" + rep_str(back) + " root=" + rep_str(root) + " miss=" + rep_str(missing) + " delr=" + std::to_string(del_refused) +
                  " gates=" + std::to_string(hk.count("gate")) + " tree=" + std::to_string(t.size()));
    }

    // SDMT-21
    {
        // F1: BOTH slots' images are safe, whichever slot the pane shows: as copy source, move source, delete target, Replace target
        Img a(G);
        const fs::path dir_b = fresh_dir("slot2");
        const std::string img_b = (dir_b / "second.img").string();
        fs::copy_file(G, img_b, fs::copy_options::overwrite_existing);
        const std::string sha_a = sha_file(a.p), sha_b = sha_file(img_b);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        bool all = true;
        std::string bad;
        auto refused = [&](const char* what, const sm::TransferReport& r) {
            if (r.failed.empty() || !r.done.empty()) { all = false; bad += std::string(what) + " "; }
        };
        for (int pane = 0; pane < 2; ++pane) {
            const std::string pane_img = pane == 0 ? a.p : img_b;
            const std::string other = pane == 0 ? img_b : a.p;
            const fs::path other_dir = fs::path(other).parent_path();
            const std::vector<std::string> held = {a.p, img_b};
            sm::TransferRequest cp = host_req(pane_img, "/MYDIR", {fs::path(other)});
            cp.held_files = held;
            refused("copy", sm::run_transfer(cp, h));
            sm::TransferRequest cpd = host_req(pane_img, "/MYDIR", {other_dir});
            cpd.held_files = held;
            refused("copy-dir", sm::run_transfer(cpd, h));
            sm::TransferRequest mv = host_req(pane_img, "/MYDIR", {fs::path(other)}, true);
            mv.held_files = held;
            refused("move", sm::run_transfer(mv, h));
            sm::TransferRequest mvd = host_req(pane_img, "/MYDIR", {other_dir}, true);
            mvd.held_files = held;
            refused("move-dir", sm::run_transfer(mvd, h));
            refused("delete", sm::delete_items(sm::Side::Host, pane_img, {other}, held, h));
            refused("delete-dir", sm::delete_items(sm::Side::Host, pane_img, {other_dir.string()}, held, h));
        }
        // Replace target: a card entry whose host name is the other slot's image
        const fs::path dir_c = fresh_dir("slot2r");
        fs::copy_file(G, dir_c / "FILE.BIN", fs::copy_options::overwrite_existing);
        const std::string held_c = (dir_c / "FILE.BIN").string();
        const std::string sha_c = sha_file(held_c);
        hk.answers = {sm::OverwriteAnswer::Replace, sm::OverwriteAnswer::Replace};
        sm::TransferRequest rp = card_req(a.p, dir_c.string(), {"/FILE.BIN"});
        rp.held_files = {a.p, held_c};
        refused("replace", sm::run_transfer(rp, h));
        // the same request with nothing held is a normal Replace (negative control: the check is the held set)
        sm::TransferRequest rn = card_req(a.p, dir_c.string(), {"/FILE.BIN"});
        const sm::TransferReport free_run = sm::run_transfer(rn, h);
        check("SDMT-21", "BOTH slots' images are protected whichever slot the pane shows: the other slot's image and a folder holding it are refused as copy source, move source, delete target, and a card entry never replaces it; the same Replace with nothing held goes through",
              all && hk.count("trash") == 1 && hk.count("gate") == 0 && sha_file(a.p) == sha_a && sha_file(img_b) == sha_b &&
                  fs::exists(img_b) && free_run.done.size() == 1 && sha_file(held_c) != sha_c,
              bad + rep_str(free_run));
    }
    // SDMT-22
    {
        // F2: an SD-writes file (existing, or not yet created) is a held file: delete target, move source, copy source, Replace target
        Img a(G);
        const fs::path dw = fresh_dir("writes");
        const std::string wf = (dw / "card.sdw").string();           // not created yet
        const std::string wf2 = (dw / "WOTEF.NEX").string();          // exists, named like a card entry
        write_host_file(wf2, kX);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const std::vector<std::string> held = {wf, wf2};
        sm::TransferRequest cp = host_req(a.p, "/MYDIR", {fs::path(wf2)});
        cp.held_files = held;
        const sm::TransferReport r1 = sm::run_transfer(cp, h);
        sm::TransferRequest mv = host_req(a.p, "/MYDIR", {dw}, true);
        mv.held_files = held;
        const sm::TransferReport r2 = sm::run_transfer(mv, h);
        const sm::TransferReport r3 = sm::delete_items(sm::Side::Host, a.p, {wf}, held, h);
        const sm::TransferReport r4 = sm::delete_items(sm::Side::Host, a.p, {wf2}, held, h);
        hk.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferRequest rp = card_req(a.p, dw.string(), {"/GAMES/WOTEF.NEX"});
        rp.held_files = held;
        const sm::TransferReport r5 = sm::run_transfer(rp, h);
        const bool msg = r3.error.find("emulator is using") != std::string::npos && r5.error.find("emulator is using") != std::string::npos;
        check("SDMT-22", "an SD-writes file, existing or not created yet, is protected as copy source, move source (and the folder holding it), delete target and Replace target, with the reason stated",
              !r1.failed.empty() && !r2.failed.empty() && !r3.failed.empty() && !r4.failed.empty() && !r5.failed.empty() && msg &&
                  hk.count("trash") == 0 && hk.count("gate") == 0 && hk.count("ask") == 0 && read_host_file(wf2) == kX,
              rep_str(r3) + " | " + rep_str(r5));
    }
    // SDMT-23
    {
        // the whole class, from a real emulator: images and writes files of both slots, the tape-save target, the loaded tape, the esxDOS root
        const fs::path d = fresh_dir("held");
        const std::string ia = (d / "a.img").string(), ib = (d / "b.img").string();
        fs::copy_file(G, ia, fs::copy_options::overwrite_existing);
        fs::copy_file(F16, ib, fs::copy_options::overwrite_existing);
        const std::string wa = (d / "a.sdw").string(), wb = (d / "b.sdw").string();
        for (const auto& pr : {std::make_pair(ia, wa), std::make_pair(ib, wb)}) {
            sd_write_journal::Binding bd;
            bd.bytes = fs::file_size(pr.first);
            bd.sha256.fill(0x5C);
            std::string why;
            sd_write_journal::bind(pr.first, pr.second, bd, false, why);
        }
        fs::create_directories(d / "stub");
        const fs::path tape = d / "loaded.tap";
        write_host_file(tape, tap_pair());
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.tape_save_file = (d / "save.tap").string();
        cfg.esxdos_stub_root = (d / "stub").string();
        emu.init(cfg);
        emu.sd_card().mount(ia, false);
        emu.sd_card2().mount(ib, false);
        const bool tape_loaded = emu.tape().load(tape.string());
        const std::vector<std::string> held = sm::held_host_files(emu);
        auto has = [&](const std::string& p) { return std::find(held.begin(), held.end(), p) != held.end(); };
        const bool listed = has(ia) && has(ib) && has(wa) && has(wb) && has(cfg.tape_save_file) && has(cfg.esxdos_stub_root) &&
                            (!tape_loaded || has(tape.string()));
        // each of them, and the folder holding them all, is refused as a delete target; so is a journal-mode copy's writes file once it exists
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport put = sm::run_transfer(host_req(ia, "/MYDIR", {host / "one.bin"}), h);
        const bool journal_made = put.done.size() == 1 && sd_write_journal::peek_file(wa).is_journal && sha_file(ia) != std::string();
        std::vector<std::string> targets = {ia, ib, wa, wb, cfg.tape_save_file, cfg.esxdos_stub_root, d.string()};
        if (tape_loaded) targets.push_back(tape.string());
        bool all_refused = true;
        for (const std::string& t : targets) {
            const sm::TransferReport r = sm::delete_items(sm::Side::Host, ia, {t}, held, h);
            if (r.failed.empty() || !r.done.empty()) all_refused = false;
        }
        const sm::TransferReport mv = sm::run_transfer([&] { auto q = host_req(ia, "/MYDIR", {fs::path(wa)}, true); q.held_files = held; return q; }(), h);
        check("SDMT-23", "held_host_files lists what a real emulator holds (both slots' images and bound writes files, the tape-save target, the loaded tape, the esxDOS root); each is refused as a delete target, and a writes file that a journal-mode copy created cannot be moved off the host",
              listed && journal_made && all_refused && !mv.failed.empty() && hk.count("trash") == 0 && sd_write_journal::peek_file(wa).is_journal && fs::exists(wa),
              "listed=" + std::to_string(listed) + " journal=" + std::to_string(journal_made) + " held=" + std::to_string(held.size()));
        emu.sd_card().unmount();
        emu.sd_card2().unmount();
        sd_write_journal::forget_all_for_test();
    }
    // SDMT-24
    {
        // F3: a host name equal to another entry's 8.3 alias would overwrite THAT entry: refused, naming it; the listing decides what exists
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        write_host_file(host / "LONGFI~1.TXT", kOne);
        const sm::TransferReport alias = sm::run_transfer(host_req(c.p, "/DATA", {host / "LONGFI~1.TXT"}), h);
        const bool refused = !alias.failed.empty() && alias.error.find("Long File Name.txt") != std::string::npos && hk.count("ask") == 0 &&
                             hk.count("gate") == 0 && sha_file(c.p) == sha_g;
        // the hand-made entry: NAME. is listed with the alias NAME~1
        write_host_file(host / "NAME~1", kOne);
        const sm::TransferReport odd = sm::run_transfer(host_req(c.p, "/ODD", {host / "NAME~1"}), h);
        const bool odd_ok = !odd.failed.empty() && hk.count("ask") == 0 && sha_file(c.p) == sha_g;
        // a name that differs only in case from the STORED name is a Replace, asked with the stored name's path
        write_host_file(host / "long file name.TXT", kOne);
        hk.answers = {sm::OverwriteAnswer::Replace};
        const sm::TransferReport same = sm::run_transfer(host_req(c.p, "/DATA", {host / "long file name.TXT"}), h);
        const bool same_ok = same.done.size() == 1 && hk.count("ask:/DATA/Long File Name.txt:file") == 1 &&
                             extracted(c.p, "/DATA/Long File Name.txt") == Tree{{"Long File Name.txt", kOne}};
        // an unrelated new name is just copied
        write_host_file(host / "OTHER.TXT", kX);
        const sm::TransferReport fresh = sm::run_transfer(host_req(c.p, "/DATA", {host / "OTHER.TXT"}), h);
        check("SDMT-24", "host to card: a name that is another entry's 8.3 alias is refused naming that entry (no question, nothing written); a case variant of a stored name is a Replace asked with the stored name's path; a new name is copied",
              refused && odd_ok && same_ok && fresh.done.size() == 1 && on_card(c.p, "/DATA/OTHER.TXT"),
              rep_str(alias) + " | " + rep_str(odd) + " | " + rep_str(same));
    }
    // SDMT-25
    {
        // F4: a Replace trashed the old host entry and then the copy failed (here: something recreated the name): the report says it is in the trash
        const fs::path dd = fresh_dir("f4");
        write_host_file(dd / "WOTEF.NEX", kX);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Replace};
        hk.on_trash = [&](const std::string&) {};
        sm::TransferHooks h = hk.hooks();
        auto inner = h.trash_host;
        h.trash_host = [&, inner](const std::string& p, std::string& err) {
            const bool ok = inner(p, err);
            write_host_file(p, kD2);   // a racing process recreates the name after the trash
            return ok;
        };
        const sm::TransferReport r = sm::run_transfer(card_req(G, dd.string(), {"/GAMES/WOTEF.NEX"}), h);
        check("SDMT-25", "a Replace whose copy fails after the old host entry went to the trash says so in the report (and names the entry)",
              !r.failed.empty() && r.done.empty() && r.error.find("already sent to the trash") != std::string::npos &&
                  r.error.find((dd / "WOTEF.NEX").string()) != std::string::npos && hk.count("trash") == 1,
              rep_str(r));
    }
    // SDMT-26
    {
        // R01: card to host Replace whose target is a held file (here: the very image, named like a card entry), or a folder holding one
        const fs::path dd = fresh_dir("r01");
        const std::string img = (dd / "FILE.BIN").string();
        fs::copy_file(G, img, fs::copy_options::overwrite_existing);
        const std::string sha = sha_file(img);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Replace, sm::OverwriteAnswer::Replace};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport own = sm::run_transfer(card_req(img, dd.string(), {"/FILE.BIN"}), h);
        // a card FOLDER whose host target folder holds a held file
        const fs::path d3 = fresh_dir("r01b");
        fs::create_directories(d3 / "MYDIR");
        write_host_file(d3 / "MYDIR" / "held.bin", kX);
        sm::TransferRequest rq = card_req(G, d3.string(), {"/MYDIR"});
        rq.held_files = {(d3 / "MYDIR" / "held.bin").string()};
        const sm::TransferReport folder = sm::run_transfer(rq, h);
        check("SDMT-26", "a card entry that would replace a held host file (the image itself) or a folder holding one is refused before any question: nothing trashed, the file untouched",
              !own.failed.empty() && !folder.failed.empty() && hk.count("ask") == 0 && hk.count("trash") == 0 && sha_file(img) == sha &&
                  read_host_file(d3 / "MYDIR" / "held.bin") == kX,
              rep_str(own) + " | " + rep_str(folder));
    }
    // SDMT-27
    {
        // R02: the Replace target is a dangling link (POSIX), or a plain file (Windows: wine cannot make links): asked, the entry itself trashed, then copied
        const fs::path dd = fresh_dir("r02");
        bool made = true;
#ifndef _WIN32
        std::error_code lec;
        fs::create_symlink(dd / "no-such-target", dd / "WOTEF.NEX", lec);
        made = !lec;
#else
        write_host_file(dd / "WOTEF.NEX", kX);
#endif
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(card_req(G, dd.string(), {"/GAMES/WOTEF.NEX"}), h);
        check("SDMT-27", "an existing host entry that is a dangling link (a plain file on Windows) is found, asked about, sent to the trash itself, and replaced by the copy",
              made && r.done.size() == 1 && hk.count("ask:" + (dd / "WOTEF.NEX").string()) == 1 && hk.count("trash:" + (dd / "WOTEF.NEX").string()) == 1 &&
                  read_host_file(dd / "WOTEF.NEX") == kWotef,
              rep_str(r));
    }
    // SDMT-28
    {
        // R14 and R37: a card delete on a card that is not writable is refused before any hook; so is the card root
        Img c(G);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const std::string s16 = sha_file(F16);
        const sm::TransferReport fat16 = sm::delete_items(sm::Side::Card, F16, {"/GAMES"}, {}, h);
        const sm::TransferReport given = sm::delete_items(sm::Side::Card, c.p, {"/FILE.BIN"}, {}, h, "the card is write-protected in this session (--sdcard-readonly)");
        const sm::TransferReport root = sm::delete_items(sm::Side::Card, c.p, {"/"}, {}, h);
        const sm::TransferReport rootw = sm::delete_items(sm::Side::Card, c.p, {"//"}, {}, h);
        check("SDMT-28", "delete on a FAT16 card or with a read-only reason is refused before any hook, the image byte-identical; deleting the card root is refused and never reaches the gate",
              !fat16.error.empty() && fat16.done.empty() && !given.error.empty() && given.done.empty() && !root.failed.empty() &&
                  !rootw.failed.empty() && hk.log.empty() && sha_file(F16) == s16 && sha_file(c.p) == sha_g,
              rep_str(fat16) + " | " + rep_str(given) + " | " + rep_str(root));
    }

    // SDMT-29
    {
        // G2: the same alias inside a folder MERGE: the engine refuses it too, so Replace All overwrites nothing
        Img c(G);
        const fs::path hd = fresh_dir("g2");
        fs::create_directories(hd / "DATA");
        write_host_file(hd / "DATA" / "LONGFI~1.TXT", kOne);
        write_host_file(hd / "DATA" / "FRESH.BIN", kX);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::ReplaceAll};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/", {hd / "DATA"}), h);
        check("SDMT-29", "a folder merge holding a file named like another card entry's 8.3 alias fails even with Replace All, naming that entry: the card file keeps its bytes and nothing of the folder is copied",
              !r.failed.empty() && r.error.find("Long File Name.txt") != std::string::npos && r.card_write_attempted &&
                  extracted(c.p, "/DATA/Long File Name.txt") == Tree{{"Long File Name.txt", kLongF}} && !on_card(c.p, "/DATA/FRESH.BIN"),
              rep_str(r));
    }
    // SDMT-30
    {
        // two card entries of one name (a shadowed 8.3-only FOO behind a long foo): which a copy would replace is unknown: refused, nothing asked
        Img c(G);
        write_host_file(host / "foo", kOne);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/SHD", {host / "foo"}), h);
        check("SDMT-30", "a host name that two card entries carry is refused (naming the clash), nothing asked, nothing written",
              !r.failed.empty() && r.error.find("two entries") != std::string::npos && hk.count("ask") == 0 && hk.count("gate") == 0 &&
                  sha_file(c.p) == sha_g,
              rep_str(r));
    }
    // SDMT-31
    {
        // a mixed-case name that fits 8.3 (stored Readme.txt, alias README.TXT): the typed alias spelling is the same NAME, a Replace
        Img c(G);
        write_host_file(host / "README.TXT", kOne);
        Hk hk;
        hk.answers = {sm::OverwriteAnswer::Replace};
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/DATA", {host / "README.TXT"}), h);
        check("SDMT-31", "a host name equal (case-folded) to a stored mixed-case name is a Replace asked with the stored name's path, not an alias refusal",
              r.done.size() == 1 && hk.count("ask:/DATA/Readme.txt:file") == 1 && extracted(c.p, "/DATA/Readme.txt") == Tree{{"Readme.txt", kOne}},
              rep_str(r));
    }
    // SDMT-32
    {
        // the registry's writers, driven for real: --wav-record, --dac-trace, a log file (output_files), an image lock file, a FIFO pair
        const fs::path d = fresh_dir("reg");
        Img a(G);
        AudioRecorder wav;
        const std::string wavp = (d / "rec.wav").string();
        const bool wav_ok = wav.start(wavp);
        DacTraceRecorder dac;
        const std::string dacp = (d / "dac.trace").string();
        const bool dac_ok = dac.start(dacp);
        const std::string logp = (d / "run.log").string();
        output_files::begin(logp);
        const fs::path lockdir = d / "locks";
        std::string lwhy;
        auto lock = sdcard::ImageLock::acquire(a.p, sdcard::ImageLock::Kind::Image, lwhy, lockdir.string());
        const bool lock_ok = lock.has_value();
        bool fifo_ok = true;
        std::string fifo_rx, fifo_tx;
#ifndef _WIN32
        std::unique_ptr<JoyUartEndpoint> ep;
        {
            std::string ferr;
            ep = JoyUartEndpoint::open_fifo((d / "joy").string(), ferr);
            fifo_ok = ep != nullptr;
            fifo_rx = (d / "joy.rx").string();
            fifo_tx = (d / "joy.tx").string();
        }
#endif
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        const std::vector<std::string> held = sm::held_host_files(emu);
        auto has = [&](const std::string& p) { return std::find(held.begin(), held.end(), p) != held.end(); };
        std::string lock_file;
        for (const std::string& p : held) if (p.rfind(lockdir.string(), 0) == 0) lock_file = p;
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        std::vector<std::string> targets = {wavp, dacp, logp, lock_file, lockdir.string(), d.string()};
        if (!fifo_rx.empty()) { targets.push_back(fifo_rx); targets.push_back(fifo_tx); }
        bool all_refused = true;
        for (const std::string& t : targets) {
            if (t.empty()) { all_refused = false; continue; }
            const sm::TransferReport r = sm::delete_items(sm::Side::Host, a.p, {t}, held, h);
            if (r.failed.empty() || !r.done.empty()) all_refused = false;
        }
        const bool listed = has(wavp) && has(dacp) && has(logp) && !lock_file.empty() && (fifo_rx.empty() || (has(fifo_rx) && has(fifo_tx)));
        // released when the holder lets go
        wav.stop();
        dac.stop();
        output_files::end(logp);
        lock.reset();
#ifndef _WIN32
        ep.reset();
#endif
        const std::vector<std::string> after = sm::held_host_files(emu);
        auto still = [&](const std::string& p) { return std::find(after.begin(), after.end(), p) != after.end(); };
        const bool released = !still(wavp) && !still(dacp) && !still(logp) && !still(lock_file) &&
                              (fifo_rx.empty() || (!still(fifo_rx) && !still(fifo_tx)));
        check("SDMT-32", "what registers itself is held while it is held and released after: --wav-record, --dac-trace, a log file, an image lock file (and the folder holding it), a FIFO pair; each is refused as a delete target, none is after the holder let go. (The video recorder's raw temp files and NextPi's overlay register the same way but need ffmpeg and QEMU: not driven here)",
              wav_ok && dac_ok && lock_ok && listed && all_refused && released && fifo_ok && hk.count("trash") == 0,
              "listed=" + std::to_string(listed) + " refused=" + std::to_string(all_refused) + " released=" + std::to_string(released));
    }
    // SDMT-33
    {
        // the configuration-only members: both slots' CONFIGURED images (and writes files) after an eject, and the NEX a read overlay serves
        const fs::path d = fresh_dir("cfg");
        const std::string ia = (d / "a.img").string(), ib = (d / "b.img").string(), nex = (d / "prog.nex").string();
        fs::copy_file(G, ia, fs::copy_options::overwrite_existing);
        fs::copy_file(F16, ib, fs::copy_options::overwrite_existing);
        write_host_file(nex, kX);
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.sd_card_image = ia;
        cfg.sd_card2_image = ib;
        cfg.load_file = nex;
        emu.init(cfg);
        emu.sd_card().unmount();
        emu.sd_card2().unmount();
        const std::vector<std::string> ejected = sm::held_host_files(emu);
        auto has = [&](const std::vector<std::string>& v, const std::string& p) { return std::find(v.begin(), v.end(), p) != v.end(); };
        const bool cards = has(ejected, emu.config().sd_card_image) && has(ejected, emu.config().sd_card2_image);
        const bool nex_free = !has(ejected, emu.config().load_file);
        emu.sd_card().set_read_overlay(100, 4, [](uint32_t, uint8_t*) { return false; });
        const std::vector<std::string> overlaid = sm::held_host_files(emu);
        check("SDMT-33", "an ejected slot's configured image is still held (slot 0 and slot 1); the NEX named by load_file is held only while a read overlay serves it",
              cards && nex_free && has(overlaid, emu.config().load_file) && !emu.config().sd_card_image.empty(),
              "cards=" + std::to_string(cards) + " nex_free=" + std::to_string(nex_free));
    }
    // SDMT-35
    {
        // output_files::begin twice for one path is ONE hold: a single end() releases it (the header's contract)
        const std::string p = (g_dir / "twice.out").string();
        auto held = [&] {
            const auto v = held_files::snapshot();
            return static_cast<int>(std::count(v.begin(), v.end(), p));
        };
        output_files::begin(p);
        output_files::begin(p);
        const int twice = held();
        output_files::end(p);
        const int after_end = held();
        output_files::end(p);   // an end() with nothing begun is harmless
        check("SDMT-35", "output_files::begin twice then end once leaves nothing held (one hold per path, however many begins), and a stray end is harmless",
              twice == 1 && after_end == 0 && held() == 0, std::to_string(twice) + std::to_string(after_end));
    }
    // SDMT-34
    {
        // an RZX recording, a tape recorder (TZX save), a loaded TZX and a loaded WAV (and a TAP save target) are held
        const fs::path d = fresh_dir("tapes");
        Img a(G);
        write_host_file(d / "t.tzx", tiny_tzx());
        write_host_file(d / "t.wav", tiny_wav());
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        const std::string rzx = (d / "rec.rzx").string(), rec = (d / "save.tzx").string(), tapsave = (d / "save.tap").string();
        const bool rzx_ok = emu.start_rzx_recording(rzx);
        const bool tzx_ok = emu.tzx_tape().load((d / "t.tzx").string());
        const bool wav_ok = emu.wav_tape().load((d / "t.wav").string());
        // a tape save cannot start while an RZX records: the recorder and the TAP save run on a second machine
        Emulator emu2;
        emu2.init(cfg);
        const bool rec_ok = emu2.start_tape_save(rec);
        std::vector<std::string> held = sm::held_host_files(emu);
        for (const std::string& p : sm::held_host_files(emu2)) held.push_back(p);
        auto has = [&](const std::string& p) { return std::find(held.begin(), held.end(), p) != held.end(); };
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        bool all_refused = true;
        for (const std::string& t : {rzx, rec, (d / "t.tzx").string(), (d / "t.wav").string()}) {
            const sm::TransferReport r = sm::delete_items(sm::Side::Host, a.p, {t}, held, h);
            if (r.failed.empty() || !r.done.empty()) all_refused = false;
        }
        emu2.stop_tape_save();
        const bool tap_ok = emu2.start_tape_save(tapsave);
        const std::vector<std::string> later = sm::held_host_files(emu2);
        emu2.stop_tape_save();
        const std::vector<std::string> done = sm::held_host_files(emu2);
        auto in = [&](const std::vector<std::string>& v, const std::string& p) { return std::find(v.begin(), v.end(), p) != v.end(); };
        // the RZX recording ends: its file is no longer held (it is rewritten by path only while recording)
        emu.stop_rzx_recording();
        const bool rzx_released = !in(sm::held_host_files(emu), rzx);
        check("SDMT-34", "an RZX recording, a tape recorder's file, a loaded TZX and a loaded WAV are held and refused as delete targets; a tape save target is held while saving and released at the end, and the RZX file once the recording has stopped",
              rzx_ok && tzx_ok && wav_ok && rec_ok && has(rzx) && has(rec) && has((d / "t.tzx").string()) && has((d / "t.wav").string()) &&
                  all_refused && tap_ok && in(later, tapsave) && !in(done, tapsave) && !in(done, rec) && rzx_released,
              "rzx=" + std::to_string(rzx_ok) + " tzx=" + std::to_string(tzx_ok) + " wav=" + std::to_string(wav_ok) + " rec=" + std::to_string(rec_ok));
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

    // SDMR-05
    {
        // R09: a relative host_dir that DOES exist relative to the working directory is still not trusted; a remembered card image with a NUL is not examined
        const fs::path fb = fresh_dir("fb05");
        std::array<sm::SlotInfo, 2> none{};
        auto host_of = [&](const std::string& rem) {
            sm::Remembered r;
            r.host_dir = rem;
            return sm::resolve_remembered(r, none, fb.string(), "").host_dir;
        };
        const bool rel_ok = host_of(".") == fb.string() && host_of("..") == fb.string() && host_of("./") == fb.string();
        Img c1(G), c2(F16);
        std::array<sm::SlotInfo, 2> s{};
        s[0] = {0, true, c1.p, true, ""};
        s[1] = {1, true, c2.p, false, "x"};
        sm::Remembered r;
        r.card_image = c2.p + std::string("\0tail", 5);   // c_str() of it is c2's path
        const int nul_slot = sm::resolve_remembered(r, s, "", "").slot;
        r.card_image = c2.p;
        const int plain_slot = sm::resolve_remembered(r, s, "", "").slot;
        check("SDMR-05", "a relative host directory that exists relative to the working directory (\".\", \"..\") still gives the fallback; a remembered card image holding a NUL is not matched against the slots (the first mounted slot is shown), the plain one is",
              rel_ok && nul_slot == 0 && plain_slot == 1, std::to_string(nul_slot) + std::to_string(plain_slot));
    }

    // ======================= the machine sees what the manager does, and the manager what the machine does =======================
    //
    // The guest's side: the SPI port pair, driven as the firmware drives it, against a REAL Emulator that keeps running
    // (never paused) while the manager changes the card between frames. The FAT32 layout is read RAW from what CMD17
    // returns (fatgen103: BPB reserved sectors @14, FAT count @16, sectors per cluster @13, FAT size @36, root cluster
    // @44; a directory entry is 32 bytes, name @0, attributes @11, cluster high @20, low @26, size @28; an LFN entry
    // holds UTF-16 characters at 1..10, 14..25, 28..31).
    struct GuestSd {
        Emulator& e;
        void sel(uint8_t v) { e.port().out(0x00E7, v); }
        void tx(uint8_t b) { e.port().out(0x00EB, b); }
        uint8_t rx() { return e.port().in(0x00EB); }
        uint8_t cmd(uint8_t c, uint32_t arg) {
            tx(static_cast<uint8_t>(0x40 | c));
            tx(static_cast<uint8_t>(arg >> 24)); tx(static_cast<uint8_t>(arg >> 16));
            tx(static_cast<uint8_t>(arg >> 8));  tx(static_cast<uint8_t>(arg));
            tx(0x95);
            for (int i = 0; i < 20; ++i) {
                const uint8_t r1 = rx();
                if (r1 != 0xFF) return r1;
            }
            return 0xFF;
        }
        void init() { cmd(0, 0); cmd(8, 0x1AA); cmd(55, 0); cmd(41, 0x40000000); cmd(58, 0); }
        // `selv` 0xFE = card 0, 0xFD = card 1 (zxnext.vhd:3311-3314). Every call is a whole guest transaction.
        bool read(uint8_t selv, uint32_t sector, Bytes& out) {
            sel(selv);
            init();
            bool ok = false;
            if (cmd(17, sector) == 0x00) {
                for (int i = 0; i < 64 && !ok; ++i) ok = rx() == 0xFE;
                if (ok) {
                    out.resize(512);
                    for (int i = 0; i < 512; ++i) out[static_cast<size_t>(i)] = rx();
                    rx(); rx();
                }
            }
            sel(0xFF);
            return ok;
        }
        bool write(uint8_t selv, uint32_t sector, const Bytes& data) {
            sel(selv);
            init();
            bool ok = false;
            if (cmd(24, sector) == 0x00) {
                tx(0xFE);
                for (int i = 0; i < 512; ++i) tx(data[static_cast<size_t>(i)]);
                tx(0xFF); tx(0xFF);
                for (int i = 0; i < 32; ++i) {
                    const uint8_t b = rx();
                    if (b != 0xFF) { ok = (b & 0x1F) == 0x05; break; }
                }
            }
            sel(0xFF);
            return ok;
        }
    };
    struct Geo { uint32_t root = 0, data = 0, spc = 0; };
    auto geometry = [&](GuestSd& g, uint8_t selv, Geo& geo) {
        Bytes bpb;
        if (!g.read(selv, kLba, bpb)) return false;
        auto u16 = [&](size_t o) { return static_cast<uint32_t>(bpb[o] | (bpb[o + 1] << 8)); };
        auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
        geo.spc = bpb[13];
        geo.data = kLba + u16(14) + bpb[16] * u32(36);
        geo.root = geo.data + (u32(44) - 2) * geo.spc;
        return geo.spc != 0;
    };
    auto contains = [](const Bytes& hay, const Bytes& needle) {
        return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
    };
    auto sfn_at = [](const Bytes& dir, const char* sfn11) -> int {   // offset of the 32-byte entry whose name is sfn11, or -1
        for (size_t o = 0; o + 32 <= dir.size(); o += 32)
            if (std::memcmp(dir.data() + o, sfn11, 11) == 0 && dir[o + 11] != 0x0F) return static_cast<int>(o);
        return -1;
    };
    const Bytes kLong = payload(700, 77);
    const fs::path longhost = host / "Long Name.txt";
    write_host_file(longhost, kLong);
    const Bytes lfn_prefix = {'L', 0, 'o', 0, 'n', 0, 'g', 0, ' ', 0};   // the first five characters of the long name, UTF-16LE

    // One run of the coherence scenario: `slot` (0 or 1) is the card the manager changes and the guest reads; `writes`
    // binds a writes file to its image. Returns what the rows assert.
    struct Coh {
        bool before_clean = false, after_has_entry = false, data_ok = false, never_paused = false, no_boot = false,
             no_reset = false, image_same = false, journal_has_changed = false, ok_run = false;
        std::string detail;
    };
    auto coherence = [&](int slot, bool writes) {
        Coh c;
        Img a(G), b(G);
        const std::string target = slot == 0 ? a.p : b.p;
        std::string jf;
        if (writes) {
            jf = (fresh_dir("cohj") / "w.sdw").string();
            sd_write_journal::Binding bd;
            bd.bytes = fs::file_size(target);
            bd.sha256.fill(0x3C);
            std::string why;
            if (!sd_write_journal::bind(target, jf, bd, false, why)) { c.detail = "bind: " + why; return c; }
        }
        const std::string sha_before = sha_file(target);
        Rig r(true, MachineType::ZXN_ISSUE2, a.p, b.p);
        GuestSd g{r.emu};
        const uint8_t selv = slot == 0 ? 0xFE : 0xFD;
        for (int i = 0; i < 3; ++i) r.emu.run_frame();
        Geo geo;
        Bytes before, after, data;
        if (!geometry(g, selv, geo) || !g.read(selv, geo.root, before)) { c.detail = "guest read failed"; return c; }
        c.before_clean = sfn_at(before, "LONGNA~1TXT") < 0 && !contains(before, lfn_prefix);
        bool paused_ever = r.paused();
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const sm::TransferReport rep = sm::run_transfer(host_req(target, "/", {longhost}), h);   // between two frames
        paused_ever = paused_ever || r.paused();
        // the very next thing the guest reads is the same sector (no frame, no other read between the two reads of it: a
        // one-block read cache in the device would answer the old bytes), and again after more frames
        Bytes right_after;
        const bool immediate = g.read(selv, geo.root, right_after) && sfn_at(right_after, "LONGNA~1TXT") >= 0;
        for (int i = 0; i < 3; ++i) r.emu.run_frame();
        paused_ever = paused_ever || r.paused();
        if (!g.read(selv, geo.root, after)) { c.detail = "guest re-read failed"; return c; }
        const int off = sfn_at(after, "LONGNA~1TXT");
        c.after_has_entry = immediate && off >= 0 && after[static_cast<size_t>(off) + 11] == 0x20 && contains(after, lfn_prefix);
        if (off >= 0) {
            auto u16 = [&](size_t o) { return static_cast<uint32_t>(after[o] | (after[o + 1] << 8)); };
            const uint32_t cluster = (u16(static_cast<size_t>(off) + 20) << 16) | u16(static_cast<size_t>(off) + 26);
            const uint32_t size = u16(static_cast<size_t>(off) + 28) | (u16(static_cast<size_t>(off) + 30) << 16);
            c.data_ok = cluster >= 2 && size == kLong.size() && g.read(selv, geo.data + (cluster - 2) * geo.spc, data) &&
                        std::equal(data.begin(), data.begin() + 512, kLong.begin());
        }
        c.never_paused = !paused_ever;
        c.no_boot = r.boots == 0;
        c.no_reset = !r.emu.take_hard_reset_request();
        c.image_same = !writes || sha_file(target) == sha_before;
        if (writes) {
            // the sectors a plain copy changes are all in the writes file
            Img plain(G);
            std::string perr;
            sdcard::add_to_image(plain.p, longhost.string(), "/Long Name.txt", false, perr, nullptr, true);
            std::ifstream f0(G, std::ios::binary), f1(plain.p, std::ios::binary);
            const auto info = sd_write_journal::peek_file(jf);
            std::vector<char> x(512), y(512);
            bool all = info.is_journal && !info.sectors.empty();
            for (uint32_t sct = 0; f0.read(x.data(), 512) && f1.read(y.data(), 512); ++sct)
                if (x != y) all = all && std::find(info.sectors.begin(), info.sectors.end(), sct) != info.sectors.end();
            c.journal_has_changed = all;
        } else {
            c.journal_has_changed = true;
        }
        c.ok_run = rep.failed.empty() && rep.error.empty() && rep.done.size() == 1;
        c.detail = rep_str(rep);
        return c;
    };
    // SDCOH-01
    {
        const Coh c = coherence(0, false);
        check("SDCOH-01", "plain card, slot 0: the machine keeps running while the manager copies 'Long Name.txt' onto the card between frames, and the guest's SPI reads (CMD17) of the root directory, before and after, show the new LFN entry, the 8.3 alias LONGNA~1TXT with the archive attribute, and the file's bytes; zero cold boots, never paused, no reset requested",
              c.ok_run && c.before_clean && c.after_has_entry && c.data_ok && c.never_paused && c.no_boot && c.no_reset, c.detail);
    }
    // SDCOH-02
    {
        const Coh c = coherence(0, true);
        check("SDCOH-02", "the same with a writes file bound: the guest sees the entry and the data, the image's bytes are unchanged, and every sector a plain copy changes is in the writes file",
              c.ok_run && c.before_clean && c.after_has_entry && c.data_ok && c.never_paused && c.no_boot && c.no_reset && c.image_same &&
                  c.journal_has_changed,
              c.detail);
    }
    // SDCOH-03
    {
        const Coh c = coherence(1, false);
        check("SDCOH-03", "the same on the SECOND card, through its own chip select (OUT 0xE7 selects card 1)",
              c.ok_run && c.before_clean && c.after_has_entry && c.data_ok && c.never_paused && c.no_boot && c.no_reset, c.detail);
    }
    // SDCOH-04
    {
        // the guest writes the sectors a plain copy of one.bin changes (the bytes come from making that copy with the
        // engine on a scratch card); the manager's listing then shows the file, in plain and in writes-file mode
        Img scratch(G);
        std::string perr;
        sdcard::add_to_image(scratch.p, (host / "one.bin").string(), "/GUEST.BIN", false, perr, nullptr, true);
        std::vector<std::pair<uint32_t, Bytes>> changed;
        {
            std::ifstream f0(G, std::ios::binary), f1(scratch.p, std::ios::binary);
            std::vector<char> x(512), y(512);
            for (uint32_t sct = 0; f0.read(x.data(), 512) && f1.read(y.data(), 512); ++sct)
                if (x != y) changed.push_back({sct, Bytes(y.begin(), y.end())});
        }
        auto arm = [&](bool writes, std::string& detail) {
            Img c(G);
            std::string jf;
            if (writes) {
                jf = (fresh_dir("cohg") / "w.sdw").string();
                sd_write_journal::Binding bd;
                bd.bytes = fs::file_size(c.p);
                bd.sha256.fill(0x4D);
                std::string why;
                if (!sd_write_journal::bind(c.p, jf, bd, false, why)) { detail = why; return false; }
            }
            Rig r(true, MachineType::ZXN_ISSUE2, c.p);
            GuestSd g{r.emu};
            for (int i = 0; i < 2; ++i) r.emu.run_frame();
            bool wrote = !changed.empty();
            for (const auto& ch : changed) wrote = wrote && g.write(0xFE, ch.first, ch.second);
            const bool listed = [&] {
                for (const auto& e : ls(c.p, "/")) if (e.name == "GUEST.BIN" && !e.dir && e.size == kOne.size()) return true;
                return false;
            }();
            const Tree got = extracted(c.p, "/GUEST.BIN");
            return wrote && listed && got == Tree{{"GUEST.BIN", kOne}} && (!writes || sha_file(c.p) == sha_g);
        };
        std::string d1, d2;
        const bool plain = arm(false, d1), journal = arm(true, d2);
        check("SDCOH-04", "the reverse direction: the guest writes a directory sector, a FAT sector and a data sector by SPI CMD24, and the manager's listing and extract then show the new file, on a plain card and with a writes file (image unchanged)",
              plain && journal && !changed.empty(), d1 + "|" + d2 + " sectors=" + std::to_string(changed.size()));
    }
    // SDMT-37
    {
        Img c(G);
        auto stamp_of = [&](const std::string& dir, const std::string& name) {
            for (const auto& e : ls(c.p, dir)) if (e.name == name) return sm::entry_stamp(e);
            return std::string();
        };
        const std::string st_a = stamp_of("/MYDIR", "A.BIN"), st_b = stamp_of("/MYDIR", "B.BIN");
        // between the listing and the action the guest renames A.BIN and creates another A.BIN of another size
        std::string err;
        const bool renamed = ops::rename(c.p, "/MYDIR/A.BIN", "OLD.BIN", err) == St::Ok;
        const bool recreated = sdcard::add_to_image(c.p, (host / "two.bin").string(), "/MYDIR/A.BIN", false, err, nullptr, true) == St::Ok;
        const std::string sha_stale = sha_file(c.p);
        Hk hk;
        sm::TransferHooks h = hk.hooks();
        const fs::path out = fresh_dir("stale");
        sm::TransferRequest cp = card_req(c.p, out.string(), {"/MYDIR/A.BIN"});
        cp.items[0].stamp = st_a;
        const sm::TransferReport rc = sm::run_transfer(cp, h);
        sm::TransferRequest mv = card_req(c.p, out.string(), {"/MYDIR/A.BIN"}, true);
        mv.items[0].stamp = st_a;
        const sm::TransferReport rm = sm::run_transfer(mv, h);
        const sm::TransferReport rd = sm::delete_items(sm::Side::Card, c.p, {"/MYDIR/A.BIN"}, {}, h, {}, {st_a});
        const sm::TransferReport rr = sm::rename_card_item(c.p, "/MYDIR/A.BIN", st_a, "NEW.BIN", h);
        auto stale_msg = [](const sm::TransferReport& r) {
            return r.error.find("changed on the card since it was listed") != std::string::npos && !r.failed.empty() && r.done.empty();
        };
        const bool nothing = sha_file(c.p) == sha_stale && tree_of(out).empty() && on_card(c.p, "/MYDIR/A.BIN") && on_card(c.p, "/MYDIR/OLD.BIN");
        // an unchanged entry proceeds with the same stamp
        sm::TransferRequest ok = card_req(c.p, out.string(), {"/MYDIR/B.BIN"});
        ok.items[0].stamp = st_b;
        const sm::TransferReport rok = sm::run_transfer(ok, h);
        check("SDMT-37", "a card item that is not the entry listed (renamed away and re-created under its name with another size) fails copy, move, delete and rename with 'changed on the card since it was listed' and writes nothing; an unchanged item with its listed stamp proceeds",
              renamed && recreated && !st_a.empty() && stale_msg(rc) && stale_msg(rm) && stale_msg(rd) && stale_msg(rr) && nothing &&
                  rok.done.size() == 1 && rok.error.empty() && tree_of(out) == Tree{{"B.BIN", kB}},
              rep_str(rc) + " | " + rep_str(rm) + " | " + rep_str(rd) + " | " + rep_str(rr));
    }
    // SDMT-38
    {
        // the overwrite question is a modal box and the guest runs while it is open: the item is verified again after it
        auto stale_msg = [](const sm::TransferReport& r) {
            return r.error.find("changed on the card since it was listed") != std::string::npos && !r.failed.empty() && r.done.empty();
        };
        // the guest renames /MYDIR/A.BIN away and creates another A.BIN (another size) under its name
        auto change = [&](const std::string& img) {
            std::string e;
            ops::rename(img, "/MYDIR/A.BIN", "OLD.BIN", e);
            sdcard::add_to_image(img, (host / "two.bin").string(), "/MYDIR/A.BIN", false, e, nullptr, true);
        };
        std::string diag;
        // card to host: the host already holds an A.BIN, so the question is asked
        auto c2h = [&](bool move, bool changed) {
            Img c(G);
            std::string st;
            for (const auto& e : ls(c.p, "/MYDIR")) if (e.name == "A.BIN") st = sm::entry_stamp(e);
            const fs::path out = fresh_dir("q2h");
            write_host_file(out / "A.BIN", kX);
            Hk hk;
            hk.answers = {sm::OverwriteAnswer::Replace};
            sm::TransferHooks h = hk.hooks();
            const auto inner = h.ask_overwrite;
            h.ask_overwrite = [&](const std::string& d, bool dd) { if (changed) change(c.p); return inner(d, dd); };
            sm::TransferRequest rq = card_req(c.p, out.string(), {"/MYDIR/A.BIN"}, move);
            rq.items[0].stamp = st;
            const sm::TransferReport r = sm::run_transfer(rq, h);
            diag += "c2h:" + rep_str(r) + " | ";
            if (changed)
                return stale_msg(r) && hk.count("trash:") == 0 && tree_of(out) == Tree{{"A.BIN", kX}} &&
                       extracted(c.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kTwo}} && on_card(c.p, "/MYDIR/OLD.BIN");
            return r.done.size() == 1 && r.error.empty() && tree_of(out) == Tree{{"A.BIN", kA}} && move == !on_card(c.p, "/MYDIR/A.BIN");
        };
        // host to card: the card already holds /MYDIR/A.BIN, so the question is asked
        auto h2c = [&](bool move, bool changed) {
            Img c(G);
            const fs::path src = fresh_dir("q2c") / "A.BIN";
            write_host_file(src, kX);
            Hk hk;
            hk.answers = {sm::OverwriteAnswer::Replace};
            sm::TransferHooks h = hk.hooks();
            const auto inner = h.ask_overwrite;
            h.ask_overwrite = [&](const std::string& d, bool dd) { if (changed) change(c.p); return inner(d, dd); };
            const sm::TransferReport r = sm::run_transfer(host_req(c.p, "/MYDIR", {src}, move), h);
            diag += "h2c:" + rep_str(r) + " | ";
            if (changed)
                return stale_msg(r) && fs::exists(src) && hk.count("trash:") == 0 &&
                       extracted(c.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kTwo}} && on_card(c.p, "/MYDIR/OLD.BIN");
            return r.done.size() == 1 && r.error.empty() && extracted(c.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kX}} && move == !fs::exists(src);
        };
        const bool ctl = c2h(false, false) && c2h(true, false) && h2c(false, false) && h2c(true, false);
        const bool c2h_stale = c2h(false, true) && c2h(true, true);
        const bool h2c_stale = h2c(false, true) && h2c(true, true);
        check("SDMT-38", "a card entry that changes while the overwrite question is open (renamed away and re-created under its name) is refused after the answer with 'changed on the card since it was listed', copy and move, in both directions: nothing is trashed, extracted or replaced; with no change the same answer proceeds",
              ctl && c2h_stale && h2c_stale, std::string(ctl ? "ctl=1 " : "ctl=0 ") + (c2h_stale ? "c2h=1 " : "c2h=0 ") + (h2c_stale ? "h2c=1 " : "h2c=0 ") + diag);
    }

    // ======================= remote_add: a copy asked for by another process (GH #568) =======================
    // Oracle: the owner's design on the issue (the card is named by image and number, the writes file must agree,
    // the manager's own read-only and held-file refusals apply, the copy means what --sdcard-file-add means) and the
    // contract in sdcard_manager.h. Card contents are read back with extract_from_image / list_dir (independent of
    // the writer). IDs SDRA-*: the SDMT-3x numbers of the plan are taken by earlier rows.
    {
        using jnext::remote::zrcp::SdFileAddReply;
        using jnext::remote::zrcp::SdFileAddRequest;
        auto req = [](const std::string& img, const fs::path& h, const std::string& dest, int slot = -1) {
            SdFileAddRequest r;
            r.image = img;
            r.host = h.string();
            r.card_path = dest;
            r.slot = slot;
            return r;
        };
        auto mount2 = [](Emulator& emu, const std::string& a, const std::string& b) {
            EmulatorConfig cfg;
            cfg.type = MachineType::ZX48K;
            emu.init(cfg);
            emu.sd_card().mount(a, false);
            if (!b.empty()) emu.sd_card2().mount(b, false);
        };
        auto unmount = [](Emulator& emu) { emu.sd_card().unmount(); emu.sd_card2().unmount(); };
        const std::string none;
        {
            Img a(G), b(G);
            Emulator emu;
            mount2(emu, a.p, b.p);
            const std::string sb = sha_file(b.p);
            const SdFileAddReply r = sm::remote_add(emu, req(a.p, host / "one.bin", "/X1.BIN", 0), none);
            sdcard::AddSummary want;
            want.dest = "/X1.BIN";
            check("SDRA-01", "a card-1 add lands: the file is on card 1 with its bytes, the reply is the offline sentence byte for byte, card 2 is byte-identical",
                  r.status == 0 && r.text == sdcard::file_add_success_text((host / "one.bin").string(), a.p, "", want) &&
                      extracted(a.p, "/X1.BIN") == Tree{{"X1.BIN", kOne}} && sha_file(b.p) == sb,
                  std::to_string(r.status) + " " + r.text);
            const std::string sa = sha_file(a.p);
            const SdFileAddReply r2 = sm::remote_add(emu, req(b.p, host / "two.bin", "/NEW/X2.BIN", 1), none);
            check("SDRA-02", "a card-2 add (slot 1) lands on card 2, directories created, and card 1 is byte-identical",
                  r2.status == 0 && extracted(b.p, "/NEW/X2.BIN") == Tree{{"X2.BIN", kTwo}} && sha_file(a.p) == sa, r2.text);
            const std::string sa2 = sha_file(a.p), sb2 = sha_file(b.p);
            const SdFileAddReply m1 = sm::remote_add(emu, req(a.p, host / "one.bin", "/M1.BIN", 1), none);
            const SdFileAddReply m2 = sm::remote_add(emu, req(b.p, host / "one.bin", "/M2.BIN", 0), none);
            check("SDRA-03", "a slot mismatch is refused both ways, 5, 'is card 1' / 'is card 2', and nothing is written",
                  m1.status == 5 && m1.text == a.p + " is card 1 in the running jnext" && m2.status == 5 &&
                      m2.text == b.p + " is card 2 in the running jnext" && sha_file(a.p) == sa2 && sha_file(b.p) == sb2,
                  m1.text + " | " + m2.text);
            Img third(G);
            const SdFileAddReply nm = sm::remote_add(emu, req(third.p, host / "one.bin", "/N.BIN"), none);
            emu.sd_card2().unmount();
            const SdFileAddReply em = sm::remote_add(emu, req(b.p, host / "one.bin", "/N.BIN"), none);
            check("SDRA-04", "an image mounted in neither slot (a third image, and one whose slot is now empty) is refused, 5, 'not mounted'",
                  nm.status == 5 && nm.text.find("is not mounted in this jnext") != std::string::npos && em.status == 5 &&
                      em.text.find("is not mounted in this jnext") != std::string::npos && !on_card(third.p, "/N.BIN"),
                  nm.text + " | " + em.text);
            unmount(emu);
        }
        {
            Img a(G);
            Emulator emu;
            EmulatorConfig cfg;
            cfg.type = MachineType::ZX48K;
            emu.init(cfg);
            emu.sd_card().mount(a.p, true);   // --sdcard-readonly
            const std::string sa = sha_file(a.p);
            const SdFileAddReply r = sm::remote_add(emu, req(a.p, host / "one.bin", "/RO1.BIN"), none);
            check("SDRA-05", "a write-protected slot is refused, 5, with the manager's sentence, nothing written",
                  r.status == 5 && r.text == "the card is read-only in the running jnext: the card is write-protected in this session (--sdcard-readonly)" &&
                      sha_file(a.p) == sa, r.text);
            emu.sd_card().unmount();
            emu.sd_card().mount(a.p, false);
            emu.sd_card().set_read_overlay(100, 4, [](uint32_t, uint8_t*) { return false; });
            const SdFileAddReply o = sm::remote_add(emu, req(a.p, host / "one.bin", "/RO2.BIN"), none);
            check("SDRA-06", "a slot under a NEX read overlay is refused, 5, with the emulator's own change-refusal reason",
                  o.status == 5 && o.text.find("read-only in the running jnext: ") != std::string::npos &&
                      o.text.find(emu.sd_card_change_refusal(0)) != std::string::npos && !emu.sd_card_change_refusal(0).empty() &&
                      sha_file(a.p) == sa, o.text);
            emu.sd_card().unmount();
        }
        {
            const fs::path d = fresh_dir("ra-journal");
            const std::string ia = (d / "a.img").string(), w = (d / "a.sdw").string(), w2 = (d / "other.sdw").string();
            fs::copy_file(G, ia, fs::copy_options::overwrite_existing);
            sd_write_journal::Binding bd;
            bd.bytes = fs::file_size(ia);
            bd.sha256.fill(0x5C);
            std::string why;
            const bool bound = sd_write_journal::bind(ia, w, bd, false, why);
            Emulator emu;
            mount2(emu, ia, none);
            const std::string si = sha_file(ia);
            SdFileAddRequest q = req(ia, host / "one.bin", "/J1.BIN");
            const SdFileAddReply r = sm::remote_add(emu, q, none);
            const auto info = sd_write_journal::peek_file(w);
            SdFileAddRequest q2 = req(ia, host / "two.bin", "/J2.BIN");
            q2.writes_given = true;
            q2.writes = (d / "sub" / ".." / "a.sdw").string();
            fs::create_directories(d / "sub");
            const SdFileAddReply r2 = sm::remote_add(emu, q2, none);
            check("SDRA-07", "with a writes file bound, an add with no writes option lands in it and leaves the image byte-identical, and the same file spelt dir/../W is accepted",
                  bound && r.status == 0 && r.text.find(", kept in " + w) != std::string::npos && info.is_journal && !info.sectors.empty() &&
                      sha_file(ia) == si && r2.status == 0, why + r.text + r2.text);
            const std::string wsha = sha_file(w);
            SdFileAddRequest q3 = req(ia, host / "one.bin", "/J3.BIN");
            q3.writes_given = true;
            q3.writes = w2;
            const SdFileAddReply r3 = sm::remote_add(emu, q3, none);
            check("SDRA-08", "a request naming another writes file than the slot's is refused, 5, naming the slot's file; neither file nor image changes",
                  r3.status == 5 && r3.text.find("keeps this card's writes in " + w) != std::string::npos && sha_file(w) == wsha &&
                      sha_file(ia) == si && !fs::exists(w2), r3.text);
            unmount(emu);
            sd_write_journal::forget_all_for_test();
            Img plain(G);
            Emulator emu2;
            mount2(emu2, plain.p, none);
            SdFileAddRequest q4 = req(plain.p, host / "one.bin", "/J4.BIN");
            q4.writes_given = true;
            q4.writes = w2;
            const std::string sp = sha_file(plain.p);
            const SdFileAddReply r4 = sm::remote_add(emu2, q4, none);
            check("SDRA-09", "a request naming a writes file for a slot that has none is refused, 5, 'no writes file', nothing written",
                  r4.status == 5 && r4.text.find("with no writes file") != std::string::npos && sha_file(plain.p) == sp && !fs::exists(w2),
                  r4.text);
            unmount(emu2);
        }
        {
            Img a(G);
            Emulator emu;
            mount2(emu, a.p, none);
            const std::string sa = sha_file(a.p);
            const SdFileAddReply self = sm::remote_add(emu, req(a.p, fs::path(a.p), "/SELF.IMG"), none);
            const SdFileAddReply folder = sm::remote_add(emu, req(a.p, fs::path(a.p).parent_path(), "/FOLDER"), none);
            check("SDRA-10", "the mounted image itself, and the folder holding it, are refused as the source: 2, 'a file the emulator is using', nothing written",
                  self.status == 2 && self.text.find("is, or holds, a file the emulator is using") != std::string::npos &&
                      folder.status == 2 && folder.text.find("is, or holds, a file the emulator is using") != std::string::npos &&
                      sha_file(a.p) == sa, self.text + " | " + folder.text);
            write_host_file(host / "A.BIN", kNewA);
            SdFileAddRequest q = req(a.p, host / "A.BIN", "/MYDIR/A.BIN");
            const SdFileAddReply ex = sm::remote_add(emu, q, none);
            const std::string s1 = sha_file(a.p);
            q.force = true;
            const SdFileAddReply fo = sm::remote_add(emu, q, none);
            check("SDRA-11", "an existing destination without force is 4 and untouched; with force it is replaced",
                  ex.status == 4 && s1 == sa && fo.status == 0 && extracted(a.p, "/MYDIR/A.BIN") == Tree{{"A.BIN", kNewA}},
                  ex.text + " | " + fo.text);
            const SdFileAddReply hi = sm::remote_add(emu, req(a.p, host / "one.bin", "/CAF\xE9.TXT"), none);
            check("SDRA-12", "a card path with a byte above 0x7E is 3 as offline (string mode, never byte mode)",
                  hi.status == 3 && !on_card(a.p, "/CAF\xE9.TXT"), hi.text);
            const Tree odd_before = extracted(a.p, "/ODD");
            const SdFileAddReply al = sm::remote_add(emu, req(a.p, host / "one.bin", "/ODD/NAME~1"), none);
            check("SDRA-13", "a name that is another entry's 8.3 alias is refused, 3, and the other entries are untouched",
                  al.status == 3 && extracted(a.p, "/ODD") == odd_before && !on_card(a.p, "/ODD/one.bin"),
                  std::to_string(al.status) + " " + al.text);
            const std::string before_busy = sha_file(a.p);
            const SdFileAddReply bz = sm::remote_add(emu, req(a.p, host / "one.bin", "/BUSY.BIN"), "an upgrade or a go back");
            check("SDRA-14", "a non-empty busy reason is refused, 5, 'a card operation owns the SD card', nothing written",
                  bz.status == 5 && bz.text.find("a card operation owns the SD card (an upgrade or a go back)") == 0 &&
                      sha_file(a.p) == before_busy, bz.text);
            const SdFileAddReply miss = sm::remote_add(emu, req(a.p, host / "nope.bin", "/MISS.BIN"), none);
            check("SDRA-15", "a missing host file is 2 with the engine's text, nothing written",
                  miss.status == 2 && !miss.text.empty() && sha_file(a.p) == before_busy, miss.text);
            unmount(emu);
        }
    }

    std::error_code ec;
    fs::permissions(g_dir, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(g_dir, ec);
    check("SDMT-20", "the fixture cards are real volumes of the types the rows claim (FAT32 partitioned, FAT16 partitioned)",
          gt == FS_FAT32 && t16 == FS_FAT16);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
