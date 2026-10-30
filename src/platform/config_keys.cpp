#include "platform/config_keys.h"

#include "core/cli_options.h"
#include "core/emulator_config.h"
#include "debug/debug_keymap.h"
#include "input/joy_source.h"
#include "peripheral/esp_host_policy.h"
#include "platform/pointer_capture.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <vector>

namespace config_keys {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower(c);
    return out;
}

std::string trimmed(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

// "7" -> `startup.cpu_speed: VALUE must be a whole number from 0 to 3, not "7"`.
bool bad(std::string& why, const std::string& key, const std::string& val,
         const std::string& expected) {
    why = key + ": VALUE must be " + expected + ", not \"" + val + "\"";
    return false;
}

std::string num(double v) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, 6);
    return std::string(buf, r.ptr);
}

std::string words_phrase(const char* list) {
    std::string out;
    for (int i = 0;; ++i) {
        const std::string_view w = word_at(list, i);
        if (w.empty()) break;
        if (i > 0) out += ", ";
        out += w;
    }
    return out;
}

bool canon_value(const Key& k, const std::string& key, const std::string& val,
                 std::string& canon, std::string& why) {
    switch (k.type) {
        case Type::Bool: {
            const std::string v = to_lower(val);
            if (v == "true" || v == "false") { canon = v; return true; }
            return bad(why, key, val, "true or false");
        }
        case Type::Int: {
            long n = 0;
            if (cli::parse_int(val.c_str(), static_cast<long>(k.lo), static_cast<long>(k.hi), n)) {
                canon = std::to_string(n);
                return true;
            }
            return bad(why, key, val, "a whole number from " + std::to_string(static_cast<long>(k.lo))
                                      + " to " + std::to_string(static_cast<long>(k.hi)));
        }
        case Type::Float: {
            double d = 0;
            const char* b = val.c_str();
            const char* e = b + val.size();
            const auto r = std::from_chars(b, e, d);
            if (!val.empty() && r.ec == std::errc() && r.ptr == e && std::isfinite(d)
                && d >= k.lo && d <= k.hi) {
                canon = num(d);
                return true;
            }
            return bad(why, key, val, "a number from " + num(k.lo) + " to " + num(k.hi));
        }
        case Type::Word: {
            const std::string v = to_lower(trimmed(val));
            switch (k.id) {
                case KeyId::InputJoy1Source:
                case KeyId::InputJoy2Source: {
                    JoySource js;
                    if (parse_joy_source(v.c_str(), js)) { canon = joy_source_str(js); return true; }
                    break;
                }
                case KeyId::InputMouseRelease: {
                    pointer_capture::ReleaseMethod m;
                    if (pointer_capture::parse_release_method(v, m)) {
                        canon = pointer_capture::release_method_name(m);
                        return true;
                    }
                    break;
                }
                default: {
                    const int i = word_index(k.values, v);
                    if (i >= 0) { canon = std::string(word_at(k.values, i)); return true; }
                    break;
                }
            }
            return bad(why, key, val, "one of " + words_phrase(k.values));
        }
        case Type::Machine: {
            MachineType mt;
            if (parse_machine_type(trimmed(val), mt)) { canon = machine_type_key(mt); return true; }
            return bad(why, key, val, "one of " + words_phrase(k.values));
        }
        case Type::Text:
        case Type::Path:
            canon = val;
            return true;
        case Type::TrimmedText:
            canon = trimmed(val);
            return true;
        case Type::HostList: {
            EspHostPolicy policy;
            if (!val.empty()) {
                size_t begin = 0;
                for (;;) {
                    const size_t comma = val.find(',', begin);
                    const std::string host = val.substr(
                        begin, comma == std::string::npos ? std::string::npos : comma - begin);
                    if (!policy.add(host))
                        return bad(why, key, val, "a comma-separated list of host names with no blank entry");
                    if (comma == std::string::npos) break;
                    begin = comma + 1;
                }
            }
            canon.clear();
            for (const std::string& h : policy.allowed_hosts) {
                if (!canon.empty()) canon += ',';
                canon += h;
            }
            return true;
        }
        case Type::DeviceId: {
            const std::string v = trimmed(val);
            if (v.empty()) { canon.clear(); return true; }
            std::string id;
            if (normalize_joy_device_id(v, id)) { canon = id; return true; }
            return bad(why, key, val, "empty (Automatic) or a controller id of 32 hex digits, "
                                      "optionally followed by #N");
        }
        case Type::Keep: {
            int n = 0;
            if (cli::parse_quick_snapshot_keep(val.c_str(), n)) {
                canon = n == 0 ? "unlimited" : std::to_string(n);
                return true;
            }
            return bad(why, key, val, "a whole number from 1 to 1000000, or unlimited");
        }
        case Type::Combo: {
            jnext::dbgkeys::Combo c;
            std::string reason;
            if (jnext::dbgkeys::parse_combo(val, c, reason)) {
                canon = jnext::dbgkeys::render_combo(c);
                return true;
            }
            why = key + ": VALUE \"" + val + "\" is not a key combination: " + reason;
            return false;
        }
    }
    return false;
}

} // namespace

int word_index(std::string_view list, std::string_view word) {
    for (int i = 0;; ++i) {
        const std::string_view w = word_at(list, i);
        if (w.empty()) return -1;
        if (w.size() != word.size()) continue;
        bool same = true;
        for (size_t j = 0; j < w.size(); ++j)
            if (lower(w[j]) != lower(word[j])) { same = false; break; }
        if (same) return i;
    }
}

const Key* find(std::string_view section, std::string_view name) {
    for (const Key& k : KEYS) {
        if (section != k.section) continue;
        if (k.id == KeyId::DebuggerKey) {
            if (jnext::dbgkeys::find_action(std::string(name))) return &k;
            continue;
        }
        if (name == k.name) return &k;
    }
    return nullptr;
}

bool parse_assignment(const char* arg, Assignment& out, std::string& why) {
    const std::string a = arg ? arg : "";
    const std::string quoted = "\"" + a + "\"";

    const size_t eq = a.find('=');
    if (eq == std::string::npos) {
        why = quoted + ": expected SECTION.KEY=VALUE, with no '=' found";
        return false;
    }
    const std::string lhs = a.substr(0, eq);
    const std::string val = a.substr(eq + 1);
    const size_t dot = lhs.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == lhs.size()) {
        why = quoted + ": expected SECTION.KEY=VALUE, with a section and a key before the '='";
        return false;
    }
    const std::string sec = lhs.substr(0, dot);
    const std::string nm  = lhs.substr(dot + 1);

    const Key* k = find(sec, nm);
    if (!k) {
        bool known_section = false;
        for (const Key& r : KEYS) if (sec == r.section) known_section = true;
        if (!known_section) {
            why = quoted + ": unknown section \"" + sec + "\"";
        } else if (sec == section(KeyId::DebuggerKey)) {
            why = quoted + ": unknown debugger action \"" + nm + "\"";
        } else {
            why = quoted + ": unknown key \"" + nm + "\" in section \"" + sec + "\"";
        }
        return false;
    }
    if (k->not_setting) {
        why = quoted + ": " + lhs + " is not a setting: " + k->not_setting;
        return false;
    }

    std::string canon;
    if (!canon_value(*k, lhs, val, canon, why)) return false;

    out.id = k->id;
    out.family_name = k->id == KeyId::DebuggerKey ? nm : std::string();
    out.text = canon;
    return true;
}

} // namespace config_keys
