// Warm-start tests (GH #234, the structural fix for GH #72).
//
// Two halves, both offline:
//
//   WSC-*  the on-disk cache — where it lives, what it stores, and the four
//          keys that invalidate it. The cache-key rows are the point of the
//          suite: a stale recording silently served to the wrong machine is
//          the worst failure this mechanism can have, because the machine it
//          produces LOOKS booted. Every one of the four keys therefore gets a
//          row in the refusing direction, not just the accepting one.
//
//   WSR-*  the residency criterion. NOT the recording itself: taking one
//          needs an SD image with firmware on it, so the end-to-end
//          record -> cache -> restore round trip is the `warm-start-func`
//          regression row. What is unit-testable here is the criterion that
//          decides whether a boot MAY be recorded, and in particular that it
//          refuses a machine that never booted — which is exactly the state
//          `--load` has always produced (GH #226).
//
// Row index:
//   WSC-PATH-01   cache_dir() is <config-dir>/warm-start, from $JNEXT_CONFIG_DIR
//   WSC-PATH-02   unset/empty override falls back to $HOME/.jnext/warm-start
//   WSC-PATH-03   cache_path() is per machine type (no two share a file)
//   WSC-RT-01     store() then load() round-trips the exact bytes
//   WSC-RT-02     store() creates the cache directory when it does not exist
//   WSC-RT-03     store() leaves no .tmp file behind
//   WSC-RT-04     a second store() replaces in place — one file per machine
//   WSC-HDR-01    file length is exactly kHeaderBytes + state_bytes
//   WSC-HDR-02    the SD digest is stored as ASCII hex at its documented offset
//   WSC-INV-01    a different SD image digest is REFUSED
//   WSC-INV-02    a different machine type is REFUSED
//   WSC-INV-03    a bumped state-format version is REFUSED
//   WSC-INV-04    a different state-stream length is REFUSED
//   WSC-INV-05    a truncated payload is REFUSED
//   WSC-INV-06    a wrong magic is REFUSED
//   WSC-INV-07    a missing file is REFUSED
//   WSC-INV-08    a file shorter than the header is REFUSED
//   WSC-INV-09    a refused load leaves the caller's buffer untouched
//   WSC-INV-10    every refusal states a reason
//   WSC-STORE-01  store() refuses a header that disagrees with its payload
//   WSC-STORE-02  store() refuses a digest too long for the header field
//   WSR-RES-01    a non-Next machine is refused, naming the machine
//   WSR-RES-02    a Next that never booted is refused although the two
//                 FIRMWARE-LESS checks pass — the case with no symptom
//   WSR-RES-03    ensure_warm_start_state() declines on a non-Next
//   WSR-RES-04    ensure_warm_start_state() declines with no SD image
//   WSR-RES-05    the decline is sticky — a second call declines too

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/warm_start_cache.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // getpid()

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

std::string g_dir;

void set_config_dir(const std::string& d) {
    ::setenv("JNEXT_CONFIG_DIR", d.c_str(), 1);
}

// A digest-shaped string: 64 lower-case hex characters, as sha256_file
// returns. The rows never hash a real file — what is under test is the
// comparison, not OpenSSL.
std::string digest(char fill) { return std::string(64, fill); }

std::vector<uint8_t> payload(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7);
    return v;
}

warm_start::Identity make_id(const std::string& sha, uint8_t mtype, size_t bytes) {
    warm_start::Identity id;
    id.sd_image_sha256 = sha;
    id.machine_type    = mtype;
    id.format_version  = warm_start::kFormatVersion;
    id.state_bytes     = bytes;
    return id;
}

std::vector<uint8_t> read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

}  // namespace

int main()
{
    g_dir = (std::filesystem::temp_directory_path() /
             ("jnext_warmstart_" + std::to_string(::getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    set_config_dir(g_dir);

    std::printf("Warm-start tests (GH #234)\n\n");

    // ── Paths ────────────────────────────────────────────────────────
    {
        check("WSC-PATH-01", "cache_dir() is <config-dir>/warm-start",
              warm_start::cache_dir() == g_dir + "/warm-start",
              warm_start::cache_dir());

        const char* home = std::getenv("HOME");
        ::unsetenv("JNEXT_CONFIG_DIR");
        const std::string unset_dir = warm_start::cache_dir();
        set_config_dir("");
        const std::string empty_dir = warm_start::cache_dir();
        set_config_dir(g_dir);
        const std::string want_home =
            std::string(home && *home ? home : ".") + "/.jnext/warm-start";
        check("WSC-PATH-02",
              "unset or empty $JNEXT_CONFIG_DIR falls back to $HOME/.jnext",
              unset_dir == want_home && empty_dir == want_home,
              unset_dir + " / " + empty_dir);

        check("WSC-PATH-03", "cache_path() differs per machine type",
              warm_start::cache_path(0) != warm_start::cache_path(1) &&
              warm_start::cache_path(0).rfind(warm_start::cache_dir(), 0) == 0,
              warm_start::cache_path(0) + " vs " + warm_start::cache_path(1));
    }

    // ── Round trip ───────────────────────────────────────────────────
    const auto state = payload(4096, 0x11);
    const auto id    = make_id(digest('a'), 0, state.size());
    {
        std::filesystem::remove_all(warm_start::cache_dir(), ec);
        std::string why;
        const bool stored = warm_start::store(id, state, why);
        check("WSC-RT-02", "store() creates the cache directory",
              stored && std::filesystem::is_directory(warm_start::cache_dir()), why);

        std::vector<uint8_t> back;
        const bool loaded = warm_start::load(id, back, why);
        check("WSC-RT-01", "store() then load() round-trips the exact bytes",
              loaded && back == state, why);

        check("WSC-RT-03", "no .tmp file is left behind",
              !std::filesystem::exists(warm_start::cache_path(0) + ".tmp"));

        check("WSC-HDR-01", "file length is kHeaderBytes + state_bytes",
              std::filesystem::file_size(warm_start::cache_path(0)) ==
                  warm_start::kHeaderBytes + state.size());

        const auto raw = read_file(warm_start::cache_path(0));
        check("WSC-HDR-02", "the SD digest is ASCII hex in the header",
              raw.size() > 88 &&
                  std::string(reinterpret_cast<const char*>(raw.data()) + 24, 64) ==
                      id.sd_image_sha256);

        // A second, different recording for the same machine type replaces
        // the first: the identity check invalidates, the filename does not,
        // so a changed SD image must not leave an orphan beside it.
        const auto state2 = payload(2048, 0x55);
        const auto id2    = make_id(digest('b'), 0, state2.size());
        warm_start::store(id2, state2, why);
        std::vector<uint8_t> back2;
        const bool loaded2 = warm_start::load(id2, back2, why);
        std::vector<uint8_t> stale;
        const bool old_gone = !warm_start::load(id, stale, why);
        check("WSC-RT-04", "a second store() replaces in place — one file per machine",
              loaded2 && back2 == state2 && old_gone);

        // Put the first recording back for the invalidation rows.
        warm_start::store(id, state, why);
    }

    // ── Invalidation: each of the four keys, in the refusing direction ──
    {
        std::vector<uint8_t> out;
        std::string why;

        auto refused = [&](warm_start::Identity want) {
            out.clear();
            why.clear();
            return !warm_start::load(want, out, why) && !why.empty();
        };

        check("WSC-INV-01", "a different SD image digest is refused",
              refused(make_id(digest('c'), 0, state.size())), why);
        // The machine type is BOTH the filename and a header field, and the
        // filename alone would answer this: asking for type 1 looks at a path
        // that does not exist, so it is refused for the wrong reason (measured
        // — the row passed with the header check disabled). Corrupt the field
        // in THIS machine's own file instead, which is the case the field is
        // actually for: a recording copied or renamed across machine types.
        {
            const std::string p = warm_start::cache_path(0);
            std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(12);            // kOffMachineType
            f.put(static_cast<char>(1));
            f.close();
            check("WSC-INV-02",
                  "a header recorded for another machine type is refused",
                  refused(make_id(digest('a'), 0, state.size())), why);
            warm_start::store(id, state, why);
        }

        warm_start::Identity bumped = make_id(digest('a'), 0, state.size());
        bumped.format_version = warm_start::kFormatVersion + 1;
        check("WSC-INV-03", "a bumped state-format version is refused",
              refused(bumped), why);

        check("WSC-INV-04", "a different state-stream length is refused",
              refused(make_id(digest('a'), 0, state.size() + 1)), why);

        check("WSC-INV-10", "every refusal states a reason", !why.empty(), why);

        // Truncate the payload without touching the header: the length the
        // header declares no longer matches what is on disk.
        {
            const std::string p = warm_start::cache_path(0);
            std::filesystem::resize_file(p, warm_start::kHeaderBytes + 16, ec);
            check("WSC-INV-05", "a truncated payload is refused",
                  refused(id), why);
            warm_start::store(id, state, why);
        }

        // Corrupt the magic.
        {
            const std::string p = warm_start::cache_path(0);
            std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(0);
            f.put('X');
            f.close();
            check("WSC-INV-06", "a wrong magic is refused", refused(id), why);
            warm_start::store(id, state, why);
        }

        // A buffer the caller already owns must survive a refusal: the
        // fallback path keeps running on whatever it had.
        {
            out.assign(8, 0xAB);
            std::vector<uint8_t> before = out;
            warm_start::load(make_id(digest('c'), 0, state.size()), out, why);
            check("WSC-INV-09", "a refused load leaves the caller's buffer untouched",
                  out == before);
        }

        // Missing file.
        {
            std::filesystem::remove(warm_start::cache_path(0), ec);
            check("WSC-INV-07", "a missing file is refused", refused(id), why);
        }

        // Shorter than the header itself.
        {
            const std::string p = warm_start::cache_path(0);
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            const char stub[] = "JNEXT";
            f.write(stub, 5);
            f.close();
            check("WSC-INV-08", "a file shorter than the header is refused",
                  refused(id), why);
        }
    }

    // ── store() refusals ─────────────────────────────────────────────
    {
        std::string why;
        auto bad_len = make_id(digest('a'), 0, state.size() + 1);
        check("WSC-STORE-01",
              "store() refuses a header that disagrees with its payload",
              !warm_start::store(bad_len, state, why) && !why.empty(), why);

        auto bad_sha = make_id(std::string(80, 'a'), 0, state.size());
        check("WSC-STORE-02",
              "store() refuses a digest too long for the header field",
              !warm_start::store(bad_sha, state, why) && !why.empty(), why);
    }

    // ── Residency criterion ──────────────────────────────────────────
    {
        // A 48K machine: refused at the first question, because there is no
        // firmware to record and the mechanism must say so rather than
        // quietly doing nothing.
        Emulator emu48;
        EmulatorConfig c48;
        c48.type = MachineType::ZX48K;
        c48.rewind_buffer_frames = 0;
        emu48.init(c48);
        std::string why;
        check("WSR-RES-01", "a non-Next machine is refused, naming the machine",
              !emu48.nextzxos_resident(why) && why.find("Next") != std::string::npos,
              why);
        check("WSR-RES-03", "ensure_warm_start_state() declines on a non-Next",
              !emu48.ensure_warm_start_state());

        // A Next that never booted. Its boot-ROM overlay is off and its
        // config mode is clear — the two questions a firmware-less machine
        // answers exactly as a booted one does (Emulator::init() commits
        // NR 0x03 itself on this path, GH #226) — so this row is the one
        // that proves the criterion does not stop there.
        Emulator emun;
        EmulatorConfig cn;
        cn.type = MachineType::ZXN_ISSUE2;
        cn.rewind_buffer_frames = 0;
        emun.init(cn);
        why.clear();
        const bool resident = emun.nextzxos_resident(why);
        check("WSR-RES-02",
              "a Next that never booted is refused although the two "
              "firmware-less checks pass",
              !resident && why.find("NextZXOS") != std::string::npos, why);

        check("WSR-RES-04", "ensure_warm_start_state() declines with no SD image",
              !emun.ensure_warm_start_state());
        check("WSR-RES-05", "the decline is sticky — a second call declines too",
              !emun.ensure_warm_start_state());
    }

    std::filesystem::remove_all(g_dir, ec);

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
