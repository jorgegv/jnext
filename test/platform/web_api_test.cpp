// The browser build's JavaScript API, native half (GH #465): the WEB_OPTIONS table and
// build_argv() (src/platform/web_api.h), the volume mapping, the man page's browser
// section diffed against the tables, and the SdlApp load hook the page's "ready" rests on.
//
// No VHDL: this is host code. Oracles: the two tables the options bind to
// (cli_options.h, config_keys.h), the refusal functions of the browser build, the
// contract in the man page's THE BROWSER BUILD section, and the documented load
// semantics of SdlApp::tick() (a pending load is applied when its countdown reaches 0).

#include "platform/web_api.h"
#include "platform/browser_page.h"
#include "platform/sdl_app.h"
#include "audio/mixer.h"
#include "../test_portable.h"
#include "../row_id.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef JNEXT_MAN_SRC
#error "JNEXT_MAN_SRC must name doc/man/jnext.1.md"
#endif
#ifndef JNEXT_TAP_FIXTURE
#error "JNEXT_TAP_FIXTURE must name test/00regression/tap/rom_charset_test.tap"
#endif

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Argv = std::vector<std::string>;

std::string join(const Argv& v)
{
    std::string s = "[";
    for (const auto& e : v) s += (s.size() > 1 ? ", '" : "'") + e + "'";
    return s + "]";
}

struct Result { bool ok; Argv argv; std::string why; };
Result run(const std::string& json)
{
    Result r{};
    r.ok = web_api::build_argv(json, r.argv, r.why);
    return r;
}

const std::string HEX = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const std::string IMG = "\"image\":{\"path\":\"/sdcard/image.img\",\"sha256\":\"" + HEX + "\"}";

bool refused_naming(const std::string& json, const char* name)
{
    const Result r = run(json);
    return !r.ok && r.argv.empty() && r.why.find(name) != std::string::npos;
}

// ---- the man page --------------------------------------------------------------------------
std::vector<std::string> man_lines()
{
    std::ifstream in(JNEXT_MAN_SRC);
    std::vector<std::string> v;
    std::string l;
    while (std::getline(in, l)) v.push_back(l);
    return v;
}

// The lines of subsection `sub` of the section `sect` ("# ..." / "## ..."), without headings.
std::vector<std::string> man_sub(const std::string& sect, const std::string& sub)
{
    std::vector<std::string> out;
    bool in_sect = false, in_sub = false;
    for (const std::string& l : man_lines()) {
        if (l.compare(0, 2, "# ") == 0) { in_sect = (l == sect); in_sub = false; continue; }
        if (l.compare(0, 3, "## ") == 0) { in_sub = in_sect && l == sub; continue; }
        if (in_sub) out.push_back(l);
    }
    return out;
}

std::vector<std::string> runs(const std::string& line, const std::string& delim)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < line.size();) {
        const size_t a = line.find(delim, i);
        if (a == std::string::npos) break;
        const size_t b = line.find(delim, a + delim.size());
        if (b == std::string::npos) break;
        out.push_back(line.substr(a + delim.size(), b - a - delim.size()));
        i = b + delim.size();
    }
    return out;
}

std::string unescape(std::string s)
{
    if (s.compare(0, 2, "\\-") == 0) s.erase(0, 1);
    return s;
}

struct JsEntry { std::vector<std::string> names; std::string text; };

std::vector<JsEntry> man_js_entries()
{
    std::vector<JsEntry> out;
    for (const std::string& l : man_sub("# THE BROWSER BUILD", "## JavaScript options")) {
        if (!l.empty() && l[0] == '`') {
            JsEntry e;
            // names are the backtick runs before the first italic metavar
            const std::string head = l.substr(0, l.find(" *"));
            e.names = runs(head, "`");
            e.text = l + "\n";
            out.push_back(e);
        } else if (!out.empty()) {
            out.back().text += l + "\n";
        }
    }
    return out;
}

bool has_bold(const std::string& text, const std::string& spelling)
{
    for (const std::string& b : runs(text, "**"))
        if (unescape(b) == spelling) return true;
    return false;
}

// ---- the SdlApp hook ------------------------------------------------------------------------
std::unique_ptr<SdlApp> make_app(bool& ok)
{
    jtp::set_env("SDL_VIDEODRIVER", "dummy");
    jtp::set_env("SDL_AUDIODRIVER", "dummy");
    auto app = std::make_unique<SdlApp>();   // heap: Emulator is large (1 MB Windows stack)
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.silent = true;
    app->set_config(cfg);
    ok = app->init(0, nullptr);
    return app;
}

struct Hook {
    std::vector<std::pair<std::string, bool>> calls;
    std::vector<int> at_tick;
    int tick = 0;
};

void tick_n(SdlApp& app, Hook& h, int n)
{
    for (int i = 0; i < n; ++i) {
        uint32_t s = 0;
        ++h.tick;
        app.tick(s);
    }
}

}  // namespace

int main()
{
    std::printf("Browser JavaScript API test (GH #465)\n");
    std::printf("====================================================\n");

    // --- WAPI-01: every Flag/Image row names a Documented flag of arity 1 ---------------------
    {
        bool all = true;
        std::string bad;
        int n = 0;
        for (const web_api::Option& o : web_api::WEB_OPTIONS) {
            for (auto id : {o.opt, o.opt2}) {
                if (!id) continue;
                ++n;
                const cli::Option* found = nullptr;
                for (const cli::Option& c : cli::OPTIONS)
                    if (c.id == *id && c.doc == cli::Doc::Documented) found = &c;
                if (!found || found->arity != 1) { all = false; bad += std::string(" ") + o.name; }
            }
            if ((o.kind == web_api::Kind::Image) != o.opt2.has_value()) { all = false; bad += " opt2:" + std::string(o.name); }
            if ((o.kind == web_api::Kind::Image || o.kind == web_api::Kind::Flag) != o.opt.has_value()) { all = false; bad += " opt:" + std::string(o.name); }
        }
        check("WAPI-01", "every Flag and Image row's OptId has a Documented spelling in cli::OPTIONS, arity 1",
              all && n == 4, bad + " n=" + std::to_string(n));
    }

    // --- WAPI-02: the SetKey row's key is settable, on the SDL frontend, of the JS type --------
    {
        bool all = true;
        int n = 0;
        for (const web_api::Option& o : web_api::WEB_OPTIONS) {
            if (o.kind != web_api::Kind::SetKey) continue;
            ++n;
            const config_keys::Key* k = nullptr;
            for (const config_keys::Key& c : config_keys::KEYS) if (c.id == *o.key) k = &c;
            if (!k || k->not_setting != nullptr || !(k->frontends & config_keys::SDL) ||
                k->type != config_keys::Type::Bool)   // muted is a JS boolean
                all = false;
        }
        check("WAPI-02", "every SetKey row's key is settable, has the SDL frontend bit, and is Bool (as `muted`)",
              all && n == 1);
    }

    // --- WAPI-03: nothing maps to a refused flag or key -----------------------------------------
    {
        bool clean = true;
        std::string bad;
        for (const web_api::Option& o : web_api::WEB_OPTIONS) {
            for (auto id : {o.opt, o.opt2})
                if (id && (cli::browser_refusal(*id) || browser_page::option_refusal(*id))) { clean = false; bad += o.name; }
            if (o.key) {
                for (const config_keys::Key& c : config_keys::KEYS)
                    if (c.id == *o.key && config_keys::browser_refusal(c)) { clean = false; bad += o.name; }
            }
        }
        check("WAPI-03", "no row maps to a spelling refused by cli::browser_refusal or browser_page::option_refusal, "
                         "or to a key refused by config_keys::browser_refusal", clean, bad);
    }

    // --- WAPI-04: the whole mapping, exactly ---------------------------------------------------
    {
        const Result r = run("{" + IMG + ",\"file\":\"/media/0-game.nex\",\"machine\":\"next\",\"muted\":false,"
                             "\"set\":{\"startup.cpu_speed\":\"3\"},\"args\":[\"--rtc\",\"2026-01-01 00:00:00\"],"
                             "\"width\":640,\"height\":512,\"onReady\":true}");
        const Argv want = {"--sdcard", "/sdcard/image.img", "--sdcard-sha256", HEX, "--machine", "next",
                           "--load", "/media/0-game.nex", "--set", "startup.silent=false",
                           "--set", "startup.cpu_speed=3", "--rtc", "2026-01-01 00:00:00"};
        check("WAPI-04", "a full options object becomes exactly the documented command line",
              r.ok && r.argv == want, r.ok ? join(r.argv) : r.why);
    }

    // --- WAPI-05: an unknown option is refused and named ---------------------------------------
    {
        const Result r = run("{" + IMG + ",\"machin\":\"next\"}");
        check("WAPI-05", "an unknown option is refused, the reason names it, argv is empty",
              !r.ok && r.argv.empty() && r.why.find("machin") != std::string::npos, r.why);
    }

    // --- WAPI-06: wrong types, each naming its option ------------------------------------------
    {
        struct Case { const char* name; std::string json; };
        const std::vector<Case> cases = {
            {"machine", "{" + IMG + ",\"machine\":5}"},
            {"muted", "{" + IMG + ",\"muted\":\"yes\"}"},
            {"set", "{" + IMG + ",\"set\":[]}"},
            {"a.b", "{" + IMG + ",\"set\":{\"a.b\":{}}}"},
            {"args", "{" + IMG + ",\"args\":\"x\"}"},
            {"args", "{" + IMG + ",\"args\":[1]}"},
            {"onReady", "{" + IMG + ",\"onReady\":5}"},
            {"image", "{\"image\":\"x\"}"},
            {"image", "{\"image\":{\"path\":1,\"sha256\":\"" + HEX + "\"}}"},
            {"file", "{" + IMG + ",\"file\":7}"},
            {"width", "{" + IMG + ",\"width\":true}"},
            {"wasmUrl", "{" + IMG + ",\"wasmUrl\":3}"},
            {"options", "[]"},
            {"options", "5"},
        };
        std::string bad;
        for (const Case& c : cases) if (!refused_naming(c.json, c.name)) bad += std::string(" ") + c.name;
        check("WAPI-06", "wrong types are refused, each naming its option", bad.empty(), bad);
    }

    // --- WAPI-07: `image` is required ----------------------------------------------------------
    {
        const Result r = run("{\"machine\":\"next\"}");
        check("WAPI-07", "no image: refused, \"image is required\"",
              !r.ok && r.why == "image is required", r.why);
    }

    // --- WAPI-08: set values as text; order named < set < args ---------------------------------
    {
        const Result v = run("{" + IMG + ",\"set\":{\"a.n\":28,\"a.b\":true,\"a.s\":\"x y\",\"a.f\":2.5}}");
        const Argv want_v = {"--sdcard", "/sdcard/image.img", "--sdcard-sha256", HEX,
                             "--set", "a.b=true", "--set", "a.f=2.5", "--set", "a.n=28", "--set", "a.s=x y"};
        const Result o = run("{" + IMG + ",\"args\":[\"--machine\",\"48k\"],\"set\":{\"startup.machine_type\":\"128k\"},"
                             "\"machine\":\"next\"}");
        const Argv want_o = {"--sdcard", "/sdcard/image.img", "--sdcard-sha256", HEX, "--machine", "next",
                             "--set", "startup.machine_type=128k", "--machine", "48k"};
        check("WAPI-08", "set values: 28 -> 28, true -> true, a string verbatim, in key order; named < set < args",
              v.ok && v.argv == want_v && o.ok && o.argv == want_o,
              (v.ok ? join(v.argv) : v.why) + " | " + (o.ok ? join(o.argv) : o.why));
    }

    // --- WAPI-09: the man page's JavaScript options, both ways ---------------------------------
    {
        const auto entries = man_js_entries();
        std::set<std::string> man_names, table_names;
        for (const JsEntry& e : entries) for (const auto& n : e.names) man_names.insert(n);
        std::string bad;
        for (const web_api::Option& o : web_api::WEB_OPTIONS) {
            table_names.insert(o.name);
            const JsEntry* e = nullptr;
            for (const JsEntry& c : entries) for (const auto& n : c.names) if (n == o.name) e = &c;
            if (!e) { bad += std::string(" missing:") + o.name; continue; }
            bool named = true;
            switch (o.kind) {
            case web_api::Kind::Image:
                named = has_bold(e->text, web_api::flag_spelling(*o.opt)) && has_bold(e->text, web_api::flag_spelling(*o.opt2));
                break;
            case web_api::Kind::Flag: named = has_bold(e->text, web_api::flag_spelling(*o.opt)); break;
            case web_api::Kind::SetKey: named = e->text.find("`" + web_api::key_spelling(*o.key) + "`") != std::string::npos; break;
            case web_api::Kind::SetMap: named = has_bold(e->text, "--set"); break;
            case web_api::Kind::Argv: break;
            case web_api::Kind::Host:
            case web_api::Kind::Callback: named = e->text.find("Not a jnext setting") != std::string::npos; break;
            }
            if (!named) bad += std::string(" unnamed:") + o.name;
        }
        for (const auto& n : man_names) if (!table_names.count(n)) bad += " extra:" + n;
        check("WAPI-09", "the man page's JavaScript options list exactly the WEB_OPTIONS names, both ways, "
                         "each naming its flag(s) or --set key", bad.empty() && !entries.empty(), bad);
    }

    // --- WAPI-10: the man page's refused list, both ways ---------------------------------------
    {
        std::set<std::string> man_flags, man_keys, want_flags, want_keys;
        for (const std::string& l : man_sub("# THE BROWSER BUILD", "## Not available in the browser")) {
            for (const auto& b : runs(l, "**")) if (unescape(b).compare(0, 2, "--") == 0) man_flags.insert(unescape(b));
            for (const auto& k : runs(l, "`")) man_keys.insert(k);
        }
        for (const cli::Option& o : cli::OPTIONS)
            if (o.doc == cli::Doc::Documented && (cli::browser_refusal(o.id) || browser_page::option_refusal(o.id)))
                want_flags.insert(o.name);
        for (const config_keys::Key& k : config_keys::KEYS)
            if (config_keys::browser_refusal(k)) want_keys.insert(std::string(k.section) + "." + k.name);
        std::string bad;
        for (const auto& f : want_flags) if (!man_flags.count(f)) bad += " missing:" + f;
        for (const auto& f : man_flags) if (!want_flags.count(f)) bad += " extra:" + f;
        for (const auto& f : want_keys) if (!man_keys.count(f)) bad += " missing:" + f;
        for (const auto& f : man_keys) if (!want_keys.count(f)) bad += " extra:" + f;
        check("WAPI-10", "the man page's refused list is exactly the flags refused by cli::browser_refusal / "
                         "browser_page::option_refusal and the --set keys refused by config_keys::browser_refusal",
              bad.empty() && !want_flags.empty() && !want_keys.empty(), bad);
    }

    // --- WAPI-11: the volume mapping -----------------------------------------------------------
    {
        const auto g1 = web_api::volume_to_gain_db(1.0), g5 = web_api::volume_to_gain_db(0.5),
                   g1t = web_api::volume_to_gain_db(0.1), g0 = web_api::volume_to_gain_db(0.0);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        check("WAPI-11", "volume_to_gain_db: 1 -> 0, 0.5 -> -6.0206, 0.1 -> -20, 0 -> kMuteDb; outside [0,1] and NaN refused",
              g1 && *g1 == 0.0 && g5 && std::fabs(*g5 + 6.0206) < 1e-3 && g1t && std::fabs(*g1t + 20.0) < 1e-9 &&
              g0 && *g0 == web_api::kMuteDb &&
              !web_api::volume_to_gain_db(-0.1) && !web_api::volume_to_gain_db(1.5) && !web_api::volume_to_gain_db(nan));
    }

    // --- WAPI-12: kMuteDb through the real mixer is silence ------------------------------------
    {
        Beeper beeper; TurboSound ts; Dac dac; Mixer mixer;
        beeper.set_ear(true); beeper.set_mic(true);
        dac.write_channel(0, 0xFF); dac.write_channel(1, 0xFF);
        mixer.set_output_gain_db(web_api::kMuteDb);
        mixer.generate_sample(beeper, ts, dac);
        Beeper b2; TurboSound t2; Dac d2;
        d2.write_channel(0, 0x00); d2.write_channel(1, 0x00);
        mixer.generate_sample(b2, t2, d2);
        int16_t s[4]{1, 1, 1, 1};
        const int n = mixer.read_samples(s, 2);
        check("WAPI-12", "at kMuteDb a full-scale positive and a full-scale negative source both come out as 0",
              n == 2 && s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0,
              "n=" + std::to_string(n));
    }

    // --- WAPI-13..16: the load hook of SdlApp ---------------------------------------------------
    const std::string TAP = JNEXT_TAP_FIXTURE;
    {
        bool ok = false;
        auto app = make_app(ok);
        Hook h;
        app->set_on_load_applied([&h](const std::string& f, bool good) {
            h.calls.emplace_back(f, good); h.at_tick.push_back(h.tick);
        });
        app->set_pending_load(TAP, 2);
        tick_n(*app, h, 2);
        const size_t before = h.calls.size();
        tick_n(*app, h, 1);
        const size_t after = h.calls.size();
        tick_n(*app, h, 3);
        const bool once = h.calls.size() == 1 && h.calls[0].first == TAP && h.calls[0].second;
        app->shutdown();
        check("WAPI-13", "set_pending_load(file, 2): the hook fires exactly once, with (file, true), on the tick the "
                         "countdown reaches 0, and not before",
              ok && before == 0 && after == 1 && once && h.at_tick[0] == 3,
              "before=" + std::to_string(before) + " after=" + std::to_string(after) + " n=" + std::to_string(h.calls.size()));
    }
    {
        bool ok = false;
        auto app = make_app(ok);
        Hook h;
        app->set_on_load_applied([&h](const std::string& f, bool good) { h.calls.emplace_back(f, good); });
        const std::string missing = TAP + ".does-not-exist.tap";
        app->set_pending_load(missing, 1);
        tick_n(*app, h, 5);
        app->shutdown();
        check("WAPI-14", "a missing file: the hook fires once with (file, false)",
              ok && h.calls.size() == 1 && h.calls[0].first == missing && !h.calls[0].second,
              "n=" + std::to_string(h.calls.size()));
    }
    {
        bool ok = false;
        auto app = make_app(ok);
        Hook h;
        app->set_on_load_applied([&h](const std::string& f, bool good) { h.calls.emplace_back(f, good); });
        tick_n(*app, h, 5);
        app->shutdown();
        check("WAPI-15", "no load: the hook never fires over 5 ticks", ok && h.calls.empty());
    }
    {
        bool ok = false;
        auto app = make_app(ok);
        Hook h;
        app->set_on_load_applied([&h](const std::string& f, bool good) { h.calls.emplace_back(f, good); });
        app->set_pending_load(TAP, 0);
        tick_n(*app, h, 2);
        const size_t first = h.calls.size();
        app->cold_boot(TAP);
        const bool pending = app->pending_load_file() == TAP;
        tick_n(*app, h, 8);
        app->shutdown();
        check("WAPI-16", "cold_boot(file) after a first load: the hook fires again for the reboot's load",
              ok && first == 1 && pending && h.calls.size() == 2 && h.calls[1].second,
              "first=" + std::to_string(first) + " n=" + std::to_string(h.calls.size()));
    }

    // --- WAPI-17: an all-zero digest never goes through ----------------------------------------
    {
        const std::string zero(64, '0');
        const Result r = run("{\"image\":{\"path\":\"/sdcard/image.img\",\"sha256\":\"" + zero + "\"}}");
        check("WAPI-17", "an image with an all-zero sha256 is refused (the cache's \"no digest\" never reaches the core)",
              !r.ok && r.argv.empty() && r.why.find("all zeros") != std::string::npos, r.why);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %d  Passed: %d  Failed: %d  Skipped: 0\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
