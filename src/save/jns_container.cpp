#include "save/jns_container.h"

// The ONE translation unit that includes the vendored JSON header (§14.1's
// mitigation: nlohmann is a heavy header, so it is confined to the snapshot
// module's parsing TU and nothing else in jnext includes it).
#include "third_party/nlohmann-json/nlohmann/json.hpp"

#include <zlib.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>

namespace jnext {
namespace jns {

using nlohmann::json;

namespace {

// NOT `quoted`: nlohmann pulls in <iomanip>, whose std::quoted is found by
// ADL on a std::string argument and wins the overload.
std::string quote(const std::string& s) { return "'" + s + "'"; }
std::string u64s(uint64_t v) { return std::to_string(v); }

std::string hex8(uint32_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(8, '0');
    for (int i = 7; i >= 0; --i) {
        s[static_cast<size_t>(i)] = d[v & 0xF];
        v >>= 4;
    }
    return s;
}

bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

/// `state/<name>.json` -> `<name>`; empty when the path is not of that shape.
std::string subsystem_of(const std::string& path) {
    const std::string pre = kStatePrefix;
    const std::string suf = ".json";
    if (!starts_with(path, pre)) return {};
    if (path.size() <= pre.size() + suf.size()) return {};
    if (path.compare(path.size() - suf.size(), suf.size(), suf) != 0) return {};
    return path.substr(pre.size(), path.size() - pre.size() - suf.size());
}

/// Record an unknown key, as a dotted path, for the caller to log (§12.2).
void note_unknown(std::vector<std::string>& out, const std::string& prefix,
                  const json& obj, const std::set<std::string>& known) {
    if (!obj.is_object()) return;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (known.count(it.key()) == 0) out.push_back(prefix + it.key());
    }
}

/// Read an unsigned integer key. `present` distinguishes "absent" from
/// "present but wrong", which are different failures and must not be
/// conflated in a message.
bool get_u32_key(const json& obj, const char* key, uint32_t& out, bool& present,
                 const std::string& where, std::string& why) {
    present = obj.is_object() && obj.contains(key);
    if (!present) return true;
    const json& v = obj.at(key);
    if (!v.is_number_integer() || v.is_number_float()) {
        why = where + "." + key + " is not an integer";
        return false;
    }
    const int64_t n = v.get<int64_t>();
    if (n < 0) {
        why = where + "." + key + " is negative (" + std::to_string(n) + ")";
        return false;
    }
    if (n > static_cast<int64_t>(UINT32_MAX)) {
        why = where + "." + key + " is " + std::to_string(n) +
              ", too large for a 32-bit field";
        return false;
    }
    out = static_cast<uint32_t>(n);
    return true;
}

bool get_u64_key(const json& obj, const char* key, uint64_t& out,
                 const std::string& where, std::string& why) {
    if (!obj.is_object() || !obj.contains(key)) return true;
    const json& v = obj.at(key);
    if (!v.is_number_integer() || v.is_number_float()) {
        why = where + "." + key + " is not an integer";
        return false;
    }
    const int64_t n = v.get<int64_t>();
    if (n < 0) {
        why = where + "." + key + " is negative (" + std::to_string(n) + ")";
        return false;
    }
    out = static_cast<uint64_t>(n);
    return true;
}

bool get_str_key(const json& obj, const char* key, std::string& out,
                 const std::string& where, std::string& why) {
    if (!obj.is_object() || !obj.contains(key)) return true;
    const json& v = obj.at(key);
    if (!v.is_string()) {
        why = where + "." + key + " is not a string";
        return false;
    }
    out = v.get<std::string>();
    return true;
}

bool get_bool_key(const json& obj, const char* key, bool& out, bool& present,
                  const std::string& where, std::string& why) {
    present = obj.is_object() && obj.contains(key);
    if (!present) return true;
    const json& v = obj.at(key);
    if (!v.is_boolean()) {
        why = where + "." + key + " is not a boolean";
        return false;
    }
    out = v.get<bool>();
    return true;
}

}  // namespace

std::string SdIdentity::describe() const {
    return "size=" + u64s(image_bytes) + " vol-id=" +
           (fat32_volume_id.empty() ? std::string("(none)") : fat32_volume_id) +
           " mbr=" +
           (mbr_sha256.empty() ? std::string("(none)")
                               : mbr_sha256.substr(0, 16)) +
           " lba=" + u64s(partition_lba);
}

// ─────────────────────────────────────────────────────────────────────────
// Manifest serialisation
// ─────────────────────────────────────────────────────────────────────────

std::string manifest_to_json(const Manifest& m) {
    json j;
    j["format_version"] = m.format_version;
    if (!m.created.empty()) j["created"] = m.created;

    json prod = json::object();
    prod["jnext_version"] = m.producer.jnext_version;
    // Omitted rather than emitted empty when the build has no git-describe
    // mechanism: an empty string would read as "describes as nothing", which
    // is a different and wrong claim from "not recorded".
    if (!m.producer.git_describe.empty())
        prod["git_describe"] = m.producer.git_describe;
    prod["platform"] = m.producer.platform;
    j["producer"] = prod;

    json model = json::object();
    model["state_model_revision"] = m.model.state_model_revision;
    model["machine"]              = m.model.machine;
    model["ram_kb"]               = m.model.ram_kb;
    if (!m.model.timing.empty()) model["timing"] = m.model.timing;
    model["cpu_speed_nr07"] = m.model.cpu_speed_nr07;
    j["model"] = model;

    json cap = json::object();
    cap["frame"]          = m.capture.frame;
    cap["frame_boundary"] = m.capture.frame_boundary;
    j["capture"] = cap;

    json media = json::object();
    if (m.sdcard.present) {
        json sd = json::object();
        sd["mounted_path"] = m.sdcard.mounted_path;
        sd["read_only"]    = m.sdcard.read_only;

        json id = json::object();
        id["image_bytes"]     = m.sdcard.identity.image_bytes;
        id["mbr_sha256"]      = m.sdcard.identity.mbr_sha256;
        id["fat32_volume_id"] = m.sdcard.identity.fat32_volume_id;
        id["partition_lba"]   = m.sdcard.identity.partition_lba;
        sd["identity"] = id;

        // Carried, never compared (§11.3).
        json info = json::object();
        info["fat32_bs_vollab"] = m.sdcard.vollab;
        sd["informational"] = info;

        json stamp = json::object();
        stamp["sha256"]    = m.sdcard.content_sha256;
        stamp["mtime_utc"] = m.sdcard.content_mtime_utc;
        sd["content_stamp"] = stamp;

        media["sdcard"] = sd;
    }

    // §10.2 P3 — ROM identity. Omitted entirely rather than emitted empty
    // when the writer recorded none: an empty object reads as "this machine
    // had no ROMs", which is a different and wrong claim from "not recorded".
    if (m.roms.populated() || !m.roms.source.empty()) {
        json roms = json::object();
        if (!m.roms.source.empty()) roms["source"] = m.roms.source;
        if (!m.roms.sha256.empty()) {
            json d = json::object();
            for (const auto& kv : m.roms.sha256) d[kv.first] = kv.second;
            roms["sha256"] = d;
        }
        media["roms"] = roms;
        if (!m.roms.boot_rom_sha256.empty())
            media["boot_rom_sha256"] = m.roms.boot_rom_sha256;
    }

    // §10.2 P4 — tape identity, present only when a tape was attached. Its
    // absence is how a reader tells "no tape" from "a tape it cannot find".
    if (m.tape.present) {
        json tape = json::object();
        tape["path"]             = m.tape.path;
        tape["sha256"]           = m.tape.sha256;
        tape["position_tstates"] = m.tape.position_tstates;
        tape["realtime"]         = m.tape.realtime;
        media["tape"] = tape;
    }

    if (!m.esxdos_root.empty()) media["esxdos_root"] = m.esxdos_root;

    j["media"] = media;

    // §10.2 P5 — the preview image's declaration. The BYTES are the ZIP
    // member `meta/preview.png`; this says it is there and how big the
    // picture is, so a reader can size it without inflating and a gallery
    // can list it without a decode.
    if (m.preview.present) {
        json pv = json::object();
        pv["path"]   = std::string(kMetaPrefix) + "preview.png";
        pv["width"]  = m.preview.width;
        pv["height"] = m.preview.height;
        j["preview"] = pv;
    }

    json members = json::object();
    for (const auto& kv : m.members) {
        json d = json::object();
        d["bytes"] = kv.second.bytes;
        d["crc32"] = hex8(kv.second.crc32);
        members[kv.first] = d;
    }
    j["members"] = members;

    j["subsystems"] = m.subsystems;

    // Two-space indent, and `\n` endings. A manifest is meant to be read with
    // `unzip -p snap.jns manifest.json | jq` or a text editor (G5), and it is
    // a few kilobytes — the whitespace costs nothing after deflate.
    return j.dump(2) + "\n";
}

namespace {

/// The parse proper. EVERY nlohmann accessor below is reached only behind a
/// `contains()` / `is_*()` guard, because nlohmann signals a surprise by
/// THROWING and an unguarded `at()` or `get<T>()` would terminate jnext rather
/// than refuse the file. The wrapper below is the backstop for the guard
/// nobody remembered — see it for why that backstop is not optional here.
bool manifest_parse(const std::string& text, Manifest& out,
                    std::vector<std::string>& unknown_keys,
                    std::string& why) {
    out = Manifest{};
    unknown_keys.clear();

    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        why = std::string("manifest.json is not valid JSON: ") + e.what();
        return false;
    }
    if (!j.is_object()) {
        why = "manifest.json is not a JSON object";
        return false;
    }

    // format_version — the one key whose absence is itself a refusal (§7.3).
    if (!j.contains("format_version")) {
        why = "manifest.json has no format_version key";
        return false;
    }
    {
        const json& v = j.at("format_version");
        if (!v.is_number_integer() || v.is_number_float()) {
            why = "manifest.json format_version is not an integer";
            return false;
        }
        const int64_t n = v.get<int64_t>();
        if (n < 0 || n > static_cast<int64_t>(UINT32_MAX)) {
            why = "manifest.json format_version " + std::to_string(n) +
                  " is out of range";
            return false;
        }
        out.format_version = static_cast<uint32_t>(n);
    }

    if (!get_str_key(j, "created", out.created, "manifest", why)) return false;

    note_unknown(unknown_keys, "", j,
                 {"format_version", "created", "producer", "model", "capture",
                  "media", "members", "subsystems", "preview"});

    if (j.contains("producer")) {
        const json& p = j.at("producer");
        if (!p.is_object()) {
            why = "manifest.producer is not an object";
            return false;
        }
        if (!get_str_key(p, "jnext_version", out.producer.jnext_version,
                         "manifest.producer", why) ||
            !get_str_key(p, "git_describe", out.producer.git_describe,
                         "manifest.producer", why) ||
            !get_str_key(p, "platform", out.producer.platform,
                         "manifest.producer", why)) {
            return false;
        }
        note_unknown(unknown_keys, "producer.", p,
                     {"jnext_version", "git_describe", "platform"});
    }

    if (!j.contains("model")) {
        why = "manifest.json has no model object";
        return false;
    }
    {
        const json& m = j.at("model");
        if (!m.is_object()) {
            why = "manifest.model is not an object";
            return false;
        }
        bool present = false;
        if (!get_u32_key(m, "state_model_revision", out.model.state_model_revision,
                         present, "manifest.model", why)) {
            return false;
        }
        if (!present) {
            why = "manifest.model has no state_model_revision key";
            return false;
        }
        if (!get_str_key(m, "machine", out.model.machine, "manifest.model", why))
            return false;
        if (out.model.machine.empty()) {
            why = "manifest.model has no machine key";
            return false;
        }
        if (!get_u32_key(m, "ram_kb", out.model.ram_kb, present,
                         "manifest.model", why)) {
            return false;
        }
        if (!present) {
            // §8: the reader refuses any value it cannot construct rather than
            // ASSUMING 2048, and an absent key is the same class of assumption.
            why = "manifest.model has no ram_kb key";
            return false;
        }
        if (!get_str_key(m, "timing", out.model.timing, "manifest.model", why))
            return false;
        if (!get_u32_key(m, "cpu_speed_nr07", out.model.cpu_speed_nr07, present,
                         "manifest.model", why)) {
            return false;
        }
        note_unknown(unknown_keys, "model.", m,
                     {"state_model_revision", "machine", "ram_kb", "timing",
                      "cpu_speed_nr07"});
    }

    if (!j.contains("capture")) {
        why = "manifest.json has no capture object";
        return false;
    }
    {
        const json& c = j.at("capture");
        if (!c.is_object()) {
            why = "manifest.capture is not an object";
            return false;
        }
        if (!get_u64_key(c, "frame", out.capture.frame, "manifest.capture", why))
            return false;
        bool present = false;
        if (!get_bool_key(c, "frame_boundary", out.capture.frame_boundary,
                          present, "manifest.capture", why)) {
            return false;
        }
        if (!present) {
            why = "manifest.capture has no frame_boundary key";
            return false;
        }
        note_unknown(unknown_keys, "capture.", c, {"frame", "frame_boundary"});
    }

    if (j.contains("media")) {
        const json& med = j.at("media");
        if (!med.is_object()) {
            why = "manifest.media is not an object";
            return false;
        }
        // Only `sdcard` is modelled in stage S1. `roms`, `tape` and
        // `esxdos_root` need digests of objects the container cannot reach
        // yet; they arrive with the stages that serialise those subsystems,
        // and until then they are ignored exactly as §12.2 says unknown keys
        // are.
        note_unknown(unknown_keys, "media.", med,
                     {"sdcard", "roms", "boot_rom_sha256", "tape",
                      "esxdos_root"});

        if (med.contains("sdcard")) {
            const json& sd = med.at("sdcard");
            if (!sd.is_object()) {
                why = "manifest.media.sdcard is not an object";
                return false;
            }
            out.sdcard.present = true;
            const std::string where = "manifest.media.sdcard";
            bool present = false;
            if (!get_str_key(sd, "mounted_path", out.sdcard.mounted_path, where,
                             why) ||
                !get_bool_key(sd, "read_only", out.sdcard.read_only, present,
                              where, why)) {
                return false;
            }
            note_unknown(unknown_keys, "media.sdcard.", sd,
                         {"mounted_path", "read_only", "identity",
                          "informational", "content_stamp"});

            if (sd.contains("identity")) {
                const json& id = sd.at("identity");
                if (!id.is_object()) {
                    why = where + ".identity is not an object";
                    return false;
                }
                if (!get_u64_key(id, "image_bytes", out.sdcard.identity.image_bytes,
                                 where + ".identity", why) ||
                    !get_str_key(id, "mbr_sha256", out.sdcard.identity.mbr_sha256,
                                 where + ".identity", why) ||
                    !get_str_key(id, "fat32_volume_id",
                                 out.sdcard.identity.fat32_volume_id,
                                 where + ".identity", why) ||
                    !get_u64_key(id, "partition_lba",
                                 out.sdcard.identity.partition_lba,
                                 where + ".identity", why)) {
                    return false;
                }
                note_unknown(unknown_keys, "media.sdcard.identity.", id,
                             {"image_bytes", "mbr_sha256", "fat32_volume_id",
                              "partition_lba"});
            }
            if (sd.contains("informational")) {
                const json& inf = sd.at("informational");
                if (!inf.is_object()) {
                    why = where + ".informational is not an object";
                    return false;
                }
                if (!get_str_key(inf, "fat32_bs_vollab", out.sdcard.vollab,
                                 where + ".informational", why)) {
                    return false;
                }
                note_unknown(unknown_keys, "media.sdcard.informational.", inf,
                             {"fat32_bs_vollab"});
            }
            if (sd.contains("content_stamp")) {
                const json& cs = sd.at("content_stamp");
                if (!cs.is_object()) {
                    why = where + ".content_stamp is not an object";
                    return false;
                }
                if (!get_str_key(cs, "sha256", out.sdcard.content_sha256,
                                 where + ".content_stamp", why) ||
                    !get_str_key(cs, "mtime_utc", out.sdcard.content_mtime_utc,
                                 where + ".content_stamp", why)) {
                    return false;
                }
                note_unknown(unknown_keys, "media.sdcard.content_stamp.", cs,
                             {"sha256", "mtime_utc"});
            }
        }

        // ── §10.2 P3 — ROM identity ─────────────────────────────────────
        if (med.contains("roms")) {
            const json& r = med.at("roms");
            if (!r.is_object()) {
                why = "manifest.media.roms is not an object";
                return false;
            }
            note_unknown(unknown_keys, "media.roms.", r, {"source", "sha256"});
            if (!get_str_key(r, "source", out.roms.source, "manifest.media.roms",
                             why)) {
                return false;
            }
            if (r.contains("sha256")) {
                const json& d = r.at("sha256");
                if (!d.is_object()) {
                    why = "manifest.media.roms.sha256 is not an object";
                    return false;
                }
                for (auto it = d.begin(); it != d.end(); ++it) {
                    if (!it.value().is_string()) {
                        why = "manifest.media.roms.sha256[" + quote(it.key()) +
                              "] is not a string";
                        return false;
                    }
                    out.roms.sha256[it.key()] = it.value().get<std::string>();
                }
            }
        }
        if (!get_str_key(med, "boot_rom_sha256", out.roms.boot_rom_sha256,
                         "manifest.media", why)) {
            return false;
        }

        // ── §10.2 P4 — tape identity ────────────────────────────────────
        if (med.contains("tape")) {
            const json& t = med.at("tape");
            if (!t.is_object()) {
                why = "manifest.media.tape is not an object";
                return false;
            }
            note_unknown(unknown_keys, "media.tape.", t,
                         {"path", "sha256", "position_tstates", "realtime"});
            out.tape.present = true;
            const std::string where = "manifest.media.tape";
            if (!get_str_key(t, "path", out.tape.path, where, why) ||
                !get_str_key(t, "sha256", out.tape.sha256, where, why) ||
                !get_u64_key(t, "position_tstates", out.tape.position_tstates,
                             where, why)) {
                return false;
            }
            bool present = false;
            if (!get_bool_key(t, "realtime", out.tape.realtime, present, where,
                              why)) {
                return false;
            }
        }

        if (!get_str_key(med, "esxdos_root", out.esxdos_root, "manifest.media",
                         why)) {
            return false;
        }
    }

    // ── §10.2 P5 — the preview declaration ──────────────────────────────
    if (j.contains("preview")) {
        const json& pv = j.at("preview");
        if (!pv.is_object()) {
            why = "manifest.preview is not an object";
            return false;
        }
        note_unknown(unknown_keys, "preview.", pv, {"path", "width", "height"});
        out.preview.present = true;
        bool present = false;
        const std::string where = "manifest.preview";
        if (!get_u32_key(pv, "width", out.preview.width, present, where, why) ||
            !get_u32_key(pv, "height", out.preview.height, present, where, why)) {
            return false;
        }
    }

    if (j.contains("members")) {
        const json& mem = j.at("members");
        if (!mem.is_object()) {
            why = "manifest.members is not an object";
            return false;
        }
        for (auto it = mem.begin(); it != mem.end(); ++it) {
            const std::string path = it.key();
            std::string path_why;
            if (!zip::valid_member_path(path, path_why)) {
                why = "manifest.members declares " + path_why;
                return false;
            }
            if (!it.value().is_object()) {
                why = "manifest.members[" + quote(path) + "] is not an object";
                return false;
            }
            BlobDecl d;
            const std::string where = "manifest.members[" + quote(path) + "]";
            if (!it.value().contains("bytes")) {
                why = where + " has no bytes key";
                return false;
            }
            if (!get_u64_key(it.value(), "bytes", d.bytes, where, why))
                return false;

            if (!it.value().contains("crc32")) {
                why = where + " has no crc32 key";
                return false;
            }
            std::string crc_text;
            if (!get_str_key(it.value(), "crc32", crc_text, where, why))
                return false;
            // Exactly eight lower-case hex digits. A shorter or upper-case
            // spelling is refused rather than normalised: the schema pins this
            // pattern, so accepting a second spelling would make jnext and an
            // external validator disagree about the same file.
            if (crc_text.size() != 8) {
                why = where + " crc32 " + quote(crc_text) +
                      " is not 8 hex digits";
                return false;
            }
            uint32_t crc = 0;
            for (char c : crc_text) {
                uint32_t nib;
                if (c >= '0' && c <= '9')      nib = static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') nib = static_cast<uint32_t>(c - 'a' + 10);
                else {
                    why = where + " crc32 " + quote(crc_text) +
                          " is not lower-case hex";
                    return false;
                }
                crc = (crc << 4) | nib;
            }
            d.crc32 = crc;
            out.members[path] = d;
        }
    }

    if (j.contains("subsystems")) {
        const json& s = j.at("subsystems");
        if (!s.is_array()) {
            why = "manifest.subsystems is not an array";
            return false;
        }
        for (const json& e : s) {
            if (!e.is_string()) {
                why = "manifest.subsystems contains a non-string entry";
                return false;
            }
            const std::string name = e.get<std::string>();
            if (name.empty()) {
                why = "manifest.subsystems contains an empty name";
                return false;
            }
            if (std::find(out.subsystems.begin(), out.subsystems.end(), name) !=
                out.subsystems.end()) {
                why = "manifest.subsystems lists " + quote(name) + " twice";
                return false;
            }
            out.subsystems.push_back(name);
        }
    }

    why.clear();
    return true;
}

}  // namespace

bool manifest_from_json(const std::string& text, Manifest& out,
                        std::vector<std::string>& unknown_keys,
                        std::string& why) {
    try {
        return manifest_parse(text, out, unknown_keys, why);
    } catch (const json::exception& e) {
        // The backstop, and it is NOT decorative. A `.jns` manifest is meant
        // to be opened with `jq` and a text editor (G5), so hand-mangled files
        // are an expected input, and the stages after this one add on the
        // order of two thousand more field accessors to this parser. Every one
        // of them must be guarded; the first that is not would, without this,
        // ABORT the emulator on a file it should merely have refused.
        //
        // It is reachable and it is tested: the S1 mutation battery removes
        // each guard in turn, and every removal lands here instead of taking
        // the process down. `JNSN-28` is the row that asserts the property
        // this protects — that every syntactically valid manifest, however
        // hostile, produces a verdict rather than a crash.
        //
        // The inner catch around `json::parse` stays: it produces the better
        // message for the common case, and this one would swallow it.
        out = Manifest{};
        unknown_keys.clear();
        why = std::string("manifest.json has a value of the wrong shape: ") +
              e.what();
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Writer
// ─────────────────────────────────────────────────────────────────────────

SnapshotWriter::SnapshotWriter(bool uncompressed)
    : uncompressed_(uncompressed) {}

bool SnapshotWriter::add_subsystem(const std::string& name,
                                   const std::string& json_text,
                                   std::string& why) {
    if (name.empty()) {
        why = "a subsystem with an empty name";
        return false;
    }
    if (std::find(manifest_.subsystems.begin(), manifest_.subsystems.end(),
                  name) != manifest_.subsystems.end()) {
        why = "subsystem " + quote(name) + " was already added";
        return false;
    }
    const std::string path = std::string(kStatePrefix) + name + ".json";
    if (!zip::valid_member_path(path, why)) return false;

    Pending p;
    p.path = path;
    p.bytes.assign(json_text.begin(), json_text.end());
    pending_.push_back(std::move(p));
    manifest_.subsystems.push_back(name);
    why.clear();
    return true;
}

bool SnapshotWriter::add_blob(const std::string& path, const uint8_t* data,
                              size_t len, std::string& why) {
    if (!starts_with(path, kMemPrefix)) {
        why = "blob " + quote(path) + " is not under " + quote(kMemPrefix);
        return false;
    }
    if (!zip::valid_member_path(path, why)) return false;
    if (manifest_.members.count(path) != 0) {
        why = "blob " + quote(path) + " was already added";
        return false;
    }

    BlobDecl d;
    d.bytes = len;
    d.crc32 = static_cast<uint32_t>(
        ::crc32(0L, data, static_cast<unsigned>(len)));
    manifest_.members[path] = d;

    Pending p;
    p.path = path;
    p.bytes.assign(data, data + len);
    pending_.push_back(std::move(p));
    why.clear();
    return true;
}

bool SnapshotWriter::add_meta(const std::string& path, const uint8_t* data,
                              size_t len, std::string& why) {
    if (!starts_with(path, kMetaPrefix)) {
        why = "meta member " + quote(path) + " is not under " +
              quote(kMetaPrefix);
        return false;
    }
    if (!zip::valid_member_path(path, why)) return false;
    for (const Pending& p : pending_) {
        if (p.path == path) {
            why = "meta member " + quote(path) + " was already added";
            return false;
        }
    }
    Pending p;
    p.path = path;
    p.bytes.assign(data, data + len);
    pending_.push_back(std::move(p));
    why.clear();
    return true;
}

bool SnapshotWriter::finish(std::vector<uint8_t>& out, std::string& why) {
    manifest_.format_version = kFormatVersion;
    if (!manifest_.capture.frame_boundary) {
        // §8/§10.2 P7: the writer advances to the next frame boundary, so
        // there is no such thing as a mid-frame `.jns`. Refusing to WRITE one
        // is what makes the reader's check of the same fact meaningful — if
        // the writer could emit `false`, the reader's refusal would be
        // describing a file we ourselves produce.
        why = "the capture is not at a frame boundary; a snapshot is only "
              "written at one";
        return false;
    }

    const zip::Method method =
        uncompressed_ ? zip::Method::Stored : zip::Method::Deflate;

    zip::Writer w{kArchiveComment};
    // manifest.json FIRST, so it is member 0 (§6). Serialised after every
    // blob has been added, so `members` is complete and its declarations were
    // computed from the bytes actually written — the torn-file case §12.4
    // refuses on read cannot be constructed here.
    const std::string manifest_text = manifest_to_json(manifest_);
    if (!w.add(kManifestMember, manifest_text, method, why)) return false;

    for (const Pending& p : pending_) {
        if (!w.add(p.path, p.bytes.data(), p.bytes.size(), method, why))
            return false;
    }
    return w.finish(out, why);
}

// ─────────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────────

namespace {

bool refuse(Verdict& v, const std::string& msg) {
    v.ok = false;
    v.refusal = msg;
    return false;
}

}  // namespace

bool open_snapshot(const uint8_t* data, size_t len, const ReaderEnv& env,
                   zip::Reader& zip, Manifest& manifest, Verdict& v) {
    v = Verdict{};
    manifest = Manifest{};

    // ── Container framing ────────────────────────────────────────────────
    std::string why;
    if (!zip.open(data, len, why)) {
        return refuse(v, "not a jnext snapshot: " + why);
    }
    if (zip.comment() != kArchiveComment) {
        return refuse(v, "not a jnext snapshot: the archive comment is " +
                             quote(zip.comment()) + ", not " +
                             quote(kArchiveComment));
    }
    if (zip.entries().empty() || zip.entries()[0].name != kManifestMember) {
        const std::string first =
            zip.entries().empty() ? std::string("(none)") : zip.entries()[0].name;
        return refuse(v, "not a jnext snapshot: member 0 is " + quote(first) +
                             ", not " + quote(kManifestMember));
    }

    std::string manifest_text;
    if (!zip.read_text(kManifestMember, manifest_text, why)) {
        return refuse(v, "not a jnext snapshot: " + why);
    }

    std::vector<std::string> unknown_keys;
    if (!manifest_from_json(manifest_text, manifest, unknown_keys, why)) {
        return refuse(v, why);
    }
    for (const std::string& k : unknown_keys) {
        v.warnings.push_back("manifest.json carries unknown key " + quote(k) +
                             "; ignored");
    }

    // ── format_version (§7.3) ────────────────────────────────────────────
    //
    // FIRST among the semantic checks, and necessarily: every rule below
    // interprets keys under a grammar, and a grammar we do not know is not one
    // we may interpret under. Never a best-effort read.
    const uint32_t fv = manifest.format_version;
    if (fv > env.versions.max_readable) {
        const std::string by = manifest.producer.jnext_version.empty()
                                   ? std::string("an unrecorded jnext version")
                                   : "jnext " + manifest.producer.jnext_version;
        return refuse(v, "snapshot format " + u64s(fv) + " was written by " +
                             by + "; this build reads up to format " +
                             u64s(env.versions.max_readable));
    }
    if (env.versions.readable.count(fv) == 0) {
        auto d = env.versions.dropped.find(fv);
        if (d != env.versions.dropped.end()) {
            return refuse(v, "snapshot format " + u64s(fv) +
                                 " is no longer read; its reader was removed in "
                                 "jnext " + d->second);
        }
        return refuse(v, "snapshot format " + u64s(fv) +
                             " has no reader in this build");
    }

    // ── Facts the file asserts about itself (§8) ─────────────────────────
    if (!manifest.capture.frame_boundary) {
        return refuse(v, "the snapshot declares capture.frame_boundary=false; "
                         "a snapshot is only written at a frame boundary");
    }

    // ── Archive against manifest (§12.4) ─────────────────────────────────
    //
    // Both directions, because they are different failures: a declaration with
    // no member is a torn file, and a member with no declaration is a blob
    // outside §5.3's validation chain.
    for (const auto& kv : manifest.members) {
        const zip::Entry* e = zip.find(kv.first);
        if (e == nullptr) {
            return refuse(v, "manifest declares blob " + quote(kv.first) +
                                 " but the archive has no such member");
        }
        if (e->uncomp_size != kv.second.bytes) {
            return refuse(v, "blob " + quote(kv.first) + " holds " +
                                 u64s(e->uncomp_size) +
                                 " bytes but the manifest declares " +
                                 u64s(kv.second.bytes));
        }
        if (e->crc32 != kv.second.crc32) {
            // The ZIP's own CRC is authoritative for INTEGRITY; a disagreement
            // means the manifest and the archive were not written together,
            // which is a torn file however it happened.
            return refuse(v, "blob " + quote(kv.first) +
                                 " has ZIP CRC-32 0x" + hex8(e->crc32) +
                                 " but the manifest declares 0x" +
                                 hex8(kv.second.crc32));
        }
    }

    for (const zip::Entry& e : zip.entries()) {
        if (e.name == kManifestMember) continue;

        if (starts_with(e.name, kMemPrefix)) {
            if (manifest.members.count(e.name) == 0) {
                // Stricter than the ignore-unknown rule ON PURPOSE: `mem/` is
                // a closed namespace, and an undeclared blob has no length or
                // CRC to check against.
                return refuse(v, "the archive holds blob " + quote(e.name) +
                                     " which the manifest does not declare; "
                                     "mem/ is a closed namespace");
            }
            continue;
        }

        const std::string sub = subsystem_of(e.name);
        if (!sub.empty()) {
            if (std::find(manifest.subsystems.begin(),
                          manifest.subsystems.end(),
                          sub) == manifest.subsystems.end()) {
                v.ignored_members.push_back(e.name);
            }
            continue;
        }

        if (starts_with(e.name, kMetaPrefix)) continue;

        // A well-formed member in an unrecognised namespace. §12.1: ignored
        // and logged, so a newer file read by an older jnext says what it
        // dropped.
        v.ignored_members.push_back(e.name);
    }

    for (const std::string& sub : manifest.subsystems) {
        const std::string path = std::string(kStatePrefix) + sub + ".json";
        if (!zip.has(path)) {
            // The list exists exactly to separate "deliberately not saved"
            // from "missing or corrupt". Conflating them is the failure it
            // prevents.
            return refuse(v, "the manifest lists subsystem " + quote(sub) +
                                 " but the archive has no member " +
                                 quote(path));
        }
    }

    // ── What this build can construct (§7.3, §8) ─────────────────────────
    if (env.constructible_ram_kb.count(manifest.model.ram_kb) == 0) {
        return refuse(v, "the snapshot declares " +
                             u64s(manifest.model.ram_kb) +
                             " KB of RAM, which this build cannot construct");
    }
    if (env.constructible_machines.count(manifest.model.machine) == 0) {
        return refuse(v, "the snapshot declares machine " +
                             quote(manifest.model.machine) +
                             ", which this build cannot construct");
    }
    if (manifest.model.machine != env.machine) {
        // RECONFIGURE, do not refuse. The user must not have to get
        // `--machine` right to reload their own save (§7.3).
        v.reconfigure_to = manifest.model.machine;
        v.warnings.push_back("the snapshot is a " + manifest.model.machine +
                             " machine and a " + env.machine +
                             " is running; reconfiguring to " +
                             manifest.model.machine);
    }

    // ── SD card identity (§11.3) ─────────────────────────────────────────
    if (manifest.sdcard.present && !env.card.present) {
        return refuse(v, "the snapshot was taken with the SD card at " +
                             quote(manifest.sdcard.mounted_path) +
                             " mounted (volume label " +
                             quote(manifest.sdcard.vollab) +
                             "); no card is mounted now");
    }
    if (!manifest.sdcard.present && env.card.present) {
        // The machine did not depend on it; a card that appeared is not a
        // reason to refuse, but it is a reason the run may diverge.
        v.warnings.push_back("the snapshot was taken with no SD card; " +
                             quote(env.card.mounted_path) +
                             " is mounted now");
    }
    if (manifest.sdcard.present && env.card.present) {
        const bool identity_same =
            manifest.sdcard.identity == env.card.identity;
        // Two unpopulated identities are NOT "the same card": they are two
        // absences. Treating them as a match would silently disable the whole
        // Tier-1 check for any snapshot written before the identity could be
        // computed.
        const bool identity_known = manifest.sdcard.identity.populated() &&
                                    env.card.identity.populated();

        if (!identity_same || !identity_known) {
            const std::string detail =
                "snapshot [" + manifest.sdcard.identity.describe() +
                "] vs mounted [" + env.card.identity.describe() + "]";
            if (!env.force_sdcard) {
                return refuse(v, "the mounted SD card is not the one the "
                                 "snapshot was taken on: " + detail);
            }
            v.warnings.push_back(
                "the mounted SD card is not the one the snapshot was taken "
                "on, but --snapshot-force-sdcard was given: " + detail);
        }

        if (manifest.sdcard.read_only != env.card.read_only) {
            // A snapshot taken read-only restoring onto a writable mount is
            // legal and common; the reverse means writes the snapshot's
            // program made were never persisted. Named, not refused.
            v.warnings.push_back(
                std::string("the snapshot was taken with the card mounted ") +
                (manifest.sdcard.read_only ? "read-only" : "writable") +
                " and it is mounted " +
                (env.card.read_only ? "read-only" : "writable") + " now");
        }

        // AN UNKNOWN STAMP IS NOT A CHANGED STAMP (GH #27 S7).
        //
        // Tier 2 can be absent on either side: a snapshot written before the
        // stamp existed, or a live card whose digest failed part-way through a
        // gigabyte of I/O. A plain `==` reports that as a CHANGE — and then
        // says so in a message quoting an empty string as the new digest,
        // which is not true and not actionable. Tier 1 already got this right
        // (`identity_known` above); this is the same rule one tier down.
        //
        // The three cases are genuinely different, so there are three:
        //   both known and equal    -> silent
        //   both known and differing-> warn (refuse if mid-transfer)
        //   either unknown          -> "could not be compared" (refuse if
        //                              mid-transfer, because §11.3's last row
        //                              REQUIRES a match there and an unknown
        //                              stamp is not a match)
        const bool content_known = !manifest.sdcard.content_sha256.empty() &&
                                   !env.card.content_sha256.empty();
        const bool content_same =
            manifest.sdcard.content_sha256 == env.card.content_sha256;
        if (!content_known) {
            const std::string which =
                manifest.sdcard.content_sha256.empty()
                    ? (env.card.content_sha256.empty()
                           ? std::string("neither the snapshot nor the mounted "
                                         "card has one")
                           : std::string("the snapshot has none"))
                    : std::string("the mounted card has none");
            if (env.sd_transfer_in_flight) {
                return refuse(v, "the SD card's contents could not be verified "
                                 "against the snapshot (" + which +
                                 ") and the card was mid-transfer when it was "
                                 "taken");
            }
            v.warnings.push_back("the SD card's contents could not be compared "
                                 "with the snapshot (" + which + ")");
        } else if (!content_same) {
            if (env.sd_transfer_in_flight) {
                // The strictness is EARNED exactly here: the machine was in
                // the middle of reading a sector, and a half-finished read
                // against changed bytes is the "streams garbage" failure.
                return refuse(v, "the SD card's contents changed since the "
                                 "snapshot (" +
                                 quote(manifest.sdcard.content_sha256) +
                                 " -> " + quote(env.card.content_sha256) +
                                 ") and the card was mid-transfer when it was "
                                 "taken");
            }
            v.warnings.push_back("the SD card's contents changed since the "
                                 "snapshot (" +
                                 quote(manifest.sdcard.content_sha256) +
                                 " -> " + quote(env.card.content_sha256) + ")");
        }
        // `informational.fat32_bs_vollab` is deliberately not compared here,
        // and there is deliberately no warning for it either (§11.3).
    }

    // ── ROM identity (§8, §10.2 P3) ──────────────────────────────────────
    //
    // The ROMs are NOT in the file — N3 forbids shipping firmware — so this
    // is the only thing standing between a user and a snapshot that runs
    // DIFFERENT CODE with no indication anywhere. It warns rather than
    // refuses by default, because a corrected or regionalised ROM is a thing
    // people legitimately have and the machine may well run fine on it;
    // `--snapshot-strict` turns it into a refusal for the cases where "well"
    // is not good enough.
    //
    // Only names present in BOTH sides are compared. A name this build does
    // not have is a ROM this machine does not use (a 48K snapshot against a
    // build that also extracted `plus3.rom`), and treating that as a
    // mismatch is the cries-wolf failure §11.1 rejects for the SD card.
    {
        std::vector<std::string> differing;
        for (const auto& kv : manifest.roms.sha256) {
            const auto it = env.roms.sha256.find(kv.first);
            if (it == env.roms.sha256.end()) continue;
            if (kv.second.empty() || it->second.empty()) continue;
            if (kv.second != it->second) differing.push_back(kv.first);
        }
        if (!manifest.roms.boot_rom_sha256.empty() &&
            !env.roms.boot_rom_sha256.empty() &&
            manifest.roms.boot_rom_sha256 != env.roms.boot_rom_sha256) {
            differing.push_back("nextboot.rom");
        }
        if (!differing.empty()) {
            std::string names;
            for (const std::string& n : differing)
                names += (names.empty() ? "" : ", ") + n;
            const std::string msg =
                "the snapshot was taken against different ROM content (" +
                names + ")";
            if (env.strict) {
                return refuse(v, msg + "; --snapshot-strict refuses it");
            }
            v.warnings.push_back(msg + "; restoring anyway");
        }
    }

    // ── Tape identity (§8, §10.2 P4) ─────────────────────────────────────
    //
    // Tape position is independent of CPU rewind, so the tape objects are
    // excluded from the state stream by design and a snapshot taken DURING a
    // load restores a machine waiting for a tape that is not playing. The
    // file is recorded by reopenable identity — the esxDOS-handle shape — and
    // an absent one WARNS and restores without it. Never a refusal, and not
    // under `--snapshot-strict` either: a machine whose tape has finished
    // loading is a perfectly good machine, and refusing to restore it because
    // the .tzx has been moved would be the format getting in the way.
    if (manifest.tape.present && !env.tape_file_available) {
        v.warnings.push_back(
            "the tape " +
            quote(manifest.tape.path.empty() ? std::string("(unnamed)")
                                             : manifest.tape.path) +
            " is not available; restoring without it, so a load in progress "
            "will not continue");
    }

    // ── The moving version (§7.3) ────────────────────────────────────────
    if (manifest.model.state_model_revision != env.state_model_revision) {
        const std::string msg =
            "the snapshot models state revision " +
            u64s(manifest.model.state_model_revision) + " (jnext " +
            (manifest.producer.jnext_version.empty()
                 ? std::string("unrecorded")
                 : manifest.producer.jnext_version) +
            ") and this build models revision " +
            u64s(env.state_model_revision);
        if (env.strict) {
            return refuse(v, msg + "; --snapshot-strict refuses it");
        }
        v.warnings.push_back(msg + "; restoring anyway");
    }

    v.ok = true;
    v.refusal.clear();
    return true;
}

}  // namespace jns
}  // namespace jnext
