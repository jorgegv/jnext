// The `.jns` snapshot's SD-card media identity — GH #27 stage S7.
// Oracle: doc/design/NEXT-SNAPSHOT-FORMAT.md §11 (The SD card question),
// §11.3 (the two-tier identity and the six-row restore-behaviour matrix).
//
// WHY THIS IS A SEPARATE SUITE FROM `snapshot_test`.
//
// `snapshot_test` links `jnext_save` ALONE, deliberately (design §16.1), so
// the container's rules are provable without an emulator, a card or a
// filesystem — and its `JNSI-01..17` rows prove exactly that: fed two
// hand-written identities, the reader refuses, warns or stays silent per
// §11.3's matrix. What those rows structurally cannot prove is that the
// identity handed to them DESCRIBES THE CARD. That is this suite's job, and
// it needs `jnext_core` (the FAT32 parser) and real bytes on disk.
//
// The distinction matters because it is where the failure would live. A
// producer that returned a constant would pass every `JNSI` row: both sides
// would agree, every restore would be silent, and the whole mechanism would be
// decorative. So the rows below are written the other way round — each one
// mutates ONE thing in a real image and asserts what must and must NOT move.
//
//   JNSI-P01..P06  the five Tier-1 fields, read from a real image, plus the
//                  serial's fixed WIDTH (only a leading-zero serial shows it)
//   JNSI-P07..P12  the DISCRIMINATIVE pairs: what changes the identity and,
//                  more importantly, what must not
//   JNSI-P13..P14  the boot sector without an extended signature, and a label
//                  that is not printable ASCII
//   JNSI-P15..P19  the refusals, each NAMING the defect
//   JNSI-P20..P26b `describe_sdcard_for_snapshot` and the Tier-2 stamp: both
//                  tiers, the lazy variant, the UTC rendering pinned to one
//                  known epoch, and the two failures that must not half-fill
//                  their outputs
//   JNSI-P27..P31b end-to-end through the real reader, against real images —
//                  §16.2's three legs at the unit tier, plus --force. Leg 3
//                  carries a correction to the design that writing it found —
//                  see JNSI-P31.
//   JNSI-P32..P34  the canonical 1 GB image: Tier 1 populated, the MEASURED
//                  cost §11.3 asks for, and Tier 2 equal to
//                  `sdcard::sha256_file` (the "reused verbatim" claim)
//
// `--verdict SNAP_IMG CUR_IMG [--mid-transfer] [--force]` is the same code
// path as a driver for `snapshot-sdcard-mismatch-func`: it exits non-zero on a
// refusal, which is what that row asserts.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/sd_rom_extractor.h"
#include "core/sd_snapshot_identity.h"
#include "core/sdcard_provisioner.h"
#include "save/jns_container.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>  // utimensat (POSIX)
#include <fcntl.h>     // AT_FDCWD
#include <unistd.h>   // getpid (POSIX)

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
        std::fflush(stdout);
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

// ── Scratch images ───────────────────────────────────────────────────────

std::vector<std::string> g_scratch;

std::string tmp_path(const char* name) {
    const char* base = std::getenv("TMPDIR");
    const std::string dir = (base && *base) ? base : "/tmp";
    std::string p = dir + "/jnext_sdid_" + std::to_string(::getpid()) + "_" +
                    name + ".img";
    g_scratch.push_back(p);
    return p;
}

void cleanup_scratch() {
    for (const std::string& p : g_scratch) std::remove(p.c_str());
}

void wr16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v);
                                    p[1] = static_cast<uint8_t>(v >> 8); }
void wr32(uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v);
                                    p[1] = static_cast<uint8_t>(v >> 8);
                                    p[2] = static_cast<uint8_t>(v >> 16);
                                    p[3] = static_cast<uint8_t>(v >> 24); }

constexpr uint32_t kPartLba      = 2048;
constexpr uint32_t kTotalSectors = 8192;        // 4 MB image, sparse on disk
constexpr uint32_t kVolIdValue   = 0x1A2B3C4Du;

// A hand-built MBR + FAT32 boot sector.
//
// Hand-built RATHER THAN produced by FatFs, and that is the point: a spec-valid
// 8 KB-cluster FAT32 needs a >= 540 MB partition (see fat32_image_test), which
// no unit row should be writing, and none of the fields Tier 1 reads depends on
// the volume being mountable. What the rows need is the ability to move ONE
// byte at a time, which is exactly what a formatter takes away.
//
// Every field the parser validates is filled with a legal value, so a row that
// mutates one of them is testing that mutation and nothing else.
bool make_image(const std::string& path, uint8_t part_type = 0x0C,
                uint8_t boot_sig = 0x29, uint32_t volid = kVolIdValue,
                const char* vollab = "NEXT       ",
                uint8_t sectors_per_cluster = 16) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    uint8_t mbr[512] = {0};
    // Bootstrap code, 0x000-0x1BD. Deliberately NON-ZERO: JNSI-P08 mutates it
    // and requires the identity not to move, and a zero-filled region would let
    // that row pass against a digest that simply never saw the bytes.
    for (size_t i = 0; i < 0x1BE; ++i) mbr[i] = static_cast<uint8_t>(i * 7 + 3);
    uint8_t* pe = mbr + 0x1BE;
    pe[0] = 0x80;                 // bootable
    pe[4] = part_type;
    wr32(pe + 8, kPartLba);
    wr32(pe + 12, kTotalSectors);
    mbr[510] = 0x55; mbr[511] = 0xAA;
    f.write(reinterpret_cast<const char*>(mbr), 512);

    // Zero padding up to the partition's first sector.
    std::vector<char> gap(static_cast<size_t>(kPartLba - 1) * 512, 0);
    f.write(gap.data(), static_cast<std::streamsize>(gap.size()));

    uint8_t bpb[512] = {0};
    bpb[0] = 0xEB; bpb[1] = 0x58; bpb[2] = 0x90;        // jump
    std::memcpy(bpb + 3, "MSDOS5.0", 8);                 // OEM name
    wr16(bpb + 11, 512);                                 // bytes per sector
    bpb[13] = sectors_per_cluster;
    wr16(bpb + 14, 32);                                  // reserved sectors
    bpb[16] = 2;                                         // num FATs
    wr16(bpb + 17, 0);                                   // root entries (FAT32)
    wr32(bpb + 32, kTotalSectors);                       // total sectors 32
    wr32(bpb + 36, 64);                                  // FATSz32
    wr32(bpb + 44, 2);                                   // root cluster
    bpb[0x40] = 0x80;                                    // BS_DrvNum
    bpb[0x42] = boot_sig;                                // BS_BootSig
    wr32(bpb + 0x43, volid);                             // BS_VolID
    std::memcpy(bpb + 0x47, vollab, 11);                 // BS_VolLab
    std::memcpy(bpb + 0x52, "FAT32   ", 8);              // BS_FilSysType
    bpb[510] = 0x55; bpb[511] = 0xAA;
    f.write(reinterpret_cast<const char*>(bpb), 512);

    // Extend to the full image length without writing it: the rows only ever
    // touch the MBR, the BPB and one far-away data sector.
    const uint64_t bytes =
        static_cast<uint64_t>(kPartLba + kTotalSectors) * 512;
    f.seekp(static_cast<std::streamoff>(bytes - 1));
    const char z = 0;
    f.write(&z, 1);
    return f.good();
}

bool copy_image(const std::string& from, const std::string& to) {
    std::ifstream i(from, std::ios::binary);
    std::ofstream o(to, std::ios::binary | std::ios::trunc);
    if (!i || !o) return false;
    o << i.rdbuf();
    return o.good();
}

// Flip one byte at an absolute offset. The whole discriminative half of this
// suite is built on it, so it reports failure rather than being assumed.
bool poke(const std::string& path, uint64_t offset, uint8_t value) {
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) return false;
    f.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    f.write(reinterpret_cast<const char*>(&value), 1);
    return f.good();
}

bool is_lower_hex(const std::string& s, size_t want_len) {
    if (s.size() != want_len) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

/// `YYYY-MM-DDTHH:MM:SSZ`, checked as a shape rather than against a clock.
bool is_iso8601_utc(const std::string& s) {
    if (s.size() != 20) return false;
    static const char* pat = "dddd-dd-ddTdd:dd:ddZ";
    for (size_t i = 0; i < s.size(); ++i) {
        if (pat[i] == 'd') { if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false; }
        else if (s[i] != pat[i]) return false;
    }
    return true;
}

std::string canonical_image() {
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") +
           "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

// ── The end-to-end seam, shared with `--verdict` ──────────────────────────

using jnext::jns::Manifest;
using jnext::jns::ReaderEnv;
using jnext::jns::Verdict;

/// Write a `.jns` whose manifest describes `snap_image`, then open it against
/// `cur_image`. The ONE path both the rows below and the functional row's
/// driver take, so the row and the shipped behaviour cannot drift apart.
bool run_verdict(const std::string& snap_image, const std::string& cur_image,
                 bool mid_transfer, bool force, Verdict& v, std::string& why) {
    jnext::jns::SdCardInfo snap_card;
    // `describe_...` returns false ONLY when Tier 1 could not be read; a
    // Tier-2 failure returns true with `why` set, which is a card this suite
    // can still run a verdict against.
    if (!jnext::describe_sdcard_for_snapshot(snap_image, false, snap_card,
                                             why)) {
        return false;
    }

    Manifest m;
    m.created                = "2026-09-24T00:00:00Z";
    m.producer.jnext_version = "s7";
    m.producer.platform      = "linux-x86_64";
    m.model.machine          = "next";
    m.model.ram_kb           = 2048;
    m.capture.frame          = 1;
    m.capture.frame_boundary = true;
    m.sdcard                 = snap_card;

    jnext::jns::SnapshotWriter w(false);
    w.set_manifest(m);
    if (!w.add_subsystem("cpu", "{\"pc\":0}", why)) return false;
    std::vector<uint8_t> z;
    if (!w.finish(z, why)) return false;

    ReaderEnv e;
    e.machine               = "next";
    e.sd_transfer_in_flight = mid_transfer;
    e.force_sdcard          = force;
    if (!jnext::describe_sdcard_for_snapshot(cur_image, false, e.card, why)) {
        return false;
    }

    jnext::zip::Reader r;
    Manifest got;
    jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
    return true;
}

/// `--make-image PATH` — write the very image `make_image` builds for the rows
/// above, and print the byte offsets a caller must poke to move each field.
///
/// The offsets are PRINTED rather than documented, so `snapshot-sdcard-mismatch-func`
/// can mutate the image without a second copy of the FAT32 layout living in
/// bash. A shell-side constant would go stale the day this fixture moves its
/// partition, and would do it silently — the row would poke a byte nothing
/// reads and still pass.
int make_image_main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: sd_identity_test --make-image PATH\n");
        return 2;
    }
    const std::string path = argv[2];
    if (!make_image(path)) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return 2;
    }
    g_scratch.clear();   // the caller owns this file, not the scratch cleaner
    const uint64_t bpb = static_cast<uint64_t>(kPartLba) * 512;
    std::printf("IMAGE %s\n", path.c_str());
    std::printf("OFFSET_DATA %llu\n",
                static_cast<unsigned long long>(bpb + 4000ull * 512));
    std::printf("OFFSET_VOLID %llu\n",
                static_cast<unsigned long long>(bpb + 0x43));
    std::printf("OFFSET_VOLLAB %llu\n",
                static_cast<unsigned long long>(bpb + 0x47));
    std::printf("VOLID %s\n", "1a2b3c4d");
    return 0;
}

int verdict_main(int argc, char** argv) {
    std::string snap, cur;
    bool mid = false, force = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--mid-transfer") mid = true;
        else if (a == "--force")   force = true;
        else if (snap.empty())     snap = a;
        else if (cur.empty())      cur = a;
    }
    if (snap.empty() || cur.empty()) {
        std::fprintf(stderr,
                     "usage: sd_identity_test --verdict SNAP_IMG CUR_IMG "
                     "[--mid-transfer] [--force]\n");
        return 2;
    }
    Verdict v;
    std::string why;
    if (!run_verdict(snap, cur, mid, force, v, why)) {
        std::fprintf(stderr, "HARNESS: %s\n", why.c_str());
        return 2;
    }
    std::printf("VERDICT: %s\n", v.ok ? "ok" : "refused");
    if (!v.refusal.empty()) std::printf("REFUSAL: %s\n", v.refusal.c_str());
    for (const std::string& w : v.warnings) {
        std::printf("WARNING: %s\n", w.c_str());
    }
    std::fflush(stdout);
    return v.ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--verdict") == 0) {
        return verdict_main(argc, argv);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--make-image") == 0) {
        return make_image_main(argc, argv);
    }

    const std::string real = canonical_image();
    {
        std::ifstream probe(real, std::ios::binary);
        if (!probe) {
            // A missing fixture is a FATAL harness fault, not a skip — the
            // precedent is sd_rom_extractor_test, and the reason is the same:
            // turning it into skips makes the suite's row count depend on the
            // environment, and a smaller still-green number is how a whole
            // suite once went missing (Task 37).
            std::fprintf(stderr,
                "\nFATAL: SD image fixture missing: %s\n"
                "  This suite cannot run without it, and will NOT pretend to.\n"
                "  Provision it with: jnext --headless --sdcard-download-confirm\n"
                "  Or point JNEXT_TEST_SD_IMAGE at the canonical 1 GB image.\n\n",
                real.c_str());
            return 1;
        }
    }

    const std::string base_img = tmp_path("base");
    if (!make_image(base_img)) {
        std::fprintf(stderr, "\nFATAL: cannot write scratch image %s\n\n",
                     base_img.c_str());
        cleanup_scratch();
        return 1;
    }

    SdImageIdentity base;
    std::string why;
    const bool base_ok = read_sd_image_identity(base_img, base, why);

    // ─────────────────────────────────────────────────────────────────────
    // JNSI-P01..P06 — the five Tier-1 fields, and the serial's fixed width
    // ─────────────────────────────────────────────────────────────────────
    check("JNSI-P01",
          "a well-formed MBR + FAT32 image is read, and `image_bytes` is the "
          "file's real size",
          base_ok &&
              base.image_bytes ==
                  static_cast<uint64_t>(kPartLba + kTotalSectors) * 512,
          det("ok=%d bytes=%llu why='%s'", base_ok,
              static_cast<unsigned long long>(base.image_bytes), why.c_str()));

    check("JNSI-P02",
          "`partition_lba` is the FAT32-LBA partition's start sector from the "
          "MBR table, not a constant",
          base_ok && base.partition_lba == kPartLba,
          det("lba=%u", base.partition_lba));

    check("JNSI-P03",
          "`fat32_volume_id` is BS_VolID (BPB 0x43, little-endian) as exactly "
          "8 lower-case hex digits — the genuinely stable identifier the "
          "Tier-1 refusal rests on (§11.3)",
          base_ok && base.fat32_volume_id == "1a2b3c4d" &&
              is_lower_hex(base.fat32_volume_id, 8),
          det("volid='%s'", base.fat32_volume_id.c_str()));

    check("JNSI-P04",
          "`fat32_bs_vollab` is BS_VolLab (BPB 0x47), all 11 bytes, with the "
          "trailing spaces PRESERVED — trimming would normalise two different "
          "labels into one",
          base_ok && base.fat32_bs_vollab == "NEXT       " &&
              base.fat32_bs_vollab.size() == 11,
          det("vollab='%s' len=%zu", base.fat32_bs_vollab.c_str(),
              base.fat32_bs_vollab.size()));

    {
        // THE WIDTH IS PART OF THE CONTRACT, and only a serial with leading
        // zeros can say so. `%x` and `%08x` render 0x1A2B3C4D identically, so
        // JNSI-P03 alone cannot tell them apart — and a variable-width serial
        // is not a cosmetic defect: it makes 0x0000ABCD and 0xABCD (a serial
        // this fixture could equally have) render to the SAME eight characters
        // in a field the Tier-1 refusal rests on.
        const std::string m = tmp_path("volid-leading-zeros");
        SdImageIdentity id;
        std::string w;
        const bool prepared = make_image(m, 0x0C, 0x29, 0x000000FFu);
        const bool read = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P05",
              "a BS_VolID with leading zeros keeps its full width: 0x000000FF "
              "is `000000ff`, eight characters, never `ff`",
              read && id.fat32_volume_id == "000000ff",
              det("read=%d volid='%s'", read, id.fat32_volume_id.c_str()));
    }

    check("JNSI-P06",
          "`mbr_sha256` is 64 lower-case hex characters",
          base_ok && is_lower_hex(base.mbr_sha256, 64),
          det("mbr='%s'", base.mbr_sha256.c_str()));

    // ─────────────────────────────────────────────────────────────────────
    // JNSI-P07..P12 — the discriminative pairs
    // ─────────────────────────────────────────────────────────────────────
    //
    // This is the half a constant-returning producer fails. Each row moves ONE
    // byte and asserts what must move with it — and, for the three that matter
    // most, what must NOT.
    {
        const std::string m = tmp_path("mbr-table");
        SdImageIdentity id;
        std::string w;
        const bool prepared = copy_image(base_img, m) &&
                              poke(m, 0x1BE + 12, 0x77);   // partition SIZE field
        const bool read = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P07",
              "changing a byte of the MBR PARTITION TABLE changes `mbr_sha256` "
              "— the partitioning is part of the card's identity",
              read && id.mbr_sha256 != base.mbr_sha256,
              det("prepared=%d read=%d same=%d", prepared, read,
                  read && id.mbr_sha256 == base.mbr_sha256));
    }
    {
        const std::string m = tmp_path("mbr-boot");
        SdImageIdentity id;
        std::string w;
        const bool prepared = copy_image(base_img, m) && poke(m, 0x40, 0xEE);
        const bool read = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P08",
              "changing a byte of the MBR BOOTSTRAP CODE (offset < 0x1BE) does "
              "NOT change `mbr_sha256`. `fdisk`, `syslinux` and several imaging "
              "tools rewrite it without touching the partitioning, and a "
              "refusal on that would be the cries-wolf failure §11.1 exists to "
              "avoid — the same argument that keeps BS_VolLab out of the test",
              read && id.mbr_sha256 == base.mbr_sha256,
              det("read=%d mbr='%s'", read, id.mbr_sha256.c_str()));
    }
    {
        const std::string m = tmp_path("volid");
        SdImageIdentity id;
        std::string w;
        const bool prepared = copy_image(base_img, m) &&
                              poke(m, static_cast<uint64_t>(kPartLba) * 512 + 0x43,
                                   0x99);
        const bool read = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P09",
              "changing BS_VolID changes `fat32_volume_id` and nothing else in "
              "the identity",
              read && id.fat32_volume_id == "1a2b3c99" &&
                  id.mbr_sha256 == base.mbr_sha256 &&
                  id.image_bytes == base.image_bytes &&
                  id.partition_lba == base.partition_lba,
              det("read=%d volid='%s'", read, id.fat32_volume_id.c_str()));
    }
    {
        const std::string m = tmp_path("vollab");
        SdImageIdentity id;
        std::string w;
        const bool prepared = copy_image(base_img, m) &&
                              poke(m, static_cast<uint64_t>(kPartLba) * 512 + 0x47,
                                   'X');
        const bool read = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P10",
              "changing BS_VolLab changes ONLY the informational label: every "
              "Tier-1 field is byte-identical, so a tool that rewrites the "
              "label cannot produce a refusal on the same physical card (§11.3)",
              read && id.fat32_bs_vollab == "XEXT       " &&
                  id.fat32_volume_id == base.fat32_volume_id &&
                  id.mbr_sha256 == base.mbr_sha256 &&
                  id.image_bytes == base.image_bytes &&
                  id.partition_lba == base.partition_lba,
              det("read=%d vollab='%s'", read, id.fat32_bs_vollab.c_str()));
    }
    {
        const std::string m = tmp_path("grown");
        SdImageIdentity id;
        std::string w;
        bool prepared = copy_image(base_img, m);
        if (prepared) {
            std::ofstream f(m, std::ios::binary | std::ios::app);
            const char pad[512] = {0};
            f.write(pad, 512);
            prepared = f.good();
        }
        const bool read = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P11",
              "growing the image changes `image_bytes` and leaves the other "
              "four fields alone — a resized card is a different card, and the "
              "row says which field noticed",
              read && id.image_bytes == base.image_bytes + 512 &&
                  id.mbr_sha256 == base.mbr_sha256 &&
                  id.fat32_volume_id == base.fat32_volume_id,
              det("read=%d bytes=%llu", read,
                  static_cast<unsigned long long>(id.image_bytes)));
    }
    {
        // THE ROW THE WHOLE TIERING EXISTS FOR. jnext opens the image
        // read-write and persists guest writes, so a data sector changing is
        // the NORMAL case, not the alarming one.
        const std::string m = tmp_path("datasector");
        SdImageIdentity id;
        std::string w;
        const uint64_t far = static_cast<uint64_t>(kPartLba + 4000) * 512;
        const bool prepared = copy_image(base_img, m) && poke(m, far, 0x5A);
        const bool read = prepared && read_sd_image_identity(m, id, w);
        const bool unchanged =
            read && id.image_bytes == base.image_bytes &&
            id.mbr_sha256 == base.mbr_sha256 &&
            id.fat32_volume_id == base.fat32_volume_id &&
            id.partition_lba == base.partition_lba &&
            id.fat32_bs_vollab == base.fat32_bs_vollab;
        check("JNSI-P12",
              "writing a DATA SECTOR leaves the whole Tier-1 identity "
              "byte-identical. This is what makes the identity usable at all: "
              "jnext persists guest writes, so a Tier-1 that moved here would "
              "refuse against the very card the snapshot was taken on (§11.1)",
              unchanged, det("read=%d", read));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSI-P13..P14 — the boot sector's extended fields
    // ─────────────────────────────────────────────────────────────────────
    {
        const std::string m = tmp_path("nobootsig");
        SdImageIdentity id;
        std::string w;
        const bool prepared = make_image(m, 0x0C, 0x00);
        const bool read = prepared && read_sd_image_identity(m, id, w);

        jnext::jns::SdIdentity as_jns;
        as_jns.image_bytes     = id.image_bytes;
        as_jns.mbr_sha256      = id.mbr_sha256;
        as_jns.fat32_volume_id = id.fat32_volume_id;
        as_jns.partition_lba   = id.partition_lba;

        check("JNSI-P13",
              "a boot sector with no extended signature (BS_BootSig != 0x29) "
              "leaves the serial and the label EMPTY rather than reporting "
              "whatever bytes sit there — the FAT specification does not define "
              "them, and `SdIdentity::populated()` is then false, so the reader "
              "refuses instead of trusting an identity nothing stands behind",
              read && id.fat32_volume_id.empty() &&
                  id.fat32_bs_vollab.empty() && !as_jns.populated() &&
                  id.image_bytes != 0 && !id.mbr_sha256.empty(),
              det("read=%d volid='%s' populated=%d", read,
                  id.fat32_volume_id.c_str(), as_jns.populated()));
    }
    {
        const std::string m = tmp_path("binarylabel");
        SdImageIdentity id;
        std::string w;
        // Exactly 11 bytes, two of them outside printable ASCII.
        const bool prepared = make_image(m, 0x0C, 0x29, kVolIdValue,
                                         "N\xE9XT\x01" "ZZZZZZ");
        const bool read = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P14",
              "a BS_VolLab byte outside printable ASCII becomes '?'. The label "
              "is OEM-charset on disk and this string lands in `manifest.json`, "
              "which must be valid UTF-8 — carrying the raw bytes would make a "
              "high-byte label produce an unparseable manifest",
              read && id.fat32_bs_vollab == "N?XT?ZZZZZZ" &&
                  id.fat32_bs_vollab.size() == 11,
              det("read=%d vollab='%s'", read, id.fat32_bs_vollab.c_str()));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSI-P15..P19 — the refusals, each NAMING the defect
    // ─────────────────────────────────────────────────────────────────────
    //
    // G9 is a testable property, not a slogan: a refusal reading only "cannot
    // read the image" is a failing row.
    {
        SdImageIdentity id;
        std::string w;
        const std::string missing = tmp_path("does-not-exist");
        std::remove(missing.c_str());
        const bool ok = read_sd_image_identity(missing, id, w);
        check("JNSI-P15",
              "a missing image refuses, and the message NAMES the path",
              !ok && w.find(missing) != std::string::npos, w);
    }
    {
        const std::string m = tmp_path("nosig");
        SdImageIdentity id;
        std::string w;
        const bool prepared = copy_image(base_img, m) && poke(m, 510, 0x00);
        const bool ok = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P16",
              "a missing MBR boot signature refuses, and the message names the "
              "signature",
              prepared && !ok && w.find("MBR signature") != std::string::npos,
              w);
    }
    {
        const std::string m = tmp_path("notfat32");
        SdImageIdentity id;
        std::string w;
        const bool prepared = make_image(m, 0x83);   // Linux, not FAT32-LBA
        const bool ok = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P17",
              "an image with no FAT32-LBA partition refuses, and the message "
              "says so",
              prepared && !ok &&
                  w.find("no FAT32-LBA partition") != std::string::npos,
              w);
    }
    {
        const std::string m = tmp_path("badbpb");
        SdImageIdentity id;
        std::string w;
        // sectors_per_cluster = 3 is not a power of two: the same validation
        // `extract_sd_rom` applies, reached through the new entry point.
        const bool prepared = make_image(m, 0x0C, 0x29, kVolIdValue,
                                         "NEXT       ", 3);
        const bool ok = prepared && read_sd_image_identity(m, id, w);
        check("JNSI-P18",
              "a BPB that fails validation refuses, and the message names the "
              "field AND its value",
              prepared && !ok &&
                  w.find("sectors_per_cluster=3") != std::string::npos,
              w);
    }
    {
        const std::string m = tmp_path("truncated");
        {
            std::ofstream f(m, std::ios::binary | std::ios::trunc);
            const char tiny[16] = {0};
            f.write(tiny, 16);
        }
        SdImageIdentity id;
        std::string w;
        const bool ok = read_sd_image_identity(m, id, w);
        check("JNSI-P19",
              "a file too short to hold an MBR refuses without reading past it, "
              "and the message names the path",
              !ok && w.find(m) != std::string::npos, w);
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSI-P20..P26b — `describe_sdcard_for_snapshot` and the Tier-2 stamp
    // ─────────────────────────────────────────────────────────────────────
    {
        jnext::jns::SdCardInfo info;
        std::string w;
        const bool ok =
            jnext::describe_sdcard_for_snapshot(base_img, true, info, w);
        check("JNSI-P20",
              "the producer fills the manifest's card block: present, the "
              "mounted path, the read-only flag, and every Tier-1 field",
              ok && info.present && info.mounted_path == base_img &&
                  info.read_only &&
                  info.identity.image_bytes == base.image_bytes &&
                  info.identity.mbr_sha256 == base.mbr_sha256 &&
                  info.identity.fat32_volume_id == base.fat32_volume_id &&
                  info.identity.partition_lba == base.partition_lba &&
                  info.vollab == base.fat32_bs_vollab &&
                  info.identity.populated(),
              det("ok=%d present=%d why='%s'", ok, info.present, w.c_str()));

        check("JNSI-P21",
              "…and Tier 2: a 64-hex whole-image digest and an ISO-8601 UTC "
              "mtime (`YYYY-MM-DDTHH:MM:SSZ`)",
              ok && is_lower_hex(info.content_sha256, 64) &&
                  is_iso8601_utc(info.content_mtime_utc),
              det("sha='%s' mtime='%s'", info.content_sha256.c_str(),
                  info.content_mtime_utc.c_str()));
    }
    {
        // The two tiers answer DIFFERENT questions, and this is the row that
        // proves they do rather than asserting it: one mutation, two verdicts.
        const std::string m = tmp_path("tier2-drift");
        jnext::jns::SdCardInfo a, b;
        std::string w;
        const uint64_t far = static_cast<uint64_t>(kPartLba + 4000) * 512;
        const bool prepared = copy_image(base_img, m) && poke(m, far, 0x5A);
        const bool ok =
            prepared &&
            jnext::describe_sdcard_for_snapshot(base_img, false, a, w) &&
            jnext::describe_sdcard_for_snapshot(m, false, b, w);
        check("JNSI-P22",
              "a written data sector moves Tier 2 and NOT Tier 1. That is the "
              "whole design: Tier 1 says 'same card', Tier 2 says 'it has "
              "drifted', and only the first refuses",
              ok && a.identity == b.identity &&
                  a.content_sha256 != b.content_sha256,
              det("ok=%d id_same=%d sha_same=%d", ok,
                  ok && a.identity == b.identity,
                  ok && a.content_sha256 == b.content_sha256));
    }
    {
        jnext::jns::SdCardInfo info;
        std::string w;
        const bool ok = jnext::describe_sdcard_for_snapshot(base_img, false,
                                                            info, w, false);
        check("JNSI-P23",
              "`want_content_stamp = false` fills Tier 1 and leaves Tier 2 "
              "empty — the lever for a caller that has already established "
              "Tier 1 does not match, never a way to skip the check",
              ok && info.present && info.identity.populated() &&
                  info.content_sha256.empty() &&
                  info.content_mtime_utc.empty(),
              det("ok=%d sha='%s'", ok, info.content_sha256.c_str()));
    }
    {
        jnext::jns::SdCardInfo info;
        std::string w;
        const std::string missing = tmp_path("absent-for-describe");
        std::remove(missing.c_str());
        const bool ok =
            jnext::describe_sdcard_for_snapshot(missing, false, info, w);
        check("JNSI-P24",
              "a failure leaves the struct with `present == false` and NOTHING "
              "filled. A half-filled identity would compare unequal against "
              "itself on reload and refuse for a reason nobody could act on",
              !ok && !info.present && info.mounted_path.empty() &&
                  !info.identity.populated() && info.content_sha256.empty() &&
                  info.vollab.empty(),
              det("ok=%d present=%d path='%s'", ok, info.present,
                  info.mounted_path.c_str()));
    }
    {
        // The mtime string is UTC, and this is the row that says so. Every
        // other assertion on it checks only the SHAPE, which a `localtime_r`
        // would satisfy exactly as well as a `gmtime_r` — and then ship a local
        // timestamp with a 'Z' on the end, which is a different instant wearing
        // the right suit. Pinning one KNOWN epoch to one known rendering is the
        // only assertion that can tell them apart.
        const std::string m = tmp_path("fixed-mtime");
        std::string sha, mtime, w;
        bool prepared = copy_image(base_img, m);
        if (prepared) {
            struct timespec ts[2];
            ts[0].tv_sec = 1700000000; ts[0].tv_nsec = 0;   // atime
            ts[1].tv_sec = 1700000000; ts[1].tv_nsec = 0;   // mtime
            prepared = ::utimensat(AT_FDCWD, m.c_str(), ts, 0) == 0;
        }
        const bool ok = prepared &&
                        jnext::read_sd_image_content_stamp(m, sha, mtime, w);
        check("JNSI-P25",
              "the Tier-2 mtime is rendered in UTC: epoch 1700000000 is "
              "`2023-11-14T22:13:20Z` exactly, whatever the host's timezone",
              ok && mtime == "2023-11-14T22:13:20Z",
              det("prepared=%d ok=%d mtime='%s'", prepared, ok, mtime.c_str()));
    }
    {
        std::string sha = "leftover", mtime = "leftover", w;
        const std::string missing = tmp_path("absent-for-stamp");
        std::remove(missing.c_str());
        const bool ok = jnext::read_sd_image_content_stamp(missing, sha, mtime, w);
        check("JNSI-P26",
              "a Tier-2 stamp that cannot be taken refuses, NAMES the path, and "
              "leaves BOTH outputs empty — a half-filled stamp would be "
              "compared against the snapshot's and report a drift that never "
              "happened",
              !ok && sha.empty() && mtime.empty() &&
                  w.find(missing) != std::string::npos,
              det("ok=%d sha='%s' mtime='%s' why='%s'", ok, sha.c_str(),
                  mtime.c_str(), w.c_str()));
    }
    {
        // …AND THE OTHER ORDER, which the row above cannot reach. A missing
        // path fails at the STAT, so the digest is never attempted and the
        // clean-up after a FAILED DIGEST is never executed. This row drives
        // that second branch: a DIRECTORY stats perfectly and cannot be read,
        // so the mtime is computed and the digest then fails — and the mtime
        // must still come back empty.
        //
        // It exists because a mutation found the hole: deleting the
        // `mtime_utc_out.clear()` in the digest-failure path left every row
        // green. A half-filled stamp is not harmless — it is written into the
        // manifest as a stamp, and the reader has no way to tell it apart from
        // a real one.
        std::string sha = "leftover", mtime = "leftover", w;
        const char* base = std::getenv("TMPDIR");
        const std::string dir = (base && *base) ? base : "/tmp";
        const bool ok =
            jnext::read_sd_image_content_stamp(dir, sha, mtime, w);
        check("JNSI-P26b",
              "a stamp whose DIGEST fails after the stat SUCCEEDED also leaves "
              "both outputs empty — the mtime computed on the way must not "
              "survive the failure",
              !ok && sha.empty() && mtime.empty() && !w.empty(),
              det("ok=%d sha='%s' mtime='%s' why='%s'", ok, sha.c_str(),
                  mtime.c_str(), w.c_str()));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSI-P27..P31b — end to end, against real images
    // ─────────────────────────────────────────────────────────────────────
    //
    // §16.2's three legs at the unit tier, through the SAME `run_verdict` seam
    // the functional row's driver uses. `snapshot_test`'s JNSI rows prove the
    // reader's RULES against hand-written identities; these prove the rules and
    // the producer agree when the identities come off a disk.
    {
        Verdict v;
        std::string w;
        const bool ran = run_verdict(base_img, base_img, false, false, v, w);
        check("JNSI-P27",
              "the same image on both sides restores SILENTLY — no refusal and "
              "no warning at all",
              ran && v.ok && v.warnings.empty() && v.refusal.empty(),
              det("ran=%d ok=%d n=%zu '%s'", ran, v.ok, v.warnings.size(),
                  v.refusal.c_str()));
    }
    {
        const std::string m = tmp_path("e2e-data");
        Verdict v;
        std::string w;
        const uint64_t far = static_cast<uint64_t>(kPartLba + 4000) * 512;
        const bool prepared = copy_image(base_img, m) && poke(m, far, 0x5A);
        const bool ran = prepared &&
                         run_verdict(base_img, m, false, false, v, w);
        const bool warned =
            ran && v.warnings.size() == 1 &&
            v.warnings[0].find("contents changed") != std::string::npos;
        check("JNSI-P28",
              "LEG 1 — a mutated DATA SECTOR restores with exactly one warning "
              "naming the drift. The run proceeds: the card drifting is "
              "legitimate and common",
              ran && v.ok && warned,
              det("ran=%d ok=%d n=%zu '%s'", ran, v.ok, v.warnings.size(),
                  v.warnings.empty() ? "" : v.warnings[0].c_str()));
    }
    {
        const std::string m = tmp_path("e2e-volid");
        Verdict v;
        std::string w;
        const bool prepared =
            copy_image(base_img, m) &&
            poke(m, static_cast<uint64_t>(kPartLba) * 512 + 0x43, 0x99);
        const bool ran = prepared &&
                         run_verdict(base_img, m, false, false, v, w);
        check("JNSI-P29",
              "LEG 2 — a mutated BS_VolID REFUSES, and the refusal names BOTH "
              "serials so a user can see which card is mounted without opening "
              "the file",
              ran && !v.ok &&
                  v.refusal.find("not the one") != std::string::npos &&
                  v.refusal.find("1a2b3c4d") != std::string::npos &&
                  v.refusal.find("1a2b3c99") != std::string::npos,
              det("ran=%d '%s'", ran, v.refusal.c_str()));

        Verdict vf;
        const bool ran_f = prepared &&
                           run_verdict(base_img, m, false, true, vf, w);
        check("JNSI-P30",
              "…and `--snapshot-force-sdcard` downgrades that refusal to a "
              "warning that still names both serials",
              ran_f && vf.ok &&
                  std::any_of(vf.warnings.begin(), vf.warnings.end(),
                              [](const std::string& s) {
                                  return s.find("1a2b3c4d") != std::string::npos &&
                                         s.find("1a2b3c99") != std::string::npos;
                              }),
              det("ran=%d ok=%d n=%zu", ran_f, vf.ok, vf.warnings.size()));
    }
    {
        // LEG 3, AND THE ONE PLACE §16.2'S WORDING NEEDED CORRECTING.
        //
        // The design's leg 3 reads "mutate only BS_VolLab -> assert no warning
        // and no refusal". The first half of that is UNACHIEVABLE, and this row
        // is what proved it: BS_VolLab lives at partition_lba*512 + 0x47,
        // INSIDE the file, so rewriting it necessarily moves the whole-image
        // SHA-256 that Tier 2 IS — and Tier 2 is the warm-start cache's digest
        // "reused verbatim" (§11.3, pinned by JNSI-P34), so carving a region
        // out of it would be a DIFFERENT mechanism, not the same one.
        //
        // The Tier-2 warning here is TRUE: a byte on the card did change. What
        // §11.3's third matrix row actually promises — and what this row
        // asserts — is that the LABEL IS NEVER COMPARED: no refusal, and no
        // warning mentioning the label or the identity. A reader that compared
        // labels would refuse or warn HERE and stay silent on leg 1, so the row
        // is discriminative in exactly the direction that matters.
        //
        // §11.3's matrix and §16.2's row were corrected to say this; see the S7
        // append in the design doc.
        const std::string m = tmp_path("e2e-vollab");
        Verdict v;
        std::string w;
        const bool prepared =
            copy_image(base_img, m) &&
            poke(m, static_cast<uint64_t>(kPartLba) * 512 + 0x47, 'X');

        // The premise, MEASURED rather than assumed: the two cards really do
        // carry different labels and identical identities. Without it the row
        // would also pass against a producer that never read the label at all.
        SdImageIdentity a, b;
        std::string wa, wb;
        const bool got = prepared && read_sd_image_identity(base_img, a, wa) &&
                         read_sd_image_identity(m, b, wb);
        const bool premise = got && a.fat32_bs_vollab != b.fat32_bs_vollab &&
                             a.fat32_volume_id == b.fat32_volume_id &&
                             a.mbr_sha256 == b.mbr_sha256 &&
                             a.image_bytes == b.image_bytes &&
                             a.partition_lba == b.partition_lba;
        check("JNSI-P31",
              "LEG 3, premise — rewriting BS_VolLab really does give two cards "
              "with DIFFERENT labels and a byte-identical Tier-1 identity",
              premise,
              det("got=%d '%s' vs '%s'", got, a.fat32_bs_vollab.c_str(),
                  b.fat32_bs_vollab.c_str()));

        const bool ran = prepared &&
                         run_verdict(base_img, m, false, false, v, w);
        const bool label_silent = std::none_of(
            v.warnings.begin(), v.warnings.end(), [](const std::string& x) {
                return x.find("label") != std::string::npos ||
                       x.find("not the one") != std::string::npos;
            });
        check("JNSI-P31b",
              "LEG 3 — a differing BS_VolLab produces NO refusal and NO warning "
              "about the label or the identity. It is a stale copy several "
              "tools rewrite, and a false refusal on the same physical card is "
              "the cries-wolf failure §11.1 exists to avoid. (The Tier-2 drift "
              "warning IS present and IS true: the label is a byte of the "
              "image, so the whole-image digest moved with it.)",
              ran && v.ok && v.refusal.empty() && label_silent,
              det("ran=%d ok=%d n=%zu '%s'", ran, v.ok, v.warnings.size(),
                  v.warnings.empty() ? v.refusal.c_str()
                                     : v.warnings[0].c_str()));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSI-P32..P34 — the canonical 1 GB image, and the measured cost
    // ─────────────────────────────────────────────────────────────────────
    {
        SdImageIdentity id;
        std::string w;
        const bool ok = read_sd_image_identity(real, id, w);
        jnext::jns::SdIdentity as_jns;
        as_jns.image_bytes     = id.image_bytes;
        as_jns.mbr_sha256      = id.mbr_sha256;
        as_jns.fat32_volume_id = id.fat32_volume_id;
        as_jns.partition_lba   = id.partition_lba;
        check("JNSI-P32",
              "the REAL canonical NextZXOS image yields a populated Tier-1 "
              "identity. The rows above run on images this suite built; this "
              "one runs on the card jnext actually mounts",
              ok && as_jns.populated() && id.image_bytes > 0 &&
                  is_lower_hex(id.mbr_sha256, 64) &&
                  is_lower_hex(id.fat32_volume_id, 8) && id.partition_lba > 0,
              det("ok=%d bytes=%llu lba=%u volid='%s' why='%s'", ok,
                  static_cast<unsigned long long>(id.image_bytes),
                  id.partition_lba, id.fat32_volume_id.c_str(), w.c_str()));
    }
    {
        // §11.3: "the Tier-2 digest is ~0.5 s warm, ~1.2 s cold on a 1 GB
        // image… Recommend shipping it eager and measuring." Measured here and
        // PRINTED, so the figure in the design doc is a number this suite
        // reproduces rather than a claim nobody re-checks.
        std::string sha, mtime, w;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok =
            jnext::read_sd_image_content_stamp(real, sha, mtime, w);
        const auto t1 = std::chrono::steady_clock::now();
        const double secs =
            std::chrono::duration<double>(t1 - t0).count();
        std::printf("  NOTE JNSI-P33: Tier-2 digest of %s took %.3f s\n",
                    real.c_str(), secs);
        std::fflush(stdout);
        check("JNSI-P33",
              "the Tier-2 stamp of the real 1 GB image is a 64-hex digest and "
              "an ISO-8601 UTC mtime (the cost is printed above, not asserted: "
              "a wall-clock threshold on a shared build host is a flaky row, "
              "not a measurement)",
              ok && is_lower_hex(sha, 64) && is_iso8601_utc(mtime),
              det("ok=%d secs=%.3f why='%s'", ok, secs, w.c_str()));

        check("JNSI-P34",
              "…and it is the warm-start cache's mechanism REUSED VERBATIM: "
              "byte-for-byte `sdcard::sha256_file` of the same image, not a "
              "second digest implementation that could drift from it",
              ok && sha == sdcard::sha256_file(real),
              det("sha='%s'", sha.c_str()));
    }

    cleanup_scratch();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
