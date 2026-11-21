// GH #492 - the bug report writer. See bug_report.h for the contract.

#include "platform/bug_report.h"

#include "core/emulator.h"
#include "core/host_platform.h"
#include "core/log.h"
#include "core/rzx.h"
#include "core/screenshot.h"
#include "core/sd_snapshot_identity.h"
#include "core/sha256.h"
#include "debug/debug_keymap.h"
#include "debug/debugger.h"
#include "debug/disasm.h"
#include "debug/disasm_text.h"
#include "debug/nextreg_names.h"
#include "platform/config_keys.h"
#include "save/zip_archive.h"
#include "third_party/nlohmann-json/nlohmann/json.hpp"
#include "version.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif

namespace bug_report {

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace dbg = jnext::dbg;

namespace {

// ---------------------------------------------------------------------------
// Names for the debugger's enums (the facade has no string form of them)
// ---------------------------------------------------------------------------

const char* space_kind_name(dbg::MemSpace::Kind k)
{
    using K = dbg::MemSpace::Kind;
    switch (k) {
        case K::Cpu:          return "cpu";
        case K::Page:         return "page";
        case K::Rom:          return "rom";
        case K::BootRom:      return "boot-rom";
        case K::MultifaceRom: return "multiface-rom";
        case K::MultifaceRam: return "multiface-ram";
        case K::DivMmcRom:    return "divmmc-rom";
        case K::DivMmcRam:    return "divmmc-ram";
        case K::AltRom:       return "alt-rom";
        case K::Layer2:       return "layer2";
        case K::Config:       return "config";
        case K::Floating:     return "floating";
    }
    return "unknown";
}

json space_json(dbg::MemSpace s)
{
    return json{{"kind", space_kind_name(s.kind)}, {"index", s.index}};
}

const char* event_kind_name(dbg::EventKind k)
{
    static const char* const names[] = {
        "execute", "mem", "port", "nextreg-write", "frame", "scanline", "cycle",
        "reset", "int-ack", "nmi", "magic", "host", "copper", "dma",
    };
    const size_t i = static_cast<size_t>(k);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "unknown";
}

const char* action_name(dbg::Action a)
{
    switch (a) {
        case dbg::Action::Stop:     return "stop";
        case dbg::Action::Log:      return "log";
        case dbg::Action::Continue: return "continue";
    }
    return "unknown";
}

const char* access_name(dbg::Access a)
{
    switch (a) {
        case dbg::Access::None:      return "none";
        case dbg::Access::Read:      return "read";
        case dbg::Access::Write:     return "write";
        case dbg::Access::ReadWrite: return "read-write";
    }
    return "unknown";
}

const char* pause_kind_name(dbg::PauseReason::Kind k)
{
    using K = dbg::PauseReason::Kind;
    switch (k) {
        case K::None:       return "none";
        case K::User:       return "user";
        case K::Breakpoint: return "breakpoint";
        case K::Watch:      return "watch";
        case K::Step:       return "step";
        case K::RunTo:      return "run-to";
        case K::Magic:      return "magic";
        case K::Corrupt:    return "corrupt";
        case K::Script:     return "script";
    }
    return "unknown";
}

const char* step_mode_name(dbg::StepMode m)
{
    switch (m) {
        case dbg::StepMode::None:           return "none";
        case dbg::StepMode::Into:           return "into";
        case dbg::StepMode::Over:           return "over";
        case dbg::StepMode::Out:            return "out";
        case dbg::StepMode::RunToCycle:     return "run-to-cycle";
        case dbg::StepMode::StepBack:       return "step-back";
        case dbg::StepMode::RunBackToCycle: return "run-back-to-cycle";
    }
    return "unknown";
}

const char* call_type_name(CallType t)
{
    // Names by ordinal: the header's enumerators are CALL / RST / INT / NMI.
    switch (static_cast<int>(t)) {
        case 0:  return "call";
        case 1:  return "rst";
        case 2:  return "int";
        case 3:  return "nmi";
        default: return "unknown";
    }
}

const char* ram_withheld_code(Emulator::RamWithheld w)
{
    switch (w) {
        case Emulator::RamWithheld::None:          return "";
        case Emulator::RamWithheld::CardSession:   return "card-session";
        case Emulator::RamWithheld::BootRomRan:    return "boot-rom-ran";
        case Emulator::RamWithheld::StateRestored: return "state-restored";
    }
    return "unknown";
}

const char* ram_withheld_reason(Emulator::RamWithheld w)
{
    switch (w) {
        case Emulator::RamWithheld::None:
            return "";
        case Emulator::RamWithheld::CardSession:
            return "this session has an SD card: its memory holds NextZXOS, ROM and firmware "
                   "from the card, which a report never carries";
        case Emulator::RamWithheld::BootRomRan:
            return "the boot ROM ran in this session: it may have copied itself into memory";
        case Emulator::RamWithheld::StateRestored:
            return "a saved state was loaded in this session: its memory is not known to be "
                   "the program's alone";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string base_name(const std::string& p)
{
    const size_t i = p.find_last_of("/\\");
    return i == std::string::npos ? p : p.substr(i + 1);
}

std::string utc_now_iso()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string arch_name()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__)
    return "arm";
#elif defined(__wasm__)
    return "wasm";
#else
    return "unknown";
#endif
}

void host_os(std::string& os, std::string& release)
{
    (void)release;   // not every platform below fills it
#if defined(__EMSCRIPTEN__)
    os = "emscripten";
#elif defined(__unix__) || defined(__APPLE__)
    struct utsname u {};
    if (uname(&u) == 0) {
        os = u.sysname;
        release = u.release;
    }
#elif defined(_WIN32)
    os = "Windows";
#endif
}

std::string to_lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// A zip member path component from a program's name: lower-case `[a-z0-9._-]`,
/// starting with `[a-z0-9]` (the container's grammar).
std::string member_component(const std::string& name)
{
    std::string out;
    for (char c : to_lower(name)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        out += ok ? c : '_';
    }
    if (out.empty() || !((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= '0' && out[0] <= '9')))
        out = "p" + out;
    if (out == "." || out == "..") out = "p";
    if (out.size() > 100) out.resize(100);
    return out;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

std::string hex_dump_json_text(const json& j) { return j.dump(2) + "\n"; }

/// A refused `[debugger_keys]` entry is logged as `kConfigIssueTag` NAME=TEXT: reason, where the
/// name and text are as hand-typed and the reason quotes the text again: nothing after the tag goes
/// into a report.
std::string scrub_config_issue(const std::string& line)
{
    const std::string tag = kConfigIssueTag;
    const size_t at = line.find(tag);
    if (at == std::string::npos) return line;
    return line.substr(0, at + tag.size()) + "<blanked>";
}

// Every free text the report takes from outside goes through this (argv, log,
// settings values, file and symbol names, error strings).
struct Redactor {
    RedactContext ctx;
    std::string operator()(const std::string& s) const { return redact(s, ctx); }
};

// ---------------------------------------------------------------------------
// The state-document filter's table
// ---------------------------------------------------------------------------

struct KeyRow { const char* key; KeyClass cls; };

// `subsystem.key`. State = a register file; Memory = can carry guest memory; Never = card
// sectors and serial traffic. Pinned by BR-15 against the walk, both ways.
const KeyRow kKeyTable[] = {
    { "mmu.slots",                     KeyClass::State },
    { "mmu.nr_mmu",                    KeyClass::State },
    { "nextreg.regs",                  KeyClass::State },
    { "emulator.im2_int_enable",       KeyClass::State },
    { "emulator.im2_int_status",       KeyClass::State },
    { "turbosound.ay0_reg",            KeyClass::State },
    { "turbosound.ay1_reg",            KeyClass::State },
    { "turbosound.ay2_reg",            KeyClass::State },
    { "dac.channels",                  KeyClass::State },
    { "keyboard.matrix",               KeyClass::State },
    { "keyboard.shift_hist",           KeyClass::State },
    { "rtc.regs",                      KeyClass::State },
    { "renderer.fallback_per_line",    KeyClass::State },
    { "renderer.border_per_line",      KeyClass::State },
    { "renderer.port_ff_log",          KeyClass::State },
    { "sprites.attributes",            KeyClass::State },
    { "palette.ula_rgb333",            KeyClass::State },
    { "palette.layer2_rgb333",         KeyClass::State },
    { "palette.sprite_rgb333",         KeyClass::State },
    { "palette.tilemap_rgb333",        KeyClass::State },
    { "palette.layer2_priority",       KeyClass::State },
    { "copper.instructions",           KeyClass::Memory },
    { "membrane_stick.keymap",         KeyClass::Memory },
    { "sdcard.cmd_buf",                KeyClass::Never },
    { "sdcard.resp_buf",               KeyClass::Never },
    { "sdcard.data_block",             KeyClass::Never },
    { "uart.ch0_tx_fifo",              KeyClass::Never },
    { "uart.ch0_rx_fifo",              KeyClass::Never },
    { "uart.ch1_tx_fifo",              KeyClass::Never },
    { "uart.ch1_rx_fifo",              KeyClass::Never },
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

/// How a jnext.conf key's value goes into the report. DECIDED PER KEY in `config_value_rule()`, a
/// switch with no default: `-Werror=switch` makes a key added to `config_keys::KEYS` a build error
/// here until someone decides it, so no key is ever kept by an accident of its type.
enum class ValueRule {
    Blank,      ///< can name the host (a path, a device, a command, a history): never shipped
    Plain,      ///< a number, boolean or word: kept only as config_keys' own validator accepts it, in canonical form
    Combo,      ///< a debugger key combination: kept only when it parses (rendered canonically)
    Geometry,   ///< "W,H" or "X,Y" as jnext writes a window's size and position: kept only then
};

ValueRule config_value_rule(config_keys::KeyId id)
{
    using K = config_keys::KeyId;
    switch (id) {
        case K::StartupMachineType:
        case K::StartupCpuSpeed:
        case K::StartupEmulatorSpeedPercent:
        case K::StartupWindowScale:
        case K::StartupCrtFilter:
        case K::StartupCrtShader:
        case K::StartupCrtShaderStyle:
        case K::StartupSilent:
        case K::StartupTapeFastLoad:
        case K::StartupTapeAutoPlay:
        case K::StartupWhenSlowPrefer:
        case K::StartupFastBootSeconds:
        case K::StartupFastBootHideScreen:
        case K::AudioGainDb:
        case K::AudioGainBeeperDb:
        case K::AudioGainAy0Db:
        case K::AudioGainAy1Db:
        case K::AudioGainAy2Db:
        case K::AudioGainDacDb:
        case K::ScreenshotQuickFormat:
        case K::SnapshotQuickKeep:
        case K::InputJoy1Source:
        case K::InputJoy2Source:
        case K::InputMouseRelease:
        case K::SourceColourScheme:
        case K::EspEnabled:
        case K::NextpiEnabled:
        case K::NextpiRelease:
        case K::ConfigVersion:
        case K::DebuggerAttached:
        case K::DebuggerPhysicalAddresses:
            return ValueRule::Plain;
        case K::PathsLastLoadDir:
        case K::PathsSdCardPath:
        case K::PathsSdCard2Path:
        case K::PathsScreenshotDir:
        case K::PathsSourceDirs:
        case K::RecentFiles:
        case K::ScreenshotQuickDir:
        case K::InputJoy1Device:
        case K::InputJoy2Device:
        case K::InputJoy1DeviceName:
        case K::InputJoy2DeviceName:
        case K::EspAllowedHosts:
        case K::NextpiDir:
        case K::NextpiQemuBinary:
        case K::NextpiAudio:        // empty, a mode, or `wav:FILE`
        case K::NextpiQemuArgs:
        case K::DebuggerLayout:     // the remembered panel layout: free text, nothing a report needs
            return ValueRule::Blank;
        case K::DebuggerKey:
            return ValueRule::Combo;
        case K::DebuggerSize:
        case K::DebuggerPosition:
            return ValueRule::Geometry;
        case K::Count:
            return ValueRule::Blank;
    }
    return ValueRule::Blank;
}

/// A Plain key's value: kept only in the canonical form config_keys' own validator writes (the one
/// `--set` and the settings file share), never as whatever short token the file holds. A key that is
/// not a setting (config.version) is kept only as a short number; nextpi.release only as empty,
/// `latest` or a release tag (digits and dots, an optional leading `v`).
bool plain_value_for_report(const config_keys::Key& k, const std::string& value, std::string& out)
{
    auto digits = [](const std::string& t, size_t max) {
        if (t.empty() || t.size() > max) return false;
        for (char c : t) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    };
    if (k.not_setting) {
        if (!digits(value, 4)) return false;
        out = value;
        return true;
    }
    config_keys::Assignment a;
    std::string why;
    const std::string arg = std::string(k.section) + "." + k.name + "=" + value;
    if (!config_keys::parse_assignment(arg.c_str(), a, why)) return false;
    if (k.id == config_keys::KeyId::NextpiRelease) {
        const std::string& t = a.text;
        bool tag = t.empty() || t == "latest";
        if (!tag) {
            size_t i = (t[0] == 'v') ? 1 : 0;
            bool dot = false, ok = i < t.size() && t.size() <= 24;
            for (; ok && i < t.size(); ++i) {
                if (t[i] == '.') { if (dot) ok = false; dot = true; }
                else if (std::isdigit(static_cast<unsigned char>(t[i]))) dot = false;
                else ok = false;
            }
            tag = ok && !dot;
        }
        if (!tag) return false;
    }
    out = a.text;
    return true;
}

bool is_geometry_value(const std::string& v)
{
    // `W,H` or `X,Y` (spaces around the comma allowed): two small signed integers.
    const size_t comma = v.find(',');
    if (comma == std::string::npos) return false;
    auto num = [](std::string t) {
        while (!t.empty() && t.front() == ' ') t.erase(t.begin());
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (t.empty() || t.size() > 6) return false;
        size_t i = (t[0] == '-') ? 1 : 0;
        if (i == t.size()) return false;
        for (; i < t.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
        return true;
    };
    return num(v.substr(0, comma)) && num(v.substr(comma + 1));
}

/// The value to write for `section.name` = `value`: itself when the key's rule keeps it and it
/// passes, else "<blanked>".
std::string config_value_for_report(const std::string& section, const std::string& name, const std::string& value)
{
    const config_keys::Key* k = config_keys::find(section, name);
    if (!k) return "<blanked>";   // a key the table does not know
    switch (config_value_rule(k->id)) {
        case ValueRule::Blank:
            return "<blanked>";
        case ValueRule::Plain: {
            std::string canon;
            return plain_value_for_report(*k, value, canon) ? canon : "<blanked>";
        }
        case ValueRule::Combo: {
            jnext::dbgkeys::Combo c;
            std::string why;
            return jnext::dbgkeys::parse_combo(value, c, why) ? jnext::dbgkeys::render_combo(c) : "<blanked>";
        }
        case ValueRule::Geometry:
            return is_geometry_value(value) ? value : "<blanked>";
    }
    return "<blanked>";
}

/// argv with every path redacted: an argument (or the value of `--opt=value`) that
/// contains a path separator keeps only its base name.
std::vector<std::string> redact_argv(const std::vector<std::string>& argv, const Redactor& R)
{
    std::vector<std::string> out;
    for (const std::string& a : argv) {
        std::string head, value = a;
        if (a.rfind("--", 0) == 0) {
            const size_t eq = a.find('=');
            if (eq != std::string::npos) { head = a.substr(0, eq + 1); value = a.substr(eq + 1); }
        }
        if (value.find_first_of("/\\") != std::string::npos)
            value = "<path>/" + base_name(value);
        out.push_back(R(head + value));
    }
    return out;
}

// ---------------------------------------------------------------------------
// The pieces
// ---------------------------------------------------------------------------

struct Member {
    std::string          name;
    std::vector<uint8_t> bytes;
};

void add_text(std::vector<Member>& m, const std::string& name, const std::string& text)
{
    m.push_back({name, std::vector<uint8_t>(text.begin(), text.end())});
}

json registers_json(const Z80Registers& r)
{
    return json{
        {"AF", r.AF}, {"BC", r.BC}, {"DE", r.DE}, {"HL", r.HL},
        {"AF2", r.AF2}, {"BC2", r.BC2}, {"DE2", r.DE2}, {"HL2", r.HL2},
        {"IX", r.IX}, {"IY", r.IY}, {"SP", r.SP}, {"PC", r.PC},
        {"MEMPTR", r.MEMPTR}, {"I", r.I}, {"R", r.R},
        {"IFF1", r.IFF1}, {"IFF2", r.IFF2}, {"IM", r.IM}, {"halted", r.halted},
    };
}

json clip_json(const dbg::ClipWindow& c)
{
    return json{{"x1", c.x1}, {"x2", c.x2}, {"y1", c.y1}, {"y2", c.y2}};
}

json machine_json(Emulator& emu, const dbg::Debugger& d, bool ram_ships)
{
    json m = json::object();

    const Z80Registers regs = d.registers();
    m["registers"] = registers_json(regs);

    json nr = json::array();
    for (int r = 0; r < 256; ++r) {
        const uint8_t reg = static_cast<uint8_t>(r);
        nr.push_back(json{{"reg", r}, {"name", dbg::nextreg_name(reg)}, {"value", d.nextreg_peek(reg)}});
    }
    m["nextregs"] = nr;
    m["nextreg_selected"] = d.nextreg_selected();

    json slots = json::array();
    const auto sl = d.mmu_slots();
    for (size_t i = 0; i < sl.size(); ++i) {
        slots.push_back(json{
            {"slot", i}, {"nr_page", sl[i].nr_page}, {"effective_page", sl[i].effective_page},
            {"is_rom", sl[i].is_rom}, {"space", space_json(sl[i].space)},
            {"space_offset", sl[i].space_offset}, {"now", space_json(sl[i].now)},
        });
    }
    const dbg::PagingPorts pp = d.paging_ports();
    m["paging"] = json{
        {"slots", slots},
        {"ports", json{{"port_7ffd", pp.port_7ffd}, {"port_1ffd", pp.port_1ffd}, {"port_dffd", pp.port_dffd}}},
        {"rom_select", space_json(d.rom_select())},
    };

    json video = json::object();
    const RasterState rs = d.raster();
    video["raster"] = json{
        {"raw_hc", rs.raw_hc}, {"raw_vc", rs.raw_vc}, {"hc_ula", rs.hc_ula}, {"vc_ula", rs.vc_ula},
        {"cvc", rs.cvc}, {"phc", rs.phc},
        {"region", raster_region_name(rs.region)}, {"fetch", ula_fetch_name(rs.fetch)},
    };
    const dbg::Time t = d.time();
    video["time"] = json{
        {"master_cycle", t.master_cycle}, {"tstates_total", t.tstates_total}, {"frame", t.frame},
        {"cycle_in_frame", t.cycle_in_frame}, {"vc_raw", t.vc_raw}, {"hc_raw", t.hc_raw},
    };
    const dbg::MachineInfo mi = d.machine();
    video["machine"] = json{
        {"type", machine_type_key(mi.type)}, {"cpu_divisor", mi.cpu_divisor},
        {"tstates_per_line", mi.tstates_per_line}, {"tstates_per_frame", mi.tstates_per_frame},
        {"master_cycles_per_line", mi.master_cycles_per_line},
        {"master_cycles_per_frame", mi.master_cycles_per_frame},
        {"lines", mi.lines}, {"fps", mi.fps}, {"hc_max", mi.hc_max}, {"vc_max", mi.vc_max},
    };
    const dbg::UlaScreenRegs u = d.ula_screen_regs();
    video["ula"] = json{
        {"active_bank", u.active_bank}, {"port_ff", u.port_ff}, {"scroll_x", u.scroll_x},
        {"scroll_y", u.scroll_y}, {"border", u.border}, {"second_palette", u.second_palette},
        {"enabled", u.enabled},
    };
    json clips = json::object();
    clips["layer2"]  = clip_json(d.clip_window(dbg::ClipLayer::Layer2));
    clips["sprites"] = clip_json(d.clip_window(dbg::ClipLayer::Sprites));
    clips["ula"]     = clip_json(d.clip_window(dbg::ClipLayer::Ula));
    clips["tilemap"] = clip_json(d.clip_window(dbg::ClipLayer::Tilemap));
    video["clip"] = clips;
    video["sprite_clip"] = clip_json(d.sprite_clip());

    json sprites = json::array();
    const auto sp = d.sprites();
    for (size_t i = 0; i < sp.size(); ++i) {
        sprites.push_back(json{
            {"index", i}, {"x", sp[i].x}, {"y", sp[i].y}, {"pattern", sp[i].pattern},
            {"palette_offset", sp[i].palette_offset}, {"visible", sp[i].visible},
            {"x_mirror", sp[i].x_mirror}, {"y_mirror", sp[i].y_mirror}, {"rotate", sp[i].rotate},
            {"is_4bit", sp[i].is_4bit}, {"x_scale", sp[i].x_scale}, {"y_scale", sp[i].y_scale},
        });
    }
    video["sprites"] = sprites;

    const dbg::CopperState cs = d.copper();
    json copper = json{{"pc", cs.pc}, {"running", cs.running}, {"mode", cs.mode}};
    if (ram_ships) {   // the Copper's instruction RAM is memory
        json prog = json::array();
        for (size_t i = 0; i < cs.program.size; ++i) prog.push_back(cs.program.data[i]);
        copper["program"] = prog;
    }
    video["copper"] = copper;

    static const char* const bank_names[8] = {
        "ula-first", "layer2-first", "sprite-first", "tilemap-first",
        "ula-second", "layer2-second", "sprite-second", "tilemap-second",
    };
    json palettes = json::object();
    for (int b = 0; b < 8; ++b) {
        const auto id = static_cast<dbg::PaletteId>(b);
        json p = json::object();
        json e = json::array();
        for (uint16_t v : d.palette(id)) e.push_back(v);
        p["rgb333"] = e;
        json pr = json::array();
        for (uint8_t v : d.palette_priority(id)) pr.push_back(v);
        p["priority"] = pr;
        palettes[bank_names[b]] = p;
    }
    video["palettes"] = palettes;
    video["active_ula_palette_bank"] = d.active_ula_palette_bank();
    m["video"] = video;

    json ints = json{{"iff1", regs.IFF1}, {"iff2", regs.IFF2}, {"im", regs.IM}, {"halted", regs.halted}};
    json ir = json::array();
    for (int r = 0xC0; r <= 0xCF; ++r)
        ir.push_back(json{{"reg", r}, {"name", dbg::nextreg_name(static_cast<uint8_t>(r))},
                          {"value", d.nextreg_peek(static_cast<uint8_t>(r))}});
    ints["nextregs"] = ir;
    m["interrupts"] = ints;

    const DivMmc& dm = emu.divmmc();
    m["divmmc"] = json{
        {"enabled", dm.is_enabled()}, {"port_io_enable", dm.port_io_enable()}, {"conmem", dm.conmem()},
        {"mapram", dm.mapram()}, {"automap_active", dm.automap_active()},
        {"automap_held", dm.automap_held()}, {"button_nmi", dm.button_nmi()},
    };
    const Multiface& mf = emu.multiface();
    m["multiface"] = json{
        {"enabled", mf.is_enabled()}, {"mf_enable", mf.mf_enable()}, {"mem_active", mf.is_mem_active()},
        {"nmi_active", mf.nmi_active()}, {"invisible", mf.invisible()}, {"mf_port_en", mf.mf_port_en()},
    };

    json audio = json{
        {"turbosound_enabled", d.turbosound_enabled()},
        {"ay_mode", d.ay_mode() == dbg::AyChipMode::Ay ? "ay" : "ym"},
        {"stereo_mode", d.stereo_mode() == dbg::StereoMode::Acb ? "acb" : "abc"},
    };
    json ay = json::array();
    for (int c = 0; c < static_cast<int>(dbg::AY_CHIP_COUNT); ++c) {
        json r = json::array();
        const auto e = d.ay_registers(c);
        if (e) for (uint8_t v : e.value) r.push_back(v);
        ay.push_back(r);
    }
    audio["ay_registers"] = ay;
    m["audio"] = audio;
    return m;
}

json debugger_json(const dbg::Debugger& d, bool ram_ships, const Redactor& R)
{
    json j = json::object();
    const dbg::RunState rs = d.state();
    j["run_state"] = json{
        {"paused", rs.paused}, {"step_mode", step_mode_name(rs.step_mode)},
        {"pause_reason", pause_kind_name(rs.pause_reason.kind)},
        {"cycle", rs.cycle}, {"frame", rs.frame}, {"pc", rs.pc},
    };
    if (auto sym = d.lookup(rs.pc)) j["pc_symbol"] = R(*sym);
    if (const SourceLocation* sl = d.source_location()) {
        j["pc_source"] = json{{"file", R(base_name(sl->file))}, {"line", sl->line}, {"column", sl->column}};
    }
    j["pc_space"] = space_json(d.source_page(rs.pc));

    json bps = json::array();
    for (const dbg::SubscriptionInfo& s : d.subscriptions(false)) {
        bps.push_back(json{
            {"id", s.id}, {"kind", event_kind_name(s.kind)}, {"lo", s.filter.lo}, {"hi", s.filter.hi},
            {"page", s.filter.page}, {"space", space_json(s.filter.space)},
            {"access", access_name(s.access)}, {"enabled", s.enabled}, {"live", s.live},
            {"once", s.once}, {"action", action_name(s.action)}, {"has_condition", s.has_condition},
        });
    }
    j["breakpoints"] = bps;

    json cstack = json::array();
    for (const CallFrame& f : d.call_stack()) {
        json fr = json{
            {"caller_pc", f.caller_pc}, {"target_pc", f.target_pc}, {"sp_at_call", f.sp_at_call},
            {"type", call_type_name(f.type)},
            {"caller_space", space_json(f.caller_space)}, {"target_space", space_json(f.target_space)},
        };
        if (auto n = d.lookup(f.target_pc)) fr["target_symbol"] = R(*n);
        cstack.push_back(fr);
    }
    j["call_stack"] = json{{"enabled", d.call_stack_enabled()}, {"frames", cstack}};

    json trace = json{{"enabled", d.trace_enabled()}};
    json entries = json::array();
    if (d.trace_enabled()) {
        const auto te = d.trace_entries();
        if (te) {
            const std::vector<TraceEntry>& v = te.value;
            const size_t first = v.size() > 256 ? v.size() - 256 : 0;
            for (size_t i = first; i < v.size(); ++i) {
                const TraceEntry& e = v[i];
                json en = json{
                    {"cycle", e.cycle}, {"pc", e.pc}, {"af", e.af}, {"bc", e.bc}, {"de", e.de},
                    {"hl", e.hl}, {"af2", e.af2}, {"bc2", e.bc2}, {"de2", e.de2}, {"hl2", e.hl2},
                    {"ix", e.ix}, {"iy", e.iy}, {"sp", e.sp},
                    {"fetch_space", space_json(e.fetch_space())},
                };
                if (ram_ships) {   // the opcode bytes are memory
                    json ob = json::array();
                    const int n = std::min<int>(e.opcode_len, 4);
                    for (int b = 0; b < n; ++b) ob.push_back(e.opcode_bytes[b]);
                    en["opcode_bytes"] = ob;
                    en["opcode_len"] = e.opcode_len;
                    const uint16_t pc = e.pc;
                    const uint8_t* bytes = e.opcode_bytes;
                    const DisasmLine dl = disasm_one(pc, [pc, bytes](uint16_t a) -> uint8_t {
                        const uint16_t off = static_cast<uint16_t>(a - pc);
                        return off < 4 ? bytes[off] : 0;
                    });
                    en["disasm"] = std::string(dl.mnemonic);
                }
                entries.push_back(en);
            }
        }
    }
    trace["entries"] = entries;
    j["trace"] = trace;

    if (ram_ships) {
        // 32 instructions forward from PC, in the Disassembly panel's own copy format.
        const uint16_t pc = rs.pc;
        const uint16_t end = static_cast<uint16_t>(std::min<uint32_t>(0xFFFFu, uint32_t(pc) + 0xFFu));
        std::vector<disasm_text::CopyLine> lines =
            disasm_text::collect_range(pc, end, d.memory_reader(), disasm_text::SymbolNamer{});
        if (lines.size() > 32) lines.resize(32);
        j["disasm_at_pc"] = disasm_text::format_lines(lines, disasm_text::CopyFormat::WithAddresses);

        // 32 bytes at SP, wrapping at the top of the address space as the Z80's stack does.
        const uint16_t sp = d.registers().SP;
        std::array<uint8_t, 32> stack{};
        const size_t first = std::min<size_t>(32, 0x10000u - sp);
        const auto p1 = d.peek(dbg::MemSpace::cpu(), sp, first, stack.data());
        bool ok = p1 && p1.value == first;
        if (ok && first < 32) {
            const auto p2 = d.peek(dbg::MemSpace::cpu(), 0, 32 - first, stack.data() + first);
            ok = p2 && p2.value == 32 - first;
        }
        json sj = json::array();
        if (ok) for (uint8_t b : stack) sj.push_back(b);
        j["stack"] = json{{"sp", sp}, {"bytes", sj}};
    }
    return j;
}

// ---------------------------------------------------------------------------
// Program files
// ---------------------------------------------------------------------------

constexpr uint64_t kMaxProgramBytes = 32ull * 1024 * 1024;   // a zip member is capped at 64 MiB

struct ProgramResult {
    json                     manifest;
    std::vector<Member>      members;
};

/// Each program file loaded since RAM was last cleared: shipped as the user's own file,
/// re-read now, unless it changed on disk since it was loaded (then its SHA-256 only: the
/// bytes that ran are not the bytes there now) or is too large for a zip member.
ProgramResult programs_json(const Emulator& emu, const Redactor& R)
{
    ProgramResult out;
    out.manifest = json::array();
    std::set<std::string> used;
    for (const Emulator::ProgramFile& pf : emu.program_files()) {
        json e = json{{"name", R(pf.name)}, {"bytes", pf.size}};
        std::error_code ec;
        const auto size  = fs::file_size(pf.path, ec);
        const bool size_ok = !ec;
        ec.clear();
        const auto mtime = fs::last_write_time(pf.path, ec);
        const bool time_ok = !ec;
        // Hashed by streaming, so a file too large for a member is never held in memory.
        const std::string sha = (size_ok && time_ok) ? sdcard::sha256_file(pf.path) : std::string();
        std::vector<uint8_t> bytes;
        std::string code, reason;
        if (sha.empty()) {
            code = "unreadable";
            reason = "the file cannot be read now";
        } else {
            e["sha256"] = sha;
            if (holds_machine_snapshot(pf.path)) {
                // A jnext snapshot holds the machine's whole memory, which after a NextZXOS boot
                // is NextZXOS and DivMMC firmware: jnext never packs that into a report.
                code = "snapshot";
                reason = "a jnext snapshot holds the machine's whole memory, which a report does not carry";
            } else if (static_cast<uint64_t>(size) != pf.size ||
                static_cast<int64_t>(mtime.time_since_epoch().count()) != pf.mtime) {
                code = "changed-since-load";
                reason = "the file on disk is not the one that was loaded (its size or time changed)";
                e["bytes_now"] = static_cast<uint64_t>(size);
            } else if (static_cast<uint64_t>(size) > kMaxProgramBytes) {
                code = "too-large";
                reason = "the file is larger than a report member may be";
            } else if (!read_file(pf.path, bytes)) {
                code = "unreadable";
                reason = "the file cannot be read now";
            }
        }
        if (code.empty()) {
            std::string comp = member_component(pf.name);
            std::string member = "program/" + comp;
            for (int n = 2; used.count(member); ++n) member = "program/" + comp + "-" + std::to_string(n);
            used.insert(member);
            e["included"] = true;
            e["member"] = member;
            out.members.push_back({member, std::move(bytes)});
        } else {
            e["included"] = false;
            e["withheld"] = json{{"code", code}, {"reason", reason}};
        }
        out.manifest.push_back(e);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------

void card_json(json& cards, int slot, const std::string& path, bool read_only,
               const Redactor& R, Outcome& out)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    json c = json{{"slot", slot}, {"name", R(base_name(path))}, {"readonly", read_only}};
    if (ec) {
        c["bytes"] = 0;
        out.warnings.push_back("card " + std::to_string(slot) + " could not be read: " + ec.message());
        cards.push_back(c);
        return;
    }
    c["bytes"] = static_cast<uint64_t>(size);
    const std::string sha = sdcard::sha256_file(path);
    if (sha.empty())
        out.warnings.push_back("card " + std::to_string(slot) + " could not be hashed");
    c["sha256"] = sha;
    jnext::jns::SdCardInfo info;
    std::string why;
    if (jnext::describe_sdcard_for_snapshot(path, read_only, info, why)) {
        c["identity"] = json{
            {"image_bytes", info.identity.image_bytes},
            {"mbr_partition_table_sha256", info.identity.mbr_partition_table_sha256},
            {"fat32_volume_id", info.identity.fat32_volume_id},
            {"partition_lba", info.identity.partition_lba},
        };
    } else {
        out.warnings.push_back("card " + std::to_string(slot) + " identity unavailable: " + R(why));
    }
    cards.push_back(c);
}

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------

void add_pictures(std::vector<Member>& members, const dbg::Debugger& d, Outcome& out)
{
    const dbg::ConstU32s fb = d.framebuffer();
    const int w = static_cast<int>(dbg::RENDER_WIDTH);
    const int rows = fb.size ? static_cast<int>(fb.size / dbg::RENDER_WIDTH) : 0;
    if (rows == 0) {
        out.warnings.push_back("no picture: the machine has not drawn a frame yet");
        return;
    }
    std::vector<uint8_t> png;
    if (encode_png(fb.data, w, rows, png)) members.push_back({"screen/composite.png", std::move(png)});
    else out.warnings.push_back("the composite picture could not be encoded");

    struct View { dbg::Layer layer; const char* name; };
    static const View views[] = {
        { dbg::Layer::UlaPrimary,   "ula-primary" },
        { dbg::Layer::UlaShadow,    "ula-shadow" },
        { dbg::Layer::Layer2Active, "layer2-active" },
        { dbg::Layer::Layer2Shadow, "layer2-shadow" },
        { dbg::Layer::Sprites,      "sprites" },
        { dbg::Layer::Tilemap,      "tilemap" },
        { dbg::Layer::Background,   "background" },
    };
    std::vector<uint32_t> buf(static_cast<size_t>(w) * rows);
    for (const View& v : views) {
        std::fill(buf.begin(), buf.end(), 0u);
        const dbg::Result r = d.render_layer(v.layer, rows - 1, buf.data(), static_cast<size_t>(w));
        if (r != dbg::Result::Ok) {
            out.warnings.push_back(std::string("the ") + v.name + " view could not be drawn");
            continue;
        }
        std::vector<uint8_t> p;
        if (encode_png(buf.data(), w, rows, p)) members.push_back({std::string("screen/") + v.name + ".png", std::move(p)});
        else out.warnings.push_back(std::string("the ") + v.name + " view could not be encoded");
    }
}

// ---------------------------------------------------------------------------
// Memory (only when the RAM rule allows it)
// ---------------------------------------------------------------------------

void add_memory(std::vector<Member>& members, const dbg::Debugger& d, Outcome& out)
{
    // Every logical page, until the machine has no more. ROM-class SRAM (ROM, DivMMC,
    // Multiface, alternate ROM) is not reachable through the page space: a Page is SRAM
    // page + 0x20.
    std::vector<uint8_t> ram;
    std::vector<uint8_t> page(0x2000);
    for (uint16_t p = 0; p < 0xE0; ++p) {
        const auto r = d.peek(dbg::MemSpace::page(p), 0, page.size(), page.data());
        if (!r || r.value != page.size()) break;
        ram.insert(ram.end(), page.begin(), page.end());
    }
    members.push_back({"memory/ram.bin", std::move(ram)});

    std::vector<uint8_t> cpu(0x10000);
    const auto cr = d.peek(dbg::MemSpace::cpu(), 0, cpu.size(), cpu.data());
    if (cr && cr.value == cpu.size()) members.push_back({"memory/cpu.bin", std::move(cpu)});
    else out.warnings.push_back("the CPU view could not be read");

    const dbg::ConstBytes pat = d.pattern_ram();
    if (pat.data && pat.size)
        members.push_back({"memory/patterns.bin", std::vector<uint8_t>(pat.data, pat.data + pat.size)});

    // A linear disassembly of the whole CPU view, in the Disassembly panel's copy format.
    std::vector<disasm_text::CopyLine> lines =
        disasm_text::collect_range(0, 0xFFFF, d.memory_reader(), disasm_text::SymbolNamer{});
    add_text(members, "memory/cpu-disasm.txt",
             disasm_text::format_lines(lines, disasm_text::CopyFormat::WithAddresses));
}

}  // namespace

// ---------------------------------------------------------------------------
// build()
// ---------------------------------------------------------------------------

std::vector<uint8_t> build(Emulator& emu, const dbg::Debugger& d, const Inputs& in, Outcome& out)
{
    out = Outcome{};
    const RedactContext ctx = redact_context_from_environment();
    const Redactor R{ctx};
    const EmulatorConfig& cfg = emu.config();

    const Emulator::RamWithheld withheld = emu.ram_withheld();
    const bool ram_ships = (withheld == Emulator::RamWithheld::None);
    out.memory_included = ram_ships;
    if (!ram_ships) out.memory_reason = ram_withheld_reason(withheld);

    std::vector<Member> members;   // everything but manifest.json, in a stable order

    // settings.json
    {
        json s = json::object();
        s["command_line"] = redact_argv(in.argv, R);
        if (in.config_file_read) {
            json cf = json::object();
            int unknown = 0;
            for (const auto& kv : in.config_file) {
                const size_t dot = kv.first.find('.');
                const std::string section = dot == std::string::npos ? "" : kv.first.substr(0, dot);
                const std::string name    = dot == std::string::npos ? kv.first : kv.first.substr(dot + 1);
                if (!config_keys::find(section, name)) {
                    // An unknown key's NAME can carry a hand-typed secret too: it is not echoed.
                    cf[section + ".<unknown-" + std::to_string(++unknown) + ">"] = "<blanked>";
                    continue;
                }
                cf[R(kv.first)] = R(config_value_for_report(section, name, kv.second));
            }
            s["config_file"] = cf;
        } else {
            s["config_file"] = "not read by this frontend";
        }
        s["config_file_read"] = in.config_file_read;
        s["session"] = json{
            {"no_sdcard", cfg.no_sdcard}, {"sdcard_readonly", cfg.sd_card_readonly},
            {"esxdos_stub", cfg.esxdos_stub}, {"esxdos_stub_writable", cfg.esxdos_stub_writable},
            {"esp_enabled", cfg.esp_enabled}, {"rewind_frames", cfg.rewind_buffer_frames},
            {"trace", cfg.trace},
        };
        add_text(members, "settings.json", hex_dump_json_text(s));
    }

    add_text(members, "machine.json", hex_dump_json_text(machine_json(emu, d, ram_ships)));

    // state/<subsystem>.json, filtered by key class
    {
        std::vector<Emulator::StateDocument> docs;
        std::string why;
        if (!emu.state_documents(docs, why)) {
            out.ok = false;
            out.why = "the machine state could not be described: " + why;
            return {};
        }
        for (const Emulator::StateDocument& doc : docs) {
            std::vector<std::string> drop;
            for (const std::string& key : doc.content_keys) {
                const KeyClass c = key_class(doc.subsystem, key);
                if (c == KeyClass::Never || (c == KeyClass::Memory && !ram_ships)) drop.push_back(key);
            }
            std::string text = doc.json;
            if (!drop.empty()) {
                json j = json::parse(doc.json, nullptr, false);
                if (j.is_discarded()) {
                    out.ok = false;
                    out.why = "the state of '" + doc.subsystem + "' could not be filtered";
                    return {};
                }
                for (const std::string& key : drop) j.erase(key);
                text = j.dump(2) + "\n";
            }
            add_text(members, "state/" + doc.subsystem + ".json", text);
        }
    }

    add_pictures(members, d, out);

    {
        std::string log;
        for (const std::string& line : Log::tail()) log += R(scrub_config_issue(line)) + "\n";
        add_text(members, "log.txt", log);
    }

    add_text(members, "debugger.json", hex_dump_json_text(debugger_json(d, ram_ships, R)));

    ProgramResult programs = programs_json(emu, R);
    for (Member& m : programs.members) members.push_back(std::move(m));

    if (ram_ships) add_memory(members, d, out);

    // manifest.json
    json man = json::object();
    man["kind"] = "jnext-bug-report";
    man["format_version"] = 1;
    man["created_utc"] = utc_now_iso();
    std::string os, os_release;
    host_os(os, os_release);
    man["producer"] = json{
        {"jnext_version", JNEXT_VERSION_STRING}, {"platform", host_platform_name()},
        {"os", os}, {"os_release", os_release}, {"arch", arch_name()},
        {"frontend", in.frontend}, {"toolkit", in.toolkit},
    };
    man["machine"] = machine_type_key(cfg.type);
    json mem = json{{"included", ram_ships}};
    if (!ram_ships)
        mem["withheld"] = json{{"code", ram_withheld_code(withheld)}, {"reason", ram_withheld_reason(withheld)}};
    man["memory"] = mem;
    json cards = json::array();
    if (emu.sd_card().mounted())
        card_json(cards, 0, emu.sd_card().image_path(), cfg.sd_card_readonly, R, out);
    if (emu.sd_card2().mounted())
        card_json(cards, 1, emu.sd_card2().image_path(), cfg.sd_card2_readonly, R, out);
    man["cards"] = cards;
    man["programs"] = programs.manifest;
    json mlist = json::array();
    for (const Member& m : members) mlist.push_back(json{{"name", m.name}, {"bytes", m.bytes.size()}});
    man["members"] = mlist;
    man["warnings"] = out.warnings;

    // The belt to the redaction's braces, for the three members that carry free text
    // from the host (the command line and settings, the log, the debugger's names): a
    // user name or the home directory still in one is replaced. Not the manifest or the
    // machine documents: a user named "state" must not rewrite a member name there.
    auto scrub = [&](const std::string& name, std::string text) {
        if (name != "settings.json" && name != "log.txt" && name != "debugger.json") return text;
        for (const std::string& u : ctx.users) {
            for (size_t at = 0; (at = text.find(u, at)) != std::string::npos;) {
                text.replace(at, u.size(), "<user>");
                at += 6;
            }
        }
        if (ctx.home.size() >= 3) {
            for (size_t at = 0; (at = text.find(ctx.home, at)) != std::string::npos;) {
                text.replace(at, ctx.home.size(), "<path>");
                at += 6;
            }
        }
        return text;
    };

    jnext::zip::Writer zw;
    std::string why;
    if (!zw.add("manifest.json", hex_dump_json_text(man), jnext::zip::Method::Deflate, why)) {
        out.ok = false;
        out.why = "manifest.json: " + why;
        return {};
    }
    for (const Member& m : members) {
        const bool text = m.name == "settings.json" || m.name == "log.txt" || m.name == "debugger.json";
        bool ok;
        if (text) {
            ok = zw.add(m.name, scrub(m.name, std::string(m.bytes.begin(), m.bytes.end())),
                        jnext::zip::Method::Deflate, why);
        } else {
            ok = zw.add(m.name, m.bytes.data(), m.bytes.size(), jnext::zip::Method::Deflate, why);
        }
        if (!ok) {
            out.ok = false;
            out.why = m.name + ": " + why;
            return {};
        }
    }
    std::vector<uint8_t> zip;
    if (!zw.finish(zip, why)) {
        out.ok = false;
        out.why = "the zip could not be finished: " + why;
        return {};
    }
    out.ok = true;
    return zip;
}

Outcome write(Emulator& emu, const dbg::Debugger& d, const Inputs& in, const std::string& path)
{
    Outcome out;
    const std::vector<uint8_t> zip = build(emu, d, in, out);
    if (!out.ok) return out;
    out.path = path;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        out.ok = false;
        out.why = "cannot open '" + path + "' for writing: " + std::strerror(errno);
        return out;
    }
    f.write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));
    f.close();
    if (!f) {
        out.ok = false;
        out.why = "cannot write '" + path + "': " + std::strerror(errno);
        return out;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The file name
// ---------------------------------------------------------------------------

std::string file_name(const std::string& program, const std::tm& local, int n)
{
    std::string stem = program;
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0) stem.resize(dot);
    std::string name;
    for (char c : stem) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        name += ok ? c : '_';
    }
    if (!name.empty() && name[0] == '.') name[0] = '_';
    if (name.size() > 64) name.resize(64);
    if (name.empty()) name = "jnext";
    char when[16];
    std::strftime(when, sizeof(when), "%Y%m%d%H%M", &local);
    std::string out = name + "." + when;
    if (n > 1) out += "-" + std::to_string(n);
    return out + ".jnext-bug.zip";
}

bool resolve_path(const std::string& arg, const std::string& program, const std::tm& now,
                  std::string& out_path, std::string& why)
{
    if (arg.empty()) { why = "empty path"; return false; }
    std::error_code ec;
    bool is_dir = fs::is_directory(arg, ec);
    const char last = arg.back();
    bool sep = last == '/';
#ifdef _WIN32
    sep = sep || last == '\\';
#endif
    if (!is_dir && !sep) { out_path = arg; return true; }
    if (!is_dir) {
        fs::create_directories(arg, ec);
        if (ec) { why = "cannot create directory '" + arg + "': " + ec.message(); return false; }
    }
    fs::path dir(arg);
    for (int n = 1; n < 1000; ++n) {
        const fs::path p = dir / file_name(program, now, n);
        ec.clear();
        if (!fs::exists(p, ec)) { out_path = p.string(); return true; }
    }
    why = "no free file name in '" + arg + "'";
    return false;
}

bool holds_machine_snapshot(const std::string& path)
{
    const std::string ext = to_lower(fs::path(path).extension().string());
    if (ext == ".jns") return true;
    if (ext != ".rzx") return false;
    RzxRecording rec;
    if (!rzx::parse(path, rec)) return true;          // cannot be inspected: fail safe
    if (rec.snapshot_data.empty()) return false;       // input only
    if (rec.snapshot_ext != "sna" && rec.snapshot_ext != "szx" && rec.snapshot_ext != "z80") return true;
    const auto& d = rec.snapshot_data;                 // a zip is a jnext snapshot whatever it is called
    return d.size() >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 3 && d[3] == 4;
}

std::string program_name(const Emulator& emu)
{
    const auto& v = emu.program_files();
    return v.empty() ? std::string() : v.back().name;
}

// ---------------------------------------------------------------------------
// Redaction
// ---------------------------------------------------------------------------

RedactContext redact_context_from_environment()
{
    RedactContext c;
    auto env = [](const char* n) { const char* v = std::getenv(n); return v ? std::string(v) : std::string(); };
    c.home = env("HOME");
    if (c.home.empty()) c.home = env("USERPROFILE");
    std::set<std::string> users;
    for (const char* n : { "USER", "USERNAME", "LOGNAME" }) {
        const std::string u = env(n);
        if (u.size() >= 3) users.insert(u);
    }
    if (!c.home.empty()) {
        std::string h = c.home;
        while (h.size() > 1 && (h.back() == '/' || h.back() == '\\')) h.pop_back();
        const std::string last = base_name(h);
        if (last.size() >= 3) users.insert(last);
    }
    c.users.assign(users.begin(), users.end());
    // Longest first, so a name that contains another is replaced whole.
    std::sort(c.users.begin(), c.users.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    return c;
}

namespace {

bool is_path_end(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\'' || c == '`' ||
           c == ',' || c == ';' || c == ')' || c == ']' || c == '}';
}

/// May an absolute path start right after `prev`? (start of text, a space, `=`, a quote or
/// an opening bracket: not the middle of a word or of a URL).
bool can_start_path_after(char prev)
{
    return std::isspace(static_cast<unsigned char>(prev)) || prev == '=' || prev == '"' ||
           prev == '\'' || prev == '`' || prev == '(' || prev == '[' || prev == '{' ||
           prev == ',' || prev == ';';
}

}  // namespace

std::string redact(const std::string& text, const RedactContext& ctx)
{
    // 1. user names, whole, longest first: a name with a space is one name
    std::string s = text;
    for (const std::string& u : ctx.users) {
        if (u.size() < 3) continue;
        for (size_t at = 0; (at = s.find(u, at)) != std::string::npos;) {
            s.replace(at, u.size(), "<user>");
            at += 6;
        }
    }
    // 2. absolute paths
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        const char prev = i == 0 ? ' ' : s[i - 1];
        bool start = false;
        if (c == '/' && prev == ':' && i + 1 < s.size() && s[i + 1] != '/' && !is_path_end(s[i + 1])) {
            start = true;   // `wav:/abs/path`, `file:/abs/path`; a `scheme://` URL is not a host path
        } else if (can_start_path_after(prev)) {
            if (c == '/' && i + 1 < s.size() && !is_path_end(s[i + 1])) start = true;
            else if (c == '\\' && i + 1 < s.size() && s[i + 1] == '\\' && i + 2 < s.size() &&
                     !is_path_end(s[i + 2])) start = true;   // UNC
            else if (std::isalpha(static_cast<unsigned char>(c)) && i + 2 < s.size() && s[i + 1] == ':' &&
                     (s[i + 2] == '\\' || s[i + 2] == '/')) start = true;   // X:\ or X:/
        }
        if (!start) { out += c; ++i; continue; }
        size_t j = i;
        while (j < s.size() && !is_path_end(s[j])) ++j;
        const std::string token = s.substr(i, j - i);
        out += "<path>/" + base_name(token);
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The key table
// ---------------------------------------------------------------------------

KeyClass key_class(const std::string& subsystem, const std::string& key)
{
    const std::string k = subsystem + "." + key;
    for (const KeyRow& r : kKeyTable)
        if (k == r.key) return r.cls;
    return KeyClass::Memory;
}

std::vector<std::string> classified_keys()
{
    std::vector<std::string> v;
    for (const KeyRow& r : kKeyTable) v.push_back(r.key);
    return v;
}

}  // namespace bug_report
