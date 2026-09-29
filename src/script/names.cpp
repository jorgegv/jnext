// jnext::script — the name tables. See names.h.

#include "script/names.h"

namespace jnext {
namespace script {

namespace {

const char* const RESERVED[] = {
    // structure
    "var", "disabled", "on", "once", "when", "do", "end",
    // events and their qualifiers
    "execute", "read", "write", "io_read", "io_write", "nextreg", "frame", "scanline",
    "cycle", "interrupt", "nmi", "reset", "hostkey", "stop", "copper", "move", "wait",
    "halt", "at", "dma", "start", "byte", "page", "mask", "value",
    // actions
    "log", "indent", "assert", "exit", "dump_regs", "dump_mmu", "dump_mem", "snap",
    "unsnap", "dump_diff", "enable", "disable", "screenshot", "save_snapshot",
    "compare_scr", "press", "for", "release", "joystick", "set", "out", "if", "then",
    "else",
    // operators and literals
    "and", "or", "not", "true", "false",
    // accessor stems (lexed as one token with their bracket)
    "mem", "mem16", "phys", "mmu", "stack", "changed", "depth",
};

struct NameRow {
    const char* name;
    Builtin     b;
};

const NameRow LIVE[] = {
    {"A", Builtin::A},       {"B", Builtin::B},       {"C", Builtin::C},
    {"D", Builtin::D},       {"E", Builtin::E},       {"H", Builtin::H},
    {"L", Builtin::L},       {"F", Builtin::F},       {"I", Builtin::I},
    {"R", Builtin::R},       {"AF", Builtin::AF},     {"BC", Builtin::BC},
    {"DE", Builtin::DE},     {"HL", Builtin::HL},     {"IX", Builtin::IX},
    {"IY", Builtin::IY},     {"SP", Builtin::SP},     {"PC", Builtin::PC},
    {"AF2", Builtin::AF2},   {"BC2", Builtin::BC2},   {"DE2", Builtin::DE2},
    {"HL2", Builtin::HL2},   {"CF", Builtin::CF},     {"ZF", Builtin::ZF},
    {"SF", Builtin::SF},     {"PF", Builtin::PF},     {"HF", Builtin::HF},
    {"NF", Builtin::NF},     {"IFF1", Builtin::IFF1}, {"IFF2", Builtin::IFF2},
    {"IM", Builtin::IM},     {"HALTED", Builtin::HALTED},
    {"FRAME", Builtin::FRAME}, {"CYCLE", Builtin::CYCLE}, {"TFRAME", Builtin::TFRAME},
    {"RAW_HC", Builtin::RAW_HC}, {"RAW_VC", Builtin::RAW_VC},
    {"HC_ULA", Builtin::HC_ULA}, {"VC_ULA", Builtin::VC_ULA},
    {"CVC", Builtin::CVC},   {"PHC", Builtin::PHC},
    {"AUDIO_MUTE", Builtin::AUDIO_MUTE}, {"MACHINE", Builtin::MACHINE},
};

const NameRow PAYLOAD[] = {
    {"ADDR", Builtin::P_ADDR},     {"VALUE", Builtin::P_VALUE},
    {"PREV", Builtin::P_PREV},     {"PAGE", Builtin::P_PAGE},
    {"PORT", Builtin::P_PORT},     {"REG", Builtin::P_REG},
    {"SOURCE", Builtin::P_SOURCE}, {"KEY", Builtin::P_KEY},
    {"REASON", Builtin::P_REASON}, {"CPC", Builtin::P_CPC},
    {"WAIT_V", Builtin::P_WAIT_V}, {"WAIT_H", Builtin::P_WAIT_H},
    {"SRC", Builtin::P_SRC},       {"DST", Builtin::P_DST},
    {"LEN", Builtin::P_LEN},       {"DMA_MODE", Builtin::P_DMA_MODE},
    {"IO_SRC", Builtin::P_IO_SRC}, {"IO_DST", Builtin::P_IO_DST},
    {"PC", Builtin::P_PC},         {"HC_ULA", Builtin::P_HC_ULA},
    {"CVC", Builtin::P_CVC},
};

}  // namespace

bool is_reserved_word(const std::string& word) {
    for (const char* r : RESERVED)
        if (word == r) return true;
    return false;
}

std::optional<Builtin> live_builtin(const std::string& name) {
    for (const NameRow& r : LIVE)
        if (name == r.name) return r.b;
    return std::nullopt;
}

std::optional<Builtin> payload_builtin(const std::string& name) {
    for (const NameRow& r : PAYLOAD)
        if (name == r.name) return r.b;
    return std::nullopt;
}

std::optional<int32_t> source_constant(const std::string& name) {
    // The SOURCE constants of §2.1 — CPU 0, DMA 1, COPPER 2. NOT the backend's
    // `EventSource` numbering (Cpu 0, Copper 1, Dma 2): the evaluator maps a
    // payload `source` onto these.
    if (name == "CPU") return 0;
    if (name == "DMA") return 1;
    if (name == "COPPER") return 2;
    return std::nullopt;
}

bool is_builtin_name(const std::string& name) {
    return live_builtin(name) || payload_builtin(name) || source_constant(name);
}

bool is_assignable(Builtin b) {
    switch (b) {
        case Builtin::A: case Builtin::B: case Builtin::C: case Builtin::D:
        case Builtin::E: case Builtin::H: case Builtin::L: case Builtin::F:
        case Builtin::I: case Builtin::R:
        case Builtin::AF: case Builtin::BC: case Builtin::DE: case Builtin::HL:
        case Builtin::IX: case Builtin::IY: case Builtin::SP: case Builtin::PC:
        case Builtin::AF2: case Builtin::BC2: case Builtin::DE2: case Builtin::HL2:
        case Builtin::CF: case Builtin::ZF: case Builtin::SF: case Builtin::PF:
        case Builtin::HF: case Builtin::NF:
        case Builtin::IFF1: case Builtin::IFF2: case Builtin::IM:
        case Builtin::AUDIO_MUTE:
            return true;
        default:
            return false;
    }
}

const char* builtin_spelling(Builtin b) {
    for (const NameRow& r : LIVE)
        if (r.b == b) return r.name;
    for (const NameRow& r : PAYLOAD)
        if (r.b == b) return r.name;
    return "?";
}

}  // namespace script
}  // namespace jnext
