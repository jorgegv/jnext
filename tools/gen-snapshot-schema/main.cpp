// `tools/gen-snapshot-schema` — the ONE command that produces
// `doc/formats/jns-snapshot.schema.json`, GH #27 stage S2.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §9.3, §13.2(3)/(4), §16.3.
//
// Usage:
//   gen-snapshot-schema --overlay src/save/jns-schema-overlay.json \
//                       --out doc/formats/jns-snapshot.schema.json
//   gen-snapshot-schema --overlay ... --stdout      (print, write nothing)
//
// ── DETERMINISM IS A REQUIREMENT ON THIS PROGRAM, NOT A HOPE (§16.3) ─────
//
// Stable key order (sorted, because every object here is a `std::map`-backed
// `nlohmann::json`), no timestamp, no absolute path, no build id, no jnext
// version, `\n` endings. `docs-check` learned this the hard way when mkdocs
// stamped `sitemap.xml.gz` with the build date and the gate went red on an
// unchanged tree — a staleness gate that reports a false positive gets
// disabled, and a disabled gate is worse than none.
//
// ── THE SUBSYSTEM REGISTRY IS EMPTY IN S2 ───────────────────────────────
//
// That is the correct state (§17): S2 builds the descriptor layer, S3-S5
// migrate 34 subsystems into it. The gate exists NOW so the first migration
// shows up as a schema diff, rather than being added after the thirty-fourth
// and missing every diff it exists to surface.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "save/state_desc_schema.h"
#include "third_party/nlohmann-json/nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

int fail(const std::string& why)
{
    std::fprintf(stderr, "gen-snapshot-schema: %s\n", why.c_str());
    return 1;
}

/// Strip the overlay's `_comment` / `_why` annotations.
///
/// They are the reason the overlay is reviewable — every constraint states
/// which section of the design it comes from — and they are NOT JSON Schema
/// keywords, so leaving them in the generated artefact would hand a validator
/// vocabulary it must ignore and a reader vocabulary it cannot check. They are
/// kept in the SOURCE, which is where a human reads them, and dropped from the
/// OUTPUT, which is where a machine reads it.
void strip_annotations(json& j)
{
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end();) {
            if (it.key() == "_comment" || it.key() == "_why") {
                it = j.erase(it);
            } else {
                strip_annotations(*it);
                ++it;
            }
        }
    } else if (j.is_array()) {
        for (auto& e : j) strip_annotations(e);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    std::string overlay_path;
    std::string out_path;
    bool        to_stdout = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--overlay" && i + 1 < argc)   overlay_path = argv[++i];
        else if (a == "--out" && i + 1 < argc)  out_path = argv[++i];
        else if (a == "--stdout")               to_stdout = true;
        else return fail("unknown argument " + a);
    }
    if (overlay_path.empty()) return fail("--overlay is required");
    if (out_path.empty() && !to_stdout) return fail("--out or --stdout is required");

    std::ifstream in(overlay_path, std::ios::binary);
    if (!in) return fail("cannot read overlay " + overlay_path);
    std::ostringstream buf;
    buf << in.rdbuf();

    json overlay = json::parse(buf.str(), nullptr, /*allow_exceptions=*/false);
    if (overlay.is_discarded()) return fail(overlay_path + " is not valid JSON");
    if (!overlay.is_object())   return fail(overlay_path + " is not a JSON object");

    json root = json::object();
    for (const char* k : {"$schema", "$id", "title", "description"}) {
        if (overlay.contains(k)) root[k] = overlay[k];
    }
    root["type"] = "object";
    // §12.1 — a reader IGNORES any member whose path it does not recognise,
    // including whole unknown directory prefixes, so the archive level must
    // stay open. The strictness lives one level down, inside each document.
    root["additionalProperties"] = true;

    json defs  = overlay.contains("$defs") ? overlay["$defs"] : json::object();
    json props = overlay.contains("properties") ? overlay["properties"] : json::object();
    json req   = overlay.contains("required") ? overlay["required"] : json::array();

    // The generated half: one `$defs` entry and one `properties` entry per
    // REGISTERED subsystem. Empty in S2 (see the header).
    for (const auto& e : jnext::save::SchemaRegistry::entries()) {
        jnext::save::SchemaDesc d;
        e.fn(d);
        if (d.failed()) {
            return fail(std::string("subsystem ") + e.member +
                        " has a broken declaration: " +
                        (d.failure() ? d.failure() : "?"));
        }
        json sub = json::parse(d.str(), nullptr, /*allow_exceptions=*/false);
        if (sub.is_discarded()) {
            return fail(std::string("subsystem ") + e.member +
                        " emitted invalid JSON");
        }

        const std::string def_key    = "state." + e.member;
        const std::string member_key = "state/" + e.member + ".json";
        // A silent overwrite here would let the hand-written overlay mask the
        // very generated defect it exists beside (§13.2(4)). Refuse instead.
        if (defs.contains(def_key)) {
            return fail("overlay and generator both define $defs/" + def_key);
        }
        if (props.contains(member_key)) {
            return fail("overlay and generator both define properties/" + member_key);
        }
        defs[def_key]     = sub;
        json ref          = json::object();
        ref["$ref"]       = "#/$defs/" + def_key;
        props[member_key] = ref;
    }

    root["properties"] = props;
    root["required"]   = req;
    root["$defs"]      = defs;
    if (overlay.contains("allOf")) root["allOf"] = overlay["allOf"];

    strip_annotations(root);

    const std::string text = root.dump(2) + "\n";

    if (to_stdout) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) return fail("cannot write " + out_path);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) return fail("short write to " + out_path);
    return 0;
}
