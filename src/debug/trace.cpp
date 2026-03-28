#include "debug/trace.h"

#include <cstdio>
#include <fstream>

// ---------------------------------------------------------------------------
// z80_instruction_length — returns the byte length of the instruction at addr
// ---------------------------------------------------------------------------

int z80_instruction_length(uint16_t addr, std::function<uint8_t(uint16_t)> read)
{
    uint8_t op = read(addr);

    // CB prefix: all CB xx instructions are 2 bytes
    if (op == 0xCB) return 2;

    // ED prefix
    if (op == 0xED) {
        uint8_t op2 = read(addr + 1);
        // Z80N extended opcodes and standard ED opcodes
        // Most ED xx are 2 bytes; a few take an immediate word (3 or 4 bytes)
        switch (op2) {
            // Standard 4-byte ED instructions: LD (nn),rr / LD rr,(nn)
            case 0x43: case 0x4B: case 0x53: case 0x5B:
            case 0x63: case 0x6B: case 0x73: case 0x7B:
                return 4;
            // Z80N: NEXTREG nn,nn (ED 91 nn nn) = 4 bytes
            case 0x91:
                return 4;
            // Z80N: NEXTREG nn,A (ED 92 nn) = 3 bytes
            case 0x92:
                return 3;
            // Z80N: TEST nn (ED 27 nn) = 3 bytes
            case 0x27:
                return 3;
            // Z80N: ADD rr,nn (ED 34/35/36 nn nn) = 4 bytes
            case 0x34: case 0x35: case 0x36:
                return 4;
            // Z80N: PUSH nn (ED 8A nn nn) = 4 bytes
            case 0x8A:
                return 4;
            default:
                return 2;
        }
    }

    // DD/FD prefix (IX/IY)
    if (op == 0xDD || op == 0xFD) {
        uint8_t op2 = read(addr + 1);
        if (op2 == 0xCB) {
            // DD CB dd oo / FD CB dd oo — always 4 bytes
            return 4;
        }
        // DD/FD prefix instructions: same length as unprefixed + 1
        // Recursively get length of the unprefixed instruction and add 1,
        // but only if it's actually an indexable instruction.
        // Simpler approach: categorize by op2.
        switch (op2) {
            // 1-byte operand instructions become 3 bytes with prefix
            // LD r,(IX+d) / LD (IX+d),r / ADD IX,rr etc.
            case 0x09: case 0x19: case 0x29: case 0x39: // ADD IX,rr
            case 0x23: case 0x2B: // INC/DEC IX
            case 0xE1: case 0xE5: // POP/PUSH IX
            case 0xE3: // EX (SP),IX
            case 0xE9: // JP (IX)
            case 0xF9: // LD SP,IX
            // Undocumented single-byte ops on IXH/IXL
            case 0x44: case 0x45: case 0x4C: case 0x4D:
            case 0x54: case 0x55: case 0x5C: case 0x5D:
            case 0x60: case 0x61: case 0x62: case 0x63:
            case 0x64: case 0x65: case 0x67:
            case 0x68: case 0x69: case 0x6A: case 0x6B:
            case 0x6C: case 0x6D: case 0x6F:
            case 0x7C: case 0x7D:
            case 0x84: case 0x85: case 0x8C: case 0x8D:
            case 0x94: case 0x95: case 0x9C: case 0x9D:
            case 0xA4: case 0xA5: case 0xAC: case 0xAD:
            case 0xB4: case 0xB5: case 0xBC: case 0xBD:
            case 0x24: case 0x25: case 0x2C: case 0x2D:
                return 2;
            // LD IXH/IXL,n — 3 bytes
            case 0x26: case 0x2E:
                return 3;
            // LD IX,nn / LD (nn),IX / LD IX,(nn)
            case 0x21:
            case 0x22: case 0x2A:
                return 4;
            // (IX+d) operations — 3 bytes
            case 0x34: case 0x35: // INC/DEC (IX+d)
            case 0x46: case 0x4E: case 0x56: case 0x5E:
            case 0x66: case 0x6E: case 0x7E: // LD r,(IX+d)
            case 0x70: case 0x71: case 0x72: case 0x73:
            case 0x74: case 0x75: case 0x77: // LD (IX+d),r
            case 0x86: case 0x8E: case 0x96: case 0x9E:
            case 0xA6: case 0xAE: case 0xB6: case 0xBE: // ALU (IX+d)
                return 3;
            // LD (IX+d),n — 4 bytes
            case 0x36:
                return 4;
            default:
                // Unrecognized: treat prefix as NOP, instruction is 2 bytes
                return 2;
        }
    }

    // Unprefixed instructions
    switch (op) {
        // 1-byte instructions
        case 0x00: // NOP
        case 0x02: case 0x0A: case 0x12: case 0x1A: // LD (BC/DE),A / LD A,(BC/DE)
        case 0x03: case 0x04: case 0x05: case 0x07: // INC/DEC/RLCA
        case 0x08: // EX AF,AF'
        case 0x09: case 0x0B: case 0x0C: case 0x0D: case 0x0F:
        case 0x13: case 0x14: case 0x15: case 0x17:
        case 0x19: case 0x1B: case 0x1C: case 0x1D: case 0x1F:
        case 0x23: case 0x24: case 0x25: case 0x27:
        case 0x29: case 0x2B: case 0x2C: case 0x2D: case 0x2F:
        case 0x2A: // LD HL,(nn) is 3 bytes — handled below; 0x2A is NOT 1 byte
            break; // fall through to default
        case 0x33: case 0x34: case 0x35: case 0x37:
        case 0x39: case 0x3B: case 0x3C: case 0x3D: case 0x3F:
        case 0x76: // HALT
        case 0xC9: // RET
        case 0xD9: // EXX
        case 0xE3: // EX (SP),HL
        case 0xE9: // JP (HL)
        case 0xEB: // EX DE,HL
        case 0xF3: // DI
        case 0xF9: // LD SP,HL
        case 0xFB: // EI
            return 1;
        default:
            break;
    }

    // Handle all register-to-register moves and ALU r instructions (1 byte)
    if ((op >= 0x40 && op <= 0x7F && op != 0x76) || // LD r,r (excluding HALT)
        (op >= 0x80 && op <= 0xBF)) {                // ALU r
        return 1;
    }

    // RET cc (1 byte)
    if ((op & 0xC7) == 0xC0) return 1;
    // RST (1 byte)
    if ((op & 0xC7) == 0xC7) return 1;
    // PUSH/POP (1 byte)
    if ((op & 0xCF) == 0xC1 || (op & 0xCF) == 0xC5) return 1;

    // 2-byte instructions (immediate byte)
    switch (op) {
        case 0x06: case 0x0E: case 0x16: case 0x1E: // LD r,n
        case 0x26: case 0x2E: case 0x36: case 0x3E:
        case 0xC6: case 0xCE: case 0xD6: case 0xDE: // ALU n
        case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        case 0xD3: case 0xDB: // OUT (n),A / IN A,(n)
        case 0x10: // DJNZ
        case 0x18: // JR
        case 0x20: case 0x28: case 0x30: case 0x38: // JR cc
            return 2;
    }

    // 3-byte instructions (immediate word)
    switch (op) {
        case 0x01: case 0x11: case 0x21: case 0x31: // LD rr,nn
        case 0x22: case 0x2A: // LD (nn),HL / LD HL,(nn)
        case 0x32: case 0x3A: // LD (nn),A / LD A,(nn)
        case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA: // JP cc,nn / JP nn
        case 0xE2: case 0xEA: case 0xF2: case 0xFA:
        case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC: // CALL cc,nn / CALL nn
        case 0xE4: case 0xEC: case 0xF4: case 0xFC:
            return 3;
    }

    // Default: assume 1 byte for anything we missed
    return 1;
}

// ---------------------------------------------------------------------------
// TraceLog implementation
// ---------------------------------------------------------------------------

TraceLog::TraceLog(size_t capacity)
    : buffer_(capacity), capacity_(capacity)
{
}

void TraceLog::set_enabled(bool e)
{
    enabled_ = e;
}

bool TraceLog::enabled() const
{
    return enabled_;
}

void TraceLog::record(const TraceEntry& entry)
{
    buffer_[head_] = entry;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_)
        ++count_;
}

void TraceLog::clear()
{
    head_ = 0;
    count_ = 0;
}

size_t TraceLog::size() const
{
    return count_;
}

const TraceEntry& TraceLog::at(size_t index) const
{
    // Map logical index (0=oldest) to physical position in circular buffer.
    size_t physical;
    if (count_ < capacity_) {
        // Buffer not yet full: entries start at 0.
        physical = index;
    } else {
        // Buffer full: oldest entry is at head_.
        physical = (head_ + index) % capacity_;
    }
    return buffer_[physical];
}

bool TraceLog::export_to_file(const std::string& path) const
{
    std::ofstream ofs(path);
    if (!ofs.is_open())
        return false;

    char line[256];
    for (size_t i = 0; i < count_; ++i) {
        const TraceEntry& e = at(i);

        // Format opcode bytes as hex
        char bytes_str[16] = {};
        int pos = 0;
        for (int b = 0; b < e.opcode_len && b < 4; ++b) {
            pos += std::snprintf(bytes_str + pos, sizeof(bytes_str) - pos,
                                  "%s%02X", (b > 0 ? " " : ""), e.opcode_bytes[b]);
        }

        // Decode flags from F register (low byte of AF)
        uint8_t f = static_cast<uint8_t>(e.af & 0xFF);
        char flags[9];
        flags[0] = (f & 0x80) ? 'S' : '-';
        flags[1] = (f & 0x40) ? 'Z' : '-';
        flags[2] = (f & 0x20) ? '5' : '-';
        flags[3] = (f & 0x10) ? 'H' : '-';
        flags[4] = (f & 0x08) ? '3' : '-';
        flags[5] = (f & 0x04) ? 'P' : '-';
        flags[6] = (f & 0x02) ? 'N' : '-';
        flags[7] = (f & 0x01) ? 'C' : '-';
        flags[8] = '\0';

        std::snprintf(line, sizeof(line),
            "%012llu  $%04X  AF=%04X BC=%04X DE=%04X HL=%04X"
            "  AF'=%04X BC'=%04X DE'=%04X HL'=%04X"
            "  IX=%04X IY=%04X SP=%04X  [%s]  %s\n",
            static_cast<unsigned long long>(e.cycle),
            e.pc, e.af, e.bc, e.de, e.hl,
            e.af2, e.bc2, e.de2, e.hl2,
            e.ix, e.iy, e.sp, flags, bytes_str);

        ofs << line;
    }

    return ofs.good();
}
