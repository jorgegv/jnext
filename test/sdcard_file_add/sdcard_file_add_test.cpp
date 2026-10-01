// --sdcard-file-add compliance tests (GH #269).
//
// Covers src/core/sdcard_file_add.{h,cpp}: the copy-and-exit mode that puts a
// host file into the FAT32 partition of an SD-card image.
//
// WHAT THIS SUITE PROVES, AND WHAT IT DOES NOT.
//
// It proves the DECISIONS: path validation, missing-directory creation, the
// refuse-then-force overwrite policy, the free-space check, the status/exit-code
// mapping, and that a write does not disturb what was already on the card.
// GH #292 added directory trees (SDFA-T??), the root default when no dest is
// given (SDFA-D??) and the add/dest pairing rule (SDFA-U??): merge into
// existing directories, follow symlinks but refuse loops, refuse whole and
// untouched before writing, roll back a copy that fails part-way.
//
// It does NOT prove the bytes are FAT32 any foreign driver would accept, and it
// deliberately does not try: every reader used here is jnext's own, and a writer
// validated only by its own reader is exactly the .szx failure this project
// already had once. The foreign validation — fsck.vfat plus mtools mdir/mcopy on
// a real 1 GB NextZXOS card — is the `sdcard-file-add-func` row of the
// regression suite, and it is the acceptance test for this feature.
//
// FIXTURES ARE BUILT HERE, not checked in. Each test makes a small MBR +
// FAT32 image with FatFs f_mkfs and 512-byte clusters (1 sector per cluster,
// the smallest spec-valid FAT32 at ~34 MB, sparse on disk). That is also
// DISCRIMINATIVE: production cards use 8 KB clusters, so anything that only
// works at one cluster size shows up as a difference between this suite and
// the regression row.
//
// EXPLICITLY NOT COVERED — declared rather than hidden, and each one measured
// rather than assumed:
//   * A source whose size cannot be determined at all (tellg returning -1).
//     Every host path that reaches add_file_to_image either opens and reports a
//     size or fails to open outright; the guard stays because a negative size
//     would otherwise become an enormous unsigned one.
//   * The f_write-error and short-write-after-the-pre-check paths. They cannot
//     be provoked deterministically in a single-threaded test, and a racy test
//     is worse than none. Their SIBLING — the short READ — is covered: a
//     directory given as the source reaches it on any filesystem where a
//     directory opens (btrfs, ext4), which is also why SDFA-W31 asserts the
//     partial file was removed rather than that the image is unchanged. On
//     tmpfs the same directory fails to open instead, and SDFA-W30 holds
//     either way. That difference is the reason these two rows are separate
//     from the missing-source pair: one assertion cannot be true on both.
//   * Directory-slot exhaustion (f_mkdir / f_open FR_DENIED -> ImageFull),
//     which needs a directory of 65536 entries to construct. (The same
//     FR_DENIED from running out of CLUSTERS while a directory grows IS
//     covered, by the tree rollback rows SDFA-T32..T36.)
//   * In a tree, a file that shrinks or an f_write that fails part-way. Both
//     end in the same rollback as the out-of-room case SDFA-T32..T36 drives.
//   * SDFA-T29/T30 (unreadable file / directory) cannot be built as root,
//     which reads through permission bits; they SKIP there, as SD-28 does.
//   * The COPY CHUNK SIZE. Shrinking it from 256 KB to 1 KB changes no row,
//     and must not: it is the one mutation here whose survival is the correct
//     answer, and it is also what says the multi-chunk loop works at all
//     (SDFA-W12's 9000-byte file is then nine iterations instead of one).
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/sdcard_file_add.h"

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <sys/stat.h>   // mkfifo, chmod (GH #292 tree rows)
#include <unistd.h>     // geteuid

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../row_id.h"

namespace fs = std::filesystem;
using sdcard::FileAddStatus;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

void skip(const char* id, const char* desc, const std::string& why) {
    report_row_id(id);
    ++g_total;
    ++g_skip;
    std::printf("  SKIP %s: %s [%s]\n", id, desc, why.c_str());
}

// ---------------------------------------------------------------------------
// Fixture plumbing
// ---------------------------------------------------------------------------

// Partition layout of every fixture image. 68000 sectors at 1 sector/cluster
// leaves ~66900 clusters, just over the 65525 FAT32 minimum FatFs enforces,
// for a 34.8 MB file that is entirely sparse until written.
constexpr uint32_t kPartLba      = 2048;
constexpr uint32_t kPartSectors  = 68000;
constexpr uint32_t kSectorSize   = 512;
constexpr uint32_t kClusterBytes = 512;   // au_size passed to f_mkfs

fs::path g_scratch;

void wr_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

bool write_host_file(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty())
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    return f.good();
}

bool read_host_file(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    return true;
}

// Deterministic pseudo-random payload, so a byte-comparison failure is a real
// difference and not two runs of zeros agreeing by accident.
std::vector<uint8_t> payload(std::size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t s = seed * 2654435761u + 1u;
    for (std::size_t i = 0; i < n; ++i) {
        s = s * 1103515245u + 12345u;
        v[i] = static_cast<uint8_t>(s >> 16);
    }
    return v;
}

// 64-bit FNV-1a over a whole file; used to assert an image was NOT modified.
uint64_t file_digest(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return 0;
    uint64_t h = 1469598103934665603ull;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<uint8_t>(buf[i]);
            h *= 1099511628211ull;
        }
    }
    return h;
}

// Write an MBR whose single entry is a type-0x0C FAT32-LBA partition.
bool write_mbr(const fs::path& image) {
    std::fstream f(image, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    uint8_t mbr[kSectorSize] = {};
    uint8_t* pe = mbr + 0x1BE;
    pe[0] = 0x00;                  // not bootable
    pe[1] = pe[2] = pe[3] = 0xFE;  // CHS start (ignored for LBA)
    pe[4] = 0x0C;                  // FAT32 LBA
    pe[5] = pe[6] = pe[7] = 0xFE;  // CHS end
    wr_u32(pe + 8,  kPartLba);
    wr_u32(pe + 12, kPartSectors);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    f.seekp(0, std::ios::beg);
    f.write(reinterpret_cast<const char*>(mbr), kSectorSize);
    return f.good();
}

// Create a fresh fixture image: MBR + a FAT32 volume holding
//   /README.TXT          (48 bytes)
//   /NEXTZXOS/KEEPME.BIN (3000 bytes, spans several 512-byte clusters)
// Both are read back after every write test, so a writer that corrupts the
// existing tree is caught rather than merely "not proven safe".
//
// f_mkfs is called here rather than through fat32_format_and_populate()
// because that helper pins 8 KB clusters, which would need a 537 MB fixture.
bool make_fixture(const fs::path& image, std::string& why) {
    std::error_code ec;
    fs::remove(image, ec);
    { std::ofstream create(image, std::ios::binary); if (!create) { why = "create"; return false; } }
    fs::resize_file(image,
                    static_cast<std::uintmax_t>(kPartLba + kPartSectors) * kSectorSize, ec);
    if (ec) { why = "resize: " + ec.message(); return false; }
    if (!write_mbr(image)) { why = "mbr"; return false; }

    std::string err;
    if (!fatfs_glue::attach(0, image.string(), kPartLba, kPartSectors, err)) {
        why = "attach: " + err;
        return false;
    }
    struct Detacher { ~Detacher() { fatfs_glue::detach(0); } } detacher;

    MKFS_PARM parm{};
    parm.fmt     = FM_FAT32 | FM_SFD;
    parm.n_fat   = 2;
    parm.align   = 0;
    parm.n_root  = 0;
    parm.au_size = kClusterBytes;
    std::vector<uint8_t> work(64 * 1024);
    if (f_mkfs("0:", &parm, work.data(), static_cast<UINT>(work.size())) != FR_OK) {
        why = "f_mkfs";
        return false;
    }
    FATFS fsobj{};
    if (f_mount(&fsobj, "0:", 1) != FR_OK) { why = "f_mount"; return false; }

    auto emit = [](const char* path, const std::vector<uint8_t>& data) -> bool {
        FIL fp{};
        if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
        UINT bw = 0;
        const bool ok = data.empty() ||
            (f_write(&fp, data.data(), static_cast<UINT>(data.size()), &bw) == FR_OK &&
             bw == data.size());
        return f_close(&fp) == FR_OK && ok;
    };
    bool ok = emit("0:/README.TXT", payload(48, 7));
    ok = ok && f_mkdir("0:/NEXTZXOS") == FR_OK;
    ok = ok && emit("0:/NEXTZXOS/KEEPME.BIN", payload(3000, 11));
    f_mount(nullptr, "0:", 0);
    if (!ok) { why = "populate"; return false; }
    return true;
}

// The two fixture files, re-read through the independent short-name reader
// after every mutation. `extract_sd_rom` is a different code path from the
// writer (its own MBR/BPB/FAT walk), so this is a real check that the write
// left the rest of the volume alone.
bool fixture_tree_intact(const fs::path& image) {
    std::vector<uint8_t> got;
    if (!extract_sd_rom(image.string(), "/README.TXT", got)) return false;
    if (got != payload(48, 7)) return false;
    if (!extract_sd_rom(image.string(), "/NEXTZXOS/KEEPME.BIN", got)) return false;
    return got == payload(3000, 11);
}

// Read the two FAT copies of a fixture image and report whether they agree.
// A writer that updates only FAT #1 passes every read-back test jnext can run
// (its readers all use FAT #1) and produces a card that fsck and any driver
// using the mirror will disagree with.
bool fats_agree(const fs::path& image, std::string& detail) {
    std::ifstream f(image, std::ios::binary);
    if (!f) { detail = "open"; return false; }
    uint8_t bpb[kSectorSize];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kPartLba) * kSectorSize));
    f.read(reinterpret_cast<char*>(bpb), kSectorSize);
    if (!f.good()) { detail = "bpb"; return false; }
    const uint16_t reserved = static_cast<uint16_t>(bpb[14] | (bpb[15] << 8));
    const uint8_t  n_fats   = bpb[16];
    const uint32_t fat_sz   = static_cast<uint32_t>(bpb[36]) |
                              (static_cast<uint32_t>(bpb[37]) << 8) |
                              (static_cast<uint32_t>(bpb[38]) << 16) |
                              (static_cast<uint32_t>(bpb[39]) << 24);
    if (n_fats != 2 || fat_sz == 0) { detail = "geometry"; return false; }
    const uint64_t fat0 =
        (static_cast<uint64_t>(kPartLba) + reserved) * kSectorSize;
    const uint64_t fat1 = fat0 + static_cast<uint64_t>(fat_sz) * kSectorSize;
    const std::size_t len = static_cast<std::size_t>(fat_sz) * kSectorSize;
    std::vector<uint8_t> a(len), b(len);
    f.seekg(static_cast<std::streamoff>(fat0));
    f.read(reinterpret_cast<char*>(a.data()), static_cast<std::streamsize>(len));
    f.seekg(static_cast<std::streamoff>(fat1));
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(len));
    if (!f.good()) { detail = "read"; return false; }
    for (std::size_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            detail = "first difference at FAT byte " + std::to_string(i);
            return false;
        }
    }
    return true;
}

// FSInfo free-cluster count (offset 488 of the FSInfo sector, whose number the
// BPB gives at offset 48). 0xFFFFFFFF means "unknown".
uint32_t fsinfo_free_count(const fs::path& image) {
    std::ifstream f(image, std::ios::binary);
    if (!f) return 0xFFFFFFFFu;
    uint8_t bpb[kSectorSize];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kPartLba) * kSectorSize));
    f.read(reinterpret_cast<char*>(bpb), kSectorSize);
    if (!f.good()) return 0xFFFFFFFFu;
    const uint16_t fsinfo_sec = static_cast<uint16_t>(bpb[48] | (bpb[49] << 8));
    uint8_t sec[kSectorSize];
    f.seekg(static_cast<std::streamoff>(
        (static_cast<uint64_t>(kPartLba) + fsinfo_sec) * kSectorSize));
    f.read(reinterpret_cast<char*>(sec), kSectorSize);
    if (!f.good()) return 0xFFFFFFFFu;
    // Signatures 0x41615252 / 0x61417272 / 0xAA550000.
    if (sec[0] != 0x52 || sec[1] != 0x52 || sec[2] != 0x61 || sec[3] != 0x41)
        return 0xFFFFFFFFu;
    return static_cast<uint32_t>(sec[488]) |
           (static_cast<uint32_t>(sec[489]) << 8) |
           (static_cast<uint32_t>(sec[490]) << 16) |
           (static_cast<uint32_t>(sec[491]) << 24);
}

// Does `long_name` appear in the directory `dir` of the image, with `size`
// bytes? Uses fat32_read_tree, which reconstructs VFAT long names — the short
// name reader (extract_sd_rom) cannot see them at all.
bool tree_has(const fs::path& image, const std::string& dir,
              const std::string& long_name, std::size_t size) {
    uint32_t part = 0;
    if (!fat32_find_partition(image.string(), part)) return false;
    Fat32Tree tree;
    if (!fat32_read_tree(image.string(), part, tree)) return false;
    const std::vector<Fat32Node>* level = &tree.root;
    if (!dir.empty()) {
        const Fat32Node* found = nullptr;
        for (const auto& n : *level)
            if (n.is_dir && n.name == dir) { found = &n; break; }
        if (!found) return false;
        level = &found->children;
    }
    for (const auto& n : *level)
        if (!n.is_dir && n.name == long_name && n.data.size() == size) return true;
    return false;
}

// The node at `path` ("/A/B/c.txt", long names, exact case) in a tree read by
// fat32_read_tree, or nullptr. That reader is jnext's lenient hand-rolled one,
// NOT FatFs — so a tree written through FatFs is read back by a different
// implementation.
const Fat32Node* find_node(const Fat32Tree& tree, const std::string& path) {
    const std::vector<Fat32Node>* level = &tree.root;
    const Fat32Node* found = nullptr;
    std::string comp;
    std::vector<std::string> parts;
    for (char c : path) {
        if (c == '/') { if (!comp.empty()) parts.push_back(comp); comp.clear(); }
        else comp.push_back(c);
    }
    if (!comp.empty()) parts.push_back(comp);
    for (const std::string& want : parts) {
        found = nullptr;
        for (const auto& n : *level)
            if (n.name == want) { found = &n; break; }
        if (!found) return nullptr;
        level = &found->children;
    }
    return found;
}

bool read_tree(const fs::path& image, Fat32Tree& tree) {
    uint32_t part = 0;
    if (!fat32_find_partition(image.string(), part)) return false;
    tree = Fat32Tree{};
    return fat32_read_tree(image.string(), part, tree);
}

// Does the card hold `data` at `path`?
bool card_file_is(const fs::path& image, const std::string& path,
                  const std::vector<uint8_t>& data) {
    Fat32Tree tree;
    if (!read_tree(image, tree)) return false;
    const Fat32Node* n = find_node(tree, path);
    return n && !n->is_dir && n->data == data;
}

bool card_has(const fs::path& image, const std::string& path) {
    Fat32Tree tree;
    if (!read_tree(image, tree)) return false;
    return find_node(tree, path) != nullptr;
}

// ---------------------------------------------------------------------------
// Destination-path validation (SDFA-P??)
// ---------------------------------------------------------------------------

void test_paths() {
    std::string out, err;

    check("SDFA-P01", "a root-level name normalises to a FatFs path",
          sdcard::normalize_dest_path("/FOO.BIN", out, err) && out == "0:/FOO.BIN", out);
    check("SDFA-P02", "the leading slash is optional",
          sdcard::normalize_dest_path("FOO.BIN", out, err) && out == "0:/FOO.BIN", out);
    check("SDFA-P03", "nested components join and empty ones collapse",
          sdcard::normalize_dest_path("//A//B/C.BIN", out, err) && out == "0:/A/B/C.BIN", out);
    check("SDFA-P04", "an empty path names no file",
          !sdcard::normalize_dest_path("", out, err) && !err.empty());
    // The message matters as much as the refusal: every "." and ".." component
    // also ends in a dot, so the trailing-dot rule below would reject them too
    // — and tell the user their name "ends in a '.' which FAT strips", which is
    // not what is wrong with "../foo".
    check("SDFA-P05", "a '.' component is refused, as a path component",
          !sdcard::normalize_dest_path("/A/./B.BIN", out, err) &&
          err.find("full path from the card root") != std::string::npos, err);
    check("SDFA-P06", "a '..' component is refused, as a path component",
          !sdcard::normalize_dest_path("/A/../B.BIN", out, err) &&
          err.find("'..'") != std::string::npos, err);
    // FatFs takes '\\' as a SECOND separator, so accepting it would silently
    // turn one file name into a chain of directories.
    check("SDFA-P07", "a backslash is refused and the message says to use '/'",
          !sdcard::normalize_dest_path("\\NEXTZXOS\\DRV-A.DSK", out, err) &&
          err.find("use '/'") != std::string::npos, err);
    check("SDFA-P08", "'*' is refused",
          !sdcard::normalize_dest_path("/A*.BIN", out, err));
    check("SDFA-P09", "':' is refused",
          !sdcard::normalize_dest_path("/A:B.BIN", out, err));
    check("SDFA-P10", "a non-ASCII byte is refused",
          !sdcard::normalize_dest_path("/CAF\xC3\xA9.BIN", out, err));
    check("SDFA-P11", "a control character is refused",
          !sdcard::normalize_dest_path("/A\x01" "B.BIN", out, err));
    // FAT strips a trailing dot or space, so the file would not carry the name
    // that was asked for — refused rather than silently renamed.
    check("SDFA-P12", "a name ending in '.' is refused",
          !sdcard::normalize_dest_path("/NAME.", out, err));
    check("SDFA-P13", "a name ending in a space is refused",
          !sdcard::normalize_dest_path("/NAME ", out, err));
    check("SDFA-P14", "a component longer than FF_MAX_LFN is refused",
          !sdcard::normalize_dest_path("/" + std::string(FF_MAX_LFN + 1, 'x'), out, err));
    check("SDFA-P15", "a component of exactly FF_MAX_LFN is accepted",
          sdcard::normalize_dest_path("/" + std::string(FF_MAX_LFN, 'x'), out, err));
    // There is no separate all-blank rule, and there does not need to be: a
    // component of nothing but spaces necessarily ends in one. (A dedicated
    // check for it was written, found to be unreachable by mutation, and
    // deleted.)
    check("SDFA-P16", "an all-blank name is refused by the trailing-space rule",
          !sdcard::normalize_dest_path("/A/   /B.BIN", out, err) &&
          err.find("ends in a '.' or a space") != std::string::npos, err);
    check("SDFA-P17", "a path that is only slashes names no file",
          !sdcard::normalize_dest_path("///", out, err));
}

// ---------------------------------------------------------------------------
// Copy behaviour (SDFA-W??)
// ---------------------------------------------------------------------------

void test_writes() {
    const fs::path img = g_scratch / "card.img";
    const fs::path src = g_scratch / "src.bin";
    std::string why, err;

    // ---- a plain add to the root ------------------------------------------
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    const std::vector<uint8_t> small = payload(700, 3);   // > 1 cluster of 512
    write_host_file(src, small);
    const uint32_t free_before = fsinfo_free_count(img);
    FileAddStatus st = sdcard::add_file_to_image(img.string(), src.string(),
                                                 "/NEW.BIN", false, err);
    check("SDFA-W01", "a file is copied into the root", st == FileAddStatus::Ok, err);
    std::vector<uint8_t> got;
    check("SDFA-W02", "the copied bytes read back identical",
          extract_sd_rom(img.string(), "/NEW.BIN", got) && got == small,
          "read " + std::to_string(got.size()) + " of " + std::to_string(small.size()));
    check("SDFA-W03", "the files that were already there are untouched",
          fixture_tree_intact(img));
    std::string fat_detail;
    check("SDFA-W04", "both FAT copies still agree after the write",
          fats_agree(img, fat_detail), fat_detail);
    // 700 bytes at 512-byte clusters is 2 clusters.
    const uint32_t free_after = fsinfo_free_count(img);
    check("SDFA-W05", "the FSInfo free count drops by exactly the clusters used",
          free_before != 0xFFFFFFFFu && free_after == free_before - 2,
          std::to_string(free_before) + " -> " + std::to_string(free_after));

    // ---- overwrite policy ---------------------------------------------------
    const std::vector<uint8_t> other = payload(700, 99);
    write_host_file(src, other);
    st = sdcard::add_file_to_image(img.string(), src.string(), "/NEW.BIN", false, err);
    check("SDFA-W06", "an existing destination is refused by default",
          st == FileAddStatus::DestExists, err);
    check("SDFA-W07", "...and the file on the card still holds the ORIGINAL bytes",
          extract_sd_rom(img.string(), "/NEW.BIN", got) && got == small);
    check("SDFA-W08", "the refusal names the flag that allows it and the size it found",
          err.find("--sdcard-file-force") != std::string::npos &&
          err.find("(700 bytes)") != std::string::npos, err);
    st = sdcard::add_file_to_image(img.string(), src.string(), "/NEW.BIN", true, err);
    check("SDFA-W09", "--sdcard-file-force replaces it",
          st == FileAddStatus::Ok, err);
    check("SDFA-W10", "...and the card now holds the NEW bytes",
          extract_sd_rom(img.string(), "/NEW.BIN", got) && got == other);

    // ---- missing directories are created ------------------------------------
    const std::vector<uint8_t> demo = payload(9000, 5);   // 18 clusters
    write_host_file(src, demo);
    st = sdcard::add_file_to_image(img.string(), src.string(),
                                   "/DEMOS/SUB/DEMO.NEX", false, err);
    check("SDFA-W11", "missing directories along the path are created",
          st == FileAddStatus::Ok, err);
    check("SDFA-W12", "a multi-cluster file in a created directory reads back identical",
          extract_sd_rom(img.string(), "/DEMOS/SUB/DEMO.NEX", got) && got == demo,
          "read " + std::to_string(got.size()));
    // A second file in a directory the previous call created proves the new
    // directory is a real, re-openable directory and not a one-shot artefact.
    write_host_file(src, small);
    st = sdcard::add_file_to_image(img.string(), src.string(),
                                   "/DEMOS/SUB/TWO.BIN", false, err);
    check("SDFA-W13", "a second file lands in the directory a previous call made",
          st == FileAddStatus::Ok && extract_sd_rom(img.string(), "/DEMOS/SUB/TWO.BIN", got) &&
          got == small, err);
    check("SDFA-W14", "the first file in that directory survived the second",
          extract_sd_rom(img.string(), "/DEMOS/SUB/DEMO.NEX", got) && got == demo);

    // ---- empty file ---------------------------------------------------------
    write_host_file(src, {});
    st = sdcard::add_file_to_image(img.string(), src.string(), "/EMPTY.BIN", false, err);
    check("SDFA-W15", "a zero-byte source is copied as a zero-byte file",
          st == FileAddStatus::Ok &&
          extract_sd_rom(img.string(), "/EMPTY.BIN", got) && got.empty(), err);

    // ---- long file names ----------------------------------------------------
    const std::vector<uint8_t> lfn_data = payload(1500, 17);
    write_host_file(src, lfn_data);
    st = sdcard::add_file_to_image(img.string(), src.string(),
                                   "/A Long Demo Name.nex", false, err);
    check("SDFA-W16", "a name that is not 8.3 is accepted",
          st == FileAddStatus::Ok, err);
    check("SDFA-W17", "...and comes back with its long name intact",
          tree_has(img, "", "A Long Demo Name.nex", lfn_data.size()));
    // Two long names that share their first six characters must not collide:
    // the short names they generate differ only in the ~N tail.
    write_host_file(src, small);
    st = sdcard::add_file_to_image(img.string(), src.string(),
                                   "/A Long Demo Other.nex", false, err);
    check("SDFA-W18", "a second long name sharing a prefix does not collide",
          st == FileAddStatus::Ok &&
          tree_has(img, "", "A Long Demo Other.nex", small.size()) &&
          tree_has(img, "", "A Long Demo Name.nex", lfn_data.size()), err);
    // An 8.3-clean uppercase name must NOT be mangled to a ~N short name: the
    // Next's own SFN-only readers, and NextZXOS's DRV-x.DSK automount, look it
    // up by exactly that short name. extract_sd_rom is short-name-only, so
    // finding it there is the proof.
    const std::vector<uint8_t> dsk = payload(2048, 23);
    write_host_file(src, dsk);
    st = sdcard::add_file_to_image(img.string(), src.string(),
                                   "/NEXTZXOS/DRV-A.DSK", false, err);
    check("SDFA-W19", "an 8.3-clean name keeps its exact short name",
          st == FileAddStatus::Ok &&
          extract_sd_rom(img.string(), "/NEXTZXOS/DRV-A.DSK", got) && got == dsk, err);

    check("SDFA-W20", "the original fixture files survived every write above",
          fixture_tree_intact(img));
    check("SDFA-W21", "both FAT copies still agree after all of them",
          fats_agree(img, fat_detail), fat_detail);

    // ---- destination conflicts ---------------------------------------------
    write_host_file(src, small);
    st = sdcard::add_file_to_image(img.string(), src.string(), "/NEXTZXOS", false, err);
    check("SDFA-W22", "a destination that is an existing directory is refused",
          st == FileAddStatus::DestInvalid, err);
    st = sdcard::add_file_to_image(img.string(), src.string(),
                                   "/README.TXT/X.BIN", false, err);
    // Named, not merely refused: without the is-it-a-directory test the same
    // path still fails, but with FatFs's own FR_NO_PATH and nothing telling
    // the user which component of their path is the problem.
    check("SDFA-W23", "a path through an existing FILE is refused, and says which one",
          st == FileAddStatus::DestInvalid &&
          err.find("already exists on the card as a file") != std::string::npos, err);
    st = sdcard::add_file_to_image(img.string(), src.string(), "/BAD:NAME", false, err);
    check("SDFA-W24", "an invalid destination name is refused before anything is written",
          st == FileAddStatus::DestInvalid, err);

    // A read-only destination is not replaced even with force. FF_USE_CHMOD is
    // off, so the attribute is set by patching the directory entry directly —
    // the name is unique enough to find by its 11-byte short form.
    write_host_file(src, small);
    st = sdcard::add_file_to_image(img.string(), src.string(), "/ZQXRDONL.BIN", false, err);
    check("SDFA-W25", "the read-only fixture file is created", st == FileAddStatus::Ok, err);
    {
        std::fstream f(img, std::ios::in | std::ios::out | std::ios::binary);
        std::vector<char> whole((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
        const std::string sfn = "ZQXRDONLBIN";
        const std::size_t at = std::string(whole.data(), whole.size()).find(sfn);
        check("SDFA-W26", "its 8.3 directory entry is on the card verbatim",
              at != std::string::npos);
        if (at != std::string::npos) {
            f.clear();
            f.seekp(static_cast<std::streamoff>(at + 11), std::ios::beg);
            const char attr = 0x21;  // AM_ARC | AM_RDO
            f.write(&attr, 1);
        }
    }
    st = sdcard::add_file_to_image(img.string(), src.string(), "/ZQXRDONL.BIN", true, err);
    check("SDFA-W27", "a read-only destination is not replaced even with force",
          st == FileAddStatus::DestInvalid, err);

    // ---- bad sources --------------------------------------------------------
    const uint64_t digest_before = file_digest(img);
    st = sdcard::add_file_to_image(img.string(), (g_scratch / "nope.bin").string(),
                                   "/X.BIN", false, err);
    check("SDFA-W28", "a missing source file is reported as unopenable",
          st == FileAddStatus::SourceUnreadable &&
          err.find("cannot open source file") != std::string::npos, err);
    check("SDFA-W29", "...without modifying a single byte of the image",
          file_digest(img) == digest_before);

    // A DIRECTORY as the source takes one of two routes depending on the
    // filesystem the scratch area is on, and both are correct: on tmpfs the
    // open fails outright, on btrfs/ext4 it opens, reports a size, and the
    // first read comes back short. So the outcome is asserted, and then the
    // thing that is true on BOTH routes — that no half-written file is left
    // behind. (A digest comparison would be wrong here: on the second route
    // the destination really is created and then unlinked, which moves bytes.)
    st = sdcard::add_file_to_image(img.string(), g_scratch.string(), "/X.BIN", false, err);
    check("SDFA-W30", "a directory given as the source is reported as such",
          st == FileAddStatus::SourceUnreadable, err);
    check("SDFA-W31", "...and leaves no partial file at the destination",
          !extract_sd_rom(img.string(), "/X.BIN", got));

    // FAT32 stores a 32-bit size, so >= 4 GiB cannot be represented. The
    // source is sparse: it costs no disk and is never read.
    {
        const fs::path huge = g_scratch / "huge.bin";
        std::error_code ec;
        { std::ofstream c(huge, std::ios::binary); }
        // Sparse: 4 GiB of apparent size, no blocks. It is refused on its
        // declared size alone, so not one byte of it is ever read.
        fs::resize_file(huge, 0x100000000ull, ec);
        st = ec ? FileAddStatus::Ok
                : sdcard::add_file_to_image(img.string(), huge.string(), "/HUGE.BIN",
                                            false, err);
        check("SDFA-W32", "a source of 4 GiB or more is refused",
              !ec && st == FileAddStatus::SourceUnreadable,
              ec ? "cannot create the sparse fixture: " + ec.message() : err);
        fs::remove(huge, ec);
    }

    // ---- no room ------------------------------------------------------------
    {
        const fs::path big = g_scratch / "big.bin";
        std::error_code ec;
        { std::ofstream c(big, std::ios::binary); }
        // Larger than the whole 34 MB volume, so the pre-check refuses it
        // without reading or writing anything.
        fs::resize_file(big, 64ull * 1024 * 1024, ec);
        const uint64_t before = file_digest(img);
        st = sdcard::add_file_to_image(img.string(), big.string(), "/BIG.BIN", false, err);
        check("SDFA-W33", "a source larger than the free space is refused",
              st == FileAddStatus::ImageFull, err);
        check("SDFA-W34", "...before writing anything to the image",
              file_digest(img) == before);
        fs::remove(big, ec);
    }

    // One byte past what fits. The interesting case is not "much too big" but
    // "too big by less than a cluster": a free-space test that rounds the
    // requirement DOWN passes this, writes until the volume is full, and only
    // then fails — leaving the image churned instead of untouched.
    {
        const uint64_t free_bytes =
            static_cast<uint64_t>(fsinfo_free_count(img)) * kClusterBytes;
        const fs::path edge = g_scratch / "edge.bin";
        std::error_code ec;
        { std::ofstream c(edge, std::ios::binary); }
        fs::resize_file(edge, free_bytes + 1, ec);
        const uint64_t before = file_digest(img);
        st = sdcard::add_file_to_image(img.string(), edge.string(), "/EDGE.BIN",
                                       false, err);
        check("SDFA-W46", "a source one byte past the free space is refused",
              !ec && st == FileAddStatus::ImageFull,
              ec ? "cannot create the sparse fixture: " + ec.message() : err);
        check("SDFA-W47", "...with the image still byte-for-byte as it was",
              file_digest(img) == before);
        fs::remove(edge, ec);
    }

    // ---- bad images ---------------------------------------------------------
    write_host_file(src, small);
    st = sdcard::add_file_to_image((g_scratch / "no-such.img").string(), src.string(),
                                   "/X.BIN", false, err);
    check("SDFA-W35", "a missing image is reported as unusable",
          st == FileAddStatus::ImageUnusable, err);
    // ...and says the file could not be OPENED. Answering "no FAT32-LBA
    // partition in its MBR" for a mistyped path sends the reader looking at
    // the wrong thing entirely.
    check("SDFA-W43", "...saying it could not be opened, not that it lacks a partition",
          err.find("cannot open SD image") != std::string::npos, err);

    const fs::path junk = g_scratch / "junk.img";
    write_host_file(junk, payload(1 << 20, 41));
    st = sdcard::add_file_to_image(junk.string(), src.string(), "/X.BIN", false, err);
    check("SDFA-W36", "a file with no MBR partition table is reported as unusable",
          st == FileAddStatus::ImageUnusable &&
          err.find("no FAT32-LBA partition") != std::string::npos, err);

    // An MBR that DOES declare a FAT32-LBA partition, over a boot sector that
    // is not a filesystem: this is the only way to reach the f_mount failure
    // (an under-clustered card lands here too).
    const fs::path nofs = g_scratch / "nofs.img";
    {
        std::error_code ec;
        { std::ofstream c(nofs, std::ios::binary); }
        fs::resize_file(nofs,
                        static_cast<std::uintmax_t>(kPartLba + kPartSectors) * kSectorSize, ec);
        write_mbr(nofs);
        std::fstream f(nofs, std::ios::in | std::ios::out | std::ios::binary);
        uint8_t bpb[kSectorSize] = {};
        bpb[11] = 0x00; bpb[12] = 0x02;               // 512 bytes per sector
        wr_u32(bpb + 32, kPartSectors);               // total sectors
        f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(kPartLba) * kSectorSize));
        f.write(reinterpret_cast<const char*>(bpb), kSectorSize);
    }
    st = sdcard::add_file_to_image(nofs.string(), src.string(), "/X.BIN", false, err);
    check("SDFA-W37", "a partition holding no mountable filesystem is refused",
          st == FileAddStatus::ImageUnusable, err);
    check("SDFA-W38", "...and the message points at the re-clustering tool",
          err.find("fix-sdcard-image.sh") != std::string::npos, err);

    // Sector sizes other than 512 cannot be presented to FatFs by the glue.
    const fs::path bigsec = g_scratch / "bigsec.img";
    {
        std::error_code ec;
        fs::copy_file(img, bigsec, fs::copy_options::overwrite_existing, ec);
        std::fstream f(bigsec, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(static_cast<std::streamoff>(
            static_cast<uint64_t>(kPartLba) * kSectorSize + 11), std::ios::beg);
        const char four_k[2] = {0x00, 0x10};          // 4096
        f.write(four_k, 2);
    }
    st = sdcard::add_file_to_image(bigsec.string(), src.string(), "/X.BIN", false, err);
    // FatFs would refuse a 4096-byte-sector volume on its own (FF_MAX_SS is
    // 512), so the STATUS alone does not show this check ran. The message does
    // — and it is the one that tells the user what is actually wrong, rather
    // than an FR_NO_FILESYSTEM code.
    check("SDFA-W39", "a non-512-byte sector size is refused",
          st == FileAddStatus::ImageUnusable, err);
    check("SDFA-W44", "...naming the sector size, not just failing to mount",
          err.find("4096-byte sectors") != std::string::npos, err);

    // A truncated image whose BPB still claims the full partition.
    const fs::path cut = g_scratch / "cut.img";
    {
        std::error_code ec;
        fs::copy_file(img, cut, fs::copy_options::overwrite_existing, ec);
        fs::resize_file(cut, static_cast<std::uintmax_t>(kPartLba + 100) * kSectorSize, ec);
    }
    st = sdcard::add_file_to_image(cut.string(), src.string(), "/X.BIN", false, err);
    check("SDFA-W40", "an image shorter than the partition it declares is refused",
          st == FileAddStatus::ImageUnusable, err);

    // A BPB claiming a zero-sector partition. Both size fields have to be
    // cleared: FAT32 carries the count at offset 32 and falls back to the
    // 16-bit one at offset 19. Nothing downstream can work with a zero-sector
    // volume, and FatFs is never asked to try.
    const fs::path zerosec = g_scratch / "zerosec.img";
    {
        std::error_code ec2;
        fs::copy_file(img, zerosec, fs::copy_options::overwrite_existing, ec2);
        std::fstream f(zerosec, std::ios::in | std::ios::out | std::ios::binary);
        const uint8_t zero[4] = {0, 0, 0, 0};
        f.seekp(static_cast<std::streamoff>(
            static_cast<uint64_t>(kPartLba) * kSectorSize + 19), std::ios::beg);
        f.write(reinterpret_cast<const char*>(zero), 2);
        f.seekp(static_cast<std::streamoff>(
            static_cast<uint64_t>(kPartLba) * kSectorSize + 32), std::ios::beg);
        f.write(reinterpret_cast<const char*>(zero), 4);
    }
    st = sdcard::add_file_to_image(zerosec.string(), src.string(), "/X.BIN", false, err);
    check("SDFA-W45", "a partition declaring zero sectors is refused",
          st == FileAddStatus::ImageUnusable &&
          err.find("zero-sector") != std::string::npos, err);

    // ---- naming the same image two different ways ---------------------------
    // The "you are writing the shared default image" warning is decided by
    // same_image_file(). It used to be `==` on the two strings, which any
    // relative spelling defeats — silently, and only for the user who is about
    // to clobber the image every other run and the whole test suite boot from.
    {
        const fs::path other = g_scratch / "other.img";
        write_host_file(other, payload(64, 61));
        std::error_code ec;

        check("SDFA-W48", "a path with './' and '..' in it names the same image",
              sdcard::same_image_file(
                  img.string(),
                  (g_scratch / "." / "sub" / ".." / img.filename()).string()));

        // The user's actual command: `cd <dir>; jnext --sdcard card.img ...`.
        const fs::path cwd = fs::current_path(ec);
        bool rel_ok = false;
        if (!ec) {
            fs::current_path(g_scratch, ec);
            if (!ec) rel_ok = sdcard::same_image_file(img.string(),
                                                      img.filename().string());
            std::error_code back;
            fs::current_path(cwd, back);
        }
        check("SDFA-W49", "a relative path from the image's own directory names it",
              rel_ok);

        const fs::path link = g_scratch / "card-link.img";
        fs::remove(link, ec);
        std::error_code link_ec;
        fs::create_symlink(img, link, link_ec);
        check("SDFA-W50", "a symlink to the image names the same image",
              !link_ec && sdcard::same_image_file(img.string(), link.string()),
              link_ec ? "cannot create a symlink here: " + link_ec.message() : "");
        fs::remove(link, ec);

        // A HARD link is the case that needs filesystem identity rather than
        // path normalisation: there is no symlink to follow and no '.' to
        // fold, just two directory entries pointing at one inode. It is also
        // the only one of those cases this host can exercise — a
        // case-insensitive volume, where `CARD.IMG` and `card.img` are one
        // file, is answered by the same `equivalent` call and cannot be built
        // on Linux.
        const fs::path hard = g_scratch / "card-hard.img";
        fs::remove(hard, ec);
        std::error_code hard_ec;
        fs::create_hard_link(img, hard, hard_ec);
        check("SDFA-W55", "a hard link to the image names the same image",
              !hard_ec && sdcard::same_image_file(img.string(), hard.string()),
              hard_ec ? "cannot create a hard link here: " + hard_ec.message() : "");
        fs::remove(hard, ec);

        // The other direction matters as much: a warning that fires for every
        // path is a warning nobody reads.
        check("SDFA-W51", "two different files do not name the same image",
              !sdcard::same_image_file(img.string(), other.string()));
        check("SDFA-W52", "a path that does not exist does not name an existing image",
              !sdcard::same_image_file(img.string(),
                                       (g_scratch / "absent.img").string()));
        // Both absent: there is no filesystem identity to consult, so the
        // normalised paths decide. That fallback has to keep working, because
        // the default image legitimately does not exist on a machine that has
        // never provisioned one.
        check("SDFA-W53", "two spellings of the same ABSENT path still match",
              sdcard::same_image_file(
                  (g_scratch / "absent.img").string(),
                  (g_scratch / "." / "absent.img").string()));
        check("SDFA-W54", "an empty path never names an image",
              !sdcard::same_image_file("", img.string()) &&
              !sdcard::same_image_file(img.string(), "") &&
              !sdcard::same_image_file("", ""));
        fs::remove(other, ec);
    }

    // ---- the exit-code contract --------------------------------------------
    // These numbers are the documented process exit codes (jnext(1) EXIT
    // STATUS) and scripts branch on them, so they are pinned here rather than
    // left to whatever order the enum happens to be written in. 1 is absent on
    // purpose: it stays jnext's generic usage error.
    check("SDFA-W41", "the status values are the documented exit codes",
          static_cast<int>(FileAddStatus::Ok)               == 0 &&
          static_cast<int>(FileAddStatus::SourceUnreadable) == 2 &&
          static_cast<int>(FileAddStatus::DestInvalid)      == 3 &&
          static_cast<int>(FileAddStatus::DestExists)       == 4 &&
          static_cast<int>(FileAddStatus::ImageUnusable)    == 5 &&
          static_cast<int>(FileAddStatus::ImageFull)        == 6);
    check("SDFA-W42", "every status has a distinct name",
          std::strcmp(sdcard::file_add_status_name(FileAddStatus::Ok), "ok") == 0 &&
          std::strcmp(sdcard::file_add_status_name(FileAddStatus::DestExists),
                      "dest-exists") == 0 &&
          std::strcmp(sdcard::file_add_status_name(FileAddStatus::ImageFull),
                      "image-full") == 0);
}

// ---------------------------------------------------------------------------
// GH #292 — how --sdcard-file-add / -dest / -force combine (SDFA-U??)
// ---------------------------------------------------------------------------

void test_usage() {
    using sdcard::file_add_usage_error;
    // add, dest, dest-empty, force
    check("SDFA-U01", "an add with no dest is accepted (the dest is optional)",
          file_add_usage_error(1, 0, true, false).empty(),
          file_add_usage_error(1, 0, true, false));
    check("SDFA-U02", "an add with one non-empty dest, and force, is accepted",
          file_add_usage_error(1, 1, false, true).empty());
    check("SDFA-U03", "nothing given at all is not an error",
          file_add_usage_error(0, 0, true, false).empty());
    std::string e = file_add_usage_error(0, 1, false, false);
    check("SDFA-U04", "a dest with nothing to copy is refused, naming what it needs",
          e.find("--sdcard-file-dest requires --sdcard-file-add") == 0, e);
    e = file_add_usage_error(0, 0, true, true);
    check("SDFA-U05", "a force with nothing to copy is refused, naming what it needs",
          e.find("--sdcard-file-force requires --sdcard-file-add") == 0, e);
    // The repeat used to overwrite silently: copy only the last, report success.
    e = file_add_usage_error(2, 0, true, false);
    check("SDFA-U06", "--sdcard-file-add given twice is refused, by name",
          e.find("--sdcard-file-add was given more than once") == 0, e);
    e = file_add_usage_error(1, 2, false, false);
    check("SDFA-U07", "--sdcard-file-dest given twice is refused, by name",
          e.find("--sdcard-file-dest was given more than once") == 0, e);
    e = file_add_usage_error(1, 1, true, false);
    check("SDFA-U08", "an EMPTY dest is refused rather than read as 'not given'",
          e.find("empty PATH") != std::string::npos, e);
}

// ---------------------------------------------------------------------------
// GH #292 — the default destination: the card root (SDFA-D??)
// ---------------------------------------------------------------------------

void test_defaults() {
    std::string dest, err;
    check("SDFA-D01", "a file lands in the root under its own name",
          sdcard::default_dest_path("some/dir/game.nex", dest, err) &&
          dest == "/game.nex", dest + err);
    check("SDFA-D02", "a trailing separator on a directory does not lose its name",
          sdcard::default_dest_path("some/games/", dest, err) && dest == "/games",
          dest + err);
    // `.` has no name of its own; the directory it means does.
    {
        const fs::path named = g_scratch / "cwdname";
        std::error_code ec;
        fs::create_directories(named, ec);
        const fs::path cwd = fs::current_path(ec);
        bool ok = false;
        if (!ec) {
            fs::current_path(named, ec);
            if (!ec) ok = sdcard::default_dest_path(".", dest, err) && dest == "/cwdname";
            std::error_code back;
            fs::current_path(cwd, back);
        }
        check("SDFA-D03", "'.' takes the name of the current directory", ok, dest + err);
        fs::create_directories(named / "child", ec);
        ok = false;
        if (!ec) {
            fs::current_path(named / "child", ec);
            if (!ec) ok = sdcard::default_dest_path("..", dest, err) && dest == "/cwdname";
            std::error_code back;
            fs::current_path(cwd, back);
        }
        check("SDFA-D14", "'..' takes the name of the parent directory", ok, dest + err);
    }
    check("SDFA-D04", "the host root has no name to take, and says to give a dest",
          !sdcard::default_dest_path("/", dest, err) &&
          err.find("cannot take a card name from '/'") != std::string::npos &&
          err.find("--sdcard-file-dest") != std::string::npos, err);
    check("SDFA-D05", "a host name FAT cannot hold is refused, not renamed",
          !sdcard::default_dest_path("dir/caf\xC3\xA9.nex", dest, err) &&
          err.find("--sdcard-file-dest") != std::string::npos, err);
    // Lexical: the name TYPED, not the link's target.
    {
        const fs::path target = g_scratch / "v3.nex";
        const fs::path link   = g_scratch / "latest.nex";
        write_host_file(target, payload(10, 1));
        std::error_code ec;
        fs::remove(link, ec);
        fs::create_symlink(target, link, ec);
        check("SDFA-D06", "a symlink lands under the link's name, not its target's",
              !ec && sdcard::default_dest_path(link.string(), dest, err) &&
              dest == "/latest.nex", ec ? ec.message() : dest + err);
    }

    // ...and add_to_image() really uses it.
    const fs::path img = g_scratch / "defaults.img";
    std::string why;
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    const fs::path file = g_scratch / "rootme.bin";
    const std::vector<uint8_t> data = payload(1234, 77);
    write_host_file(file, data);
    sdcard::AddSummary sum;
    FileAddStatus st = sdcard::add_to_image(img.string(), file.string(), "", false,
                                            err, &sum);
    check("SDFA-D07", "with no dest a FILE is copied to the card root",
          st == FileAddStatus::Ok && card_file_is(img, "/rootme.bin", data), err);
    check("SDFA-D08", "...and the summary names where it went",
          !sum.is_dir && sum.dest == "/rootme.bin" && sum.files == 1 &&
          sum.bytes == data.size(), sum.dest);

    const fs::path dir = g_scratch / "rootdir";
    fs::create_directories(dir / "inner");
    write_host_file(dir / "inner" / "x.bin", payload(40, 78));
    st = sdcard::add_to_image(img.string(), dir.string(), "", false, err, &sum);
    check("SDFA-D09", "with no dest a DIRECTORY is copied to the card root by name",
          st == FileAddStatus::Ok &&
          card_file_is(img, "/rootdir/inner/x.bin", payload(40, 78)) &&
          sum.is_dir && sum.dest == "/rootdir", err + sum.dest);

    // `/` for a directory: its CONTENTS go into the root.
    const fs::path flat = g_scratch / "flat";
    fs::create_directories(flat);
    write_host_file(flat / "flatfile.bin", payload(33, 79));
    st = sdcard::add_to_image(img.string(), flat.string(), "/", false, err, &sum);
    check("SDFA-D10", "a directory with dest '/' is merged into the card root",
          st == FileAddStatus::Ok && card_file_is(img, "/flatfile.bin", payload(33, 79)) &&
          !card_has(img, "/flat") && sum.dest == "/", err);
    // ...but a FILE cannot become the root.
    const uint64_t before = file_digest(img);
    st = sdcard::add_to_image(img.string(), file.string(), "/", false, err);
    check("SDFA-D11", "a file with dest '/' is refused, card untouched",
          st == FileAddStatus::DestInvalid && file_digest(img) == before, err);
    // A FIFO given directly is refused before it is opened: opening one for
    // reading blocks until a writer appears, which in a script is forever.
    const fs::path fifo = g_scratch / "top.fifo";
    std::error_code ec;
    fs::remove(fifo, ec);
    const bool made = ::mkfifo(fifo.c_str(), 0600) == 0;
    st = made ? sdcard::add_to_image(img.string(), fifo.string(), "/F.BIN", false, err)
              : FileAddStatus::Ok;
    check("SDFA-D12", "a FIFO given as the source is refused without opening it",
          made && st == FileAddStatus::SourceUnreadable &&
          err.find("FIFO") != std::string::npos && file_digest(img) == before,
          made ? err : "mkfifo failed");
    fs::remove(fifo, ec);
    check("SDFA-D13", "the fixture tree and both FATs survived the default-dest copies",
          fixture_tree_intact(img) && fats_agree(img, why), why);
}

// ---------------------------------------------------------------------------
// GH #292 — directory trees (SDFA-T??)
// ---------------------------------------------------------------------------

// The host tree the copy rows use. Created in REVERSE name order, so a writer
// that copies in whatever order the host lists a directory, instead of
// sorting, shows up in SDFA-T05.
void make_host_tree(const fs::path& root) {
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    write_host_file(root / "zero.bin", {});
    fs::create_directories(root / "sub" / "nested");
    fs::create_directories(root / "sub" / "empty");
    write_host_file(root / "sub" / "nested" / "inner.dat", payload(300, 104));
    write_host_file(root / "sub" / "deep.bin", payload(9000, 103));
    write_host_file(root / "alpha.bin", payload(700, 101));
    write_host_file(root / "Long Name File.txt", payload(1500, 102));
    write_host_file(root / ".hidden", payload(10, 105));
}

void test_tree() {
    const fs::path img  = g_scratch / "tree.img";
    const fs::path tree = g_scratch / "tree";
    std::string why, err;
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    make_host_tree(tree);

    const uint32_t free_before = fsinfo_free_count(img);
    sdcard::AddSummary sum;
    FileAddStatus st = sdcard::add_to_image(img.string(), tree.string(), "/TREE",
                                            false, err, &sum);
    check("SDFA-T01", "a directory tree is copied", st == FileAddStatus::Ok, err);
    check("SDFA-T02", "every file in it reads back identical, at its depth",
          card_file_is(img, "/TREE/alpha.bin", payload(700, 101)) &&
          card_file_is(img, "/TREE/Long Name File.txt", payload(1500, 102)) &&
          card_file_is(img, "/TREE/sub/deep.bin", payload(9000, 103)) &&
          card_file_is(img, "/TREE/sub/nested/inner.dat", payload(300, 104)) &&
          card_file_is(img, "/TREE/zero.bin", {}));
    // Hidden files are copied: "nothing is silently skipped" includes them.
    check("SDFA-T03", "a dot-file is copied too, under its own name",
          card_file_is(img, "/TREE/.hidden", payload(10, 105)));
    {
        Fat32Tree t;
        const Fat32Node* e = read_tree(img, t) ? find_node(t, "/TREE/sub/empty") : nullptr;
        check("SDFA-T04", "an empty directory is created, and is empty",
              e && e->is_dir && e->children.empty());
    }
    {
        Fat32Tree t;
        const Fat32Node* d = read_tree(img, t) ? find_node(t, "/TREE") : nullptr;
        std::string order;
        if (d) for (const auto& n : d->children) order += n.name + "|";
        check("SDFA-T05", "entries are written in name order",
              order == ".hidden|Long Name File.txt|alpha.bin|sub|zero.bin|", order);
    }
    check("SDFA-T06", "the summary counts files, new directories and bytes",
          sum.is_dir && sum.dest == "/TREE" && sum.files == 6 &&
          sum.dirs_created == 4 && sum.bytes == 700 + 1500 + 9000 + 300 + 10,
          std::to_string(sum.files) + " files, " +
          std::to_string(sum.dirs_created) + " dirs, " + std::to_string(sum.bytes));
    // Data: 700->2, 1500->3, 9000->18, 300->1, 10->1, 0->0 = 25 clusters.
    // Directories TREE, sub, nested, empty: one 512-byte cluster each (the
    // largest, TREE, holds 10 entries = 320 bytes). Nothing else may move.
    const uint32_t free_after = fsinfo_free_count(img);
    check("SDFA-T07", "the free count drops by exactly the clusters the tree uses",
          free_before != 0xFFFFFFFFu && free_after == free_before - 29,
          std::to_string(free_before) + " -> " + std::to_string(free_after));
    check("SDFA-T08", "the files already on the card are untouched",
          fixture_tree_intact(img));
    check("SDFA-T09", "both FAT copies agree after the tree copy",
          fats_agree(img, why), why);

    // ---- merging into what is there -------------------------------------------
    const fs::path more = g_scratch / "more";
    fs::create_directories(more / "sub");
    write_host_file(more / "sub" / "extra.bin", payload(64, 106));
    st = sdcard::add_to_image(img.string(), more.string(), "/TREE", false, err, &sum);
    check("SDFA-T10", "a tree whose directories already exist is MERGED into them",
          st == FileAddStatus::Ok &&
          card_file_is(img, "/TREE/sub/extra.bin", payload(64, 106)) &&
          card_file_is(img, "/TREE/sub/deep.bin", payload(9000, 103)) &&
          sum.dirs_created == 0, err);

    // ---- clashes: refused whole, before anything is written -----------------
    write_host_file(tree / "zzz-new.bin", payload(50, 107));  // not on the card yet
    uint64_t before = file_digest(img);
    st = sdcard::add_to_image(img.string(), tree.string(), "/TREE", false, err);
    check("SDFA-T11", "existing files in the way refuse the WHOLE copy",
          st == FileAddStatus::DestExists, err);
    check("SDFA-T12", "...counting them all and naming the flag that allows it",
          err.find("'/TREE/.hidden' already exists") != std::string::npos &&
          err.find("and so do 5 more files") != std::string::npos &&
          err.find("--sdcard-file-force") != std::string::npos, err);
    check("SDFA-T13", "...with the card byte-for-byte untouched (the new file too)",
          file_digest(img) == before && !card_has(img, "/TREE/zzz-new.bin"));

    write_host_file(tree / "alpha.bin", payload(700, 201));
    st = sdcard::add_to_image(img.string(), tree.string(), "/TREE", true, err, &sum);
    check("SDFA-T14", "--sdcard-file-force replaces the files in the way",
          st == FileAddStatus::Ok &&
          card_file_is(img, "/TREE/alpha.bin", payload(700, 201)) &&
          card_file_is(img, "/TREE/zzz-new.bin", payload(50, 107)), err);
    check("SDFA-T15", "...and a file only the card had is left alone",
          card_file_is(img, "/TREE/sub/extra.bin", payload(64, 106)));

    // A host DIRECTORY where the card has a FILE. A new file sorts ahead of
    // it, so a writer that found the clash only while writing would already
    // have written (and then rolled back) that file, moving bytes.
    const fs::path onfile = g_scratch / "onfile";
    fs::create_directories(onfile / "README.TXT");
    write_host_file(onfile / "README.TXT" / "in.bin", payload(5, 110));
    write_host_file(onfile / "AFIRST.BIN", payload(5, 111));
    before = file_digest(img);
    st = sdcard::add_to_image(img.string(), onfile.string(), "/", false, err);
    check("SDFA-T16", "a directory onto an existing FILE is refused, card untouched",
          st == FileAddStatus::DestInvalid &&
          err.find("already exists on the card as a file") != std::string::npos &&
          file_digest(img) == before, err);
    // A host FILE where the card has a DIRECTORY: `NEXTZXOS` merged into root,
    // again behind a new file.
    const fs::path clash = g_scratch / "clash";
    fs::create_directories(clash);
    write_host_file(clash / "AFIRST.BIN", payload(5, 112));
    write_host_file(clash / "NEXTZXOS", payload(5, 108));
    st = sdcard::add_to_image(img.string(), clash.string(), "/", false, err);
    check("SDFA-T17", "a file onto an existing DIRECTORY is refused, card untouched",
          st == FileAddStatus::DestInvalid &&
          err.find("already exists on the card as a directory") != std::string::npos &&
          file_digest(img) == before, err);

    // A read-only file in the way is not replaced even with force. As in
    // SDFA-W27 the attribute is patched straight into the directory entry.
    const fs::path ro = g_scratch / "ro";
    fs::create_directories(ro);
    write_host_file(ro / "AAFIRST.BIN", payload(20, 113));
    write_host_file(ro / "ZQXTREEO.BIN", payload(20, 109));
    st = sdcard::add_to_image(img.string(), ro.string(), "/", false, err);
    // New bytes for the file that sorts first: replacing it before reaching
    // the read-only one would change the card.
    write_host_file(ro / "AAFIRST.BIN", payload(20, 114));
    bool patched = false;
    if (st == FileAddStatus::Ok) {
        std::fstream f(img, std::ios::in | std::ios::out | std::ios::binary);
        std::vector<char> whole((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
        const std::size_t at = std::string(whole.data(), whole.size()).find("ZQXTREEOBIN");
        if (at != std::string::npos) {
            f.clear();
            f.seekp(static_cast<std::streamoff>(at + 11), std::ios::beg);
            const char attr = 0x21;  // AM_ARC | AM_RDO
            f.write(&attr, 1);
            patched = f.good();
        }
    }
    before = file_digest(img);
    st = sdcard::add_to_image(img.string(), ro.string(), "/", true, err);
    check("SDFA-T18", "a read-only file in the way is not replaced even with force",
          patched && st == FileAddStatus::DestInvalid &&
          err.find("read-only") != std::string::npos && file_digest(img) == before,
          patched ? err : "could not build the read-only fixture");
    // The destination of a DIRECTORY is validated like a file's: FAT would
    // strip the trailing dot and create "NAME", which is not what was asked.
    before = file_digest(img);
    st = sdcard::add_to_image(img.string(), more.string(), "/NAME.", false, err);
    check("SDFA-T38", "a directory's malformed destination is refused, card untouched",
          st == FileAddStatus::DestInvalid &&
          err.find("ends in a '.' or a space") != std::string::npos &&
          file_digest(img) == before, err);
    check("SDFA-T19", "after all of it the fixture files and both FATs are intact",
          fixture_tree_intact(img) && fats_agree(img, why), why);
}

// Host-side refusals: each one leaves the card byte-for-byte as it was.
void test_tree_refusals() {
    const fs::path img = g_scratch / "refuse.img";
    std::string why, err;
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    const uint64_t pristine = file_digest(img);
    std::error_code ec;
    auto fresh = [&](const char* name) {
        const fs::path d = g_scratch / name;
        fs::remove_all(d, ec);
        fs::create_directories(d / "sub", ec);
        // A valid file BEFORE the bad entry in name order, so a writer that
        // checked as it went would already have written it.
        write_host_file(d / "a-first.bin", payload(100, 110));
        return d;
    };

    // Symlinks are FOLLOWED: to a file, to a directory outside the tree.
    {
        const fs::path d = fresh("links");
        const fs::path outside = g_scratch / "outside";
        fs::create_directories(outside, ec);
        write_host_file(outside / "far.bin", payload(77, 111));
        write_host_file(g_scratch / "target.bin", payload(88, 112));
        std::error_code e1, e2;
        fs::create_symlink(g_scratch / "target.bin", d / "filelink.bin", e1);
        fs::create_directory_symlink(outside, d / "dirlink", e2);
        const FileAddStatus st =
            sdcard::add_to_image(img.string(), d.string(), "/LINKS", false, err);
        check("SDFA-T20", "a symlink to a file is followed: the card gets the bytes",
              !e1 && st == FileAddStatus::Ok &&
              card_file_is(img, "/LINKS/filelink.bin", payload(88, 112)), err);
        check("SDFA-T21", "a symlink to a directory is followed: the card gets its tree",
              !e2 && card_file_is(img, "/LINKS/dirlink/far.bin", payload(77, 111)), err);
    }
    const uint64_t base = file_digest(img);

    auto refused = [&](const char* id, const char* desc, FileAddStatus want,
                       const fs::path& d, const char* must_say) {
        const FileAddStatus st =
            sdcard::add_to_image(img.string(), d.string(), "/BAD", false, err);
        check(id, desc,
              st == want && err.find(must_say) != std::string::npos &&
              file_digest(img) == base && !card_has(img, "/BAD"), err);
    };
    {
        const fs::path d = fresh("loop");
        fs::create_directory_symlink(d, d / "sub" / "loop", ec);
        refused("SDFA-T22", "a symlink back into its own ancestry is refused, card untouched",
                FileAddStatus::SourceUnreadable, d, "never end");
    }
    {
        const fs::path d = fresh("dangle");
        fs::create_symlink(g_scratch / "no-such-target", d / "sub" / "gone.bin", ec);
        refused("SDFA-T23", "a dangling symlink is refused, card untouched",
                FileAddStatus::SourceUnreadable, d, "symbolic link to nothing");
    }
    {
        const fs::path d = fresh("fifo");
        const bool made = ::mkfifo((d / "sub" / "pipe").c_str(), 0600) == 0;
        if (made)
            refused("SDFA-T24", "a FIFO inside the tree is refused, card untouched",
                    FileAddStatus::SourceUnreadable, d, "FIFO");
        else
            check("SDFA-T24", "a FIFO inside the tree is refused, card untouched",
                  false, "mkfifo failed");
    }
    {
        const fs::path d = fresh("names");
        write_host_file(d / "sub" / "caf\xC3\xA9.txt", payload(3, 113));
        refused("SDFA-T25", "a host name FAT cannot hold is refused, card untouched",
                FileAddStatus::DestInvalid, d, "not printable ASCII");
    }
    {
        const fs::path d = fresh("cases");
        write_host_file(d / "sub" / "Game.nex", payload(3, 114));
        write_host_file(d / "sub" / "GAME.NEX", payload(3, 115));
        refused("SDFA-T26", "two names differing only in case are refused, card untouched",
                FileAddStatus::DestInvalid, d, "would be the same name on the card");
    }
    {
        const fs::path d = fresh("huge");
        { std::ofstream c(d / "sub" / "huge.bin", std::ios::binary); }
        fs::resize_file(d / "sub" / "huge.bin", 0x100000000ull, ec);
        refused("SDFA-T27", "a 4 GiB file inside the tree is refused, card untouched",
                FileAddStatus::SourceUnreadable, d, "4 GiB");
    }
    {
        // Bigger than the whole 34 MB volume, so the pre-check refuses it.
        const fs::path d = fresh("full");
        { std::ofstream c(d / "sub" / "big.bin", std::ios::binary); }
        fs::resize_file(d / "sub" / "big.bin", 64ull * 1024 * 1024, ec);
        refused("SDFA-T28", "a tree larger than the free space is refused, card untouched",
                FileAddStatus::ImageFull, d, "KB free");
    }
    // Unreadable: root reads through any permission bits, so these two cannot
    // be constructed as root (CI runs in a container as root) — the same
    // reason, and the same handling, as sdcard_test's SD-28.
    if (::geteuid() == 0) {
        skip("SDFA-T29", "an unreadable file in the tree is refused, card untouched",
             "running as root; cannot construct an unreadable file");
        skip("SDFA-T30", "an unreadable directory in the tree is refused, card untouched",
             "running as root; cannot construct an unreadable directory");
    } else {
        const fs::path d = fresh("unread");
        write_host_file(d / "sub" / "secret.bin", payload(3, 116));
        ::chmod((d / "sub" / "secret.bin").c_str(), 0);
        refused("SDFA-T29", "an unreadable file in the tree is refused, card untouched",
                FileAddStatus::SourceUnreadable, d, "cannot open source file");
        ::chmod((d / "sub" / "secret.bin").c_str(), 0600);

        const fs::path d2 = fresh("unlist");
        ::chmod((d2 / "sub").c_str(), 0);
        refused("SDFA-T30", "an unreadable directory in the tree is refused, card untouched",
                FileAddStatus::SourceUnreadable, d2, "cannot list directory");
        ::chmod((d2 / "sub").c_str(), 0700);
    }
    check("SDFA-T31", "no refusal touched the card: it is the image the links left",
          file_digest(img) != pristine && fixture_tree_intact(img) &&
          fats_agree(img, why), why);
}

// A copy that fails PART-WAY is rolled back.
//
// The free-space pre-check is a lower bound — one cluster per new directory —
// so a directory of many long names passes it and then runs out while its
// entries are being written. The card is filled first so that exactly three
// clusters are left: the new directory takes one, two more hold 32 of its
// entries, and the next file cannot be created.
void test_tree_rollback() {
    const fs::path img = g_scratch / "rollback.img";
    std::string why, err;
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    std::error_code ec;
    // Something to REPLACE, in a directory that already exists, so the
    // rollback has a replaced file it cannot restore.
    const fs::path keep = g_scratch / "keep.bin";
    write_host_file(keep, payload(20, 120));
    sdcard::add_file_to_image(img.string(), keep.string(), "/RB/AAA.BIN", false, err);

    const uint32_t leave = 3;
    const fs::path filler = g_scratch / "filler.bin";
    { std::ofstream c(filler, std::ios::binary); }
    const uint32_t now = fsinfo_free_count(img);
    fs::resize_file(filler, static_cast<std::uintmax_t>(now - leave) * kClusterBytes, ec);
    const FileAddStatus fst =
        sdcard::add_file_to_image(img.string(), filler.string(), "/FILLER.BIN", false, err);
    fs::remove(filler, ec);
    const uint32_t free_before = fsinfo_free_count(img);

    const fs::path many = g_scratch / "many";
    fs::remove_all(many, ec);
    fs::create_directories(many / "lots", ec);
    write_host_file(many / "AAA.BIN", payload(20, 121));      // replaces /RB/AAA.BIN
    for (int i = 0; i < 40; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "a rather long file name %02d.txt", i);
        write_host_file(many / "lots" / name, {});
    }
    const FileAddStatus st =
        sdcard::add_to_image(img.string(), many.string(), "/RB", true, err);
    check("SDFA-T32", "a copy that runs out of room part-way fails as image-full",
          fst == FileAddStatus::Ok && free_before == leave &&
          st == FileAddStatus::ImageFull,
          "fixture free=" + std::to_string(free_before) + "; " + err);
    check("SDFA-T33", "...says it was rolled back, and that a replaced file is lost",
          err.find("rolled back") != std::string::npos &&
          err.find("1 existing file(s) had already been replaced") != std::string::npos,
          err);
    check("SDFA-T34", "...and nothing it created is left on the card",
          !card_has(img, "/RB/lots") && card_has(img, "/RB"));
    check("SDFA-T35", "...with every cluster it took given back",
          fsinfo_free_count(img) == free_before,
          std::to_string(free_before) + " -> " + std::to_string(fsinfo_free_count(img)));
    check("SDFA-T36", "...and the volume consistent: fixture intact, FATs agree",
          fixture_tree_intact(img) && fats_agree(img, why), why);

    // One cluster per new directory is the pre-check's floor: four empty
    // directories cannot fit in three clusters, and that is known before
    // anything is written.
    const fs::path dirs = g_scratch / "dirs";
    fs::remove_all(dirs, ec);
    for (const char* d : {"D1", "D2", "D3", "D4"})
        fs::create_directories(dirs / d, ec);
    const uint64_t before = file_digest(img);
    const FileAddStatus st2 =
        sdcard::add_to_image(img.string(), dirs.string(), "/", false, err);
    check("SDFA-T37", "directories that cannot possibly fit are refused, card untouched",
          fsinfo_free_count(img) == leave && st2 == FileAddStatus::ImageFull &&
          file_digest(img) == before, err);

    // Data is counted in WHOLE clusters: one byte past the three free
    // clusters needs a fourth. Rounding down would pass the pre-check and
    // then run out while writing.
    const fs::path edge = g_scratch / "edge";
    fs::remove_all(edge, ec);
    fs::create_directories(edge, ec);
    write_host_file(edge / "EDGE.BIN", payload(leave * kClusterBytes + 1, 122));
    const FileAddStatus st3 =
        sdcard::add_to_image(img.string(), edge.string(), "/", false, err);
    check("SDFA-T39", "a tree one byte past the free space is refused, card untouched",
          st3 == FileAddStatus::ImageFull && file_digest(img) == before, err);
}

// The card fills while a file is being REPLACED: that file is gone (it was
// truncated to be rewritten), and the message has to say so rather than
// claim the card is as it was. A directory of many long names, sorted ahead
// of the replaced file, eats the clusters the pre-check set aside for it.
// Unlike SDFA-T33, where the replacement had already SUCCEEDED, here the
// failing file is the replaced one.
void test_tree_replace_lost() {
    const fs::path img = g_scratch / "lost.img";
    std::string why, err;
    if (!make_fixture(img, why)) {
        std::printf("FATAL: cannot build fixture image (%s)\n", why.c_str());
        std::exit(2);
    }
    std::error_code ec;
    const fs::path small = g_scratch / "small.bin";
    write_host_file(small, payload(100, 130));              // one cluster
    sdcard::add_file_to_image(img.string(), small.string(), "/RL/ZZZ.BIN", false, err);

    // Leave exactly what the pre-check will ask for: 1 (new dir) + 20 (data).
    const uint32_t leave = 21;
    const fs::path filler = g_scratch / "filler2.bin";
    { std::ofstream c(filler, std::ios::binary); }
    fs::resize_file(filler,
                    static_cast<std::uintmax_t>(fsinfo_free_count(img) - leave) *
                    kClusterBytes, ec);
    sdcard::add_file_to_image(img.string(), filler.string(), "/FILLER.BIN", false, err);
    fs::remove(filler, ec);

    const fs::path t = g_scratch / "lost";
    fs::remove_all(t, ec);
    fs::create_directories(t / "lots", ec);
    for (int i = 0; i < 40; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "a rather long file name %02d.txt", i);
        write_host_file(t / "lots" / name, {});
    }
    // Lowercase on the host so it sorts AFTER "lots" (byte order puts
    // uppercase first); FAT matches it to ZZZ.BIN regardless of case.
    write_host_file(t / "zzz.bin", payload(20 * kClusterBytes, 131));
    const uint32_t free_before = fsinfo_free_count(img);
    const FileAddStatus st =
        sdcard::add_to_image(img.string(), t.string(), "/RL", true, err);
    check("SDFA-T40", "a file lost while being replaced is reported as lost",
          free_before == leave && st == FileAddStatus::ImageFull &&
          err.find("1 existing file(s) had already been replaced") != std::string::npos,
          "fixture free=" + std::to_string(free_before) + "; " + err);
}

}  // namespace

int main() {
    std::error_code ec;
    const char* env_tmp = std::getenv("TMPDIR");
    const fs::path base = env_tmp && *env_tmp ? fs::path(env_tmp)
                                              : fs::temp_directory_path();
    // Unique per run so two concurrent invocations (or a leftover directory
    // from a killed one) never share fixtures.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    g_scratch = base / ("jnext-sdfa-" + std::to_string(static_cast<unsigned long long>(stamp)));
    fs::create_directories(g_scratch, ec);
    if (ec) {
        std::printf("FATAL: cannot create scratch directory %s (%s)\n",
                    g_scratch.string().c_str(), ec.message().c_str());
        return 2;
    }

    test_paths();
    test_writes();
    test_usage();
    test_defaults();
    test_tree();
    test_tree_refusals();
    test_tree_rollback();
    test_tree_replace_lost();

    fs::remove_all(g_scratch, ec);

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
