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
    v("nmi",      nmi_source_);
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
/// `constructible_machines` set and `parse_machine_type` both use. NOT
/// `machine_type_str`, which returns display strings ("ZX Next", "+3") a
/// reader would have to special-case back.
const char* manifest_machine_name(MachineType t) {
    switch (t) {
        case MachineType::ZX48K:      return "48k";
        case MachineType::ZX128K:     return "128k";
        case MachineType::ZX_PLUS3:   return "plus3";
        case MachineType::ZXN_ISSUE2: return "next";
    }
    return "next";
}

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
                       b.key + "', which has no member name in §6.1's layout";
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
            tape_.is_loaded()     ? tape_.filename()     :
            tzx_tape_.is_loaded() ? tzx_tape_.filename() :
            wav_tape_.is_loaded() ? wav_tape_.filename() : std::string();
        if (!tape_path.empty()) {
            m.tape.present          = true;
            m.tape.path             = tape_path;
            m.tape.sha256           = sdcard::sha256_file(tape_path);
            m.tape.position_tstates = tape_sample_tstates();
            // `--tape-realtime` lives in the FRONTEND, not in EmulatorConfig,
            // so the emulator-side truth is the loader's own flag: real-time
            // playback is exactly "not fast-load".
            m.tape.realtime =
                tape_.is_loaded()     ? !tape_.fast_load()     :
                tzx_tape_.is_loaded() ? !tzx_tape_.fast_load() : true;
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
        if (!w.add_subsystem("esxdos", j.dump(2) + "\n", why)) return false;
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
                       b.key + "', which has no member name in §6.1's layout";
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

bool Emulator::load_jns(const uint8_t* data, std::size_t len,
                        const jnext::JnsLoadOptions& opt,
                        jnext::JnsLoadReport& report, std::string& why)
{
    why.clear();

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
    env.sd_transfer_in_flight = sd_card_.transfer_in_flight();

    env.constructible_ram_kb = { static_cast<uint32_t>(ram_.size() / 1024) };

    if (!config_.sd_card_image.empty()) {
        std::string card_why;
        // BRANCH ON THE RETURN VALUE, NEVER ON `why.empty()`. That function
        // sets `why` on SUCCESS too, when Tier 1 was read and only the Tier-2
        // digest failed — the card is then perfectly usable and the return
        // value says so. The distinction is written down here because this is
        // its first real call site.
        if (!jnext::describe_sdcard_for_snapshot(config_.sd_card_image,
                                                 config_.sd_card_readonly,
                                                 env.card, card_why)) {
            // `env.card.present` stays false, which is exactly right: the
            // container then refuses a snapshot that HAD a card, naming the
            // path — the decision belongs there, not here.
            Log::emulator()->warn("load_jns: the mounted SD card could not be "
                                  "identified: {}", card_why);
        } else if (!card_why.empty()) {
            // Tier 1 read, Tier 2 unavailable. The container distinguishes an
            // UNKNOWN stamp from a CHANGED one (S7), so this warns rather than
            // manufacturing a drift that did not happen.
            Log::emulator()->warn("load_jns: the mounted SD card's content "
                                  "stamp is unavailable: {}", card_why);
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

    jnext::zip::Reader   zip;
    jnext::jns::Manifest m;
    jnext::jns::Verdict  v;

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
            std::ifstream probe_f(probe.tape.path, std::ios::binary);
            env.tape_file_available = static_cast<bool>(probe_f);
        }
    }

    if (!jnext::jns::open_snapshot(data, len, env, zip, m, v)) {
        why = v.refusal;
        return false;
    }
    report.warnings.insert(report.warnings.end(), v.warnings.begin(),
                           v.warnings.end());
    report.ignored_members = v.ignored_members;
    report.reconfigured_to = v.reconfigure_to;

    // §7.3 — the file's machine wins, the same courtesy `.sna`/`.szx`/`.z80`
    // already get: the user must not have to get `--machine` right to reload
    // their own save.
    if (!v.reconfigure_to.empty()) {
        MachineType want = config_.type;
        if (parse_machine_type(v.reconfigure_to, want) && want != config_.type) {
            // `init(config_)` is what `load_snapshot_from_memory` already does
            // for a `.sna`/`.szx`/`.z80` whose machine differs — same
            // mechanism, so a `.jns` gets no special reconfiguration path of
            // its own to keep correct.
            config_.type = want;
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
        Log::emulator()->error("load_jns: {} — the machine is NOT trustworthy",
                               why);
        return false;
    }

    // ── §9.5(4): the esxDOS handle table ────────────────────────────────
    if (zip.has(state_member("esxdos"))) {
        std::string text, read_why;
        if (!zip.read_text(state_member("esxdos"), text, read_why)) {
            why = "state/esxdos.json: " + read_why;
            return false;
        }
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            why = "state/esxdos.json is not a JSON object";
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
    return true;
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

bool Emulator::load_jns_file(const std::string& path)
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
