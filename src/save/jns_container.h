#pragma once
//
// The `.jns` container layer (GH #27 stage S1): the `manifest.json` grammar,
// the two version fields and their rules, the identity/provenance header, and
// the reader-rule table.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §6 (layout), §7 (versions),
// §8 (identity and provenance), §11 (the SD card), §12 (compatibility).
//
// WHAT THIS LAYER IS AND IS NOT. It owns the archive and its manifest — the
// framing every subsystem's state will later sit inside. It owns no subsystem
// state at all: there is no field descriptor here, no serialisation of the
// emulated machine, and no CLI or GUI surface. Those are stages S2 onward
// (§17), and mixing them in would put the container's own rules behind a
// dependency on the whole emulator, which is exactly what makes the S1 rows
// cheap to run and cheap to read.
//
// NO nlohmann/json IN THIS HEADER. The vendored JSON header is confined to
// `jns_container.cpp`, the single translation unit that parses and emits
// (§14.1's mitigation). Everything here is plain C++ so that a caller — and a
// test — never pays for it.
//
// ── DECOUPLING (F2) ──────────────────────────────────────────────────────
//
// `Manifest` is a description of the FILE, not a mirror of any emulator
// struct. `machine` is a string, not `MachineType`; `ram_kb` is a number the
// reader checks against what the build can construct, not an enum. That is
// deliberate: the container must not acquire a compile-time dependency on the
// emulated machine's C++ representation, or the format starts tracking the
// structs again — the failure F2 exists to prevent.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "save/zip_archive.h"

namespace jnext {
namespace jns {

/// The grammar version this build writes. §7.1 — it versions the member
/// namespace, the required manifest keys, the encoding rules and the
/// unknown-member rule, and it says nothing about the emulated machine.
/// It is FROZEN: it moves only when a reader built for version N could not
/// correctly read a version N+1 file.
constexpr uint32_t kFormatVersion = 1;

/// The EOCD comment, verbatim. `unzip -z snapshot.jns` then identifies the
/// file and its grammar with no JSON parse, and a TRUNCATED file is still
/// classifiable (§6).
constexpr char kArchiveComment[] = "jnext-snapshot format=1";

/// Member 0, always. A reader parses it before touching anything else (§6).
constexpr char kManifestMember[] = "manifest.json";

/// The member namespaces. `state/` and `meta/` are open — an unrecognised
/// member there is ignored and logged (§12.1). `mem/` is CLOSED: an
/// undeclared blob has no length or CRC to check, so it is not covered by
/// §5.3's validation chain and the archive is refused (§12.4).
constexpr char kStatePrefix[] = "state/";
constexpr char kMemPrefix[]   = "mem/";
constexpr char kMetaPrefix[]  = "meta/";

// ─────────────────────────────────────────────────────────────────────────
// Manifest
// ─────────────────────────────────────────────────────────────────────────

/// §8 — provenance only. A mismatch here is NEVER a refusal; the fields exist
/// to turn "this snapshot behaves oddly" into a bisectable fact.
struct Producer {
    std::string jnext_version;   ///< JNEXT_VERSION_STRING at write time
    std::string git_describe;    ///< optional; omitted when the caller has none
    std::string platform;        ///< e.g. "linux-x86_64"
};

/// §7.2 / §8 — what the file says about the machine it modelled.
/// `state_model_revision` is the MOVING number: a provenance stamp for the
/// emulated model, not a compatibility contract.
struct Model {
    uint32_t    state_model_revision = 1;
    std::string machine;              ///< drives reconstruction, not validation
    uint32_t    ram_kb = 2048;
    std::string timing;
    uint32_t    cpu_speed_nr07 = 0;
};

/// §8 — `frame_boundary` is always true, or the file was not written. It is
/// recorded as a fact a reader can CHECK, not as a mode. There is deliberately
/// no `paused` key: a mid-frame pause is a state a `.jns` cannot be written
/// from at all (the writer advances to the next frame boundary), so a flag
/// saying so would describe a file that does not exist.
struct Capture {
    uint64_t frame = 0;
    bool     frame_boundary = true;
};

/// Tier 1 of the SD identity (§11.3): "is this the same card?". Every field is
/// one a FILE WRITE DOES NOT TOUCH, which is the whole point — jnext opens the
/// image read-write and persists guest writes, so a whole-image digest would
/// report a mismatch against the very card the snapshot was taken on. An
/// identity that cries wolf gets ignored, and an ignored identity is worse
/// than none.
struct SdIdentity {
    uint64_t    image_bytes = 0;
    std::string mbr_sha256;
    std::string fat32_volume_id;   ///< BS_VolID, BPB offset 0x43, 8 hex chars
    uint64_t    partition_lba = 0;

    bool operator==(const SdIdentity& o) const {
        return image_bytes == o.image_bytes && mbr_sha256 == o.mbr_sha256 &&
               fat32_volume_id == o.fat32_volume_id &&
               partition_lba == o.partition_lba;
    }
    bool operator!=(const SdIdentity& o) const { return !(*this == o); }

    /// A usable identity has at least a size and a volume serial. A snapshot
    /// written before the identity could be computed carries neither, and the
    /// reader must not treat two empty identities as "the same card".
    bool populated() const {
        return image_bytes != 0 && !fat32_volume_id.empty();
    }

    std::string describe() const;
};

struct SdCardInfo {
    bool        present = false;
    std::string mounted_path;
    bool        read_only = false;

    SdIdentity  identity;

    /// INFORMATIONAL ONLY — NEVER COMPARED (§11.3). `BS_VolLab` is a stale
    /// copy: the authoritative FAT32 volume label is the root-directory entry
    /// carrying ATTR_VOLUME_ID, which jnext's own FAT walk explicitly skips
    /// (`sd_rom_extractor.cpp:292`). The two disagree routinely, and a tool
    /// that rewrites `BS_VolLab` (several do, writing both) would produce a
    /// FALSE REFUSAL on the same physical card. That is the cries-wolf failure
    /// §11.1 exists to avoid, so this is carried and never tested.
    std::string vollab;

    /// Tier 2 (§11.3): "has it changed since?". The whole-image digest.
    std::string content_sha256;
    std::string content_mtime_utc;
};

/// §8 / §10.2 P3 — ROM identity.
///
/// The gap it closes: for `--machine 48k/128k/plus3` the ROM lives in the
/// separate `Rom rom_` object, which is NOT serialised — ROM content comes
/// from the SD image at load time and never travels — so a snapshot restored
/// against a DIFFERENT `48.rom` runs different code with no indication
/// anywhere. Recording the digests makes that detectable.
///
/// Digests, never content: N3 (do not redistribute firmware). 64 KB of ROM in
/// a `.jns` would make every snapshot a firmware redistribution, which is the
/// same reason the SD image is recorded by identity rather than copied (§11).
///
/// On the Next the ROM content sits in SRAM pages inside `ram_`, so it travels
/// in the snapshot already and the digest is provenance rather than a check.
struct RomDigests {
    /// Where the ROMs came from: "sdcard" for the extracted TBBlue set,
    /// "embedded" for a build serving only the baked-in boot ROM, empty when
    /// the writer did not record it. Not compared — provenance.
    std::string source;

    /// Basename -> lower-case hex sha256, e.g. "48.rom" -> "…". A name the
    /// reader does not have is not a mismatch: it is a ROM this machine does
    /// not use, and comparing only the intersection is what keeps a 48K
    /// snapshot loadable on a build that also extracted `plus3.rom`.
    std::map<std::string, std::string> sha256;

    /// The FPGA boot ROM, which is baked into the binary rather than read
    /// from the card (`src/core/embed_rom.cmake`). A mismatch here means the
    /// two jnext builds disagree about silicon, so it is carried separately
    /// rather than as another entry above.
    std::string boot_rom_sha256;

    bool populated() const {
        return !sha256.empty() || !boot_rom_sha256.empty();
    }
};

/// §8 / §10.2 P4 — tape identity.
///
/// `tape_`/`tzx_tape_`/`wav_tape_` are excluded from the state stream by
/// design (tape position is independent of CPU rewind), so a snapshot taken
/// DURING a tape load restores a machine waiting for a tape that is not
/// playing. This is the esxDOS-handle shape applied to the tape: an external
/// resource recorded by reopenable identity, never copied.
struct TapeInfo {
    bool        present = false;
    std::string path;
    std::string sha256;

    /// Where the playback had got to, in the emulator's monotonic T-state
    /// clock — the same clock `Emulator::monotonic_tstates()` keeps
    /// continuous across a restore, which is what makes the position
    /// meaningful on the other side.
    uint64_t    position_tstates = 0;

    /// `--tape-realtime`: a fast (trapped) load and a real-time load are
    /// different machines, so the flag travels with the position.
    bool        realtime = false;
};

/// §10.2 P5 — the preview image, `meta/preview.png`.
///
/// The framebuffer is regenerated by the next render, so a snapshot restored
/// PAUSED shows the previous frame until the user steps. The preview doubles
/// as the restore-time paused image, and is free: jnext already writes PNG.
///
/// Declared rather than merely present, so a reader can size it before
/// inflating and a gallery can show it without a decode. `meta/` is an OPEN
/// namespace (§12.1), so a reader that does not know this member ignores it.
struct PreviewInfo {
    bool     present = false;
    uint32_t width   = 0;
    uint32_t height  = 0;
};

/// §8 — the blob declaration §5.3's validation chain rests on. The schema
/// pins its shape, the ZIP's own CRC-32 checks the bytes, and the reader
/// checks the length. The manifest cannot validate a blob's CONTENTS —
/// nothing can, short of running the machine — but it makes the declaration
/// checkable by an implementation that is not ours.
struct BlobDecl {
    uint64_t bytes = 0;
    uint32_t crc32 = 0;

    bool operator==(const BlobDecl& o) const {
        return bytes == o.bytes && crc32 == o.crc32;
    }
    bool operator!=(const BlobDecl& o) const { return !(*this == o); }
};

struct Manifest {
    uint32_t    format_version = kFormatVersion;
    std::string created;          ///< ISO-8601 UTC; provenance only

    Producer    producer;
    Model       model;
    Capture     capture;
    SdCardInfo  sdcard;
    RomDigests  roms;
    TapeInfo    tape;
    PreviewInfo preview;

    /// `media.esxdos_root` — `--esxdos-stub-root`. The open handles travel in
    /// the state stream as (path, offset, mode); this is the root they are
    /// relative to, recorded so a restore onto a different root is visible
    /// rather than silently reopening the wrong files.
    std::string esxdos_root;

    std::map<std::string, BlobDecl> members;

    /// The writer's own list of what it wrote. Its purpose is to let a reader
    /// distinguish "this subsystem was deliberately not saved" from "this
    /// member is missing or corrupt", which are different failures and must
    /// not be conflated (§8).
    std::vector<std::string> subsystems;
};

// ─────────────────────────────────────────────────────────────────────────
// Reader environment and verdict
// ─────────────────────────────────────────────────────────────────────────

/// Which grammars this build can read (§7.3).
///
/// It is a VALUE rather than a compile-time constant so both halves of the
/// "older format" rule are reachable in a test. With `kFormatVersion` at 1
/// there is no older grammar in existence, so "too old with a reader present"
/// and "too old with the reader removed" would otherwise be dead branches that
/// ship untested and go wrong the first time they are ever needed. The
/// production values are the defaults below; a test constructs a build that
/// claims to read 2 and to have dropped 1.
struct FormatVersionSupport {
    uint32_t           max_readable = kFormatVersion;
    std::set<uint32_t> readable     = {kFormatVersion};

    /// version -> the release that dropped its reader. §7.3's policy: a
    /// `format_version` bump ships with the previous grammar's reader retained
    /// for at least one public minor release, and its removal is a ChangeLog
    /// line. The refusal has to be able to NAME that release.
    std::map<uint32_t, std::string> dropped;
};

/// Everything the reader compares the manifest against. Supplied by the
/// caller, so the container has no dependency on the emulator.
struct ReaderEnv {
    FormatVersionSupport versions;

    uint32_t              state_model_revision = 1;
    std::string           machine;                ///< currently constructed
    std::set<std::string> constructible_machines =
        {"48k", "128k", "plus3", "pentagon", "next"};
    std::set<uint32_t>    constructible_ram_kb = {2048};

    /// The card mounted RIGHT NOW (`present == false` when none is).
    SdCardInfo card;

    /// Set from `SdCardDevice::transfer_in_flight()`, which GH #27 S6 added
    /// along with the FSM it reads (§10.2 P1). It had no producer at all from
    /// S1 to S5b, which is why the last row of §11.3 could not fire.
    ///
    /// §11.3's last row: when the machine was in the middle of a sector
    /// transfer at capture, a Tier-2 content drift stops being a warning and
    /// becomes a refusal — a half-finished sector read against changed bytes
    /// is exactly the "streams garbage" failure. The rule is a manifest rule
    /// and lives here; its one input arrives with S5.
    bool sd_transfer_in_flight = false;

    /// §10.2 P3 — the ROMs the CURRENT machine has, for the digest
    /// comparison. Only names present in BOTH are compared.
    RomDigests roms;

    /// §10.2 P4 — whether `media.tape.path` can be reopened right now.
    /// The container does NO filesystem I/O (that is what keeps its rows
    /// cheap and its dependencies none), so the caller stats and answers.
    bool tape_file_available = false;

    bool strict        = false;   ///< --snapshot-strict
    bool force_sdcard  = false;   ///< --snapshot-force-sdcard
};

/// The outcome. Every refusal NAMES the offending thing: G9 is a testable
/// property, not a slogan, and a refusal reading only "invalid snapshot" is a
/// FAILING row (§16.1, `JNSM`).
struct Verdict {
    bool                     ok = false;
    std::string              refusal;
    std::vector<std::string> warnings;

    /// Well-formed members the reader did not recognise. §12.1: ignored, and
    /// logged, so a newer file read by an older jnext says what it dropped.
    std::vector<std::string> ignored_members;

    /// Non-empty when `model.machine` differs from the constructed machine:
    /// the reader RECONFIGURES and restores rather than refusing (§7.3), the
    /// same way `Emulator::load_snapshot_buffer` already does for
    /// `.sna`/`.szx`/`.z80`. The user must not have to get `--machine` right
    /// to reload their own save.
    std::string reconfigure_to;
};

// ─────────────────────────────────────────────────────────────────────────
// Writer
// ─────────────────────────────────────────────────────────────────────────

/// Assembles a `.jns`. `manifest.json` is added first and every blob's
/// declaration is filled in from the bytes actually added, so the manifest and
/// the archive cannot be written out of agreement — the torn-file case §12.4
/// refuses on read is unconstructible here by design.
class SnapshotWriter {
public:
    /// `uncompressed` is settled point 6's debugging mode: every member is
    /// STORED instead of DEFLATE. One flag on the member writer, not a second
    /// code path, so the debug mode is exercised by the same reader.
    explicit SnapshotWriter(bool uncompressed = false);

    void set_manifest(const Manifest& m) { manifest_ = m; }
    Manifest& manifest() { return manifest_; }

    /// Add a `state/<name>.json` member and record `<name>` in
    /// `manifest.subsystems`.
    bool add_subsystem(const std::string& name, const std::string& json,
                       std::string& why);

    /// Add a `mem/<name>` blob and its `manifest.members` declaration.
    bool add_blob(const std::string& path, const uint8_t* data, size_t len,
                  std::string& why);

    /// Add a `meta/` member (preview PNG, README). Optional, ignored on read
    /// by anything that does not know it.
    bool add_meta(const std::string& path, const uint8_t* data, size_t len,
                  std::string& why);

    bool finish(std::vector<uint8_t>& out, std::string& why);

private:
    struct Pending {
        std::string          path;
        std::vector<uint8_t> bytes;
    };

    bool    uncompressed_;
    Manifest manifest_;
    std::vector<Pending> pending_;
};

/// Serialise a manifest to JSON text. Exposed because the writer's own output
/// is the thing a test compares against, and because the schema generator of
/// stage S2 will need it.
std::string manifest_to_json(const Manifest& m);

/// Parse manifest text. Returns false with `why` naming the defect.
bool manifest_from_json(const std::string& text, Manifest& out,
                        std::vector<std::string>& unknown_keys,
                        std::string& why);

// ─────────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────────

/// Open a `.jns` and apply every rule of §7.3, §11.3 and §12.4, in that
/// order. On success `zip` is left open so the caller can read the members;
/// on refusal `v.refusal` names what was wrong.
///
/// This deliberately stops at the container: it validates the framing, the
/// manifest, the declarations and the identity, and hands the caller an open
/// archive. Interpreting `state/*.json` is stage S2's job.
bool open_snapshot(const uint8_t* data, size_t len, const ReaderEnv& env,
                   zip::Reader& zip, Manifest& manifest, Verdict& v);

}  // namespace jns
}  // namespace jnext
