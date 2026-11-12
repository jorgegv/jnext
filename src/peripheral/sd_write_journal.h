#pragma once
// A copy-on-write journal for the SD card: the guest's sector writes go into a
// small side file and are laid over the image on every read, so the image is
// never written. One Journal is shared by every SdCardDevice that mounts the
// bound image (the live machine, the warm-start recorder, a cold boot, an SD
// change back to it), through a process-wide registry keyed by the image path.
//
// File format (all integers little-endian; the developer guide's media chapter
// is the reference):
//   offset  size  field
//   0       8     magic "JNEXTSDW"
//   8       4     format version = 1
//   12      4     record payload size = 512
//   16      8     image size in bytes
//   24      8     image mtime, raw file_clock ticks (0 when bound by digest)
//   32      32    image SHA-256 (all zero = bound by size + mtime)
//   64      records, in commit order; a later record supersedes an earlier
//                 one of the same sector (or, for a kept-file record, any
//                 earlier kept-file record). Two kinds:
//                   sector record, 516 bytes: sector u32 (< 0xFFFFFFFF) + 512 bytes
//                   kept-file record: 0xFFFFFFFF u32, L u32 (the bytes after it),
//                     name length n u16, the name (n bytes), the file's bytes
//                     (L-2-n bytes). The file is the one the esxDOS stand-in of a
//                     directly loaded program keeps; there is one per journal.
//
// This module has no SHA-256 (peripheral does not link core): the digest of
// the image is handed in by the caller, and the digest of the journal's
// content is computed by the warm-start code from for_each_sorted().

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sd_write_journal {

constexpr uint32_t kSectorBytes   = 512;
constexpr uint32_t kHeaderBytes   = 64;
constexpr uint32_t kRecordBytes   = 4 + kSectorBytes;
constexpr uint32_t kFormatVersion = 1;
/// The sector field of a kept-file record. No real sector has this number: a
/// binding of 2^32 sectors or more is refused (kMaxImageBytes).
constexpr uint32_t kFileMarker    = 0xFFFFFFFFu;
constexpr uint64_t kMaxImageBytes = (1ull << 32) * kSectorBytes;

/// The file the esxDOS stand-in keeps: its name as the guest spelled it, and its bytes.
struct KeptFile {
    std::string          name;
    std::vector<uint8_t> data;
};

/// The identity of the image a journal belongs to. `sha256` all zero means the
/// image is identified by size + mtime (the warm-start key's desktop rule).
struct Binding {
    uint64_t                bytes  = 0;
    int64_t                 mtime  = 0;
    std::array<uint8_t, 32> sha256{};
    bool by_digest() const { return sha256 != std::array<uint8_t, 32>{}; }
};

class Journal {
public:
    Journal(std::string file, const Binding& b) : file_(std::move(file)), binding_(b) {}

    /// The 512 bytes the guest last wrote to `sector`, when it did.
    bool lookup(uint32_t sector, uint8_t out[kSectorBytes]) const;

    /// Record a sector write: append to the file, then keep it in memory. False
    /// when the host write failed; the file then keeps its last whole record
    /// and nothing is recorded. The persist hook runs after a success.
    bool commit(uint32_t sector, const uint8_t in[kSectorBytes]);

    /// Keep the stand-in's file: append a kept-file record (it supersedes any
    /// earlier one), then the persist hook. An identical (name, bytes) pair
    /// appends nothing and runs no hook. False when the host write failed; the
    /// file keeps its last whole record and nothing is recorded.
    bool commit_file(const std::string& name, const std::vector<uint8_t>& data);

    /// The kept file, when there is one.
    std::optional<KeptFile> kept_file() const;

    /// Drop every write, the kept file included: the file is removed and the
    /// persist hook runs.
    void clear();

    /// True when no SECTOR is journaled (a kept file does not count: it is not card state).
    bool empty() const;
    const std::string& file() const { return file_; }
    const Binding& binding() const { return binding_; }

    /// Every journaled sector (the kept file is not one), ascending, with its last 512 bytes.
    template <class F> void for_each_sorted(F f) const {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& kv : map_) f(kv.first, kv.second.data());
    }

private:
    friend bool bind(const std::string&, const std::string&, const Binding&, bool, std::string&);
    mutable std::mutex mu_;
    std::string        file_;
    Binding            binding_;
    uint64_t           file_len_ = 0;   // length of the whole records on disk (0 = no file yet)
    std::optional<KeptFile> kept_;
    std::map<uint32_t, std::array<uint8_t, kSectorBytes>> map_;
};

/// Open or create the journal FILE for `image_path` and register it. Applies
/// `reset` first (a valid journal bound to any image is discarded; a file that
/// is not a journal is never touched). `why` says what refused it. Does not run
/// the persist hook.
bool bind(const std::string& image_path, const std::string& file,
          const Binding& binding, bool reset, std::string& why);

/// The journal bound to `image_path`, or null.
std::shared_ptr<Journal> for_image(const std::string& image_path);

/// Called after every committed write and after clear(). No hook by default.
using PersistHook = void (*)();
void set_persist_hook(PersistHook hook);

/// Drop every registration and the hook.
void forget_all_for_test();

}  // namespace sd_write_journal
