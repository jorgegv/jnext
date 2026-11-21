// ===========================================================================
// tools/jnext-bug-view.py (GH #492 stage 3): the viewer of a bug report.
//
// NO VHDL ORACLE: a host-side tool. The oracle is the report itself: reports are built in memory with
// bug_report::build() from a machine with no card (memory included) and from a synthetic card (memory
// withheld), and everything the viewer shows is compared with the members it was given. The viewer is
// run as `python3 -I tools/jnext-bug-view.py` in a child with an EXPLICIT minimal environment (LANG=C,
// PATH=/usr/bin:/bin, no HOME), in a scratch directory, with --no-open.
//
// Posix only (it forks python3): `# os: posix` in test/unit-tests.conf.
// Run: ./build/test/bug_view_test
// ===========================================================================
#include "core/emulator.h"
#include "core/fat32_image.h"
#include "core/log.h"
#include "debug/debugger.h"
#include "platform/bug_report.h"
#include "save/zip_archive.h"
#include "third_party/nlohmann-json/nlohmann/json.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace dbg = jnext::dbg;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

std::string fmtf(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else      { ++g_fail; std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str()); }
}

std::string g_tmp;
std::string tmp_path(const std::string& n) { return (fs::path(g_tmp) / n).string(); }

void write_bytes(const std::string& path, const std::vector<uint8_t>& b) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

std::vector<uint8_t> noise(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed ? seed : 1;
    for (size_t i = 0; i < n; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; v[i] = static_cast<uint8_t>(x >> 11); }
    return v;
}

// ---- running the viewer ------------------------------------------------------

struct Run { int rc = -1; std::string out, err; };

Run run_viewer_raw(const std::vector<std::string>& args);

// Every call that could write a page gets an -o inside the scratch directory, so even a viewer that
// wrongly accepts a file leaves nothing in /tmp (a page written without -o lands in a folder of its own).
Run run_viewer(std::vector<std::string> args) {
    static int n = 0;
    bool o = false, text = false;
    for (const auto& a : args) { if (a == "-o") o = true; if (a == "--text") text = true; }
    if (!o && !text) { args.push_back("-o"); args.push_back(tmp_path("auto-" + std::to_string(++n) + ".html")); }
    return run_viewer_raw(args);
}

Run run_viewer_raw(const std::vector<std::string>& args) {
    int po[2], pe[2];
    if (pipe(po) != 0 || pipe(pe) != 0) return {};
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(po[1], 1); dup2(pe[1], 2);
        close(po[0]); close(po[1]); close(pe[0]); close(pe[1]);
        if (chdir(g_tmp.c_str()) != 0) _exit(126);
        alarm(60);   // a hung child dies
        const std::string script = std::string(JNEXT_TOOLS_DIR) + "/jnext-bug-view.py";
        std::vector<std::string> a = { "python3", "-I", script };
        a.insert(a.end(), args.begin(), args.end());
        std::vector<char*> argv;
        for (auto& s : a) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        // The environment is explicit and minimal: the output must not depend on the user's.
        char lang[] = "LANG=C", path[] = "PATH=/usr/bin:/bin";
        char* envp[] = { lang, path, nullptr };
        execve("/usr/bin/python3", argv.data(), envp);
        execve("/bin/python3", argv.data(), envp);
        _exit(127);
    }
    close(po[1]); close(pe[1]);
    Run r;
    auto drain = [](int fd, std::string& s) {
        char buf[65536];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) s.append(buf, static_cast<size_t>(n));
        close(fd);
    };
    // Both pipes: stderr is small; read stdout first, then stderr (the child writes little to stderr).
    drain(po[0], r.out);
    drain(pe[0], r.err);
    int st = 0;
    waitpid(pid, &st, 0);
    r.rc = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    return r;
}

// ---- reports ---------------------------------------------------------------------

struct Session { std::unique_ptr<Emulator> emu; std::unique_ptr<dbg::Debugger> d; };

Session open_session(const EmulatorConfig& cfg) {
    Session s;
    s.emu = std::make_unique<Emulator>();
    s.emu->init(cfg);
    s.d = std::make_unique<dbg::Debugger>(*s.emu);
    return s;
}

std::vector<uint8_t> g_pat_prog;
std::string g_prog_path;

Session nocard_session() {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.no_sdcard = true;
    cfg.load_file = "program.nex";
    Session s = open_session(cfg);
    s.emu->port().out(0x243B, 0x03);
    s.emu->port().out(0x253B, 0x01);
    s.emu->inject_binary(g_prog_path, 0x8000, 0x8000);
    return s;
}

std::string g_card;
bool make_card(const std::string& path) {
    const uint32_t part_lba = 63, total_sectors = 70000;
    const uint64_t bytes = static_cast<uint64_t>(part_lba + total_sectors) * 512;
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        std::vector<uint8_t> mbr(512, 0);
        uint8_t* pe = mbr.data() + 0x1BE;
        pe[4] = 0x0C;
        for (int i = 0; i < 4; ++i) pe[8 + i] = static_cast<uint8_t>(part_lba >> (8 * i));
        for (int i = 0; i < 4; ++i) pe[12 + i] = static_cast<uint8_t>(total_sectors >> (8 * i));
        mbr[510] = 0x55; mbr[511] = 0xAA;
        f.write(reinterpret_cast<const char*>(mbr.data()), 512);
        f.seekp(static_cast<std::streamoff>(bytes - 1));
        const char z = 0;
        f.write(&z, 1);
        if (!f.good()) return false;
    }
    Fat32Node next; next.name = "NEXT"; next.is_dir = true;
    Fat32Node rom; rom.name = "48.rom"; rom.data = noise(0x4000, 11);
    next.children.push_back(rom);
    Fat32Node machines; machines.name = "MACHINES"; machines.is_dir = true;
    machines.children.push_back(next);
    Fat32Tree tree;
    tree.root.push_back(machines);
    Fat32WriteOptions opts;
    opts.au_size = 512;
    std::string err;
    return fat32_format_and_populate(path, part_lba, total_sectors, tree, err, opts);
}

Session card_session() {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image = g_card;
    cfg.sd_card_readonly = true;
    cfg.load_file = "program.nex";
    Session s = open_session(cfg);
    s.emu->port().out(0x243B, 0x03);
    s.emu->port().out(0x253B, 0x01);
    s.emu->inject_binary(g_prog_path, 0x8000, 0x8000);
    return s;
}

std::vector<uint8_t> build_report(Session& s) {
    bug_report::Inputs in;
    in.argv = { "jnext", "--headless" };
    in.frontend = "headless";
    in.toolkit = "test";
    bug_report::Outcome out;
    return bug_report::build(*s.emu, *s.d, in, out);
}

using Members = std::vector<std::pair<std::string, std::vector<uint8_t>>>;

Members members_of(const std::vector<uint8_t>& zip) {
    Members m;
    jnext::zip::Reader r;
    std::string why;
    if (!r.open(zip.data(), zip.size(), why)) return m;
    for (const auto& e : r.entries()) {
        std::vector<uint8_t> b;
        r.read(e.name, b, why);
        m.push_back({e.name, b});
    }
    return m;
}

std::vector<uint8_t> zip_of(const Members& m) {
    jnext::zip::Writer w;
    std::string why;
    for (const auto& e : m) w.add(e.first, e.second.data(), e.second.size(), jnext::zip::Method::Deflate, why);
    std::vector<uint8_t> out;
    w.finish(out, why);
    return out;
}

std::vector<uint8_t>* find(Members& m, const std::string& n) {
    for (auto& e : m) if (e.first == n) return &e.second;
    return nullptr;
}

json member_json(Members& m, const std::string& n) {
    auto* b = find(m, n);
    return b ? json::parse(std::string(b->begin(), b->end()), nullptr, false) : json();
}

void put_json(Members& m, const std::string& n, const json& j) {
    const std::string t = j.dump(2);
    if (auto* b = find(m, n)) *b = std::vector<uint8_t>(t.begin(), t.end());
    else m.push_back({n, std::vector<uint8_t>(t.begin(), t.end())});
}

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string html_of(const std::string& zip_path, Run* r_out = nullptr, const std::string& name = "out.html") {
    const std::string out = tmp_path(name);
    Run r = run_viewer({zip_path, "-o", out, "--no-open"});
    if (r_out) *r_out = r;
    return r.rc == 0 ? slurp(out) : std::string();
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string hex_row(const std::vector<uint8_t>& d, size_t off) {
    std::string s;
    char b[8];
    for (size_t i = 0; i < 16 && off + i < d.size(); ++i) { std::snprintf(b, sizeof b, i ? " %02X" : "%02X", d[off + i]); s += b; }
    return s;
}

// A standard base64 decoder, for the data: URLs.
std::vector<uint8_t> b64decode(const std::string& t) {
    std::vector<uint8_t> out;
    int val = 0, bits = -8;
    for (char c : t) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 0) { out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

}  // namespace

int main() {
    Log::init();
    Log::parse_levels("warn");
    g_tmp = jtp::make_temp_dir("jnext-bugview-test-");
    if (g_tmp.empty()) { std::printf("FATAL: no scratch directory\n"); return 1; }
    std::set_terminate([] { std::error_code e; fs::remove_all(g_tmp, e); std::abort(); });

    g_pat_prog = noise(0x300, 303);
    g_prog_path = tmp_path("prog.bin");
    write_bytes(g_prog_path, g_pat_prog);
    g_card = tmp_path("fake-card.img");
    const bool card_ok = make_card(g_card);

    Session sn = nocard_session();
    sn.emu->run_frame();
    const std::vector<uint8_t> nocard_zip = build_report(sn);
    const std::string nocard_path = tmp_path("nocard.zip");
    write_bytes(nocard_path, nocard_zip);
    Session sc = card_session();
    const std::vector<uint8_t> card_zip = build_report(sc);
    const std::string card_path = tmp_path("card.zip");
    write_bytes(card_path, card_zip);
    Members nocard = members_of(nocard_zip), card = members_of(card_zip);
    std::printf("fixtures: no-card %zu members, card %zu members, card image %s\n", nocard.size(), card.size(), card_ok ? "built" : "FAILED");

    // BV-01
    {
        const std::string p = tmp_path("notazip.zip");
        write_bytes(p, noise(500, 7));
        Run r = run_viewer({p, "--no-open"});
        check("BV-01", "a file that is not a zip is refused: exit 2, the message names the file",
              r.rc == 2 && has(r.err, "notazip.zip") && r.err.find("Traceback") == std::string::npos, fmtf("rc=%d err=%s", r.rc, r.err.c_str()));
    }
    // BV-02
    {
        Members nomanifest;
        for (auto& e : nocard) if (e.first != "manifest.json") nomanifest.push_back(e);
        write_bytes(tmp_path("nomanifest.zip"), zip_of(nomanifest));
        Members wrongkind = nocard;
        json m = member_json(wrongkind, "manifest.json");
        m["kind"] = "something-else";
        put_json(wrongkind, "manifest.json", m);
        write_bytes(tmp_path("wrongkind.zip"), zip_of(wrongkind));
        Run a = run_viewer({tmp_path("nomanifest.zip"), "--no-open"});
        Run b = run_viewer({tmp_path("wrongkind.zip"), "--no-open"});
        check("BV-02", "a zip with no manifest, and one of another kind, are refused (exit 2)",
              a.rc == 2 && b.rc == 2 && has(a.err, "manifest") && has(b.err, "kind"), fmtf("a=%d b=%d", a.rc, b.rc));
    }
    // BV-03
    {
        Members newer = nocard;
        json m = member_json(newer, "manifest.json");
        m["format_version"] = 2;
        put_json(newer, "manifest.json", m);
        write_bytes(tmp_path("newer.zip"), zip_of(newer));
        Run r = run_viewer({tmp_path("newer.zip"), "--no-open"});
        check("BV-03", "format_version 2 is refused as newer (exit 2)", r.rc == 2 && has(r.err, "newer"), fmtf("rc=%d %s", r.rc, r.err.c_str()));
    }
    // BV-04
    {
        Members missing;
        for (auto& e : nocard) if (e.first != "log.txt") missing.push_back(e);
        write_bytes(tmp_path("missing.zip"), zip_of(missing));
        Run r = run_viewer({tmp_path("missing.zip"), "--no-open"});
        check("BV-04", "a declared member that is missing is refused, and named (exit 2)",
              r.rc == 2 && has(r.err, "log.txt"), fmtf("rc=%d %s", r.rc, r.err.c_str()));
    }
    // BV-05: a 65 MiB member declared in a real zip (zeros deflate to ~64 KB), made by python with a minimal env
    {
        const std::string p = tmp_path("bomb.zip");
        {
            const std::string script = std::string("import zipfile,json,sys\n") +
                "z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED)\n"
                "z.writestr('manifest.json',json.dumps({'kind':'jnext-bug-report','format_version':1,'members':[]}))\n"
                "z.writestr('memory/ram.bin',bytes(65*1024*1024))\nz.close()\n";
            const std::string sp = tmp_path("mkbomb.py");
            { std::ofstream f(sp); f << script; }
            pid_t pid = fork();
            if (pid == 0) {
                char lang[] = "LANG=C", path[] = "PATH=/usr/bin:/bin";
                char* envp[] = { lang, path, nullptr };
                char* argv[] = { const_cast<char*>("python3"), const_cast<char*>("-I"), const_cast<char*>(sp.c_str()),
                                 const_cast<char*>(p.c_str()), nullptr };
                execve("/usr/bin/python3", argv, envp);
                execve("/bin/python3", argv, envp);
                _exit(127);
            }
            int st = 0;
            waitpid(pid, &st, 0);
        }
        Run r = run_viewer({p, "--no-open"});
        check("BV-05", "a member declaring 65 MiB is refused before it is read (exit 2, the limit named)",
              fs::exists(p) && r.rc == 2 && has(r.err, "limit"), fmtf("rc=%d %s", r.rc, r.err.c_str()));
    }
    // BV-06
    Run r6;
    const std::string html = html_of(nocard_path, &r6);
    {
        const char* ids[] = { "summary", "settings", "machine", "nextregs", "paging", "video", "interrupts",
                              "pictures", "log", "debugger", "memory", "raw-state" };
        std::string missing;
        for (const char* id : ids) if (!has(html, std::string("id=\"") + id + "\"")) missing += std::string(id) + " ";
        // Nothing is extracted: the scratch directory holds only what the rows made, plus out.html.
        // A VALID report must never trip the wrong-shape guard (it would hide a bug in a renderer): neither the
        // page nor the text output (with memory) may say a section is of an unexpected shape.
        Run t6 = run_viewer({nocard_path, "--text", "--memory"});
        Run c6 = run_viewer({card_path, "--text"});
        const std::string h6c = html_of(card_path, nullptr, "card6.html");
        const bool no_guard = !has(html, "unexpected shape") && !has(t6.out, "unexpected shape") &&
                              !has(h6c, "unexpected shape") && !has(c6.out, "unexpected shape") && t6.rc == 0 && !h6c.empty();
        check("BV-06", "the HTML has every section id, no <script>, is written to -o without opening anything, and a valid report (page and text, with and without memory) never says \"unexpected shape\"",
              r6.rc == 0 && missing.empty() && !has(html, "<script") && !has(html, "http://") && !has(html, "https://") &&
              fs::exists(tmp_path("out.html")) && no_guard, "missing: " + missing + fmtf(" rc=%d no_guard=%d", r6.rc, no_guard));
    }
    // BV-07
    {
        Members rn = nocard;
        json m = member_json(rn, "machine.json");
        for (auto& e : m["nextregs"]) e["name"] = "X" + std::to_string(e["reg"].get<int>());
        put_json(rn, "machine.json", m);
        write_bytes(tmp_path("renamed.zip"), zip_of(rn));
        const std::string h = html_of(tmp_path("renamed.zip"), nullptr, "renamed.html");
        bool all = !h.empty();
        for (int i = 0; all && i < 256; ++i) {
            char row[160];
            std::snprintf(row, sizeof row, "<td>0x%02X</td>\n<td>X%d</td>\n", i, i);
            // The viewer writes a row as one line of cells.
            std::snprintf(row, sizeof row, "<td>0x%02X</td><td>X%d</td><td>0x%02X</td>", i, i,
                          m["nextregs"][i]["value"].get<int>());
            if (!has(h, row)) all = false;
        }
        check("BV-07", "NextREG names come from the report: 256 renamed registers all render, with the report's values", all);
    }
    // BV-08
    {
        json mj = member_json(nocard, "machine.json");
        bool ok = html.size() > 0 && mj["paging"]["slots"].size() == 8;
        for (const auto& s : mj["paging"]["slots"]) {
            const std::string row = fmtf("<td>%d</td><td>%d</td><td>%d</td><td>%s</td><td>%s %d</td>", s["slot"].get<int>(),
                s["nr_page"].get<int>(), s["effective_page"].get<int>(), s["is_rom"].get<bool>() ? "True" : "False",
                s["space"]["kind"].get<std::string>().c_str(), s["space"]["index"].get<int>());
            if (!has(html, row)) ok = false;
        }
        check("BV-08", "the paging map: 8 rows, each slot's page and space as machine.json has them", ok);
    }
    // BV-09
    {
        std::vector<std::vector<uint8_t>> imgs;
        size_t at = 0;
        const std::string tag = "src=\"data:image/png;base64,";
        while ((at = html.find(tag, at)) != std::string::npos) {
            const size_t b = at + tag.size(), e = html.find('"', b);
            imgs.push_back(b64decode(html.substr(b, e - b)));
            at = e;
        }
        std::vector<std::vector<uint8_t>> want;
        for (auto& e : nocard) if (e.first.rfind("screen/", 0) == 0) want.push_back(e.second);
        std::sort(imgs.begin(), imgs.end());
        std::sort(want.begin(), want.end());
        check("BV-09", "the pictures: every data:image/png is a report member, byte for byte (the composite and 7 layers)",
              imgs.size() == 8 && imgs == want, fmtf("imgs=%zu want=%zu", imgs.size(), want.size()));
    }
    // BV-10
    {
        const auto& cpu = *find(nocard, "memory/cpu.bin");
        const auto& ram = *find(nocard, "memory/ram.bin");
        const std::string r1 = fmtf("$%04X: %-47s", 0x8000, hex_row(cpu, 0x8000).c_str());
        const std::string r2 = fmtf("$%04X: %-47s", 0x8010, hex_row(cpu, 0x8010).c_str());
        const std::string r3 = fmtf("$%04X: %-47s", 0x8200, hex_row(cpu, 0x8200).c_str());
        const std::string r4 = fmtf("$%04X: %-47s", 0, hex_row(ram, 4 * 8192).c_str());   // RAM page 4 (the program's bank)
        check("BV-10", "memory present: the hex rows at three CPU offsets and at page 4 equal cpu.bin and ram.bin",
              has(html, r1) && has(html, r2) && has(html, r3) && has(html, r4) && has(html, "CPU view (64 KB)"));
    }
    // BV-11
    {
        const std::string h = html_of(card_path, nullptr, "card.html");
        json m = member_json(card, "manifest.json");
        const std::string reason = m["memory"]["withheld"]["reason"].get<std::string>();
        const size_t mem = h.find("id=\"memory\"");
        const size_t next = h.find("<section", mem + 1);
        const std::string sec = mem == std::string::npos ? "" : h.substr(mem, next - mem);
        // A report that CLAIMS no memory but carries memory/cpu.bin: the manifest decides, not the member.
        Members liar = nocard;
        json lm = member_json(liar, "manifest.json");
        lm["memory"] = json{{"included", false}, {"withheld", json{{"code", "card-session"}, {"reason", "claimed withheld"}}}};
        put_json(liar, "manifest.json", lm);
        write_bytes(tmp_path("liar.zip"), zip_of(liar));
        const std::string hl = html_of(tmp_path("liar.zip"), nullptr, "liar.html");
        check("BV-11", "memory absent: the section says not included, with the manifest's reason, and has no hex rows; the manifest decides, not a stray cpu.bin",
              card_ok && !h.empty() && has(sec, "not included") && has(sec, reason.substr(0, 30)) && !has(sec, "<pre>") &&
              !has(sec, "CPU view") && !hl.empty() && !has(hl, "CPU view (64 KB)") && has(hl, "not included: claimed withheld"),
              "section: " + sec.substr(0, 200));
    }
    // BV-12
    {
        Members hostile = nocard;
        const std::string evil = "</pre><script>alert(1)</script><img src=x onerror=alert(2)>";
        *find(hostile, "log.txt") = std::vector<uint8_t>(evil.begin(), evil.end());
        json m = member_json(hostile, "manifest.json");
        m["warnings"] = json::array({ evil });
        m["producer"]["jnext_version"] = evil;
        m["programs"][0]["name"] = evil;
        put_json(hostile, "manifest.json", m);
        json st = member_json(hostile, "settings.json");
        st["command_line"] = json::array({ evil });
        st["config_file"] = json{{ "a<b", evil }};
        put_json(hostile, "settings.json", st);
        // A "picture" that is not a PNG (an SVG with a handler) must not be embedded at all.
        const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" onload=\"alert(3)\"/>";
        *find(hostile, "screen/composite.png") = std::vector<uint8_t>(svg.begin(), svg.end());
        write_bytes(tmp_path("hostile.zip"), zip_of(hostile));
        const std::string h = html_of(tmp_path("hostile.zip"), nullptr, "hostile.html");
        const std::string svg_b64 = "PHN2Zy";   // base64 of "<svg"
        check("BV-12", "hostile text (a log line, warnings, names, settings) appears only escaped, and a non-PNG picture is never embedded",
              !h.empty() && has(h, "&lt;/pre&gt;&lt;script&gt;alert(1)&lt;/script&gt;") && !has(h, "<script") &&
              !has(h, "<img src=x") && !has(h, "onerror=alert(2)>") && !has(h, svg_b64) && has(h, "not a PNG, not shown"));
    }
    // BV-13
    {
        Run r = run_viewer({nocard_path, "--text"});
        json mj = member_json(nocard, "machine.json");
        bool ok = r.rc == 0 && has(r.out, "== Summary ==") && has(r.out, "machine: next");
        for (auto it = mj["registers"].begin(); it != mj["registers"].end(); ++it) {
            const int v = it.value().is_boolean() ? int(it.value().get<bool>()) : it.value().get<int>();
            static const std::set<std::string> wide = { "AF", "BC", "DE", "HL", "AF2", "BC2", "DE2", "HL2", "IX", "IY", "SP", "PC", "MEMPTR" };
            const std::string line = fmtf("%s = 0x%0*X (%d)", it.key().c_str(), wide.count(it.key()) ? 4 : 2, v, v);
            if (!has(r.out, line + "\n")) { ok = false; std::printf("    missing: %s\n", line.c_str()); }
        }
        check("BV-13", "--text: the summary, and every register value as machine.json has it", ok, fmtf("rc=%d", r.rc));
    }
    // BV-14
    {
        Run r = run_viewer({nocard_path, "--text", "--memory"});
        const auto& cpu = *find(nocard, "memory/cpu.bin");
        const std::string row = fmtf("$%04X: %-47s", 0x8000, hex_row(cpu, 0x8000).c_str());
        Run plain = run_viewer({nocard_path, "--text"});
        check("BV-14", "--text --memory prints the hex row at $8000 as cpu.bin has it; without --memory there is none",
              r.rc == 0 && has(r.out, row) && !has(plain.out, row));
    }
    // BV-15: hostile structure must not crash the viewer
    {
        Members deep = nocard;
        const std::string nest = std::string(100000, '[') + std::string(100000, ']');
        *find(deep, "settings.json") = std::vector<uint8_t>(nest.begin(), nest.end());
        write_bytes(tmp_path("deep.zip"), zip_of(deep));
        Run r = run_viewer({tmp_path("deep.zip"), "-o", tmp_path("deep.html"), "--no-open"});
        Members junk = nocard;
        *find(junk, "machine.json") = noise(4000, 5);     // not JSON at all
        write_bytes(tmp_path("junk.zip"), zip_of(junk));
        Run j = run_viewer({tmp_path("junk.zip"), "-o", tmp_path("junk.html"), "--no-open"});
        Run missing_file = run_viewer({tmp_path("no-such-file.zip"), "--no-open"});
        check("BV-15", "hostile structure: JSON nested 100000 deep and a machine.json of noise are shown as absent, never a crash; a missing file is exit 1",
              r.rc == 0 && j.rc == 0 && r.err.find("Traceback") == std::string::npos && j.err.find("Traceback") == std::string::npos &&
              missing_file.rc == 1, fmtf("deep=%d junk=%d nofile=%d %s", r.rc, j.rc, missing_file.rc, r.err.substr(0, 120).c_str()));
    }

    // A python helper (minimal env) that makes the hostile zips of BV-17..BV-19.
    auto make_zips = [&](const std::string& script_text) {
        const std::string sp = tmp_path("mkzips.py");
        { std::ofstream f(sp); f << script_text; }
        pid_t pid = fork();
        if (pid == 0) {
            char lang[] = "LANG=C", path[] = "PATH=/usr/bin:/bin";
            char* envp[] = { lang, path, nullptr };
            char* argv[] = { const_cast<char*>("python3"), const_cast<char*>("-I"), const_cast<char*>(sp.c_str()),
                             const_cast<char*>(g_tmp.c_str()), nullptr };
            execve("/usr/bin/python3", argv, envp);
            execve("/bin/python3", argv, envp);
            _exit(127);
        }
        int st = 0;
        waitpid(pid, &st, 0);
    };

    // BV-16: terminal escape sequences in a report never reach the terminal through --text
    {
        Members esc = nocard;
        const std::string osc = "\x1b]0;pwned\x07\x1b[31mred\x1b]52;c;QUJD\x07\xC2\x9B" "2J\x7f";
        json m = member_json(esc, "manifest.json");
        m["producer"]["jnext_version"] = osc;
        m["machine"] = osc;
        m["warnings"] = json::array({ osc });
        m["programs"][0]["name"] = osc;
        put_json(esc, "manifest.json", m);
        json mj = member_json(esc, "machine.json");
        mj["registers"][osc] = 7;
        put_json(esc, "machine.json", mj);
        write_bytes(tmp_path("esc.zip"), zip_of(esc));
        Run r = run_viewer({tmp_path("esc.zip"), "--text", "--memory"});
        Run bad = run_viewer({std::string("/nonexistent/") + osc + ".zip", "--text"});
        auto clean = [](const std::string& t) {
            for (unsigned char c : t) if ((c < 0x20 && c != '\n' && c != '\t') || c == 0x7F) return false;
            return t.find("\xC2\x9B") == std::string::npos;   // U+009B (CSI) as UTF-8
        };
        check("BV-16", "--text writes no control byte (ESC, BEL, DEL, C1) of the report: they appear as \\xNN, on stdout and stderr",
              r.rc == 0 && clean(r.out) && clean(r.err) && clean(bad.err) && has(r.out, "\\x1b]0;pwned\\x07") && bad.rc == 1,
              fmtf("rc=%d clean_out=%d clean_err=%d", r.rc, clean(r.out), clean(bad.err)));
    }

    // BV-17: well-formed JSON of the wrong type
    {
        Members wt = nocard;
        json m = member_json(wt, "manifest.json");
        m["cards"] = 5; m["programs"] = 5; m["warnings"] = 5;
        put_json(wt, "manifest.json", m);
        json mj = member_json(wt, "machine.json");
        mj["registers"] = 5; mj["nextregs"] = 5; mj["video"]["sprites"] = 5; mj["video"]["palettes"] = json{{"ula-first", json{{"rgb333", 5}}}};
        mj["paging"]["slots"] = 5;
        put_json(wt, "machine.json", mj);
        json st = member_json(wt, "settings.json");
        st["command_line"] = 5; st["config_file"] = 5; st["session"] = 5;
        put_json(wt, "settings.json", st);
        json dj = member_json(wt, "debugger.json");
        dj["breakpoints"] = json{{"a", 1}}; dj["trace"] = 5; dj["call_stack"] = 5; dj["run_state"] = 5;
        put_json(wt, "debugger.json", dj);
        write_bytes(tmp_path("wrongtypes.zip"), zip_of(wt));
        Members badmembers = nocard;
        json m2 = member_json(badmembers, "manifest.json");
        m2["members"] = 5;
        put_json(badmembers, "manifest.json", m2);
        write_bytes(tmp_path("badmembers.zip"), zip_of(badmembers));
        Run h = run_viewer({tmp_path("wrongtypes.zip")});
        Run t = run_viewer({tmp_path("wrongtypes.zip"), "--text", "--memory"});
        Run bm = run_viewer({tmp_path("badmembers.zip")});
        check("BV-17", "wrong-typed JSON (cards, registers, nextregs, sprites, palettes, command line, breakpoints...) is shown as absent, never a traceback; a non-list members is refused as malformed",
              h.rc == 0 && t.rc == 0 && h.err.find("Traceback") == std::string::npos && t.err.find("Traceback") == std::string::npos &&
              bm.rc == 2 && has(bm.err, "malformed"),
              fmtf("html=%d text=%d members=%d %s", h.rc, t.rc, bm.rc, (h.err + t.err).substr(0, 160).c_str()));
    }

    // BV-18 and BV-19: the limits, and members that cannot be read (real zips made by python)
    make_zips(R"PY(
import zipfile, json, sys, struct
d = sys.argv[1]
MAN = lambda members: json.dumps({'kind': 'jnext-bug-report', 'format_version': 1, 'members': members})
def zipwrite(path, entries, method=zipfile.ZIP_DEFLATED):
    z = zipfile.ZipFile(path, 'w', method)
    for n, b in entries: z.writestr(n, b)
    z.close()
# 5 members of 60 MiB each: every one under the 64 MiB cap, 300 MiB in all
zipwrite(d + '/total.zip', [('manifest.json', MAN([]))] + [('memory/m%d.bin' % i, bytes(60 * 1024 * 1024)) for i in range(5)])
# 4097 tiny members
zipwrite(d + '/entries.zip', [('manifest.json', MAN([]))] + [('state/s%d.json' % i, b'{}') for i in range(4096)])
# a minimal report whose log.txt is damaged three ways
def minimal(path):
    zipwrite(path, [('manifest.json', MAN([{'name': 'log.txt', 'bytes': 100}])), ('log.txt', b'hello log line ' * 40)])
def patch(path, fn):
    b = bytearray(open(path, 'rb').read()); fn(b); open(path, 'wb').write(bytes(b))
def headers(b, sig):
    i = 0
    while True:
        i = b.find(sig, i)
        if i < 0: return
        yield i
        i += 4
def name_at(b, i, local):
    off = 30 if local else 46
    n = struct.unpack_from('<H', b, i + (26 if local else 28))[0]
    return bytes(b[i + off:i + off + n])
def flag(b):
    for i in headers(b, b'PK\x03\x04'):
        if name_at(b, i, True) == b'log.txt': b[i + 6] |= 1
    for i in headers(b, b'PK\x01\x02'):
        if name_at(b, i, False) == b'log.txt': b[i + 8] |= 1
def method(b):
    for i in headers(b, b'PK\x03\x04'):
        if name_at(b, i, True) == b'log.txt': struct.pack_into('<H', b, i + 8, 99)
    for i in headers(b, b'PK\x01\x02'):
        if name_at(b, i, False) == b'log.txt': struct.pack_into('<H', b, i + 10, 99)
def corrupt(b):
    for i in headers(b, b'PK\x03\x04'):
        if name_at(b, i, True) == b'log.txt':
            start = i + 30 + struct.unpack_from('<H', b, i + 26)[0] + struct.unpack_from('<H', b, i + 28)[0]
            for k in range(start, start + 8): b[k] ^= 0xFF
for name, fn in (('encrypted', flag), ('method', method), ('corrupt', corrupt)):
    p = d + '/' + name + '.zip'
    minimal(p); patch(p, fn)
)PY");
    {
        Run t = run_viewer({tmp_path("total.zip")});
        Run e = run_viewer({tmp_path("entries.zip")});
        check("BV-18", "the limits: 300 MiB in all (each member under 64 MiB) and 4097 members are refused (exit 2)",
              fs::exists(tmp_path("total.zip")) && fs::exists(tmp_path("entries.zip")) &&
              t.rc == 2 && has(t.err, "in all") && e.rc == 2 && has(e.err, "members"),
              fmtf("total=%d(%s) entries=%d(%s)", t.rc, t.err.c_str(), e.rc, e.err.c_str()));
    }
    {
        Run enc = run_viewer({tmp_path("encrypted.zip")});
        Run met = run_viewer({tmp_path("method.zip")});
        Run cor = run_viewer({tmp_path("corrupt.zip")});
        auto refused = [](const Run& r) { return r.rc == 2 && has(r.err, "cannot be read") && r.err.find("Traceback") == std::string::npos; };
        check("BV-19", "an encrypted member, an unsupported compression method and a corrupt member are refused (exit 2), not a crash",
              fs::exists(tmp_path("encrypted.zip")) && refused(enc) && refused(met) && refused(cor),
              fmtf("enc=%d met=%d cor=%d %s", enc.rc, met.rc, cor.rc, (enc.err + met.err + cor.err).substr(0, 200).c_str()));
    }

    std::error_code ec;
    fs::remove_all(g_tmp, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
