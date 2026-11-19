// The second SD card in a `.jns` (GH #505, stage 3): `media.sdcard2` and the
// `state/sdcard2.json` member.
//
// Oracles: the first card's own rules (design NEXT-SNAPSHOT-FORMAT.md 11.3, rows
// JNSI-*): a card the snapshot was taken on must be the card mounted now, because
// NextZXOS keeps D:'s FAT state in RAM and a restore onto another card corrupts it on
// the next write; a snapshot without a card is not refused for one that has appeared
// (a warning); two unpopulated identities are two absences, not one card. And the
// owner rule: the slot is OPTIONAL, so a snapshot taken without a second card (every
// snapshot older than the slot) has no `media.sdcard2` and no `state/sdcard2.json` and
// loads exactly as before.
//
// Container rows build their files by hand (no emulator). The Emulator rows build a
// tiny FAT32 image at run time (the first card is never needed: card 0 is empty).
//
// Output follows the project-wide line: Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/emulator.h"
#include "core/jns_snapshot.h"
#include "peripheral/sd_card.h"
#include "save/jns_container.h"
#include "save/zip_archive.h"
#include "../row_id.h"
#include "../test_portable.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using jnext::jns::Manifest;
using jnext::jns::ReaderEnv;
using jnext::jns::SdIdentity;
using jnext::jns::SnapshotWriter;
using jnext::jns::Verdict;

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

std::string det(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

const char* kJson = "{\n  \"pc\": 312\n}\n";

ReaderEnv make_env() {
    ReaderEnv e;
    e.machine              = "next";
    e.state_model_revision = 1;
    return e;
}

Manifest make_manifest() {
    Manifest m;
    m.created                    = "2026-11-18T10:11:12Z";
    m.producer.jnext_version     = "1.3.20";
    m.producer.platform          = "linux-x86_64";
    m.model.state_model_revision = 1;
    m.model.machine              = "next";
    m.model.ram_kb               = 2048;
    m.model.timing               = "next";
    m.model.cpu_speed_nr07       = 3;
    m.capture.frame              = 100;
    m.capture.frame_boundary     = true;
    return m;
}

SdIdentity make_identity(const char* volid = "1a2b3c4d") {
    SdIdentity i;
    i.image_bytes     = 67108864ull;
    i.mbr_partition_table_sha256 = std::string(64, 'a');
    i.fat32_volume_id = volid;
    i.partition_lba   = 2048;
    return i;
}

void fill_card(jnext::jns::SdCardInfo& c, const char* path, const SdIdentity& id, bool ro,
               const char* vollab = "SECOND     ") {
    c.present      = true;
    c.mounted_path = path;
    c.identity     = id;
    c.read_only    = ro;
    c.vollab       = vollab;
}

// A valid .jns whose manifest carries what the caller put in `m`.
std::vector<uint8_t> build(const Manifest& m) {
    SnapshotWriter w(false);
    w.set_manifest(m);
    std::string why;
    w.add_subsystem("cpu", kJson, why);
    std::vector<uint8_t> z;
    w.finish(z, why);
    return z;
}

struct Opened {
    bool     ok = false;
    Manifest m;
    Verdict  v;
};

Opened open_with(const std::vector<uint8_t>& z, const ReaderEnv& e) {
    Opened o;
    jnext::zip::Reader r;
    o.ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r, o.m, o.v);
    return o;
}

bool has_warning(const Verdict& v, const std::string& token) {
    return std::any_of(v.warnings.begin(), v.warnings.end(), [&](const std::string& s) {
        return s.find(token) != std::string::npos;
    });
}

// ---- a tiny FAT32 image (the first sectors only; sparse), as sd_identity_test makes one ----
void wr16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

bool make_image(const std::string& path, uint32_t volid) {
    constexpr uint32_t kPartLba = 2048, kTotal = 8192;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    uint8_t mbr[512] = {0};
    for (size_t i = 0; i < 0x1BE; ++i) mbr[i] = static_cast<uint8_t>(i * 7 + 3);
    uint8_t* pe = mbr + 0x1BE;
    pe[0] = 0x80; pe[4] = 0x0C;
    wr32(pe + 8, kPartLba); wr32(pe + 12, kTotal);
    mbr[510] = 0x55; mbr[511] = 0xAA;
    f.write(reinterpret_cast<const char*>(mbr), 512);
    std::vector<char> gap(static_cast<size_t>(kPartLba - 1) * 512, 0);
    f.write(gap.data(), static_cast<std::streamsize>(gap.size()));
    uint8_t bpb[512] = {0};
    bpb[0] = 0xEB; bpb[1] = 0x58; bpb[2] = 0x90;
    std::memcpy(bpb + 3, "MSDOS5.0", 8);
    wr16(bpb + 11, 512); bpb[13] = 16; wr16(bpb + 14, 32); bpb[16] = 2;
    wr32(bpb + 32, kTotal); wr32(bpb + 36, 64); wr32(bpb + 44, 2);
    bpb[0x40] = 0x80; bpb[0x42] = 0x29;
    wr32(bpb + 0x43, volid);
    std::memcpy(bpb + 0x47, "SECOND     ", 11);
    std::memcpy(bpb + 0x52, "FAT32   ", 8);
    bpb[510] = 0x55; bpb[511] = 0xAA;
    f.write(reinterpret_cast<const char*>(bpb), 512);
    f.seekp(static_cast<std::streamoff>((static_cast<uint64_t>(kPartLba) + kTotal) * 512 - 1));
    const char z = 0;
    f.write(&z, 1);
    return f.good();
}

// The first data byte of sector 0 and the next two, as the GUEST reads them (CMD17), leaving the
// card ready to stream: the card is left MID-TRANSFER on purpose when `leave_open`.
struct Stream3 { bool ok = false; uint8_t b[3] = {0, 0, 0}; };

bool start_cmd17(SdCardDevice& sd) {
    auto cmd = [&sd](uint8_t c, uint32_t arg) {
        const uint8_t bytes[6] = {static_cast<uint8_t>(0x40 | c), static_cast<uint8_t>(arg >> 24),
                                  static_cast<uint8_t>(arg >> 16), static_cast<uint8_t>(arg >> 8),
                                  static_cast<uint8_t>(arg), 0x95};
        for (uint8_t b : bytes) (void)sd.receive(b);
        for (int i = 0; i < 16; ++i) {
            const uint8_t r = sd.send();
            if (r != 0xFF) return r;
        }
        return uint8_t{0xFF};
    };
    sd.reset();
    cmd(0, 0); cmd(8, 0x1AA); cmd(55, 0); cmd(41, 0x40000000); cmd(58, 0);
    if (cmd(17, 0) != 0x00) return false;
    for (int i = 0; i < 16; ++i) if (sd.send() == 0xFE) return true;
    return false;
}

std::vector<uint8_t> zip_text(const std::vector<uint8_t>& z, const char* member, bool& found) {
    jnext::zip::Reader r;
    std::string w;
    found = false;
    if (!r.open(z.data(), z.size(), w)) return {};
    std::string text;
    if (!r.has(member)) return {};
    found = r.read_text(member, text, w);
    return std::vector<uint8_t>(text.begin(), text.end());
}

}  // namespace

int main() {
    std::printf("Second SD card in a .jns (GH #505 stage 3)\n");

    const SdIdentity id_a = make_identity("1a2b3c4d");

    // SDSLOT-40: the manifest round trip, and the absent case.
    {
        Manifest m = make_manifest();
        fill_card(m.sdcard2, "/second.img", id_a, true);
        const auto z = build(m);
        const Opened o = open_with(z, [&] { ReaderEnv e = make_env(); fill_card(e.card2, "/second.img", id_a, true); return e; }());
        bool j = false;
        const auto mf = zip_text(z, "manifest.json", j);
        const std::string text(mf.begin(), mf.end());

        Manifest plain = make_manifest();
        const Opened p = open_with(build(plain), make_env());
        check("SDSLOT-40",
              "media.sdcard2 is written and parsed back (path, read-only, the four identity fields, the "
              "informational label) and does not appear as media.sdcard; a manifest without it parses as absent",
              o.ok && o.m.sdcard2.present && !o.m.sdcard.present &&
                  o.m.sdcard2.mounted_path == "/second.img" && o.m.sdcard2.read_only &&
                  o.m.sdcard2.identity == id_a && o.m.sdcard2.vollab == "SECOND     " &&
                  text.find("\"sdcard2\"") != std::string::npos && p.ok && !p.m.sdcard2.present &&
                  p.m.sdcard2.mounted_path.empty(),
              det("ok=%d present=%d p.ok=%d", o.ok, o.m.sdcard2.present, p.ok));
    }

    // SDSLOT-41: had a second card, none mounted now: refused, naming the second card.
    {
        Manifest m = make_manifest();
        fill_card(m.sdcard2, "/second.img", id_a, false);
        const Opened o = open_with(build(m), make_env());
        check("SDSLOT-41",
              "a snapshot taken with a second SD card is refused when none is mounted, and the refusal names "
              "the second card and its path",
              !o.ok && o.v.refusal.find("second SD card") != std::string::npos &&
                  o.v.refusal.find("/second.img") != std::string::npos,
              o.v.refusal);
    }

    // SDSLOT-42: had none, one is mounted now: a warning, never a refusal; with neither, silence.
    {
        ReaderEnv e = make_env();
        fill_card(e.card2, "/second.img", id_a, false);
        const Opened o = open_with(build(make_manifest()), e);
        const Opened none = open_with(build(make_manifest()), make_env());
        check("SDSLOT-42",
              "a snapshot taken without a second card is not refused for one mounted now (a warning naming "
              "it), and with no second card on either side the verdict has no warning at all",
              o.ok && has_warning(o.v, "no second SD card") && has_warning(o.v, "/second.img") &&
                  none.ok && none.v.warnings.empty(),
              det("ok=%d warnings=%zu none.warnings=%zu", o.ok, o.v.warnings.size(), none.v.warnings.size()));
    }

    // SDSLOT-43: a different identity refuses (naming both); --snapshot-mode force downgrades it.
    {
        Manifest m = make_manifest();
        fill_card(m.sdcard2, "/second.img", id_a, false);
        const auto z = build(m);
        ReaderEnv e = make_env();
        fill_card(e.card2, "/other.img", make_identity("99887766"), false);
        const Opened o = open_with(z, e);
        e.force_sdcard = true;
        const Opened f = open_with(z, e);
        check("SDSLOT-43",
              "a different second card is refused naming BOTH identities; --snapshot-mode force turns it into "
              "a warning that still names both",
              !o.ok && o.v.refusal.find("second SD card") != std::string::npos &&
                  o.v.refusal.find("1a2b3c4d") != std::string::npos &&
                  o.v.refusal.find("99887766") != std::string::npos && f.ok &&
                  has_warning(f.v, "99887766") && has_warning(f.v, "1a2b3c4d"),
              o.v.refusal);
    }

    // SDSLOT-44: only the read-only flag differs: a warning.
    {
        Manifest m = make_manifest();
        fill_card(m.sdcard2, "/second.img", id_a, true);
        ReaderEnv e = make_env();
        fill_card(e.card2, "/second.img", id_a, false);
        const Opened o = open_with(build(m), e);
        check("SDSLOT-44",
              "a second card taken read-only and mounted writable is a warning naming the second card, "
              "not a refusal",
              o.ok && has_warning(o.v, "second card mounted read-only") && o.v.refusal.empty(),
              det("ok=%d warnings=%zu", o.ok, o.v.warnings.size()));
    }

    // SDSLOT-46: two unpopulated identities are two absences, not one card; the first card's verdict is
    // untouched by a second card (a first-card mismatch still refuses, naming the first card).
    {
        SdIdentity none;
        Manifest m = make_manifest();
        fill_card(m.sdcard2, "/second.img", none, false, "");
        ReaderEnv e = make_env();
        fill_card(e.card2, "/second.img", none, false, "");
        const Opened o = open_with(build(m), e);

        Manifest both = make_manifest();
        fill_card(both.sdcard, "/first.img", id_a, false, "NEXT       ");
        fill_card(both.sdcard2, "/second.img", id_a, false);
        ReaderEnv e2 = make_env();
        fill_card(e2.card, "/first.img", make_identity("99887766"), false, "NEXT       ");
        fill_card(e2.card2, "/second.img", id_a, false);
        const Opened b = open_with(build(both), e2);
        check("SDSLOT-46",
              "two unpopulated second-card identities are not 'the same card' (refused), and a first-card "
              "mismatch is still refused as the first card's, whatever the second card is",
              !o.ok && o.v.refusal.find("second SD card") != std::string::npos && !b.ok &&
                  b.v.refusal.find("second SD card") == std::string::npos &&
                  b.v.refusal.find("mounted SD card is not the one") != std::string::npos,
              o.v.refusal + " / " + b.v.refusal);
    }

    // ---- the Emulator: save and restore through the real writer and reader ----
    const std::string dir = jtp::make_temp_dir("jnext-sd2jns-");
    const std::string img_a = dir + "/a.img";
    const std::string img_b = dir + "/b.img";
    const bool made = !dir.empty() && make_image(img_a, 0x1A2B3C4Du) && make_image(img_b, 0x99887766u);

    auto fresh = [&](const std::string& card2) {
        auto e = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        cfg.sd_card2_image = card2;
        e->init(cfg);
        return e;
    };

    // SDSLOT-45: with a second card, the file carries media.sdcard2 and state/sdcard2.json, and a
    // restore resumes the card's CMD17 stream where the save left it.
    {
        auto src = fresh(img_a);
        for (int i = 0; i < 3; ++i) src->run_frame();
        const bool started = start_cmd17(src->sd_card2());
        jnext::JnsSaveOptions so;
        jnext::JnsLoadReport rep;
        std::string why;
        std::vector<uint8_t> z;
        const bool wrote = src->save_jns(so, z, rep, why);
        uint8_t first[3] = {src->sd_card2().send(), src->sd_card2().send(), src->sd_card2().send()};

        bool found = false;
        zip_text(z, "state/sdcard2.json", found);
        auto dst = fresh(img_a);
        (void)start_cmd17(dst->sd_card2());
        for (int i = 0; i < 40; ++i) (void)dst->sd_card2().send();   // somewhere else in the transfer
        dst->sd_card2().reset();                                     // as a fresh process would be
        jnext::JnsLoadOptions lo;
        jnext::JnsLoadReport lrep;
        const bool read = wrote && dst->load_jns(z.data(), z.size(), lo, lrep, why);
        uint8_t again[3] = {dst->sd_card2().send(), dst->sd_card2().send(), dst->sd_card2().send()};
        check("SDSLOT-45",
              "a .jns saved with a second card mounted carries media.sdcard2 and state/sdcard2.json, and the "
              "restore resumes the card mid-CMD17: the next bytes of the sector are the ones the saved machine "
              "went on to read",
              made && started && wrote && found && read && std::equal(first, first + 3, again) &&
                  first[0] == 3 && first[1] == 10 && first[2] == 17,
              det("wrote=%d found=%d read=%d first=%d,%d,%d again=%d,%d,%d %s", wrote, found, read, first[0],
                  first[1], first[2], again[0], again[1], again[2], why.c_str()));
    }

    // SDSLOT-47: owner rule (the slot is optional). A snapshot taken WITHOUT a second card has no
    // media.sdcard2 and no state/sdcard2.json (it is what an older release writes), loads onto a machine
    // without one with no new warning, and onto a machine WITH one with a single warning, not a refusal.
    {
        auto src = fresh("");
        for (int i = 0; i < 3; ++i) src->run_frame();
        jnext::JnsSaveOptions so;
        jnext::JnsLoadReport rep;
        std::string why;
        std::vector<uint8_t> z;
        const bool wrote = src->save_jns(so, z, rep, why);
        bool member = true;
        zip_text(z, "state/sdcard2.json", member);
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool opened = jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, m, v);
        bool in_list = false;
        for (const std::string& s : m.subsystems) in_list = in_list || s == "sdcard2";

        auto plain_dst = fresh("");
        jnext::JnsLoadOptions lo;
        jnext::JnsLoadReport r1;
        const bool ok1 = wrote && plain_dst->load_jns(z.data(), z.size(), lo, r1, why);
        auto card_dst = fresh(img_b);
        jnext::JnsLoadReport r2;
        const bool ok2 = wrote && card_dst->load_jns(z.data(), z.size(), lo, r2, why);
        const auto has = [](const jnext::JnsLoadReport& r, const char* t) {
            return std::any_of(r.warnings.begin(), r.warnings.end(),
                               [&](const std::string& s) { return s.find(t) != std::string::npos; });
        };
        check("SDSLOT-47",
              "a .jns saved without a second card has no media.sdcard2, no state/sdcard2.json and no sdcard2 "
              "entry in its subsystem list; it loads with no second-card warning onto a machine without one, "
              "and with exactly the 'no second SD card' warning (no refusal) onto a machine that has one",
              made && wrote && opened && !m.sdcard2.present && !member && !in_list && ok1 &&
                  !has(r1, "second") && ok2 && has(r2, "no second SD card"),
              det("wrote=%d opened=%d member=%d ok1=%d ok2=%d %s", wrote, opened, member, ok1, ok2,
                  why.c_str()));
    }

    // SDSLOT-48: restoring onto a DIFFERENT second card is refused (the real card-2 identity, through
    // the real producer); force restores with a warning.
    {
        auto src = fresh(img_a);
        for (int i = 0; i < 3; ++i) src->run_frame();
        jnext::JnsSaveOptions so;
        jnext::JnsLoadReport rep;
        std::string why, why2;
        std::vector<uint8_t> z;
        const bool wrote = src->save_jns(so, z, rep, why);
        auto dst = fresh(img_b);
        jnext::JnsLoadOptions lo;
        jnext::JnsLoadReport r1, r2;
        const bool refused = wrote && !dst->load_jns(z.data(), z.size(), lo, r1, why);
        auto dst2 = fresh(img_b);
        lo.force_sdcard = true;
        const bool forced = wrote && dst2->load_jns(z.data(), z.size(), lo, r2, why2);
        check("SDSLOT-48",
              "restoring a .jns onto a different second card (another FAT32 volume) is refused naming the "
              "second card; --snapshot-mode force restores it with a warning",
              made && wrote && refused && why.find("second SD card") != std::string::npos && forced,
              why);
    }

    if (!dir.empty()) {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
