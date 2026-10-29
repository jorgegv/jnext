// GH #401 - a host path that is not valid UTF-8 must not abort a .jns save.
//
// A Linux file name is bytes; "t\xE4pe" (Latin-1) is a legal directory name
// there and not valid UTF-8. nlohmann's default dump() throws on it, which
// aborted the whole process (Quick Save killed the GUI). The writer now
// replaces each invalid byte with U+FFFD (RFC 8259 s8.1: JSON text exchanged
// between systems MUST be UTF-8; nlohmann error_handler_t::replace). The
// recorded path is lossy by design: it will not reopen, and a load warns.
//
// The suite is `# os: linux` because only a Linux filesystem can hold such a
// name (NTFS names are UTF-16; APFS refuses non-UTF-8).
//
//   JNSP-01  a tape under a non-UTF-8 directory: save succeeds, strict parse, U+FFFD, real digest
//   JNSP-02  an esxDOS root + open handle under a non-UTF-8 directory: save succeeds, both JSON members parse
//   JNSP-03  the JNSP-01 archive loads, with one tape warning and no refusal
//   JNSP-04  a tape loaded through a symlink is recorded under the link name (symlinks kept, D2)
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/esxdos_hostfs.h"
#include "core/jns_snapshot.h"
#include "core/host_path.h"
#include "core/sha256.h"
#include "core/tap_loader.h"
#include "save/jns_container.h"
#include "save/zip_archive.h"
#include "third_party/nlohmann-json/nlohmann/json.hpp"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

const char kFFFD[] = "\xEF\xBF\xBD";

bool strict_parses(const std::string& text) {
    return nlohmann::json::accept(text);   // strict parse, no exceptions
}

bool member(const std::vector<uint8_t>& jns, const char* name, std::string& text) {
    jnext::zip::Reader r;
    std::string why;
    return r.open(jns.data(), jns.size(), why) && r.read_text(name, text, why);
}

std::vector<uint8_t> tap_bytes() {
    std::vector<uint8_t> v = {19, 0, 0x00};
    uint8_t x = 0;
    for (int i = 0; i < 17; ++i) v.push_back(static_cast<uint8_t>(0x30 + i));
    for (size_t i = 2; i < v.size(); ++i) x ^= v[i];
    v.push_back(x);
    return v;
}

void build(Emulator& e, const EmulatorConfig& base) {
    EmulatorConfig cfg = base;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = 2;
    e.init(cfg);
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path() /
                          ("jnext-jnsp-" + std::to_string(getpid()));
    fs::remove_all(base, ec);

    // JNSP-01 / JNSP-03
    std::vector<uint8_t> jns1;
    bool saved1 = false;
    {
        const fs::path dir = base / "t\xE4pe";
        fs::create_directories(dir);
        const auto bytes = tap_bytes();
        {
            std::ofstream f(dir / "game.tap", std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        }
        auto up = std::make_unique<Emulator>();
        build(*up, EmulatorConfig{});
        const bool attached = up->load_tap((dir / "game.tap").string());
        jnext::JnsSaveOptions opt;
        jnext::JnsLoadReport rep;
        std::string why;
        saved1 = attached && up->save_jns(opt, jns1, rep, why);
        std::string text;
        jnext::jns::Manifest m;
        std::vector<std::string> unk;
        const bool have = saved1 && member(jns1, jnext::jns::kManifestMember, text);
        const bool parsed = have && strict_parses(text) &&
                            jnext::jns::manifest_from_json(text, m, unk, why);
        check("JNSP-01",
              "a tape under a non-UTF-8 directory: the save succeeds, the manifest strict-parses, "
              "the path holds U+FFFD where the byte was, the digest is of the real file",
              parsed && m.tape.path.find(kFFFD) != std::string::npos &&
                  m.tape.sha256 == sdcard::sha256_hex(bytes),
              m.tape.path);
    }
    {
        auto up = std::make_unique<Emulator>();
        build(*up, EmulatorConfig{});
        jnext::JnsLoadOptions lopt;
        jnext::JnsLoadReport rep;
        std::string refusal;
        const bool ok = saved1 && up->load_jns(jns1.data(), jns1.size(), lopt, rep, refusal);
        int tape_warnings = 0;
        for (const auto& w : rep.warnings)
            if (w.find("tape") != std::string::npos) ++tape_warnings;
        check("JNSP-03",
              "the archive of JNSP-01 loads: no refusal, exactly one tape warning (the documented degradation)",
              ok && tape_warnings == 1, refusal);
    }

    // JNSP-02
    {
        const fs::path root = base / "r\xE4ot";
        fs::create_directories(root);
        {
            std::ofstream f(root / "f.bin", std::ios::binary);
            f << "0123456789";
        }
        EmulatorConfig cfg;
        cfg.esxdos_stub = true;
        cfg.esxdos_stub_root = root.string();
        auto up = std::make_unique<Emulator>();
        build(*up, cfg);
        uint8_t handle = 0;
        const bool opened =
            up->esxdos_hostfs().open("/f.bin", EsxdosHostFs::kModeRead, handle) ==
            EsxdosHostFs::kOk;
        jnext::JnsSaveOptions opt;
        jnext::JnsLoadReport rep;
        std::string why;
        std::vector<uint8_t> out;
        const bool saved = opened && up->save_jns(opt, out, rep, why);
        std::string man, esx;
        const bool both = saved && member(out, jnext::jns::kManifestMember, man) &&
                          member(out, "state/esxdos_hostfs.json", esx);
        check("JNSP-02",
              "an esxDOS root and an open handle under a non-UTF-8 directory: the save succeeds and "
              "manifest.json and state/esxdos_hostfs.json both strict-parse",
              both && strict_parses(man) && strict_parses(esx) &&
                  esx.find(std::string("r") + kFFFD + "ot") != std::string::npos &&
                  man.find(std::string("r") + kFFFD + "ot") != std::string::npos &&
                  esx.find('\xE4') == std::string::npos && man.find('\xE4') == std::string::npos,
              both ? std::string() : "opened=" + std::to_string(opened) + " saved=" + std::to_string(saved));
    }

    // JNSP-04
    {
        const fs::path dir = base / "lnk";
        fs::create_directories(dir);
        {
            std::ofstream f(dir / "v2.tap", std::ios::binary);
            const auto b = tap_bytes();
            f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        fs::create_symlink("v2.tap", dir / "current.tap");
        const fs::path cwd0 = fs::current_path();
        fs::current_path(dir);
        TapLoader t;
        const bool ok = t.load("current.tap");
        const std::string h = jnext::absolute_host_path("current.tap");
        fs::current_path(cwd0);
        const std::string tail = "/lnk/current.tap";
        auto ends = [&](const std::string& s) {
            return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
        };
        check("JNSP-04",
              "a tape loaded through a symlink is recorded under the LINK name, not its target",
              ok && ends(t.path()) && ends(h), t.path());
    }

    fs::remove_all(base, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
