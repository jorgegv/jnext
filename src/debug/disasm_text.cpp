#include "debug/disasm_text.h"

#include "debug/symbol_table.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace disasm_text {

namespace {

/// Width of the opcode-bytes column in WithAddresses: four bytes as "XX" with
/// single spaces between them.
constexpr int BYTES_FIELD = 4 * 3 - 1;   // 11

} // namespace

uint16_t extract_immediate16(const char* mnemonic)
{
    // Scan for a $XXXX pattern (exactly 4 hex digits after $).
    for (const char* p = mnemonic; *p; ++p) {
        if (*p != '$') continue;
        const char* start = p + 1;
        int digits = 0;
        while (start[digits] && std::isxdigit(static_cast<unsigned char>(start[digits])))
            ++digits;
        if (digits == 4) {
            char buf[5] = {};
            std::memcpy(buf, start, 4);
            return static_cast<uint16_t>(std::strtoul(buf, nullptr, 16));
        }
    }
    return 0;
}

std::string apply_symbols(const char* mnemonic, const SymbolTable* symbols)
{
    std::string out(mnemonic);
    if (!symbols) return out;

    const uint16_t imm = extract_immediate16(mnemonic);
    // extract_immediate16() returns 0 both for "no immediate" and for a real
    // $0000, so an explicit "$0000" in the text is what tells the two apart.
    if (imm == 0 && !std::strstr(mnemonic, "$0000")) return out;

    const auto sym = symbols->lookup(imm);
    if (!sym) return out;

    char target[8];
    std::snprintf(target, sizeof(target), "$%04X", imm);

    // Replace every occurrence, matching QString::replace() — which is what
    // the painter used before this rule was factored out.
    const std::string needle(target);
    std::string::size_type pos = 0;
    while ((pos = out.find(needle, pos)) != std::string::npos) {
        out.replace(pos, needle.size(), *sym);
        pos += sym->size();
    }
    return out;
}

std::vector<CopyLine> collect_range(uint16_t start, uint16_t end,
                                    const DisasmReadFn& read_fn,
                                    const SymbolTable* symbols)
{
    std::vector<CopyLine> out;
    if (end < start) return out;

    uint32_t cur = start;
    // The address space is 64K and every instruction is at least one byte, so
    // this bound can only be reached by a pathological read_fn.
    for (int guard = 0; guard <= 0x10000; ++guard) {
        if (cur > end) break;

        const DisasmLine line = disasm_one(static_cast<uint16_t>(cur), read_fn);

        CopyLine cl;
        cl.addr       = line.addr;
        cl.byte_count = line.byte_count;
        for (int b = 0; b < line.byte_count && b < 4; ++b)
            cl.bytes[b] = line.bytes[b];
        cl.mnemonic = apply_symbols(line.mnemonic, symbols);
        out.push_back(std::move(cl));

        const int len = (line.byte_count > 0) ? line.byte_count : 1;
        cur += static_cast<uint32_t>(len);
        if (cur > 0xFFFF) break;   // wrapped past the top of the address space
    }
    return out;
}

std::string format_lines(const std::vector<CopyLine>& lines, CopyFormat fmt)
{
    std::string out;
    for (const auto& l : lines) {
        if (fmt == CopyFormat::AsmOnly) {
            out += ASM_INDENT;
            out += l.mnemonic;
        } else {
            char addr[8];
            std::snprintf(addr, sizeof(addr), "$%04X", l.addr);
            out += addr;
            out += "  ";

            std::string bytes;
            for (int b = 0; b < l.byte_count && b < 4; ++b) {
                if (b > 0) bytes += ' ';
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02X", l.bytes[b]);
                bytes += hex;
            }
            bytes.resize(BYTES_FIELD, ' ');
            out += bytes;
            out += "  ";
            out += l.mnemonic;
        }
        out += '\n';
    }
    return out;
}

} // namespace disasm_text
