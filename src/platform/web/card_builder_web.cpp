// The browser page's minimal-card builder (GH #458): what jnext-image.js calls to turn the
// official NextZXOS distribution zip, pushed in chunks from JavaScript, into the 40 MiB SD
// card in the in-memory file system. The builder is core/minimal_card.cpp, the same code
// the make-minimal-card tool runs, so the card is byte for byte the one the tests boot.
// The page never holds the zip: JS copies one chunk at a time into a staging buffer and
// calls jnext_card_feed(). Linked into the jnext_web target only.
#include "core/minimal_card.h"
#include "third_party/nlohmann-json/nlohmann/json.hpp"

#include <emscripten.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

std::unique_ptr<minimal_card::Builder> g_builder;
std::string g_error;

// minimal_card::Status as the ints the page sees.
int code(minimal_card::Status s)
{
    switch (s) {
    case minimal_card::Status::NeedMore:    return 0;
    case minimal_card::Status::InflateDone: return 1;
    case minimal_card::Status::Done:        return 2;
    case minimal_card::Status::Error:       break;
    }
    return 3;
}

char* dup(const std::string& s)
{
    char* m = static_cast<char*>(std::malloc(s.size() + 1));
    if (m) std::memcpy(m, s.c_str(), s.size() + 1);
    return m;
}

}  // namespace

// The one distribution this release accepts, as JSON, malloc'd (JS frees it):
// {url, file, size, zipSha256, cardSha256, recipe}.
extern "C" EMSCRIPTEN_KEEPALIVE char* jnext_card_distribution()
{
    const minimal_card::Pins& p = minimal_card::kDistribution;
    nlohmann::json j = {{"url", p.url}, {"file", p.file}, {"size", p.zip_size}, {"zipSha256", p.zip_sha256},
                        {"cardSha256", p.card_sha256}, {"recipe", p.recipe}};
    return dup(j.dump());
}

// Start a build of the zip into out_path. expect_json null: the production pins. Otherwise
// an object whose present keys {file, zipSha256, size, cardSha256} replace those pins (the
// localhost development seam: a stand-in zip; an empty string or 0 = not checked).
// 0 = begun, 3 = refused (jnext_card_error says why).
extern "C" EMSCRIPTEN_KEEPALIVE int jnext_card_begin(const char* out_path, const char* expect_json)
{
    g_builder.reset();
    g_error.clear();
    minimal_card::Expect ex = minimal_card::pinned(minimal_card::Input::Zip);
    if (expect_json) {
        nlohmann::json j = nlohmann::json::parse(expect_json, nullptr, false);
        if (j.is_discarded() || !j.is_object()) { g_error = "jnext_card_begin: the expectation is not a JSON object"; return 3; }
        if (j.contains("file") && j["file"].is_string()) ex.file = j["file"].get<std::string>();
        if (j.contains("zipSha256") && j["zipSha256"].is_string()) ex.zip_sha256 = j["zipSha256"].get<std::string>();
        if (j.contains("size") && j["size"].is_number_unsigned()) ex.zip_size = j["size"].get<uint64_t>();
        if (j.contains("cardSha256") && j["cardSha256"].is_string()) ex.card_sha256 = j["cardSha256"].get<std::string>();
    }
    g_builder.reset(new minimal_card::Builder(minimal_card::Input::Zip, out_path ? out_path : "", ex));
    return 0;
}

// The next bytes of the zip, in order, from byte 0. 0 = carry on, 1 = everything the card
// needs has been read (keep feeding: the digest covers the whole file), 3 = refused.
extern "C" EMSCRIPTEN_KEEPALIVE int jnext_card_feed(const uint8_t* p, uint32_t n)
{
    if (!g_builder) { g_error = "jnext_card_feed: no build begun"; return 3; }
    const int c = code(g_builder->feed(p, n));
    if (c == 3) g_error = g_builder->error();
    return c;
}

// All of the zip given: check the pins, write the card, check it. 2 = done, 3 = refused.
extern "C" EMSCRIPTEN_KEEPALIVE int jnext_card_finish()
{
    if (!g_builder) { g_error = "jnext_card_finish: no build begun"; return 3; }
    const int c = code(g_builder->finish());
    if (c == 3) g_error = g_builder->error();
    return c;
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* jnext_card_error() { return g_error.c_str(); }

// Bytes of the SD image read so far (a double: above 2^31 for the real distribution).
extern "C" EMSCRIPTEN_KEEPALIVE double jnext_card_inflated()
{
    return g_builder ? static_cast<double>(g_builder->inflated()) : 0.0;
}

// The SHA-256 of the finished card (malloc'd, JS frees it); empty before Done.
extern "C" EMSCRIPTEN_KEEPALIVE char* jnext_card_sha256()
{
    return dup(g_builder ? g_builder->card_sha256() : std::string());
}
