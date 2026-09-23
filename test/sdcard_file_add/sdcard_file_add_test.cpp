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
//     which needs a directory of 65536 entries to construct.
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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdcard::FileAddStatus;

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
    }
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

    fs::remove_all(g_scratch, ec);

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
