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

Run run_viewer(const std::vector<std::string>& args) {
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
        check("BV-06", "the HTML has every section id, no <script>, and is written to -o without opening anything",
              r6.rc == 0 && missing.empty() && !has(html, "<script") && !has(html, "http://") && !has(html, "https://") &&
              fs::exists(tmp_path("out.html")), "missing: " + missing + fmtf(" rc=%d", r6.rc));
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
        check("BV-11", "memory absent: the section says not included, with the manifest's reason, and has no hex rows",
              card_ok && !h.empty() && has(sec, "not included") && has(sec, reason.substr(0, 30)) && !has(sec, "<pre>") &&
              !has(sec, "CPU view"), "section: " + sec.substr(0, 200));
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
        write_bytes(tmp_path("hostile.zip"), zip_of(hostile));
        const std::string h = html_of(tmp_path("hostile.zip"), nullptr, "hostile.html");
        check("BV-12", "hostile text (a log line, warnings, names, settings) appears only escaped: no <script, no raw tag",
              !h.empty() && has(h, "&lt;/pre&gt;&lt;script&gt;alert(1)&lt;/script&gt;") && !has(h, "<script") &&
              !has(h, "<img src=x") && !has(h, "onerror=alert(2)>"));
    }
    // BV-13
    {
        Run r = run_viewer({nocard_path, "--text"});
        json mj = member_json(nocard, "machine.json");
        bool ok = r.rc == 0 && has(r.out, "== Summary ==") && has(r.out, "machine: next");
        for (auto it = mj["registers"].begin(); it != mj["registers"].end(); ++it) {
            const int v = it.value().is_boolean() ? int(it.value().get<bool>()) : it.value().get<int>();
            const std::string line = fmtf("%s = 0x%0*X (%d)", it.key().c_str(), v > 255 ? 4 : 2, v, v);
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

    std::error_code ec;
    fs::remove_all(g_tmp, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
