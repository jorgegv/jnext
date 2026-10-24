// `.jns` whole-machine save and load — GH #27 stage S8.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §6, §10, §11.3, §12, §15.
//
// ── WHAT THIS FILE IS, AND WHY IT DID NOT EXIST BEFORE S8 ────────────────
//
// S1 built the container, S2 the field descriptor and its three realisations,
// S3-S5b migrated thirty-four subsystems onto it, S6 closed the state gaps and
// S7 produced the SD identity. Every PART of a `.jns` existed. Nothing
// assembled one: no code walked the declarations with `JsonWriteDesc`, emitted
// the `state/*.json` members and the `mem/*.bin` blobs, filled the manifest and
// handed the lot to `SnapshotWriter`. This file is that, and its inverse.
//
// ── THE ONE LIST ─────────────────────────────────────────────────────────
//
// `Emulator::visit_jns_subsystems` is the single enumeration, walked by BOTH
// directions through a visitor. Two hand-kept lists is the shape that produces
// "saved but never restored" — the defect class the descriptor layer exists to
// make impossible one level down, applied one level up.
//
// ── ORDER IS NOT SEMANTICS HERE ──────────────────────────────────────────
//
// The binary stream is positional: order IS the format, and a sentinel after
// every block exists to localise a desync. A `.jns` is named members carrying
// named keys, so the visit order below decides only which member is written
// first. It follows `save_state`'s order anyway, because a reader comparing
// the two should not have to hold a permutation in their head.

#include "core/emulator.h"

#include "core/jns_snapshot.h"
#include "core/log.h"
#include "core/program_archive.h"
#include "core/saveable.h"
#include "core/embedded_nextboot_rom.h"
#include "core/sd_snapshot_identity.h"
#include "core/sdcard_provisioner.h"
#include "peripheral/joy_uart_source.h"
#include "version.h"
#include "save/jns_container.h"
#include "save/state_desc_json.h"

#include "third_party/nlohmann-json/nlohmann/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>

using nlohmann::json;
using jnext::save::JsonWriteDesc;

namespace {

/// `state/<name>.json`, the one place the member path is spelled.
std::string state_member(const std::string& name) {
    return std::string(jnext::jns::kStatePrefix) + name + ".json";
}

/// The blob member path for one declaration's blob key.
///
/// THE NAMES ARE THE SPEC'S, NOT A RULE (§6.1 lists them literally:
/// `mem/ram.bin`, `mem/bank5-vram.bin`, `mem/sprite-patterns.bin`,
/// `mem/bank7-bram.bin`, `mem/multiface-ram.bin`). A generated
/// `mem/<subsystem>-<key>.bin` looks tidier and is wrong twice over: it is not
/// what the document says a `.jns` contains, and `mem/ram.bin` in particular is
/// spelled *in the code* — `JsonWriteDesc::ram_window` emits it as the `ref` of
/// the DivMMC window and `JsonReadDesc::ram_window` refuses any other value. A
/// derived name therefore produced an archive whose own RAM reference pointed
/// at a member that did not exist.
///
/// A table, so adding a blob is a deliberate edit rather than whatever a
/// generator happens to produce. An unknown pair is a hard failure: silently
/// inventing a name is how the first version of this went wrong.
const char* blob_member(const std::string& subsystem, const std::string& key) {
    if (subsystem == "ram"       && key == "ram")         return "mem/ram.bin";
    if (subsystem == "mmu"       && key == "bank5_vram")  return "mem/bank5-vram.bin";
    if (subsystem == "mmu"       && key == "bank7_bram")  return "mem/bank7-bram.bin";
    if (subsystem == "sprites"   && key == "pattern_ram") return "mem/sprite-patterns.bin";
    if (subsystem == "multiface" && key == "ram")         return "mem/multiface-ram.bin";
    return nullptr;
}

std::string iso8601_now_utc() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    if (gmtime_s(&tm, &now) != 0) return {};
#else
    if (gmtime_r(&now, &tm) == nullptr) return {};
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) return {};
    return buf;
}


// ── state/tape.json (GH #402) ──────────────────────────────────────────────
//
// The deck's position. Identity (path, digest, mode, the archive a .zip tape
// came from) stays in the manifest; this member holds what only the loaders
// know. Hand-written like esxdos_hostfs (§9.5(4)) and presence-gated like
// joy_uart (§9.5(5)): written only when a tape is attached. Every 64-bit value
// is a decimal string (§7.4); times that are relative to the save instant are
// signed distances from `now` (§9.5(3)), because load_jns re-bases the clock.

const char* const kTypistStates[] = {"inactive", "waiting", "post_delay"};
const char* const kTypistModes[]  = {"jpp", "enter"};

/// Everything state/tape.json holds, in memory.
struct TapeMember {
    std::string                 loader;       ///< "tap" | "tzx" | "wav"
    bool                        auto_play = true;
    TapePollDetector::Snapshot  poll;
    PhantomTypist::Snapshot     typist;
    TapLoader::DeckState        tap;
    TzxLoader::DeckState        tzx;
    WavLoader::DeckState        wav;
};

json tape_member_to_json(const TapeMember& t, uint64_t now)
{
    json j;
    j["loader"]    = t.loader;
    j["auto_play"] = t.auto_play;
    j["poll"] = {{"valid", t.poll.valid}, {"run", t.poll.run},
                 {"last_in", std::to_string(static_cast<int64_t>(t.poll.last - now))}};
    j["typist"] = {{"state", kTypistStates[t.typist.state]},
                   {"mode", kTypistModes[t.typist.mode]},
                   {"ports_read", t.typist.ports_read},
                   {"post_delay", t.typist.post_delay}};
    if (t.loader == "tap") {
        const auto& d = t.tap;
        j["tap"] = {{"block", d.block}, {"playing", d.playing}, {"held", d.held},
                    {"phase", d.phase}, {"pulse_left", d.pulse_left},
                    {"leader_left", d.leader_left}, {"byte", d.byte}, {"bit", d.bit},
                    {"half", d.half}, {"ear", d.ear}};
    } else if (t.loader == "tzx") {
        const auto& d = t.tzx;
        j["tzx"] = {{"fast_offset", d.fast_offset}, {"offset", d.offset},
                    {"playing", d.playing}, {"held", d.held}, {"level", d.level},
                    {"edge_in", std::to_string(d.edge_in)}, {"phase", d.phase},
                    {"pilot_pulse", d.pilot_pulse}, {"sync1", d.sync1}, {"sync2", d.sync2},
                    {"zero_pulse", d.zero_pulse}, {"one_pulse", d.one_pulse},
                    {"pilot_left", d.pilot_left}, {"block_data", d.block_data},
                    {"data_len", d.data_len}, {"data_pos", d.data_pos},
                    {"bit_pos", d.bit_pos}, {"pulse_half", d.pulse_half},
                    {"used_bits_last", d.used_bits_last},
                    {"pause_tstates", d.pause_tstates}, {"tone_pulse", d.tone_pulse},
                    {"tone_left", d.tone_left}, {"pulse_lengths", d.pulse_lengths},
                    {"pulse_count", d.pulse_count}, {"pulse_idx", d.pulse_idx},
                    {"sample_tstates", d.sample_tstates},
                    {"direct_level", d.direct_level}, {"loop_start", d.loop_start},
                    {"loop_count", d.loop_count}, {"gap_done", d.gap_done}};
    } else {
        j["wav"] = {{"playing", t.wav.playing},
                    {"start_in", std::to_string(t.wav.start_in)}};
    }
    return j;
}

/// Typed reads from the member. Each names the offending key and returns false
/// on a missing key, a wrong type or a value outside [lo, hi]: a hand-edited or
/// damaged member must never reach a loader.
struct TapeJsonReader {
    std::string why;

    bool fail(const std::string& where, const char* key, const char* what) {
        why = where + "." + key + " " + what;
        return false;
    }
    bool num(const json& o, const std::string& where, const char* key,
             int64_t lo, int64_t hi, int64_t& out) {
        if (!o.is_object() || !o.contains(key)) return fail(where, key, "is missing");
        const json& v = o.at(key);
        if (!v.is_number_integer()) return fail(where, key, "is not an integer");
        if (v.is_number_unsigned()) {
            const uint64_t u = v.get<uint64_t>();
            if (u > static_cast<uint64_t>(hi)) return fail(where, key, "is out of range");
            out = static_cast<int64_t>(u);
        } else {
            out = v.get<int64_t>();
            if (out < lo || out > hi) return fail(where, key, "is out of range");
        }
        return true;
    }
    bool flag(const json& o, const std::string& where, const char* key, bool& out) {
        if (!o.is_object() || !o.contains(key)) return fail(where, key, "is missing");
        if (!o.at(key).is_boolean()) return fail(where, key, "is not a boolean");
        out = o.at(key).get<bool>();
        return true;
    }
    bool str(const json& o, const std::string& where, const char* key, std::string& out) {
        if (!o.is_object() || !o.contains(key)) return fail(where, key, "is missing");
        if (!o.at(key).is_string()) return fail(where, key, "is not a string");
        out = o.at(key).get<std::string>();
        return true;
    }
    /// A signed 64-bit value written as decimal text (§7.4).
    bool i64s(const json& o, const std::string& where, const char* key, int64_t& out) {
        std::string t;
        if (!str(o, where, key, t)) return false;
        errno = 0;
        char* end = nullptr;
        const long long v = std::strtoll(t.c_str(), &end, 10);
        if (t.empty() || *end != '\0' || errno == ERANGE)
            return fail(where, key, "is not a decimal integer");
        out = v;
        return true;
    }
    bool obj(const json& o, const char* key, const json*& out) {
        if (!o.is_object() || !o.contains(key) || !o.at(key).is_object()) {
            why = std::string(key) + " is missing or not an object";
            return false;
        }
        out = &o.at(key);
        return true;
    }
};

template <size_t N>
int name_index(const char* const (&names)[N], const std::string& s)
{
    for (size_t i = 0; i < N; ++i) if (s == names[i]) return static_cast<int>(i);
    return -1;
}

bool tape_member_from_json(const std::string& text, uint64_t now, TapeMember& t,
                           std::string& why)
{
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        why = "state/tape.json is not a JSON object";
        return false;
    }
    TapeJsonReader r;
    auto done = [&](bool ok) { if (!ok) why = r.why; return ok; };
    int64_t v = 0;

    if (!r.str(j, "tape", "loader", t.loader)) return done(false);
    if (t.loader != "tap" && t.loader != "tzx" && t.loader != "wav")
        return done(r.fail("tape", "loader", "is not tap, tzx or wav"));
    if (!r.flag(j, "tape", "auto_play", t.auto_play)) return done(false);

    const json* o = nullptr;
    if (!r.obj(j, "poll", o)) return done(false);
    if (!r.flag(*o, "poll", "valid", t.poll.valid) ||
        !r.num(*o, "poll", "run", 0, UINT32_MAX, v)) return done(false);
    t.poll.run = static_cast<uint32_t>(v);
    if (!r.i64s(*o, "poll", "last_in", v)) return done(false);
    t.poll.last = now + static_cast<uint64_t>(v);

    if (!r.obj(j, "typist", o)) return done(false);
    std::string name;
    if (!r.str(*o, "typist", "state", name)) return done(false);
    int idx = name_index(kTypistStates, name);
    if (idx < 0) return done(r.fail("typist", "state", "is unknown"));
    t.typist.state = static_cast<uint8_t>(idx);
    if (!r.str(*o, "typist", "mode", name)) return done(false);
    idx = name_index(kTypistModes, name);
    if (idx < 0) return done(r.fail("typist", "mode", "is unknown"));
    t.typist.mode = static_cast<uint8_t>(idx);
    if (!r.num(*o, "typist", "ports_read", 0, 255, v)) return done(false);
    t.typist.ports_read = static_cast<uint8_t>(v);
    if (!r.num(*o, "typist", "post_delay", 0, 255, v)) return done(false);
    t.typist.post_delay = static_cast<uint8_t>(v);
    { PhantomTypist probe; if (!probe.restore(t.typist)) return done(r.fail("typist", "post_delay", "is out of range")); }

    auto u32 = [&](const char* where, const char* key, uint32_t& out) {
        if (!r.num(*o, where, key, 0, UINT32_MAX, v)) return false;
        out = static_cast<uint32_t>(v);
        return true;
    };
    auto i32 = [&](const char* where, const char* key, int32_t& out) {
        if (!r.num(*o, where, key, INT32_MIN, INT32_MAX, v)) return false;
        out = static_cast<int32_t>(v);
        return true;
    };

    if (t.loader == "tap") {
        if (!r.obj(j, "tap", o)) return done(false);
        auto& d = t.tap;
        return done(u32("tap", "block", d.block) && r.flag(*o, "tap", "playing", d.playing) &&
                    r.flag(*o, "tap", "held", d.held) && r.str(*o, "tap", "phase", d.phase) &&
                    u32("tap", "pulse_left", d.pulse_left) &&
                    u32("tap", "leader_left", d.leader_left) && u32("tap", "byte", d.byte) &&
                    u32("tap", "bit", d.bit) && u32("tap", "half", d.half) &&
                    u32("tap", "ear", d.ear));
    }
    if (t.loader == "tzx") {
        if (!r.obj(j, "tzx", o)) return done(false);
        auto& d = t.tzx;
        return done(i32("tzx", "fast_offset", d.fast_offset) && i32("tzx", "offset", d.offset) &&
                    r.flag(*o, "tzx", "playing", d.playing) && r.flag(*o, "tzx", "held", d.held) &&
                    u32("tzx", "level", d.level) && r.i64s(*o, "tzx", "edge_in", d.edge_in) &&
                    r.str(*o, "tzx", "phase", d.phase) && u32("tzx", "pilot_pulse", d.pilot_pulse) &&
                    u32("tzx", "sync1", d.sync1) && u32("tzx", "sync2", d.sync2) &&
                    u32("tzx", "zero_pulse", d.zero_pulse) && u32("tzx", "one_pulse", d.one_pulse) &&
                    i32("tzx", "pilot_left", d.pilot_left) && i32("tzx", "block_data", d.block_data) &&
                    i32("tzx", "data_len", d.data_len) && i32("tzx", "data_pos", d.data_pos) &&
                    i32("tzx", "bit_pos", d.bit_pos) && i32("tzx", "pulse_half", d.pulse_half) &&
                    i32("tzx", "used_bits_last", d.used_bits_last) &&
                    u32("tzx", "pause_tstates", d.pause_tstates) &&
                    u32("tzx", "tone_pulse", d.tone_pulse) && i32("tzx", "tone_left", d.tone_left) &&
                    i32("tzx", "pulse_lengths", d.pulse_lengths) &&
                    i32("tzx", "pulse_count", d.pulse_count) && i32("tzx", "pulse_idx", d.pulse_idx) &&
                    u32("tzx", "sample_tstates", d.sample_tstates) &&
                    u32("tzx", "direct_level", d.direct_level) && i32("tzx", "loop_start", d.loop_start) &&
                    i32("tzx", "loop_count", d.loop_count) && u32("tzx", "gap_done", d.gap_done));
    }
    if (!r.obj(j, "wav", o)) return done(false);
    return done(r.flag(*o, "wav", "playing", t.wav.playing) &&
                r.i64s(*o, "wav", "start_in", t.wav.start_in));
}

/// Can the tape a manifest names be reopened right now? From the archive when
/// it came out of a .zip (GH #402), else from `path` exactly as recorded: an
/// absolute path is the tape; a bare name (older files) resolves against the
/// CWD, as it always did (GH #401). No filesystem work beyond one open.
bool tape_source_available(const jnext::jns::TapeInfo& t)
{
    const std::string& src = t.archive.empty() ? t.path : t.archive;
    if (src.empty()) return false;
    std::ifstream f(src, std::ios::binary);
    return static_cast<bool>(f);
}

/// How the tape is named in a warning: '<path>', or '<archive>' (<member>).
std::string tape_label(const jnext::jns::TapeInfo& t)
{
    if (!t.archive.empty()) return "'" + t.archive + "' (" + t.member + ")";
    return "'" + (t.path.empty() ? std::string("(unnamed)") : t.path) + "'";
}

}  // namespace

namespace jnext {

bool is_jns_path(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".jns";
}

}  // namespace jnext

// ─────────────────────────────────────────────────────────────────────────
// The ONE list
// ─────────────────────────────────────────────────────────────────────────

template <typename V>
void Emulator::visit_jns_subsystems(V&& v) {
    // Core.
    v("clock",    clock_);
    v("ram",      ram_);
    v("mmu",      mmu_);
    v("nextreg",  nextreg_);
    v("cpu",      cpu_);
    // §9.5(2): the binary stream puts the IM2 fabric and its timing in two
    // sentinel-delimited blocks. One member here, because nothing in JSON
    // needs them apart.
    v("im2",      im2_,  &Im2Controller::describe_state,
                         &Im2Controller::describe_timing);

    // Video.
    v("palette",  palette_);
    v("layer2",   layer2_);
    v("sprites",  sprites_);
    v("tilemap",  tilemap_);
    v("renderer", renderer_);        // includes the ULA and its histories

    // Peripherals.
    v("copper",   copper_);
    v("ctc",      ctc_,  &Ctc::describe_state, &Ctc::describe_timing);
    v("dma",      dma_);
    v("spi",      spi_);
    v("i2c",      i2c_);
    v("rtc",      rtc_);
    v("uart",     uart_);
    v("divmmc",   divmmc_);
    v("multiface", multiface_);
    v("nmi_source", nmi_source_);
    v("sdcard",   sd_card_);

    // Audio.
    v("beeper",     beeper_);
    v("turbosound", turbosound_);
    v("dac",        dac_);
    v("i2s",        i2s_);

    // Input. Six classes, six members: the binary stream wraps them in one
    // sentinel block because a sentinel is per-block, not per-class, and there
    // is no such constraint here.
    v("keyboard",       keyboard_);
    v("joystick",       joystick_);
    v("mouse",          mouse_);
    v("md6",            md6_);
    v("membrane_stick", membrane_stick_);
    v("iomode",         iomode_);

    // The Emulator's own scalars: five declarations the binary stream must
    // keep in five blocks, plus S8's sixth for the §9.5 exceptions. All six
    // into `state/emulator.json`, which is where §9.5(2) says they belong.
    v("emulator", *this, &Emulator::describe_frame_origin,
                         &Emulator::describe_state,
                         &Emulator::describe_nmi_tail,
                         &Emulator::describe_nextreg_appends,
                         &Emulator::describe_tail,
                         &Emulator::describe_jns_exceptions);
}

// ─────────────────────────────────────────────────────────────────────────
// The sixth declaration — §9.5's exceptions, staged
// ─────────────────────────────────────────────────────────────────────────

void Emulator::describe_jns_exceptions(jnext::save::StateDesc& d)
{
    // §9.5(3). The live FUSE T-state counter is serialised nowhere, so the
    // binary stream writes the FOLDED value (base + live at capture) and
    // `load_state` re-establishes it as the new base with a zeroed live
    // counter. `monotonic_tstates()` is then exactly continuous across a
    // restore, including one taken from a mid-frame pause. The fold and the
    // re-seating happen in `save_jns`/`load_jns`; here it is a plain `u64`.
    d.u64("monotonic_tstates", jns_monotonic_tstates_);

    // §9.5(3), same class. The CPU's /INT window, written RELATIVE to the FUSE
    // counter because that counter is not restored — `load_state` re-seeds it
    // at the next frame start. An open-ended window's `INT64_MAX` end travels
    // as `"open"` (§7.4), which is what `i64_open` is for.
    d.i64("int_window_first", jns_int_first_ts_);
    d.i64_open("int_window_last", jns_int_last_ts_);

    // GH #84 / G49 — the hidden "next RETN reads NR C3:C2" latch. The public
    // CPU registers cannot reconstruct it: PC may be anywhere in the handler
    // and ordinary code can run with SP two bytes below an earlier value.
    d.boolean("stackless_retn_active", jns_stackless_retn_, false);

    // §9.5(5). Presence, not content: the binary stream needs these because a
    // positional reader has to know whether the next bytes are there at all.
    // A `.jns` reader could infer both from whether the member exists — and
    // they are carried anyway, because "the writer deliberately had none" and
    // "the member is missing or corrupt" are different failures that §8 says
    // must not be conflated.
    d.boolean("joy_uart_present",  jns_joy_uart_present_,  false);
    d.boolean("multiface_present", jns_multiface_present_, true);
}

// ─────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────

namespace {

/// The manifest's `model.machine`, in the vocabulary the container's
/// `constructible_machines` set and `parse_machine_type` both use:
/// `machine_type_key`. NOT `machine_type_str`, which returns display strings
/// ("ZX Next", "+3") a reader would have to special-case back.
inline const char* manifest_machine_name(MachineType t) { return machine_type_key(t); }

/// The write visitor. One member per subsystem, plus every blob that
/// subsystem's declaration says it owns.
struct SaveVisitor {
    jnext::jns::SnapshotWriter* w = nullptr;
    std::string*                why = nullptr;
    bool                        ok  = true;

    template <typename T, typename... M>
    void operator()(const char* name, T& obj, M... methods) {
        if (!ok) return;
        std::vector<JsonWriteDesc::BlobRef> blobs;
        std::string text;
        if (sizeof...(methods) == 0) {
            JsonWriteDesc d;
            d.set_machine_level(true);
            obj.describe_state(d);
            if (d.failed()) {
                *why = std::string("subsystem ") + name + ": " +
                       (d.failure() ? d.failure() : "declaration failed");
                ok = false;
                return;
            }
            blobs = d.blobs();
            text  = d.str();
        } else {
            JsonWriteDesc d;
            d.set_machine_level(true);
            // A fold over the pack: every declaration into ONE document
            // (§9.5(2)).
            using expand = int[];
            (void)expand{0, ((obj.*methods)(d), 0)...};
            if (d.failed()) {
                *why = std::string("subsystem ") + name + ": " +
                       (d.failure() ? d.failure() : "declaration failed");
                ok = false;
                return;
            }
            blobs = d.blobs();
            text  = d.str();
        }

        if (!w->add_subsystem(name, text, *why)) { ok = false; return; }
        for (const auto& b : blobs) {
            const char* member = blob_member(name, b.key);
            if (!member) {
                *why = std::string("subsystem ") + name + " declares blob '" +
                       b.key + "', which has no member name in section 6.1's layout";
                ok = false;
                return;
            }
            if (!w->add_blob(member, b.data, b.len, *why)) { ok = false; return; }
        }
    }
};

}  // namespace

bool Emulator::save_jns(const jnext::JnsSaveOptions& opt,
                        std::vector<uint8_t>& out,
                        jnext::JnsLoadReport& report, std::string& why)
{
    out.clear();
    why.clear();

    // §10.2 P7, owner decision: ALWAYS advance, NEVER refuse. A snapshot is
    // only coherent at a frame boundary, the debugger breaks mid-frame, and
    // that is exactly when a developer reaches for File ▸ Save Snapshot. The
    // advance completes the in-flight frame through the ordinary path — it
    // does NOT re-run `begin_new_frame()` on a frame already in progress,
    // which is the Task 40 defect that wiped `beast.nex`'s Copper gradient.
    report.advanced_to_frame_boundary = advance_to_frame_boundary();

    // Stage the four §9.5 exceptions so the sixth declaration can carry them.
    jns_monotonic_tstates_ = monotonic_tstates();
    {
        const int64_t now_ts = static_cast<int64_t>(*fuse_z80_tstates_ptr());
        const int64_t last   = cpu_.int_window_last_ts();
        jns_int_first_ts_ = cpu_.int_window_first_ts() - now_ts;
        jns_int_last_ts_  = (last == INT64_MAX) ? last : last - now_ts;
    }
    jns_stackless_retn_    = cpu_.stackless_retn_active();
    jns_joy_uart_present_  = (joy_uart_source_ != nullptr);
    jns_multiface_present_ = true;

    jnext::jns::SnapshotWriter w(opt.uncompressed);
    jnext::jns::Manifest m;
    m.format_version = jnext::jns::kFormatVersion;
    m.created        = iso8601_now_utc();

    m.producer.jnext_version = JNEXT_VERSION_STRING;
#ifdef __linux__
    m.producer.platform = "linux";
#elif defined(_WIN32)
    m.producer.platform = "windows";
#elif defined(__APPLE__)
    m.producer.platform = "macos";
#else
    m.producer.platform = "unknown";
#endif

    m.model.machine        = manifest_machine_name(config_.type);
    m.model.ram_kb         = static_cast<uint32_t>(ram_.size() / 1024);
    m.model.timing         = manifest_machine_name(config_.type);
    m.model.cpu_speed_nr07 = nextreg_.read(0x07) & 0x03;

    // §8 — WHICH FRAME THIS IS, and deliberately NOT `frame_num_`. That
    // member is the REWIND RING's ordinal: it is incremented only by
    // `take_snapshot`, so with rewind off — which is every headless run and
    // the default — it stays 0 and every manifest would carry `"frame": 0`.
    // A provenance field that reads zero in the common case is worse than
    // none, because a reader cannot tell it apart from a real frame 0.
    //
    // The monotonic T-state clock is the honest source: it counts from the
    // machine's start, is continuous across restores, and divided by the
    // machine's own frame length gives the frame ordinal a reader means by
    // "when was this taken".
    {
        const uint64_t per_frame =
            static_cast<uint64_t>(timing_.lines_per_frame) *
            static_cast<uint64_t>(timing_.tstates_per_line);
        m.capture.frame = per_frame ? (monotonic_tstates() / per_frame) : 0;
    }
    m.capture.frame_boundary = true;   // guaranteed by the advance above

    // ── media.sdcard (§11.3, S7's producer) ─────────────────────────────
    if (!config_.sd_card_image.empty()) {
        std::string card_why;
        if (!jnext::describe_sdcard_for_snapshot(config_.sd_card_image,
                                                 config_.sd_card_readonly,
                                                 m.sdcard, card_why)) {
            // Not fatal: a card jnext cannot identify is still a card the
            // machine is using, and refusing to SAVE because of it would be
            // the cries-wolf failure one step earlier. The reader's
            // unpopulated-identity rule (JNSI-13) then refuses the RESTORE,
            // which is where the decision belongs.
            Log::emulator()->warn("save_jns: SD identity unavailable: {}",
                                  card_why);
            report.warnings.push_back(
                "the mounted SD card could not be identified (" + card_why +
                "); this snapshot will not verify it on restore");
        }
    }

    // ── media.roms (§8, §10.2 P3) ───────────────────────────────────────
    //
    // The ROM this machine is RUNNING, not the file it came from: `rom_` is
    // what the CPU fetches, and digesting it is a claim that can be checked
    // rather than one that depends on a path still meaning what it meant.
    //
    // On the Next it is provenance rather than a check — ROM content sits in
    // SRAM pages inside `ram_` and travels in the snapshot already — so two
    // Next snapshots agree here by construction, which is correct.
    {
        const uint8_t* rom_base = rom_.page_ptr(0);
        if (rom_base) {
            m.roms.source = "sdcard";
            m.roms.sha256["rom"] = sdcard::sha256_hex(
                std::vector<uint8_t>(rom_base, rom_base + Rom::ROM_SIZE));
        }
        m.roms.boot_rom_sha256 = sdcard::sha256_hex(
            std::vector<uint8_t>(embedded_nextboot_rom_data(),
                                 embedded_nextboot_rom_data() +
                                     embedded_nextboot_rom_size()));
    }

    // ── media.tape (§8, §10.2 P4) ───────────────────────────────────────
    //
    // Recorded by REOPENABLE IDENTITY, never copied — the esxDOS-handle shape.
    // Tape state is excluded from the state stream by design (position is
    // independent of CPU rewind), so without this a snapshot taken during a
    // load restores a machine waiting for a tape that is not playing.
    {
        const std::string tape_path =
            tape_.is_loaded()     ? tape_.path()     :
            tzx_tape_.is_loaded() ? tzx_tape_.path() :
            wav_tape_.is_loaded() ? wav_tape_.path() : std::string();
        if (!tape_path.empty()) {
            m.tape.present          = true;
            m.tape.path             = tape_path;
            // The digest of the bytes the LOADER read (GH #402), so it
            // describes the deck's content even if the file was edited after
            // it was attached; load_jns compares it with a fresh read.
            m.tape.sha256 =
                tape_.is_loaded()     ? tape_.sha256()     :
                tzx_tape_.is_loaded() ? tzx_tape_.sha256() : wav_tape_.sha256();
            m.tape.position_tstates = tape_sample_tstates();
            // `--tape-realtime` lives in the FRONTEND, not in EmulatorConfig,
            // so the emulator-side truth is the loader's own flag: real-time
            // playback is exactly "not fast-load".
            m.tape.realtime =
                tape_.is_loaded()     ? !tape_.fast_load()     :
                tzx_tape_.is_loaded() ? !tzx_tape_.fast_load() : true;
            // A tape extracted from a .zip lives in a scratch directory that
            // dies with this process: name the archive too (GH #402).
            program_archive::extracted_from(tape_path, m.tape.archive, m.tape.member);
        }
    }

    // ── meta/preview.png (§10.2 P5) ─────────────────────────────────────
    if (!opt.preview_png.empty()) {
        m.preview.present = true;
        m.preview.width   = opt.preview_width;
        m.preview.height  = opt.preview_height;
    }

    m.esxdos_root = config_.esxdos_stub_root;

    w.set_manifest(m);

    // ── the subsystems ──────────────────────────────────────────────────
    SaveVisitor v;
    v.w   = &w;
    v.why = &why;
    cpu_.marshal_for_describe();   // the FUSE-only EI grace / iff2 quirk flag (GH #402 review)
    visit_jns_subsystems(v);
    if (!v.ok) return false;

    // ── §9.5(4): the esxDOS handle table, a variable-length list ────────
    //
    // The one part of the machine no declaration can express: a count and then
    // that many (path, offset, mode) triples. Hand-written here rather than
    // forced into the descriptor vocabulary, which is what §9.5 exists to
    // permit — and it is a `state/` member like any other, so a reader that
    // does not know it ignores it and says so.
    {
        json j;
        json handles = json::array();
        for (const auto& h : esxdos_hostfs_.snapshot()) {
            json e;
            e["handle"]   = h.handle;
            e["is_dir"]   = h.is_dir;
            e["mode"]     = h.mode;
            e["position"] = std::to_string(h.position);   // §7.4: u64 as text
            e["path"]     = h.path;
            handles.push_back(e);
        }
        j["handles"] = handles;
        j["cwd"]     = esxdos_hostfs_.cwd_for_snapshot();
        if (!w.add_subsystem("esxdos_hostfs", j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n", why)) return false;
    }

    // ── §9.5(5): the joystick cable, present only when attached ─────────
    if (joy_uart_source_) {
        const JoyUartSource::Snapshot s = joy_uart_source_->snapshot_for_jns();
        json j;
        j["pos"]       = s.pos;
        j["delivered"] = s.delivered;
        j["dropped"]   = s.dropped;
        j["frames"]    = s.frames;
        j["timer"]     = s.timer;
        if (!w.add_subsystem("joy_uart", j.dump(2) + "\n", why)) return false;
    }

    // ── GH #402: the tape deck's position, present only when a tape is attached ──
    if (m.tape.present) {
        TapeMember t;
        t.auto_play = tape_auto_play_;
        t.poll      = tape_poll_.snapshot();
        t.typist    = phantom_typist_.snapshot();
        if (tape_.is_loaded())          { t.loader = "tap"; t.tap = tape_.deck_state(); }
        else if (tzx_tape_.is_loaded()) { t.loader = "tzx"; t.tzx = tzx_tape_.deck_state(jns_monotonic_tstates_); }
        else                            { t.loader = "wav"; t.wav = wav_tape_.deck_state(jns_monotonic_tstates_); }
        const json j = tape_member_to_json(t, jns_monotonic_tstates_);
        if (!w.add_subsystem("tape", j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n", why)) return false;
    }

    if (!opt.preview_png.empty()) {
        if (!w.add_meta("meta/preview.png", opt.preview_png.data(),
                        opt.preview_png.size(), why)) {
            return false;
        }
    }

    return w.finish(out, why);
}

// ─────────────────────────────────────────────────────────────────────────
// Load
// ─────────────────────────────────────────────────────────────────────────

namespace {

/// The read visitor. Mirrors `SaveVisitor` field for field — deliberately, so
/// the two can be read side by side and a divergence is visible rather than
/// inferred.
///
/// A MISSING member is not a failure here: §12.4 says a subsystem the manifest
/// does not list was deliberately not saved, and the container has already
/// refused the case where the manifest lists one that is absent. A subsystem
/// with no member keeps the state `reset()` left it in.
struct LoadVisitor {
    const jnext::zip::Reader* zip = nullptr;
    const jnext::jns::Manifest* manifest = nullptr;
    jnext::JnsLoadReport*     report = nullptr;
    std::string*              why = nullptr;
    bool                      ok  = true;

    bool fetch(const char* name, std::string& text) {
        const std::string member = state_member(name);
        if (!zip->has(member)) {
            // §12.4: a subsystem the manifest does not list was DELIBERATELY
            // not saved, and is not a failure. It is not silent either — the
            // subsystem keeps whatever `reset()` left, which is a real
            // difference from the machine that was saved, and a user restoring
            // a file from a jnext that did not write it should be told rather
            // than left to wonder why the sound is wrong.
            //
            // Our own writer always writes all of them (`JNS-RT-09` pins the
            // list), so this only arises for a foreign or older file.
            report->warnings.push_back(
                std::string("the snapshot carries no state for '") + name +
                "'; it has been left at its power-on defaults");
            return false;
        }
        std::string read_why;
        if (!zip->read_text(member, text, read_why)) {
            *why = member + ": " + read_why;
            ok = false;
            return false;
        }
        return true;
    }

    void note_unclaimed(const char* name,
                        const std::vector<std::string>& keys) {
        for (const std::string& k : keys) {
            report->ignored_keys.push_back(std::string(name) + "." + k);
        }
    }

    template <typename T, typename... M>
    void operator()(const char* name, T& obj, M... methods) {
        if (!ok) return;
        std::string text;
        if (!fetch(name, text)) return;

        jnext::save::JsonReadDesc d(text);
        d.set_machine_level(true);
        if (!d.failed()) {
            if (sizeof...(methods) == 0) {
                obj.describe_state(d);
            } else {
                using expand = int[];
                (void)expand{0, ((d.failed() ? void() : (obj.*methods)(d)), 0)...};
            }
        }
        note_unclaimed(name, d.unclaimed_keys());
        if (d.failed()) {
            *why = std::string("state/") + name + ".json: " + d.refusal();
            ok = false;
            return;
        }

        // THE BYTES. `JsonReadDesc::blob` records where they go and cannot
        // fetch them — it knows nothing about the archive — so this is the one
        // place that carries them across. Omitting it restored every scalar
        // and no memory at all, which no field-level comparison could see.
        //
        // The length is checked against the DECLARATION, not taken from the
        // file: a member that is not exactly the size the subsystem declared is
        // refused, never truncated and never zero-padded.
        for (const auto& b : d.blobs()) {
            const char* member = blob_member(name, b.key);
            if (!member) {
                *why = std::string("subsystem ") + name + " declares blob '" +
                       b.key + "', which has no member name in section 6.1's layout";
                ok = false;
                return;
            }
            if (!zip->has(member)) {
                *why = std::string(member) + " is missing, but state/" + name +
                       ".json declares it";
                ok = false;
                return;
            }
            std::vector<uint8_t> bytes;
            std::string read_why;
            if (!zip->read(member, bytes, read_why)) {
                *why = std::string(member) + ": " + read_why;
                ok = false;
                return;
            }
            if (bytes.size() != b.len) {
                *why = std::string(member) + " is " +
                       std::to_string(bytes.size()) + " bytes, but " + name +
                       " declares " + std::to_string(b.len);
                ok = false;
                return;
            }
            if (b.data && b.len) std::memcpy(b.data, bytes.data(), b.len);
        }
    }
};

}  // namespace

bool Emulator::check_jns(const uint8_t* data, std::size_t len,
                        const jnext::JnsLoadOptions& opt,
                        jnext::zip::Reader& zip, jnext::jns::Manifest& m,
                        jnext::jns::Verdict& v)
{

    // ── Everything checkable is checked BEFORE the machine is touched ────
    //
    // The container validates the framing, the manifest, the blob
    // declarations and the media identity, and hands back an open archive.
    // A refusal here leaves the running machine exactly as it was, which is
    // the property a user reloading the wrong file depends on.
    jnext::jns::ReaderEnv env;
    env.machine              = manifest_machine_name(config_.type);
    env.state_model_revision = 1;
    env.strict               = opt.strict;
    env.force_sdcard         = opt.force_sdcard;

    env.constructible_ram_kb = { static_cast<uint32_t>(ram_.size() / 1024) };

    if (!config_.sd_card_image.empty()) {
        std::string card_why;
        // Tier 1 only (GH #396): a few sectors, never the whole image.
        if (!jnext::describe_sdcard_for_snapshot(config_.sd_card_image,
                                                 config_.sd_card_readonly,
                                                 env.card, card_why)) {
            // `env.card.present` stays false, which is exactly right: the
            // container then refuses a snapshot that HAD a card, naming the
            // path — the decision belongs there, not here.
            Log::emulator()->warn("load_jns: the mounted SD card could not be "
                                  "identified: {}", card_why);
        }
    }
    {
        const uint8_t* rom_base = rom_.page_ptr(0);
        if (rom_base) {
            env.roms.source = "sdcard";
            env.roms.sha256["rom"] = sdcard::sha256_hex(
                std::vector<uint8_t>(rom_base, rom_base + Rom::ROM_SIZE));
        }
        env.roms.boot_rom_sha256 = sdcard::sha256_hex(
            std::vector<uint8_t>(embedded_nextboot_rom_data(),
                                 embedded_nextboot_rom_data() +
                                     embedded_nextboot_rom_size()));
    }

    // §10.2 P4 — the container asks whether the tape can be reopened; it does
    // no filesystem I/O itself, which is what keeps its rows cheap.
    {
        jnext::jns::Manifest probe;
        std::vector<std::string> unknown;
        std::string probe_why;
        std::string text;
        jnext::zip::Reader probe_zip;
        if (probe_zip.open(data, len, probe_why) &&
            probe_zip.read_text(jnext::jns::kManifestMember, text, probe_why) &&
            jnext::jns::manifest_from_json(text, probe, unknown, probe_why) &&
            probe.tape.present && !probe.tape.path.empty()) {
            env.tape_file_available = tape_source_available(probe.tape);
        }
    }

    return jnext::jns::open_snapshot(data, len, env, zip, m, v);
}

bool Emulator::probe_jns_file(const std::string& path, std::string& why)
{
    why.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) { why = "cannot open '" + path + "'"; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) { why = "'" + path + "' is empty"; return false; }
    jnext::JnsLoadOptions opt;
    opt.strict       = config_.jns_strict;
    opt.force_sdcard = config_.jns_force_sdcard;
    jnext::zip::Reader   zip;
    jnext::jns::Manifest m;
    jnext::jns::Verdict  v;
    if (!check_jns(bytes.data(), bytes.size(), opt, zip, m, v)) {
        why = v.refusal;
        return false;
    }
    return true;
}

bool Emulator::load_jns(const uint8_t* data, std::size_t len,
                        const jnext::JnsLoadOptions& opt,
                        jnext::JnsLoadReport& report, std::string& why)
{
    why.clear();

    // Everything checkable is checked BEFORE the machine is touched; the
    // same checks run in probe_jns_file (GH #321).
    jnext::zip::Reader   zip;
    jnext::jns::Manifest m;
    jnext::jns::Verdict  v;
    if (!check_jns(data, len, opt, zip, m, v)) {
        why = v.refusal;
        return false;
    }
    report.warnings.insert(report.warnings.end(), v.warnings.begin(),
                           v.warnings.end());
    report.ignored_members = v.ignored_members;
    report.reconfigured_to = v.reconfigure_to;

    // §7.3 — the file's machine wins: the user must not have to get
    // `--machine` right to reload their own save. The same rule the
    // `.sna`/`.szx`/`.z80` loaders follow (GH #323), through the same helper.
    if (!v.reconfigure_to.empty()) {
        MachineType want = config_.type;
        if (parse_machine_type(v.reconfigure_to, want) && want != config_.type) {
            switch_machine_for_load_(want, "the .jns");
            init(config_);
        }
    }

    // ── the subsystems ──────────────────────────────────────────────────
    LoadVisitor lv;
    lv.zip      = &zip;
    lv.manifest = &m;
    lv.report   = &report;
    lv.why      = &why;
    visit_jns_subsystems(lv);
    if (!lv.ok) {
        // A refusal HERE is not the same class as one above: the machine has
        // been partly written and is not trustworthy. Say so the way
        // `load_state`'s sentinel mismatch does rather than implying the
        // machine survived.
        Log::emulator()->error("load_jns: {} - the machine is NOT trustworthy",
                               why);
        return false;
    }

    // ── §9.5(4): the esxDOS handle table ────────────────────────────────
    if (zip.has(state_member("esxdos_hostfs"))) {
        std::string text, read_why;
        if (!zip.read_text(state_member("esxdos_hostfs"), text, read_why)) {
            why = "state/esxdos_hostfs.json: " + read_why;
            return false;
        }
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            why = "state/esxdos_hostfs.json is not a JSON object";
            return false;
        }
        std::vector<EsxdosHostFs::HandleSnapshot> handles;
        if (j.contains("handles") && j.at("handles").is_array()) {
            for (const auto& e : j.at("handles")) {
                if (!e.is_object()) continue;
                EsxdosHostFs::HandleSnapshot h;
                h.handle   = e.value("handle", 0);
                h.is_dir   = e.value("is_dir", false);
                h.mode     = e.value("mode", 0);
                h.position = std::strtoull(
                    e.value("position", std::string("0")).c_str(), nullptr, 10);
                h.path     = e.value("path", std::string());
                handles.push_back(h);
            }
        }
        esxdos_hostfs_.restore(handles);
        esxdos_hostfs_.restore_cwd(j.value("cwd", std::string()));
    }

    // ── §9.5(5): the joystick cable ─────────────────────────────────────
    if (joy_uart_source_ && zip.has(state_member("joy_uart"))) {
        std::string text, read_why;
        if (zip.read_text(state_member("joy_uart"), text, read_why)) {
            json j = json::parse(text, nullptr, false);
            if (!j.is_discarded() && j.is_object()) {
                JoyUartSource::Snapshot s;
                s.pos       = j.value("pos", 0u);
                s.delivered = j.value("delivered", 0u);
                s.dropped   = j.value("dropped", 0u);
                s.frames    = j.value("frames", 0u);
                s.timer     = j.value("timer", 0u);
                joy_uart_source_->restore_from_jns(s);
            }
        }
    }

    // ── the §9.5 exceptions, un-staged ──────────────────────────────────
    //
    // The three with side effects, applied AFTER the walk and in the order
    // `load_state` applies them, because each depends on a counter the walk
    // has just restored.
    // §9.5(3), and in the ORDER `load_state` applies it (`emulator.cpp:12198`):
    // the saved value is the FOLDED monotonic instant, re-established as the
    // base with the live FUSE counter zeroed, so `monotonic_tstates()` is
    // exactly the saved value. What the CPU restored against that counter
    // moves with it, which is what `rebase_interrupt_window` is for, and only
    // THEN is the saved /INT window put on the re-seeded counter.
    tstates_frame_base_ = jns_monotonic_tstates_;
    cpu_.rebase_interrupt_window(static_cast<int64_t>(*fuse_z80_tstates_ptr()));
    *fuse_z80_tstates_ptr() = 0;
    cpu_.apply_after_describe();   // EI grace and iff2_read, which only live in the FUSE struct
    {
        const int64_t now_ts = static_cast<int64_t>(*fuse_z80_tstates_ptr());
        const int64_t first  = jns_int_first_ts_ + now_ts;
        int64_t       last   = jns_int_last_ts_;
        if (last != INT64_MAX) last += now_ts;
        cpu_.set_int_window_for_load(first, last);
    }
    cpu_.set_stackless_retn_active_for_load(jns_stackless_retn_);

    // ── meta/preview.png (§10.2 P5) ─────────────────────────────────────
    if (m.preview.present && zip.has("meta/preview.png")) {
        std::string read_why;
        if (!zip.read("meta/preview.png", report.preview_png, read_why)) {
            report.preview_png.clear();
            report.warnings.push_back("the preview image could not be read (" +
                                      read_why + ")");
        }
    }

    // A restore is only ever applied between frames, so the next `run_frame()`
    // starts one cleanly.
    frame_in_progress_ = false;

    // ── RE-DERIVE, BY ROUND-TRIPPING THROUGH `load_state` ────────────────
    //
    // Restoring every FIELD is not restoring the machine. `load_state`
    // additionally performs about twenty cross-subsystem RE-DERIVATIONS,
    // interleaved through its walk: the contention model rebuilt for the
    // machine type and CPU speed, the video timing re-pushed, DivMMC's
    // rom3 mirror, the I2C peripheral enables, and — the one that shows —
    // GH #261's render-history rebuild, where the ULA's palette selectors are
    // mirrors of the PaletteManager's and are in no stream at all.
    //
    // THIS WAS MEASURED, NOT ASSUMED. Without it a `.jns` round trip produced
    // a machine whose BINARY STATE STREAM was byte-identical to the source's —
    // `JNS-RT-02` passed — and whose rendered screen differed in 139 448
    // pixels. A field-level oracle cannot see a derived-state defect, which is
    // exactly why `snapshot-jns-roundtrip-func` compares PIXELS.
    //
    // Copying those twenty calls here was the obvious fix and is the wrong
    // one: they are interleaved with the walk because several depend on a
    // subsystem loaded just before them, the order is load-bearing, and a
    // hand-kept second copy is the "two lists" failure this whole issue exists
    // to avoid — one which no gate would catch, because the copy would be
    // wrong only in the cases nobody tested.
    //
    // So: serialise the just-restored machine and load it straight back. The
    // round trip is a no-op on every field (`RW-RT-03`/`RW-RT-04` pin that
    // save->load->save is byte-identical) and runs every re-derivation in its
    // own order, from the one place that defines it. It costs one ~2 MB
    // serialise per FILE load, which is not a rate anyone notices, and it
    // cannot drift from `load_state` because it IS `load_state`.
    {
        StateWriter measure;
        save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        save_state(w);
        StateReader rd(buf.data(), buf.size());
        if (!load_state(rd)) {
            why = "the restored machine could not be re-derived (" +
                  last_state_error_ + ")";
            Log::emulator()->error("load_jns: {}", why);
            return false;
        }
    }

    // `load_state` has now fired `on_input_state_restored` and re-synced the
    // ESP association itself, so neither is repeated here.

    // ── GH #402: the tape, back where it was ─────────────────────────────
    //
    // LAST, because the round trip above resets the poll detector and the
    // deck's times are relative to the clock it re-seated. A tape that cannot
    // be put back warns and leaves the deck as it was; it never refuses.
    reattach_jns_tape_(zip, m, opt, report);
    return true;
}

void Emulator::reattach_jns_tape_(const jnext::zip::Reader& zip,
                                  const jnext::jns::Manifest& m,
                                  const jnext::JnsLoadOptions& opt,
                                  jnext::JnsLoadReport& report)
{
    const jnext::jns::TapeInfo& ti = m.tape;
    if (!ti.present) return;
    // The container has already warned once that the tape is not available.
    if (!tape_source_available(ti)) return;

    const std::string label = tape_label(ti);
    auto warn = [&](const std::string& what) {
        report.warnings.push_back(what);
    };

    // An older file records no position: attaching the tape at its start would
    // hand block 0 to a program waiting for a later one, with nothing to say it
    // was a guess. Not attaching is what those files always did.
    const std::string member = state_member("tape");
    const bool listed = std::find(m.subsystems.begin(), m.subsystems.end(), "tape") !=
                        m.subsystems.end();
    if (!listed || !zip.has(member)) {
        warn("the snapshot does not record where the tape " + label +
             " was (it was written by an older jnext); the tape was not reattached");
        return;
    }

    std::string text, why;
    TapeMember t;
    const uint64_t now = monotonic_tstates();
    if (!zip.read_text(member, text, why) || !tape_member_from_json(text, now, t, why)) {
        warn("the recorded tape position could not be read (" + why + "); the tape " +
             label + " was not reattached");
        return;
    }

    // The bytes: from the archive for a .zip tape, else the recorded path.
    std::string file = ti.path;
    if (!ti.archive.empty()) {
        program_archive::ProgramArchive archive;
        if (!archive.open(ti.archive, why) ||
            !archive.extract(ti.member, file, why)) {
            warn("the tape " + label + " could not be extracted (" + why +
                 "); it was not reattached");
            return;
        }
    }

    TapLoader tap;
    TzxLoader tzx;
    WavLoader wav;
    std::string digest;
    bool loaded = false;
    const char* kind = "";
    if (t.loader == "tap")      { loaded = tap.load(file); digest = tap.sha256(); kind = "TAP"; }
    else if (t.loader == "tzx") { loaded = tzx.load(file); digest = tzx.sha256(); kind = "TZX"; }
    else                        { loaded = wav.load(file); digest = wav.sha256(); kind = "WAV"; }
    if (!loaded) {
        warn(std::string("the tape ") + label + " could not be read as a " + kind +
             " file; it was not reattached");
        return;
    }
    if (digest.empty() || digest != ti.sha256) {
        warn("the tape " + label + " has changed since the snapshot was saved; "
             "it was not reattached");
        return;
    }

    bool ok;
    if (t.loader == "tap")      ok = tap.restore_deck(t.tap, why);
    else if (t.loader == "tzx") ok = tzx.restore_deck(t.tzx, now, why);
    else                        ok = ti.realtime ? wav.restore_deck(t.wav, now, why)
                                                 : (why = "a WAV tape is always real time", false);
    if (!ok) {
        warn("the recorded position of the tape " + label + " is not valid for that file (" +
             why + "); it was not reattached");
        return;
    }

    // Every check has passed: from here the deck is replaced. No jnext setting
    // is written (owner, GH #402): the session's fast/real-time mode
    // (`opt.tape_realtime`) and auto play (`tape_auto_play_`) apply, and a deck
    // saved in another mode is converted at a block boundary (plan section 3.1).
    // The loader holds the SAVED mode first, so the deck is exactly the saved one.
    std::string converted;
    const bool saved_rt = ti.realtime;
    const bool want_rt = opt.tape_realtime;
    const bool auto_differs = t.auto_play != tape_auto_play_;
    if (t.loader == "tap") {
        tap.set_fast_load(!saved_rt);
        if (saved_rt && !want_rt) {
            converted = tap.handover_to_fast_at_gap()
                ? "at a block boundary: fast loading now"
                : "finishing the current block in real time, then fast loading";
        } else if (!saved_rt && want_rt) {
            tap.start_realtime_from_fast(tape_auto_play_);
            converted = tape_auto_play_ ? "real time, held before the next block"
                                        : "real time, playing the next block";
        } else if (saved_rt && tap.is_held() && !tape_auto_play_) {
            tap.resume();
            converted = "auto play is off: the held deck plays on";
        }
        tape_ = std::move(tap);
        if (tzx_tape_.is_loaded()) tzx_tape_.eject();
        if (wav_tape_.is_loaded()) wav_tape_.eject();
    } else if (t.loader == "tzx") {
        tzx.set_fast_load(!saved_rt);
        if (saved_rt && !want_rt) {
            converted = tzx.handover_to_fast_at_gap()
                ? "at a block boundary: fast loading now"
                : "finishing the current block in real time, then fast loading";
        } else if (!saved_rt && want_rt) {
            tzx.start_playback_from_fast_position(now, tape_auto_play_);
            converted = tape_auto_play_ ? "real time, held before the next block"
                                        : "real time, playing the next block";
        } else if (saved_rt && tzx.is_held() && !tape_auto_play_) {
            tzx.resume(now);
            converted = "auto play is off: the held deck plays on";
        } else if (saved_rt && !t.auto_play && tape_auto_play_) {
            tzx.hold_if_stopped();   // a stop block holds under auto play
        }
        tzx_tape_ = std::move(tzx);
        if (tape_.is_loaded()) tape_.eject();
        if (wav_tape_.is_loaded()) wav_tape_.eject();
    } else {
        wav_tape_ = std::move(wav);
        if (tape_.is_loaded()) tape_.eject();
        if (tzx_tape_.is_loaded()) tzx_tape_.eject();
    }
    // Parking is restored machine state and a restore types nothing: no
    // resume_from_park(), no typist arm(), no LOAD "" (D8).
    // The poll run was counted under the recorded auto play setting; under a
    // different one it means nothing, which is what toggling auto play does.
    if (auto_differs) tape_poll_.reset(); else tape_poll_.restore(t.poll);
    phantom_typist_.restore(t.typist);
    tap_sync_valid_ = false;
    if (rzx_recorder_.is_recording()) rzx_suspend_tape_traps();

    Log::emulator()->info("load_jns: tape {} reattached ({}, {}, block/offset {})", label, kind,
                          (t.loader == "tap" ? tape_.fast_load() : t.loader == "tzx" ? tzx_tape_.fast_load() : false)
                              ? "fast" : "realtime",   // the mode it continues in
                          t.loader == "tap" ? static_cast<long>(t.tap.block)
                          : t.loader == "tzx" ? static_cast<long>(t.tzx.offset) : 0L);
    if (!converted.empty())
        Log::emulator()->info("load_jns: the session's tape settings differ from the snapshot's: {}", converted);
}

// ─────────────────────────────────────────────────────────────────────────
// Path wrappers — what the dispatch sites call
// ─────────────────────────────────────────────────────────────────────────

bool Emulator::save_jns_file(const std::string& path)
{
    jns_report_ = jnext::JnsLoadReport{};
    jns_error_.clear();

    jnext::JnsSaveOptions opt;
    opt.uncompressed = config_.jns_uncompressed;

    std::vector<uint8_t> out;
    if (!save_jns(opt, out, jns_report_, jns_error_)) {
        Log::emulator()->error("save_jns: {}: {}", path, jns_error_);
        return false;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        jns_error_ = "cannot open '" + path + "' for writing";
        Log::emulator()->error("save_jns: {}", jns_error_);
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    if (!f.good()) {
        jns_error_ = "short write to '" + path + "'";
        Log::emulator()->error("save_jns: {}", jns_error_);
        return false;
    }
    Log::emulator()->info("Snapshot saved: {} ({} bytes{})", path, out.size(),
                          jns_report_.advanced_to_frame_boundary
                              ? ", advanced to the next frame boundary" : "");
    for (const std::string& w : jns_report_.warnings) {
        Log::emulator()->warn("save_jns: {}", w);
    }
    return true;
}

bool Emulator::load_jns_file(const std::string& path, bool tape_realtime)
{
    jns_report_ = jnext::JnsLoadReport{};
    jns_error_.clear();

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        jns_error_ = "cannot open '" + path + "'";
        Log::emulator()->error("load_jns: {}", jns_error_);
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        jns_error_ = "'" + path + "' is empty";
        Log::emulator()->error("load_jns: {}", jns_error_);
        return false;
    }

    jnext::JnsLoadOptions opt;
    opt.strict       = config_.jns_strict;
    opt.force_sdcard = config_.jns_force_sdcard;
    opt.tape_realtime = tape_realtime;

    if (!load_jns(bytes.data(), bytes.size(), opt, jns_report_, jns_error_)) {
        Log::emulator()->error("load_jns: {}: {}", path, jns_error_);
        return false;
    }
    // Every warning is LOGGED here and SHOWN by the GUI (§15.2). Both, not
    // either: the log is what a bug report carries and the status bar is what
    // the user actually sees.
    for (const std::string& w : jns_report_.warnings) {
        Log::emulator()->warn("load_jns: {}", w);
    }
    for (const std::string& m : jns_report_.ignored_members) {
        Log::emulator()->info("load_jns: ignored unknown member '{}'", m);
    }
    for (const std::string& k : jns_report_.ignored_keys) {
        Log::emulator()->info("load_jns: ignored unknown key '{}'", k);
    }
    Log::emulator()->info("Snapshot loaded: {}", path);
    return true;
}
