#include "platform/web_api.h"

#include "third_party/nlohmann-json/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace web_api {

namespace {

using json = nlohmann::json;

const Option* find_option(const std::string& name)
{
    for (const Option& o : WEB_OPTIONS)
        if (name == o.name) return &o;
    return nullptr;
}

bool is_hex64(const std::string& s)
{
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool all_zero(const std::string& s)
{
    return std::all_of(s.begin(), s.end(), [](char c) { return c == '0'; });
}

bool refuse(std::string& why, const std::string& msg)
{
    why = msg;
    return false;
}

// A --set value as text: a string verbatim, a boolean or number as JSON writes it.
bool set_value(const json& v, std::string& out)
{
    if (v.is_string()) { out = v.get<std::string>(); return true; }
    if (v.is_boolean() || v.is_number()) { out = v.dump(); return true; }
    return false;
}

}  // namespace

const char* flag_spelling(cli::OptId id)
{
    for (const cli::Option& o : cli::OPTIONS)
        if (o.id == id && o.doc == cli::Doc::Documented) return o.name;
    return nullptr;
}

std::string key_spelling(config_keys::KeyId id)
{
    for (const config_keys::Key& k : config_keys::KEYS)
        if (k.id == id) return std::string(k.section) + "." + k.name;
    return {};
}

bool build_argv(std::string_view options_json, std::vector<std::string>& argv, std::string& why)
{
    argv.clear();
    json o = json::parse(options_json.begin(), options_json.end(), nullptr, false);
    if (o.is_discarded() || !o.is_object()) return refuse(why, "options: not a JSON object");

    // Names first: an unknown one is named before any shape is looked at.
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!find_option(it.key())) return refuse(why, "unknown option '" + it.key() + "'");
    for (const Option& t : WEB_OPTIONS)
        if (t.required && !o.contains(t.name)) return refuse(why, std::string(t.name) + " is required");

    auto bad = [&](const char* name, const char* want) {
        return refuse(why, std::string("option '") + name + "' must be " + want);
    };

    std::vector<std::string> out;
    std::vector<std::string> sets;
    std::vector<std::string> extra;
    std::string load;

    for (const Option& t : WEB_OPTIONS) {
        if (!o.contains(t.name)) continue;
        const json& v = o[t.name];
        switch (t.kind) {
        case Kind::Image: {
            if (!v.is_object() || !v.contains("path") || !v["path"].is_string() ||
                !v.contains("sha256") || !v["sha256"].is_string())
                return bad(t.name, "an object {path, sha256}");
            const std::string& sha = v["sha256"].get_ref<const std::string&>();
            // An all-zero digest is the cache's "no digest": never let one through.
            if (is_hex64(sha) && all_zero(sha)) return refuse(why, "option 'image': the sha256 is all zeros");
            out.push_back(flag_spelling(*t.opt));
            out.push_back(v["path"].get<std::string>());
            out.push_back(flag_spelling(*t.opt2));
            out.push_back(sha);
            break;
        }
        case Kind::Flag: {
            if (!v.is_string()) return bad(t.name, "a string");
            if (*t.opt == cli::OptId::Load) {
                load = v.get<std::string>();
            } else {
                out.push_back(flag_spelling(*t.opt));
                out.push_back(v.get<std::string>());
            }
            break;
        }
        case Kind::SetKey: {
            if (!v.is_boolean()) return bad(t.name, "a boolean");
            sets.push_back("--set");
            sets.push_back(key_spelling(*t.key) + "=" + v.dump());
            break;
        }
        case Kind::SetMap: {
            if (!v.is_object()) return bad(t.name, "an object of section.key values");
            for (auto it = v.begin(); it != v.end(); ++it) {
                std::string val;
                if (!set_value(it.value(), val))
                    return refuse(why, "option 'set': the value of '" + it.key() + "' must be a string, number or boolean");
                extra.push_back("--set");
                extra.push_back(it.key() + "=" + val);
            }
            break;
        }
        case Kind::Argv: {
            if (!v.is_array()) return bad(t.name, "an array of strings");
            for (const json& a : v) {
                if (!a.is_string()) return bad(t.name, "an array of strings");
                extra.push_back(a.get<std::string>());
            }
            break;
        }
        case Kind::Host: {
            const bool is_size = std::string(t.name) == "width" || std::string(t.name) == "height";
            if (is_size ? !(v.is_number() || v.is_string()) : !v.is_string())
                return bad(t.name, is_size ? "a number or a string" : "a string");
            break;
        }
        case Kind::Callback: {
            if (!v.is_boolean()) return bad(t.name, "a function");
            break;
        }
        }
    }

    // Order: image, the named flags, --load, the muted --set, then set entries, then args.
    // The command line's rule is that the later one wins.
    if (!load.empty()) { out.push_back(flag_spelling(cli::OptId::Load)); out.push_back(load); }
    out.insert(out.end(), sets.begin(), sets.end());
    out.insert(out.end(), extra.begin(), extra.end());
    argv = std::move(out);
    return true;
}

std::optional<double> volume_to_gain_db(double v)
{
    if (!(v >= 0.0 && v <= 1.0)) return std::nullopt;   // also NaN
    if (v == 0.0) return static_cast<double>(kMuteDb);
    return 20.0 * std::log10(v);
}

}  // namespace web_api
