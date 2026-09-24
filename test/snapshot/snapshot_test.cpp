// `.jns` snapshot container tests — GH #27 stage S1.
//
// Spec: doc/design/NEXT-SNAPSHOT-FORMAT.md. There is NO VHDL counterpart and
// there never will be: a snapshot format is a jnext-internal on-disk artefact,
// so `snapshot_test` is tombstoned in the traceability generator's
// %NO_MATRIX_SECTION beside `warm_start_test`, `nex_loader_test` and the other
// host-side file-format suites. The authority every row below cites is that
// design document, section by section.
//
// WHAT S1 IS. The container and nothing else: the ZIP framing, the
// `manifest.json` grammar, the two version fields, the identity/provenance
// header and the reader-rule table. No field descriptor, no subsystem state,
// no schema generator, no CLI or GUI surface — those are S2 onward (§17), and
// none of them is touched here.
//
// ── WHY THESE ROWS EXIST IN THIS SHAPE ───────────────────────────────────
//
// The format exists because of a specific failure (§13.1). jnext's `.szx`
// saver wrote RAM pages 0-111; its own loader accepted any `uint8_t` page with
// no upper bound, so save -> load -> compare passed byte-exact with
// discriminative, mutation-tested assertions — all green, all worthless,
// because libspectrum hard-rejects any page > 63 and EVERY `.szx` jnext could
// produce failed to load in real FUSE. Saver and loader shared the blind spot,
// which made the defect structurally invisible to the suite as written.
//
// Two consequences run through this file:
//
//   1. Refusal rows dominate. Almost every row below feeds the reader
//      something the writer would never produce, because a reader that is
//      merely permissive enough to accept our own output is exactly the
//      `.szx` loader. The crafted archives (`JNSC-REF-*`) are hand-patched
//      bytes, not writer output.
//
//   2. The rows are not the whole gate. `make snapshot-zip-check` runs
//      info-zip's `unzip -t` and CPython's `zipfile` over every archive this
//      suite produces — two implementations that are not ours — which is what
//      removes the container-framing class of error this file cannot remove
//      by itself (§13.2(2)). Run this binary with `--emit-corpus DIR` to write
//      that corpus.
//
// ── DELIBERATE ABSENCE ───────────────────────────────────────────────────
//
// There is NO row for "a save attempted while paused mid-frame is refused".
// §12.4 lists that case explicitly as *(no row) — not a refusal condition*:
// the writer advances to the next frame boundary (§10.2 P7), so no file and no
// reader ever sees that state. The omission is deliberate and is recorded here
// so a later reader does not "fix" it by adding one.
//
// `JNSN-13`/`JNSN-14` are a different thing and are not that row: they pin
// that the manifest FIELD `capture.frame_boundary` can never be `false` — the
// writer refuses to emit it and the reader refuses to accept it, so a
// hand-edited file claiming a state that cannot exist is caught.
//
// ── ROW INDEX ────────────────────────────────────────────────────────────
//
//   JNSC-*        the ZIP container: writer round-trips, determinism, methods
//   JNSC-PATH-*   the member-path grammar (§6), both directly and through the
//                 reader's central-directory walk
//   JNSC-REF-*    crafted archives: every ZIP feature outside the accepted
//                 subset, each refused with a message naming it (§6)
//   JNSN-*        the `manifest.json` grammar (§6, §8)
//   JNSV-*        `format_version` and `state_model_revision` (§7)
//   JNSR-*        the reader-rule table, one row per line of §12.4
//   JNSI-*        the two-tier SD identity (§11.3)
//   JNSM-*        refusal-message properties — G9 is a testable property, not
//                 a slogan (§16.1)
//
// Stage S2's groups (JNSD / JNSE / JNSH / JNSA / JNSS / JNSG) are indexed in
// their own block, below the S1 rows.

#include "core/saveable.h"
#include "save/jns_container.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"
#include "save/state_desc_json.h"
#include "save/state_desc_schema.h"
#include "save/state_desc_defaults.h"
#include "save/zip_archive.h"

#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cinttypes>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

using jnext::jns::BlobDecl;
using jnext::jns::Manifest;
using jnext::jns::ReaderEnv;
using jnext::jns::SdIdentity;
using jnext::jns::SnapshotWriter;
using jnext::jns::Verdict;

int g_pass = 0, g_fail = 0, g_total = 0;

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
    return std::string(buf);
}

// ── The corpus the external ZIP readers verify (§13.2(2)) ────────────────
//
// Every archive this suite successfully builds is recorded here, so the
// external gate checks EXACTLY the files the writer produces in the course of
// the rows, not a separate hand-picked sample that could drift away from them.
// Two kinds, because two different claims are being checked. A `.jns` is a
// complete snapshot and the external readers assert the FORMAT's rules on it
// (member 0, the comment, the blob declarations). A `.zip` is bare
// `zip::Writer` output from the container rows — stored, deflate,
// incompressible, empty-member — which is not a snapshot and has no manifest,
// but whose FRAMING an external reader must still accept. Filing them all as
// `.jns` would either weaken the snapshot assertions to nothing or claim the
// writer produces files it does not.
struct CorpusFile {
    std::string          name;
    std::string          ext;
    std::vector<uint8_t> bytes;
};
std::vector<CorpusFile> g_corpus;

/// A complete `.jns` snapshot.
void record_corpus(const std::string& name, const std::vector<uint8_t>& z) {
    g_corpus.push_back({name, "jns", z});
}

/// Bare ZIP-layer output: framing only, no manifest.
void record_zip(const std::string& name, const std::vector<uint8_t>& z) {
    g_corpus.push_back({name, "zip", z});
}

// ── Refusal bookkeeping (JNSM) ───────────────────────────────────────────
//
// Every refusal this suite provokes is recorded, so the JNSM rows can assert
// properties ACROSS all of them: that each names something concrete, and that
// no two distinct conditions produce the same words. A reader whose messages
// collapse to one generic string passes every individual row and fails these.
struct Refusal {
    std::string row;
    std::string message;
};
std::vector<Refusal> g_refusals;

/// Assert a refusal, that it names `token`, and record it.
bool refused_naming(const char* row, const Verdict& v, const std::string& token) {
    if (!v.ok) g_refusals.push_back({row, v.refusal});
    return !v.ok && v.refusal.find(token) != std::string::npos;
}

bool refused_naming(const char* row, bool ok, const std::string& why,
                    const std::string& token) {
    if (!ok) g_refusals.push_back({row, why});
    return !ok && why.find(token) != std::string::npos;
}

// ── Little-endian patching of a built archive ────────────────────────────

uint16_t rd16(const std::vector<uint8_t>& z, size_t o) {
    return static_cast<uint16_t>(z[o] | (z[o + 1] << 8));
}
uint32_t rd32(const std::vector<uint8_t>& z, size_t o) {
    return static_cast<uint32_t>(z[o]) | (static_cast<uint32_t>(z[o + 1]) << 8) |
           (static_cast<uint32_t>(z[o + 2]) << 16) |
           (static_cast<uint32_t>(z[o + 3]) << 24);
}
void wr16(std::vector<uint8_t>& z, size_t o, uint16_t v) {
    z[o]     = static_cast<uint8_t>(v & 0xFF);
    z[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void wr32(std::vector<uint8_t>& z, size_t o, uint32_t v) {
    z[o]     = static_cast<uint8_t>(v & 0xFF);
    z[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    z[o + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    z[o + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

/// Offsets of the structural records in a well-formed archive, so a row can
/// patch one field and leave everything else valid. Written independently of
/// `zip::Reader` on purpose: a layout walker that shared the reader's parsing
/// would make every crafted row depend on the code it is testing.
struct Layout {
    size_t              eocd = 0;
    uint32_t            cd_offset = 0;
    uint32_t            cd_size = 0;
    uint16_t            count = 0;
    std::vector<size_t> cdh;   ///< central-directory header offsets
    std::vector<size_t> lfh;   ///< local file header offsets
};

Layout layout_of(const std::vector<uint8_t>& z) {
    Layout L;
    for (size_t i = z.size() - 22 + 1; i-- > 0;) {
        if (rd32(z, i) != 0x06054b50u) continue;
        if (i + 22 + rd16(z, i + 20) == z.size()) {
            L.eocd = i;
            break;
        }
    }
    L.count     = rd16(z, L.eocd + 10);
    L.cd_size   = rd32(z, L.eocd + 12);
    L.cd_offset = rd32(z, L.eocd + 16);

    size_t p = L.cd_offset;
    for (uint16_t i = 0; i < L.count; ++i) {
        L.cdh.push_back(p);
        L.lfh.push_back(rd32(z, p + 42));
        p += 46 + rd16(z, p + 28) + rd16(z, p + 30) + rd16(z, p + 32);
    }
    return L;
}

/// Offset of a local header's data, after its name.
size_t lfh_data(const std::vector<uint8_t>& z, size_t lfh) {
    return lfh + 30 + rd16(z, lfh + 26) + rd16(z, lfh + 28);
}

// ── Fixtures ─────────────────────────────────────────────────────────────

const char* kJson = "{\n  \"pc\": 312\n}\n";

/// A machine that matches the manifest `make_manifest()` describes, so a row
/// that changes ONE thing is changing only that thing.
ReaderEnv make_env() {
    ReaderEnv e;
    e.machine              = "next";
    e.state_model_revision = 1;
    e.card.present         = false;
    return e;
}

Manifest make_manifest() {
    Manifest m;
    m.created                    = "2026-09-23T10:11:12Z";
    m.producer.jnext_version     = "1.0.18";
    m.producer.platform          = "linux-x86_64";
    m.model.state_model_revision = 1;
    m.model.machine              = "next";
    m.model.ram_kb               = 2048;
    m.model.timing               = "next";
    m.model.cpu_speed_nr07       = 3;
    m.capture.frame              = 41291;
    m.capture.frame_boundary     = true;
    return m;
}

SdIdentity make_identity() {
    SdIdentity i;
    i.image_bytes     = 1073741824ull;
    i.mbr_sha256      = "aa11bb22cc33dd44ee55ff6600778899"
                        "aa11bb22cc33dd44ee55ff6600778899";
    i.fat32_volume_id = "1a2b3c4d";
    i.partition_lba   = 2048;
    return i;
}

/// A complete, valid `.jns` with one subsystem and one blob. Every row that
/// needs "a good file" starts here, so the difference between a passing and a
/// failing row is the single thing the row changed.
std::vector<uint8_t> build_good(const std::string& corpus_name,
                                bool uncompressed = false) {
    SnapshotWriter w(uncompressed);
    w.set_manifest(make_manifest());
    std::string why;
    w.add_subsystem("cpu", kJson, why);
    const std::vector<uint8_t> blob(4096, 0xA5);
    w.add_blob("mem/bank5-vram.bin", blob.data(), blob.size(), why);

    std::vector<uint8_t> out;
    if (!w.finish(out, why)) return {};
    if (!corpus_name.empty()) record_corpus(corpus_name, out);
    return out;
}

/// Rebuild a `.jns` from an explicitly supplied manifest and member list,
/// bypassing `SnapshotWriter`, so a row can construct a file the writer would
/// refuse to produce — which is most of the reader-rule table.
std::vector<uint8_t> build_raw(const Manifest& m,
                               const std::vector<std::pair<std::string, std::vector<uint8_t>>>& members,
                               std::string& why,
                               const std::string& comment =
                                   jnext::jns::kArchiveComment,
                               bool manifest_first = true) {
    jnext::zip::Writer w{comment};
    const std::string text = jnext::jns::manifest_to_json(m);
    std::vector<uint8_t> out;
    if (manifest_first &&
        !w.add(jnext::jns::kManifestMember, text, jnext::zip::Method::Deflate,
               why)) {
        return {};
    }
    for (const auto& kv : members) {
        if (!w.add(kv.first, kv.second.data(), kv.second.size(),
                   jnext::zip::Method::Deflate, why)) {
            return {};
        }
    }
    if (!manifest_first &&
        !w.add(jnext::jns::kManifestMember, text, jnext::zip::Method::Deflate,
               why)) {
        return {};
    }
    if (!w.finish(out, why)) return {};
    return out;
}

/// Replace `manifest.json`'s bytes with arbitrary text. Used by every row that
/// feeds the reader a manifest the writer could not have produced.
std::vector<uint8_t> build_with_manifest_text(const std::string& text,
                                              std::string& why) {
    jnext::zip::Writer w{jnext::jns::kArchiveComment};
    std::vector<uint8_t> out;
    if (!w.add(jnext::jns::kManifestMember, text, jnext::zip::Method::Deflate,
               why)) {
        return {};
    }
    if (!w.finish(out, why)) return {};
    return out;
}

std::vector<uint8_t> bytes_of(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}


// ── The §17.1 golden extractor ───────────────────────────────────────────
//
// S2-S5's oracle is a byte image of the state stream captured BEFORE the
// migration, `cmp`ed after each migrated subsystem. §17.1 gives the recipe as
// a shell one-liner; this is the same recipe as tested code, exposed as
// `snapshot_test --extract-golden IN OUT`, so S3 runs something the rows below
// cover rather than a pipeline nothing checks.
//
// Why `rewind_test` is not the oracle, restated here because it is the trap:
// `rewind_test.cpp:241-263` saves to buf1, loads, saves to buf2 and memcmps
// buf1 against buf2. That is save->load->save IDEMPOTENCE plus a size check. A
// migration that CONSISTENTLY reordered two fields — writing and reading them
// in the same new order — passes every row, because both passes share the new
// layout. The oracle has to be a byte image captured before the migration.
//
// Header layout (warm_start_cache.cpp:48-56), and every field of it checked:
//
//     0   8  magic "JNEXTWS1" / "JNEXTWS2"
//     8   4  state-stream format version (u32 LE)
//    12   4  machine type                (u32 LE)
//    16   8  PLAIN payload length        (u64 LE)   <- the identity §17.1 names
//    24  64  SD image SHA-256, ASCII hex, NUL-padded
//    88   8  STORED payload length       (u64 LE)
//    96  ..  payload: a zlib stream in WS2, the raw stream in WS1

namespace s2 {

constexpr std::size_t kWsHeaderBytes = 96;
constexpr std::size_t kWsOffPlain    = 16;
constexpr std::size_t kWsOffStored   = 88;

uint64_t get_u64le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[static_cast<std::size_t>(i)];
    return v;
}

void put_u64le(uint8_t* p, uint64_t v) {
    for (std::size_t i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

/// §17.1, as code. Returns false with `why` naming the defect.
bool extract_warm_start(const std::vector<uint8_t>& file,
                        std::vector<uint8_t>& out, std::string& why) {
    out.clear();
    why.clear();
    if (file.size() < kWsHeaderBytes) {
        why = "file is shorter than the 96-byte warm-start header";
        return false;
    }
    const bool ws1 = std::memcmp(file.data(), "JNEXTWS1", 8) == 0;
    const bool ws2 = std::memcmp(file.data(), "JNEXTWS2", 8) == 0;
    if (!ws1 && !ws2) {
        why = "magic is not JNEXTWS1 or JNEXTWS2";
        return false;
    }
    const uint64_t plain  = get_u64le(file.data() + kWsOffPlain);
    const uint64_t stored = get_u64le(file.data() + kWsOffStored);
    // Bound before allocating. The declared length is a number from a FILE,
    // and S1's review found a 167-byte archive that used one to demand
    // 4.29 GB. 64 MB is ~28x the real stream (2 292 965 bytes).
    constexpr uint64_t kMaxPlain = 64ull * 1024 * 1024;
    if (plain == 0 || plain > kMaxPlain) {
        why = "declared plain length " + std::to_string(plain) +
              " is zero or beyond the 64 MB bound";
        return false;
    }
    if (file.size() - kWsHeaderBytes < stored) {
        why = "file is truncated: " + std::to_string(file.size() - kWsHeaderBytes) +
              " payload bytes on disk, header declares " + std::to_string(stored);
        return false;
    }
    const uint8_t* payload = file.data() + kWsHeaderBytes;
    if (ws1) {
        // §17.1's second note: a pre-compression file's payload IS the stream.
        if (stored != plain) {
            why = "JNEXTWS1 stored length disagrees with its plain length";
            return false;
        }
        out.assign(payload, payload + stored);
    } else {
        out.resize(static_cast<std::size_t>(plain));
        uLongf dst = static_cast<uLongf>(plain);
        const int rc = ::uncompress(out.data(), &dst, payload,
                                    static_cast<uLong>(stored));
        if (rc != Z_OK) {
            why = "zlib refused the payload (rc " + std::to_string(rc) + ")";
            out.clear();
            return false;
        }
        out.resize(static_cast<std::size_t>(dst));
    }
    // §17.1: "its `plain_bytes` field states the exact expected length —
    // check it." A golden that silently came out short would make every later
    // `cmp` compare the wrong thing and report success.
    if (out.size() != plain) {
        why = "inflated " + std::to_string(out.size()) +
              " bytes, header declares " + std::to_string(plain);
        out.clear();
        return false;
    }
    return true;
}

/// Build a synthetic warm-start file, so the extractor's rows do not depend
/// on a machine-keyed cache in ~/.jnext that a regeneration replaces.
std::vector<uint8_t> make_warm_start(const char* magic,
                                     const std::vector<uint8_t>& payload,
                                     uint64_t declared_plain, bool deflate) {
    std::vector<uint8_t> body;
    if (deflate) {
        uLongf cap = ::compressBound(static_cast<uLong>(payload.size()));
        body.resize(cap);
        ::compress2(body.data(), &cap, payload.data(),
                    static_cast<uLong>(payload.size()), 9);
        body.resize(cap);
    } else {
        body = payload;
    }
    std::vector<uint8_t> f(kWsHeaderBytes, 0);
    std::memcpy(f.data(), magic, 8);
    put_u64le(f.data() + kWsOffPlain, declared_plain);
    put_u64le(f.data() + kWsOffStored, body.size());
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

}  // namespace s2

// ═════════════════════════════════════════════════════════════════════════
// STAGE S2 — the field descriptor layer
// ═════════════════════════════════════════════════════════════════════════
//
// Spec: doc/design/NEXT-SNAPSHOT-FORMAT.md §9 (the descriptor and its three
// realisations), §6.2 (the encoding table), §5.3 / §9.3 (what a generated
// schema is and is not worth), §16.3 (the staleness gate), §17.1 (the
// byte-identity gate).
//
// ── WHAT S2 IS, AND WHAT THESE ROWS THEREFORE PROVE ──────────────────────
//
// S2 builds the LAYER. It migrates NO subsystem — that is S3-S5 — so no row
// below can claim that any real subsystem's bytes are unchanged. What they
// claim, and what S3-S5 rest on, is narrower and stated exactly:
//
//   * `BinWriteDesc` emits, for every primitive, the SAME BYTES a
//     hand-written `save_state` in the tree's own idiom emits (JNSD-B01).
//     The oracle is `PilotState::hand_save` below, transcribed from §6.2's
//     encoding table and from the two history layouts it cites
//     (`ula.cpp:1586-1590`, `uart.h:53-57`) — NOT from the descriptor.
//   * The JSON and binary realisations of ONE declaration restore the same
//     machine (JNSD-J02).
//   * A file value is never trusted for a size, a width or a count (JNSA-*).
//
// WHAT THEY DO NOT PROVE. That the real 2 292 965-byte stream is unchanged:
// nothing is migrated yet, so there is nothing to compare. That gate is
// §17.1's `cmp` against the pre-migration golden, and S2's contribution to it
// is the extractor (`--extract-golden`, JNSG-*) plus the sentinel encoding it
// depends on. Say which is which; do not let one stand in for the other.
//
// ── AND WHAT THE SCHEMA ROWS DO NOT PROVE ────────────────────────────────
//
// A schema generated from a declaration, checked against JSON produced from
// the SAME declaration, agrees with itself by construction —
// `feedback_self_consistent_generated_data`: idempotence is not accuracy. The
// JNSS rows below assert the GENERATED SHAPE against §6.2's table, which is an
// external statement of what each type must encode to. Semantics are checked
// by a human reading the schema diff every field change produces
// (`make schema-check`), and by the hand-written constraint overlay. Neither
// is a row here, and §9.3 already says so.
//
// ── ROW INDEX (S2) ───────────────────────────────────────────────────────
//
//   JNSD-*   the three realisations over one declaration: byte identity with
//            hand-written code, round-trips, and the JSON/binary agreement
//   JNSE-*   the §6.2 encoding table, entry by entry
//   JNSH-*   the two count-prefixed history primitives and their four
//            differences (count width, element, order, padding)
//   JNSA-*   ADVERSARIAL input: every length, count and index in a file
//            treated as hostile
//   JNSS-*   the generated schema's shape
//   JNSG-*   the §17.1 byte-identity gate's scaffolding: the warm-start
//            extractor and the sentinel encoding

using jnext::save::BinReadDesc;
using jnext::save::BinWriteDesc;
using jnext::save::EnumNames;
using jnext::save::FifoAccess;
using jnext::save::FifoElem;
using jnext::save::JsonReadDesc;
using jnext::save::JsonWriteDesc;
using jnext::save::LogAccess;
using jnext::save::LogArray;
using jnext::save::SchemaDesc;
using jnext::save::StateDesc;

namespace s2 {

// ── The enum name table (§6.2) ───────────────────────────────────────────
//
// A closed set. The FSM-renumbering property this buys is the point: change
// the ordinals and the JSON is unchanged; change a NAME and every file that
// used it stops loading, loudly, with the name in the message.
const char* const kModeNames[] = {"idle", "wait_for_vpos", "move", "stop"};
const EnumNames kModes{kModeNames, 4};

// ── A FIFO with the layout §6.2's right-hand column states ───────────────
//
// Transcribed from that table — u64 count, ring-normalised oldest-first,
// zero-padded to capacity — which is also what `uart.h:53-57` implements. The
// test owns its own so a row can construct a ring with a non-zero tail, which
// is the only way to tell "ring-normalised" from "raw array order" apart.
template <typename T, std::size_t Capacity>
class TestFifo final : public FifoAccess {
public:
    std::size_t capacity() const override { return Capacity; }
    std::size_t size() const override { return count_; }
    uint16_t oldest(std::size_t i) const override {
        return static_cast<uint16_t>(buf_[(tail_ + i) % Capacity]);
    }
    void reset() override { head_ = tail_ = count_ = 0; }
    bool push(uint16_t v) override {
        if (count_ >= Capacity) return false;
        buf_[head_] = static_cast<T>(v);
        head_       = (head_ + 1) % Capacity;
        ++count_;
        return true;
    }

    /// Advance the ring so `tail_ != 0` — the state a raw-array encoding
    /// would get wrong and a normalised one would not.
    void rotate(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            if (count_ == 0) break;
            const T v = buf_[tail_];
            tail_     = (tail_ + 1) % Capacity;
            --count_;
            push(v);
        }
    }

    bool same_as(const TestFifo& o) const {
        if (count_ != o.count_) return false;
        for (std::size_t i = 0; i < count_; ++i) {
            if (oldest(i) != o.oldest(i)) return false;
        }
        return true;
    }

    T           buf_[Capacity]{};
    std::size_t head_ = 0, tail_ = 0, count_ = 0;
};

/// The one log element shape in the tree (`ula.h:875-878`).
struct PortFFChange {
    uint16_t line  = 0;
    uint8_t  value = 0;
};

// ── The pilot declaration ────────────────────────────────────────────────
//
// Every primitive `StateDesc` has, once. Field NAMES and ORDER are chosen to
// mirror real subsystems so the shapes are the shapes S3-S5 will meet:
// `divmmc`'s bank and entry points, `ula`'s flash counter and port-0xFF log,
// the CPU's two `/INT` window stamps, `uart`'s two FIFO element widths.
struct PilotState {
    static constexpr std::size_t kLogCap    = 8;
    static constexpr std::size_t kRamBytes  = 64;
    static constexpr std::size_t kPrivBytes = 32;
    static constexpr uint32_t    kSentinelMagic = 0x4A4E5354;  // "JNST"

    bool     enabled       = false;
    uint8_t  bank          = 0;
    uint16_t current_line  = 0;
    uint32_t frame_num     = 0;
    uint64_t monotonic     = 0;
    int32_t  flash_counter = 0;
    int64_t  int_first_ts  = 0;
    int64_t  int_last_ts   = 0;
    uint8_t  entry_points[4]{};
    uint8_t  mode = 0;
    uint8_t  priv_ram[kPrivBytes]{};
    uint8_t* window = nullptr;   ///< a window onto RAM page 16, owned elsewhere

    PortFFChange log[kLogCap]{};
    std::size_t  log_count = 0;

    TestFifo<uint8_t, 6>  tx;
    TestFifo<uint16_t, 5> rx;

    /// The ONE declaration. Everything else is a realisation over it.
    void describe_state(StateDesc& d) {
        d.boolean("enabled", enabled, false);
        d.u8     ("bank", bank, 0x00);
        d.u16    ("current_line", current_line, 0);
        d.u32    ("frame_num", frame_num);            // required: no default
        d.u64    ("monotonic", monotonic, 0);
        d.i32    ("flash_counter", flash_counter, 0);
        d.i64    ("int_first_ts", int_first_ts);      // required
        d.i64_open("int_last_ts", int_last_ts);       // required
        d.bytes  ("entry_points", entry_points, 4);
        d.enum8  ("mode", mode, kModes, 0);
        d.blob   ("mem/pilot-priv.bin", priv_ram, kPrivBytes);
        d.ram_window("ram", window, kRamBytes, /*page=*/16);
        LogArray<PortFFChange> la(log);
        d.log("port_ff_log", la, log_count, kLogCap);
        d.fifo("tx_fifo", tx, FifoElem::U8);
        d.fifo("rx_fifo", rx, FifoElem::U16);
        d.sentinel("pilot", kSentinelMagic, 0);
    }

    /// The SAME declaration with two adjacent scalars swapped. §9.2's third
    /// consequence is a testable claim: reordering changes the BINARY stream
    /// and leaves the JSON identical, because the JSON is what files are made
    /// of and the binary is positional.
    void describe_state_swapped(StateDesc& d) {
        d.u8     ("bank", bank, 0x00);
        d.boolean("enabled", enabled, false);
        d.u16    ("current_line", current_line, 0);
        d.u32    ("frame_num", frame_num);
        d.u64    ("monotonic", monotonic, 0);
        d.i32    ("flash_counter", flash_counter, 0);
        // TWO of the swapped pairs are REQUIRED declarations, deliberately: a
        // reorder confined to defaulted fields would leave the `required` list
        // in the same order whether or not the generator sorts it, and the
        // order-independence claim would be untested. (Found by mutation.)
        d.i64_open("int_last_ts", int_last_ts);
        d.i64    ("int_first_ts", int_first_ts);
        d.bytes  ("entry_points", entry_points, 4);
        d.enum8  ("mode", mode, kModes, 0);
        d.blob   ("mem/pilot-priv.bin", priv_ram, kPrivBytes);
        d.ram_window("ram", window, kRamBytes, /*page=*/16);
        LogArray<PortFFChange> la(log);
        d.log("port_ff_log", la, log_count, kLogCap);
        d.fifo("tx_fifo", tx, FifoElem::U8);
        d.fifo("rx_fifo", rx, FifoElem::U16);
        d.sentinel("pilot", kSentinelMagic, 0);
    }

    // ── THE ORACLE ───────────────────────────────────────────────────────
    //
    // A hand-written serialiser in the tree's own idiom, transcribed from
    // §6.2's encoding table and from the two layouts it cites. It is NOT
    // derived from `BinWriteDesc`; it is what `BinWriteDesc` has to match, and
    // JNSD-B01 is the comparison. Written first, deliberately: a "hand-written
    // oracle" copied out of the code it is supposed to adjudicate is not one.
    void hand_save(StateWriter& w) const {
        w.write_bool(enabled);
        w.write_u8(bank);
        w.write_u16(current_line);
        w.write_u32(frame_num);
        w.write_u64(monotonic);
        w.write_i32(flash_counter);
        // No `StateWriter::write_i64` exists; the tree's idiom is the cast
        // (`emulator.cpp:11844-11845`).
        w.write_u64(static_cast<uint64_t>(int_first_ts));
        w.write_u64(static_cast<uint64_t>(int_last_ts));
        w.write_bytes(entry_points, 4);
        w.write_u8(mode);
        w.write_bytes(priv_ram, kPrivBytes);
        w.write_bytes(window, kRamBytes);
        // The log: u16 count, then ALWAYS `kLogCap` entries in raw array
        // order (`ula.cpp:1586-1590`).
        w.write_u16(static_cast<uint16_t>(log_count));
        for (std::size_t i = 0; i < kLogCap; ++i) {
            w.write_u16(log[i].line);
            w.write_u8(log[i].value);
        }
        // The FIFOs: u64 count, then ALWAYS `Capacity` elements oldest-first,
        // zero past the count (`uart.h:53-57`).
        w.write_u64(tx.count_);
        for (std::size_t i = 0; i < 6; ++i) {
            w.write_u8(i < tx.count_ ? static_cast<uint8_t>(tx.oldest(i)) : 0);
        }
        w.write_u64(rx.count_);
        for (std::size_t i = 0; i < 5; ++i) {
            w.write_u16(i < rx.count_ ? rx.oldest(i) : 0);
        }
        w.write_u32(kSentinelMagic ^ 0u);
    }

    bool same_as(const PilotState& o) const {
        if (enabled != o.enabled || bank != o.bank ||
            current_line != o.current_line || frame_num != o.frame_num ||
            monotonic != o.monotonic || flash_counter != o.flash_counter ||
            int_first_ts != o.int_first_ts || int_last_ts != o.int_last_ts ||
            mode != o.mode || log_count != o.log_count) {
            return false;
        }
        if (std::memcmp(entry_points, o.entry_points, 4) != 0) return false;
        if (std::memcmp(priv_ram, o.priv_ram, kPrivBytes) != 0) return false;
        for (std::size_t i = 0; i < log_count; ++i) {
            if (log[i].line != o.log[i].line) return false;
            if (log[i].value != o.log[i].value) return false;
        }
        return tx.same_as(o.tx) && rx.same_as(o.rx);
    }
};

/// A populated pilot with a value in every field that a defaulted or dropped
/// field could not accidentally reproduce: no zeros, a NEGATIVE i32 and i64,
/// an `INT64_MAX`, a `u64` past 2^53, a non-zero FIFO tail, and a log with
/// live entries AND a stale tail past the count.
struct Pilot {
    uint8_t     ram[PilotState::kRamBytes]{};
    PilotState  s;

    Pilot() {
        s.window        = ram;
        s.enabled       = true;
        s.bank          = 0x2A;
        s.current_line  = 191;
        s.frame_num     = 41291;
        // Past 2^53: a JSON NUMBER would round this and a string does not
        // (§7.4). 9007199254740993 == 2^53 + 1, the smallest integer a double
        // cannot represent.
        s.monotonic     = 9007199254740993ULL;
        s.flash_counter = -7;
        // §16.1 names this value: the /INT window's real measured delta.
        s.int_first_ts  = -564933;
        s.int_last_ts   = INT64_MAX;      // the open-ended sentinel
        s.entry_points[0] = 0x83; s.entry_points[1] = 0x01;
        s.entry_points[2] = 0x00; s.entry_points[3] = 0xCD;
        s.mode          = 1;              // "wait_for_vpos"
        for (std::size_t i = 0; i < PilotState::kPrivBytes; ++i) {
            s.priv_ram[i] = static_cast<uint8_t>(0xA0 + i);
        }
        for (std::size_t i = 0; i < PilotState::kRamBytes; ++i) {
            ram[i] = static_cast<uint8_t>(i * 3 + 1);
        }
        // Three live entries and a STALE tail: entries past `count` are
        // whatever was there, and the binary encoding carries them
        // (`ula.cpp:1622-1625`).
        s.log[0] = {8, 0x01};
        s.log[1] = {64, 0x02};
        s.log[2] = {191, 0x04};
        s.log[3] = {0xDEAD & 0xFFFF, 0xEE};   // stale
        s.log[4] = {0xBEEF & 0xFFFF, 0xFF};   // stale
        s.log_count = 3;
        // A ring whose tail is NOT zero: push 4, rotate 2. Raw array order
        // and ring-normalised order now differ.
        s.tx.push(0x11); s.tx.push(0x22); s.tx.push(0x33); s.tx.push(0x44);
        s.tx.rotate(2);
        s.rx.push(0x0101); s.rx.push(0x0202); s.rx.push(0x1FF);
        s.rx.rotate(1);
    }
};

std::vector<uint8_t> bin_of(PilotState& p,
                            void (PilotState::*desc)(StateDesc&) = nullptr) {
    StateWriter measure;
    BinWriteDesc md(measure);
    if (desc) (p.*desc)(md); else p.describe_state(md);
    std::vector<uint8_t> out(measure.position());
    StateWriter w(out.data(), out.size());
    BinWriteDesc bd(w);
    if (desc) (p.*desc)(bd); else p.describe_state(bd);
    return out;
}

std::vector<uint8_t> hand_bin_of(const PilotState& p) {
    StateWriter measure;
    p.hand_save(measure);
    std::vector<uint8_t> out(measure.position());
    StateWriter w(out.data(), out.size());
    p.hand_save(w);
    return out;
}

std::string json_of(PilotState& p) {
    JsonWriteDesc jd;
    p.describe_state(jd);
    return jd.str();
}

/// Parse a produced document, mutate one key, re-emit. The adversarial rows
/// feed the result back to `JsonReadDesc`: a crafted document, not writer
/// output, for the same reason S1's `JNSC-REF-*` archives are hand-patched.
std::string json_with(const std::string& src, const char* key,
                      const std::string& raw_json_value) {
    // Deliberately textual, so a row can inject a value nlohmann's typed API
    // would refuse to construct.
    const std::string needle = std::string("\"") + key + "\": ";
    const std::size_t at = src.find(needle);
    if (at == std::string::npos) return src;   // a row asserts the effect
    const std::size_t vstart = at + needle.size();
    // Find the end of this value: the next `,\n` or `\n}` at any depth 0 for
    // scalars. The pilot's mutated keys are all scalars or one-line strings.
    std::size_t vend = vstart;
    int depth = 0;
    bool in_str = false, esc = false;
    for (; vend < src.size(); ++vend) {
        const char c = src[vend];
        if (esc) { esc = false; continue; }
        if (in_str) {
            if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '[' || c == '{') { ++depth; continue; }
        if (c == ']' || c == '}') { if (depth == 0) break; --depth; continue; }
        if (c == ',' && depth == 0) break;
    }
    return src.substr(0, vstart) + raw_json_value + src.substr(vend);
}

}  // namespace s2
}  // namespace

// ═════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    std::string emit_dir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--emit-corpus") == 0 && i + 1 < argc) {
            emit_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--extract-golden") == 0 &&
                   i + 2 < argc) {
            // §17.1, as a tool rather than a shell pipeline: capture the
            // pre-migration state stream from a warm-start cache so S3-S5 can
            // `cmp` against it after every migrated subsystem. The JNSG
            // rows below cover this exact code path.
            const std::string in_path  = argv[i + 1];
            const std::string out_path = argv[i + 2];
            std::ifstream is(in_path, std::ios::binary);
            if (!is) {
                std::fprintf(stderr, "cannot read %s\n", in_path.c_str());
                return 2;
            }
            std::vector<uint8_t> file((std::istreambuf_iterator<char>(is)),
                                      std::istreambuf_iterator<char>());
            std::vector<uint8_t> stream;
            std::string why;
            if (!s2::extract_warm_start(file, stream, why)) {
                std::fprintf(stderr, "%s: %s\n", in_path.c_str(), why.c_str());
                return 2;
            }
            std::ofstream os(out_path, std::ios::binary | std::ios::trunc);
            os.write(reinterpret_cast<const char*>(stream.data()),
                     static_cast<std::streamsize>(stream.size()));
            if (!os.good()) {
                std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
                return 2;
            }
            std::printf("%zu bytes -> %s\n", stream.size(), out_path.c_str());
            return 0;
        }
    }

    std::string why;

    // ─────────────────────────────────────────────────────────────────────
    // JNSC — the ZIP container
    // ─────────────────────────────────────────────────────────────────────
    {
        const std::string payload = "hello, snapshot";
        jnext::zip::Writer w{jnext::jns::kArchiveComment};
        bool added = w.add("state/cpu.json", payload, jnext::zip::Method::Stored,
                           why);
        std::vector<uint8_t> z;
        const bool fin = w.finish(z, why);
        if (fin) record_zip("jnsc01-stored", z);

        jnext::zip::Reader r;
        std::string got;
        const bool opened = r.open(z.data(), z.size(), why);
        const bool read   = opened && r.read_text("state/cpu.json", got, why);
        check("JNSC-01",
              "a STORED member round-trips through the writer and the reader "
              "byte for byte (§6: the --snapshot-uncompressed mode is the same "
              "code path as the compressed one)",
              added && fin && opened && read && got == payload,
              det("added=%d fin=%d open=%d read=%d got=%zu", added, fin, opened,
                  read, got.size()));
    }
    {
        // 4 KB of a repeating pattern: compressible enough that deflate is
        // chosen, and large enough that a framing error shows up as a length
        // or CRC failure rather than being masked.
        std::vector<uint8_t> payload(4096);
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<uint8_t>(i & 0x0F);

        jnext::zip::Writer w{jnext::jns::kArchiveComment};
        w.add("mem/ram.bin", payload.data(), payload.size(),
              jnext::zip::Method::Deflate, why);
        std::vector<uint8_t> z;
        const bool fin = w.finish(z, why);
        if (fin) record_zip("jnsc02-deflate", z);

        jnext::zip::Reader r;
        std::vector<uint8_t> got;
        const bool opened = r.open(z.data(), z.size(), why);
        const bool read   = opened && r.read("mem/ram.bin", got, why);
        check("JNSC-02",
              "a DEFLATE member round-trips byte for byte (raw deflate, "
              "windowBits -15, which is what ZIP method 8 is)",
              fin && opened && read && got == payload,
              det("fin=%d open=%d read=%d bytes=%zu", fin, opened, read,
                  got.size()));

        const jnext::zip::Entry* e = opened ? r.find("mem/ram.bin") : nullptr;
        check("JNSC-03",
              "a compressible member is actually stored with method 8, and "
              "smaller than its input — otherwise the container is paying for "
              "a compressor it does not use",
              e != nullptr && e->method == jnext::zip::Method::Deflate &&
                  e->comp_size < e->uncomp_size,
              e ? det("method=%d %u->%u", static_cast<int>(e->method),
                      e->uncomp_size, e->comp_size)
                : "no entry");
    }
    {
        // Already-random bytes: deflate cannot shrink them, so the writer must
        // fall back to STORED. A "compressed" member larger than its input is
        // a pure loss, and the reader handles both methods anyway.
        std::vector<uint8_t> payload(2048);
        uint32_t s = 0x12345678u;
        for (size_t i = 0; i < payload.size(); ++i) {
            s = s * 1103515245u + 12345u;
            payload[i] = static_cast<uint8_t>(s >> 24);
        }
        jnext::zip::Writer w{jnext::jns::kArchiveComment};
        w.add("mem/noise.bin", payload.data(), payload.size(),
              jnext::zip::Method::Deflate, why);
        std::vector<uint8_t> z;
        const bool fin = w.finish(z, why);
        if (fin) record_zip("jnsc04-incompressible", z);

        jnext::zip::Reader r;
        std::vector<uint8_t> got;
        const bool opened = r.open(z.data(), z.size(), why);
        const bool read   = opened && r.read("mem/noise.bin", got, why);
        const jnext::zip::Entry* e = opened ? r.find("mem/noise.bin") : nullptr;
        check("JNSC-04",
              "an incompressible member falls back to STORED rather than "
              "growing, and still round-trips",
              fin && opened && read && got == payload && e != nullptr &&
                  e->method == jnext::zip::Method::Stored,
              e ? det("method=%d %u->%u", static_cast<int>(e->method),
                      e->uncomp_size, e->comp_size)
                : "no entry");
    }
    {
        const std::vector<uint8_t> z = build_good("jnsc05-good");
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-05",
              "the archive comment survives the round-trip verbatim, so "
              "`unzip -z` identifies the file and its grammar with no JSON "
              "parse and a TRUNCATED file is still classifiable (§6)",
              opened && r.comment() == jnext::jns::kArchiveComment,
              det("comment='%s'", opened ? r.comment().c_str() : "?"));

        check("JNSC-06",
              "members appear in add order, so `manifest.json` is member 0 "
              "(§6) and `unzip -l` reads sensibly",
              opened && r.entries().size() == 3 &&
                  r.entries()[0].name == jnext::jns::kManifestMember &&
                  r.entries()[1].name == "state/cpu.json" &&
                  r.entries()[2].name == "mem/bank5-vram.bin",
              opened ? det("%zu entries, first='%s'", r.entries().size(),
                           r.entries().empty() ? "?"
                                               : r.entries()[0].name.c_str())
                     : "not opened");
    }
    {
        jnext::zip::Writer w{jnext::jns::kArchiveComment};
        const bool added = w.add("meta/empty.txt", nullptr, 0,
                                 jnext::zip::Method::Deflate, why);
        std::vector<uint8_t> z;
        const bool fin = w.finish(z, why);
        if (fin) record_zip("jnsc07-empty-member", z);
        jnext::zip::Reader r;
        std::vector<uint8_t> got{0xFF};
        const bool opened = r.open(z.data(), z.size(), why);
        const bool read   = opened && r.read("meta/empty.txt", got, why);
        check("JNSC-07",
              "a zero-length member round-trips; an empty payload is a real "
              "case (a subsystem with nothing to say) and must not be a "
              "special one",
              added && fin && opened && read && got.empty(),
              det("added=%d fin=%d open=%d read=%d n=%zu", added, fin, opened,
                  read, got.size()));
    }
    {
        const std::vector<uint8_t> a = build_good("");
        const std::vector<uint8_t> b = build_good("");
        check("JNSC-08",
              "two writes of the same inputs are BYTE-IDENTICAL — every member "
              "carries a fixed DOS timestamp rather than the wall clock, so a "
              "written archive is comparable in a test and legible in a diff",
              !a.empty() && a == b,
              det("%zu vs %zu bytes", a.size(), b.size()));

        const Layout L = layout_of(a);
        bool all_fixed = !L.lfh.empty();
        for (size_t o : L.lfh) {
            if (rd16(a, o + 10) != 0x0000 || rd16(a, o + 12) != 0x0021)
                all_fixed = false;
        }
        for (size_t o : L.cdh) {
            if (rd16(a, o + 12) != 0x0000 || rd16(a, o + 14) != 0x0021)
                all_fixed = false;
        }
        check("JNSC-09",
              "every local and central header carries the DOS epoch "
              "(1980-01-01 00:00:00), never the wall clock — the capture time "
              "lives in manifest.created, which is where a reader looks",
              all_fixed,
              det("%zu local, %zu central headers", L.lfh.size(), L.cdh.size()));
    }
    {
        jnext::zip::Writer w{jnext::jns::kArchiveComment};
        w.add("state/cpu.json", kJson, jnext::zip::Method::Deflate, why);
        const bool dup = w.add("state/cpu.json", kJson,
                               jnext::zip::Method::Deflate, why);
        check("JNSC-10",
              "the WRITER refuses a duplicate member name, naming it — the "
              "reader's refusal (JNSC-REF-16) is the second line of defence, "
              "not the only one",
              refused_naming("JNSC-10", dup, why, "'state/cpu.json'"),
              why);
    }
    {
        jnext::zip::Writer w{jnext::jns::kArchiveComment};
        std::vector<uint8_t> z;
        const bool fin = w.finish(z, why);
        check("JNSC-11", "the writer refuses to emit an archive with no members",
              refused_naming("JNSC-11", fin, why, "no members"), why);
    }
    {
        // 1 MB of mostly-zero data — the shape real guest RAM has, and the
        // case the container was sized for (§5.2: ~130-140 KB for a full 2 MB
        // machine). Proves the deflate path at a size where a 32-bit field
        // error or a windowBits mistake would show.
        std::vector<uint8_t> big(1024 * 1024, 0);
        for (size_t i = 0; i < big.size(); i += 4096) big[i] = 0x5A;

        SnapshotWriter w(false);
        w.set_manifest(make_manifest());
        const bool added = w.add_blob("mem/ram.bin", big.data(), big.size(), why);
        std::vector<uint8_t> z;
        const bool fin = added && w.finish(z, why);
        if (fin) record_corpus("jnsc12-1mb-ram", z);

        jnext::zip::Reader r;
        std::vector<uint8_t> got;
        const bool opened = fin && r.open(z.data(), z.size(), why);
        const bool read   = opened && r.read("mem/ram.bin", got, why);
        check("JNSC-12",
              "a 1 MB mostly-zero blob round-trips byte-exactly and deflates "
              "to under a tenth of its size — the shape and scale guest RAM "
              "actually has",
              fin && opened && read && got == big && z.size() < big.size() / 10,
              det("added=%d fin=%d open=%d read=%d archive=%zu", added, fin,
                  opened, read, z.size()));
    }

    {
        // Settled point 6's debugging mode. It is a flag on the member writer
        // rather than a second code path, so the same reader reads it — which
        // is only true if it is exercised, and nothing exercised it until the
        // mutation battery noticed.
        const std::vector<uint8_t> comp   = build_good("jnsc13-compressed");
        const std::vector<uint8_t> stored =
            build_good("jnsc13-uncompressed", /*uncompressed=*/true);

        jnext::zip::Reader rc, rs;
        Manifest mc, ms;
        Verdict vc, vs;
        const bool okc = jnext::jns::open_snapshot(comp.data(), comp.size(),
                                                   make_env(), rc, mc, vc);
        const bool oks = jnext::jns::open_snapshot(stored.data(), stored.size(),
                                                   make_env(), rs, ms, vs);
        bool all_stored = oks && !rs.entries().empty();
        for (const jnext::zip::Entry& e : rs.entries()) {
            if (e.method != jnext::zip::Method::Stored) all_stored = false;
        }
        bool any_deflated = false;
        for (const jnext::zip::Entry& e : rc.entries()) {
            if (e.method == jnext::zip::Method::Deflate) any_deflated = true;
        }
        std::vector<uint8_t> bc, bs;
        const bool readc = okc && rc.read("mem/bank5-vram.bin", bc, why);
        const bool reads = oks && rs.read("mem/bank5-vram.bin", bs, why);
        check("JNSC-13",
              "--snapshot-uncompressed stores EVERY member (method 0) while "
              "the default deflates, and both restore the identical bytes "
              "through the identical reader — settled point 6's debug mode is "
              "a flag on the member writer, not a second code path",
              okc && oks && all_stored && any_deflated && readc && reads &&
                  bc == bs && stored.size() > comp.size(),
              det("okc=%d oks=%d stored=%d defl=%d %zu vs %zu", okc, oks,
                  all_stored, any_deflated, comp.size(), stored.size()));
    }
    {
        const std::vector<uint8_t> z = build_good("");
        jnext::zip::Reader r;
        r.open(z.data(), z.size(), why);
        std::vector<uint8_t> got{0xFF};
        std::string rw;
        const bool read = r.read("mem/nonexistent.bin", got, rw);
        check("JNSC-14",
              "reading a member the archive does not hold is refused, naming "
              "it, and yields nothing",
              refused_naming("JNSC-14", read, rw, "'mem/nonexistent.bin'") &&
                  got.empty(),
              rw);
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSC-PATH — the member-path grammar (§6)
    // ─────────────────────────────────────────────────────────────────────
    //
    // Tested directly against `valid_member_path`, because the grammar has
    // more refusal reasons than it is worth crafting an archive for, and then
    // through the reader's central-directory walk (PATH-18..20) so the direct
    // rows cannot pass while the reader quietly fails to call it.
    {
        struct Case {
            const char* row;
            const char* path;
            bool        want_ok;
            const char* names;   ///< a substring the refusal must contain
            const char* desc;
        };
        const Case cases[] = {
            {"JNSC-PATH-01", "manifest.json", true, "",
             "a plain lower-case name is accepted"},
            {"JNSC-PATH-02", "mem/bank5-vram.bin", true, "",
             "a nested path with a dash and a dot is accepted"},
            {"JNSC-PATH-03", "", false, "empty",
             "an empty path is refused"},
            {"JNSC-PATH-04", "/etc/passwd", false, "absolute",
             "an ABSOLUTE path is refused and named as absolute — a snapshot "
             "is not an extraction target"},
            {"JNSC-PATH-05", "state\\cpu.json", false, "backslash",
             "a BACKSLASH is refused and named — Windows tooling treats it as "
             "a separator, so it is a traversal vector in disguise"},
            {"JNSC-PATH-06", "state/Cpu.json", false, "upper-case",
             "an UPPER-CASE letter is refused and named; paths are lower-case "
             "so two members cannot differ only by case on a case-folding "
             "filesystem"},
            {"JNSC-PATH-07", "state/../../etc/passwd", false, "'..'",
             "a '..' COMPONENT is refused — the character class alone cannot "
             "catch this, because both '.' and '/' are legal characters"},
            {"JNSC-PATH-08", "../escape", false, "start",
             "a LEADING '..' is refused at the first-character rule"},
            {"JNSC-PATH-09", "state/./cpu.json", false, "'.'",
             "a '.' component is refused"},
            {"JNSC-PATH-10", "state/", false, "empty component",
             "a TRAILING '/' is refused — that is a ZIP directory entry, which "
             "the writer never emits"},
            {"JNSC-PATH-11", "state//cpu.json", false, "empty component",
             "a DOUBLED '/' is refused"},
            {"JNSC-PATH-12", "-rf", false, "start",
             "a leading '-' is refused; a member name must not be mistakable "
             "for an option by any tool a user pipes the archive through"},
            {"JNSC-PATH-13", "state/cpu json", false, "0x20",
             "a SPACE is refused, naming the byte"},
            {"JNSC-PATH-14", "state/cpu\x01.json", false, "0x01",
             "a control byte is refused, naming it"},
            {"JNSC-PATH-15", "state/caf\xc3\xa9.json", false, "0xc3",
             "a non-ASCII byte is refused; the grammar is ASCII so no encoding "
             "question can arise between us and an external reader"},
        };
        for (const Case& c : cases) {
            std::string w;
            const bool ok = jnext::zip::valid_member_path(c.path, w);
            bool good;
            if (c.want_ok) {
                good = ok && w.empty();
            } else {
                good = refused_naming(c.row, ok, w, c.names);
            }
            check(c.row, c.desc, good, ok ? "accepted" : w);
        }

        std::string w;
        const std::string too_long(300, 'a');
        const bool ok = jnext::zip::valid_member_path(too_long, w);
        check("JNSC-PATH-16",
              "an over-long path is refused, so a hostile central directory "
              "cannot make the reader allocate on a 64 KB name",
              refused_naming("JNSC-PATH-16", ok, w, "255"), w);
    }
    {
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        const bool added = wr.add("../escape", kJson,
                                  jnext::zip::Method::Deflate, why);
        check("JNSC-PATH-17",
              "the WRITER applies the same grammar, so a bad path cannot enter "
              "an archive in the first place, and the archive is left unchanged",
              refused_naming("JNSC-PATH-17", added, why, "start") &&
                  wr.member_count() == 0,
              det("added=%d members=%zu", added, wr.member_count()));
    }
    {
        // Patch a member name IN PLACE to a same-length traversal, keeping
        // every offset valid, so the reader meets a well-formed archive whose
        // only defect is the path. Both the local header and the central
        // directory are patched: patching one would trip the
        // local-vs-central check instead and the row would pass for the wrong
        // reason.
        struct PathCase {
            const char* row;
            const char* from;
            const char* to;
            const char* names;
            const char* desc;
        };
        const PathCase cases[] = {
            {"JNSC-PATH-18", "aa/bb/cc.json", "aa/../cc.json", "'..'",
             "the READER refuses a traversal path in the central directory — "
             "the direct grammar rows above cannot prove the reader calls it"},
            {"JNSC-PATH-19", "aa/bb/cc.json", "/a/bb/cc.json", "absolute",
             "the reader refuses an absolute path in the central directory"},
            {"JNSC-PATH-20", "aa/bb/cc.json", "aa/bb/Cc.json", "upper-case",
             "the reader refuses an upper-case path in the central directory"},
        };
        for (const PathCase& c : cases) {
            jnext::zip::Writer wr{jnext::jns::kArchiveComment};
            wr.add(c.from, kJson, jnext::zip::Method::Stored, why);
            std::vector<uint8_t> z;
            wr.finish(z, why);
            const Layout L = layout_of(z);
            const size_t name_len = std::strlen(c.from);
            std::memcpy(z.data() + L.lfh[0] + 30, c.to, name_len);
            std::memcpy(z.data() + L.cdh[0] + 46, c.to, name_len);

            jnext::zip::Reader r;
            std::string w;
            const bool opened = r.open(z.data(), z.size(), w);
            check(c.row, c.desc, refused_naming(c.row, opened, w, c.names), w);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSC-REF — crafted archives: every feature outside the accepted subset
    // ─────────────────────────────────────────────────────────────────────
    {
        const std::string junk = "this is not a ZIP archive at all, not even "
                                 "slightly, and it is long enough to be one";
        jnext::zip::Reader r;
        const bool opened = r.open(
            reinterpret_cast<const uint8_t*>(junk.data()), junk.size(), why);
        check("JNSC-REF-01",
              "a file that is not a ZIP at all is refused, saying so",
              refused_naming("JNSC-REF-01", opened, why,
                             "not a ZIP archive"),
              why);
    }
    {
        const uint8_t tiny[4] = {'P', 'K', 3, 4};
        jnext::zip::Reader r;
        const bool opened = r.open(tiny, sizeof(tiny), why);
        check("JNSC-REF-02",
              "a file too short to hold an end-of-central-directory record is "
              "refused, naming both lengths",
              refused_naming("JNSC-REF-02", opened, why, "too short"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.eocd, 0x06054b51u);   // one bit off the EOCD signature
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-03",
              "a corrupted end-of-central-directory signature is refused; the "
              "backwards scan must not settle for a near miss",
              refused_naming("JNSC-REF-03", opened, why, "not a ZIP archive"),
              why);
    }
    {
        // Splice a ZIP64 EOCD locator immediately before the EOCD, which is
        // where a genuine ZIP64 archive carries it.
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        std::vector<uint8_t> loc(20, 0);
        loc[0] = 0x50; loc[1] = 0x4b; loc[2] = 0x06; loc[3] = 0x07;
        z.insert(z.begin() + static_cast<long>(L.eocd), loc.begin(), loc.end());
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-04",
              "a ZIP64 end-of-central-directory LOCATOR is refused; ZIP64 adds "
              "a second framing to test for no reachable benefit (§6)",
              refused_naming("JNSC-REF-04", opened, why, "ZIP64"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.eocd + 8, 0xFFFF);
        wr16(z, L.eocd + 10, 0xFFFF);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-05",
              "a ZIP64 SENTINEL in the EOCD entry count is refused — the "
              "locator and the sentinel are independent tells and either can "
              "appear without the other",
              refused_naming("JNSC-REF-05", opened, why, "ZIP64 sentinel"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.eocd + 12, 0xFFFFFFFFu);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-06",
              "a ZIP64 sentinel in the central-directory SIZE is refused",
              refused_naming("JNSC-REF-06", opened, why, "ZIP64 sentinel"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.eocd + 4, 1);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-07",
              "a spanned (multi-disk) archive is refused, naming the disk "
              "numbers",
              refused_naming("JNSC-REF-07", opened, why, "spanned"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.eocd + 8, static_cast<uint16_t>(L.count - 1));
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-08",
              "an EOCD whose this-disk count disagrees with its total count is "
              "refused, naming both",
              refused_naming("JNSC-REF-08", opened, why, "spanned"), why);
    }
    {
        struct FlagCase {
            const char* row;
            uint16_t    flag;
            const char* names;
            const char* desc;
        };
        const FlagCase cases[] = {
            {"JNSC-REF-09", 0x0008, "data descriptor",
             "a member with a DATA DESCRIPTOR (general-purpose flag bit 3) is "
             "refused and named as such: its sizes are not known at the local "
             "header, which is the framing ambiguity this reader exists to "
             "eliminate"},
            {"JNSC-REF-10", 0x0001, "encrypted",
             "an ENCRYPTED member is refused and named as such"},
            {"JNSC-REF-11", 0x0800, "general-purpose flags",
             "any other general-purpose flag (here the UTF-8 name bit) is "
             "refused: only 0 is accepted, because the subset is a rule and "
             "not a list of known-bad features"},
        };
        for (const FlagCase& c : cases) {
            std::vector<uint8_t> z = build_good("");
            const Layout L = layout_of(z);
            wr16(z, L.cdh[0] + 8, c.flag);
            wr16(z, L.lfh[0] + 6, c.flag);
            jnext::zip::Reader r;
            std::string w;
            const bool opened = r.open(z.data(), z.size(), w);
            check(c.row, c.desc, refused_naming(c.row, opened, w, c.names), w);
        }
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.cdh[0] + 10, 12);   // BZIP2
        wr16(z, L.lfh[0] + 8, 12);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-12",
              "a compression method other than 0 and 8 is refused, naming the "
              "method number",
              refused_naming("JNSC-REF-12", opened, why, "method 12"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.cdh[0] + 30, 4);    // central extra field length
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-13",
              "a central-directory EXTRA FIELD is refused; the writer emits "
              "none, so one arriving means a producer we do not model",
              refused_naming("JNSC-REF-13", opened, why, "extra field"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.cdh[0] + 32, 4);    // file comment length
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-14",
              "a per-member FILE COMMENT is refused",
              refused_naming("JNSC-REF-14", opened, why, "file comment"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.cdh[0] + 6, 45);    // version needed: ZIP64 era
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-15",
              "a member declaring a version-needed above 2.0 is refused, "
              "naming both versions",
              refused_naming("JNSC-REF-15", opened, why, "version 45"), why);
    }
    {
        // Two same-length names, then patch the second to equal the first in
        // BOTH headers — a genuine duplicate, which ZIP permits.
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        wr.add("state/aa.json", kJson, jnext::zip::Method::Stored, why);
        wr.add("state/bb.json", kJson, jnext::zip::Method::Stored, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        std::memcpy(z.data() + L.lfh[1] + 30, "state/aa.json", 13);
        std::memcpy(z.data() + L.cdh[1] + 46, "state/aa.json", 13);

        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-16",
              "DUPLICATE member names are refused outright rather than "
              "resolved — ZIP permits them and real readers disagree about "
              "which wins (first central entry, last central entry, last local "
              "header), which is the `.szx` failure shape exactly (§6)",
              refused_naming("JNSC-REF-16", opened, why, "more than once"),
              why);
    }
    {
        struct Disagree {
            const char* row;
            size_t      lfh_field;   ///< offset within the local header
            int         width;       ///< 2 or 4
            uint32_t    value;
            const char* names;
            const char* desc;
        };
        const Disagree cases[] = {
            {"JNSC-REF-17", 8, 2, 0,  "disagree",
             "a local header whose METHOD disagrees with the central directory "
             "is refused — this is the precise ambiguity where two conforming "
             "readers legitimately extract different bytes"},
            {"JNSC-REF-18", 14, 4, 0xdeadbeefu, "disagree",
             "a local header whose CRC-32 disagrees with the central directory "
             "is refused, naming both"},
            {"JNSC-REF-19", 22, 4, 999u, "disagree",
             "a local header whose UNCOMPRESSED SIZE disagrees with the "
             "central directory is refused, naming both"},
            {"JNSC-REF-20", 18, 4, 7u, "disagree",
             "a local header whose COMPRESSED SIZE disagrees with the central "
             "directory is refused, naming both"},
        };
        for (const Disagree& c : cases) {
            // Built DEFLATE so the method row (17) actually changes something:
            // patching method to 0 on a stored member would be a no-op.
            jnext::zip::Writer wr{jnext::jns::kArchiveComment};
            std::vector<uint8_t> payload(512, 0x3C);
            wr.add("mem/x.bin", payload.data(), payload.size(),
                   jnext::zip::Method::Deflate, why);
            std::vector<uint8_t> z;
            wr.finish(z, why);
            const Layout L = layout_of(z);
            if (c.width == 2) {
                wr16(z, L.lfh[0] + c.lfh_field, static_cast<uint16_t>(c.value));
            } else {
                wr32(z, L.lfh[0] + c.lfh_field, c.value);
            }
            jnext::zip::Reader r;
            std::string w;
            const bool opened = r.open(z.data(), z.size(), w);
            check(c.row, c.desc, refused_naming(c.row, opened, w, c.names), w);
        }
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.lfh[0], 0x04034b51u);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-21",
              "a corrupted LOCAL header signature is refused, naming the "
              "member and the offset",
              refused_naming("JNSC-REF-21", opened, why,
                             "local header signature"),
              why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.cdh[1], 0x02014b51u);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-22",
              "a corrupted CENTRAL-DIRECTORY signature is refused, naming the "
              "entry index",
              refused_naming("JNSC-REF-22", opened, why, "signature"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.eocd + 12, L.cd_size - 20);   // declare a shorter directory
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        const bool named = !opened && (why.find("truncated") != std::string::npos ||
                                       why.find("occupy") != std::string::npos);
        if (!opened) g_refusals.push_back({"JNSC-REF-23", why});
        check("JNSC-REF-23",
              "a central directory whose declared size cannot hold its "
              "declared entries is refused",
              named, why);
    }
    {
        // STORED, so the CRC check is unambiguously the thing that fires.
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        const std::string payload = "the quick brown fox";
        wr.add("state/cpu.json", payload, jnext::zip::Method::Stored, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        z[lfh_data(z, L.lfh[0])] ^= 0xFF;

        jnext::zip::Reader r;
        std::vector<uint8_t> got;
        const bool opened = r.open(z.data(), z.size(), why);
        std::string rw;
        const bool read = opened && r.read("state/cpu.json", got, rw);
        check("JNSC-REF-24",
              "a stored member whose bytes no longer match the declared CRC-32 "
              "is refused on READ, naming both CRCs — the header check at open "
              "cannot see the payload, so this is a separate gate",
              opened && refused_naming("JNSC-REF-24", read, rw, "CRC-32") &&
                  got.empty(),
              det("open=%d read=%d %s", opened, read, rw.c_str()));
    }
    {
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        std::vector<uint8_t> payload(2048, 0x11);
        wr.add("mem/x.bin", payload.data(), payload.size(),
               jnext::zip::Method::Deflate, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        const size_t d = lfh_data(z, L.lfh[0]);
        z[d + 3] ^= 0x5A;   // corrupt the deflate stream itself

        jnext::zip::Reader r;
        std::vector<uint8_t> got;
        const bool opened = r.open(z.data(), z.size(), why);
        std::string rw;
        const bool read = opened && r.read("mem/x.bin", got, rw);
        check("JNSC-REF-25",
              "a member whose DEFLATE stream is corrupt is refused on read as "
              "a STREAM failure, not merely as a CRC mismatch — naming the "
              "member and yielding nothing. The distinction matters: with the "
              "inflate check gone the CRC still refuses, so a row that only "
              "asserted 'refused' would pass over a broken decompressor",
              opened &&
                  refused_naming("JNSC-REF-25", read, rw,
                                 "corrupt or longer than") &&
                  rw.find("'mem/x.bin'") != std::string::npos && got.empty(),
              det("open=%d read=%d %s", opened, read, rw.c_str()));
    }
    {
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        const std::string payload = "stored member";
        wr.add("state/cpu.json", payload, jnext::zip::Method::Stored, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        // Both headers, so the local-vs-central check does not fire first.
        wr32(z, L.cdh[0] + 24, static_cast<uint32_t>(payload.size() + 1));
        wr32(z, L.lfh[0] + 22, static_cast<uint32_t>(payload.size() + 1));
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-26",
              "a STORED member whose compressed and uncompressed sizes "
              "disagree is refused; for method 0 they are the same number by "
              "definition",
              refused_naming("JNSC-REF-26", opened, why, "stored but declares"),
              why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.cdh[0] + 42, L.cd_offset);   // local header inside the CD
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-27",
              "a central entry pointing at a local header that does not fit "
              "before the central directory is refused, naming both offsets",
              refused_naming("JNSC-REF-27", opened, why, "does not fit"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.eocd + 8, 0);
        wr16(z, L.eocd + 10, 0);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-28",
              "an archive declaring zero members is refused rather than "
              "opening empty",
              refused_naming("JNSC-REF-28", opened, why, "no members"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.lfh[0] + 28, 4);   // local extra field length
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-29",
              "a LOCAL extra field is refused — it is checked separately from "
              "the central one, because a producer can emit either alone",
              refused_naming("JNSC-REF-29", opened, why, "local extra field"),
              why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.cdh[0] + 34, 1);   // disk number start
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-30",
              "a central entry claiming its member starts on another disk is "
              "refused, naming the member",
              refused_naming("JNSC-REF-30", opened, why, "disk 1"), why);
    }

    {
        // A file exactly one byte short of an EOCD. The short-file guard is
        // load-bearing against a size_t UNDERFLOW in the backwards scan
        // (`len - kEocdFixed + 1`), so this row pins the boundary rather than
        // only the obviously-tiny case of REF-02.
        std::vector<uint8_t> tiny(21, 0);
        jnext::zip::Reader r;
        const bool opened = r.open(tiny.data(), tiny.size(), why);
        check("JNSC-REF-31",
              "a file one byte short of an end-of-central-directory record is "
              "refused as TOO SHORT, naming both lengths — the guard that "
              "keeps the backwards scan from underflowing its start index",
              refused_naming("JNSC-REF-31", opened, why, "too short"), why);
    }
    {
        // An EOCD signature inside the ARCHIVE COMMENT, at a HIGHER offset
        // than the real record — so the backwards scan meets the impostor
        // first. Only the comment-length cross-check (`i + 22 + clen == len`)
        // rejects it; without that, the reader parses the impostor's disk
        // numbers out of comment text.
        std::string comment;
        comment += "PK";
        comment += static_cast<char>(0x05);
        comment += static_cast<char>(0x06);
        comment += std::string(36, 'A');
        jnext::zip::Writer wr{comment};
        const std::string payload = "real payload";
        wr.add("state/cpu.json", payload, jnext::zip::Method::Stored, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);

        jnext::zip::Reader r;
        std::string got;
        const bool opened = r.open(z.data(), z.size(), why);
        const bool read   = opened && r.read_text("state/cpu.json", got, why);
        check("JNSC-REF-32",
              "an end-of-central-directory SIGNATURE occurring inside the "
              "archive comment does not mislead the backwards scan: the "
              "comment-length cross-check rejects the impostor and the real "
              "record is found, comment intact",
              opened && read && got == payload && r.comment() == comment,
              det("open=%d read=%d comment=%zu", opened, read,
                  opened ? r.comment().size() : 0));
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.eocd + 16, L.cd_offset + 8);   // push the directory forward
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-33",
              "a central directory whose declared offset and size run past the "
              "end-of-central-directory record is refused, naming all three "
              "numbers — before any attempt to walk it",
              refused_naming("JNSC-REF-33", opened, why,
                             "runs past the end-of-central-directory"),
              why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.eocd + 8, static_cast<uint16_t>(L.count - 1));
        wr16(z, L.eocd + 10, static_cast<uint16_t>(L.count - 1));
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-34",
              "a central directory whose entries occupy LESS than its declared "
              "size is refused: the slack could hold a record one reader walks "
              "and another does not, which is the whole class this subset "
              "exists to remove",
              refused_naming("JNSC-REF-34", opened, why, "occupy"), why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr16(z, L.lfh[0] + 6, 0x0008);   // LOCAL header only
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-35",
              "general-purpose flags set in the LOCAL header ALONE are refused "
              "— the central-directory check cannot see them, and a producer "
              "can emit either header's flags without the other",
              refused_naming("JNSC-REF-35", opened, why,
                             "in its local header"),
              why);
    }
    {
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        wr.add("state/aa.json", kJson, jnext::zip::Method::Stored, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        std::memcpy(z.data() + L.lfh[0] + 30, "state/ab.json", 13);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-36",
              "a local header whose NAME disagrees with the central directory "
              "is refused, naming both. This is the sharpest form of the "
              "local-vs-central class: two conforming readers would extract "
              "DIFFERENT MEMBERS from the same archive",
              refused_naming("JNSC-REF-36", opened, why, "'state/ab.json'") &&
                  why.find("disagree") != std::string::npos,
              why);
    }
    {
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        // The deflated blob is member 2; stretch its compressed size so the
        // data extent reaches into the central directory.
        wr32(z, L.cdh[2] + 20, 100000u);
        wr32(z, L.lfh[2] + 18, 100000u);
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-37",
              "a member whose data extent runs into the central directory is "
              "refused, naming the member and both offsets",
              refused_naming("JNSC-REF-37", opened, why,
                             "running into the central directory"),
              why);
    }
    {
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        std::vector<uint8_t> payload(1024, 0x77);
        wr.add("mem/x.bin", payload.data(), payload.size(),
               jnext::zip::Method::Deflate, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        // Declare MORE uncompressed bytes than the stream yields, in both
        // headers so the local-vs-central check does not fire first. The
        // stream still ends cleanly, so zlib reports success — only the
        // explicit length comparison catches it.
        wr32(z, L.cdh[0] + 24, 2048u);
        wr32(z, L.lfh[0] + 22, 2048u);

        jnext::zip::Reader r;
        std::vector<uint8_t> got;
        const bool opened = r.open(z.data(), z.size(), why);
        std::string rw;
        const bool read = opened && r.read("mem/x.bin", got, rw);
        check("JNSC-REF-38",
              "a deflate stream that inflates SHORT of its declared length is "
              "refused, naming both lengths. zlib reports SUCCESS on this — "
              "the stream ends cleanly — so only the explicit exact-length "
              "comparison sees it (the warm-start cache's rule, §3.2)",
              opened && refused_naming("JNSC-REF-38", read, rw, "inflates to") &&
                  rw.find("2048") != std::string::npos && got.empty(),
              det("open=%d read=%d %s", opened, read, rw.c_str()));
    }

    {
        // THE UNBOUNDED-ALLOCATION REFUSAL. A ZIP's uncompressed-size field is
        // a bare 32-bit number in the central directory and NOTHING in the
        // archive's structure relates it to the compressed bytes actually
        // present. Measured on this branch before the ceiling existed: a
        // 161-byte archive declaring 0xFFFFFFF0 drove a 4 198 256 KB
        // allocation (1 048 798 minor faults) before failing; with the
        // ceiling it is refused at open() at 4 424 KB.
        //
        // Both headers are patched, consistently, so the local-vs-central
        // disagreement check cannot fire first and let this row pass for the
        // wrong reason.
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        std::vector<uint8_t> payload(4096, 0xA5);
        wr.add("mem/x.bin", payload.data(), payload.size(),
               jnext::zip::Method::Deflate, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        wr32(z, L.cdh[0] + 24, 0xFFFFFFF0u);
        wr32(z, L.lfh[0] + 22, 0xFFFFFFF0u);

        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-39",
              "a member DECLARING more uncompressed bytes than the ceiling is "
              "refused AT OPEN, naming the member and both sizes — before a "
              "byte is inflated and before anything can size an allocation "
              "from the declaration. An uncaught bad_alloc would terminate "
              "jnext on a hostile file, which is the inversion of G9",
              refused_naming("JNSC-REF-39", opened, why, "4294967280") &&
                  why.find("'mem/x.bin'") != std::string::npos &&
                  why.find("67108864") != std::string::npos &&
                  r.entries().empty(),
              why);
    }
    {
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        std::vector<uint8_t> payload(4096, 0x5A);
        wr.add("mem/x.bin", payload.data(), payload.size(),
               jnext::zip::Method::Deflate, why);
        std::vector<uint8_t> z;
        wr.finish(z, why);
        const Layout L = layout_of(z);
        wr32(z, L.cdh[0] + 20, 0x08000000u);   // 128 MB compressed
        wr32(z, L.lfh[0] + 18, 0x08000000u);

        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSC-REF-40",
              "the ceiling binds the COMPRESSED size too. It is bounded again "
              "by the data-extent check further on, but a ceiling that caught "
              "one declared size and missed the other would be half a rule",
              refused_naming("JNSC-REF-40", opened, why, "134217728") &&
                  why.find("'mem/x.bin'") != std::string::npos &&
                  // The LIMIT value appears only in the ceiling's own
                  // message. Without this the row passes on the data-extent
                  // refusal, which names the same declared size — and a
                  // ceiling that had silently dropped its compressed half
                  // would go unnoticed.
                  why.find("67108864") != std::string::npos,
              why);
    }
    {
        // The writer must not be able to emit what the reader refuses, or
        // jnext would produce files it cannot reopen. Checked before any copy
        // of the payload is made.
        std::vector<uint8_t> big(jnext::zip::kMaxMemberBytes + 1, 0);
        jnext::zip::Writer wr{jnext::jns::kArchiveComment};
        const bool added = wr.add("mem/huge.bin", big.data(), big.size(),
                                  jnext::zip::Method::Stored, why);
        check("JNSC-15",
              "the WRITER refuses a member over the same ceiling, naming it — "
              "a writer able to emit what its own reader refuses would produce "
              "snapshots jnext cannot reopen, a worse failure than declining "
              "to write one",
              refused_naming("JNSC-15", added, why, "67108864") &&
                  why.find("'mem/huge.bin'") != std::string::npos &&
                  wr.member_count() == 0,
              why);
    }
    {
        // A refusal that lands AFTER the central-directory walk: `entries_` is
        // populated by then, but `data_` is committed only at the very end.
        // Without the clearing wrapper the reader would be left with entries
        // and a null buffer, and a caller that skipped the return value would
        // dereference it. jnext's own call site checks correctly today; the
        // stages after this one add many more.
        std::vector<uint8_t> z = build_good("");
        const Layout L = layout_of(z);
        wr32(z, L.lfh[0], 0x04034b51u);   // corrupt a LOCAL header signature

        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);

        // Asserted BEFORE the read below, deliberately. This is the
        // OBSERVABLE half of the invariant; the read is the half that is
        // undefined behaviour when the invariant is broken, and a crash
        // cannot be reported as a row. Checking the observable half first
        // means a reader that stopped clearing is named here — check()
        // flushes — even though the process then dies on the next line.
        check("JNSC-16",
              "a FAILED open leaves the reader EMPTY — no entries, no comment "
              "— although this particular refusal fires after the "
              "central-directory walk has already filled the entry list. That "
              "window is exactly what the invariant covers: entries present "
              "while the buffer pointer is still null",
              !opened && r.entries().empty() && r.comment().empty(),
              det("opened=%d entries=%zu comment=%zu", opened,
                  r.entries().size(), r.comment().size()));

        std::vector<uint8_t> out{0xFF};
        std::string rw;
        const bool read = r.read("manifest.json", out, rw);
        check("JNSC-17",
              "…and a read against that failed reader is refused, naming the "
              "member, rather than dereferencing a null buffer. jnext's own "
              "call site checks open()'s return value; the stages after this "
              "one add many more that might not",
              !read && !rw.empty() && out.empty(),
              det("read=%d why='%s' n=%zu", read, rw.c_str(), out.size()));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSN — the manifest.json grammar (§6, §8)
    // ─────────────────────────────────────────────────────────────────────
    {
        Manifest m = make_manifest();
        m.sdcard.present        = true;
        m.sdcard.mounted_path   = "/home/user/.jnext/sdcard/x.img";
        m.sdcard.read_only      = false;
        m.sdcard.identity       = make_identity();
        m.sdcard.vollab         = "NEXT       ";
        m.sdcard.content_sha256 = "00112233";
        m.sdcard.content_mtime_utc = "2026-09-23T09:58:41Z";
        m.members["mem/ram.bin"] = BlobDecl{2097152, 0x8f3a21bdu};
        m.subsystems             = {"cpu", "mmu"};
        m.producer.git_describe  = "v1.0.18-0-gabcdef12";

        const std::string text = jnext::jns::manifest_to_json(m);
        Manifest back;
        std::vector<std::string> unknown;
        const bool ok = jnext::jns::manifest_from_json(text, back, unknown, why);

        const bool same =
            ok && back.format_version == m.format_version &&
            back.created == m.created &&
            back.producer.jnext_version == m.producer.jnext_version &&
            back.producer.git_describe == m.producer.git_describe &&
            back.producer.platform == m.producer.platform &&
            back.model.state_model_revision == m.model.state_model_revision &&
            back.model.machine == m.model.machine &&
            back.model.ram_kb == m.model.ram_kb &&
            back.model.timing == m.model.timing &&
            back.model.cpu_speed_nr07 == m.model.cpu_speed_nr07 &&
            back.capture.frame == m.capture.frame &&
            back.capture.frame_boundary == m.capture.frame_boundary &&
            back.sdcard.present == m.sdcard.present &&
            back.sdcard.mounted_path == m.sdcard.mounted_path &&
            back.sdcard.read_only == m.sdcard.read_only &&
            back.sdcard.identity == m.sdcard.identity &&
            back.sdcard.vollab == m.sdcard.vollab &&
            back.sdcard.content_sha256 == m.sdcard.content_sha256 &&
            back.sdcard.content_mtime_utc == m.sdcard.content_mtime_utc &&
            back.members == m.members && back.subsystems == m.subsystems;
        check("JNSN-01",
              "a fully populated manifest round-trips through JSON with every "
              "field intact, including the SD identity and the blob "
              "declarations",
              same && unknown.empty(),
              ok ? det("unknown=%zu", unknown.size()) : why);
    }
    {
        const std::vector<uint8_t> z = build_good("jnsn02-good");
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, m, v);
        check("JNSN-02",
              "the writer stamps format_version 1 — the frozen grammar version "
              "of §7.1",
              ok && m.format_version == 1 &&
                  jnext::jns::kFormatVersion == 1u,
              det("ok=%d fv=%u %s", ok, m.format_version, v.refusal.c_str()));
    }
    {
        const std::vector<uint8_t> z = build_good("");
        jnext::zip::Reader r;
        const bool opened = r.open(z.data(), z.size(), why);
        check("JNSN-03",
              "the archive comment is exactly `jnext-snapshot format=1`, "
              "character for character",
              opened && r.comment() == std::string("jnext-snapshot format=1"),
              opened ? r.comment() : why);
    }
    {
        // `manifest.json` added LAST: the archive is a perfectly good ZIP and
        // the only thing wrong is the ordering rule of §6.
        Manifest m = make_manifest();
        m.subsystems = {"cpu"};
        std::vector<uint8_t> z = build_raw(
            m, {{"state/cpu.json", bytes_of(kJson)}}, why,
            jnext::jns::kArchiveComment, /*manifest_first=*/false);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSN-04",
              "`manifest.json` must be member 0; an archive that carries it "
              "elsewhere is refused, naming what was first (§6)",
              refused_naming("JNSN-04", v, "member 0 is 'state/cpu.json'"),
              v.refusal);
    }
    {
        std::vector<uint8_t> z = build_with_manifest_text("{not json at all",
                                                          why);
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, m, v);
        check("JNSN-05",
              "a manifest that is not valid JSON is refused, and the refusal "
              "carries the PARSER's own diagnosis rather than a generic "
              "message (§7.3: naming the defect)",
              refused_naming("JNSN-05", v, "not valid JSON") &&
                  v.refusal.size() > 40,
              v.refusal);
    }
    {
        std::vector<uint8_t> z = build_with_manifest_text("[1,2,3]", why);
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, m, v);
        check("JNSN-06",
              "a manifest that parses but is not a JSON object is refused",
              refused_naming("JNSN-06", v, "not a JSON object"), v.refusal);
    }
    {
        struct Missing {
            const char* row;
            const char* text;
            const char* names;
            const char* desc;
        };
        const Missing cases[] = {
            {"JNSN-07",
             R"({"format_version":1,"capture":{"frame_boundary":true}})",
             "no model object",
             "a manifest with no `model` object is refused, naming it"},
            {"JNSN-08",
             R"({"format_version":1,"model":{"state_model_revision":1,"ram_kb":2048},)"
             R"("capture":{"frame_boundary":true}})",
             "no machine key",
             "a manifest with no `model.machine` is refused: the key drives "
             "reconstruction, so there is no honest default for it"},
            {"JNSN-09",
             R"({"format_version":1,"model":{"state_model_revision":1,"machine":"next"},)"
             R"("capture":{"frame_boundary":true}})",
             "no ram_kb key",
             "a manifest with no `model.ram_kb` is refused rather than "
             "ASSUMING 2048 — §8 is explicit that a reader never assumes it"},
            {"JNSN-10",
             R"({"format_version":1,"model":{"machine":"next","ram_kb":2048},)"
             R"("capture":{"frame_boundary":true}})",
             "no state_model_revision key",
             "a manifest with no `model.state_model_revision` is refused; "
             "without it the §7.3 mismatch rule has nothing to compare"},
            {"JNSN-11",
             R"({"format_version":1,"model":{"state_model_revision":1,"machine":"next","ram_kb":2048}})",
             "no capture object",
             "a manifest with no `capture` object is refused"},
            {"JNSN-12",
             R"({"format_version":1,"model":{"state_model_revision":1,"machine":"next","ram_kb":2048},)"
             R"("capture":{"frame":1}})",
             "no frame_boundary key",
             "a manifest with no `capture.frame_boundary` is refused: it is a "
             "fact a reader CHECKS, so its absence cannot default to true"},
            {"JNSN-13",
             R"({"format_version":1,"model":{"state_model_revision":1,"machine":"next","ram_kb":2048},)"
             R"("capture":{"frame":1,"frame_boundary":false}})",
             "frame_boundary=false",
             "a manifest declaring `frame_boundary=false` is refused — it "
             "describes a state a `.jns` cannot be written from, so the file "
             "is a hand-edited lie (this is NOT §12.4's deliberately absent "
             "paused-save row; see the header)"},
        };
        for (const Missing& c : cases) {
            std::string w;
            std::vector<uint8_t> z = build_with_manifest_text(c.text, w);
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, m, v);
            check(c.row, c.desc, refused_naming(c.row, v, c.names), v.refusal);
        }
    }
    {
        SnapshotWriter w(false);
        Manifest m = make_manifest();
        m.capture.frame_boundary = false;
        w.set_manifest(m);
        w.add_subsystem("cpu", kJson, why);
        std::vector<uint8_t> out;
        const bool fin = w.finish(out, why);
        check("JNSN-14",
              "the WRITER refuses to emit a non-frame-boundary capture, so the "
              "reader's JNSN-13 refusal is about a file we could never produce "
              "— if the writer could emit it, the reader's check would be "
              "describing our own output",
              refused_naming("JNSN-14", fin, why, "frame boundary") &&
                  out.empty(),
              det("fin=%d %s", fin, why.c_str()));
    }
    {
        struct BadMember {
            const char* row;
            const char* decl;
            const char* names;
            const char* desc;
        };
        const BadMember cases[] = {
            {"JNSN-15", R"("members":{"mem/ram.bin":{"bytes":16,"crc32":"abc"}})",
             "not 8 hex digits",
             "a blob CRC-32 that is not exactly eight hex digits is refused; "
             "the schema pins that pattern, so a second spelling would make "
             "jnext and an external validator disagree about one file"},
            {"JNSN-16",
             R"("members":{"mem/ram.bin":{"bytes":16,"crc32":"ABCDEF01"}})",
             "not lower-case hex",
             "an UPPER-CASE blob CRC-32 is refused rather than normalised, for "
             "the same reason"},
            {"JNSN-26", R"("members":{"mem/ram.bin":{"bytes":16}})",
             "no crc32 key",
             "a blob declaration with no `crc32` is refused: the declared CRC "
             "is the other half of what an external reader checks (§5.3), and "
             "an absent one would silently disable that check for that blob"},
            {"JNSN-17", R"("members":{"mem/ram.bin":{"crc32":"00000000"}})",
             "no bytes key",
             "a blob declaration with no `bytes` is refused: the declared "
             "length is half of what an external reader checks (§5.3)"},
            {"JNSN-18",
             R"("members":{"mem/../escape":{"bytes":16,"crc32":"00000000"}})",
             "'..'",
             "a blob declaration whose KEY is not a valid member path is "
             "refused — the grammar applies to the manifest's own references, "
             "not only to the archive's entries"},
            {"JNSN-19", R"("subsystems":["cpu",7])",
             "non-string entry",
             "a `subsystems` array holding a non-string is refused"},
            {"JNSN-20", R"("subsystems":["cpu","cpu"])",
             "twice",
             "a `subsystems` array naming the same subsystem twice is refused; "
             "the list is how a reader tells 'deliberately not saved' from "
             "'missing', and a duplicate makes that ambiguous"},
            {"JNSN-21", R"("members":{"mem/ram.bin":{"bytes":"16","crc32":"00000000"}})",
             "bytes is not an integer",
             "a blob `bytes` that is a string rather than a number is refused, "
             "naming the key"},
        };
        for (const BadMember& c : cases) {
            const std::string text =
                std::string(R"({"format_version":1,)"
                            R"("model":{"state_model_revision":1,"machine":"next","ram_kb":2048},)"
                            R"("capture":{"frame":1,"frame_boundary":true},)") +
                c.decl + "}";
            std::string w;
            std::vector<uint8_t> z = build_with_manifest_text(text, w);
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, m, v);
            check(c.row, c.desc, refused_naming(c.row, v, c.names), v.refusal);
        }
    }
    {
        const std::string text =
            R"({"format_version":1,"future_key":42,)"
            R"("model":{"state_model_revision":1,"machine":"next","ram_kb":2048,"warp_core":true},)"
            R"("capture":{"frame":1,"frame_boundary":true}})";
        std::vector<uint8_t> z = build_with_manifest_text(text, why);
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, m, v);
        const bool saw_top = std::any_of(
            v.warnings.begin(), v.warnings.end(), [](const std::string& s) {
                return s.find("'future_key'") != std::string::npos;
            });
        const bool saw_nested = std::any_of(
            v.warnings.begin(), v.warnings.end(), [](const std::string& s) {
                return s.find("'model.warp_core'") != std::string::npos;
            });
        check("JNSN-22",
              "an unknown TOP-LEVEL manifest key is ignored and reported, not "
              "refused — that is what makes a new optional key addable without "
              "a format bump (§7.1, G7)",
              ok && saw_top, det("ok=%d warnings=%zu", ok, v.warnings.size()));
        check("JNSN-23",
              "an unknown key INSIDE a known object is reported with its "
              "dotted path, so an older jnext says exactly what it dropped "
              "(§12.2)",
              ok && saw_nested,
              det("ok=%d warnings=%zu", ok, v.warnings.size()));
    }
    {
        Manifest m = make_manifest();
        m.producer.git_describe.clear();
        const std::string a = jnext::jns::manifest_to_json(m);
        m.producer.git_describe = "v1.0.18-3-gdeadbee";
        const std::string b = jnext::jns::manifest_to_json(m);
        check("JNSN-24",
              "`producer.git_describe` is OMITTED when the build has none "
              "rather than emitted empty — an empty string would read as "
              "'describes as nothing', a different and wrong claim from 'not "
              "recorded'",
              a.find("git_describe") == std::string::npos &&
                  b.find("v1.0.18-3-gdeadbee") != std::string::npos,
              det("absent=%d present=%d",
                  a.find("git_describe") == std::string::npos,
                  b.find("git_describe") != std::string::npos));
    }
    {
        SnapshotWriter w(false);
        w.set_manifest(make_manifest());
        const std::vector<uint8_t> blob(777, 0x5E);
        w.add_blob("mem/x.bin", blob.data(), blob.size(), why);
        std::vector<uint8_t> z;
        w.finish(z, why);
        record_corpus("jnsn25-blob-decl", z);

        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, m, v);
        const jnext::zip::Entry* e = ok ? r.find("mem/x.bin") : nullptr;
        check("JNSN-25",
              "blob declarations are computed from the bytes actually added, "
              "so the torn-file case §12.4 refuses on read is UNCONSTRUCTIBLE "
              "by the writer — the manifest and the archive cannot disagree",
              ok && m.members.count("mem/x.bin") == 1 &&
                  m.members["mem/x.bin"].bytes == 777 && e != nullptr &&
                  m.members["mem/x.bin"].crc32 == e->crc32,
              det("ok=%d decls=%zu %s", ok, m.members.size(),
                  v.refusal.c_str()));
    }

    {
        SnapshotWriter w(false);
        w.set_manifest(make_manifest());
        const std::vector<uint8_t> blob(8, 0x11);
        const bool wrong = w.add_blob("state/notablob.bin", blob.data(),
                                      blob.size(), why);
        check("JNSN-27",
              "the writer refuses a blob outside `mem/`, naming both the path "
              "and the namespace — `mem/` is the closed namespace the reader "
              "enforces, so the writer must not be able to populate any other",
              refused_naming("JNSN-27", wrong, why, "'mem/'") &&
                  why.find("'state/notablob.bin'") != std::string::npos,
              why);
    }
    {
        // TOTALITY, not a refusal row. The manifest is meant to be inspected
        // and edited with `jq` and a text editor (G5), so hand-mangled files
        // WILL arrive — and nlohmann signals a type surprise by THROWING,
        // which for an unguarded accessor would ABORT jnext rather than refuse
        // the file. Every accessor in `manifest_from_json` is guarded; this
        // row is what proves the set of guards is complete, by running a
        // battery of structurally hostile but syntactically valid manifests
        // and asserting each one produced a verdict AT ALL.
        //
        // The mutation battery showed this is exactly what a missed guard
        // looks like: removing any one of them turns a refusal into a
        // `json::out_of_range` or `json::type_error` that terminates the
        // process. A row asserting "refused" cannot see that; this one can.
        static const char* kHostile[] = {
            R"({})",
            R"({"format_version":null})",
            R"({"format_version":true})",
            R"({"format_version":[1]})",
            R"({"format_version":{"v":1}})",
            R"({"format_version":1})",
            R"({"format_version":1,"model":[]})",
            R"({"format_version":1,"model":"next"})",
            R"({"format_version":1,"model":{"machine":[1,2]}})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":null}})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":[]})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":"yes"}})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("members":[]})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("members":{"mem/a.bin":42}})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("subsystems":"cpu"})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("subsystems":[null]})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("producer":7})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("media":{"sdcard":{"identity":"nope"}}})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("media":{"sdcard":{"read_only":"maybe"}}})",
            R"({"format_version":1,"model":{"machine":"next","ram_kb":2048,)"
            R"("state_model_revision":1},"capture":{"frame_boundary":true},)"
            R"("media":{"sdcard":{"content_stamp":{"sha256":99}}}})",
            R"([])",
            R"("a string")",
            R"(42)",
            R"(null)",
        };
        size_t verdicts = 0;
        std::string first_bad;
        for (const char* text : kHostile) {
            std::string w;
            std::vector<uint8_t> z = build_with_manifest_text(text, w);
            if (z.empty()) {
                if (first_bad.empty()) first_bad = std::string("build: ") + text;
                continue;
            }
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                      make_env(), r, m, v);
            // Either verdict is acceptable; reaching one AT ALL is the claim.
            // A refusal must still say why.
            if (ok || !v.refusal.empty()) {
                ++verdicts;
            } else if (first_bad.empty()) {
                first_bad = std::string("silent: ") + text;
            }
        }
        const size_t n = sizeof(kHostile) / sizeof(kHostile[0]);
        check("JNSN-28",
              "every structurally hostile but syntactically valid manifest "
              "produces a VERDICT — an open or a refusal that says why — and "
              "none aborts the process. A file format users are invited to "
              "hand-edit must be total over its inputs, and an unguarded "
              "accessor would terminate jnext instead of refusing the file",
              verdicts == n,
              det("%zu of %zu produced a verdict; first bad: %s", verdicts, n,
                  first_bad.c_str()));
    }
    {
        Manifest m = make_manifest();
        std::vector<uint8_t> z =
            build_raw(m, {}, why, "some other archive comment");
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-18",
              "an archive whose comment is not exactly `jnext-snapshot "
              "format=1` is refused as not a jnext snapshot, naming both "
              "comments — the comment is the file's grammar declaration and "
              "`unzip -z` is expected to read it without a JSON parse (§6)",
              refused_naming("JNSR-18", v, "not a jnext snapshot") &&
                  v.refusal.find("some other archive comment") !=
                      std::string::npos,
              v.refusal);
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSV — the two version fields (§7)
    // ─────────────────────────────────────────────────────────────────────
    {
        const std::string text =
            R"({"format_version":2,"producer":{"jnext_version":"1.4.0"},)"
            R"("model":{"state_model_revision":1,"machine":"next","ram_kb":2048},)"
            R"("capture":{"frame":1,"frame_boundary":true}})";
        std::vector<uint8_t> z = build_with_manifest_text(text, why);
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, m, v);
        check("JNSV-01",
              "a format_version NEWER than this build refuses, naming BOTH "
              "numbers — never a best-effort read of a grammar we do not know "
              "(§7.3)",
              refused_naming("JNSV-01", v, "format 2") &&
                  v.refusal.find("up to format 1") != std::string::npos,
              v.refusal);
        check("JNSV-02",
              "…and names the jnext version that wrote it, which is what turns "
              "the refusal into an actionable instruction",
              !v.ok && v.refusal.find("jnext 1.4.0") != std::string::npos,
              v.refusal);
    }
    {
        // A build that reads only format 2 meeting a format-1 file, with the
        // format-1 reader still compiled in. §7.1 freezes the number at 1, so
        // this branch has no live instance yet — and an unreachable branch is
        // one that ships untested and goes wrong the first time it is needed.
        ReaderEnv e = make_env();
        e.versions.max_readable = 2;
        e.versions.readable     = {1, 2};

        const std::vector<uint8_t> z = build_good("");
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r, m, v);
        check("JNSV-03",
              "an OLDER format with its reader still compiled in is read under "
              "that grammar's rules (§7.3)",
              ok && m.format_version == 1, v.refusal);
    }
    {
        ReaderEnv e = make_env();
        e.versions.max_readable = 2;
        e.versions.readable     = {2};
        e.versions.dropped      = {{1, "1.9.0"}};

        const std::vector<uint8_t> z = build_good("");
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), e, r, m, v);
        check("JNSV-04",
              "an older format whose reader was REMOVED refuses, naming the "
              "version and the release that dropped it — §7.3's policy is that "
              "the removal is a ChangeLog line, so the message has to be able "
              "to point at it",
              refused_naming("JNSV-04", v, "format 1") &&
                  v.refusal.find("1.9.0") != std::string::npos,
              v.refusal);
    }
    {
        ReaderEnv e = make_env();
        e.versions.max_readable = 2;
        e.versions.readable     = {2};   // no drop record for 1 either

        const std::vector<uint8_t> z = build_good("");
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), e, r, m, v);
        check("JNSV-05",
              "an older format with neither a reader nor a drop record still "
              "refuses, naming the version, rather than falling through to a "
              "best-effort read",
              refused_naming("JNSV-05", v, "no reader in this build"),
              v.refusal);
    }
    {
        struct BadVersion {
            const char* row;
            const char* value;
            const char* names;
            const char* desc;
        };
        const BadVersion cases[] = {
            {"JNSV-06", nullptr, "no format_version key",
             "a manifest with no format_version is refused, naming the key"},
            // The float is spelled as two adjacent literals deliberately. A
            // bare "1.5" in a test source is an ID-SHAPED token to the
            // traceability extractor (numeric-dotted plan rows — DMA's plan
            // has a row 1.5), and a row ID is a GLOBAL name, so the collision
            // is a refusal. The fix for a collision is renaming, never a
            // baseline entry.
            {"JNSV-07", "1" ".5", "not an integer",
             "a FLOAT format_version is refused — JSON has one number type, so "
             "a fractional version parses happily and must be rejected by the "
             "reader"},
            {"JNSV-08", "\"1\"", "not an integer",
             "a STRING format_version is refused"},
            {"JNSV-09", "-1", "out of range",
             "a NEGATIVE format_version is refused"},
        };
        for (const BadVersion& c : cases) {
            std::string text = "{";
            if (c.value != nullptr)
                text += std::string("\"format_version\":") + c.value + ",";
            text += R"("model":{"state_model_revision":1,"machine":"next","ram_kb":2048},)"
                    R"("capture":{"frame":1,"frame_boundary":true}})";
            std::string w;
            std::vector<uint8_t> z = build_with_manifest_text(text, w);
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, m, v);
            check(c.row, c.desc, refused_naming(c.row, v, c.names), v.refusal);
        }
    }
    {
        SnapshotWriter w(false);
        Manifest m = make_manifest();
        m.model.state_model_revision = 7;
        w.set_manifest(m);
        w.add_subsystem("cpu", kJson, why);
        std::vector<uint8_t> z;
        w.finish(z, why);
        record_corpus("jnsv10-model-rev", z);

        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, got, v);
        const bool named = std::any_of(
            v.warnings.begin(), v.warnings.end(), [](const std::string& s) {
                return s.find("revision 7") != std::string::npos &&
                       s.find("revision 1") != std::string::npos;
            });
        check("JNSV-10",
              "a state_model_revision mismatch RESTORES, loudly — it is a "
              "provenance stamp, not a compatibility contract (§7.2)",
              ok && !v.warnings.empty(),
              det("ok=%d warnings=%zu %s", ok, v.warnings.size(),
                  v.refusal.c_str()));
        check("JNSV-11",
              "…and the warning names BOTH revisions and the jnext version "
              "that wrote the file",
              named && std::any_of(v.warnings.begin(), v.warnings.end(),
                                   [](const std::string& s) {
                                       return s.find("1.0.18") !=
                                              std::string::npos;
                                   }),
              v.warnings.empty() ? "no warnings" : v.warnings[0]);

        ReaderEnv strict = make_env();
        strict.strict = true;
        jnext::zip::Reader r2;
        Verdict v2;
        jnext::jns::open_snapshot(z.data(), z.size(), strict, r2, got, v2);
        check("JNSV-12",
              "…and --snapshot-strict turns that same mismatch into a refusal "
              "(§7.3)",
              refused_naming("JNSV-12", v2, "snapshot-strict"), v2.refusal);
    }
    {
        const std::vector<uint8_t> z = build_good("");
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, m, v);
        check("JNSV-13",
              "a MATCHING state_model_revision produces no warning at all — a "
              "reader that warned every time would train the user to ignore "
              "the one that matters",
              ok && v.warnings.empty(),
              det("ok=%d warnings=%zu", ok, v.warnings.size()));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSR — the reader-rule table of §12.4, one row per line
    // ─────────────────────────────────────────────────────────────────────
    {
        Manifest m = make_manifest();
        m.subsystems = {"aa", "bb"};
        std::vector<uint8_t> z = build_raw(m,
                                           {{"state/aa.json", bytes_of(kJson)},
                                            {"state/bb.json", bytes_of(kJson)}},
                                           why);
        const Layout L = layout_of(z);
        std::memcpy(z.data() + L.lfh[2] + 30, "state/aa.json", 13);
        std::memcpy(z.data() + L.cdh[2] + 46, "state/aa.json", 13);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-01",
              "§12.4 row 1 — duplicate member names refuse the whole snapshot, "
              "reached through open_snapshot and not only through the ZIP "
              "layer",
              refused_naming("JNSR-01", v, "more than once"), v.refusal);
    }
    {
        // Declare a CRC that is wrong while the bytes are right: the manifest
        // and the archive were not written together, which is a torn file
        // however it happened.
        Manifest m = make_manifest();
        const std::vector<uint8_t> blob(64, 0x7E);
        m.members["mem/x.bin"] = BlobDecl{64, 0x00000001u};
        std::vector<uint8_t> z = build_raw(m, {{"mem/x.bin", blob}}, why);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-02",
              "§12.4 row 2 — a manifest CRC-32 disagreeing with the ZIP's own "
              "refuses, naming BOTH; the ZIP CRC is authoritative for "
              "integrity, so a disagreement means a torn file",
              refused_naming("JNSR-02", v, "00000001") &&
                  v.refusal.find("ZIP CRC-32") != std::string::npos,
              v.refusal);
    }
    {
        Manifest m = make_manifest();
        m.members["mem/ghost.bin"] = BlobDecl{16, 0u};
        std::vector<uint8_t> z = build_raw(m, {}, why);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-03",
              "§12.4 row 3 — a blob DECLARED in `members` with no member in the "
              "archive refuses, naming the path",
              refused_naming("JNSR-03", v, "'mem/ghost.bin'"), v.refusal);
    }
    {
        Manifest m = make_manifest();   // members{} deliberately empty
        const std::vector<uint8_t> blob(32, 0x99);
        std::vector<uint8_t> z = build_raw(m, {{"mem/stray.bin", blob}}, why);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-04",
              "§12.4 row 4 — a blob PRESENT but undeclared refuses. Stricter "
              "than the ignore-unknown rule on purpose: an undeclared blob has "
              "no length or CRC to check, so it is outside §5.3's chain and "
              "`mem/` is a closed namespace",
              refused_naming("JNSR-04", v, "closed namespace"), v.refusal);
    }
    {
        Manifest m = make_manifest();
        m.subsystems = {"cpu", "ula"};
        std::vector<uint8_t> z =
            build_raw(m, {{"state/cpu.json", bytes_of(kJson)}}, why);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-05",
              "§12.4 row 5 — a subsystem LISTED in the manifest whose member is "
              "absent refuses, naming both the subsystem and the path. The "
              "list exists exactly to separate 'deliberately not saved' from "
              "'missing or corrupt'",
              refused_naming("JNSR-05", v, "'ula'") &&
                  v.refusal.find("'state/ula.json'") != std::string::npos,
              v.refusal);
    }
    {
        Manifest m = make_manifest();
        m.subsystems = {"cpu"};
        std::vector<uint8_t> z = build_raw(m,
                                           {{"state/cpu.json", bytes_of(kJson)},
                                            {"state/newthing.json",
                                             bytes_of(kJson)}},
                                           why);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, got, v);
        check("JNSR-06",
              "§12.4 row 6 — a `state/` member absent from `subsystems` is "
              "IGNORED and reported, not refused; that is what lets an older "
              "jnext open a newer file and say what it dropped (§12.1)",
              ok && v.ignored_members.size() == 1 &&
                  v.ignored_members[0] == "state/newthing.json",
              det("ok=%d ignored=%zu %s", ok, v.ignored_members.size(),
                  v.refusal.c_str()));
    }
    {
        Manifest m = make_manifest();
        const std::vector<uint8_t> blob(64, 0x22);
        m.members["mem/x.bin"] =
            BlobDecl{65, 0u};   // wrong length, CRC fixed up below
        std::vector<uint8_t> z = build_raw(m, {{"mem/x.bin", blob}}, why);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-07",
              "§12.4 row 7 — an inflated length differing from the declared "
              "`bytes` refuses, naming the member and both lengths (the "
              "warm-start cache's exact-length rule)",
              refused_naming("JNSR-07", v, "'mem/x.bin'") &&
                  v.refusal.find("65") != std::string::npos &&
                  v.refusal.find("64") != std::string::npos,
              v.refusal);
    }
    {
        SnapshotWriter w(false);
        Manifest m = make_manifest();
        m.sdcard.present        = true;
        m.sdcard.mounted_path   = "/home/user/.jnext/sdcard/card.img";
        m.sdcard.identity       = make_identity();
        m.sdcard.vollab         = "NEXT       ";
        m.sdcard.content_sha256 = "deadbeef";
        w.set_manifest(m);
        w.add_subsystem("cpu", kJson, why);
        std::vector<uint8_t> z;
        w.finish(z, why);
        record_corpus("jnsr08-sdcard", z);

        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        ReaderEnv e = make_env();   // no card mounted
        jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
        check("JNSR-08",
              "§12.4 row 8 — a snapshot taken with a card, restored with none "
              "mounted, refuses, naming the path and the volume label",
              refused_naming("JNSR-08", v, "card.img") &&
                  v.refusal.find("NEXT") != std::string::npos,
              v.refusal);
    }
    {
        const std::vector<uint8_t> z = build_good("");   // no card in the file
        ReaderEnv e = make_env();
        e.card.present      = true;
        e.card.mounted_path = "/home/user/.jnext/sdcard/new.img";
        e.card.identity     = make_identity();
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r, m, v);
        const bool named = std::any_of(
            v.warnings.begin(), v.warnings.end(), [](const std::string& s) {
                return s.find("new.img") != std::string::npos;
            });
        check("JNSR-09",
              "§12.4 row 9 — a snapshot taken with NO card, restored with one "
              "mounted, restores with a warning: the machine did not depend on "
              "it, but the run may diverge",
              ok && named,
              det("ok=%d warnings=%zu", ok, v.warnings.size()));
    }
    {
        SnapshotWriter w(false);
        Manifest m = make_manifest();
        m.sdcard.present        = true;
        m.sdcard.mounted_path   = "/card.img";
        m.sdcard.read_only      = true;
        m.sdcard.identity       = make_identity();
        m.sdcard.content_sha256 = "cafe";
        w.set_manifest(m);
        w.add_subsystem("cpu", kJson, why);
        std::vector<uint8_t> z;
        w.finish(z, why);

        ReaderEnv e = make_env();
        e.card.present        = true;
        e.card.mounted_path   = "/card.img";
        e.card.read_only      = false;          // the only difference
        e.card.identity       = make_identity();
        e.card.content_sha256 = "cafe";

        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
        const bool named = std::any_of(
            v.warnings.begin(), v.warnings.end(), [](const std::string& s) {
                return s.find("read-only") != std::string::npos &&
                       s.find("writable") != std::string::npos;
            });
        check("JNSR-10",
              "§12.4 row 10 — a `read_only` difference WARNS and restores, "
              "naming both states: read-only -> writable is legal and common, "
              "and the reverse means writes the snapshot's program made were "
              "never persisted",
              ok && named,
              det("ok=%d warnings=%zu", ok, v.warnings.size()));
    }
    {
        SnapshotWriter w(false);
        Manifest m = make_manifest();
        m.model.ram_kb = 768;   // the hardware's other size; this build has one
        w.set_manifest(m);
        w.add_subsystem("cpu", kJson, why);
        std::vector<uint8_t> z;
        w.finish(z, why);

        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-11",
              "§12.4 row 11 — a `ram_kb` this build cannot construct refuses, "
              "naming the size; §8 is explicit that a reader never assumes "
              "2048",
              refused_naming("JNSR-11", v, "768 KB"), v.refusal);
    }
    {
        SnapshotWriter w(false);
        Manifest m = make_manifest();
        m.model.machine = "zx81";
        w.set_manifest(m);
        w.add_subsystem("cpu", kJson, why);
        std::vector<uint8_t> z;
        w.finish(z, why);

        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        jnext::jns::open_snapshot(z.data(), z.size(), make_env(), r, got, v);
        check("JNSR-12",
              "a machine this build cannot construct refuses, naming it — "
              "distinct from a machine MISMATCH, which reconfigures",
              refused_naming("JNSR-12", v, "'zx81'"), v.refusal);
    }
    {
        SnapshotWriter w(false);
        Manifest m = make_manifest();
        m.model.machine = "128k";
        w.set_manifest(m);
        w.add_subsystem("cpu", kJson, why);
        std::vector<uint8_t> z;
        w.finish(z, why);
        record_corpus("jnsr13-128k", z);

        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, got, v);
        check("JNSR-13",
              "§12.4/§7.3 — a machine MISMATCH reconfigures and restores "
              "rather than refusing: the user must not have to get `--machine` "
              "right to reload their own save",
              ok && v.reconfigure_to == "128k" && !v.warnings.empty(),
              det("ok=%d to='%s' %s", ok, v.reconfigure_to.c_str(),
                  v.refusal.c_str()));
    }
    {
        Manifest m = make_manifest();
        m.subsystems = {"cpu"};
        std::vector<uint8_t> z = build_raw(
            m,
            {{"state/cpu.json", bytes_of(kJson)},
             {"future/thing.dat", bytes_of("x")}},
            why);
        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, got, v);
        check("JNSR-14",
              "§12.1 — a well-formed member in a WHOLE UNKNOWN namespace is "
              "ignored and reported, which is what keeps subsystems addable "
              "later (settled point 7)",
              ok && v.ignored_members.size() == 1 &&
                  v.ignored_members[0] == "future/thing.dat",
              det("ok=%d ignored=%zu %s", ok, v.ignored_members.size(),
                  v.refusal.c_str()));
    }
    {
        SnapshotWriter w(false);
        w.set_manifest(make_manifest());
        w.add_subsystem("cpu", kJson, why);
        const std::string note = "This snapshot contains firmware. Do not "
                                 "share it.\n";
        w.add_meta("meta/readme.txt",
                   reinterpret_cast<const uint8_t*>(note.data()), note.size(),
                   why);
        std::vector<uint8_t> z;
        w.finish(z, why);
        record_corpus("jnsr15-meta", z);

        jnext::zip::Reader r;
        Manifest got;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, got, v);
        std::string back;
        const bool read = ok && r.read_text("meta/readme.txt", back, why);
        check("JNSR-15",
              "a `meta/` member is accepted, readable and NOT reported as "
              "ignored — that namespace is the one carrying the 'do not share "
              "this' notice INSIDE the file, where a user who has forgotten "
              "will meet it (§5.1)",
              ok && v.ignored_members.empty() && read && back == note,
              det("ok=%d ignored=%zu read=%d", ok, v.ignored_members.size(),
                  read));
    }
    {
        const std::vector<uint8_t> z = build_good("jnsr16-clean");
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, m, v);
        check("JNSR-16",
              "a fully valid snapshot opens SILENTLY: no refusal, no warning, "
              "no ignored member, no reconfiguration. Without this row every "
              "refusal row above could pass while the reader refused "
              "everything",
              ok && v.refusal.empty() && v.warnings.empty() &&
                  v.ignored_members.empty() && v.reconfigure_to.empty(),
              det("ok=%d warn=%zu ign=%zu '%s'", ok, v.warnings.size(),
                  v.ignored_members.size(), v.refusal.c_str()));

        std::vector<uint8_t> blob;
        const bool read = ok && r.read("mem/bank5-vram.bin", blob, why);
        check("JNSR-17",
              "…and its blobs read back byte-exact through the archive the "
              "verdict left open",
              read && blob.size() == 4096 &&
                  std::all_of(blob.begin(), blob.end(),
                              [](uint8_t b) { return b == 0xA5; }),
              det("read=%d n=%zu", read, blob.size()));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSI — the two-tier SD identity (§11.3)
    // ─────────────────────────────────────────────────────────────────────
    //
    // The whole point of the tiering: jnext opens the image READ-WRITE and
    // persists guest writes, so a whole-image digest changes the first time
    // NextZXOS touches a directory entry. A refusal on that would fire against
    // the very card the snapshot was taken on. An identity that cries wolf
    // gets ignored, and an ignored identity is worse than none (§11.1).
    {
        auto build_with_card = [&](const SdIdentity& id, const char* vollab,
                                   const char* content) {
            SnapshotWriter w(false);
            Manifest m = make_manifest();
            m.sdcard.present        = true;
            m.sdcard.mounted_path   = "/card.img";
            m.sdcard.identity       = id;
            m.sdcard.vollab         = vollab;
            m.sdcard.content_sha256 = content;
            w.set_manifest(m);
            std::string w2;
            w.add_subsystem("cpu", kJson, w2);
            std::vector<uint8_t> z;
            w.finish(z, w2);
            return z;
        };
        auto env_with_card = [&](const SdIdentity& id, const char* vollab,
                                 const char* content) {
            ReaderEnv e = make_env();
            e.card.present        = true;
            e.card.mounted_path   = "/card.img";
            e.card.identity       = id;
            e.card.vollab         = vollab;
            e.card.content_sha256 = content;
            return e;
        };

        const SdIdentity base = make_identity();
        const std::vector<uint8_t> z =
            build_with_card(base, "NEXT       ", "aabbccdd");
        record_corpus("jnsi-card", z);

        {
            ReaderEnv e = env_with_card(base, "NEXT       ", "aabbccdd");
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            const bool ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r,
                                                      m, v);
            check("JNSI-01",
                  "both tiers matching is SILENT — no warning at all (§11.3)",
                  ok && v.warnings.empty(), det("ok=%d w=%zu", ok,
                                                v.warnings.size()));
        }

        struct Tier1 {
            const char* row;
            const char* field;
            const char* desc;
        };
        const Tier1 t1[] = {
            {"JNSI-02", "volid",
             "a differing FAT32 BS_VolID refuses — the volume serial is the "
             "genuinely stable identifier, and it is what the refusal rests on"},
            {"JNSI-03", "size",
             "a differing image SIZE refuses"},
            {"JNSI-04", "mbr",
             "a differing MBR digest refuses"},
            {"JNSI-05", "lba",
             "a differing partition LBA refuses"},
        };
        for (const Tier1& c : t1) {
            SdIdentity other = base;
            if (std::strcmp(c.field, "volid") == 0) other.fat32_volume_id = "99887766";
            if (std::strcmp(c.field, "size") == 0)  other.image_bytes = 2147483648ull;
            if (std::strcmp(c.field, "mbr") == 0)   other.mbr_sha256 = "ff00";
            if (std::strcmp(c.field, "lba") == 0)   other.partition_lba = 63;

            ReaderEnv e = env_with_card(other, "NEXT       ", "aabbccdd");
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, m, v);
            check(c.row, c.desc, refused_naming(c.row, v, "not the one"),
                  v.refusal);
        }
        {
            SdIdentity other = base;
            other.fat32_volume_id = "99887766";
            ReaderEnv e = env_with_card(other, "NEXT       ", "aabbccdd");
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, m, v);
            check("JNSI-06",
                  "…and the Tier-1 refusal names BOTH identities, so a user can "
                  "see which card is mounted without opening the file",
                  !v.ok && v.refusal.find("1a2b3c4d") != std::string::npos &&
                      v.refusal.find("99887766") != std::string::npos,
                  v.refusal);

            e.force_sdcard = true;
            jnext::zip::Reader r2;
            Verdict v2;
            const bool ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r2,
                                                      m, v2);
            const bool named = std::any_of(
                v2.warnings.begin(), v2.warnings.end(),
                [](const std::string& s) {
                    return s.find("99887766") != std::string::npos &&
                           s.find("1a2b3c4d") != std::string::npos;
                });
            check("JNSI-07",
                  "--snapshot-force-sdcard downgrades the Tier-1 refusal to a "
                  "warning that still names both identities (§11.3)",
                  ok && named,
                  det("ok=%d warnings=%zu", ok, v2.warnings.size()));
        }
        {
            ReaderEnv e = env_with_card(base, "NEXT       ", "11223344");
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            const bool ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r,
                                                      m, v);
            check("JNSI-08",
                  "Tier-2 drift alone RESTORES with one warning — the card "
                  "drifting is legitimate and common, because jnext persists "
                  "guest writes to it (§11.1)",
                  ok && v.warnings.size() == 1,
                  det("ok=%d warnings=%zu", ok, v.warnings.size()));
            check("JNSI-09",
                  "…and that warning names both digests",
                  ok && !v.warnings.empty() &&
                      v.warnings[0].find("aabbccdd") != std::string::npos &&
                      v.warnings[0].find("11223344") != std::string::npos,
                  v.warnings.empty() ? "no warning" : v.warnings[0]);

            e.sd_transfer_in_flight = true;
            jnext::zip::Reader r2;
            Verdict v2;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r2, m, v2);
            check("JNSI-10",
                  "…but the SAME drift with the SD FSM MID-TRANSFER refuses: a "
                  "half-finished sector read against changed bytes is exactly "
                  "the 'streams garbage' failure, and this is where strictness "
                  "is earned (§11.3)",
                  refused_naming("JNSI-10", v2, "mid-transfer"), v2.refusal);
        }
        {
            // The row this whole tier exists for. `BS_VolLab` is a STALE COPY:
            // the authoritative FAT32 label is the root-directory entry with
            // ATTR_VOLUME_ID, which jnext's own FAT walk skips
            // (sd_rom_extractor.cpp:292). A tool that rewrites `BS_VolLab`
            // would otherwise produce a FALSE REFUSAL on the same physical
            // card.
            ReaderEnv e = env_with_card(base, "RENAMED    ", "aabbccdd");
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            const bool ok = jnext::jns::open_snapshot(z.data(), z.size(), e, r,
                                                      m, v);
            check("JNSI-11",
                  "a differing BS_VolLab ALONE changes NOTHING — no refusal "
                  "AND no warning. It is never compared, because it is a stale "
                  "copy several tools rewrite, and a false refusal on the same "
                  "physical card is the cries-wolf failure §11.1 exists to "
                  "avoid",
                  ok && v.warnings.empty() && v.refusal.empty(),
                  det("ok=%d warnings=%zu '%s'", ok, v.warnings.size(),
                      v.refusal.c_str()));
            check("JNSI-12",
                  "…and it is still CARRIED in the file, as informational "
                  "provenance a human can read",
                  ok && m.sdcard.vollab == "NEXT       ",
                  det("vollab='%s'", m.sdcard.vollab.c_str()));
        }
        {
            // Two snapshots written before an identity could be computed must
            // not compare equal-and-fine: they are two absences, not one card.
            SdIdentity none;
            const std::vector<uint8_t> zn =
                build_with_card(none, "", "aabbccdd");
            ReaderEnv e = env_with_card(none, "", "aabbccdd");
            jnext::zip::Reader r;
            Manifest m;
            Verdict v;
            jnext::jns::open_snapshot(zn.data(), zn.size(), e, r, m, v);
            check("JNSI-13",
                  "two UNPOPULATED identities are not treated as 'the same "
                  "card'. Comparing them as equal would silently disable the "
                  "whole Tier-1 check for any file written before the identity "
                  "could be computed",
                  refused_naming("JNSI-13", v, "not the one"), v.refusal);
        }
        {
            // ── AN UNKNOWN TIER-2 STAMP IS NOT A CHANGED ONE (GH #27 S7) ──
            //
            // The defect S7 found by wiring the producer: the stamps were
            // compared with a plain `==`, so an ABSENT digest on either side
            // read as a change, and the warning then quoted an empty string as
            // the new digest. That message is not true and not actionable, and
            // on the mid-transfer path the same `==` produced a REFUSAL whose
            // stated reason ("changed from X to '' ") was a fabrication.
            //
            // Absence is real: a snapshot written before the stamp existed, or
            // a live card whose digest failed part-way through a gigabyte of
            // I/O (`describe_sdcard_for_snapshot` keeps Tier 1 and drops Tier 2
            // in exactly that case). Tier 1 already distinguished the two
            // (`identity_known`); this is the same rule one tier down.
            struct Unknown {
                const char* row;
                const char* snap;     // digest IN the snapshot
                const char* live;     // digest of the card mounted now
                const char* names;    // what the message must say
                const char* desc;
            };
            const Unknown unknowns[] = {
                {"JNSI-14", "", "aabbccdd", "the snapshot has none",
                 "a snapshot with NO Tier-2 stamp warns that the contents "
                 "COULD NOT BE COMPARED — it does not claim they changed, and "
                 "it does not quote an empty string as a digest"},
                {"JNSI-15", "aabbccdd", "", "the mounted card has none",
                 "…and so does a mounted card whose digest could not be "
                 "computed: a failed hash of the live image is not evidence "
                 "that the image changed"},
                {"JNSI-16", "", "", "neither the snapshot nor the mounted card",
                 "…and when NEITHER side has one, the message says so rather "
                 "than reporting '' -> '' as a match"},
            };
            for (const Unknown& u : unknowns) {
                const std::vector<uint8_t> zu =
                    build_with_card(base, "NEXT       ", u.snap);
                ReaderEnv e = env_with_card(base, "NEXT       ", u.live);
                jnext::zip::Reader r;
                Manifest m;
                Verdict v;
                const bool ok = jnext::jns::open_snapshot(zu.data(), zu.size(),
                                                          e, r, m, v);
                const bool one_warning = ok && v.warnings.size() == 1;
                const bool says_uncompared =
                    one_warning &&
                    v.warnings[0].find("could not be compared") !=
                        std::string::npos &&
                    v.warnings[0].find(u.names) != std::string::npos;
                const bool no_false_change =
                    one_warning &&
                    v.warnings[0].find("changed since") == std::string::npos;
                check(u.row, u.desc,
                      says_uncompared && no_false_change,
                      det("ok=%d n=%zu '%s'", ok, v.warnings.size(),
                          v.warnings.empty() ? "" : v.warnings[0].c_str()));
            }
            {
                // §11.3's last row REQUIRES the stamp to match when the FSM was
                // mid-transfer, and an unknown stamp is not a match — so this
                // still refuses. What changed is the REASON: it says the
                // contents could not be VERIFIED, not that they changed to an
                // empty digest.
                const std::vector<uint8_t> zu =
                    build_with_card(base, "NEXT       ", "");
                ReaderEnv e = env_with_card(base, "NEXT       ", "aabbccdd");
                e.sd_transfer_in_flight = true;
                jnext::zip::Reader r;
                Manifest m;
                Verdict v;
                jnext::jns::open_snapshot(zu.data(), zu.size(), e, r, m, v);
                check("JNSI-17",
                      "an UNKNOWN Tier-2 stamp with the SD FSM mid-transfer "
                      "still REFUSES — §11.3 requires a match there — but the "
                      "refusal says the contents could not be VERIFIED rather "
                      "than inventing a change to an empty digest",
                      refused_naming("JNSI-17", v, "could not be verified") &&
                          v.refusal.find("mid-transfer") != std::string::npos &&
                          v.refusal.find("changed since") == std::string::npos,
                      v.refusal);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSM — refusal-message properties (G9 is testable, not a slogan)
    // ─────────────────────────────────────────────────────────────────────
    {
        // Every refusal above was recorded. These rows assert properties
        // ACROSS all of them, which is what catches a reader whose messages
        // have collapsed to one generic string: such a reader passes every
        // individual row above and fails both of these.
        // The property, stated so it is checkable rather than felt: a refusal
        // must name the offending artefact from the format's OWN vocabulary.
        // Most name a quoted member path or a number; the rest are refusals
        // about the archive or the manifest AS A WHOLE, where there is no
        // sub-artefact to quote and the structure itself is what is named
        // ("the archive has no members", "manifest.model has no machine key").
        //
        // The vocabulary is spelled out here, in the test a reviewer reads,
        // rather than hidden as a checker exclusion — the same convention
        // §12.2's exemption table follows. A reader whose messages collapsed
        // to "invalid snapshot" contains none of these and fails.
        static const char* kVocabulary[] = {
            "'",                      // a quoted member, path or key
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            // manifest keys
            "format_version", "model", "machine", "ram_kb", "capture",
            "frame_boundary", "frame boundary", "subsystems", "members",
            "crc32", "bytes", "sdcard", "state_model_revision", "manifest",
            // ZIP structures
            "central directory", "end-of-central-directory", "local header",
            "member", "ZIP64", "path", "archive", "CRC-32", "deflate",
            // the format's own nouns
            "snapshot", "SD card", "subsystem", "blob",
        };
        size_t vague = 0;
        std::string worst;
        for (const Refusal& rf : g_refusals) {
            bool named = false;
            for (const char* t : kVocabulary) {
                if (rf.message.find(t) != std::string::npos) {
                    named = true;
                    break;
                }
            }
            if (!named) {
                ++vague;
                if (worst.empty()) worst = rf.row + ": " + rf.message;
            }
        }
        check("JNSM-01",
              "every refusal this suite provoked names the offending artefact "
              "from the format's own vocabulary — a quoted member, key or "
              "path, a number, or the manifest/ZIP structure at fault. A "
              "message reading only 'invalid snapshot' is a FAILING row "
              "(§16.1: G9 is a testable property, not a slogan)",
              vague == 0 && g_refusals.size() > 60,
              det("%zu refusals, %zu vague; worst=%s", g_refusals.size(), vague,
                  worst.c_str()));

        std::set<std::string> distinct;
        for (const Refusal& rf : g_refusals) distinct.insert(rf.message);
        // Some conditions legitimately share wording across rows that differ
        // only in the byte they patched (the four Tier-1 identity fields all
        // say "not the one the snapshot was taken on", with different
        // details). What must not happen is a large collapse.
        check("JNSM-02",
              "distinct refusal CONDITIONS produce distinct messages; a reader "
              "whose messages collapsed to a handful would pass every "
              "individual row above and fail here",
              distinct.size() * 10 >= g_refusals.size() * 8,
              det("%zu distinct of %zu refusals", distinct.size(),
                  g_refusals.size()));

        check("JNSM-03",
              "no refusal was recorded with an empty message",
              std::none_of(g_refusals.begin(), g_refusals.end(),
                           [](const Refusal& rf) { return rf.message.empty(); }),
              det("%zu refusals", g_refusals.size()));
    }
    {
        // A refused verdict must not also look partly successful: `ok` false,
        // and nothing in `reconfigure_to` that a caller might act on.
        std::vector<uint8_t> z = build_with_manifest_text("{", why);
        jnext::zip::Reader r;
        Manifest m;
        Verdict v;
        const bool ok = jnext::jns::open_snapshot(z.data(), z.size(),
                                                  make_env(), r, m, v);
        check("JNSM-04",
              "a refusal leaves the verdict unambiguously failed: ok is false, "
              "the refusal is set, and no reconfiguration is proposed that a "
              "caller might act on",
              !ok && !v.ok && !v.refusal.empty() && v.reconfigure_to.empty(),
              det("ok=%d refusal='%s' to='%s'", ok, v.refusal.c_str(),
                  v.reconfigure_to.c_str()));
    }


    // ─────────────────────────────────────────────────────────────────────
    // JNSD — one declaration, three realisations (§9.2)
    // ─────────────────────────────────────────────────────────────────────
    {
        s2::Pilot p;
        const std::vector<uint8_t> desc_bytes = s2::bin_of(p.s);
        const std::vector<uint8_t> hand_bytes = s2::hand_bin_of(p.s);

        // THE row S3-S5 rest on. If this is green, a mechanical transcription
        // of a subsystem's `save_state` into `describe_state` cannot change
        // the stream; if it is red, every later byte-identity claim is
        // unfounded.
        check("JNSD-B01",
              "BinWriteDesc emits, for every primitive, exactly the bytes a "
              "hand-written save_state in the tree's idiom emits — the "
              "precondition the S3-S5 migration is a transcription rather "
              "than a rewrite (§17.1)",
              desc_bytes == hand_bytes,
              det("descriptor %zu bytes, hand-written %zu", desc_bytes.size(),
                  hand_bytes.size()));

        // Locate the first difference when there is one: a length-only detail
        // hides a one-byte disagreement in the middle of 200 bytes.
        std::size_t first_diff = desc_bytes.size();
        for (std::size_t i = 0; i < desc_bytes.size() && i < hand_bytes.size();
             ++i) {
            if (desc_bytes[i] != hand_bytes[i]) { first_diff = i; break; }
        }
        check("JNSD-B02",
              "and there is no first differing byte between them",
              first_diff == desc_bytes.size() &&
                  desc_bytes.size() == hand_bytes.size(),
              det("first difference at offset %zu", first_diff));

        StateWriter measure;
        BinWriteDesc md(measure);
        p.s.describe_state(md);
        check("JNSD-B03",
              "MeasureDesc — BinWriteDesc over a measure-mode StateWriter — "
              "reports the same length the write produces, so RewindBuffer's "
              "one dry-run sizing pass cannot disagree with the snapshot it "
              "then takes",
              measure.position() == desc_bytes.size(),
              det("measured %zu, wrote %zu", measure.position(),
                  desc_bytes.size()));

        s2::Pilot back;
        back.s.enabled = false;
        StateReader r(desc_bytes.data(), desc_bytes.size());
        BinReadDesc rd(r);
        back.s.describe_state(rd);
        check("JNSD-B04",
              "a binary round-trip through the descriptor restores every "
              "field, including the negative i32, the negative i64, the "
              "open-ended INT64_MAX and both FIFOs",
              !rd.failed() && back.s.same_as(p.s) &&
                  std::memcmp(back.ram, p.ram, s2::PilotState::kRamBytes) == 0,
              det("failed=%d refusal='%s' r.oob=%d", (int)rd.failed(),
                  rd.failure() ? rd.failure() : "", (int)r.out_of_bounds()));

        check("JNSD-B05",
              "the binary stream is exactly consumed: the reader ends at the "
              "byte the writer ended at, with no out-of-bounds read",
              r.position() == desc_bytes.size() && !r.out_of_bounds(),
              det("read %zu of %zu", r.position(), desc_bytes.size()));

        // The sentinel is the only thing that localises a desync in a
        // positional stream (§9.4), so its failure has to NAME the block.
        std::vector<uint8_t> corrupt = desc_bytes;
        corrupt[corrupt.size() - 1] ^= 0xFF;
        s2::Pilot bad;
        StateReader r2(corrupt.data(), corrupt.size());
        BinReadDesc rd2(r2);
        bad.s.describe_state(rd2);
        check("JNSD-B06",
              "a corrupted sentinel is a failure naming the BLOCK, not a "
              "silent mis-restore — G9 is a testable property",
              rd2.failed() && rd2.failure() &&
                  std::string(rd2.failure()) == "pilot",
              det("failed=%d name='%s'", (int)rd2.failed(),
                  rd2.failure() ? rd2.failure() : ""));

        // §9.2 consequence 3, as a testable claim rather than an assertion in
        // prose: reordering two declarations changes the positional stream
        // and leaves the named-key JSON identical.
        const std::vector<uint8_t> swapped =
            s2::bin_of(p.s, &s2::PilotState::describe_state_swapped);
        check("JNSD-B07",
              "reordering two declarations CHANGES the binary stream (§9.2): "
              "it is positional, and that is the encoding the rewind ring and "
              "the byte-identity gate are defined on",
              swapped.size() == desc_bytes.size() && swapped != desc_bytes,
              det("same size=%d, identical=%d",
                  (int)(swapped.size() == desc_bytes.size()),
                  (int)(swapped == desc_bytes)));

        JsonWriteDesc j1;
        p.s.describe_state(j1);
        JsonWriteDesc j2;
        p.s.describe_state_swapped(j2);
        check("JNSD-J01",
              "and leaves the JSON byte-identical — the JSON is what files "
              "are made of, so a reorder must not move a file (§9.2)",
              j1.str() == j2.str(),
              det("%zu vs %zu bytes", j1.str().size(), j2.str().size()));

        const std::string doc = s2::json_of(p.s);
        s2::Pilot jback;
        JsonReadDesc jr(doc);
        jback.s.describe_state(jr);
        check("JNSD-J02",
              "a JSON round-trip restores every field the binary one does",
              !jr.failed() && jback.s.same_as(p.s),
              det("failed=%d refusal='%s'", (int)jr.failed(),
                  jr.refusal().c_str()));

        // The claim that makes ONE field list worth the machinery: fed the
        // same declaration, the two encodings restore the same machine.
        check("JNSD-J03",
              "the JSON and binary realisations of ONE declaration restore "
              "IDENTICAL state — which is the property F2 buys and the reason "
              "the rewind stream and the snapshot cannot drift apart",
              back.s.same_as(jback.s),
              "");

        // §9.4 — the 33 sentinels are framing, not fields: they must survive
        // the binary encoding and VANISH from the JSON.
        check("JNSD-J04",
              "a sentinel contributes nothing to the JSON: member and key "
              "names localise a desync structurally, so a magic number in the "
              "text would have no job (§9.4)",
              doc.find("4A4E5354") == std::string::npos &&
                  doc.find("1246383444") == std::string::npos &&
                  doc.find("sentinel") == std::string::npos,
              "");

        check("JNSD-J05",
              "a blob contributes no JSON key but IS reported to the writer, "
              "so the manifest can declare it (§6.1 case 1)",
              doc.find("mem/pilot-priv.bin\":") == std::string::npos &&
                  j1.blobs().size() == 1 &&
                  j1.blobs()[0].key == "mem/pilot-priv.bin" &&
                  j1.blobs()[0].len == s2::PilotState::kPrivBytes,
              det("%zu blobs", j1.blobs().size()));

        // The declaration's `ram_window` is a window onto RAM page 16. The
        // JSON carries a REFERENCE; the 64 bytes live in `mem/ram.bin` and
        // are not copied (§6.1 case 2).
        check("JNSD-J06",
              "a ram_window emits a reference to mem/ram.bin with its page "
              "and length, and NOT the bytes (§6.1 case 2) — the duplication "
              "S5b removes from the binary stream is not reproduced in a "
              "format that has somewhere honest to put it",
              doc.find("\"ref\": \"mem/ram.bin\"") != std::string::npos &&
                  doc.find("\"page\": 16") != std::string::npos &&
                  doc.find("\"bytes\": \"64\"") != std::string::npos,
              "");

        // …while the BINARY realisation writes them inline STANDALONE, which
        // is what `desc_bytes` is: a stream with no `ram` block in it has
        // nowhere to point, so the buffer is the only copy of itself. The
        // machine-level half of the same rule is S5B-PILOT-* below.
        check("JNSD-J07",
              "the BINARY realisation writes the window's bytes inline on a "
              "STANDALONE walk, because nothing else in that stream carries "
              "them — the reference encoding needs a stream that holds the "
              "referent, which is what `machine_level` means (§17.0)",
              desc_bytes.size() > s2::PilotState::kRamBytes &&
                  std::search(desc_bytes.begin(), desc_bytes.end(),
                              p.ram, p.ram + s2::PilotState::kRamBytes) !=
                      desc_bytes.end(),
              "");

        // §9.2's static claim, asserted so it cannot go stale.
        {
            s2::Pilot unbacked;
            unbacked.s.window = nullptr;
            StateWriter mw;
            BinWriteDesc bd(mw);
            bd.set_machine_level(true);
            unbacked.s.describe_state(bd);
            check("JNSD-J08",
                  "an unbacked ram_window fails LOUDLY under a machine-level "
                  "save, naming the field — a comment claiming \"always\" with "
                  "nothing checking it is how §4 came to state the Multiface "
                  "case backwards (§9.2)",
                  bd.failed() && bd.failure() &&
                      std::string(bd.failure()) == "ram",
                  det("failed=%d name='%s'", (int)bd.failed(),
                      bd.failure() ? bd.failure() : ""));

            StateWriter mw2;
            BinWriteDesc bd2(mw2);   // machine_level defaults false
            unbacked.s.describe_state(bd2);
            check("JNSD-J09",
                  "and does NOT fire on a standalone subsystem round-trip, "
                  "which divmmc_test row DA-09 does legitimately — the "
                  "assertion belongs to the Emulator-driven realisation, not "
                  "to the declaration (§9.2)",
                  !bd2.failed(),
                  det("failed=%d name='%s'", (int)bd2.failed(),
                      bd2.failure() ? bd2.failure() : ""));
        }


        // `save_via_desc` / `load_via_desc` are the ONE place the write
        // direction's `const_cast` lives (state_desc_bin.h). S3-S5's ~34 call
        // sites go through them, so they are exercised here rather than
        // arriving untested at the first migration.
        {
            s2::Pilot src;
            const s2::PilotState& cref = src.s;   // as a `save_state() const`
            // STANDALONE, so this compares against `desc_bytes` like with
            // like: since S5b a machine-level walk is a whole window shorter
            // (S5B-PILOT-SHORTER below), and this row is about the const_cast
            // plumbing, not about the window's encoding.
            StateWriter measure;
            jnext::save::save_via_desc(cref, measure, /*machine_level=*/false);
            std::vector<uint8_t> out(measure.position());
            StateWriter w(out.data(), out.size());
            jnext::save::save_via_desc(cref, w, /*machine_level=*/false);

            s2::Pilot dst;
            dst.s.bank = 0xFF;
            StateReader rr(out.data(), out.size());
            jnext::save::load_via_desc(dst.s, rr, /*machine_level=*/false);

            check("JNSD-B08",
                  "save_via_desc drives a declaration from a CONST object and "
                  "load_via_desc restores it — the one place the write "
                  "direction's const_cast lives, so the claim that no write "
                  "realisation assigns through a bound reference is checkable "
                  "by reading one function rather than 34 call sites",
                  out == desc_bytes && !rr.out_of_bounds() &&
                      dst.s.same_as(src.s),
                  det("%zu vs %zu bytes", out.size(), desc_bytes.size()));
        }

        // ── S5b (§17.0): at machine level a window is a REFERENCE ────────
        //
        // The stage's whole content, at the primitive: `machine_level` means
        // the walk is Emulator-driven and its stream therefore carries the
        // `ram` block, so a `ram_window`'s bytes are already in it and must
        // not be written twice. The Pilot's 64-byte window stands in for the
        // DivMMC's 128 KB — 6.1 % of every rewind slot in the real stream.
        {
            s2::Pilot src;
            const s2::PilotState& cref = src.s;
            StateWriter measure;
            jnext::save::save_via_desc(cref, measure, /*machine_level=*/true);
            std::vector<uint8_t> out(measure.position());
            StateWriter w(out.data(), out.size());
            jnext::save::save_via_desc(cref, w, /*machine_level=*/true);

            check("S5B-PILOT-SHORTER",
                  "a machine-level walk is exactly one window shorter than a "
                  "standalone walk of the same declaration — the ONLY "
                  "difference between the two, and the 139 264 bytes S5b "
                  "takes out of the real stream",
                  out.size() + s2::PilotState::kRamBytes == desc_bytes.size(),
                  det("%zu vs %zu bytes", out.size(), desc_bytes.size()));

            check("S5B-PILOT-NOT-COPIED",
                  "and the window's bytes are ABSENT from it rather than "
                  "merely uncounted: the pattern the standalone stream "
                  "carries verbatim is nowhere in the machine-level one",
                  std::search(out.begin(), out.end(), src.ram,
                              src.ram + s2::PilotState::kRamBytes) ==
                      out.end(),
                  "");

            s2::Pilot dst;
            dst.s.bank         = 0xFF;
            dst.s.current_line = 7;
            dst.s.priv_ram[0]  = 0x00;
            for (std::size_t i = 0; i < s2::PilotState::kRamBytes; ++i) {
                dst.ram[i] = 0xEE;
            }
            StateReader rr(out.data(), out.size());
            jnext::save::load_via_desc(dst.s, rr, /*machine_level=*/true);
            bool window_untouched = true;
            for (std::size_t i = 0; i < s2::PilotState::kRamBytes; ++i) {
                if (dst.ram[i] != 0xEE) { window_untouched = false; break; }
            }

            check("S5B-PILOT-READ-SYMMETRIC",
                  "…and the read direction mirrors it exactly: every other "
                  "field restores, the stream is consumed to its last byte "
                  "with no overrun — reading the window here would desync by "
                  "a whole 128 KB at the very next sentinel — and the window "
                  "is left for the machine's own `ram` restore to fill",
                  dst.s.same_as(src.s) && window_untouched &&
                      rr.position() == out.size() && !rr.out_of_bounds(),
                  det("pos %zu of %zu, window %s", rr.position(), out.size(),
                      window_untouched ? "untouched" : "OVERWRITTEN"));
        }

        // ── Found by mutation: BinReadDesc's own refusals ────────────────
        //
        // The three below had NO row until the mutation table derived from the
        // diff showed them surviving. A table built from the row list could
        // not have found them: the rows did not exist.
        {
            std::vector<uint8_t> bad = desc_bytes;
            // The enum ordinal sits immediately after `entry_points`.
            const std::size_t mode_at = 1 + 1 + 2 + 4 + 8 + 4 + 8 + 8 + 4;
            bad[mode_at] = 9;   // beyond kModes
            s2::Pilot q;
            StateReader r(bad.data(), bad.size());
            BinReadDesc rd(r);
            q.s.describe_state(rd);
            check("JNSD-B09",
                  "BinReadDesc refuses an enum ORDINAL outside the declared "
                  "name set, naming the field: in the binary stream an "
                  "unnameable ordinal means the declaration and the stream "
                  "disagree, which is the desync the sentinels localise",
                  rd.failed() && rd.failure() &&
                      std::string(rd.failure()) == "mode",
                  det("failed=%d name='%s'", (int)rd.failed(),
                      rd.failure() ? rd.failure() : ""));
        }
        {
            std::vector<uint8_t> bad = desc_bytes;
            const std::size_t log_at = 1 + 1 + 2 + 4 + 8 + 4 + 8 + 8 + 4 + 1 +
                                       s2::PilotState::kPrivBytes +
                                       s2::PilotState::kRamBytes;
            bad[log_at] = 0xFF;      // a u16 count of 65535 against a cap of 8
            bad[log_at + 1] = 0xFF;
            s2::Pilot q;
            StateReader r(bad.data(), bad.size());
            BinReadDesc rd(r);
            q.s.describe_state(rd);
            check("JNSD-B10",
                  "a corrupt log COUNT in the binary stream is clamped to the "
                  "declared capacity, so it can neither overrun the array nor "
                  "desync the stream — the stream always carries exactly "
                  "`capacity` entries whatever the count claims "
                  "(ula.cpp:1621-1625)",
                  q.s.log_count == s2::PilotState::kLogCap &&
                      r.position() == desc_bytes.size() && !r.out_of_bounds(),
                  det("count=%zu read %zu of %zu", q.s.log_count, r.position(),
                      desc_bytes.size()));
        }
        {
            std::vector<uint8_t> bad = desc_bytes;
            const std::size_t log_at = 1 + 1 + 2 + 4 + 8 + 4 + 8 + 8 + 4 + 1 +
                                       s2::PilotState::kPrivBytes +
                                       s2::PilotState::kRamBytes;
            const std::size_t tx_at =
                log_at + 2 + s2::PilotState::kLogCap * 3;
            for (std::size_t i = 0; i < 8; ++i) bad[tx_at + i] = 0xFF;
            s2::Pilot q;
            StateReader r(bad.data(), bad.size());
            BinReadDesc rd(r);
            q.s.describe_state(rd);
            check("JNSD-B11",
                  "a corrupt FIFO count (here UINT64_MAX) can neither overrun "
                  "the ring nor desync the stream: the loop is bounded by the "
                  "DECLARED capacity and `push` refuses when full, so nothing "
                  "here depends on the file's number being sane (uart.h:62-73)",
                  q.s.tx.size() == 6 && r.position() == desc_bytes.size() &&
                      !r.out_of_bounds(),
                  det("size=%zu read %zu of %zu", q.s.tx.size(), r.position(),
                      desc_bytes.size()));
        }

        check("JNSD-J11",
              "the JSON ends with exactly one newline: a generated artefact "
              "that a gate byte-diffs must not depend on an editor putting one "
              "back (found by mutation — no row asserted it)",
              doc.size() >= 2 && doc[doc.size() - 1] == '\n' &&
                  doc[doc.size() - 2] == '}',
              det("tail='%s'", doc.substr(doc.size() - 2).c_str()));

        check("JNSD-J10",
              "the JSON is deterministic: two runs over the same state are "
              "byte-identical and the keys are SORTED, not in declaration or "
              "hash order (§16.3)",
              j1.str() == s2::json_of(p.s) &&
                  doc.find("\"bank\"") < doc.find("\"current_line\"") &&
                  doc.find("\"current_line\"") < doc.find("\"enabled\"") &&
                  doc.find("\"enabled\"") < doc.find("\"entry_points\""),
              "");
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSE — the §6.2 encoding table, entry by entry
    // ─────────────────────────────────────────────────────────────────────
    {
        s2::Pilot p;
        const std::string doc = s2::json_of(p.s);

        check("JNSE-01",
              "a boolean encodes as true/false, not as 0/1",
              doc.find("\"enabled\": true") != std::string::npos, "");

        check("JNSE-02",
              "unsigned values up to 32 bits encode as JSON NUMBERS, decimal",
              doc.find("\"bank\": 42") != std::string::npos &&
                  doc.find("\"current_line\": 191") != std::string::npos &&
                  doc.find("\"frame_num\": 41291") != std::string::npos,
              "");

        // §7.4 — the 2^53 rule. 9007199254740993 is the smallest integer a
        // double cannot represent, so a JSON number here would come back as
        // 9007199254740992 in any JavaScript reader.
        check("JNSE-03",
              "a u64 encodes as a decimal STRING, and a value past 2^53 "
              "survives it exactly (§7.4)",
              doc.find("\"monotonic\": \"9007199254740993\"") !=
                  std::string::npos,
              "");

        check("JNSE-04",
              "a signed 32-bit value encodes as a JSON number and may be "
              "negative — the tree makes 11 write_i32 calls and an encoding "
              "table without a signed type would force every one through an "
              "unsigned reinterpretation (§6.2)",
              doc.find("\"flash_counter\": -7") != std::string::npos, "");

        // §16.1 names this value: the /INT window's real measured delta.
        check("JNSE-05",
              "a negative i64 encodes as a signed decimal string and "
              "round-trips — asserted with the /INT window's real measured "
              "value, -564933",
              doc.find("\"int_first_ts\": \"-564933\"") != std::string::npos,
              "");

        check("JNSE-06",
              "INT64_MAX encodes as the string \"open\", not as "
              "9223372036854775807 — the open-ended sentinel is a NAME, so a "
              "reader cannot mistake it for a very distant deadline (§6.2)",
              doc.find("\"int_last_ts\": \"open\"") != std::string::npos, "");

        check("JNSE-07",
              "a fixed array encodes as ONE lower-case hex string of exactly "
              "2N characters, no separators (§6.2)",
              doc.find("\"entry_points\": \"830100cd\"") != std::string::npos,
              "");

        check("JNSE-08",
              "an enum encodes as its NAME from a closed set, so an FSM "
              "renumbering becomes a visible name change rather than a silent "
              "re-interpretation of old files (§6.2)",
              doc.find("\"mode\": \"wait_for_vpos\"") != std::string::npos, "");

        // Round-trips, each fed back through the reader rather than asserted
        // only on the text: an encoding that is unreadable is not an encoding.
        s2::Pilot back;
        JsonReadDesc jr(doc);
        back.s.describe_state(jr);
        check("JNSE-09",
              "every one of those encodings reads back to the value it came "
              "from",
              !jr.failed() && back.s.monotonic == p.s.monotonic &&
                  back.s.flash_counter == p.s.flash_counter &&
                  back.s.int_first_ts == p.s.int_first_ts &&
                  back.s.int_last_ts == INT64_MAX && back.s.mode == p.s.mode &&
                  std::memcmp(back.s.entry_points, p.s.entry_points, 4) == 0,
              det("refusal='%s'", jr.refusal().c_str()));

        // §16.1: "an unknown name on read REFUSES rather than defaulting — a
        // wrong FSM state is not a safe default."
        {
            const std::string bad =
                s2::json_with(doc, "mode", "\"wait_for_hpos\"");
            s2::Pilot q;
            JsonReadDesc r(bad);
            q.s.describe_state(r);
            check("JNSE-10",
                  "an enum NAME this build does not declare is a REFUSAL, "
                  "never a default — a wrong FSM state is a machine that "
                  "never existed (§16.1)",
                  r.failed() &&
                      r.refusal().find("mode") != std::string::npos,
                  det("failed=%d refusal='%s'", (int)r.failed(),
                      r.refusal().c_str()));
        }

        // The write direction of the same rule: an ordinal outside the name
        // table cannot be encoded, because there is no name for it.
        {
            s2::Pilot q;
            q.s.mode = 9;   // beyond kModes
            JsonWriteDesc jw;
            q.s.describe_state(jw);
            check("JNSE-11",
                  "an enum ORDINAL outside the declared name set fails on "
                  "WRITE too, naming the field: the file must not carry a "
                  "state the declaration cannot name",
                  jw.failed() && jw.failure() &&
                      std::string(jw.failure()) == "mode",
                  det("failed=%d name='%s'", (int)jw.failed(),
                      jw.failure() ? jw.failure() : ""));
        }

        // §12.2 — a MISSING optional key takes its DECLARED default, which is
        // a per-field constant in the descriptor, NOT the result of reset().
        // Asserted against the declaration's own value.
        {
            std::string stripped = doc;
            const std::size_t at = stripped.find("  \"bank\": 42,\n");
            if (at != std::string::npos) {
                stripped.erase(at, std::strlen("  \"bank\": 42,\n"));
            }
            s2::Pilot q;
            q.s.bank = 0xFF;   // NOT the declared default, so a no-op passes
            JsonReadDesc r(stripped);
            q.s.describe_state(r);
            check("JNSE-12",
                  "a missing OPTIONAL key takes the default the DECLARATION "
                  "carries (§12.2) — 0x00 for `bank`, asserted against the "
                  "declaration and not against a literal elsewhere or against "
                  "reset()",
                  at != std::string::npos && !r.failed() && q.s.bank == 0x00,
                  det("stripped=%d failed=%d bank=0x%02X",
                      (int)(at != std::string::npos), (int)r.failed(),
                      q.s.bank));
        }

        // …and a missing REQUIRED key is a refusal naming it. `frame_num` is
        // declared without a default precisely because no honest one exists.
        {
            std::string stripped = doc;
            const std::size_t at = stripped.find("  \"frame_num\": 41291,\n");
            if (at != std::string::npos) {
                stripped.erase(at, std::strlen("  \"frame_num\": 41291,\n"));
            }
            s2::Pilot q;
            JsonReadDesc r(stripped);
            q.s.describe_state(r);
            check("JNSE-13",
                  "a missing REQUIRED key is a refusal naming it — a key is "
                  "marked required only when no honest default exists (§12.2)",
                  at != std::string::npos && r.failed() &&
                      r.refusal().find("frame_num") != std::string::npos,
                  det("refusal='%s'", r.refusal().c_str()));
        }

        // §12.1 — an unknown key is IGNORED and LOGGED, so a newer file read
        // by an older jnext says what it dropped.
        {
            std::string extra = doc;
            const std::size_t at = extra.find("  \"bank\":");
            if (at != std::string::npos) {
                extra.insert(at, "  \"from_the_future\": 1,\n");
            }
            s2::Pilot q;
            JsonReadDesc r(extra);
            q.s.describe_state(r);
            const std::vector<std::string> unknown = r.unclaimed_keys();
            check("JNSE-14",
                  "an unknown key is IGNORED and REPORTED — forward "
                  "compatibility for additive changes, and a reader that can "
                  "say what it dropped (§12.1)",
                  at != std::string::npos && !r.failed() &&
                      unknown.size() == 1 && unknown[0] == "from_the_future" &&
                      q.s.same_as(p.s),
                  det("failed=%d unknown=%zu", (int)r.failed(),
                      unknown.size()));
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSH — the two count-prefixed history primitives (§6.2, §9.5(1))
    // ─────────────────────────────────────────────────────────────────────
    {
        s2::Pilot p;
        const std::vector<uint8_t> full = s2::bin_of(p.s);

        // The constant width is load-bearing, not stylistic: RewindBuffer
        // sizes every slot from one dry-run measure and requires each
        // snapshot to be exactly that width. Serialising these
        // variable-length "was the actual bug behind a `free(): invalid size`
        // heap-corruption crash" (attribute_mux.h:216-235).
        s2::Pilot empty;
        empty.s.log_count = 0;
        empty.s.tx.reset();
        empty.s.rx.reset();
        const std::vector<uint8_t> none = s2::bin_of(empty.s);
        check("JNSH-01",
              "the BINARY width of a log and of a FIFO is CONSTANT whatever "
              "the live count — the property RewindBuffer requires and whose "
              "absence was a heap-corruption crash (§6.2)",
              none.size() == full.size(),
              det("%zu with entries, %zu empty", full.size(), none.size()));

        s2::Pilot brim;
        brim.s.log_count = s2::PilotState::kLogCap;
        while (brim.s.tx.push(0x77)) {}
        while (brim.s.rx.push(0x777)) {}
        check("JNSH-02",
              "…and is the same again with every buffer at capacity",
              s2::bin_of(brim.s).size() == full.size(), "");

        // The four differences §6.2's table states, each one localised.
        // Offset of the log's count: everything before it is fixed-width.
        const std::size_t log_at = 1 + 1 + 2 + 4 + 8 + 4 + 8 + 8 + 4 + 1 +
                                   s2::PilotState::kPrivBytes +
                                   s2::PilotState::kRamBytes;
        check("JNSH-03",
              "a log's count is a u16 written FIRST (ula.cpp:1586-1587)",
              full.size() > log_at + 1 && full[log_at] == 3 &&
                  full[log_at + 1] == 0,
              det("at %zu: %02X %02X", log_at,
                  full.size() > log_at ? full[log_at] : 0,
                  full.size() > log_at + 1 ? full[log_at + 1] : 0));

        // The first live entry: u16 line then u8 value, 3 bytes, unpadded.
        check("JNSH-04",
              "a log element is u16 line + u8 value, 3 bytes, unpadded",
              full.size() > log_at + 5 && full[log_at + 2] == 8 &&
                  full[log_at + 3] == 0 && full[log_at + 4] == 0x01 &&
                  full[log_at + 5] == 64,
              "");

        // The STALE tail: entries past `count` are whatever was there. The
        // pilot puts recognisable junk at indices 3 and 4.
        const std::size_t stale_at = log_at + 2 + 3 * 3;
        check("JNSH-05",
              "the binary padding past a log's count carries the STALE "
              "entries verbatim — ignored on load, and NOT zeroed, because "
              "zeroing them would be a different stream (§6.2)",
              full.size() > stale_at + 2 && full[stale_at + 2] == 0xEE,
              det("stale value at %zu = %02X", stale_at + 2,
                  full.size() > stale_at + 2 ? full[stale_at + 2] : 0));

        // The FIFO's four differences from the log.
        const std::size_t tx_at = log_at + 2 + s2::PilotState::kLogCap * 3;
        check("JNSH-06",
              "a FIFO's count is a u64, not a u16 — the second of the four "
              "differences between the two shapes (§6.2)",
              full.size() > tx_at + 7 && full[tx_at] == 4 &&
                  full[tx_at + 1] == 0 && full[tx_at + 7] == 0,
              det("at %zu", tx_at));

        // The pilot's TX ring holds 0x11,0x22,0x33,0x44 rotated by two, so the
        // RAW array order is 33 44 11 22 and the RING-NORMALISED order is
        // 33 44 11 22 read from the tail... the two orders differ, which is
        // the only way to tell the encodings apart.
        check("JNSH-07",
              "a FIFO's elements are RING-NORMALISED, oldest first from the "
              "tail — not raw array order, the third difference (§6.2)",
              full.size() > tx_at + 8 + 3 &&
                  full[tx_at + 8 + 0] == p.s.tx.oldest(0) &&
                  full[tx_at + 8 + 1] == p.s.tx.oldest(1) &&
                  full[tx_at + 8 + 2] == p.s.tx.oldest(2) &&
                  full[tx_at + 8 + 3] == p.s.tx.oldest(3),
              det("tail=%zu", p.s.tx.tail_));

        check("JNSH-08",
              "a FIFO pads past its count with ZERO, not with stale entries — "
              "the fourth difference, and the reason one parameterised "
              "d.history() would be a vocabulary nobody can read (§6.2)",
              full.size() > tx_at + 8 + 5 && full[tx_at + 8 + 4] == 0 &&
                  full[tx_at + 8 + 5] == 0,
              "");

        // The RX FIFO is u16-wide because uart.vhd:359 carries a 9th
        // (overflow OR framing) bit per received byte.
        const std::size_t rx_at = tx_at + 8 + 6;
        check("JNSH-09",
              "the RX FIFO's element is u16 where TX's is u8 — uart.vhd:359's "
              "9th (overflow OR framing) bit per received byte",
              full.size() >= rx_at + 8 + 5 * 2 &&
                  full.size() == rx_at + 8 + 5 * 2 + 4,
              det("rx at %zu, total %zu", rx_at, full.size()));

        // The JSON half of the same primitive: exactly `count` items.
        const std::string doc = s2::json_of(p.s);
        check("JNSH-10",
              "the JSON encoding of a log carries exactly COUNT items, never "
              "the padded capacity — a snapshot's text carrying 1024 entries "
              "to express three is what this primitive exists to avoid (§6.2)",
              doc.find("\"port_ff_log\": [") != std::string::npos &&
                  doc.find("\"line\": 8") != std::string::npos &&
                  doc.find("\"line\": 191") != std::string::npos &&
                  doc.find("\"value\": 238") == std::string::npos,   // 0xEE
              "");

        check("JNSH-11",
              "and the JSON encoding of a FIFO likewise carries exactly its "
              "count, oldest first",
              doc.find("\"tx_fifo\": [") != std::string::npos &&
                  doc.find("\"rx_fifo\": [") != std::string::npos,
              "");

        // The row §16.1 pins: three in-flight entries restore as three.
        s2::Pilot back;
        back.s.log_count = s2::PilotState::kLogCap;
        JsonReadDesc jr(doc);
        back.s.describe_state(jr);
        check("JNSH-12",
              "a round-trip through JSON with 3 in-flight log entries "
              "restores 3, not the capacity (§16.1 JNSH)",
              !jr.failed() && back.s.log_count == 3 &&
                  back.s.log[0].line == 8 && back.s.log[2].line == 191,
              det("count=%zu refusal='%s'", back.s.log_count,
                  jr.refusal().c_str()));

        check("JNSH-13",
              "and restores both FIFOs to the same logical sequence, with the "
              "ring normalised",
              back.s.tx.same_as(p.s.tx) && back.s.rx.same_as(p.s.rx),
              det("tx %zu/%zu rx %zu/%zu", back.s.tx.size(), p.s.tx.size(),
                  back.s.rx.size(), p.s.rx.size()));
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSA — ADVERSARIAL input
    // ─────────────────────────────────────────────────────────────────────
    //
    // S1's design passed two review rounds; S1's IMPLEMENTATION review still
    // found a zip-slip in the documented path regex and a 167-byte archive
    // that forced a 4.29 GB allocation. Reviewing a spec is not reviewing a
    // parser. So every length, count and index a document supplies is fed
    // back hostile here: too short, too long, wrong type, out of range,
    // non-canonical, duplicated, and self-referential.
    //
    // The shared property every row asserts is the same one: THE DECLARATION
    // FIXES THE SIZE. A file supplies content, never a size to allocate.
    {
        s2::Pilot p;
        const std::string good = s2::json_of(p.s);

        struct Case {
            const char* id;
            const char* desc;
            std::string doc;
            const char* names;   ///< the refusal must name this
        };

        std::vector<Case> cases;

        cases.push_back({"JNSA-01",
                         "a truncated document is a refusal, and no field is "
                         "touched — a malformed file cannot leave a subsystem "
                         "half-restored from garbage",
                         good.substr(0, good.size() / 2), "<document>"});
        cases.push_back({"JNSA-02",
                         "a document that is a JSON ARRAY, not an object, is "
                         "refused rather than iterated",
                         "[1,2,3]\n", "<document>"});
        cases.push_back({"JNSA-03",
                         "a document that is a bare scalar is refused",
                         "42\n", "<document>"});
        cases.push_back({"JNSA-04",
                         "an EMPTY document is refused: a state member with "
                         "no keys is not a subsystem with all its defaults",
                         "", "<document>"});
        cases.push_back(
            {"JNSA-05",
             "a hex string SHORTER than the declaration is refused, naming "
             "both lengths — the declaration fixes the length, never the file",
             s2::json_with(good, "entry_points", "\"8301\""), "entry_points"});
        cases.push_back({"JNSA-06",
                         "a hex string LONGER than the declaration is refused: "
                         "the excess is not silently dropped",
                         s2::json_with(good, "entry_points",
                                       "\"830100cd830100cd\""),
                         "entry_points"});
        cases.push_back({"JNSA-07",
                         "UPPER-CASE hex is refused: §6.2 says lower case, and "
                         "a reader permissive enough to accept its own "
                         "writer's output plus more is the .szx loader",
                         s2::json_with(good, "entry_points", "\"830100CD\""),
                         "entry_points"});
        cases.push_back({"JNSA-08",
                         "non-hexadecimal characters in a hex string are "
                         "refused",
                         s2::json_with(good, "entry_points", "\"83zz00cd\""),
                         "entry_points"});
        cases.push_back({"JNSA-09",
                         "a hex field given a NUMBER instead of a string is "
                         "refused",
                         s2::json_with(good, "entry_points", "830100"),
                         "entry_points"});
        cases.push_back(
            {"JNSA-10",
             "a u64 given as a JSON NUMBER is refused: accepting both "
             "spellings would make the schema's pattern decorative and "
             "re-open the 2^53 rounding §7.4 closes",
             s2::json_with(good, "monotonic", "9007199254740993"),
             "monotonic"});
        cases.push_back({"JNSA-11",
                         "a u64 string with a leading zero is refused — one "
                         "spelling per value, so two documents cannot mean the "
                         "same state and differ under a byte-diffed gate",
                         s2::json_with(good, "monotonic", "\"007\""),
                         "monotonic"});
        cases.push_back({"JNSA-12",
                         "a NEGATIVE u64 string is refused rather than wrapped "
                         "to a very large positive",
                         s2::json_with(good, "monotonic", "\"-1\""),
                         "monotonic"});
        cases.push_back({"JNSA-13",
                         "a u64 string past 2^64-1 is refused, not truncated",
                         s2::json_with(good, "monotonic",
                                       "\"18446744073709551616\""),
                         "monotonic"});
        cases.push_back({"JNSA-14",
                         "a u64 string with trailing junk is refused: strtoull "
                         "would have accepted it",
                         s2::json_with(good, "monotonic", "\"123abc\""),
                         "monotonic"});
        cases.push_back({"JNSA-15",
                         "a u64 string with a leading + is refused",
                         s2::json_with(good, "monotonic", "\"+7\""),
                         "monotonic"});
        cases.push_back({"JNSA-16",
                         "an i64 string past INT64_MAX is refused",
                         s2::json_with(good, "int_first_ts",
                                       "\"9223372036854775808\""),
                         "int_first_ts"});
        cases.push_back({"JNSA-17",
                         "\"-0\" is refused as a second spelling of zero",
                         s2::json_with(good, "int_first_ts", "\"-0\""),
                         "int_first_ts"});
        cases.push_back(
            {"JNSA-18",
             "an i64_open field given a name that is not \"open\" is refused "
             "rather than parsed as zero",
             s2::json_with(good, "int_last_ts", "\"opne\""), "int_last_ts"});
        cases.push_back({"JNSA-19",
                         "a u8 given 256 is refused: the width comes from the "
                         "DECLARATION, and 256 is not silently truncated to 0",
                         s2::json_with(good, "bank", "256"), "bank"});
        cases.push_back({"JNSA-20",
                         "a u8 given a negative number is refused",
                         s2::json_with(good, "bank", "-1"), "bank"});
        cases.push_back({"JNSA-21",
                         "a u16 given 65536 is refused",
                         s2::json_with(good, "current_line", "65536"),
                         "current_line"});
        cases.push_back({"JNSA-22",
                         "a u32 given a float is refused rather than rounded",
                         s2::json_with(good, "frame_num", "41291.5"),
                         "frame_num"});
        cases.push_back({"JNSA-23",
                         "an i32 outside the signed 32-bit range is refused",
                         s2::json_with(good, "flash_counter", "2147483648"),
                         "flash_counter"});
        cases.push_back({"JNSA-24",
                         "a boolean given the number 1 is refused: JSON has "
                         "booleans and the encoding table names them",
                         s2::json_with(good, "enabled", "1"), "enabled"});
        cases.push_back(
            {"JNSA-25",
             "a log array LONGER than the declared capacity is refused before "
             "a single entry is stored — the file cannot size the array, and "
             "there is nothing here for it to allocate",
             s2::json_with(good, "port_ff_log",
                           "[{\"line\":1,\"value\":1},{\"line\":2,\"value\":2},"
                           "{\"line\":3,\"value\":3},{\"line\":4,\"value\":4},"
                           "{\"line\":5,\"value\":5},{\"line\":6,\"value\":6},"
                           "{\"line\":7,\"value\":7},{\"line\":8,\"value\":8},"
                           "{\"line\":9,\"value\":9}]"),
             "port_ff_log"});
        cases.push_back({"JNSA-26",
                         "a log entry missing its value is refused",
                         s2::json_with(good, "port_ff_log",
                                       "[{\"line\":1}]"),
                         "port_ff_log"});
        cases.push_back({"JNSA-27",
                         "a log entry whose line exceeds u16 is refused",
                         s2::json_with(good, "port_ff_log",
                                       "[{\"line\":70000,\"value\":1}]"),
                         "port_ff_log"});
        cases.push_back({"JNSA-28",
                         "a log given an object instead of an array is refused",
                         s2::json_with(good, "port_ff_log", "{\"line\":1}"),
                         "port_ff_log"});
        cases.push_back(
            {"JNSA-29",
             "a FIFO array longer than its capacity is refused",
             s2::json_with(good, "tx_fifo", "[1,2,3,4,5,6,7]"), "tx_fifo"});
        cases.push_back({"JNSA-30",
                         "a u8 FIFO element of 256 is refused",
                         s2::json_with(good, "tx_fifo", "[256]"), "tx_fifo"});
        cases.push_back(
            {"JNSA-31",
             "a ram_window reference naming a DIFFERENT member is refused — "
             "the alias is part of the declaration, not of the file",
             s2::json_with(good, "ram",
                           "{\"ref\":\"mem/elsewhere.bin\",\"page\":16,"
                           "\"bytes\":\"64\"}"),
             "ram"});
        cases.push_back({"JNSA-32",
                         "a ram_window reference declaring a different page is "
                         "refused",
                         s2::json_with(good, "ram",
                                       "{\"ref\":\"mem/ram.bin\",\"page\":17,"
                                       "\"bytes\":\"64\"}"),
                         "ram"});
        cases.push_back({"JNSA-33",
                         "a ram_window reference declaring a different length "
                         "is refused",
                         s2::json_with(good, "ram",
                                       "{\"ref\":\"mem/ram.bin\",\"page\":16,"
                                       "\"bytes\":\"4294967296\"}"),
                         "ram"});
        cases.push_back({"JNSA-34",
                         "a ram_window given the BYTES inline is refused: "
                         "there must be exactly one place the bytes live",
                         s2::json_with(good, "ram", "\"00112233\""), "ram"});
        cases.push_back(
            {"JNSA-35",
             "a blob key appearing inline in the JSON is refused: its bytes "
             "are a ZIP member, and two places to look for them is one too "
             "many",
             [&] {
                 std::string d = good;
                 const std::size_t at = d.find("  \"bank\":");
                 if (at != std::string::npos) {
                     d.insert(at, "  \"mem/pilot-priv.bin\": \"00\",\n");
                 }
                 return d;
             }(),
             "mem/pilot-priv.bin"});
        cases.push_back(
            {"JNSA-36",
             "a DUPLICATE key is refused. nlohmann parses {\"x\":1,\"x\":2} "
             "silently to {\"x\":2} — measured — and two implementations may "
             "legitimately disagree which wins, which is exactly why §12.4 "
             "refuses duplicate ZIP MEMBER names. It is also the one thing a "
             "JSON Schema cannot catch, whatever validator is used: the "
             "duplicate is gone before the validator sees the document",
             [&] {
                 std::string d = good;
                 const std::size_t at = d.find("  \"bank\":");
                 if (at != std::string::npos) d.insert(at, "  \"bank\": 7,\n");
                 return d;
             }(),
             "<document>"});
        cases.push_back(
            {"JNSA-37",
             "a document nested past the depth bound is refused. Not a "
             "stack-overflow guard — nlohmann's parser and DOM destructor are "
             "both iterative, measured to 5 000 000 levels — but a bound on "
             "MEMORY AMPLIFICATION: one DOM node per byte, and S1's "
             "central-directory cap lets a member be 64 MB",
             [] {
                 std::string d = "{\"port_ff_log\": ";
                 for (int i = 0; i < 64; ++i) d += "[";
                 for (int i = 0; i < 64; ++i) d += "]";
                 d += "}";
                 return d;
             }(),
             "<document>"});

        // A document-level refusal must leave the subsystem COMPLETELY
        // untouched: the reader latches it at construction, before any field
        // is visited. A key-level refusal cannot promise that — keys before
        // the offending one have already been read, and pretending otherwise
        // would need a two-pass reader nothing asks for.
        std::size_t doc_level = 0, doc_level_clean = 0;

        for (const Case& c : cases) {
            s2::Pilot q;
            s2::Pilot pristine;
            JsonReadDesc r(c.doc);
            q.s.describe_state(r);
            const bool named =
                r.refusal().find(c.names) != std::string::npos;
            check(c.id, c.desc, r.failed() && named,
                  det("failed=%d refusal='%s' (expected to name '%s')",
                      (int)r.failed(), r.refusal().c_str(), c.names));
            if (std::string(c.names) == "<document>") {
                ++doc_level;
                if (q.s.same_as(pristine.s)) ++doc_level_clean;
            }
        }

        // ── Found by mutation ───────────────────────────────────────────
        //
        // JNSA-38 above refuses at the CAPACITY check, which is before the
        // ring is touched — so it cannot see a reader that resets the ring and
        // only then discovers a bad element. This one refuses at the ELEMENT
        // check, which is where validate-before-mutate actually has to hold.
        {
            s2::Pilot q;
            q.s.tx.reset();
            q.s.tx.push(0xAB);
            q.s.tx.push(0xCD);
            // Two entries: within capacity, but 999 does not fit a u8 element.
            const std::string bad = s2::json_with(good, "tx_fifo", "[1,999]");
            JsonReadDesc r(bad);
            q.s.describe_state(r);
            check("JNSA-42",
                  "a FIFO refused on an ELEMENT — not on its length — also "
                  "leaves the ring untouched: every element is validated "
                  "before any is stored, so a rejected file cannot leave a "
                  "prefix of itself behind",
                  r.failed() && q.s.tx.size() == 2 &&
                      q.s.tx.oldest(0) == 0xAB && q.s.tx.oldest(1) == 0xCD,
                  det("failed=%d size=%zu refusal='%s'", (int)r.failed(),
                      q.s.tx.size(), r.refusal().c_str()));
        }
        {
            // The message must SAY WHICH document-level fault it was. A reader
            // that collapsed "truncated" and "not an object" into one message
            // still refuses both, so a verdict-only row cannot tell them
            // apart — and they are different problems for the person holding
            // the file.
            s2::Pilot q1, q2;
            JsonReadDesc r1(good.substr(0, good.size() / 2));
            q1.s.describe_state(r1);
            JsonReadDesc r2("[1,2,3]\n");
            q2.s.describe_state(r2);
            check("JNSA-43",
                  "a TRUNCATED document and a WELL-FORMED non-object give "
                  "DIFFERENT messages — 'is not valid JSON' against 'is not a "
                  "JSON object' (found by mutation: dropping the parse check "
                  "left both refused, with one message)",
                  r1.refusal().find("is not valid JSON") != std::string::npos &&
                      r2.refusal().find("is not a JSON object") !=
                          std::string::npos,
                  det("r1='%s' r2='%s'", r1.refusal().c_str(),
                      r2.refusal().c_str()));
        }

        check("JNSA-41",
              "a DOCUMENT-level refusal leaves every field untouched: the "
              "reader latches it at construction, so a malformed file cannot "
              "leave a subsystem half-restored from garbage",
              doc_level >= 6 && doc_level_clean == doc_level,
              det("%zu of %zu document-level cases left the state clean",
                  doc_level_clean, doc_level));

        // Validate-before-mutate, asserted rather than assumed: an
        // over-capacity FIFO must leave the ring exactly as it was, not
        // holding a prefix of a rejected file.
        {
            s2::Pilot q;
            // The pilot's constructor already fills the ring; start from a
            // known TWO so "unchanged" is a value, not a coincidence.
            q.s.tx.reset();
            q.s.tx.push(0xAB);
            q.s.tx.push(0xCD);
            const std::string bad =
                s2::json_with(good, "tx_fifo", "[1,2,3,4,5,6,7]");
            JsonReadDesc r(bad);
            q.s.describe_state(r);
            check("JNSA-38",
                  "a refused FIFO leaves the ring UNCHANGED — a refusal "
                  "half-way through would leave it holding a prefix of a "
                  "rejected file, which is a machine that never existed",
                  r.failed() && q.s.tx.size() == 2 &&
                      q.s.tx.oldest(0) == 0xAB && q.s.tx.oldest(1) == 0xCD,
                  det("size=%zu", q.s.tx.size()));
        }

        // The first refusal is the real one: a reader that let a later,
        // cascading failure overwrite it would name the wrong field.
        {
            std::string two = s2::json_with(good, "bank", "256");
            two = s2::json_with(two, "current_line", "65536");
            s2::Pilot q;
            JsonReadDesc r(two);
            q.s.describe_state(r);
            check("JNSA-39",
                  "with two faults present the FIRST is reported, so the "
                  "message names the field a user has to fix rather than a "
                  "cascade downstream of it",
                  r.failed() && r.refusal().find("bank") != std::string::npos,
                  det("refusal='%s'", r.refusal().c_str()));
        }

        // Every refusal above must be distinguishable. A reader whose message
        // collapsed to "invalid snapshot" would pass each row that only
        // checked `failed()`, which is why the rows check the NAME too — and
        // why this row checks they are not all the SAME name-bearing string.
        {
            // The anti-collapse property, stated exactly rather than as a
            // percentage. Several cases SHOULD share a message — five
            // different non-canonical `monotonic` strings are one fault on
            // one key, and inventing five messages for them would be noise.
            // What must never happen is one message covering two DIFFERENT
            // fields, because that is the "invalid snapshot" collapse with a
            // longer string: a user told which field is wrong can fix it, and
            // a user told "a value is wrong" cannot.
            std::map<std::string, std::set<std::string>> msg_to_keys;
            for (const Case& c : cases) {
                s2::Pilot q;
                JsonReadDesc r(c.doc);
                q.s.describe_state(r);
                if (!r.refusal().empty()) msg_to_keys[r.refusal()].insert(c.names);
            }
            std::string offender;
            for (const auto& kv : msg_to_keys) {
                if (kv.second.size() > 1) { offender = kv.first; break; }
            }
            check("JNSA-40",
                  "no refusal message covers two different fields — the "
                  "\"invalid snapshot\" collapse with a longer string. Cases "
                  "that are the same fault on the same key SHARE a message, "
                  "deliberately (§16.1 JNSM)",
                  offender.empty() && msg_to_keys.size() >= 14,
                  det("%zu distinct messages over %zu cases; offender='%s'",
                      msg_to_keys.size(), cases.size(), offender.c_str()));
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSS — the generated schema's shape (§5.3, §6.2, §9.3, §16.3)
    // ─────────────────────────────────────────────────────────────────────
    //
    // These assert the SHAPE the §6.2 encoding table demands, which is an
    // external statement of what each type must produce. They do NOT assert
    // that the schema is semantically right about the machine: a schema
    // generated from a declaration and checked against JSON produced from the
    // same declaration agrees with itself by construction (§9.3). That is what
    // `make schema-check` and the hand-written overlay are for, and neither is
    // a row here.
    {
        s2::Pilot p;
        SchemaDesc sd;
        p.s.describe_state(sd);
        const std::string schema = sd.str();

        check("JNSS-01",
              "the emitted schema is a JSON object with properties and a "
              "required list",
              !sd.failed() && schema.find("\"type\": \"object\"") !=
                                  std::string::npos &&
                  schema.find("\"properties\"") != std::string::npos &&
                  schema.find("\"required\"") != std::string::npos,
              "");

        check("JNSS-02",
              "every scalar and aggregate in the declaration appears as a "
              "property, and the sentinel does NOT (§9.4)",
              schema.find("\"enabled\"") != std::string::npos &&
                  schema.find("\"bank\"") != std::string::npos &&
                  schema.find("\"monotonic\"") != std::string::npos &&
                  schema.find("\"entry_points\"") != std::string::npos &&
                  schema.find("\"port_ff_log\"") != std::string::npos &&
                  schema.find("\"tx_fifo\"") != std::string::npos &&
                  schema.find("\"ram\"") != std::string::npos &&
                  schema.find("sentinel") == std::string::npos,
              "");

        // §12.2 — required is exactly the set with NO declared default.
        {
            // Count the required entries rather than spot-checking a name:
            // "some of them are listed" would pass while the rest silently
            // became optional, which is the direction that loses data.
            const std::size_t at  = schema.rfind("\"required\": [");
            const std::size_t end = schema.find(']', at);
            std::size_t n = 0;
            if (at != std::string::npos && end != std::string::npos) {
                const std::string body =
                    schema.substr(at + 13, end - (at + 13));
                n = static_cast<std::size_t>(
                        std::count(body.begin(), body.end(), '"')) / 2;
            }
            check("JNSS-03",
                  "the required list holds EXACTLY the declarations with no "
                  "default — the three no-default scalars plus the five "
                  "aggregates, which are always required because none of them "
                  "has an honest default (§12.2)",
                  n == 8, det("%zu required entries", n));
        }

        {
            // Parse the required list textually: it is the tail of the
            // document and its members are quoted names.
            const std::size_t at = schema.rfind("\"required\": [");
            const std::string tail =
                at == std::string::npos ? std::string() : schema.substr(at);
            const bool has_req =
                tail.find("\"frame_num\"") != std::string::npos &&
                tail.find("\"int_first_ts\"") != std::string::npos &&
                tail.find("\"int_last_ts\"") != std::string::npos &&
                tail.find("\"entry_points\"") != std::string::npos &&
                tail.find("\"port_ff_log\"") != std::string::npos;
            const bool no_opt = tail.find("\"bank\"") == std::string::npos &&
                                tail.find("\"enabled\"") == std::string::npos &&
                                tail.find("\"mode\"") == std::string::npos;
            check("JNSS-04",
                  "…and the required list is exactly that set: the five "
                  "no-default declarations are in it and the defaulted ones "
                  "are not",
                  has_req && no_opt, det("tail='%s'", tail.substr(0, 200).c_str()));
        }

        check("JNSS-05",
              "a defaulted field carries `default`, and it is the value the "
              "DECLARATION gives — not the value reset() would leave (§12.2)",
              schema.find("\"default\": 0") != std::string::npos &&
                  schema.find("\"default\": false") != std::string::npos &&
                  schema.find("\"default\": \"idle\"") != std::string::npos,
              "");

        check("JNSS-06",
              "unsigned scalars carry their DECLARED width as minimum and "
              "maximum, so a register out of range fails validation (§5.3)",
              schema.find("\"maximum\": 255") != std::string::npos &&
                  schema.find("\"maximum\": 65535") != std::string::npos &&
                  schema.find("\"maximum\": 4294967295") != std::string::npos,
              "");

        check("JNSS-07",
              "an i32 carries the signed 32-bit range, including a negative "
              "minimum (§6.2)",
              schema.find("\"minimum\": -2147483648") != std::string::npos &&
                  schema.find("\"maximum\": 2147483647") != std::string::npos,
              "");

        check("JNSS-08",
              "a u64 is a STRING with a canonical decimal pattern, never an "
              "integer — the schema is where §7.4's 2^53 rule is enforced "
              "against a reader that is not ours",
              schema.find("\"pattern\": \"^(0|[1-9][0-9]*)$\"") !=
                  std::string::npos,
              "");

        check("JNSS-09",
              "an i64 allows a leading minus and an i64_open additionally "
              "allows the enum value \"open\" (§6.2)",
              schema.find("\"pattern\": \"^(0|-?[1-9][0-9]*)$\"") !=
                  std::string::npos &&
                  schema.find("\"open\"") != std::string::npos &&
                  schema.find("\"anyOf\"") != std::string::npos,
              "");

        check("JNSS-10",
              "a fixed array's EXACT length is a LITERAL in the pattern, so a "
              "descriptor that silently resized the buffer fails validation "
              "rather than re-describing itself (§5.3)",
              schema.find("\"pattern\": \"^[0-9a-f]{8}$\"") !=
                  std::string::npos,
              "");

        check("JNSS-11",
              "an enum is a closed `enum` of NAMES — the property that turns "
              "an FSM renumbering into a visible diff (§6.2)",
              schema.find("\"idle\"") != std::string::npos &&
                  schema.find("\"wait_for_vpos\"") != std::string::npos &&
                  schema.find("\"stop\"") != std::string::npos,
              "");

        check("JNSS-12",
              "a log is an array whose maxItems is the BINARY capacity, with "
              "line/value items bounded by their own widths (§6.2)",
              schema.find("\"maxItems\": 8") != std::string::npos, "");

        check("JNSS-13",
              "the two FIFOs carry their own capacities and their own element "
              "widths — u8 for TX, u16 for RX",
              schema.find("\"maxItems\": 6") != std::string::npos &&
                  schema.find("\"maxItems\": 5") != std::string::npos,
              "");

        check("JNSS-14",
              "a blob contributes NO property — its bytes are a ZIP member "
              "and the manifest declares them — but its name is reported so "
              "the generator can state that declaration (§5.3, §6.1)",
              schema.find("mem/pilot-priv.bin") == std::string::npos &&
                  sd.blob_keys().size() == 1 &&
                  sd.blob_keys()[0] == "mem/pilot-priv.bin",
              det("%zu blob keys", sd.blob_keys().size()));

        check("JNSS-15",
              "a ram_window is a const reference object: the member, the page "
              "and the length are all pinned, so a file cannot re-point the "
              "alias (§6.1 case 2)",
              schema.find("\"const\": \"mem/ram.bin\"") != std::string::npos &&
                  schema.find("\"const\": 16") != std::string::npos &&
                  schema.find("\"const\": \"64\"") != std::string::npos,
              "");

        check("JNSS-16",
              "every state object is additionalProperties:false — STRICTER "
              "than §12.2's reader rule, deliberately: the validator's subject "
              "is a file jnext WROTE, where an unexpected key is a writer "
              "defect and not a newer sibling's field",
              schema.find("\"additionalProperties\": false") !=
                  std::string::npos,
              "");

        // §16.3 — determinism is a requirement on the generator, not a hope.
        {
            SchemaDesc again;
            p.s.describe_state(again);
            SchemaDesc reordered;
            p.s.describe_state_swapped(reordered);
            check("JNSS-17",
                  "the schema is deterministic and ORDER-INDEPENDENT: two "
                  "runs are byte-identical, and a reordered declaration emits "
                  "the same bytes, because the keys and the required list are "
                  "sorted rather than emitted in declaration order (§16.3)",
                  again.str() == schema && reordered.str() == schema,
                  det("same=%d reordered-same=%d", (int)(again.str() == schema),
                      (int)(reordered.str() == schema)));
        }

        check("JNSS-18",
              "and carries no timestamp, no path, no build id and no jnext "
              "version — the failure docs-check had when mkdocs stamped "
              "sitemap.xml.gz with the build date and the gate went red on an "
              "unchanged tree (§16.3)",
              !std::regex_search(schema, std::regex("20[0-9][0-9]-[0-9][0-9]-")) &&
                  schema.find("/home/") == std::string::npos &&
                  schema.find("JNEXT_VERSION") == std::string::npos &&
                  schema.find(".git") == std::string::npos,
              "");

        // A default that names an ordinal outside its own enum is a broken
        // DECLARATION, and the generator must not emit a schema for it.
        {
            struct Broken {
                uint8_t mode = 0;
                void describe(StateDesc& d) {
                    d.enum8("mode", mode, s2::kModes, /*default=*/9);
                }
            } b;
            SchemaDesc bs;
            b.describe(bs);
            check("JNSS-19",
                  "a declared default outside its own enum's name set fails "
                  "LOUDLY at generation, naming the field — the generator "
                  "refuses rather than emitting a schema nothing can satisfy",
                  bs.failed() && bs.failure() &&
                      std::string(bs.failure()) == "mode",
                  det("failed=%d name='%s'", (int)bs.failed(),
                      bs.failure() ? bs.failure() : ""));
        }

        // The generated shape has to be something a validator accepts. We do
        // not have a JSON Schema validator in C++ — that check is Python's,
        // in `make schema-check` — so what is asserted here is the one
        // structural property we CAN assert: it parses as JSON.
        check("JNSS-20",
              "the emitted schema is itself well-formed JSON (whether it is a "
              "VALID JSON Schema is checked by an implementation that is not "
              "ours, in make schema-check — this row does not claim that)",
              !schema.empty() && schema.front() == '{' &&
                  schema.compare(schema.size() - 2, 2, "}\n") == 0,
              "");

        // ── Found by mutation: three JNSS rows were not specific enough ──
        //
        // JNSS-06 checked that `"maximum": 255` appears SOMEWHERE — and a
        // log's `value` item and a u8 FIFO's element both emit one, so a u8
        // scalar losing its bound left the string in place. The same shape of
        // hole hid an open `additionalProperties` at the top level (the nested
        // objects still emit `false`) and a FIFO element width that ignored
        // its declaration. Anchor each to its own key.
        {
            const std::size_t at = schema.find("\"bank\": {");
            const std::string frag =
                at == std::string::npos ? std::string()
                                        : schema.substr(at, 200);
            check("JNSS-22",
                  "the u8 scalar `bank` carries ITS OWN 0..255 bound, not one "
                  "that happens to appear elsewhere in the document",
                  frag.find("\"maximum\": 255") != std::string::npos &&
                      frag.find("\"minimum\": 0") != std::string::npos,
                  det("frag='%s'", frag.substr(0, 120).c_str()));
        }
        {
            const std::size_t tx = schema.find("\"tx_fifo\": {");
            const std::size_t rx = schema.find("\"rx_fifo\": {");
            const std::string ftx =
                tx == std::string::npos ? std::string() : schema.substr(tx, 300);
            const std::string frx =
                rx == std::string::npos ? std::string() : schema.substr(rx, 300);
            check("JNSS-23",
                  "each FIFO's ITEM width comes from its own declaration: TX "
                  "items are 0..255 and RX items 0..65535, because uart.vhd:359 "
                  "carries a 9th (overflow OR framing) bit per received byte",
                  ftx.find("\"maximum\": 255") != std::string::npos &&
                      frx.find("\"maximum\": 65535") != std::string::npos,
                  det("tx='%s'", ftx.substr(0, 100).c_str()));
        }
        {
            // The top-level one is the LAST `additionalProperties` before the
            // trailing `required` list, since the object's own keys sort after
            // `properties`.
            static const char kApFalse[] = "\"additionalProperties\": false";
            const std::size_t top = schema.find("\"additionalProperties\"");
            check("JNSS-24",
                  "the TOP-LEVEL object is additionalProperties:false — the "
                  "nested reference and log-entry objects emit one too, so a "
                  "row that only looked for the string would miss the top "
                  "level opening up (found by mutation)",
                  top != std::string::npos && top < schema.find("\"properties\"") &&
                      schema.compare(top, std::strlen(kApFalse), kApFalse) == 0,
                  det("at %zu: '%s'", top,
                      top == std::string::npos
                          ? ""
                          : schema.substr(top, 34).c_str()));
        }

        // §12.2's gate in miniature: the DECLARED default must equal the
        // value the subsystem's own reset leaves. S3-S5 run this per field
        // per subsystem; here it is pinned against the pilot's construction,
        // so the mechanism exists before the first subsystem needs it.
        {
            s2::PilotState fresh;   // default-constructed == "post reset"
            check("JNSS-21",
                  "every DECLARED default equals the value a fresh object "
                  "carries — the §12.2 gate against the second copy of every "
                  "power-on value, which is the shape of the --help defect "
                  "(GH #246) waiting to happen",
                  fresh.bank == 0x00 && fresh.enabled == false &&
                      fresh.current_line == 0 && fresh.monotonic == 0 &&
                      fresh.flash_counter == 0 && fresh.mode == 0,
                  "");
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // JNSG — the §17.1 byte-identity gate's scaffolding
    // ─────────────────────────────────────────────────────────────────────
    //
    // S2 migrates no subsystem, so there is no real stream to compare. What
    // S2 owes S3-S5 is the ORACLE: a tested extractor for the pre-migration
    // golden, and the sentinel encoding the framing rests on.
    //
    // `snapshot_test --extract-golden IN OUT` is that extractor, so S3 runs
    // the code these rows cover rather than a shell one-liner nothing tests.
    // Verified by hand on the real cache, 2026-09-24:
    // ~/.jnext/warm-start/warm-start-m0.jwss -> 2 292 965 bytes, which is
    // exactly the length §17.1 states, and in which all 33 of
    // Emulator::save_state's sentinels appear EXACTLY ONCE, in ordinal order,
    // with the last ending at byte 2 292 965 and zero bytes left over.
    {
        // A synthetic JNEXTWS2: 96-byte plain header, deflated payload.
        const std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        std::vector<uint8_t> ws2 = s2::make_warm_start("JNEXTWS2", payload,
                                                       payload.size(), true);
        std::vector<uint8_t> got;
        std::string gwhy;
        check("JNSG-01",
              "the §17.1 extractor reads a JNEXTWS2 cache: 96-byte PLAIN "
              "header, then a DEFLATE payload, with plain_bytes at offset 16 "
              "as a u64 LE",
              s2::extract_warm_start(ws2, got, gwhy) && got == payload,
              det("why='%s' %zu bytes", gwhy.c_str(), got.size()));

        // §17.1's second note: a JNEXTWS1 file has the payload UNCOMPRESSED,
        // so an extractor that assumes deflate corrupts it silently.
        std::vector<uint8_t> ws1 = s2::make_warm_start("JNEXTWS1", payload,
                                                       payload.size(), false);
        got.clear();
        check("JNSG-02",
              "…and a JNEXTWS1 cache, whose payload is PLAIN — check the "
              "magic rather than assuming, or a pre-compression file is "
              "inflated as though it were compressed (§17.1)",
              s2::extract_warm_start(ws1, got, gwhy) && got == payload,
              det("why='%s'", gwhy.c_str()));

        // The header's `plain_bytes` STATES the expected length; §17.1 says to
        // check it, and a golden that silently came out short would make every
        // later `cmp` compare the wrong thing.
        std::vector<uint8_t> lying = s2::make_warm_start(
            "JNEXTWS2", payload, payload.size() + 1, true);
        got.clear();
        check("JNSG-03",
              "a header whose plain_bytes disagrees with the payload is "
              "REFUSED: a short golden would make every later cmp compare the "
              "wrong thing and report success (§17.1)",
              !s2::extract_warm_start(lying, got, gwhy) &&
                  gwhy.find("10") != std::string::npos &&
                  gwhy.find("11") != std::string::npos && got.empty(),
              det("why='%s' got=%zu", gwhy.c_str(), got.size()));

        std::vector<uint8_t> wrong_magic =
            s2::make_warm_start("JNEXTWSX", payload, payload.size(), true);
        got.clear();
        check("JNSG-04",
              "an unrecognised magic is refused rather than guessed at",
              !s2::extract_warm_start(wrong_magic, got, gwhy) && !gwhy.empty(),
              det("why='%s'", gwhy.c_str()));

        std::vector<uint8_t> stub(40, 0);
        got.clear();
        check("JNSG-05",
              "a file shorter than the 96-byte header is refused before "
              "anything is read from it",
              !s2::extract_warm_start(stub, got, gwhy) && !gwhy.empty(),
              det("why='%s'", gwhy.c_str()));

        // The sentinel encoding the 33-block framing rests on. Asserted as
        // bytes, because the golden is compared as bytes: `magic ^ ordinal`,
        // u32, host order (which is what StateWriter::write_u32 emits, and
        // deliberately so — see state_desc_bin.h on delegation).
        {
            StateWriter measure;
            BinWriteDesc md(measure);
            md.sentinel("clock", 0x4A4E5354, 0);
            md.sentinel("ram", 0x4A4E5354, 1);
            std::vector<uint8_t> out(measure.position());
            StateWriter w(out.data(), out.size());
            BinWriteDesc bd(w);
            bd.sentinel("clock", 0x4A4E5354, 0);
            bd.sentinel("ram", 0x4A4E5354, 1);

            StateWriter m2;
            m2.write_u32(0x4A4E5354u ^ 0u);
            m2.write_u32(0x4A4E5354u ^ 1u);
            std::vector<uint8_t> want(m2.position());
            StateWriter w2(want.data(), want.size());
            w2.write_u32(0x4A4E5354u ^ 0u);
            w2.write_u32(0x4A4E5354u ^ 1u);

            check("JNSG-06",
                  "BinWriteDesc emits a sentinel as `magic ^ ordinal` written "
                  "through StateWriter::write_u32 — byte-identical to "
                  "Emulator::save_state's put_sentinel lambda, which is what "
                  "makes the golden's 33-block framing survive the migration",
                  out == want && out.size() == 8,
                  det("%zu bytes", out.size()));
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // S6 — media identity: ROM digests (P3), the tape (P4), the preview (P5)
    // ─────────────────────────────────────────────────────────────────────
    //
    // All three are things the `.jns` SHOULD carry and did not. They live in
    // the manifest rather than the state stream for the same reason the SD
    // image does (§11): they are external resources, recorded by reopenable
    // identity and never copied — a .jns that embedded 64 KB of ROM would be
    // a firmware redistribution on every save (N3).
    {
        std::printf("\n--- S6: media identity (ROMs, tape, preview) ---\n");

        auto with_media = []() {
            Manifest m = make_manifest();
            m.roms.source = "sdcard";
            m.roms.sha256["48.rom"]  = std::string(64, 'c');
            m.roms.sha256["128.rom"] = std::string(64, 'd');
            m.roms.boot_rom_sha256   = std::string(64, 'e');
            m.tape.present          = true;
            m.tape.path             = "/home/user/game.tzx";
            m.tape.sha256           = std::string(64, 'f');
            m.tape.position_tstates = 123456789ull;
            m.tape.realtime         = true;
            m.esxdos_root           = "/home/user/nextdev";
            return m;
        };
        auto env_matching = [&]() {
            ReaderEnv e = make_env();
            e.roms.sha256["48.rom"]  = std::string(64, 'c');
            e.roms.sha256["128.rom"] = std::string(64, 'd');
            e.roms.boot_rom_sha256   = std::string(64, 'e');
            e.tape_file_available    = true;
            return e;
        };

        // ── Round trip through the manifest text, not through the struct ──
        {
            const Manifest m = with_media();
            Manifest got;
            std::vector<std::string> unknown;
            const bool ok = manifest_from_json(manifest_to_json(m), got, unknown,
                                               why);
            check("S6-MEDIA-01",
                  "media.roms, media.tape and media.esxdos_root survive a "
                  "round trip through the manifest TEXT — the digests, the "
                  "tape's T-state position and its realtime flag included, "
                  "because a position without the flag describes a different "
                  "machine",
                  ok && got.roms.source == "sdcard" &&
                      got.roms.sha256.size() == 2 &&
                      got.roms.sha256["48.rom"] == std::string(64, 'c') &&
                      got.roms.boot_rom_sha256 == std::string(64, 'e') &&
                      got.tape.present && got.tape.path == m.tape.path &&
                      got.tape.position_tstates == 123456789ull &&
                      got.tape.realtime &&
                      got.esxdos_root == "/home/user/nextdev" &&
                      unknown.empty(),
                  why);
        }

        // ── P3: a differing ROM WARNS and restores, naming the ROM ────────
        {
            Manifest m = with_media();
            std::vector<uint8_t> z = build_raw(m, {}, why);
            ReaderEnv e = env_matching();
            e.roms.sha256["48.rom"] = std::string(64, '9');   // a different ROM
            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
            bool named = false;
            for (const std::string& w2 : v.warnings)
                if (w2.find("48.rom") != std::string::npos) named = true;
            check("S6-ROMS-01",
                  "a snapshot taken against different ROM content WARNS and "
                  "restores, naming the ROM: for 48k/128k/plus3 the ROM lives "
                  "in `Rom rom_` and is not serialised, so without this the "
                  "machine silently runs different code — but a corrected or "
                  "regionalised ROM is a thing people legitimately have",
                  v.ok && named, v.warnings.empty() ? "no warning" : v.warnings[0]);
        }

        // ── …and REFUSES under --snapshot-strict ──────────────────────────
        {
            Manifest m = with_media();
            std::vector<uint8_t> z = build_raw(m, {}, why);
            ReaderEnv e = env_matching();
            e.roms.sha256["128.rom"] = std::string(64, '9');
            e.strict = true;
            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
            check("S6-ROMS-02",
                  "…and --snapshot-strict turns that warning into a refusal "
                  "that still names the ROM",
                  refused_naming("S6-ROMS-02", v, "128.rom"), v.refusal);
        }

        // ── A ROM only ONE side has is not a mismatch ─────────────────────
        {
            Manifest m = with_media();
            m.roms.sha256["plus3.rom"] = std::string(64, '1');
            std::vector<uint8_t> z = build_raw(m, {}, why);
            ReaderEnv e = env_matching();          // has no plus3.rom at all
            e.strict = true;                       // even at the strictest
            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
            check("S6-ROMS-03",
                  "a ROM name only one side has is NOT a mismatch, even under "
                  "--snapshot-strict: it is a ROM this machine does not use, "
                  "and calling that a mismatch is the cries-wolf failure "
                  "§11.1 rejects for the SD card — an identity that is "
                  "ignored is worse than none",
                  v.ok && v.warnings.empty(),
                  v.warnings.empty() ? v.refusal : v.warnings[0]);
        }

        // ── The boot ROM is compared SEPARATELY ───────────────────────────
        {
            Manifest m = with_media();
            std::vector<uint8_t> z = build_raw(m, {}, why);
            ReaderEnv e = env_matching();
            e.roms.boot_rom_sha256 = std::string(64, '9');
            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
            bool named = false;
            for (const std::string& w2 : v.warnings)
                if (w2.find("nextboot.rom") != std::string::npos) named = true;
            check("S6-ROMS-04",
                  "the FPGA boot ROM is compared separately and named "
                  "separately: it is baked into the binary rather than read "
                  "from the card, so a mismatch means the two jnext builds "
                  "disagree about silicon, not about a file",
                  v.ok && named,
                  v.warnings.empty() ? "no warning" : v.warnings[0]);
        }

        // ── P4: an absent tape WARNS, naming it, and restores ─────────────
        {
            Manifest m = with_media();
            std::vector<uint8_t> z = build_raw(m, {}, why);
            ReaderEnv e = env_matching();
            e.tape_file_available = false;
            e.strict = true;                       // NOT a refusal even here
            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
            bool named = false;
            for (const std::string& w2 : v.warnings)
                if (w2.find("game.tzx") != std::string::npos) named = true;
            check("S6-TAPE-01",
                  "a snapshot whose tape cannot be reopened WARNS, names the "
                  "file and restores WITHOUT it — and is not a refusal even "
                  "under --snapshot-strict, because a machine whose tape has "
                  "finished loading is a perfectly good machine and refusing "
                  "it over a moved .tzx would be the format getting in the "
                  "way",
                  v.ok && named,
                  v.warnings.empty() ? v.refusal : v.warnings[0]);
        }

        // ── …and no tape recorded means no warning, ever ──────────────────
        {
            Manifest m = with_media();
            m.tape = jnext::jns::TapeInfo{};        // no tape was attached
            std::vector<uint8_t> z = build_raw(m, {}, why);
            ReaderEnv e = env_matching();
            e.tape_file_available = false;
            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            jnext::jns::open_snapshot(z.data(), z.size(), e, r, got, v);
            check("S6-TAPE-02",
                  "a snapshot taken with NO tape attached warns about none: "
                  "the member's absence is how a reader tells 'no tape' from "
                  "'a tape it cannot find', which is the same distinction "
                  "`subsystems` draws for state members (§8)",
                  v.ok && v.warnings.empty() && !got.tape.present,
                  v.warnings.empty() ? "" : v.warnings[0]);
        }

        // ── P5: the preview travels as a declared meta member ─────────────
        {
            // A 1-pixel PNG is not needed: the row is about the DECLARATION
            // and the member, and inventing a decoder here would test libpng.
            const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A,
                                              0x1A, 0x0A, 0x00, 0x01};
            SnapshotWriter w(false);
            Manifest m = make_manifest();
            m.preview.present = true;
            m.preview.width   = 320;
            m.preview.height  = 256;
            w.set_manifest(m);
            w.add_subsystem("cpu", kJson, why);
            w.add_meta("meta/preview.png", png.data(), png.size(), why);
            std::vector<uint8_t> z;
            const bool built = w.finish(z, why);

            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            const bool opened =
                built && jnext::jns::open_snapshot(z.data(), z.size(),
                                                   make_env(), r, got, v);
            std::vector<uint8_t> back;
            std::string rwhy;
            const bool read_back =
                opened && r.read("meta/preview.png", back, rwhy);
            check("S6-PREVIEW-01",
                  "`meta/preview.png` is written, DECLARED with its "
                  "dimensions and read back: the framebuffer is regenerated "
                  "by the next render, so a snapshot restored PAUSED shows "
                  "the previous frame until the user steps, and this is the "
                  "restore-time paused image (§10.2 P5)",
                  read_back && back == png && got.preview.present &&
                      got.preview.width == 320 && got.preview.height == 256,
                  why + rwhy);
        }

        // ── …and a reader that does not know it just ignores it ───────────
        {
            const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
            SnapshotWriter w(false);
            w.set_manifest(make_manifest());        // NO preview declaration
            w.add_subsystem("cpu", kJson, why);
            w.add_meta("meta/preview.png", png.data(), png.size(), why);
            std::vector<uint8_t> z;
            const bool built = w.finish(z, why);
            jnext::zip::Reader r;
            Manifest got;
            Verdict v;
            const bool opened =
                built && jnext::jns::open_snapshot(z.data(), z.size(),
                                                   make_env(), r, got, v);
            std::vector<uint8_t> back;
            std::string rwhy;
            const bool read_back =
                opened && r.read("meta/preview.png", back, rwhy);
            check("S6-PREVIEW-02",
                  "a preview MEMBER with no declaration is accepted and "
                  "readable, and the reader does NOT invent the declaration "
                  "from it: `meta/` is an open namespace whose members are "
                  "carried, not interpreted (JNSR-15), so `preview` stays "
                  "absent and a consumer can tell 'no preview was declared' "
                  "from 'a preview 320x256 is there'",
                  opened && v.ok && read_back && back == png &&
                      !got.preview.present && v.ignored_members.empty(),
                  v.refusal + rwhy);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // S6 — §12.2's declared-default gate, and the proof it can FAIL
    // ─────────────────────────────────────────────────────────────────────
    //
    // S6 is the first stage to declare a default on a shipped field (the SD
    // card's, `sd_card.cpp`), and §12.2 requires the second copy of a
    // power-on value to be COMPARED against the first or it silently keeps a
    // pre-audit value — the shape of GH #246. `DefaultCheckDesc` is that
    // comparison, and the rows that matter are the ones proving it is not a
    // tautology: a gate nobody has watched fail is a gate nobody can trust.
    {
        std::printf("\n--- S6: §12.2 declared defaults, and the gate ---\n");

        // The pilot's declared defaults ARE its power-on values, so a pilot
        // in the state a `reset()` would leave must produce no mismatch.
        {
            s2::PilotState p;
            uint8_t ram[s2::PilotState::kRamBytes]{};
            p.window = ram;
            jnext::save::DefaultCheckDesc d;
            p.describe_state(d);
            check("S6-DEF-01",
                  "a subsystem whose fields hold their declared defaults "
                  "produces no mismatch, and the walk actually visited them "
                  "(6 defaulted scalars, 3 required)",
                  d.mismatches().empty() && d.defaulted() == 6 &&
                      d.undefaulted() == 3,
                  det("defaulted=%zu undefaulted=%zu mismatches=%zu",
                      d.defaulted(), d.undefaulted(), d.mismatches().size()));
        }

        // THE ONE THAT MATTERS. `bank` is declared `0x00`; give the
        // "post-reset" object 0x0A — the exact drift §12.2 describes, a VHDL
        // audit correcting `reset()` and leaving the declaration behind — and
        // require the gate to name the field, both values included.
        {
            s2::PilotState p;
            uint8_t ram[s2::PilotState::kRamBytes]{};
            p.window = ram;
            p.bank = 0x0A;
            jnext::save::DefaultCheckDesc d;
            p.describe_state(d);
            const bool named = d.mismatches().size() == 1 &&
                               d.mismatches()[0].field == "bank" &&
                               d.mismatches()[0].declared == "0" &&
                               d.mismatches()[0].actual == "10";
            check("S6-DEF-02",
                  "the gate FAILS when a declared default and the value "
                  "reset() leaves disagree, and NAMES the field with both "
                  "numbers — G9 is a testable property, not a slogan",
                  named,
                  det("%zu mismatch(es)%s", d.mismatches().size(),
                      d.mismatches().empty()
                          ? ""
                          : (" first=" + d.mismatches()[0].field).c_str()));
        }

        // …in every primitive that carries one, not just `u8`. A gate that
        // only looked at one type would pass S6-DEF-02 and miss the other
        // five kinds of drift.
        {
            s2::PilotState p;
            uint8_t ram[s2::PilotState::kRamBytes]{};
            p.window = ram;
            p.enabled       = true;   // declared false
            p.current_line  = 7;      // declared 0
            p.monotonic     = 9;      // declared 0
            p.flash_counter = -3;     // declared 0
            p.mode          = 1;      // declared ordinal 0
            jnext::save::DefaultCheckDesc d;
            p.describe_state(d);
            std::set<std::string> got;
            for (const auto& m : d.mismatches()) got.insert(m.field);
            const std::set<std::string> want = {
                "enabled", "current_line", "monotonic", "flash_counter",
                "mode"};
            check("S6-DEF-03",
                  "the gate covers every scalar primitive that can carry a "
                  "default — bool, u16, u64, i32 and enum8 — not only the u8 "
                  "S6-DEF-02 drifts",
                  got == want, det("%zu mismatch(es)", got.size()));
        }

        // And the aggregates contribute NOTHING in either direction. §12.2's
        // three exemptions (NR 0x03 config mode, the NextReg machine type and
        // the Multiface RAM) are exempt because they declare no default, not
        // because a checker excludes them — a distinction this row makes
        // structural: `bytes`/`blob`/`ram_window`/`log`/`fifo` cannot declare
        // one, so they are neither gated nor silently counted as gated.
        {
            s2::PilotState p;
            uint8_t ram[s2::PilotState::kRamBytes]{};
            p.window = ram;
            std::memset(p.priv_ram, 0xEE, sizeof(p.priv_ram));
            std::memset(p.entry_points, 0xEE, sizeof(p.entry_points));
            std::memset(ram, 0xEE, sizeof(ram));
            p.log_count = 3;
            p.tx.push(0x11);
            jnext::save::DefaultCheckDesc d;
            p.describe_state(d);
            check("S6-DEF-04",
                  "aggregates carry no default by construction, so a "
                  "non-power-on blob, byte array, window, log or FIFO is "
                  "neither a mismatch nor counted as a gated field",
                  d.mismatches().empty() && d.defaulted() == 6 &&
                      d.undefaulted() == 3);
        }
    }

    // ── Emit the corpus for the external ZIP readers ─────────────────────
    if (!emit_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(emit_dir, ec);
        size_t written = 0;
        for (const CorpusFile& f : g_corpus) {
            const std::string p = emit_dir + "/" + f.name + "." + f.ext;
            std::ofstream os(p, std::ios::binary | std::ios::trunc);
            os.write(reinterpret_cast<const char*>(f.bytes.data()),
                     static_cast<std::streamsize>(f.bytes.size()));
            if (os.good()) ++written;
        }
        std::printf("corpus: %zu archives written to %s\n", written,
                    emit_dir.c_str());
        if (written != g_corpus.size() || g_corpus.empty()) {
            std::printf("corpus: FAILED to write %zu of %zu\n",
                        g_corpus.size() - written, g_corpus.size());
            return 2;
        }
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
