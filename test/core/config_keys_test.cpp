// config_keys test (GH #417): the one table of jnext.conf keys
// (src/core/config_keys.h) and its setter parse_assignment(), plus
// dbgkeys::apply_overrides(), the debugger-key half of `--set`.
//
// No VHDL oracle: a host configuration file, not emulated hardware. The
// contract is jnext.conf as AppConfig::load()/save() read and write it
// (app_config_test AC-94..96 hold the table to that, on the real file) and the
// existing parsers a key's value already goes through (EspHostPolicy::add,
// normalize_joy_device_id, dbgkeys::parse_combo, ...).
//
// Standalone runner, no GoogleTest, no Qt: the table is Qt-free so the SDL-only
// and headless builds can use it.

#include "core/config_keys.h"

#include "core/cli_options.h"
#include "debug/debug_keymap.h"
#include "input/joy_source.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) { ++g_pass; std::printf("PASS %-8s %s\n", id, desc); }
    else {
        ++g_fail;
        std::printf("FAIL %-8s %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using config_keys::KEYS;
using config_keys::Key;
using config_keys::KeyId;
using config_keys::Type;

std::string full_name(const Key& k) { return std::string(k.section) + "." + k.name; }
bool is_family(const Key& k) { return k.id == KeyId::DebuggerKey; }

bool matches_ident(const std::string& s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// Parse `arg`; on success return the canonical text, else "<refused>" and `why`.
std::string canon(const std::string& arg, std::string* why_out = nullptr) {
    config_keys::Assignment a;
    std::string why;
    const bool ok = config_keys::parse_assignment(arg.c_str(), a, why);
    if (why_out) *why_out = why;
    return ok ? a.text : std::string("<refused>");
}

std::string upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

const Key* key_of(KeyId id) { return &config_keys::info(id); }

} // namespace

int main() {
    // SETK-01
    {
        std::set<std::string> seen;
        bool dup = false, bad_name = false;
        for (const Key& k : KEYS) {
            if (!seen.insert(full_name(k)).second) dup = true;
            if (!matches_ident(k.section)) bad_name = true;
            if (!is_family(k) && !matches_ident(k.name)) bad_name = true;
        }
        check("SETK-01", "no duplicate (section, name); every section and key matches [a-z][a-z0-9_]*",
              !dup && !bad_name && seen.size() == config_keys::KEY_COUNT);
    }

    // SETK-02
    {
        std::string bad;
        for (const Key& k : KEYS) {
            if (k.not_setting) {
                if (!*k.not_setting || k.flag) bad += full_name(k) + " (not-setting needs a reason and no flag) ";
            } else {
                if (!k.help || !*k.help || !k.values || !*k.values || k.frontends == 0)
                    bad += full_name(k) + " (help, values and frontends needed) ";
            }
        }
        check("SETK-02", "a settable key has help, a values metavar and frontends; a not-setting key has a reason and no flag",
              bad.empty(), bad);
    }

    // SETK-03
    {
        std::set<std::string> got;
        for (const Key& k : KEYS) if (k.not_setting) got.insert(full_name(k));
        const std::set<std::string> want = {"config.version", "recent.files", "input.joy1_device_name",
                                            "input.joy2_device_name", "debugger.size", "debugger.position"};
        check("SETK-03", "exactly six keys are not settings: config.version, recent.files, the two controller names, debugger.size and debugger.position",
              got == want);
    }

    // SETK-04
    {
        std::string bad;
        for (const Key& k : KEYS) {
            if (!k.flag) continue;
            bool documented = false;
            for (const cli::Option& o : cli::OPTIONS)
                if (std::string(o.name) == k.flag && o.doc == cli::Doc::Documented) documented = true;
            if (!documented) bad += std::string(k.flag) + " ";
        }
        check("SETK-04", "every dedicated flag a key names is a Documented spelling in cli::OPTIONS", bad.empty(), bad);
    }

    // SETK-05
    {
        std::string bad;
        for (const Key& k : KEYS) {
            if (is_family(k)) continue;
            if (config_keys::find(k.section, k.name) != &k) bad += full_name(k) + " ";
        }
        const int n = jnext::dbgkeys::ACTION_COUNT;
        for (int i = 0; i < n; ++i) {
            const char* id = jnext::dbgkeys::action_table()[i].id;
            if (config_keys::find("debugger_keys", id) != key_of(KeyId::DebuggerKey)) bad += std::string("debugger_keys.") + id + " ";
        }
        if (config_keys::find("debugger_keys", "nope") != nullptr) bad += "debugger_keys.nope ";
        if (config_keys::find("debugger_keys", "ACTION") != nullptr) bad += "debugger_keys.ACTION ";
        if (config_keys::find("startup", "nope") != nullptr) bad += "startup.nope ";
        if (config_keys::find("nope", "enabled") != nullptr) bad += "nope.enabled ";
        check("SETK-05", "find() resolves every table key and the 12 debugger action ids, and nothing else",
              bad.empty() && n == 12, bad);
    }

    // SETK-06
    {
        const char* refused[] = {"foo", "=1", ".x=1", "startup=1", "nope.x=1", "startup.nope=1",
                                 "debugger.size=1, 1", "recent.files=a", "debugger_keys.nope=F5"};
        std::string bad;
        for (const char* arg : refused) {
            config_keys::Assignment out;
            out.text = "sentinel";
            std::string why;
            const bool ok = config_keys::parse_assignment(arg, out, why);
            if (ok || why.find(arg) == std::string::npos || out.text != "sentinel" || out.id != KeyId::Count)
                bad += std::string(arg) + " ";
        }
        config_keys::Assignment out;
        std::string why;
        if (config_keys::parse_assignment(nullptr, out, why)) bad += "(null) ";
        check("SETK-06", "malformed, unknown and not-setting assignments are refused, the reason names what was typed, the output is untouched",
              bad.empty(), bad);
    }

    // SETK-07
    {
        std::string bad;
        if (canon("startup.silent=true") != "true") bad += "true ";
        if (canon("startup.silent=false") != "false") bad += "false ";
        if (canon("startup.silent=TRUE") != "true") bad += "TRUE ";
        for (const char* v : {"", "1", "0", "yes", "maybe"})
            if (canon(std::string("startup.silent=") + v) != "<refused>") bad += std::string("accepts '") + v + "' ";
        check("SETK-07", "a Bool is true or false in any case (canonical lower case); 1, 0, yes, maybe and empty are refused",
              bad.empty(), bad);
    }

    // SETK-08
    {
        std::string bad;
        int n = 0;
        for (const Key& k : KEYS) {
            if (k.type != Type::Int || k.not_setting) continue;
            ++n;
            const long lo = static_cast<long>(k.lo), hi = static_cast<long>(k.hi);
            const std::string f = full_name(k) + "=";
            if (canon(f + std::to_string(lo)) != std::to_string(lo)) bad += f + "lo ";
            if (canon(f + std::to_string(hi)) != std::to_string(hi)) bad += f + "hi ";
            if (canon(f + std::to_string(lo - 1)) != "<refused>") bad += f + "lo-1 ";
            if (canon(f + std::to_string(hi + 1)) != "<refused>") bad += f + "hi+1 ";
            for (const char* v : {"x", "1.5", ""})
                if (canon(f + v) != "<refused>") bad += f + "'" + v + "' ";
        }
        check("SETK-08", "for every Int key its own lo and hi are accepted, lo-1, hi+1, x, 1.5 and empty are refused",
              bad.empty() && n == 4, bad + " keys=" + std::to_string(n));
    }

    // SETK-09
    {
        std::string bad;
        if (canon("audio.gain_db=-24") != "-24") bad += "-24 ";
        if (canon("audio.gain_db=24") != "24") bad += "24 ";
        if (canon("audio.gain_db=1.5") != "1.5") bad += "1.5 ";
        for (const char* v : {"-24.1", "24.1", "nan", "inf", "x", ""})
            if (canon(std::string("audio.gain_dac_db=") + v) != "<refused>") bad += std::string("accepts '") + v + "' ";
        check("SETK-09", "a Float is a finite number in -24..24 in the text AppConfig writes; out of range, nan, inf and junk are refused",
              bad.empty(), bad);
    }

    // SETK-10
    {
        std::string bad;
        int n = 0;
        for (const Key& k : KEYS) {
            if ((k.type != Type::Word && k.type != Type::Machine) || k.not_setting) continue;
            ++n;
            const std::string f = full_name(k) + "=";
            for (int i = 0;; ++i) {
                const std::string w(config_keys::word_at(k.values, i));
                if (w.empty()) break;
                if (canon(f + upper(w)) != w) bad += f + w + " ";
            }
            if (canon(f + "nonsense") != "<refused>") bad += f + "nonsense ";
        }
        check("SETK-10", "for every Word and Machine key each listed word is accepted in upper case and canonicalises to itself; nonsense is refused",
              bad.empty() && n == 6, bad + " keys=" + std::to_string(n));
    }

    // SETK-11
    {
        std::string bad;
        if (canon("paths.last_load_dir= /a b, c ") != " /a b, c ") bad += "path ";
        if (canon("paths.last_load_dir=") != "") bad += "empty path ";
        if (canon("screenshot.quick_dir=a,b") != "a,b") bad += "comma path ";
        if (canon("nextpi.dir=  /x y  ") != "/x y") bad += "trimmed ";
        if (canon("nextpi.qemu_args=\t-a  ") != "-a") bad += "trimmed args ";
        check("SETK-11", "Text and Path keys are verbatim (spaces, commas, empty kept); TrimmedText keys are trimmed",
              bad.empty(), bad);
    }

    // SETK-12
    {
        std::string bad;
        if (canon("esp.allowed_hosts=A.example,b.example,a.example") != "A.example,b.example") bad += "dedupe ";
        if (canon("esp.allowed_hosts=") != "") bad += "empty ";
        for (const char* v : {"a,,b", ",a", "a,", " "})
            if (canon(std::string("esp.allowed_hosts=") + v) != "<refused>") bad += std::string("accepts '") + v + "' ";
        check("SETK-12", "a HostList goes through EspHostPolicy::add (case-insensitive de-duplication, spelling kept); empty is the empty list; a blank entry is refused",
              bad.empty(), bad);
    }

    // SETK-13
    {
        const std::string raw = "0123456789ABCDEF0123456789abcdef#2";
        std::string want;
        const bool norm = normalize_joy_device_id(raw, want);
        std::string bad;
        if (!norm || want != "0123456789abcdef0123456789abcdef#2") bad += "oracle ";
        if (canon("input.joy1_device=" + raw) != want) bad += "id ";
        if (canon("input.joy2_device=0123456789abcdef0123456789abcdef#1") != "0123456789abcdef0123456789abcdef") bad += "#1 ";
        if (canon("input.joy1_device=") != "") bad += "automatic ";
        if (canon("input.joy1_device=xyz") != "<refused>") bad += "xyz ";
        check("SETK-13", "a DeviceId is normalised as normalize_joy_device_id does; empty is Automatic; junk is refused",
              bad.empty(), bad);
    }

    // SETK-14
    {
        std::string bad;
        if (canon("snapshot.quick_keep=unlimited") != "unlimited") bad += "unlimited ";
        if (canon("snapshot.quick_keep=1") != "1") bad += "1 ";
        if (canon("snapshot.quick_keep=1000000") != "1000000") bad += "max ";
        for (const char* v : {"0", "-1", "1000001", "x", ""})
            if (canon(std::string("snapshot.quick_keep=") + v) != "<refused>") bad += std::string("accepts '") + v + "' ";
        check("SETK-14", "Keep accepts unlimited and 1..1000000; 0, -1, 1000001 and junk are refused",
              bad.empty(), bad);
    }

    // SETK-15
    {
        std::string bad;
        if (canon("debugger_keys.run=ctrl+f10") != "Ctrl+F10") bad += "ctrl+f10 ";
        if (canon("debugger_keys.pause=none") != "none") bad += "none ";
        if (canon("debugger_keys.run=Ctrl+") != "<refused>") bad += "Ctrl+ ";
        config_keys::Assignment a;
        std::string why;
        if (!config_keys::parse_assignment("debugger_keys.step_out=F8", a, why) ||
            a.id != KeyId::DebuggerKey || a.family_name != "step_out")
            bad += "family_name ";
        check("SETK-15", "a Combo parses through dbgkeys::parse_combo (ctrl+f10 -> Ctrl+F10, none -> none); Ctrl+ is refused; the action id is kept",
              bad.empty(), bad);
    }

    // SETK-16
    {
        std::string bad;
        if (canon("nextpi.qemu_args=-a=b c") != "-a=b c") bad += "first = ";
        if (canon("paths.last_load_dir=a=b=c") != "a=b=c") bad += "path = ";
        check("SETK-16", "the assignment splits at the FIRST '=': the value keeps every later one", bad.empty(), bad);
    }

    // ---- apply_overrides -------------------------------------------------
    using jnext::dbgkeys::Action;
    using jnext::dbgkeys::Combo;
    using jnext::dbgkeys::Key;
    using jnext::dbgkeys::Keymap;
    using jnext::dbgkeys::MOD_NONE;
    using Pairs = std::vector<std::pair<std::string, std::string>>;
    const auto bare = [](jnext::dbgkeys::Key k) { return Combo{MOD_NONE, k}; };

    // DK-80
    {
        std::string bad, why;
        Keymap m;
        if (!jnext::dbgkeys::apply_overrides(m, Pairs{{"run", "F10"}, {"pause", "none"}}, why)) bad += "apply:" + why + " ";
        if (m.combo(Action::Run) != bare(Key::F10)) bad += "run ";
        if (m.combo(Action::Pause).bound()) bad += "pause ";
        if (m.combo(Action::StepOver) != Keymap().combo(Action::StepOver)) bad += "step_over moved ";
        Keymap before;
        Keymap m2 = before;
        if (jnext::dbgkeys::apply_overrides(m2, Pairs{{"run", "F10"}, {"nope", "F11"}}, why) || m2 != before) bad += "unknown ";
        check("DK-80", "apply_overrides: a CLI combo replaces the binding, none unbinds it, an unknown action is refused and leaves the map alone",
              bad.empty(), bad);
    }

    // DK-81
    {
        std::string bad, why;
        for (const char* v : {"A", "Alt+1"}) {
            Keymap m;
            if (jnext::dbgkeys::apply_overrides(m, Pairs{{"run", v}}, why) || why.empty()) bad += std::string(v) + " ";
        }
        {
            Keymap m;
            jnext::dbgkeys::apply_overrides(m, Pairs{{"run", "Alt+1"}}, why);
            if (why.find("script host keys") == std::string::npos) bad += "reason(" + why + ") ";
        }
        Keymap a, b;
        if (!jnext::dbgkeys::apply_overrides(a, Pairs{{"run", "Ctrl+1"}}, why, false)) bad += "Ctrl+1 refused with Alt host ";
        if (jnext::dbgkeys::apply_overrides(b, Pairs{{"run", "Ctrl+1"}}, why, true)) bad += "Ctrl+1 accepted with Ctrl host ";
        Keymap c;
        if (!jnext::dbgkeys::apply_overrides(c, Pairs{{"run", "Alt+1"}}, why, true)) bad += "Alt+1 refused with Ctrl host ";
        check("DK-81", "apply_overrides: an illegal combo (bare A, the host script keys) is refused with validate_combo's reason, for either host modifier",
              bad.empty(), bad);
    }

    // DK-82
    {
        std::string bad, why;
        Keymap m;
        if (jnext::dbgkeys::apply_overrides(m, Pairs{{"run", "F7"}}, why)) bad += "default clash accepted ";
        if (why.find("'run'") == std::string::npos || why.find("'step_over'") == std::string::npos) bad += "names(" + why + ") ";
        if (m != Keymap()) bad += "map changed ";
        std::vector<jnext::dbgkeys::LoadIssue> issues;
        Keymap saved = jnext::dbgkeys::build_keymap(Pairs{{"trace_toggle", "F11"}}, issues);
        if (jnext::dbgkeys::apply_overrides(saved, Pairs{{"step_out", "F11"}}, why)) bad += "saved clash accepted ";
        if (why.find("'step_out'") == std::string::npos || why.find("'trace_toggle'") == std::string::npos) bad += "names2(" + why + ") ";
        check("DK-82", "apply_overrides: a combo held by another action's default, or by a saved redefinition, is refused naming both actions",
              bad.empty(), bad);
    }

    // DK-83
    {
        std::string bad, why;
        Keymap m;
        if (jnext::dbgkeys::apply_overrides(m, Pairs{{"run", "F10"}, {"pause", "F10"}}, why)) bad += "accepted ";
        if (why.find("'run'") == std::string::npos || why.find("'pause'") == std::string::npos) bad += "names(" + why + ") ";
        check("DK-83", "apply_overrides: two CLI entries for one combo are refused", bad.empty(), bad);
    }

    // DK-84
    {
        std::string bad, why;
        Keymap a, b;
        if (!jnext::dbgkeys::apply_overrides(a, Pairs{{"run", "F7"}, {"step_over", "none"}}, why)) bad += "order1:" + why + " ";
        if (!jnext::dbgkeys::apply_overrides(b, Pairs{{"step_over", "none"}, {"run", "F7"}}, why)) bad += "order2:" + why + " ";
        for (const Keymap* m : {&a, &b})
            if (m->combo(Action::Run) != bare(Key::F7) || m->combo(Action::StepOver).bound()) bad += "result ";
        check("DK-84", "apply_overrides: freeing a combo in the same list works in either order (only the final map is checked)",
              bad.empty(), bad);
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
