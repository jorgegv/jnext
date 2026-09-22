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
//   WSR-*  the residency criterion and the three ways the entry point can
//          decline. A SUCCESSFUL recording needs an SD image with firmware on
//          it, so record -> cache -> restore end to end is the
//          `warm-start-func` regression row; every FAILING path is reachable
//          here, including the one that really boots (WSR-RES-07, against a
//          card with no firmware on it).
//
//          `ensure_warm_start_state()` returns a bare bool and gives the same
//          `false` from all three guards, so the rows read the LOG to say
//          which one spoke — a ringbuffer sink on the emulator logger, the
//          same mechanism log_gate_test and log_test already use. Without it
//          the three rows pass for whichever guard happens to catch the
//          fixture, which is the illusory-discrimination bug WSR-RES-01 and
//          WSC-INV-02 each had.
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
//   WSC-INV-10    all eight refusal branches state a DISTINCT reason
//   WSC-STORE-01  store() refuses a header that disagrees with its payload
//   WSC-STORE-02  store() refuses a digest too long for the header field
//   WSR-RES-01    a non-Next machine is refused, naming the machine
//   WSR-RES-02    a Next that never booted is refused although the two
//                 FIRMWARE-LESS checks pass — the case with no symptom
//   WSR-RES-03    ensure_warm_start_state() declines on a non-Next BY THE
//                 MACHINE-TYPE GUARD (log-tapped; the bool cannot say)
//   WSR-RES-04    …declines with no SD image BY THE NO-SD GUARD
//   WSR-RES-05    …and through that SAME guard on every call
//   WSR-RES-06    checks 1 and 2 DID pass on that machine — asserted
//                 directly, not inferred from the refusal message
//   WSR-RES-07    a firmware-less CARD declines after a real boot,
//                 named as such, and caches nothing
//   WSR-RES-08    that verdict is latched per image — no second boot

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/warm_start_cache.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <cstdarg>
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

/// A ring sink hung off one logger for the life of a scope, with the logger's
/// level restored on the way out. Same mechanism as log_gate_test / log_test,
/// which is the point: it is what lets the WSR-RES-03/04/05 rows name WHICH
/// guard declined, where the bare `bool` that `ensure_warm_start_state()`
/// returns cannot. Each guard already logs a distinct line before returning.
struct LogTap {
    std::shared_ptr<spdlog::logger>                    log;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring;
    spdlog::level::level_enum                          saved;

    explicit LogTap(std::shared_ptr<spdlog::logger> l)
        : log(std::move(l)),
          ring(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256)),
          saved(log->level()) { log->sinks().push_back(ring); }
    ~LogTap() { log->set_level(saved); log->sinks().pop_back(); }

    int count(const char* needle) const {
        int n = 0;
        for (const auto& line : ring->last_formatted())
            if (line.find(needle) != std::string::npos) ++n;
        return n;
    }
};

// The line each decline emits, one per guard. They must stay DISTINCT — that
// is what the rows below lean on, and WSR-RES-08 asserts it rather than
// trusting it.
constexpr const char* kWhyMachineType = "only the Next boots firmware";
constexpr const char* kWhyNoSdImage   = "no SD image is mounted";
constexpr const char* kWhyNotRecorded = "Not recording";

/// Formatted failure detail. NOT called `fmt`: including a spdlog sink
/// brings the `fmt` namespace into scope and a local `fmt()` is then
/// ambiguous — the collision log_gate_test's own header comment records.
std::string det(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
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

        std::error_code sz_ec;
        const auto on_disk = std::filesystem::file_size(warm_start::cache_path(0), sz_ec);
        check("WSC-HDR-01", "file length is kHeaderBytes + state_bytes",
              !sz_ec && on_disk == warm_start::kHeaderBytes + state.size(),
              sz_ec ? sz_ec.message() : det("%ju bytes", (uintmax_t)on_disk));

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
        // "replaces in place" is a claim about the DIRECTORY, so count it.
        // Without this the row only says the new recording loads and the old
        // identity does not — which a second file sitting beside the first
        // would satisfy just as well, and an orphan per SD image is exactly
        // what the one-file-per-machine-type naming exists to prevent.
        size_t jwss = 0;
        for (const auto& e : std::filesystem::directory_iterator(warm_start::cache_dir()))
            if (e.path().extension() == ".jwss") ++jwss;
        check("WSC-RT-04", "a second store() replaces in place — one file per machine",
              loaded2 && back2 == state2 && old_gone && jwss == 1,
              det("%zu .jwss files", jwss));

        // Put the first recording back for the invalidation rows.
        warm_start::store(id, state, why);
    }

    // ── Invalidation: each of the four keys, in the refusing direction ──
    {
        std::vector<uint8_t> out;
        std::string why;
        std::vector<std::string> reasons;   // one per refusal, for WSC-INV-10

        // A refusal is only the RIGHT refusal when it comes from the branch
        // the row names. "it was refused" is satisfied by any of the eight
        // branches in warm_start::load(), so a row asserting only that passes
        // whenever the fixture happens to be wrong in some other way too —
        // the same illusory-discrimination bug WSC-INV-02 and WSR-RES-01 each
        // had. `expect` is a fragment no other branch can produce.
        auto refused = [&](warm_start::Identity want, const char* expect) {
            out.clear();
            why.clear();
            const bool rejected = !warm_start::load(want, out, why);
            if (rejected) reasons.push_back(why);
            return rejected && why.find(expect) != std::string::npos;
        };

        check("WSC-INV-01", "a different SD image digest is refused, as such",
              refused(make_id(digest('c'), 0, state.size()),
                      "recorded from a different SD image"), why);
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
                  "a header recorded for another machine type is refused, as such",
                  refused(make_id(digest('a'), 0, state.size()),
                          "recorded for machine type"), why);
            warm_start::store(id, state, why);
        }

        warm_start::Identity bumped = make_id(digest('a'), 0, state.size());
        bumped.format_version = warm_start::kFormatVersion + 1;
        check("WSC-INV-03", "a bumped state-format version is refused, as such",
              refused(bumped, "state-format version"), why);

        check("WSC-INV-04", "a different state-stream length is refused, as such",
              refused(make_id(digest('a'), 0, state.size() + 1),
                      "state stream is"), why);

        // Truncate the payload without touching the header: the length the
        // header declares no longer matches what is on disk.
        {
            const std::string p = warm_start::cache_path(0);
            std::filesystem::resize_file(p, warm_start::kHeaderBytes + 16, ec);
            check("WSC-INV-05", "a truncated payload is refused as truncated, not "
                  "as a length disagreement",
                  refused(id, "is truncated"), why);
            warm_start::store(id, state, why);
        }

        // Corrupt the magic.
        {
            const std::string p = warm_start::cache_path(0);
            std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(0);
            f.put('X');
            f.close();
            check("WSC-INV-06", "a wrong magic is refused, as such",
                  refused(id, "is not a jnext warm-start file"), why);
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
            check("WSC-INV-07", "a missing file is refused, as such",
                  refused(id, "no cached state at"), why);
        }

        // Shorter than the header itself.
        {
            const std::string p = warm_start::cache_path(0);
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            const char stub[] = "JNEXT";
            f.write(stub, 5);
            f.close();
            check("WSC-INV-08", "a file shorter than the header is refused, as such",
                  refused(id, "is shorter than its own header"), why);
        }

        // Diagnosability, asserted rather than assumed. Every branch above
        // must produce a DISTINCT message: the cache's whole recovery story is
        // that a refusal tells the user which of the keys moved, and two
        // branches sharing a message is that story quietly failing. The
        // per-row `expect` fragments above prove each branch says the right
        // thing; this proves no two of them say the same thing.
        {
            std::vector<std::string> sorted = reasons;
            std::sort(sorted.begin(), sorted.end());
            const bool all_set = std::none_of(
                sorted.begin(), sorted.end(),
                [](const std::string& r) { return r.empty(); });
            const bool all_distinct =
                std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
            check("WSC-INV-10",
                  "all eight refusal branches state a reason, and no two state "
                  "the same one",
                  reasons.size() == 8 && all_set && all_distinct,
                  det("%zu refusals", reasons.size()));
        }
    }

    // ── store() refusals ─────────────────────────────────────────────
    {
        std::string why;
        auto bad_len = make_id(digest('a'), 0, state.size() + 1);
        why.clear();
        check("WSC-STORE-01",
              "store() refuses a header that disagrees with its payload, as such",
              !warm_start::store(bad_len, state, why) &&
                  why.find("disagrees with its own payload") != std::string::npos,
              why);

        auto bad_sha = make_id(std::string(80, 'a'), 0, state.size());
        why.clear();
        check("WSC-STORE-02",
              "store() refuses a digest too long for the header field, as such",
              !warm_start::store(bad_sha, state, why) &&
                  why.find("does not fit the header field") != std::string::npos,
              why);
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
        // EXACT equality, and that is the fix for a real defect in this row.
        // It used to assert `why.find("Next")`, which THREE of the four
        // refusal messages satisfy — "NextREG 0x03 config mode is still set"
        // and "no NextZXOS ROM found…" both contain "Next" — so deleting the
        // machine-type early-return from nextzxos_resident() left the row
        // green (found by the independent review). Only check 1 can produce
        // this string, so only check 1 can satisfy the row.
        check("WSR-RES-01",
              "a non-Next machine is refused BY THE MACHINE-TYPE CHECK, not by a "
              "later one that happens to mention Next",
              !emu48.nextzxos_resident(why) && why == "not a Next machine", why);
        // `ensure_warm_start_state()` returns a bare bool and gives the same
        // `false` from the machine-type guard, the no-SD guard and a failed
        // recording — so the RETURN VALUE alone cannot say which one spoke,
        // and this fixture (a 48K with no SD image) trips two of them. The
        // guards do differ in what they LOG, so read that: same
        // ringbuffer-sink mechanism as log_gate_test / log_test.
        //
        // Asserted in BOTH directions. The positive alone would still pass
        // with the machine-type guard deleted if some later line happened to
        // contain the needle; requiring the no-SD line to be ABSENT is what
        // pins "declined here, and did not fall through".
        {
            LogTap tap(Log::emulator());
            const bool declined = !emu48.ensure_warm_start_state();
            check("WSR-RES-03",
                  "ensure_warm_start_state() declines on a non-Next BY THE "
                  "MACHINE-TYPE GUARD, not by the no-SD guard behind it",
                  declined && tap.count(kWhyMachineType) == 1 &&
                      tap.count(kWhyNoSdImage) == 0,
                  det("machine-type=%d no-sd=%d", tap.count(kWhyMachineType),
                      tap.count(kWhyNoSdImage)));
        }

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
        // The fragment is unique to check 3: no other refusal message contains
        // "no NextZXOS ROM found in SRAM pages". Asserting the bare word
        // "NextZXOS" would be the WSR-RES-01 bug in the other direction.
        check("WSR-RES-02",
              "a Next that never booted is refused BY THE MARKER SEARCH, although "
              "the two firmware-less checks pass",
              !resident &&
                  why.find("no NextZXOS ROM found in SRAM pages") != std::string::npos,
              why);
        // Both checks 1 and 2 must have PASSED for check 3 to be the one
        // that spoke — that is the whole point of the row (a firmware-less
        // machine answers them exactly as a booted one does, GH #226), so
        // assert it directly instead of inferring it from the message.
        check("WSR-RES-06",
              "and it is refused DESPITE the boot-ROM overlay being off and "
              "config mode clear — the two questions a never-booted Next answers "
              "like a booted one (GH #226)",
              !emun.mmu().boot_rom_enabled() && !emun.nextreg().nr_03_config_mode());

        // The mirror image of WSR-RES-03: a Next (so the machine-type guard
        // cannot speak) with no SD image.
        {
            LogTap tap(Log::emulator());
            const bool declined = !emun.ensure_warm_start_state();
            check("WSR-RES-04",
                  "ensure_warm_start_state() declines with no SD image BY THE "
                  "NO-SD GUARD, and the machine-type guard stays silent",
                  declined && tap.count(kWhyNoSdImage) == 1 &&
                      tap.count(kWhyMachineType) == 0,
                  det("no-sd=%d machine-type=%d", tap.count(kWhyNoSdImage),
                      tap.count(kWhyMachineType)));

            // "Every time" is a claim about the SECOND call, so count it.
            // A bare `!declined` twice would also pass if the second call
            // short-circuited somewhere else entirely — which is exactly what
            // the per-image failure latch does for the recording path, and
            // must NOT do here (no SD image is not a verdict about a card).
            const bool again = !emun.ensure_warm_start_state();
            check("WSR-RES-05",
                  "a machine with no SD image declines through the SAME guard "
                  "every time, not via a latched verdict",
                  again && tap.count(kWhyNoSdImage) == 2,
                  det("no-sd=%d over two calls", tap.count(kWhyNoSdImage)));
        }
    }

    // ── The third decline: a recording that was attempted and FAILED ──
    //
    // The one path of the three that actually boots. A readable file that is
    // not a NextZXOS card gets past the machine-type and no-SD guards and past
    // the digest, so `record_warm_start_state()` really runs: a fresh Emulator,
    // 500 frames of firmware that finds nothing, and `nextzxos_resident()`
    // refusing. That is the case GH #234 must never cache — a machine that
    // looks booted and is not — so it is worth the ~seconds it costs here.
    {
        const std::string fake_sd = g_dir + "/not-a-nextzxos-card.img";
        {   // 1 MiB of zeroes: big enough that sector reads stay in range,
            // empty enough that no ROM can be extracted from it.
            std::vector<uint8_t> zeroes(1024 * 1024, 0);
            std::ofstream f(fake_sd, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(zeroes.data()),
                    static_cast<std::streamsize>(zeroes.size()));
        }
        std::filesystem::remove_all(warm_start::cache_dir(), ec);

        Emulator emuf;
        EmulatorConfig cf;
        cf.type = MachineType::ZXN_ISSUE2;
        cf.rewind_buffer_frames = 0;
        cf.sd_card_image = fake_sd;
        emuf.init(cf);

        LogTap tap(Log::emulator());
        const bool declined = !emuf.ensure_warm_start_state();
        const bool cached = std::filesystem::exists(warm_start::cache_path(0));
        check("WSR-RES-07",
              "a card with no firmware on it declines AFTER A REAL BOOT — named "
              "as such, past the machine-type and no-SD guards — and caches nothing",
              declined && tap.count(kWhyNotRecorded) == 1 &&
                  tap.count(kWhyMachineType) == 0 && tap.count(kWhyNoSdImage) == 0 &&
                  !cached,
              det("not-recorded=%d machine-type=%d no-sd=%d cached=%d",
                  tap.count(kWhyNotRecorded), tap.count(kWhyMachineType),
                  tap.count(kWhyNoSdImage), cached ? 1 : 0));

        // The per-IMAGE failure latch, which is the opposite of WSR-RES-05's
        // claim and deliberately so: "no SD image" is not a verdict about a
        // card and must be re-asked, but "this card cannot produce a NextZXOS"
        // is, and re-asking it costs another 500-frame boot on every load.
        const bool again = !emuf.ensure_warm_start_state();
        check("WSR-RES-08",
              "and the verdict is latched for that image — a second call declines "
              "without booting again",
              again && tap.count(kWhyNotRecorded) == 1,
              det("not-recorded=%d over two calls", tap.count(kWhyNotRecorded)));
    }

    std::filesystem::remove_all(g_dir, ec);

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
