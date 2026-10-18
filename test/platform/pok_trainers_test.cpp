// POK trainer files (GH #381): the parser and the session that applies and
// removes trainers through the debugger backend (src/platform/pok_trainers.*).
//
// ORACLES. Nothing here is derived by reading the implementation back.
//   - The file format is the World of Spectrum POKformat.txt (the owner's link
//     on the issue): tags N / M / Z / Y, the four decimal fields, bank bits 0-2
//     and bit 3 "ignore", address 16384-65535, value 0-255 or 256 = ask, original
//     0 = not given. Real files agree with it: PHANTOM1.POK (CRLF, names padded
//     to 30, two-space right-aligned fields, a final Y with no line end) shapes
//     POK-01, and a survey of 8710 real .pok files (LF in 5515, CRLF in 3190, a
//     single space after the tag, 4446 of 43767 names over 30 bytes, 138 with
//     UTF-8, 6698 pokes with value 256, 53 trainers poking one address twice, 22
//     banked pokes below 0xC000) sizes the cases. No file of that survey, and no
//     game's file, is in this repository: every fixture below is written here.
//   - Where a poke lands is the hardware: port 0x7FFD writes MMU6 and MMU7 only
//     (zxnext.vhd:4679-4680, bank bits from :3763), MMU2-5 keep pages 0A 0B 04 05
//     (:4613-4616), the banks 5 and 7 are BRAM on the Next (:6562, :6670), and
//     an SRAM page >= 0xE0 does not respond (:3061). On a 48K, 0x7FFD is not
//     decoded (port_7ffd_io_en, :2399, :2593; NR 0x82 = 0xC0 at power-on).
//   - SNA layout (POK-45): the 48K form is a 27-byte header + RAM from 0x4000;
//     the 128K form is the header + banks 5, 2 and the paged one, a 4-byte
//     extended header, then the other banks in ascending order.
// The values a row checks are bytes THIS TEST placed first, always different from
// the byte a trainer writes, so a write is visible and a missed write is too.
//
// Run: ./build/test/pok_trainers_test

#include "platform/pok_trainers.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/rzx.h"
#include "core/rzx_player.h"
#include "core/sna_saver.h"
#include "debug/debugger.h"
#include "memory/mmu.h"

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

using namespace jnext::pok;
using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%02X", v);
    return b;
}

// ── fixtures: text ─────────────────────────────────────────────────────────

std::string pad(std::string s, size_t n) {
    if (s.size() < n) s.resize(n, ' ');
    return s;
}

/// PHANTOM1's shape: CRLF, names padded to 30, two-space right-aligned fields, a
/// final Y with no line end. `eol` swaps the line end.
std::string sgd_text(const std::string& eol) {
    return "N" + pad("Infinite lives", 30) + eol +
           "M  8 32771   0  53" + eol +
           "Z  8 32772  33   0" + eol +
           "N" + pad("Start with 9 lives", 30) + eol +
           "Z  8 36864 256   3" + eol +
           "Y";
}

/// The corpus shape: LF, one space after the tag, nothing padded.
std::string tight_text(const std::string& eol) {
    return "NInfinite lives" + eol + "M 8 32771 0 53" + eol + "Z 8 32772 33 0" + eol +
           "NStart with 9 lives" + eol + "Z 8 36864 256 3" + eol + "Y";
}

bool poke_is(const Poke& p, int bank, int address, int value, int original, int line) {
    return p.bank == bank && p.address == address && p.value == value &&
           p.original == original && p.line == line;
}

std::string describe(const Poke& p) {
    return "(" + std::to_string(p.bank) + "," + std::to_string(p.address) + "," +
           std::to_string(p.value) + "," + std::to_string(p.original) + ",l" +
           std::to_string(p.line) + ")";
}

/// Two parses carry the same trainers: names, lines, problems, every poke field.
bool same_file(const PokFile& a, const PokFile& b, std::string& why) {
    if (a.trainers.size() != b.trainers.size() || a.problems != b.problems) {
        why = "trainer count or file problems differ";
        return false;
    }
    for (size_t i = 0; i < a.trainers.size(); ++i) {
        const Trainer& x = a.trainers[i];
        const Trainer& y = b.trainers[i];
        if (x.name != y.name || x.line != y.line || x.problem != y.problem ||
            x.pokes.size() != y.pokes.size()) {
            why = "trainer " + std::to_string(i) + " differs";
            return false;
        }
        for (size_t k = 0; k < x.pokes.size(); ++k)
            if (!poke_is(x.pokes[k], y.pokes[k].bank, y.pokes[k].address, y.pokes[k].value,
                         y.pokes[k].original, y.pokes[k].line)) {
                why = "trainer " + std::to_string(i) + " poke " + std::to_string(k) + " " +
                      describe(x.pokes[k]) + " vs " + describe(y.pokes[k]);
                return false;
            }
    }
    return true;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size())
        s.replace(at, from.size(), to);
    return s;
}

// ── fixtures: a machine ────────────────────────────────────────────────────

/// A real Emulator with the debugger backend over it, the way the backend suite
/// builds one (no ROM, no SD image), and a session on a file of text. Heap
/// allocated: an Emulator is too big to keep a handful on a stack.
struct Rig {
    std::unique_ptr<Emulator> emu = std::make_unique<Emulator>();
    std::unique_ptr<Debugger> dbg;
    std::unique_ptr<Session>  s;
    bool ok = false;

    explicit Rig(MachineType type) {
        EmulatorConfig cfg;
        cfg.type = type;
        ok = emu->init(cfg);
        dbg = std::make_unique<Debugger>(*emu);
    }
    void load(const std::string& text) {
        s = std::make_unique<Session>(*dbg, parse_pok(text), "test.pok");
    }
    uint8_t rd(uint16_t a) const { return emu->mmu().peek(a); }
    void wr(uint16_t a, uint8_t v) { emu->mmu().write(a, v); }
    /// A byte of NR page `page` at `off`, whatever is mapped where.
    uint8_t rdp(unsigned page, unsigned off) const {
        uint8_t b = 0xEE;
        dbg->peek(MemSpace::page(static_cast<uint16_t>(page)), off, 1, &b);
        return b;
    }
    void wrp(unsigned page, unsigned off, uint8_t v) {
        dbg->poke(1, MemSpace::page(static_cast<uint16_t>(page)), off, 1, &v);
    }
    /// Page 16K bank `b` at 0xC000 the way a 128K program does.
    void bank(unsigned b) { dbg->port_out(1, 0x7FFD, static_cast<uint8_t>(0x10 | b)); }
    std::string apply(size_t i, std::optional<uint8_t> v = std::nullopt) { return s->apply(i, v); }
};

// ── the parser rows ────────────────────────────────────────────────────────

void test_parser() {
    // POK-01: PHANTOM1's real layout.
    {
        const PokFile f = parse_pok(sgd_text("\r\n"));
        const bool shape = f.trainers.size() == 2 && f.problems.empty();
        const bool t0 = shape && f.trainers[0].name == "Infinite lives" && f.trainers[0].line == 1 &&
                        f.trainers[0].problem.empty() && f.trainers[0].pokes.size() == 2 &&
                        poke_is(f.trainers[0].pokes[0], 8, 32771, 0, 53, 2) &&
                        poke_is(f.trainers[0].pokes[1], 8, 32772, 33, 0, 3) &&
                        !f.trainers[0].asks_value();
        const bool t1 = shape && f.trainers[1].name == "Start with 9 lives" && f.trainers[1].line == 4 &&
                        f.trainers[1].problem.empty() && f.trainers[1].pokes.size() == 1 &&
                        poke_is(f.trainers[1].pokes[0], 8, 36864, 256, 3, 5) &&
                        f.trainers[1].asks_value();
        check("POK-01", "an SGD-era file (CRLF, names padded to 30, two-space right-aligned fields, a final Y "
                        "with no line end - PHANTOM1's shape, WoS POKformat.txt) parses to its two trainers",
              t0 && t1, shape ? describe(f.trainers[0].pokes.empty() ? Poke{} : f.trainers[0].pokes[0])
                              : std::to_string(f.trainers.size()) + " trainers");
    }

    // POK-02: the other line ends and the corpus's single space parse the same.
    {
        const PokFile ref = parse_pok(sgd_text("\r\n"));
        std::string why_lf, why_cr, why_tight;
        const bool lf = same_file(ref, parse_pok(replace_all(sgd_text("\r\n"), "\r\n", "\n")), why_lf);
        const bool cr = same_file(ref, parse_pok(replace_all(sgd_text("\r\n"), "\r\n", "\r")), why_cr);
        const bool tight = same_file(ref, parse_pok(tight_text("\n")), why_tight);
        const bool tight_crlf = same_file(ref, parse_pok(tight_text("\r\n")), why_tight);
        check("POK-02", "LF, CR and CRLF line ends and a single space between fields parse exactly as the padded "
                        "CRLF file (WoS POKformat.txt: one or more spaces; the survey: LF 5515, CRLF 3190)",
              lf && cr && tight && tight_crlf, why_lf + "|" + why_cr + "|" + why_tight);
    }

    // POK-03
    {
        const PokFile f = parse_pok("NT\nM8 32771 0 53\nZ8 32772 1 0\n");
        const PokFile tabs = parse_pok("NTabbed\t \nM8 32771 0 53\nZ\t8\t32772 \t 1 0 \t\n");
        check("POK-03", "no space between the tag and the bank parses (WoS POKformat.txt: \"theoretically no "
                        "space\" there); so do tabs between the fields, blanks after the last one, and "
                        "blanks after a name, which are trimmed",
              f.trainers.size() == 1 && f.trainers[0].problem.empty() && f.trainers[0].pokes.size() == 2 &&
                  poke_is(f.trainers[0].pokes[0], 8, 32771, 0, 53, 2) &&
                  poke_is(f.trainers[0].pokes[1], 8, 32772, 1, 0, 3) &&
                  tabs.trainers.size() == 1 && tabs.trainers[0].name == "Tabbed" &&
                  tabs.trainers[0].problem.empty() && tabs.trainers[0].pokes.size() == 2 &&
                  poke_is(tabs.trainers[0].pokes[1], 8, 32772, 1, 0, 3));
    }

    // POK-04
    {
        std::string text;
        const int banks[] = {0, 7, 8, 9, 15, 16};
        for (int b : banks) text += "NB" + std::to_string(b) + "\nZ " + std::to_string(b) + " 32768 1 0\n";
        const PokFile f = parse_pok(text);
        bool ok = f.trainers.size() == 6;
        for (size_t k = 0; ok && k < 5; ++k)
            ok = f.trainers[k].problem.empty() && f.trainers[k].pokes.size() == 1 &&
                 f.trainers[k].pokes[0].bank == banks[k] &&
                 f.trainers[k].pokes[0].banked() == (banks[k] < 8);
        ok = ok && f.trainers[5].problem == "line 12: bank 16 is not 0-15";
        check("POK-04", "bank bits 0-2 are the bank and bit 3 means ignore it: 0 and 7 are banked, 8, 9 and 15 "
                        "are not, 16 invalidates its trainer (WoS POKformat.txt: bank 0-7, bit 3 = ignore)",
              ok, f.trainers.size() == 6 ? f.trainers[5].problem : std::string("not 6 trainers"));
    }

    // POK-05
    {
        const PokFile f = parse_pok("NA\nZ 8 32768 256 0\nNB\nZ 8 32768 257 0\n");
        check("POK-05", "value 256 asks the user and is valid; 257 invalidates its trainer (WoS POKformat.txt: "
                        "0-255, or 256 = ask)",
              f.trainers.size() == 2 && f.trainers[0].problem.empty() && f.trainers[0].asks_value() &&
                  f.trainers[1].problem == "line 4: value 257 is not 0-256" && !f.trainers[1].asks_value(),
              f.trainers.size() == 2 ? f.trainers[1].problem : std::string("not 2 trainers"));
    }

    // POK-06
    {
        const PokFile f = parse_pok("NA\nZ 8 16383 1 0\nNB\nZ 8 16384 1 0\nNC\nZ 8 65535 1 0\nND\nZ 8 65536 1 0\n"
                                    "NE\nZ 8 99999999999999999999 1 0\nNF\nZ 8 4294983680 1 0\n");
        check("POK-06", "addresses 16384 and 65535 are valid, 16383 and 65536 invalidate their trainers, "
                        "naming their lines, and so does a number too long for any integer, shown cut short "
                        "(WoS POKformat.txt: address 16384-65535)",
              f.trainers.size() == 6 &&
                  f.trainers[0].problem == "line 2: address 16383 is outside 16384-65535" &&
                  f.trainers[1].problem.empty() && f.trainers[2].problem.empty() &&
                  f.trainers[3].problem == "line 8: address 65536 is outside 16384-65535" &&
                  f.trainers[4].problem == "line 10: address 9999999999... is outside 16384-65535" &&
                  f.trainers[5].problem == "line 12: address 4294983680 is outside 16384-65535",
              f.trainers.size() == 6 ? f.trainers[4].problem + " | " + f.trainers[5].problem
                                     : std::string("not 6 trainers"));
    }

    // POK-07
    {
        const PokFile f = parse_pok("NA\nZ 8 32768 1 0\nNB\nZ 8 32768 1 255\nNC\nZ 8 32768 1 256\n");
        check("POK-07", "original 0 (not given) and 255 are kept, 256 invalidates its trainer (WoS "
                        "POKformat.txt: original 0-255, 0 when not given)",
              f.trainers.size() == 3 && f.trainers[0].problem.empty() &&
                  f.trainers[0].pokes[0].original == 0 && f.trainers[1].problem.empty() &&
                  f.trainers[1].pokes[0].original == 255 &&
                  f.trainers[2].problem == "line 6: original 256 is not 0-255",
              f.trainers.size() == 3 ? f.trainers[2].problem : std::string("not 3 trainers"));
    }

    // POK-08: five ways to get the four fields wrong, each followed by a good trainer.
    {
        const char* bad[] = {"M  8 32768 0", "M  8 32768 0 0 7", "M  8 3x768 0 0", "M  8 -1 0 0",
                             "M  8 +32768 0 0"};
        std::string detail;
        bool all = true;
        for (const char* line : bad) {
            const PokFile f = parse_pok(std::string("NA\n") + line + "\nZ  8 32768 1 0\nNB\nZ  8 32768 1 0\n");
            const bool ok = f.trainers.size() == 2 && f.problems.empty() &&
                            f.trainers[0].problem == "line 2: expected 4 numbers (bank address value original)" &&
                            f.trainers[1].problem.empty() && f.trainers[1].pokes.size() == 1;
            if (!ok) {
                all = false;
                detail += std::string("[") + line + " -> " +
                          (f.trainers.empty() ? std::string("nothing") : f.trainers[0].problem) + "]";
            }
        }
        check("POK-08", "a poke line with 3 numbers, 5 numbers, a letter in a number, a sign, invalidates its "
                        "trainer naming the line, and the next trainer is read normally (WoS POKformat.txt: "
                        "four decimal fields)",
              all, detail);
    }

    // POK-09
    {
        const PokFile f = parse_pok("NA\nM 8 32768 1 0\nNB\nZ 8 32769 1 0\n");
        check("POK-09", "an N before the previous trainer's Z invalidates the unfinished one, and the new one "
                        "is read normally (WoS POKformat.txt: M = not the last poke, Z = the last)",
              f.trainers.size() == 2 &&
                  f.trainers[0].problem == "line 2: trainer ends without its 'Z' line" &&
                  f.trainers[1].problem.empty() && f.trainers[1].name == "B",
              f.trainers.size() == 2 ? f.trainers[0].problem : std::string("not 2 trainers"));
    }

    // POK-10
    {
        const PokFile f = parse_pok("NA\nNB\nZ 8 32769 1 0\n");
        check("POK-10", "a trainer with no poke lines is invalid; the next one is read normally (WoS "
                        "POKformat.txt: a trainer is its N line and its pokes)",
              f.trainers.size() == 2 && f.trainers[0].problem == "line 1: trainer has no poke lines" &&
                  f.trainers[1].problem.empty(),
              f.trainers.size() == 2 ? f.trainers[0].problem : std::string("not 2 trainers"));
    }

    // POK-11
    {
        const PokFile y = parse_pok("NA\nM 8 32768 1 0\nY\n");
        const PokFile eof = parse_pok("NA\nM 8 32768 1 0\n");
        check("POK-11", "a Y or the end of the file inside a trainer invalidates it (WoS POKformat.txt: Z "
                        "closes a trainer, Y ends the file)",
              y.trainers.size() == 1 && y.trainers[0].problem == "line 2: trainer ends without its 'Z' line" &&
                  eof.trainers.size() == 1 &&
                  eof.trainers[0].problem == "line 2: trainer ends without its 'Z' line",
              (y.trainers.empty() ? std::string("no trainer") : y.trainers[0].problem) + " | " +
                  (eof.trainers.empty() ? std::string("no trainer") : eof.trainers[0].problem));
    }

    // POK-12
    {
        const PokFile f = parse_pok("NA\nZ 8 32768 1 0\nY\nhello\nM 8 1 0 0\nNB\nZ 8 32768 1 0\n");
        check("POK-12", "everything after Y is ignored: no extra trainer, no problem (WoS POKformat.txt: the "
                        "rest of the file is ignored)",
              f.trainers.size() == 1 && f.trainers[0].problem.empty() && f.problems.empty());
    }

    // POK-13
    {
        const PokFile f = parse_pok("NA\nZ 8 32768 1 0\nNB\nM 8 32768 1 0\nZ 8 32769 1 0");
        check("POK-13", "a file without its final Y is complete: every finished trainer is valid and nothing "
                        "is reported (a missing Y is not a defect the user can act on; Fuse disables the last "
                        "trainer, which this does not follow)",
              f.trainers.size() == 2 && f.trainers[0].problem.empty() && f.trainers[1].problem.empty() &&
                  f.trainers[1].pokes.size() == 2 && f.problems.empty());
    }

    // POK-14: blank and whitespace-only lines are ignored, but still counted for line numbers.
    {
        const std::string text = "\n"
                                 "N" + pad("Infinite lives", 30) + "\r\n"
                                 "   \t \r\n"
                                 "M  8 32771   0  53\r\n"
                                 "\r\n"
                                 "Z  8 32772  33   0\r\n"
                                 "\r\n"
                                 "N" + pad("Start with 9 lives", 30) + "\r\n"
                                 "Z  8 36864 256   3\r\n"
                                 "\r\n"
                                 "Y";
        const PokFile f = parse_pok(text);
        const bool ok = f.trainers.size() == 2 && f.problems.empty() &&
                        f.trainers[0].name == "Infinite lives" && f.trainers[0].line == 2 &&
                        f.trainers[0].problem.empty() && f.trainers[0].pokes.size() == 2 &&
                        poke_is(f.trainers[0].pokes[0], 8, 32771, 0, 53, 4) &&
                        poke_is(f.trainers[0].pokes[1], 8, 32772, 33, 0, 6) &&
                        f.trainers[1].name == "Start with 9 lives" && f.trainers[1].line == 8 &&
                        f.trainers[1].problem.empty() &&
                        poke_is(f.trainers[1].pokes[0], 8, 36864, 256, 3, 9);
        check("POK-14", "empty and whitespace-only lines between trainers and between pokes change nothing, "
                        "and the lines after them are numbered as the file numbers them (the survey's files "
                        "have none; stray ones are not a defect)",
              ok, f.trainers.size() == 2 ? std::to_string(f.trainers[1].line) : std::string("not 2 trainers"));
    }

    // POK-15
    {
        const PokFile f = parse_pok("NA\nZ 8 32768 1 0\nXyz\nNB\nZ 8 32768 1 0\n");
        check("POK-15", "a line of an unknown type outside a trainer is reported with its line and the first "
                        "character, and no trainer is lost (WoS POKformat.txt: the first character is the type)",
              f.trainers.size() == 2 && f.trainers[0].problem.empty() && f.trainers[1].problem.empty() &&
                  f.problems.size() == 1 && f.problems[0] == "line 3: unexpected line (starts with 'X')",
              f.problems.empty() ? std::string("no problem") : f.problems[0]);
    }

    // POK-16
    {
        const PokFile inside = parse_pok("NA\nXyz\nZ 8 32768 1 0\nNB\nZ 8 32768 1 0\n");
        const PokFile lower = parse_pok("nfoo\nNA\nZ 8 32768 1 0\n");
        const PokFile bom = parse_pok("\xEF\xBB\xBFNA\nZ 8 32768 1 0\nNB\nZ 8 32768 1 0\n");
        check("POK-16", "an unknown line inside a trainer invalidates it; tags are uppercase only; a UTF-8 BOM "
                        "before the first N is reported as byte 0xEF on line 1 (WoS POKformat.txt: the first "
                        "character is the type)",
              inside.trainers.size() == 2 &&
                  inside.trainers[0].problem == "line 2: unexpected line (starts with 'X')" &&
                  inside.trainers[1].problem.empty() && inside.problems.empty() &&
                  lower.problems.size() == 1 && lower.problems[0] == "line 1: unexpected line (starts with 'n')" &&
                  lower.trainers.size() == 1 && lower.trainers[0].problem.empty() &&
                  !bom.problems.empty() && bom.problems[0] == "line 1: unexpected line (starts with 0xEF)" &&
                  bom.trainers.size() == 1 && bom.trainers[0].name == "B",
              (inside.trainers.empty() ? std::string("no trainer") : inside.trainers[0].problem) + " | " +
                  (bom.problems.empty() ? std::string("no problem") : bom.problems[0]));
    }

    // POK-17
    {
        const PokFile f = parse_pok("M 8 32768 1 0\nNA\nZ 8 32768 1 0\nM 8 32769 1 0\nNB\nZ 8 32768 1 0\n");
        check("POK-17", "a poke line before any N, or after a trainer's Z, is a file problem with its line, "
                        "and the trainers around it are valid (WoS POKformat.txt: Z closes a trainer)",
              f.trainers.size() == 2 && f.trainers[0].problem.empty() && f.trainers[1].problem.empty() &&
                  f.problems.size() == 2 && f.problems[0] == "line 1: poke line outside a trainer" &&
                  f.problems[1] == "line 4: poke line outside a trainer",
              std::to_string(f.problems.size()) + " problems");
    }

    // POK-18
    {
        const std::string long_name = "Aliens always start at top of each level.";
        const std::string utf8 = std::string("Money (\xC2\xA3") + "50)";
        const PokFile f = parse_pok("N" + long_name + "!!!!\nZ 8 32768 1 0\nN" + utf8 +
                                    "\nZ 8 32768 1 0\nN\nZ 8 32768 1 0\n");
        const bool names = f.trainers.size() == 3 &&
                           f.trainers[0].name == long_name + "!!!!" && f.trainers[0].name.size() == 45 &&
                           f.trainers[1].name == utf8 &&
                           f.trainers[2].name.empty() && f.trainers[2].problem.empty();
        const bool utf8_ok = is_valid_utf8(utf8) && is_valid_utf8("") && is_valid_utf8("\xE2\x82\xAC") &&
                             is_valid_utf8("plain ASCII") && !is_valid_utf8("Caf\xE9") &&
                             !is_valid_utf8("\xC0\x80") && !is_valid_utf8("\xE0\x80\x80") &&
                             !is_valid_utf8("\xED\xA0\x80") && !is_valid_utf8("\xF0\x80\x80\x80") &&
                             !is_valid_utf8("\xF4\x90\x80\x80") && !is_valid_utf8("\xF5\x80\x80\x80") &&
                             !is_valid_utf8("\xFF") && !is_valid_utf8("\xE2\x82") &&
                             !is_valid_utf8("\xC3") && is_valid_utf8("\xF0\x9F\x98\x80");
        check("POK-18", "a 45-byte name and a name with UTF-8 bytes (\\xC2\\xA3) are kept byte for byte, and an "
                        "empty name is a name (the survey: 4446 names over 30 bytes, 138 with UTF-8); "
                        "is_valid_utf8 follows RFC 3629",
              names && utf8_ok, std::string(names ? "names ok" : "names differ") +
                                    (utf8_ok ? ", utf8 ok" : ", utf8 differs"));
    }

    // POK-24: a trainer that is wrong in several places reports the FIRST one (the line to fix first). The
    // fixtures put a different problem on each line, so the line named says which one was kept.
    {
        const PokFile pokes = parse_pok("NA\nM 8 99 1 0\nM 8 32768 300 0\nZ 8 99999 1 0\nNB\nZ 8 32768 1 0\n");
        const PokFile mixed = parse_pok("NC\nM 8 32768 1\nXyz\nZ 8 32768 1 0\nND\nZ 8 32768 1 0\n");
        check("POK-24", "a trainer with several bad lines reports the first: three bad poke lines name line 2 (the "
                        "address), and a bad poke line followed by an unknown line names the poke line "
                        "(jnext policy, plan D9: the trainer keeps its first problem); the next trainer is valid",
              pokes.trainers.size() == 2 &&
                  pokes.trainers[0].problem == "line 2: address 99 is outside 16384-65535" &&
                  pokes.trainers[1].problem.empty() && mixed.trainers.size() == 2 &&
                  mixed.trainers[0].problem == "line 2: expected 4 numbers (bank address value original)" &&
                  mixed.trainers[1].problem.empty(),
              (pokes.trainers.empty() ? std::string("no trainer") : pokes.trainers[0].problem) + " | " +
                  (mixed.trainers.empty() ? std::string("no trainer") : mixed.trainers[0].problem));
    }

    // POK-25: ... and a trainer that is already invalid keeps that problem when the next N, the Y or the end of
    // the file cuts it off - those say "no poke lines" / "without its Z" only for a trainer with no other problem.
    {
        const PokFile by_n = parse_pok("NA\nM 8 99 1 0\nNB\nZ 8 32768 1 0\n");        // no poke survived
        const PokFile by_eof = parse_pok("NA\nM 8 99 1 0\nM 8 32768 1 0\n");           // a good poke after it
        const PokFile by_y = parse_pok("NA\nM 8 99 1 0\nM 8 32768 1 0\nY\n");
        const std::string want = "line 2: address 99 is outside 16384-65535";
        check("POK-25", "a trainer cut off by the next N, by the end of the file or by Y after a bad poke line keeps "
                        "that line's problem, whether or not a good poke followed it (jnext policy, plan D9: "
                        "the trainer keeps its first problem)",
              by_n.trainers.size() == 2 && by_n.trainers[0].problem == want && by_n.trainers[1].problem.empty() &&
                  by_eof.trainers.size() == 1 && by_eof.trainers[0].problem == want &&
                  by_y.trainers.size() == 1 && by_y.trainers[0].problem == want,
              (by_n.trainers.empty() ? std::string("none") : by_n.trainers[0].problem) + " | " +
                  (by_eof.trainers.empty() ? std::string("none") : by_eof.trainers[0].problem) + " | " +
                  (by_y.trainers.empty() ? std::string("none") : by_y.trainers[0].problem));
    }

    // POK-60: the --pok-trainers list (GH #381, owner Q1).
    {
        std::vector<Pick> a, b, c, d;
        std::string ea, eb, ec, ed;
        const bool one = parse_picks("1", a, ea) && a.size() == 1 && a[0].number == 1 && !a[0].value;
        const bool many = parse_picks("1,3=9,7", b, eb) && b.size() == 3 && b[0].number == 1 && !b[0].value &&
                          b[1].number == 3 && b[1].value && *b[1].value == 9 && b[2].number == 7 && !b[2].value;
        const bool edges = parse_picks("12=255,2=0", c, ec) && c.size() == 2 && c[0].number == 12 &&
                           c[0].value && *c[0].value == 255 && c[1].number == 2 && c[1].value && *c[1].value == 0;
        // The order written is the order kept (the order trainers are applied in).
        const bool order = parse_picks("5,2,9", d, ed) && d.size() == 3 && d[0].number == 5 && d[1].number == 2 &&
                           d[2].number == 9;

        // Each bad list: refused, the error quotes the entry, and the output is left alone.
        struct Bad { const char* text; const char* quote; };
        const Bad bad[] = {{"", "list is empty"},       {"0", "'0'"},          {"x", "'x'"},
                           {"1,", "entry is empty"},    {",1", "entry is empty"}, {"1,,2", "entry is empty"},
                           {"1 ,2", "'1 '"},            {"1, 2", "' 2'"},      {"-1", "'-1'"},
                           {"+1", "'+1'"},              {"1.", "'1.'"},        {".5", "'.5'"},       {"0x10", "'0x10'"},
                           {"1=", "'1='"},              {"=5", "'=5'"},        {"1=256", "'1=256'"},
                           {"1=x", "'1=x'"},            {"1==2", "'1==2'"},    {"1=2=3", "'1=2=3'"},
                           {"1=-1", "'1=-1'"},          {"1000000", "'1000000'"},
                           {"99999999999999999999", "'9999999999...'"},
                           {"1,1", "twice"},            {"2,1,2=5", "'2=5' names trainer 2 twice"}};
        std::string detail;
        bool refused = true;
        for (const Bad& x : bad) {
            std::vector<Pick> keep(1);
            keep[0].number = 77;
            std::string err;
            const bool ok = parse_picks(x.text, keep, err);
            if (ok || keep.size() != 1 || keep[0].number != 77 || err.find(x.quote) == std::string::npos) {
                refused = false;
                detail += std::string("[") + x.text + " -> " + (ok ? "accepted" : err) + "]";
            }
        }
        check("POK-60", "a --pok-trainers list is comma-separated entries T or T=VALUE (T a trainer number from 1, "
                        "VALUE 0-255, decimal), kept in the order written; an empty list or entry, a zero, sign, "
                        "blank, hex or fractional number, a missing, too large or doubled value, a trainer named "
                        "twice (even with a value) is refused naming the entry, and leaves the output alone "
                        "(jnext's own list syntax; owner decision Q1)",
              one && many && edges && order && refused, detail + "|" + ea + eb + ec + ed);
    }
}

// ── load_pok_file rows (temp files) ────────────────────────────────────────

void test_load() {
    const std::string dir = jtp::make_temp_dir("jnext_pok_");
    auto write = [&](const char* name, const std::string& bytes) {
        const std::string p = dir + "/" + name;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return p;
    };

    // POK-19
    {
        const std::string missing = dir + "/does-not-exist.pok";
        PokFile f, g;
        std::string err, err_dir, err_dev;
        const bool ok = load_pok_file(missing, f, err);
        const bool dir_ok = load_pok_file(dir, g, err_dir);   // a directory is no file
        // Nor is a device: opened, /dev/null reads as an empty file and would say "holds no trainers".
        // (POSIX only: Windows has no such path, and the other two arms hold there.)
        bool device_refused = true;
#ifndef _WIN32
        PokFile h;
        device_refused = !load_pok_file("/dev/null", h, err_dev) &&
                         err_dev.find("/dev/null") != std::string::npos &&
                         err_dev.find("cannot read") != std::string::npos;
#endif
        check("POK-19", "a path that cannot be read (missing, a directory, or a device) fails with an error "
                        "that names the file",
              !ok && err.find(missing) != std::string::npos && err.find("cannot read") != std::string::npos &&
                  !dir_ok && err_dir.find(dir) != std::string::npos &&
                  err_dir.find("cannot read") != std::string::npos && device_refused,
              err + " | " + err_dir + " | " + err_dev);
    }

    // POK-20
    {
        const std::string big = write("big.pok", std::string(1024 * 1024 + 1, 'N'));
        const std::string max = write("max.pok", "NA\nZ 8 32768 1 0\n" + std::string(1024 * 1024 - 17, '\n'));
        PokFile f, g;
        std::string err, err_max;
        const bool refused = !load_pok_file(big, f, err);
        const bool taken = load_pok_file(max, g, err_max);
        check("POK-20", "a file of 1 MiB + 1 byte is refused as larger than 1 MiB (the largest of 8710 real "
                        "files is 7122 bytes); one of exactly 1 MiB is read",
              refused && err.find("larger than 1 MiB") != std::string::npos && taken && g.trainers.size() == 1,
              err + " | " + err_max);
    }

    // POK-21
    {
        const std::string only_y = write("only-y.pok", "Y");
        const std::string junk = write("junk.pok", "hello\n");
        PokFile f, g;
        std::string err, err_junk;
        const bool ok = load_pok_file(only_y, f, err);
        const bool junk_ok = load_pok_file(junk, g, err_junk);
        check("POK-21", "a file with no trainer (here only Y; 5 of the 8710 real files are such) fails with "
                        "\"holds no trainers\"; one that is not a POK file at all says so and quotes its first "
                        "unexpected line",
              !ok && err.find("holds no trainers") != std::string::npos && !junk_ok &&
                  err_junk.find("holds no trainers") != std::string::npos &&
                  err_junk.find("line 1: unexpected line (starts with 'h')") != std::string::npos,
              err + " | " + err_junk);
    }

    // POK-22
    {
        const std::string invalid = write("invalid.pok", "NBroken\nM 8 99 1 0\nZ 8 32768 1 0\n");
        const std::string valid = write("valid.pok", sgd_text("\r\n"));
        PokFile f, g;
        std::string err, err_valid;
        const bool loaded = load_pok_file(invalid, f, err);
        const bool loaded_valid = load_pok_file(valid, g, err_valid);
        check("POK-22", "a file whose only trainer is invalid LOADS, the trainer listed with its problem; a good "
                        "file loads with none",
              loaded && f.trainers.size() == 1 &&
                  f.trainers[0].problem == "line 2: address 99 is outside 16384-65535" && loaded_valid &&
                  g.trainers.size() == 2 && g.trainers[0].problem.empty(),
              err + " | " + err_valid);
    }

    // POK-23
    {
        check("POK-23", "is_pok_path matches the extension .pok in any case, and nothing else",
              is_pok_path("a.pok") && is_pok_path("A.POK") && is_pok_path("dir/x.Pok") &&
                  is_pok_path("C:\\games\\x.pOk") && !is_pok_path("x.pokx") && !is_pok_path("pok") &&
                  !is_pok_path(".po") && !is_pok_path("x.pok.txt") && !is_pok_path("") &&
                  !is_pok_path("x.tap"));
    }

    // POK-61: a --delayed-pok-frames request is read and checked against its file (GH #381, owner Q1).
    {
        const std::string text = std::string("NFixed\nZ  8 32771 0 53\n"
                                             "NLives\nZ  8 32772 256 0\n"
                                             "NBroken\nM  8 99 1 0\nZ  8 32773 1 0\n"
                                             "NBoth\nM  8 32774 256 0\nZ  8 32775 5 0\n"
                                             "NCaf\xE9 and a very long name that goes on past forty bytes\nZ  8 32776 256 0\n");
        const std::string path = write("request.pok", text);
        auto pick = [](size_t n, int v = -1) {
            Pick p;
            p.number = n;
            if (v >= 0) p.value = static_cast<uint8_t>(v);
            return p;
        };
        // Requests that must be accepted: the fixed trainer alone, a value trainer with its value, several.
        CliRequest okr, okr2;
        const std::string e1 = make_cli_request(path, {pick(1)}, okr);
        const std::string e2 = make_cli_request(path, {pick(1), pick(2, 5), pick(4, 0)}, okr2);
        const bool good = e1.empty() && okr.path == path && okr.file.trainers.size() == 5 && okr.picks.size() == 1 &&
                          e2.empty() && okr2.picks.size() == 3 && okr2.picks[1].number == 2 &&
                          okr2.picks[1].value && *okr2.picks[1].value == 5 && okr2.picks[2].number == 4;

        struct Bad { std::vector<Pick> picks; std::vector<const char*> must_say; };
        const Bad bad[] = {
            {{pick(6)}, {"trainer 6", "has only 5 trainer(s)"}},                                   // one past the end
            {{pick(1), pick(3)}, {"trainer 3", "('Broken')", "invalid", "line 6: address 99 is outside"}},
            {{pick(2)}, {"trainer 2", "('Lives')", "leaves a value open", "2=VALUE"}},
            {{pick(4)}, {"trainer 4", "('Both')", "leaves a value open", "4=VALUE"}},             // 256 and a fixed poke
            {{pick(1, 4)}, {"trainer 1", "('Fixed')", "leaves no value open", "=4"}},
            {{pick(1), pick(2, 5), pick(1, 0)}, {"trainer 1", "leaves no value open"}},            // the later entry
        };
        std::string detail;
        bool refused = true;
        for (const Bad& x : bad) {
            CliRequest keep;
            keep.path = "untouched";
            const std::string err = make_cli_request(path, x.picks, keep);
            bool says = !err.empty() && keep.path == "untouched";
            for (const char* w : x.must_say) says = says && err.find(w) != std::string::npos;
            if (!says) {
                refused = false;
                detail += "[" + err + "]";
            }
        }
        // The name in a message is ASCII and cut: Latin-1 byte 0xE9 as \xE9, more than 40 bytes as 40 and "...".
        CliRequest keep;
        const std::string name_err = make_cli_request(path, {pick(5)}, keep);
        const bool ascii_name = name_err.find("Caf\\xE9 and a very long name that goes on p...") !=
                                    std::string::npos &&
                                std::all_of(name_err.begin(), name_err.end(),
                                            [](char c) { return static_cast<unsigned char>(c) < 0x80; });
        // A file that cannot be read is the file's own error, and nothing is filled in.
        CliRequest none;
        none.path = "untouched";
        const std::string missing = make_cli_request(dir + "/no-such.pok", {pick(1)}, none);
        const std::string empty_file = make_cli_request(write("only-y.pok", "Y"), {pick(1)}, none);
        const bool file_errors = missing.find("cannot read") != std::string::npos &&
                                 empty_file.find("holds no trainers") != std::string::npos && none.path == "untouched";
        check("POK-61", "make_cli_request reads the file and checks the picks against it: a fixed trainer alone, a "
                        "value trainer with its value and several are accepted; a trainer past the end, an invalid "
                        "one, a value trainer without a value (also one that mixes 256 with a fixed poke), a fixed "
                        "trainer with a value are refused, naming the trainer by number and name (ASCII, cut at 40 "
                        "bytes) and what to do, and leave the output alone; an unreadable or trainerless file "
                        "gives the file's own error (jnext's own request rules; owner decision Q1)",
              good && refused && ascii_name && file_errors, detail + "|" + name_err + "|" + missing + "|" + empty_file);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ── the session rows ───────────────────────────────────────────────────────

constexpr uint16_t PROG = 0x8000;

/// The program of POK-46, at 0x8000, as machine code:
///   LD HL,0x9000 / DEC (HL) / LD BC,0x0400 / DEC BC; LD A,B; OR C; JR NZ / JR start
/// DEC (HL) is the byte at 0x8003 (0x35), the byte a trainer NOPs (0x00).
void put_program(Rig& r) {
    const uint8_t code[] = {0x21, 0x00, 0x90, 0x35, 0x01, 0x00, 0x04,
                            0x0B, 0x78, 0xB1, 0x20, 0xFB, 0x18, 0xF2};
    for (size_t i = 0; i < sizeof(code); ++i) r.wr(static_cast<uint16_t>(PROG + i), code[i]);
    Z80Registers regs = r.emu->cpu().get_registers();
    regs.PC = PROG;
    regs.SP = 0xFF00;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    r.emu->cpu().set_registers(regs);
}

void run_frames(Rig& r, int n) {
    for (int i = 0; i < n; ++i) r.emu->run_frame();
}

void test_session() {
    // POK-30: a bank-8 poke is the CPU map; Remove puts back the byte it replaced.
    {
        Rig r(MachineType::ZX48K);
        r.load("NInfinite lives\nZ  8 32771 0 53\n");
        r.wr(0x8003, 0x35);
        const std::string a = r.apply(0);
        const bool applied = a.empty() && r.rd(0x8003) == 0x00 && r.s->state(0) == State::Applied;
        const std::string d = r.s->remove(0);
        const bool restored = r.rd(0x8003) == 0x35 && r.s->state(0) == State::NotApplied;
        r.wr(0x8003, 0x00);   // the program writes the poked value itself: that is no applied trainer of ours
        const bool found = r.s->state(0) == State::AppliedFound;
        check("POK-30", "on a 48K a bank-8 poke lands at its address in the memory map (0x8000 is page 04, "
                        "zxnext.vhd:4613-4616) and Remove restores the byte it replaced and forgets the undo "
                        "record (the poked value in memory afterwards reads \"found\", not \"applied\")",
              r.ok && applied && d.empty() && restored && found, a + "|" + d + "|" + hex(r.rd(0x8003)));
    }

    // POK-31: a banked poke at 0xC000+ reaches its bank whatever is paged in, on every 128K-class machine.
    {
        std::string detail;
        bool all = true;
        const MachineType types[] = {MachineType::ZX128K, MachineType::ZX_PLUS3, MachineType::PENTAGON};
        for (MachineType t : types) {
            Rig r(t);
            r.load("NBank 3\nM  3 49443 90 0\nZ  3 57635 91 0\n");
            r.bank(0);
            r.wrp(6, 0x123, 0x11);   // bank 3 low half (0xC123 when paged), high half
            r.wrp(7, 0x123, 0x22);
            r.wrp(0, 0x123, 0x33);   // bank 0, which IS paged in
            r.wrp(1, 0x123, 0x44);
            const std::string a = r.apply(0);
            const bool untouched = r.rd(0xC123) == 0x33 && r.rd(0xE123) == 0x44;
            r.bank(3);
            const bool landed = r.rd(0xC123) == 90 && r.rd(0xE123) == 91;
            const std::string d = r.s->remove(0);
            const bool back = r.rd(0xC123) == 0x11 && r.rd(0xE123) == 0x22;
            if (!(r.ok && a.empty() && untouched && landed && d.empty() && back)) {
                all = false;
                detail += "[type " + std::to_string(static_cast<int>(t)) + " a=" + a + " untouched=" +
                          std::to_string(untouched) + " landed=" + std::to_string(landed) + " back=" +
                          std::to_string(back) + "]";
            }
        }
        check("POK-31", "on a 128K, a +3 and a Pentagon a bank-3 poke at 0xC123 / 0xE123 changes bank 3 (pages "
                        "6 and 7) and not the bank paged in; paging bank 3 shows it; Remove restores it (port "
                        "0x7FFD writes MMU6/MMU7, zxnext.vhd:3763, :4679-4680)",
              all, detail);
    }

    // POK-32: the Next's BRAM banks.
    {
        Rig r(MachineType::ZXN_ISSUE2);
        r.load("NBank 5\nZ  5 49443 77 0\nNBank 7\nZ  7 49443 78 0\n");
        r.wrp(0x0A, 0x123, 0x11);
        r.wrp(0x0E, 0x123, 0x22);
        const std::string a5 = r.apply(0);
        const std::string a7 = r.apply(1);
        const bool bank5 = a5.empty() && r.rd(0x4123) == 77;   // MMU2 = 0x0A
        r.bank(7);
        const bool bank7 = a7.empty() && r.rd(0xC123) == 78;   // MMU6 = 0x0E
        check("POK-32", "on the Next a bank-5 poke at 0xC123 is page 0x0A (visible at 0x4123) and a bank-7 poke "
                        "is page 0x0E (visible at 0xC123 once bank 7 is paged): the two BRAM banks "
                        "(zxnext.vhd:4613, :4679-4680, :6562, :6670)",
              r.ok && bank5 && bank7, a5 + "|" + a7 + "|" + hex(r.rd(0x4123)) + "|" + hex(r.rd(0xC123)));
    }

    // POK-33: a banked poke BELOW 0xC000 is the current map, not bank*0x4000 + (address & 0x3FFF).
    {
        Rig r(MachineType::ZX128K);
        r.load("NStarglider\nM  0 44796 2 0\nM  0 49151 5 0\nM  0 49152 6 0\nZ  0 60367 0 0\n");
        r.bank(4);
        r.wrp(5, 0x0EFC, 0x77);   // 0xAEFC in the map: bank 2, high half
        r.wrp(1, 0x0EFC, 0x66);   // where bank 0 + (44796 & 0x3FFF) would land
        r.wrp(1, 0x0BCF, 0x55);   // bank 0 at 0xEBCF
        r.wrp(5, 0x1FFF, 0x99);   // 0xBFFF, the last byte below the banked window
        r.wrp(0, 0x0000, 0x98);   // bank 0 at 0xC000, the first byte of it
        r.wrp(8, 0x0000, 0x97);   // bank 4 at 0xC000, which IS paged in
        const std::string a = r.apply(0);
        check("POK-33", "a bank-0 poke at 44796 (below 0xC000) lands at 0xAEFC of the current map (page 05) and "
                        "leaves the bank-0 byte at that offset alone; so does one at 0xBFFF; the bank-0 pokes "
                        "at 0xC000 and 60367 reach bank 0 (page 0, page 1) although bank 4 is paged (only "
                        "0xC000+ is banked, zxnext.vhd:4613-4616, :4679-4680)",
              r.ok && a.empty() && r.rd(44796) == 2 && r.rdp(5, 0x0EFC) == 2 && r.rdp(1, 0x0EFC) == 0x66 &&
                  r.rdp(1, 0x0BCF) == 0 && r.rdp(5, 0x1FFF) == 5 && r.rdp(0, 0x0000) == 6 &&
                  r.rdp(8, 0x0000) == 0x97 && r.rd(0xC000) == 0x97,
              a + "|" + hex(r.rd(44796)) + "|" + hex(r.rdp(1, 0x0EFC)) + "|" + hex(r.rdp(1, 0x0BCF)) + "|" +
                  hex(r.rdp(5, 0x1FFF)) + "|" + hex(r.rdp(0, 0)) + "|" + hex(r.rdp(8, 0)));
    }

    // POK-34: 48K has bank 0 at 0xC000 and nothing else; a refusal writes nothing.
    {
        Rig r(MachineType::ZX48K);
        r.load("NT\nM  8 32771 0 0\nZ  3 49443 0 0\nNU\nZ  0 49443 7 0\n");
        r.wr(0x8003, 0x5A);
        r.wr(0xC123, 0x6B);
        const std::string refused = r.apply(0);
        const bool atomic = r.rd(0x8003) == 0x5A;
        const std::string second = r.apply(1);
        check("POK-34", "on a 48K a bank-3 poke is refused naming its line and the 48K, and the trainer's other "
                        "poke is not written either; bank 0 at 0xC000 is the 48K's own RAM and is written "
                        "(0x7FFD is not decoded: port_7ffd_io_en, zxnext.vhd:2399, :2593; NR 0x82 = 0xC0)",
              r.ok && refused.find("line 3") != std::string::npos &&
                  refused.find("48K") != std::string::npos && atomic && second.empty() &&
                  r.rd(0xC123) == 7,
              refused + "|" + second + "|" + hex(r.rd(0x8003)) + "|" + hex(r.rd(0xC123)));
    }

    // POK-57: ... for every window below 0xC000 and every bank, on a 128K and on the Next: the bank is ignored.
    {
        // address, bank: banks that are NOT what the memory map shows there (5 at 0x4000, 2 at 0x8000), so a poke
        // through the bank would land elsewhere. The bank's byte is spelled as the other emulators index it:
        // bank*0x4000 + (address & 0x3FFF), in 8K pages.
        struct Case { int address, bank; };
        const Case cases[] = {{0x4000, 1}, {0x5FFF, 3}, {0x6000, 7}, {0x7FFF, 0},
                              {0x8000, 4}, {0x9FFF, 6}, {0xA000, 1}, {0xBFFF, 3}};
        std::string detail;
        bool all = true;
        for (MachineType t : {MachineType::ZX128K, MachineType::ZXN_ISSUE2}) {
            Rig r(t);
            std::string text;
            for (size_t i = 0; i < 8; ++i)
                text += "NC" + std::to_string(i) + "\nZ  " + std::to_string(cases[i].bank) + " " +
                        std::to_string(cases[i].address) + " " + std::to_string(0xA0 + i) + " 0\n";
            r.load(text);
            for (size_t i = 0; i < 8; ++i) {
                const unsigned lin = static_cast<unsigned>(cases[i].bank) * 0x4000u +
                                     static_cast<unsigned>(cases[i].address & 0x3FFF);
                r.wrp(lin / 0x2000, lin % 0x2000, static_cast<uint8_t>(0x11 + i));
                r.wr(static_cast<uint16_t>(cases[i].address), 0x5A);   // the memory map's byte there
            }
            for (size_t i = 0; i < 8; ++i) {
                const unsigned lin = static_cast<unsigned>(cases[i].bank) * 0x4000u +
                                     static_cast<unsigned>(cases[i].address & 0x3FFF);
                const std::string a = r.apply(i);
                const bool landed = a.empty() && r.rd(static_cast<uint16_t>(cases[i].address)) == 0xA0 + i &&
                                    r.rdp(lin / 0x2000, lin % 0x2000) == 0x11 + i;
                const std::string d = r.s->remove(i);
                const bool back = d.empty() && r.rd(static_cast<uint16_t>(cases[i].address)) == 0x5A;
                if (!(landed && back)) {
                    all = false;
                    detail += "[" + std::string(t == MachineType::ZX128K ? "128K" : "Next") + " " +
                              hex(static_cast<unsigned>(cases[i].address >> 8)) + " bank " +
                              std::to_string(cases[i].bank) + " a=" + a + " d=" + d + " map=" +
                              hex(r.rd(static_cast<uint16_t>(cases[i].address))) + " bank byte=" +
                              hex(r.rdp(lin / 0x2000, lin % 0x2000)) + "]";
                }
            }
            all = all && r.ok;
        }
        check("POK-57", "on a 128K and on the Next a bank 0-7 poke at an address below 0xC000 (the edges of every 8K "
                        "page of 0x4000-0xBFFF, banks 0, 1, 3, 4, 6, 7) writes the memory the map shows there and "
                        "leaves byte address & 0x3FFF of the named bank alone; Remove puts the map byte back (only "
                        "0xC000+ is banked: zxnext.vhd:4613-4616, :4679-4680; the real Starglider 128K: its "
                        "\"Infinite Missiles (Bank#0)\" line at 44796 is the CPU byte 0xAEFC)",
              all, detail);
    }

    // POK-35: undo goes back to where the trainer wrote, not to wherever the map points then.
    {
        Rig r(MachineType::ZX128K);
        r.load("NT\nZ  8 49443 66 0\n");
        r.bank(1);
        r.wrp(2, 0x123, 0x21);   // bank 1
        r.wrp(8, 0x123, 0x44);   // bank 4
        const std::string a = r.apply(0);
        const bool applied = a.empty() && r.rdp(2, 0x123) == 66 && r.rdp(8, 0x123) == 0x44;
        r.bank(4);
        const std::string d = r.s->remove(0);
        check("POK-35", "a bank-8 poke at 0xC123 with bank 1 paged changes bank 1; after bank 4 is paged, Remove "
                        "puts the byte back in bank 1 and does not touch bank 4",
              r.ok && applied && d.empty() && r.rdp(2, 0x123) == 0x21 && r.rdp(8, 0x123) == 0x44,
              a + "|" + d + "|" + hex(r.rdp(2, 0x123)) + "|" + hex(r.rdp(8, 0x123)));
    }

    // POK-36: one address poked twice, the second a fixed value.
    {
        Rig r(MachineType::ZX48K);
        r.load("NT\nM  8 32768 256 0\nZ  8 32768 4 0\n");
        r.wr(0x8000, 0x77);
        const std::string a = r.apply(0, 9);
        const bool ok_apply = a.empty() && r.rd(0x8000) == 4 && r.s->state(0) == State::Applied;
        const std::string d = r.s->remove(0);
        check("POK-36", "a trainer that pokes one address twice leaves the last value, reads Applied against "
                        "it, and Remove restores the byte from before the trainer, not the intermediate one "
                        "(the survey: 53 such trainers)",
              r.ok && ok_apply && d.empty() && r.rd(0x8000) == 0x77,
              a + "|" + d + "|" + hex(r.rd(0x8000)));
    }

    // POK-37: under an RZX the backend refuses, and so does the trainer.
    {
        Rig r(MachineType::ZX48K);
        r.load("NT\nZ  8 32771 0 53\n");
        r.wr(0x8003, 0x35);
        r.emu->rzx_player().start(RzxRecording{});
        const std::string refused = r.apply(0);
        const bool untouched = r.rd(0x8003) == 0x35 && r.s->state(0) == State::NotApplied;
        r.emu->rzx_player().stop();
        const std::string again = r.apply(0);
        r.emu->rzx_player().start(RzxRecording{});
        const std::string stuck = r.s->remove(0);   // the undo is an edit too
        const bool still = r.rd(0x8003) == 0x00 && r.s->state(0) == State::Applied;
        r.emu->rzx_player().stop();
        const std::string undone = r.s->remove(0);
        check("POK-37", "while an RZX plays, Apply is refused with a reason that says RZX and writes nothing; "
                        "once it stops the same trainer applies; Remove is refused the same way while one "
                        "plays and keeps the trainer applied (a memory edit would diverge the recording: "
                        "the backend's poke refuses under RZX)",
              r.ok && refused.find("RZX") != std::string::npos && untouched && again.empty() &&
                  stuck.find("RZX") != std::string::npos && still && undone.empty() && r.rd(0x8003) == 0x35,
              refused + "|" + again + "|" + stuck + "|" + undone + "|" + hex(r.rd(0x8003)));
    }

    // POK-38: state is derived from memory.
    {
        Rig r(MachineType::ZX48K);
        r.load("NT\nZ  8 32771 0 53\n");
        r.wr(0x8003, 0x35);
        r.apply(0);
        r.wr(0x8003, 0x99);   // the program overwrote the poke
        const bool lost = r.s->state(0) == State::NotApplied;
        const std::string d = r.s->remove(0);
        r.apply(0);           // re-apply: the byte it replaces is now 0x99
        r.s->remove(0);
        check("POK-38", "a poke the program overwrote reads Not applied and Remove refuses \"not applied\"; "
                        "Apply again records the byte it finds and Remove puts that one back",
              r.ok && lost && d == "not applied" && r.rd(0x8003) == 0x99, d + "|" + hex(r.rd(0x8003)));
    }

    // POK-39: after the undo records are dropped, memory still shows the trainer, and the file's original is used.
    {
        Rig r(MachineType::ZX48K);
        r.load("NT\nZ  8 32771 0 53\nNU\nM  8 32780 7 11\nZ  8 32780 0 99\n");
        r.wr(0x8003, 0x77);   // not the file's original (53 = 0x35)
        r.wr(0x800C, 0x66);
        r.apply(0);
        r.apply(1);
        r.s->forget_applied();
        const bool found = r.s->state(0) == State::AppliedFound && r.s->state(1) == State::AppliedFound;
        const std::string twice = r.s->apply(0, std::nullopt);
        const std::string d = r.s->remove(0);
        const std::string d1 = r.s->remove(1);
        check("POK-39", "after forget_applied() a trainer whose bytes are in memory reads \"found in memory\" "
                        "and cannot be applied again, and Remove writes the file's original value - for an "
                        "address poked twice the first poke's (WoS POKformat.txt: a non-zero original is "
                        "\"used to remove a POKE\" from a game already POKEd)",
              r.ok && found && twice == "already applied" && d.empty() && r.rd(0x8003) == 0x35 &&
                  r.s->state(0) == State::NotApplied && d1.empty() && r.rd(0x800C) == 11,
              twice + "|" + d + "|" + d1 + "|" + hex(r.rd(0x8003)) + "|" + hex(r.rd(0x800C)));
    }

    // POK-40: no original in the file, no removal.
    {
        Rig r(MachineType::ZX48K);
        r.load("NT\nZ  8 32772 33 0\n");
        r.wr(0x8004, 33);
        const bool found = r.s->state(0) == State::AppliedFound;
        const std::string d = r.s->remove(0);
        check("POK-40", "a trainer found in memory whose file gives no original value cannot be removed: the "
                        "refusal names the line and the address and the byte stays (WoS POKformat.txt: it "
                        "\"cannot handle\" a POKE whose original value was 0)",
              r.ok && found && d.find("line 2") != std::string::npos &&
                  d.find("32772") != std::string::npos && r.rd(0x8004) == 33,
              d + "|" + hex(r.rd(0x8004)));
    }

    // POK-41: value 256 asks, once, for every open poke of the trainer.
    {
        Rig r(MachineType::ZX48K);
        r.load("NLives\nM  8 36864 256 0\nM  8 36870 201 0\nZ  8 36900 256 0\n");
        r.wr(36864, 0x11);
        r.wr(36870, 0x22);
        r.wr(36900, 0x33);
        const std::string without = r.apply(0);
        const bool nothing = r.rd(36864) == 0x11 && r.rd(36870) == 0x22 && r.rd(36900) == 0x33;
        const std::string a = r.apply(0, 42);
        const bool landed = r.rd(36864) == 42 && r.rd(36870) == 201 && r.rd(36900) == 42;
        r.s->forget_applied();   // a value the user typed cannot be found again in memory
        // Nor is one found where memory happens to hold the fixed pokes and zero for the open ones.
        Rig z(MachineType::ZX48K);
        z.load("NLives\nM  8 36864 256 0\nM  8 36870 201 0\nZ  8 36900 256 0\n");
        z.wr(36864, 0);
        z.wr(36870, 201);
        z.wr(36900, 0);
        const bool not_found = z.s->state(0) == State::NotApplied &&
                               z.apply(0).find("asks for a value") != std::string::npos;
        check("POK-41", "a trainer with value-256 pokes refuses to apply without a value and writes nothing; "
                        "with 42 every value-256 poke gets 42 and the fixed poke keeps its own 201; once the "
                        "undo records are forgotten it reads Not applied, as the value is unknown, and so "
                        "does one whose open pokes happen to hold zero (WoS "
                        "POKformat.txt: 256 = ask the user; the survey: 362 trainers with 2+ such pokes)",
              r.ok && without.find("asks for a value") != std::string::npos && nothing && a.empty() &&
                  landed && r.s->state(0) == State::NotApplied && not_found,
              without + "|" + a + "|" + hex(r.rd(36864)) + hex(r.rd(36870)) + hex(r.rd(36900)));
    }

    // POK-42: the offer is what memory holds, not the file's original.
    {
        Rig r(MachineType::ZX48K);
        r.load("NLives\nM  8 36864 256 77\nZ  8 36900 256 0\nNFar\nZ  3 49443 256 0\n");
        r.wr(36864, 5);
        const std::optional<uint8_t> d = r.s->value_default(0);
        const std::string why = r.s->apply(1, std::nullopt);   // what the dialog shows instead of a prompt
        check("POK-42", "the value offered for a value-256 trainer is the byte memory holds at its first such "
                        "poke (here 5, not the file's original 77); a location that cannot be resolved (bank "
                        "3 on a 48K) offers nothing, and Apply then says why rather than asking for a value",
              r.ok && d && *d == 5 && !r.s->value_default(1).has_value() &&
                  why.find("48K") != std::string::npos,
              (d ? hex(*d) : std::string("none")) + "|" + why);
    }

    // POK-43: an SRAM page that does not respond is not RAM.
    {
        Rig r(MachineType::ZXN_ISSUE2);
        r.load("NT\nM  8 32768 1 0\nZ  8 40960 1 0\n");
        r.wr(0x8000, 0x5A);
        r.dbg->nextreg_write(1, 0x55, 0xFF);   // slot 5 (0xA000) unmapped
        const std::string refused = r.apply(0);
        check("POK-43", "a poke whose address is in a slot that no RAM answers (NR 0x55 = 0xFF, SRAM page >= "
                        "0xE0, zxnext.vhd:3061) is refused naming the address, and the trainer's other poke is "
                        "not written",
              r.ok && refused.find("40960") != std::string::npos && r.rd(0x8000) == 0x5A, refused);
    }

    // POK-44: the log line is ASCII.
    {
        Rig r(MachineType::ZX48K);
        r.load(std::string("NCaf\xE9 \xC2\xA3\nZ  8 32771 0 53\n"));
        r.wr(0x8003, 0x35);
        std::ostringstream out;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
        sink->set_pattern("%l %v");
        Log::platform()->sinks().push_back(sink);
        const std::string a = r.apply(0);
        Log::platform()->sinks().pop_back();
        const std::string text = out.str();
        const bool ascii = std::all_of(text.begin(), text.end(),
                                       [](char c) { return static_cast<unsigned char>(c) < 0x80; });
        check("POK-44", "applying a trainer logs exactly one line, and a name with bytes >= 0x80 is written "
                        "as \\xHH escapes there, so the console stays ASCII (GH #382)",
              r.ok && a.empty() && std::count(text.begin(), text.end(), '\n') == 1 &&
                  text.find("Caf\\xE9 \\xC2\\xA3") != std::string::npos && ascii,
              a + "|" + text);
    }

    // POK-45: the snapshot a user saves afterwards holds the poke.
    {
        Rig r48(MachineType::ZX48K);
        r48.load("NT\nZ  8 32771 0 53\n");
        r48.wr(0x8003, 0x35);
        const std::string a48 = r48.apply(0);
        const std::vector<uint8_t> sna48 = SnaSaver::save(*r48.emu);

        Rig r128(MachineType::ZX128K);
        r128.load("NT\nZ  3 49443 90 0\n");
        r128.bank(0);
        r128.wrp(6, 0x123, 0x11);
        const std::string a128 = r128.apply(0);
        const std::vector<uint8_t> sna128 = SnaSaver::save(*r128.emu);
        // 48K: 27-byte header, then RAM from 0x4000. 128K: header, banks 5, 2 and the paged bank 0 (49152
        // bytes), the 4-byte extended header, then banks 1, 3, 4, 6, 7: bank 3 is the second.
        const size_t at48 = 27 + (32771 - 16384);
        const size_t at128 = 27 + 49152 + 4 + 16384 * 1 + 0x123;
        check("POK-45", "a snapshot saved after a trainer holds its bytes: the 48K .sna at 27 + (32771 - 16384), "
                        "the 128K .sna at the bank-3 block (header 27 + 3 x 16384 + 4 + 16384) + 0x123 (SNA "
                        "layout)",
              r48.ok && r128.ok && a48.empty() && a128.empty() && sna48.size() == 49179 &&
                  sna48[at48] == 0x00 && sna128.size() == 131103 && sna128[at128] == 90,
              a48 + "|" + a128 + "|" + std::to_string(sna48.size()) + "|" + std::to_string(sna128.size()));
    }

    // POK-46: the running program changes.
    {
        Rig r(MachineType::ZX48K);
        r.load("NInfinite lives\nZ  8 32771 0 53\n");
        put_program(r);
        r.wr(0x9000, 200);
        run_frames(r, 10);
        const uint8_t free_run = r.rd(0x9000);
        const std::string a = r.apply(0);
        const uint8_t before = r.rd(0x9000);
        run_frames(r, 10);
        const uint8_t patched = r.rd(0x9000);
        const std::string d = r.s->remove(0);
        const uint8_t restarted = r.rd(0x9000);
        run_frames(r, 10);
        const uint8_t after = r.rd(0x9000);
        check("POK-46", "a program that counts down at 0x9000 with DEC (HL) (0x35) counts down in 10 frames, "
                        "stops counting while the trainer has NOPed that byte (0x00) and counts again once it "
                        "is removed (Z80: DEC (HL) = 0x35, NOP = 0x00)",
              r.ok && a.empty() && d.empty() && free_run < 200 && patched == before && after < restarted,
              std::to_string(free_run) + " " + std::to_string(before) + "->" + std::to_string(patched) +
                  " " + std::to_string(restarted) + "->" + std::to_string(after));
    }

    // POK-47: overlapping trainers.
    {
        Rig r(MachineType::ZX48K);
        r.load("NOne\nZ  8 32771 0 0\nNTwo\nZ  8 32771 201 0\n");
        r.wr(0x8003, 0x35);
        const std::string a1 = r.apply(0);
        const std::string a2 = r.apply(1);
        const bool one_hidden = r.s->state(0) == State::NotApplied;   // T2 overwrote it; its record is kept
        const std::string d2 = r.s->remove(1);
        const bool one_back = r.rd(0x8003) == 0 && r.s->state(0) == State::Applied;
        const std::string d1 = r.s->remove(0);
        check("POK-47", "two trainers that poke one address: while the second is applied the first reads Not "
                        "applied, and removing the second makes it Applied again (its record was kept); "
                        "removing the first then restores the original byte (the survey: 820 files with "
                        "trainers sharing an address)",
              r.ok && a1.empty() && a2.empty() && one_hidden && d2.empty() && one_back && d1.empty() &&
                  r.rd(0x8003) == 0x35,
              a1 + "|" + a2 + "|" + d2 + "|" + d1 + "|" + hex(r.rd(0x8003)));
    }

    // POK-48: the refusal texts.
    {
        Rig r(MachineType::ZX48K);
        r.load("NGood\nZ  8 32771 0 53\nNBad\nM  8 99 1 0\nZ  8 32772 1 0\n");
        r.apply(0);
        const std::string twice = r.apply(0);
        r.s->remove(0);
        const std::string none = r.s->remove(0);
        const std::string invalid = r.apply(1);
        check("POK-48", "Apply on an applied trainer says \"already applied\", Remove on one that is not applied "
                        "says \"not applied\", Apply on an invalid trainer says why it is invalid, and a "
                        "trainer number the file does not have is refused",
              r.ok && twice == "already applied" && none == "not applied" &&
                  invalid == "line 4: address 99 is outside 16384-65535" &&
                  r.apply(99) == "no such trainer" && r.s->remove(99) == "no such trainer" &&
                  r.s->state(99) == State::NotApplied && !r.s->value_default(99).has_value(),
              twice + "|" + none + "|" + invalid);

    }

    // POK-49: a slot the map shows as ROM is not RAM to poke.
    {
        Rig r(MachineType::ZX48K);
        r.load("NT\nM  8 49152 2 0\nZ  8 32768 1 0\n");
        r.wr(0xC000, 0x6B);
        r.emu->mmu().map_rom(4, 0);   // 0x8000-0x9FFF is ROM now
        const std::string refused = r.apply(0);
        check("POK-49", "a poke at an address the memory map shows as ROM is refused naming the address and the "
                        "ROM, and the trainer's other poke is not written (ROM ignores writes; a recorded "
                        "original would be a lie)",
              r.ok && refused.find("32768") != std::string::npos && refused.find("ROM") != std::string::npos &&
                  r.rd(0xC000) == 0x6B,
              refused + "|" + hex(r.rd(0xC000)));
    }

    // The next rows sweep a trainer's locations one at a time: a rule that must hold for EVERY location is
    // otherwise satisfied by checking the first only, or the last only, when the fixture breaks one place.
    const uint16_t where[3]   = {0x8003, 0x8004, 0x8005};   // 32771..32773
    const uint8_t  before[3]  = {0x35, 0x36, 0x37};         // what the test puts there first (53, 54, 55)
    const uint8_t  poked[3]   = {0x00, 0x11, 0x22};         // what the trainers below write (0, 17, 34)
    const char*    tag[3]     = {"M", "M", "Z"};

    // POK-50: a trainer reads Applied only while EVERY location it wrote still holds its poke.
    {
        std::string detail;
        bool all = true;
        for (int k = 0; k < 3; ++k) {
            Rig r(MachineType::ZX48K);
            r.load("NT\nM  8 32771 0 0\nM  8 32772 17 0\nZ  8 32773 34 0\n");
            for (int j = 0; j < 3; ++j) r.wr(where[j], before[j]);
            const std::string a = r.apply(0);
            const bool applied = a.empty() && r.s->state(0) == State::Applied;
            r.wr(where[k], 0x99);   // the program overwrote location k alone
            const bool lost = r.s->state(0) == State::NotApplied;
            const std::string d = r.s->remove(0);
            bool bytes = true;
            for (int j = 0; j < 3; ++j) bytes = bytes && r.rd(where[j]) == (j == k ? 0x99 : poked[j]);
            if (!(r.ok && applied && lost && d == "not applied" && bytes)) {
                all = false;
                detail += "[k=" + std::to_string(k) + " a=" + a + " applied=" + std::to_string(applied) +
                          " lost=" + std::to_string(lost) + " d=" + d + " bytes=" + std::to_string(bytes) + "]";
            }
        }
        check("POK-50", "a three-location trainer is Applied only while all three bytes hold their pokes: with any "
                        "ONE of them overwritten (first, middle or last) it reads Not applied, Remove says \"not "
                        "applied\" and writes nothing (plan section 3.2: Applied = a record whose every location "
                        "holds its final)",
              all, detail);
    }

    // POK-51: ... and a trainer is "found in memory" only when EVERY location holds its poke.
    {
        std::string detail;
        bool all = true;
        for (int k = 0; k < 3; ++k) {
            Rig r(MachineType::ZX48K);
            r.load("NT\nM  8 32771 0 53\nM  8 32772 17 54\nZ  8 32773 34 55\n");
            for (int j = 0; j < 3; ++j) r.wr(where[j], poked[j]);
            const bool whole = r.s->state(0) == State::AppliedFound;   // control: the fixture is a complete trainer
            r.wr(where[k], 0x99);
            const bool partial = r.s->state(0) == State::NotApplied;
            const std::string a = r.apply(0);   // so it applies, rather than "already applied"
            bool landed = a.empty() && r.s->state(0) == State::Applied;
            for (int j = 0; j < 3; ++j) landed = landed && r.rd(where[j]) == poked[j];
            if (!(r.ok && whole && partial && landed)) {
                all = false;
                detail += "[k=" + std::to_string(k) + " whole=" + std::to_string(whole) + " partial=" +
                          std::to_string(partial) + " a=" + a + "]";
            }
        }
        check("POK-51", "memory that holds all three bytes of a trainer reads \"found in memory\"; with any ONE of "
                        "them different it reads Not applied and Apply goes ahead (plan section 3.2: AppliedFound "
                        "= every location holds its final)",
              all, detail);
    }

    // POK-52: ... and a trainer found in memory is removable only if EVERY poke gives its original.
    {
        std::string detail;
        bool all = true;
        const char* orig_text[3] = {"53", "54", "55"};
        const char* poke_text[3] = {"0", "17", "34"};
        for (int k = -1; k < 3; ++k) {   // k = the poke whose original the file leaves out; -1 = none (control)
            std::string text = "NT\n";
            for (int j = 0; j < 3; ++j)
                text += std::string(tag[j]) + "  8 " + std::to_string(32771 + j) + " " + poke_text[j] + " " +
                        (j == k ? "0" : orig_text[j]) + "\n";
            Rig r(MachineType::ZX48K);
            r.load(text);
            for (int j = 0; j < 3; ++j) r.wr(where[j], poked[j]);
            const bool found = r.s->state(0) == State::AppliedFound;
            const std::string d = r.s->remove(0);
            bool ok;
            if (k < 0) {
                ok = d.empty();
                for (int j = 0; j < 3; ++j) ok = ok && r.rd(where[j]) == before[j];
            } else {
                ok = d.find("line " + std::to_string(k + 2)) != std::string::npos &&
                     d.find(std::to_string(32771 + k)) != std::string::npos &&
                     r.s->state(0) == State::AppliedFound;
                for (int j = 0; j < 3; ++j) ok = ok && r.rd(where[j]) == poked[j];
            }
            if (!(r.ok && found && ok)) {
                all = false;
                detail += "[k=" + std::to_string(k) + " found=" + std::to_string(found) + " d=" + d + "]";
            }
        }
        check("POK-52", "removing a trainer found in memory is refused when ANY of its pokes (first, middle or "
                        "last) gives no original value: the refusal names that line and address and nothing is "
                        "written; with every original given it writes them all (WoS POKformat.txt: a non-zero "
                        "original is \"used to remove a POKE\"; one that is 0 \"cannot be handled\")",
              all, detail);
    }

    // POK-53: a trainer with a bad line is never applied, even though the pokes that did parse are in memory.
    {
        Rig r(MachineType::ZX48K);
        r.load("NBad\nM  8 99 1 0\nZ  8 32772 1 5\n");   // line 2 is out of range; line 3 parsed, with an original
        r.wr(0x8004, 1);
        const Trainer& t = r.s->file().trainers[0];
        const bool fixture = !t.problem.empty() && t.pokes.size() == 1 && t.pokes[0].original == 5;
        const State st = r.s->state(0);
        const std::string d = r.s->remove(0);
        const std::string a = r.apply(0);
        check("POK-53", "a trainer that is invalid reads Not applied although the one poke that parsed is in memory "
                        "and gives an original: Remove says \"not applied\", Apply says why it is invalid, and "
                        "the byte is not touched (plan section 3.2: an invalid trainer is never applied)",
              r.ok && fixture && st == State::NotApplied && d == "not applied" && a == t.problem &&
                  r.rd(0x8004) == 1,
              d + "|" + a + "|" + hex(r.rd(0x8004)));
    }

    // POK-54: Apply again after the program overwrote PART of a trainer (the only way forward: the row reads Not
    // applied and Remove is refused) must not take the bytes it poked itself for the originals.
    {
        // Three places, the program rewriting one of them (the first, the middle, the last in turn): the other
        // two are still our pokes.
        std::string detail;
        bool pair = true;
        for (int k = 0; k < 3; ++k) {
            Rig r(MachineType::ZX48K);
            r.load("NT\nM  8 32771 0 0\nM  8 32772 17 0\nZ  8 32773 34 0\n");
            for (int j = 0; j < 3; ++j) r.wr(where[j], before[j]);
            const std::string a1 = r.apply(0);
            r.wr(where[k], 0x99);
            const bool lost = r.s->state(0) == State::NotApplied;
            const std::string a2 = r.apply(0);
            const std::string d = r.s->remove(0);
            bool bytes = true;
            for (int j = 0; j < 3; ++j) bytes = bytes && r.rd(where[j]) == (j == k ? 0x99 : before[j]);
            if (!(r.ok && a1.empty() && lost && a2.empty() && d.empty() && bytes)) {
                pair = false;
                detail += "[k=" + std::to_string(k) + " " + a1 + "|" + a2 + "|" + d + "|" + hex(r.rd(where[0])) +
                          hex(r.rd(where[1])) + hex(r.rd(where[2])) + "]";
            }
        }

        // An infinite-lives style trainer: a code patch and a lives counter the user chooses (value 256). The game
        // loses a life; the dialog offers the byte memory holds, and the user accepts it.
        Rig v(MachineType::ZX48K);
        v.load("NLives\nM  8 32771 0 0\nZ  8 36864 256 0\n");
        v.wr(0x8003, 0x35);
        v.wr(36864, 0x11);
        const std::string b1 = v.apply(0, 5);
        v.wr(36864, 4);
        const std::optional<uint8_t> offer = v.s->value_default(0);
        const bool lost_v = v.s->state(0) == State::NotApplied;
        const std::string b2 = v.apply(0, offer);
        const std::string e = v.s->remove(0);
        const bool lives = b1.empty() && offer && *offer == 4 && lost_v && b2.empty() && e.empty() &&
                           v.rd(0x8003) == 0x35 && v.rd(36864) == 4;
        check("POK-54", "after the program overwrote one byte of a trainer (any of three), Apply again and Remove "
                        "put each byte still poked back to what it was BEFORE the first Apply and leave the byte "
                        "the program wrote as the program has it (0x99); the same for a code patch plus a lives "
                        "counter, re-applied with the value the dialog offered (FEATURES / user guide 5.11: "
                        "Remove restores the bytes replaced)",
              v.ok && pair && lives,
              detail + "|" + b1 + "|" + b2 + "|" + e + "|" + hex(v.rd(0x8003)) + hex(v.rd(36864)));
    }

    // POK-55: ... the older record is matched to the new one by LOCATION, so a byte at another place that merely
    // holds the same value is the program's.
    {
        // Three places in one page whose bytes the program rotated: each holds ANOTHER place's poke.
        Rig r(MachineType::ZX48K);
        r.load("NR\nM  8 32771 1 0\nM  8 32772 2 0\nZ  8 32773 3 0\n");
        for (int j = 0; j < 3; ++j) r.wr(where[j], before[j]);
        const std::string a1 = r.apply(0);
        r.wr(where[0], 2);
        r.wr(where[1], 3);
        r.wr(where[2], 1);
        const std::string a2 = r.apply(0);
        const std::string d = r.s->remove(0);
        const bool rotated = a1.empty() && a2.empty() && d.empty() && r.rd(where[0]) == 2 &&
                             r.rd(where[1]) == 3 && r.rd(where[2]) == 1;

        // The map moved between the two Applies: a bank-8 poke at 0xC123 follows what is paged in, and the other
        // bank holds, by chance, the byte the first Apply wrote.
        Rig m(MachineType::ZX128K);
        m.load("NMoved\nM  8 49443 66 0\nZ  8 32771 0 0\n");
        m.bank(1);
        m.wrp(2, 0x123, 0x21);   // bank 1 low half = 0xC123 while bank 1 is paged
        m.wr(0x8003, 0x35);
        const std::string b1 = m.apply(0);
        m.wr(0x8003, 0x99);      // the program overwrote the code byte
        m.bank(4);
        m.wrp(8, 0x123, 66);     // bank 4 low half: the value the first Apply wrote into bank 1
        const bool lost = m.s->state(0) == State::NotApplied;
        const std::string b2 = m.apply(0);
        const std::string e = m.s->remove(0);
        const bool moved = b1.empty() && lost && b2.empty() && e.empty() && m.rdp(8, 0x123) == 66 &&
                           m.rd(0x8003) == 0x99;
        check("POK-55", "the record an Apply replaces is matched to the new one by place: three places whose "
                        "bytes were rotated (each holds another's poke) come back as the program had them, and "
                        "after the memory map moved a byte of the new place that equals the old poke is the "
                        "program's and is left alone (plan section 3.2; user guide 5.11)",
              r.ok && m.ok && rotated && moved,
              a1 + "|" + a2 + "|" + d + "|" + hex(r.rd(where[0])) + hex(r.rd(where[1])) + hex(r.rd(where[2])) +
                  "|" + b1 + "|" + b2 + "|" + e + "|" + hex(m.rdp(8, 0x123)) + hex(m.rd(0x8003)));
    }

    // POK-56: Remove logs too, one ASCII line, by either route (a recorded undo, or a trainer found in memory).
    {
        auto remove_logged = [](Rig& r, std::string& d) {
            std::ostringstream out;
            auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
            sink->set_pattern("%l %v");
            Log::platform()->sinks().push_back(sink);
            d = r.s->remove(0);
            Log::platform()->sinks().pop_back();
            return out.str();
        };
        const std::string file = std::string("NCaf\xE9 \xC2\xA3\nZ  8 32771 0 53\n");
        Rig r(MachineType::ZX48K);
        r.load(file);
        r.wr(0x8003, 0x35);
        r.apply(0);
        std::string d1, d2;
        const std::string recorded = remove_logged(r, d1);
        Rig f(MachineType::ZX48K);
        f.load(file);
        f.wr(0x8003, 0x00);   // memory holds the poke, and the session has no record of putting it there
        const bool found = f.s->state(0) == State::AppliedFound;
        const std::string from_file = remove_logged(f, d2);
        auto one_line = [](const std::string& text) {
            return std::count(text.begin(), text.end(), '\n') == 1 &&
                   text.find("removed") != std::string::npos &&
                   text.find("Caf\\xE9 \\xC2\\xA3") != std::string::npos &&
                   std::all_of(text.begin(), text.end(),
                               [](char c) { return static_cast<unsigned char>(c) < 0x80; });
        };
        check("POK-56", "removing a trainer logs exactly one line saying it was removed, with the name escaped as "
                        "\\xHH, both when the session recorded the undo and when the trainer was found in memory "
                        "and the file's original is used (plan section 3.2: remove logs; GH #382 for the ASCII)",
              r.ok && f.ok && found && d1.empty() && d2.empty() && one_line(recorded) && one_line(from_file),
              recorded + "|" + from_file);
    }

    // POK-62: run_cli_request applies the picks in the order given, with their values, and leaves a trainer
    // that memory already holds (GH #381, owner Q1).
    {
        auto request = [](const std::string& text, std::vector<Pick> picks) {
            CliRequest req;
            req.path = "test.pok";
            req.file = parse_pok(text);
            req.picks = std::move(picks);
            return req;
        };
        auto pick = [](size_t n, int v = -1) {
            Pick p;
            p.number = n;
            if (v >= 0) p.value = static_cast<uint8_t>(v);
            return p;
        };
        // Two trainers that poke one byte differently: the one applied LAST is what stays.
        const std::string two = "NA\nZ  8 32780 1 0\nNB\nZ  8 32780 2 0\n";
        Rig r12(MachineType::ZX48K), r21(MachineType::ZX48K);
        const std::string o12 = run_cli_request(*r12.dbg, request(two, {pick(1), pick(2)}));
        const std::string o21 = run_cli_request(*r21.dbg, request(two, {pick(2), pick(1)}));
        const bool order = r12.ok && r21.ok && o12.empty() && o21.empty() && r12.rd(0x800C) == 2 && r21.rd(0x800C) == 1;

        // A value goes to every open poke of its trainer; a fixed poke keeps its own value.
        Rig v(MachineType::ZX48K);
        v.wr(0x8003, 0x35);
        const std::string ov = run_cli_request(*v.dbg, request("NFix\nZ  8 32771 0 53\nNLives\nM  8 36864 256 0\n"
                                                              "M  8 36870 201 0\nZ  8 36900 256 0\n",
                                                              {pick(1), pick(2, 42)}));
        const bool values = v.ok && ov.empty() && v.rd(0x8003) == 0 && v.rd(36864) == 42 && v.rd(36870) == 201 &&
                            v.rd(36900) == 42;

        // Run it again on the same machine: the fixed trainer's bytes are already there and it is left as it is
        // (said once on the log); the value trainer is written again. The run still succeeds.
        std::ostringstream out;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
        sink->set_pattern("%l %v");
        Log::platform()->sinks().push_back(sink);
        const std::string again = run_cli_request(*v.dbg, request("NFix\nZ  8 32771 0 53\nNLives\nM  8 36864 256 0\n"
                                                                  "M  8 36870 201 0\nZ  8 36900 256 0\n",
                                                                  {pick(1), pick(2, 7)}));
        Log::platform()->sinks().pop_back();
        const std::string log = out.str();
        const bool found = again.empty() && v.rd(0x8003) == 0 && v.rd(36864) == 7 && v.rd(36900) == 7 &&
                           log.find("trainer 1 ('Fix') is already in memory") != std::string::npos &&
                           log.find("applied 'Lives'") != std::string::npos &&
                           log.find("applied 'Fix'") == std::string::npos;
        check("POK-62", "run_cli_request applies the picks in the order given (two trainers poking one byte leave "
                        "the last one's value, either way round), gives a trainer's value to every open poke of it, "
                        "and a trainer whose bytes memory already holds is logged as in memory and left, the run "
                        "still succeeding (jnext's own request rules; owner decision Q1)",
              order && values && found, o12 + "|" + o21 + "|" + ov + "|" + again + "|" + log);
    }

    // POK-63: ... and stops at the first refusal, naming it; the ones before it stay applied.
    {
        auto request = [](const std::string& text, std::vector<size_t> numbers) {
            CliRequest req;
            req.path = "test.pok";
            req.file = parse_pok(text);
            for (size_t n : numbers) {
                Pick p;
                p.number = n;
                req.picks.push_back(p);
            }
            return req;
        };
        Rig r(MachineType::ZX48K);
        r.wr(0x8003, 0x5A);
        r.wr(0x800C, 0x6B);
        const std::string why = run_cli_request(*r.dbg, request("NA\nZ  8 32771 9 0\nNB bank\nZ  3 49443 1 0\n"
                                                                "NC\nZ  8 32780 5 0\n", {1, 2, 3}));
        const bool stops = r.ok && why.find("trainer 2 ('B bank')") != std::string::npos &&
                           why.find("line 4") != std::string::npos && why.find("48K") != std::string::npos &&
                           r.rd(0x8003) == 9 && r.rd(0x800C) == 0x6B;

        // Under an RZX the backend refuses every poke, and the run says so.
        Rig z(MachineType::ZX48K);
        z.wr(0x8003, 0x5A);
        z.emu->rzx_player().start(RzxRecording{});
        const std::string rzx = run_cli_request(*z.dbg, request("NA\nZ  8 32771 9 0\n", {1}));
        z.emu->rzx_player().stop();
        const bool refused = rzx.find("trainer 1 ('A')") != std::string::npos && rzx.find("RZX") != std::string::npos &&
                             z.rd(0x8003) == 0x5A;
        check("POK-63", "run_cli_request stops at the first trainer that cannot be applied and returns the reason "
                        "with the trainer's number and name (a bank-3 line on a 48K, an RZX that plays), the trainer "
                        "before it stays applied and the one after it is not written (jnext's own request rules; "
                        "owner decision Q1)",
              stops && refused, why + "|" + rzx + "|" + hex(r.rd(0x8003)) + hex(r.rd(0x800C)));
    }

    // POK-64: CliJob counts the frames: the request is applied by the poll after N have run, once (GH #381).
    {
        auto job_for = [](Rig& r, const std::string& text, int frames, std::vector<size_t> numbers) {
            CliRequest req;
            req.path = "test.pok";
            req.file = parse_pok(text);
            for (size_t n : numbers) {
                Pick p;
                p.number = n;
                req.picks.push_back(p);
            }
            (void)r;
            return CliJob(std::move(req), frames);
        };
        Rig r(MachineType::ZX48K);
        r.wr(0x8003, 0x5A);
        CliJob j3 = job_for(r, "NA\nZ  8 32771 9 0\n", 3, {1});
        bool early = j3.pending();
        for (int k = 1; k <= 3; ++k) {
            early = early && j3.poll(*r.dbg, false) && j3.pending() && r.rd(0x8003) == 0x5A;
            (void)k;
        }
        const bool fires = j3.poll(*r.dbg, false) && !j3.pending() && r.rd(0x8003) == 9;
        r.wr(0x8003, 0x99);   // the program rewrites it; a spent job must not poke it back
        const bool once = j3.poll(*r.dbg, false) && j3.poll(*r.dbg, true) && !j3.pending() && r.rd(0x8003) == 0x99;

        // 0 frames: the first poll. At 60 Hz the frame count is the same.
        Rig z(MachineType::ZX48K);
        z.wr(0x8003, 0x5A);
        CliJob j0 = job_for(z, "NA\nZ  8 32771 9 0\n", 0, {1});
        const bool now = j0.pending() && j0.poll(*z.dbg, true) && !j0.pending() && z.rd(0x8003) == 9;
        Rig h(MachineType::ZX48K);
        h.wr(0x8003, 0x5A);
        CliJob j2 = job_for(h, "NA\nZ  8 32771 9 0\n", 2, {1});
        const bool sixty = j2.poll(*h.dbg, true) && j2.poll(*h.dbg, true) && h.rd(0x8003) == 0x5A &&
                           j2.poll(*h.dbg, true) && h.rd(0x8003) == 9;

        // A refusal comes back from the poll that applies it (false), and disarms the job.
        Rig f(MachineType::ZX48K);
        CliJob jf = job_for(f, "NA\nZ  3 49443 1 0\n", 1, {1});
        const bool early_ok = jf.poll(*f.dbg, false);
        const bool refused = !jf.poll(*f.dbg, false) && !jf.pending() && jf.poll(*f.dbg, false);
        check("POK-64", "a CliJob applies its request on the poll after N frames have counted (N = 3: not on polls "
                        "1-3, on the 4th; N = 0: the first; the refresh rate does not matter for frames), once - a "
                        "later poll does not poke a rewritten byte again - and a poll that applies a request that "
                        "is refused returns false, after which the job is spent (the loop owners poll once per tick "
                        "before the frame; owner decision Q1)",
              r.ok && z.ok && h.ok && f.ok && early && fires && once && now && sixty && early_ok && refused,
              hex(r.rd(0x8003)) + hex(z.rd(0x8003)) + hex(h.rd(0x8003)));
    }
}

}  // namespace

int main() {
    std::printf("POK trainer files (GH #381)\n\n");
    test_parser();
    test_load();
    test_session();
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
