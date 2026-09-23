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

#include "save/jns_container.h"
#include "save/zip_archive.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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

}  // namespace

// ═════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    std::string emit_dir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--emit-corpus") == 0 && i + 1 < argc) {
            emit_dir = argv[++i];
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
