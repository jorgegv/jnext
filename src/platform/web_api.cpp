#include "platform/web_api.h"
#include "core/minimal_card.h"

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

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
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
    // card: {source: 'none'} runs a NEX with no SD card at all (--no-sdcard): the page layer
    // makes no card, so the internal $card entry is absent and must stay so.
    const bool no_card = o.contains("card") && o["card"].is_object() && o["card"].contains("source") &&
                         o["card"]["source"].is_string() && o["card"]["source"].get<std::string>() == "none";
    if (no_card) {
        if (o.contains("$card"))
            return refuse(why, "card source 'none' with an SD card (an internal error of the page layer)");
        if (!o.contains("file"))
            return refuse(why, "card source 'none' needs file: with no SD card there is no NextZXOS menu to boot to");
        if (o.contains("keepWrites"))
            return refuse(why, "keepWrites needs an SD card to keep the writes of: not with card source 'none'");
        if (o["card"].contains("dirs"))
            return refuse(why, "card.dirs needs an SD card: not with card source 'none'");
    }
    for (const Option& t : WEB_OPTIONS)
        if (t.required && !(no_card && t.internal) && !o.contains(t.name))
            return refuse(why, t.internal ? std::string("the SD card is missing (an internal error of the page layer)")
                                          : std::string(t.name) + " is required");

    auto bad = [&](const char* name, const char* want) {
        return refuse(why, std::string("option '") + name + "' must be " + want);
    };

    std::vector<std::string> out;
    std::vector<std::string> sets;
    std::vector<std::string> extra;
    std::vector<std::string> debug;
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
            if (is_hex64(sha) && all_zero(sha)) return refuse(why, std::string("option '") + t.name + "': the sha256 is all zeros");
            out.push_back(flag_spelling(*t.opt));
            out.push_back(v["path"].get<std::string>());
            out.push_back(flag_spelling(*t.opt2));
            out.push_back(sha);
            break;
        }
        case Kind::Flag: {
            if (*t.opt == cli::OptId::Load && v.is_object()) {
                // {card: PATH}: a NEX on the SD card (GH #510). Validated here, not turned into a flag:
                // the page layer replaces it with the copy's path and $serveDir once the card exists.
                const std::string F = "option 'file': ";
                for (auto it = v.begin(); it != v.end(); ++it)
                    if (it.key() != "card") return refuse(why, F + "unknown key '" + it.key() + "'");
                if (no_card) return refuse(why, F + "{card} needs an SD card: not with card source 'none'");
                if (!v.contains("card") || !v["card"].is_string()) return refuse(why, F + "{card} must be a string");
                const std::string p = v["card"].get<std::string>();
                if (p.empty() || p[0] != '/') return refuse(why, F + "{card} must be an absolute path");
                std::vector<std::string> parts;
                for (size_t i = 1; i <= p.size();) {
                    const size_t e = p.find('/', i);
                    const std::string c = p.substr(i, (e == std::string::npos ? p.size() : e) - i);
                    if (c.empty() || c == "." || c == "..")
                        return refuse(why, F + "{card} has an empty, '.' or '..' component: " + p);
                    parts.push_back(c);
                    if (e == std::string::npos) break;
                    i = e + 1;
                }
                if (p.size() < 4 || lower(p.substr(p.size() - 4)) != ".nex") return refuse(why, F + "{card} takes a .nex");
                // Component-wise inside one canonical card.dirs entry (lower case, so case does not matter).
                const std::string lp = lower(p);
                std::vector<std::string> canon;
                if (o.contains("card") && o["card"].is_object() && o["card"].contains("dirs") && o["card"]["dirs"].is_array()) {
                    std::vector<std::string> in;
                    for (const json& d : o["card"]["dirs"]) if (d.is_string()) in.push_back(d.get<std::string>());
                    std::string dwhy;
                    if (!minimal_card::canonical_dirs(in, canon, dwhy)) canon.clear();
                }
                bool inside = false;
                for (const std::string& d : canon)
                    if (lp.size() > d.size() && lp.compare(0, d.size(), d) == 0 && lp[d.size()] == '/') inside = true;
                if (!inside) return refuse(why, F + p + " is not on the card: add its directory to card.dirs");
                break;
            }
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
            if (std::string(t.name) == "$serveDir") {
                // Internal (GH #510): the program was copied out of the card with its directory.
                if (!v.is_boolean() || !v.get<bool>() || !o.contains("$card") || !o.contains("file") || !o["file"].is_string())
                    return refuse(why, "$serveDir needs a program copied from the SD card (an internal error of the page layer)");
                out.push_back(flag_spelling(cli::OptId::EsxdosStub));
                break;
            }
            if (std::string(t.name) == "card") {
                if (!v.is_object()) return bad(t.name, "an object {source, dirs}");
                for (auto it = v.begin(); it != v.end(); ++it) {
                    if (it.key() == "dirs") {   // GH #509: extra distribution paths, one canonicaliser (minimal_card)
                        if (!it.value().is_array()) return refuse(why, "option 'card': dirs must be an array of strings");
                        std::vector<std::string> in, canon;
                        for (const json& d : it.value()) {
                            if (!d.is_string()) return refuse(why, "option 'card': dirs must be an array of strings");
                            in.push_back(d.get<std::string>());
                        }
                        std::string dwhy;
                        if (!minimal_card::canonical_dirs(in, canon, dwhy)) return refuse(why, "option 'card': dirs: " + dwhy);
                        continue;
                    }
                    if (it.key() != "source") return refuse(why, "option 'card': unknown key '" + it.key() + "'");
                    const json& sv = it.value();
                    if (!sv.is_string() || (sv.get_ref<const std::string&>() != "auto" &&
                                            sv.get_ref<const std::string&>() != "fetch" &&
                                            sv.get_ref<const std::string&>() != "pick" &&
                                            sv.get_ref<const std::string&>() != "none"))
                        // GH #540: 'none' is accepted, but this message is the one existing pages and
                        // tests match, so it keeps naming the three sources that obtain a card.
                        return refuse(why, "option 'card': source must be 'auto', 'fetch' or 'pick'");
                }
                if (no_card) out.push_back(flag_spelling(cli::OptId::NoSdcard));
                break;
            }
            const bool is_size = std::string(t.name) == "width" || std::string(t.name) == "height";
            if (is_size ? !(v.is_number() || v.is_string()) : !v.is_string())
                return bad(t.name, is_size ? "a number or a string" : "a string");
            break;
        }
        case Kind::Debug: {
            // GH #545: one entry, or 1..3. Each becomes `--debug-channel PROTOCOL`; `transport` and `url`
            // are the page layer's (like card.source) and never reach the core.
            const std::string D = "option 'debug': ";
            if (!v.is_object() && !(v.is_array() && !v.empty()))
                return refuse(why, "option 'debug' must be an object {protocol, transport, url} or an array of 1 to 3 of them");
            if (v.is_array() && v.size() > 3) return refuse(why, D + "at most 3 entries (one for each of dzrp, zrcp, gdb)");
            std::vector<std::string> seen;
            const json list = v.is_array() ? v : json::array({v});
            for (size_t i = 0; i < list.size(); ++i) {
                const json& e = list[i];
                const std::string E = D + "entry " + std::to_string(i + 1) + ": ";
                if (!e.is_object()) return refuse(why, E + "must be an object {protocol, transport, url}");
                for (auto it = e.begin(); it != e.end(); ++it)
                    if (it.key() != "protocol" && it.key() != "transport" && it.key() != "url")
                        return refuse(why, E + "unknown key '" + it.key() + "'");
                if (!e.contains("protocol") || !e["protocol"].is_string())
                    return refuse(why, E + "protocol is required: 'dzrp', 'zrcp' or 'gdb'");
                const std::string proto = e["protocol"].get<std::string>();
                cli::DebugProtocol dp;
                if (!cli::parse_debug_protocol(proto.c_str(), dp))
                    return refuse(why, E + "protocol must be 'dzrp', 'zrcp' or 'gdb'");
                if (std::find(seen.begin(), seen.end(), proto) != seen.end())
                    return refuse(why, E + "protocol '" + proto + "' is given twice");
                seen.push_back(proto);
                std::string transport = "channel";
                if (e.contains("transport")) {
                    if (!e["transport"].is_string() ||
                        (e["transport"].get<std::string>() != "channel" && e["transport"].get<std::string>() != "ws"))
                        return refuse(why, E + "transport must be 'channel' or 'ws'");
                    transport = e["transport"].get<std::string>();
                }
                if (transport == "ws") {
                    if (!e.contains("url") || !e["url"].is_string())
                        return refuse(why, E + "transport 'ws' needs a url: a string starting ws:// or wss://");
                    const std::string u = e["url"].get<std::string>();
                    if (u.rfind("ws://", 0) != 0 && u.rfind("wss://", 0) != 0)
                        return refuse(why, E + "url must start with ws:// or wss://");
                } else if (e.contains("url")) {
                    return refuse(why, E + "url is only for transport 'ws'");
                }
                debug.push_back(flag_spelling(cli::OptId::DebugChannel));
                debug.push_back(proto);
            }
            break;
        }
        case Kind::Callback: {
            if (!v.is_boolean()) return bad(t.name, "a function");
            break;
        }
        }
    }

    // Order: image, the named flags, --debug-channel, --load, the muted --set, then set entries, then args.
    // The command line's rule is that the later one wins.
    out.insert(out.end(), debug.begin(), debug.end());
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
