#pragma once
//
// A first-party ZIP reader/writer over zlib — the container layer of the
// `.jns` snapshot format (GH #27 stage S1).
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §5 (container decision),
// §6 (on-disk layout), §12 (compatibility and the unknown-member rule).
//
// WHY FIRST-PARTY. minizip / minizip-ng would save ~600 lines and cost a new
// `Requires:` in five packaging surfaces (rpm spec, debian, Flatpak manifest,
// Homebrew macOS leg, MinGW Windows cross-build), with two incompatible
// upstream APIs in circulation (§14.1). zlib is already required
// (`find_package(ZLIB REQUIRED)`, CMakeLists.txt:151) and supplies everything
// this needs: raw DEFLATE and CRC-32.
//
// ── THE ACCEPTED SUBSET, AND WHY IT IS A RULE RATHER THAN A LIST ──────────
//
// The reader accepts ONLY the exact subset the writer emits; every other ZIP
// feature is a REFUSAL (§6, last bullet). That is deliberate. ZIP has a long
// tail — data descriptors, encryption, spanning, extra fields, ZIP64, a local
// header disagreeing with the central directory for the same member — and a
// permissive reader has nothing to buy (a `.jns` is not an interchange format,
// settled points 1 and 2) and a silently-differing interpretation to lose.
// That divergence IS the `.szx` failure shape this whole format exists to
// avoid: two readers, both believing they succeeded, disagreeing about the
// bytes.
//
// ACCEPTED:
//   * local file header + file data + central directory + EOCD, in that order
//   * general-purpose bit flag == 0
//   * version-needed-to-extract == 20
//   * method 0 (STORED) or 8 (raw DEFLATE, windowBits -15)
//   * empty extra field and empty file comment on every member
//   * single disk, entry count < 0xFFFF, every size and offset < 0xFFFFFFFF
//   * an EOCD comment (the `.jns` layer pins its exact text)
//
// REFUSED (each with a message naming the offending thing — G9):
//   * ZIP64 in any form (EOCD64 locator present, or a 0xFFFF/0xFFFFFFFF
//     sentinel in the EOCD)
//   * a data descriptor (GP flag bit 3) — sizes would then be unknown at the
//     local header, which is exactly the framing ambiguity we refuse
//   * encryption (GP flag bit 0 or bit 6), or any other GP flag bit
//   * any compression method other than 0 and 8
//   * multi-disk / spanned archives
//   * a local file header disagreeing with its central-directory entry on
//     name, method, flags, CRC or either size
//   * a non-empty extra field or file comment
//   * DUPLICATE MEMBER NAMES — ZIP permits them and real readers disagree
//     about which wins (some the first central-directory entry, some the last,
//     some the last *local* header). Refused outright rather than picked (§6).
//   * a member path failing the grammar of `valid_member_path()` below
//   * truncation, overlapping members, or data extending into the central
//     directory
//   * a CRC-32 or inflated length that disagrees with what the headers declare
//
// ── DETERMINISM ──────────────────────────────────────────────────────────
//
// Every member is stamped with a FIXED DOS timestamp (1980-01-01 00:00:00,
// the DOS epoch), never the wall clock, so the same inputs produce the same
// bytes. A snapshot's real capture time lives in `manifest.json`'s `created`
// field, which is where a reader looks for it. This makes a written archive
// byte-comparable in a test and in a diff; a wall-clock stamp would make every
// write differ and quietly destroy that.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jnext {
namespace zip {

/// The two compression methods this container uses. `Stored` is the
/// `--snapshot-uncompressed` debugging mode of settled point 6 — a flag on the
/// member writer, not a second code path, which is what makes the debug mode
/// exercised by the same reader as the compressed one.
enum class Method : uint16_t {
    Stored  = 0,
    Deflate = 8,
};

/// One member, as the central directory describes it.
struct Entry {
    std::string name;
    Method      method        = Method::Stored;
    uint32_t    crc32         = 0;
    uint32_t    comp_size     = 0;
    uint32_t    uncomp_size   = 0;
    uint32_t    local_offset  = 0;
};

/// Member-path grammar (§6): lower-case, `/`-separated, no leading `/`, no
/// `..`. A reader refuses any archive containing a path that does not match
/// `^[a-z0-9][a-z0-9._/-]*$` — plus the component rules the regex alone
/// cannot express.
///
/// The regex is NOT sufficient by itself and that is the subtle part: `.` and
/// `/` are both in the character class, so `state/../../etc/passwd` matches it
/// and is still a traversal. The component checks below are what actually
/// close zip-slip. A `.jns` is not an extraction target, but the hazard is not
/// accepted even in a private format (§6).
///
/// Refuses, each naming the reason:
///   * an empty path
///   * a first character outside `[a-z0-9]` — this is what rejects an absolute
///     path (`/etc/passwd`), a leading dot, and a leading dash
///   * any character outside `[a-z0-9._/-]` — this is what rejects a backslash
///     (`state\cpu.json`, which Windows tooling treats as a separator), any
///     upper-case letter, and every control or non-ASCII byte
///   * an empty component — a trailing `/` (a ZIP directory entry, which the
///     writer never emits), a leading `/`, or a `//` in the middle
///   * a component that is exactly `.` or `..`
///   * a path longer than `kMaxPathLen`
bool valid_member_path(const std::string& path, std::string& why);

/// Longest member path accepted. Every path this format uses is under 32
/// bytes; the bound exists so a hostile central directory cannot make the
/// reader allocate on a 64 KB name.
constexpr size_t kMaxPathLen = 255;

/// Largest member this container will write or read, compressed OR
/// uncompressed. **This is a bound on a DECLARED size, and it is the reason
/// it exists.**
///
/// A ZIP member's uncompressed length is a 32-bit field in the central
/// directory. Nothing in the archive's structure relates it to the
/// compressed bytes actually present: a 167-byte file can legitimately
/// declare a 4 GB member, and a reader that sizes its output buffer from
/// that declaration allocates 4 GB on a file that fits in a packet. On a
/// memory-constrained host the allocation throws instead, and an uncaught
/// `std::bad_alloc` **terminates the process** — the exact inversion of G9,
/// which requires a hostile file to be refused loudly rather than to abort.
///
/// So the declaration is bounded BEFORE anything is allocated from it, in
/// `Reader::open`'s central-directory walk, and a member above the ceiling
/// is refused by name like every other feature outside the accepted subset.
/// Checking it at the walk rather than at the allocation site is deliberate:
/// it refuses at `open()`, before a single member has been read, and it
/// covers every present and future consumer of `Entry` rather than the one
/// call site that happens to allocate today.
///
/// 64 MB against a format whose largest legitimate member is
/// `mem/ram.bin` at 2 097 152 bytes (design §6.1) — about 32x headroom, so
/// the ceiling can never be reached by a file jnext itself wrote, while a
/// hostile declaration costs at most a bounded, survivable allocation.
///
/// A ratio bound (uncompressed <= compressed x the DEFLATE maximum) was
/// considered and REJECTED: 2 MB of zero-filled guest RAM — the single most
/// likely real blob — deflates at a ratio near 1000:1, close enough to
/// DEFLATE's 1032:1 theoretical maximum that a bound there would risk
/// refusing legitimate files. A wrong bound that rejects real snapshots is
/// worse than a generous one that bounds the damage.
constexpr uint64_t kMaxMemberBytes = 64ull * 1024 * 1024;

// ─────────────────────────────────────────────────────────────────────────
// Writer
// ─────────────────────────────────────────────────────────────────────────

/// Builds an archive in memory. Members are emitted in the order they are
/// added, in both the local-header sequence and the central directory, so
/// "`manifest.json` is member 0" is expressible simply by adding it first.
class Writer {
public:
    /// `comment` becomes the EOCD comment. The `.jns` layer passes the exact
    /// text `jnext-snapshot format=1` so that `unzip -z` identifies the file
    /// and its grammar without a JSON parse, and so a TRUNCATED file is still
    /// classifiable (§6).
    explicit Writer(std::string comment = {});

    /// Add one member. Returns false and sets `why` without modifying the
    /// archive if the path fails the grammar, duplicates an existing member,
    /// or the sizes would require ZIP64.
    ///
    /// Compression is attempted at level 9 (written once, read often — the
    /// warm-start cache's reasoning). If DEFLATE does not actually shrink the
    /// data the member is stored instead: a "compressed" member larger than
    /// its input is a pure loss, and the reader handles both methods anyway.
    bool add(const std::string& name, const uint8_t* data, size_t len,
             Method method, std::string& why);

    bool add(const std::string& name, const std::string& text, Method method,
             std::string& why);

    /// Serialise. Returns false and sets `why` if the archive is empty or any
    /// offset would exceed the 32-bit fields (ZIP64 is neither written nor
    /// read, §6).
    bool finish(std::vector<uint8_t>& out, std::string& why) const;

    size_t member_count() const { return members_.size(); }

private:
    struct Member {
        std::string          name;
        Method               method = Method::Stored;
        uint32_t             crc32 = 0;
        uint32_t             uncomp_size = 0;
        std::vector<uint8_t> payload;   ///< exactly as it goes on disk
    };

    std::string         comment_;
    std::vector<Member> members_;
};

// ─────────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────────

/// Parses an archive held in memory. Every refusal names what was wrong and
/// where — a reader that says only "invalid archive" fails its own test row
/// (§16.1, `JNSM`).
class Reader {
public:
    /// Parse the central directory, the EOCD and EVERY local file header,
    /// cross-checking the two. Returns false and sets `why` on any refusal.
    ///
    /// Local headers are validated here rather than lazily at `read()` time
    /// deliberately: a member nobody reads can still make the archive one a
    /// different implementation resolves differently, and the whole point of
    /// the accepted subset is that there is exactly one reading.
    ///
    /// **A failed open leaves the reader EMPTY**, not half-parsed. The walk
    /// populates `entries_` long before `data_` is committed, so a refusal in
    /// between would otherwise leave a reader whose `entries()` is non-empty
    /// while its buffer pointer is null — and a caller that forgot to check
    /// the return value would dereference it. The invariant is restored here,
    /// in one place, rather than guarded at each of the twenty-odd refusal
    /// sites or patched at the one accessor that dereferences today.
    bool open(const uint8_t* data, size_t len, std::string& why);

    const std::vector<Entry>& entries() const { return entries_; }
    const std::string&        comment() const { return comment_; }

    bool          has(const std::string& name) const;
    const Entry*  find(const std::string& name) const;

    /// Inflate (or copy) one member. Refuses if the inflated length differs
    /// from the declared `uncomp_size` — the warm-start cache's exact-length
    /// rule (§3.2) — or if the CRC-32 of the result differs from the declared
    /// one.
    bool read(const std::string& name, std::vector<uint8_t>& out,
              std::string& why) const;

    bool read_text(const std::string& name, std::string& out,
                   std::string& why) const;

private:
    /// The parse proper. Every refusal returns false from here; `open()` is
    /// the wrapper that clears a partial parse.
    bool open_impl(const uint8_t* data, size_t len, std::string& why);

    const uint8_t*     data_ = nullptr;
    size_t             len_  = 0;
    std::vector<Entry> entries_;
    std::string        comment_;
};

}  // namespace zip
}  // namespace jnext
