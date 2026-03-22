#include "disasm.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static DisasmLine disasm_cb(uint16_t addr, DisasmReadFn const& read_fn);
static DisasmLine disasm_ed(uint16_t addr, DisasmReadFn const& read_fn);
static DisasmLine disasm_ddfd(uint16_t addr, DisasmReadFn const& read_fn, uint8_t prefix);

// ---------------------------------------------------------------------------
// Helper: read a 16-bit little-endian word
// ---------------------------------------------------------------------------
static uint16_t read16(uint16_t addr, DisasmReadFn const& read_fn) {
    uint8_t lo = read_fn(addr);
    uint8_t hi = read_fn(static_cast<uint16_t>(addr + 1));
    return static_cast<uint16_t>(lo | (hi << 8));
}

// ---------------------------------------------------------------------------
// Condition code names (index by bits 5-3 of opcode)
// ---------------------------------------------------------------------------
static const char* const cc_names[8] = {
    "NZ", "Z", "NC", "C", "PO", "PE", "P", "M"
};

// ---------------------------------------------------------------------------
// 8-bit register names (index by bits 2-0 or 5-3 of opcode)
// ---------------------------------------------------------------------------
static const char* const r_names[8] = {
    "B", "C", "D", "E", "H", "L", "(HL)", "A"
};

// ---------------------------------------------------------------------------
// 16-bit register pair names (index by bits 5-4 of opcode)
// ---------------------------------------------------------------------------
static const char* const rp_names[4] = {
    "BC", "DE", "HL", "SP"
};

// ---------------------------------------------------------------------------
// 16-bit register pair names for PUSH/POP (index by bits 5-4)
// ---------------------------------------------------------------------------
static const char* const rp2_names[4] = {
    "BC", "DE", "HL", "AF"
};

// ---------------------------------------------------------------------------
// ALU operation names (index by bits 5-3)
// ---------------------------------------------------------------------------
static const char* const alu_names[8] = {
    "ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "
};

// ---------------------------------------------------------------------------
// CB-prefix rotation/shift operation names (index by bits 5-3)
// ---------------------------------------------------------------------------
static const char* const rot_names[8] = {
    "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"
};

// ---------------------------------------------------------------------------
// Build a DisasmLine result, collecting raw bytes
// ---------------------------------------------------------------------------
static DisasmLine make_line(uint16_t addr, int byte_count,
                            DisasmReadFn const& read_fn,
                            const char* mnemonic) {
    DisasmLine line;
    line.addr = addr;
    line.byte_count = byte_count;
    std::memset(line.bytes, 0, sizeof(line.bytes));
    for (int i = 0; i < byte_count && i < 4; i++) {
        line.bytes[i] = read_fn(static_cast<uint16_t>(addr + i));
    }
    std::strncpy(line.mnemonic, mnemonic, sizeof(line.mnemonic) - 1);
    line.mnemonic[sizeof(line.mnemonic) - 1] = '\0';
    return line;
}

// ---------------------------------------------------------------------------
// Main unprefixed opcode disassembly
// ---------------------------------------------------------------------------
DisasmLine disasm_one(uint16_t addr, DisasmReadFn read_fn) {
    uint8_t op = read_fn(addr);
    char buf[64];

    // Check for prefix bytes first
    if (op == 0xCB) return disasm_cb(addr, read_fn);
    if (op == 0xED) return disasm_ed(addr, read_fn);
    if (op == 0xDD || op == 0xFD) return disasm_ddfd(addr, read_fn, op);

    // Decode using the x/y/z/p/q decomposition from Sean Young's Z80 undocumented doc
    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    switch (x) {
    case 0:
        switch (z) {
        case 0:
            switch (y) {
            case 0: return make_line(addr, 1, read_fn, "NOP");
            case 1: return make_line(addr, 1, read_fn, "EX AF,AF'");
            case 2: { // DJNZ d
                int8_t d = static_cast<int8_t>(read_fn(static_cast<uint16_t>(addr + 1)));
                uint16_t target = static_cast<uint16_t>(addr + 2 + d);
                std::sprintf(buf, "DJNZ $%04X", target);
                return make_line(addr, 2, read_fn, buf);
            }
            case 3: { // JR d
                int8_t d = static_cast<int8_t>(read_fn(static_cast<uint16_t>(addr + 1)));
                uint16_t target = static_cast<uint16_t>(addr + 2 + d);
                std::sprintf(buf, "JR $%04X", target);
                return make_line(addr, 2, read_fn, buf);
            }
            default: { // JR cc,d (y=4..7 → cc=y-4)
                int8_t d = static_cast<int8_t>(read_fn(static_cast<uint16_t>(addr + 1)));
                uint16_t target = static_cast<uint16_t>(addr + 2 + d);
                std::sprintf(buf, "JR %s,$%04X", cc_names[y - 4], target);
                return make_line(addr, 2, read_fn, buf);
            }
            }
            break;
        case 1:
            if (q == 0) {
                // LD rp,nn
                uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
                std::sprintf(buf, "LD %s,$%04X", rp_names[p], nn);
                return make_line(addr, 3, read_fn, buf);
            } else {
                // ADD HL,rp
                std::sprintf(buf, "ADD HL,%s", rp_names[p]);
                return make_line(addr, 1, read_fn, buf);
            }
            break;
        case 2:
            if (q == 0) {
                switch (p) {
                case 0: return make_line(addr, 1, read_fn, "LD (BC),A");
                case 1: return make_line(addr, 1, read_fn, "LD (DE),A");
                case 2: {
                    uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
                    std::sprintf(buf, "LD ($%04X),HL", nn);
                    return make_line(addr, 3, read_fn, buf);
                }
                case 3: {
                    uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
                    std::sprintf(buf, "LD ($%04X),A", nn);
                    return make_line(addr, 3, read_fn, buf);
                }
                }
            } else {
                switch (p) {
                case 0: return make_line(addr, 1, read_fn, "LD A,(BC)");
                case 1: return make_line(addr, 1, read_fn, "LD A,(DE)");
                case 2: {
                    uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
                    std::sprintf(buf, "LD HL,($%04X)", nn);
                    return make_line(addr, 3, read_fn, buf);
                }
                case 3: {
                    uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
                    std::sprintf(buf, "LD A,($%04X)", nn);
                    return make_line(addr, 3, read_fn, buf);
                }
                }
            }
            break;
        case 3:
            if (q == 0) {
                std::sprintf(buf, "INC %s", rp_names[p]);
            } else {
                std::sprintf(buf, "DEC %s", rp_names[p]);
            }
            return make_line(addr, 1, read_fn, buf);
        case 4:
            std::sprintf(buf, "INC %s", r_names[y]);
            return make_line(addr, 1, read_fn, buf);
        case 5:
            std::sprintf(buf, "DEC %s", r_names[y]);
            return make_line(addr, 1, read_fn, buf);
        case 6: {
            // LD r,n
            uint8_t n = read_fn(static_cast<uint16_t>(addr + 1));
            std::sprintf(buf, "LD %s,$%02X", r_names[y], n);
            return make_line(addr, 2, read_fn, buf);
        }
        case 7:
            switch (y) {
            case 0: return make_line(addr, 1, read_fn, "RLCA");
            case 1: return make_line(addr, 1, read_fn, "RRCA");
            case 2: return make_line(addr, 1, read_fn, "RLA");
            case 3: return make_line(addr, 1, read_fn, "RRA");
            case 4: return make_line(addr, 1, read_fn, "DAA");
            case 5: return make_line(addr, 1, read_fn, "CPL");
            case 6: return make_line(addr, 1, read_fn, "SCF");
            case 7: return make_line(addr, 1, read_fn, "CCF");
            }
            break;
        }
        break;

    case 1:
        if (z == 6 && y == 6) {
            return make_line(addr, 1, read_fn, "HALT");
        }
        std::sprintf(buf, "LD %s,%s", r_names[y], r_names[z]);
        return make_line(addr, 1, read_fn, buf);

    case 2:
        // ALU A,r
        std::sprintf(buf, "%s%s", alu_names[y], r_names[z]);
        return make_line(addr, 1, read_fn, buf);

    case 3:
        switch (z) {
        case 0:
            // RET cc
            std::sprintf(buf, "RET %s", cc_names[y]);
            return make_line(addr, 1, read_fn, buf);
        case 1:
            if (q == 0) {
                std::sprintf(buf, "POP %s", rp2_names[p]);
                return make_line(addr, 1, read_fn, buf);
            } else {
                switch (p) {
                case 0: return make_line(addr, 1, read_fn, "RET");
                case 1: return make_line(addr, 1, read_fn, "EXX");
                case 2: return make_line(addr, 1, read_fn, "JP (HL)");
                case 3: return make_line(addr, 1, read_fn, "LD SP,HL");
                }
            }
            break;
        case 2: {
            // JP cc,nn
            uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
            std::sprintf(buf, "JP %s,$%04X", cc_names[y], nn);
            return make_line(addr, 3, read_fn, buf);
        }
        case 3:
            switch (y) {
            case 0: {
                uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
                std::sprintf(buf, "JP $%04X", nn);
                return make_line(addr, 3, read_fn, buf);
            }
            case 1: // CB prefix — handled above, but just in case
                return disasm_cb(addr, read_fn);
            case 2: {
                uint8_t n = read_fn(static_cast<uint16_t>(addr + 1));
                std::sprintf(buf, "OUT ($%02X),A", n);
                return make_line(addr, 2, read_fn, buf);
            }
            case 3: {
                uint8_t n = read_fn(static_cast<uint16_t>(addr + 1));
                std::sprintf(buf, "IN A,($%02X)", n);
                return make_line(addr, 2, read_fn, buf);
            }
            case 4: return make_line(addr, 1, read_fn, "EX (SP),HL");
            case 5: return make_line(addr, 1, read_fn, "EX DE,HL");
            case 6: return make_line(addr, 1, read_fn, "DI");
            case 7: return make_line(addr, 1, read_fn, "EI");
            }
            break;
        case 4: {
            // CALL cc,nn
            uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
            std::sprintf(buf, "CALL %s,$%04X", cc_names[y], nn);
            return make_line(addr, 3, read_fn, buf);
        }
        case 5:
            if (q == 0) {
                std::sprintf(buf, "PUSH %s", rp2_names[p]);
                return make_line(addr, 1, read_fn, buf);
            } else {
                switch (p) {
                case 0: {
                    uint16_t nn = read16(static_cast<uint16_t>(addr + 1), read_fn);
                    std::sprintf(buf, "CALL $%04X", nn);
                    return make_line(addr, 3, read_fn, buf);
                }
                case 1: // DD prefix
                    return disasm_ddfd(addr, read_fn, 0xDD);
                case 2: // ED prefix
                    return disasm_ed(addr, read_fn);
                case 3: // FD prefix
                    return disasm_ddfd(addr, read_fn, 0xFD);
                }
            }
            break;
        case 6: {
            // ALU A,n
            uint8_t n = read_fn(static_cast<uint16_t>(addr + 1));
            std::sprintf(buf, "%s$%02X", alu_names[y], n);
            return make_line(addr, 2, read_fn, buf);
        }
        case 7: {
            // RST y*8
            std::sprintf(buf, "RST $%02X", y * 8);
            return make_line(addr, 1, read_fn, buf);
        }
        }
        break;
    }

    // Fallback: should not reach here
    std::sprintf(buf, "DB $%02X", op);
    return make_line(addr, 1, read_fn, buf);
}

// ---------------------------------------------------------------------------
// CB-prefix disassembly
// ---------------------------------------------------------------------------
static DisasmLine disasm_cb(uint16_t addr, DisasmReadFn const& read_fn) {
    uint8_t op = read_fn(static_cast<uint16_t>(addr + 1));
    char buf[64];

    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;

    switch (x) {
    case 0:
        // Rotation/shift: rot[y] r[z]
        std::sprintf(buf, "%s %s", rot_names[y], r_names[z]);
        break;
    case 1:
        // BIT y,r[z]
        std::sprintf(buf, "BIT %d,%s", y, r_names[z]);
        break;
    case 2:
        // RES y,r[z]
        std::sprintf(buf, "RES %d,%s", y, r_names[z]);
        break;
    case 3:
        // SET y,r[z]
        std::sprintf(buf, "SET %d,%s", y, r_names[z]);
        break;
    }

    return make_line(addr, 2, read_fn, buf);
}

// ---------------------------------------------------------------------------
// ED-prefix disassembly (standard Z80 + Z80N extensions)
// ---------------------------------------------------------------------------
static DisasmLine disasm_ed(uint16_t addr, DisasmReadFn const& read_fn) {
    uint8_t op = read_fn(static_cast<uint16_t>(addr + 1));
    char buf[64];

    // ---- Z80N extensions first (they occupy otherwise-undefined ED space) ----
    switch (op) {
    case 0x23: return make_line(addr, 2, read_fn, "SWAPNIB");
    case 0x24: return make_line(addr, 2, read_fn, "MIRROR A");
    case 0x27: {
        uint8_t n = read_fn(static_cast<uint16_t>(addr + 2));
        std::sprintf(buf, "TEST $%02X", n);
        return make_line(addr, 3, read_fn, buf);
    }
    case 0x28: return make_line(addr, 2, read_fn, "BSLA DE,B");
    case 0x29: return make_line(addr, 2, read_fn, "BSRA DE,B");
    case 0x2A: return make_line(addr, 2, read_fn, "BSRL DE,B");
    case 0x2B: return make_line(addr, 2, read_fn, "BSRF DE,B");
    case 0x2C: return make_line(addr, 2, read_fn, "BRLC DE,B");
    case 0x30: return make_line(addr, 2, read_fn, "MUL D,E");
    case 0x31: return make_line(addr, 2, read_fn, "ADD HL,A");
    case 0x32: return make_line(addr, 2, read_fn, "ADD DE,A");
    case 0x33: return make_line(addr, 2, read_fn, "ADD BC,A");
    case 0x34: {
        uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
        std::sprintf(buf, "ADD HL,$%04X", nn);
        return make_line(addr, 4, read_fn, buf);
    }
    case 0x35: {
        uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
        std::sprintf(buf, "ADD DE,$%04X", nn);
        return make_line(addr, 4, read_fn, buf);
    }
    case 0x36: {
        uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
        std::sprintf(buf, "ADD BC,$%04X", nn);
        return make_line(addr, 4, read_fn, buf);
    }
    case 0x8A: {
        // PUSH nnnn — big-endian: high byte at addr+2, low byte at addr+3
        uint8_t hi = read_fn(static_cast<uint16_t>(addr + 2));
        uint8_t lo = read_fn(static_cast<uint16_t>(addr + 3));
        uint16_t nn = static_cast<uint16_t>((hi << 8) | lo);
        std::sprintf(buf, "PUSH $%04X", nn);
        return make_line(addr, 4, read_fn, buf);
    }
    case 0x90: return make_line(addr, 2, read_fn, "OUTINB");
    case 0x91: {
        uint8_t reg = read_fn(static_cast<uint16_t>(addr + 2));
        uint8_t val = read_fn(static_cast<uint16_t>(addr + 3));
        std::sprintf(buf, "NEXTREG $%02X,$%02X", reg, val);
        return make_line(addr, 4, read_fn, buf);
    }
    case 0x92: {
        uint8_t reg = read_fn(static_cast<uint16_t>(addr + 2));
        std::sprintf(buf, "NEXTREG $%02X,A", reg);
        return make_line(addr, 3, read_fn, buf);
    }
    case 0x93: return make_line(addr, 2, read_fn, "PIXELDN");
    case 0x94: return make_line(addr, 2, read_fn, "PIXELAD");
    case 0x95: return make_line(addr, 2, read_fn, "SETAE");
    case 0x98: return make_line(addr, 2, read_fn, "JP (C)");
    case 0xA4: return make_line(addr, 2, read_fn, "LDIX");
    case 0xA5: return make_line(addr, 2, read_fn, "LDWS");
    case 0xAC: return make_line(addr, 2, read_fn, "LDDX");
    case 0xB4: return make_line(addr, 2, read_fn, "LDIRX");
    case 0xB7: return make_line(addr, 2, read_fn, "LDPIRX");
    case 0xBC: return make_line(addr, 2, read_fn, "LDDRX");
    case 0xC6: return make_line(addr, 2, read_fn, "LOOP");
    }

    // ---- Standard Z80 ED opcodes ----
    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    if (x == 1) {
        switch (z) {
        case 0:
            if (y == 6) {
                return make_line(addr, 2, read_fn, "IN (C)");
            }
            std::sprintf(buf, "IN %s,(C)", r_names[y]);
            return make_line(addr, 2, read_fn, buf);
        case 1:
            if (y == 6) {
                return make_line(addr, 2, read_fn, "OUT (C),0");
            }
            std::sprintf(buf, "OUT (C),%s", r_names[y]);
            return make_line(addr, 2, read_fn, buf);
        case 2:
            if (q == 0) {
                std::sprintf(buf, "SBC HL,%s", rp_names[p]);
            } else {
                std::sprintf(buf, "ADC HL,%s", rp_names[p]);
            }
            return make_line(addr, 2, read_fn, buf);
        case 3:
            if (q == 0) {
                uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
                std::sprintf(buf, "LD ($%04X),%s", nn, rp_names[p]);
                return make_line(addr, 4, read_fn, buf);
            } else {
                uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
                std::sprintf(buf, "LD %s,($%04X)", rp_names[p], nn);
                return make_line(addr, 4, read_fn, buf);
            }
        case 4:
            return make_line(addr, 2, read_fn, "NEG");
        case 5:
            if (y == 1) {
                return make_line(addr, 2, read_fn, "RETI");
            }
            return make_line(addr, 2, read_fn, "RETN");
        case 6:
            switch (y & 3) {
            case 0: return make_line(addr, 2, read_fn, "IM 0");
            case 1: return make_line(addr, 2, read_fn, "IM 0");  // IM 0/1 alias
            case 2: return make_line(addr, 2, read_fn, "IM 1");
            case 3: return make_line(addr, 2, read_fn, "IM 2");
            }
            break;
        case 7:
            switch (y) {
            case 0: return make_line(addr, 2, read_fn, "LD I,A");
            case 1: return make_line(addr, 2, read_fn, "LD R,A");
            case 2: return make_line(addr, 2, read_fn, "LD A,I");
            case 3: return make_line(addr, 2, read_fn, "LD A,R");
            case 4: return make_line(addr, 2, read_fn, "RRD");
            case 5: return make_line(addr, 2, read_fn, "RLD");
            case 6: return make_line(addr, 2, read_fn, "NOP");  // ED 76
            case 7: return make_line(addr, 2, read_fn, "NOP");  // ED 7F
            }
            break;
        }
    } else if (x == 2) {
        // Block instructions: y >= 4, z <= 3
        if (y >= 4 && z <= 3) {
            static const char* const block_names[4][4] = {
                // z=0     z=1     z=2     z=3
                {"LDI",  "CPI",  "INI",  "OUTI" },  // y=4
                {"LDD",  "CPD",  "IND",  "OUTD" },  // y=5
                {"LDIR", "CPIR", "INIR", "OTIR" },  // y=6
                {"LDDR", "CPDR", "INDR", "OTDR" },  // y=7
            };
            return make_line(addr, 2, read_fn, block_names[y - 4][z]);
        }
    }

    // Undefined ED opcode — treat as NOP (Z80 behavior)
    std::sprintf(buf, "DB $ED,$%02X", op);
    return make_line(addr, 2, read_fn, buf);
}

// ---------------------------------------------------------------------------
// DD/FD-prefix disassembly (IX/IY variants)
// ---------------------------------------------------------------------------
static DisasmLine disasm_ddfd(uint16_t addr, DisasmReadFn const& read_fn, uint8_t prefix) {
    const char* ireg = (prefix == 0xDD) ? "IX" : "IY";
    const char* ireg_h = (prefix == 0xDD) ? "IXH" : "IYH";
    const char* ireg_l = (prefix == 0xDD) ? "IXL" : "IYL";

    uint8_t op = read_fn(static_cast<uint16_t>(addr + 1));
    char buf[64];

    // DD CB / FD CB — indexed bit operations (4-byte instructions)
    if (op == 0xCB) {
        int8_t d = static_cast<int8_t>(read_fn(static_cast<uint16_t>(addr + 2)));
        uint8_t cb_op = read_fn(static_cast<uint16_t>(addr + 3));

        uint8_t x = (cb_op >> 6) & 3;
        uint8_t y_val = (cb_op >> 3) & 7;
        uint8_t z = cb_op & 7;

        char disp_str[16];
        if (d >= 0) {
            std::sprintf(disp_str, "(%s+$%02X)", ireg, static_cast<uint8_t>(d));
        } else {
            std::sprintf(disp_str, "(%s-$%02X)", ireg, static_cast<uint8_t>(-d));
        }

        switch (x) {
        case 0:
            if (z == 6) {
                std::sprintf(buf, "%s %s", rot_names[y_val], disp_str);
            } else {
                // Undocumented: result also stored in r[z]
                std::sprintf(buf, "%s %s,%s", rot_names[y_val], disp_str, r_names[z]);
            }
            break;
        case 1:
            std::sprintf(buf, "BIT %d,%s", y_val, disp_str);
            break;
        case 2:
            if (z == 6) {
                std::sprintf(buf, "RES %d,%s", y_val, disp_str);
            } else {
                std::sprintf(buf, "RES %d,%s,%s", y_val, disp_str, r_names[z]);
            }
            break;
        case 3:
            if (z == 6) {
                std::sprintf(buf, "SET %d,%s", y_val, disp_str);
            } else {
                std::sprintf(buf, "SET %d,%s,%s", y_val, disp_str, r_names[z]);
            }
            break;
        }

        return make_line(addr, 4, read_fn, buf);
    }

    // Helper: format displacement string for (IX+d)/(IY+d)
    auto fmt_disp = [&](uint16_t disp_addr, char* out) {
        int8_t d = static_cast<int8_t>(read_fn(disp_addr));
        if (d >= 0) {
            std::sprintf(out, "(%s+$%02X)", ireg, static_cast<uint8_t>(d));
        } else {
            std::sprintf(out, "(%s-$%02X)", ireg, static_cast<uint8_t>(-d));
        }
    };

    // Decode using x/y/z decomposition
    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    // Helper to get register name with IX/IY substitutions
    // H→IXH/IYH, L→IXL/IYL, (HL) stays as (IX+d)/(IY+d)
    auto r_name_ix = [&](uint8_t r) -> const char* {
        if (r == 4) return ireg_h;
        if (r == 5) return ireg_l;
        return r_names[r]; // Note: (HL) case (r==6) handled specially
    };

    // Helper to get register pair name with HL→IX/IY substitution
    auto rp_name_ix = [&](uint8_t rp_idx) -> const char* {
        if (rp_idx == 2) return ireg; // HL → IX/IY
        return rp_names[rp_idx];
    };

    switch (x) {
    case 0:
        switch (z) {
        case 1:
            if (q == 0) {
                // LD rp,nn (with HL→IX/IY)
                uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
                std::sprintf(buf, "LD %s,$%04X", rp_name_ix(p), nn);
                return make_line(addr, 4, read_fn, buf);
            } else {
                // ADD IX/IY,rp
                std::sprintf(buf, "ADD %s,%s", ireg, rp_name_ix(p));
                return make_line(addr, 2, read_fn, buf);
            }
        case 2:
            if (q == 0 && p == 2) {
                // LD (nn),IX/IY
                uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
                std::sprintf(buf, "LD ($%04X),%s", nn, ireg);
                return make_line(addr, 4, read_fn, buf);
            } else if (q == 1 && p == 2) {
                // LD IX/IY,(nn)
                uint16_t nn = read16(static_cast<uint16_t>(addr + 2), read_fn);
                std::sprintf(buf, "LD %s,($%04X)", ireg, nn);
                return make_line(addr, 4, read_fn, buf);
            }
            break;
        case 3:
            if (p == 2) {
                if (q == 0) {
                    std::sprintf(buf, "INC %s", ireg);
                } else {
                    std::sprintf(buf, "DEC %s", ireg);
                }
                return make_line(addr, 2, read_fn, buf);
            }
            break;
        case 4:
            // INC r (with H→IXH, L→IXL, (HL)→(IX+d))
            if (y == 6) {
                char disp_str[16];
                fmt_disp(static_cast<uint16_t>(addr + 2), disp_str);
                std::sprintf(buf, "INC %s", disp_str);
                return make_line(addr, 3, read_fn, buf);
            } else if (y == 4 || y == 5) {
                std::sprintf(buf, "INC %s", r_name_ix(y));
                return make_line(addr, 2, read_fn, buf);
            }
            break;
        case 5:
            // DEC r (same substitutions)
            if (y == 6) {
                char disp_str[16];
                fmt_disp(static_cast<uint16_t>(addr + 2), disp_str);
                std::sprintf(buf, "DEC %s", disp_str);
                return make_line(addr, 3, read_fn, buf);
            } else if (y == 4 || y == 5) {
                std::sprintf(buf, "DEC %s", r_name_ix(y));
                return make_line(addr, 2, read_fn, buf);
            }
            break;
        case 6:
            // LD r,n (with substitutions)
            if (y == 6) {
                // LD (IX+d),n — displacement at addr+2, immediate at addr+3
                char disp_str[16];
                fmt_disp(static_cast<uint16_t>(addr + 2), disp_str);
                uint8_t n = read_fn(static_cast<uint16_t>(addr + 3));
                std::sprintf(buf, "LD %s,$%02X", disp_str, n);
                return make_line(addr, 4, read_fn, buf);
            } else if (y == 4 || y == 5) {
                uint8_t n = read_fn(static_cast<uint16_t>(addr + 2));
                std::sprintf(buf, "LD %s,$%02X", r_name_ix(y), n);
                return make_line(addr, 3, read_fn, buf);
            }
            break;
        }
        // Fall through to treating as NOP (prefix ignored)
        break;

    case 1:
        // LD r,r' with IX/IY substitutions
        // When either src or dst is (HL), it becomes (IX+d)
        if (y == 6 && z == 6) {
            // HALT — prefix is ignored
            return make_line(addr, 2, read_fn, "HALT");
        }
        if (y == 6) {
            // LD (IX+d),r
            char disp_str[16];
            fmt_disp(static_cast<uint16_t>(addr + 2), disp_str);
            std::sprintf(buf, "LD %s,%s", disp_str, r_names[z]); // src uses plain register
            return make_line(addr, 3, read_fn, buf);
        }
        if (z == 6) {
            // LD r,(IX+d)
            char disp_str[16];
            fmt_disp(static_cast<uint16_t>(addr + 2), disp_str);
            std::sprintf(buf, "LD %s,%s", r_names[y], disp_str); // dst uses plain register
            return make_line(addr, 3, read_fn, buf);
        }
        // Both are non-(HL) registers — H/L get substituted
        {
            const char* dst = r_name_ix(y);
            const char* src = r_name_ix(z);
            // Special: if both are H/L variants, both get substituted (undocumented)
            // But if one is H/L and the other isn't, the non-H/L stays plain
            // r_name_ix already handles this correctly
            std::sprintf(buf, "LD %s,%s", dst, src);
            return make_line(addr, 2, read_fn, buf);
        }

    case 2:
        // ALU A,r with IX/IY substitutions
        if (z == 6) {
            char disp_str[16];
            fmt_disp(static_cast<uint16_t>(addr + 2), disp_str);
            std::sprintf(buf, "%s%s", alu_names[y], disp_str);
            return make_line(addr, 3, read_fn, buf);
        }
        if (z == 4 || z == 5) {
            std::sprintf(buf, "%s%s", alu_names[y], r_name_ix(z));
            return make_line(addr, 2, read_fn, buf);
        }
        // Other registers: prefix ignored, 2-byte instruction
        std::sprintf(buf, "%s%s", alu_names[y], r_names[z]);
        return make_line(addr, 2, read_fn, buf);

    case 3:
        switch (z) {
        case 1:
            if (q == 0 && p == 2) {
                std::sprintf(buf, "POP %s", ireg);
                return make_line(addr, 2, read_fn, buf);
            }
            if (q == 1) {
                switch (p) {
                case 2:
                    std::sprintf(buf, "JP (%s)", ireg);
                    return make_line(addr, 2, read_fn, buf);
                case 3:
                    std::sprintf(buf, "LD SP,%s", ireg);
                    return make_line(addr, 2, read_fn, buf);
                }
            }
            break;
        case 3:
            if (y == 4) {
                std::sprintf(buf, "EX (SP),%s", ireg);
                return make_line(addr, 2, read_fn, buf);
            }
            break;
        case 5:
            if (q == 0 && p == 2) {
                std::sprintf(buf, "PUSH %s", ireg);
                return make_line(addr, 2, read_fn, buf);
            }
            break;
        }
        break;
    }

    // If we reach here, the DD/FD prefix is ignored and the opcode is treated
    // as its unprefixed equivalent. Disassemble from addr+1 and adjust.
    DisasmLine inner = disasm_one(static_cast<uint16_t>(addr + 1), read_fn);
    // Rebuild with the prefix byte included
    DisasmLine result;
    result.addr = addr;
    result.byte_count = inner.byte_count + 1;
    if (result.byte_count > 4) result.byte_count = 4; // safety clamp
    result.bytes[0] = prefix;
    for (int i = 0; i < inner.byte_count && i < 3; i++) {
        result.bytes[i + 1] = inner.bytes[i];
    }
    std::strncpy(result.mnemonic, inner.mnemonic, sizeof(result.mnemonic) - 1);
    result.mnemonic[sizeof(result.mnemonic) - 1] = '\0';
    return result;
}

// ---------------------------------------------------------------------------
// instruction_length — fast length calculation without full formatting
// ---------------------------------------------------------------------------
int instruction_length(uint16_t addr, DisasmReadFn read_fn) {
    uint8_t op = read_fn(addr);

    if (op == 0xCB) return 2;

    if (op == 0xED) {
        uint8_t ed_op = read_fn(static_cast<uint16_t>(addr + 1));
        // Z80N 4-byte instructions
        if (ed_op == 0x34 || ed_op == 0x35 || ed_op == 0x36 ||
            ed_op == 0x8A || ed_op == 0x91) return 4;
        // Z80N 3-byte instructions
        if (ed_op == 0x27 || ed_op == 0x92) return 3;
        // Standard ED 4-byte: LD (nn),rp / LD rp,(nn)
        uint8_t x = (ed_op >> 6) & 3;
        uint8_t z = ed_op & 7;
        if (x == 1 && z == 3) return 4;
        return 2;
    }

    if (op == 0xDD || op == 0xFD) {
        uint8_t next = read_fn(static_cast<uint16_t>(addr + 1));
        if (next == 0xCB) return 4; // DD CB d op / FD CB d op

        // Reuse the full disassembler for accuracy on DD/FD
        // (DD/FD prefix can be ignored for some opcodes)
        DisasmLine line = disasm_ddfd(addr, read_fn, op);
        return line.byte_count;
    }

    // Unprefixed opcodes
    uint8_t x = (op >> 6) & 3;
    uint8_t y = (op >> 3) & 7;
    uint8_t z = op & 7;
    uint8_t p = (y >> 1) & 3;
    uint8_t q = y & 1;

    switch (x) {
    case 0:
        switch (z) {
        case 0:
            if (y == 0 || y == 1) return 1;
            return 2; // DJNZ, JR, JR cc
        case 1:
            return (q == 0) ? 3 : 1; // LD rp,nn vs ADD HL,rp
        case 2:
            if ((q == 0 && p >= 2) || (q == 1 && p >= 2)) return 3; // LD (nn),HL etc.
            return 1;
        case 3: return 1;
        case 4: return 1;
        case 5: return 1;
        case 6: return 2; // LD r,n
        case 7: return 1;
        }
        break;
    case 1: return 1;
    case 2: return 1;
    case 3:
        switch (z) {
        case 0: return 1;
        case 1: return 1;
        case 2: return 3; // JP cc,nn
        case 3:
            switch (y) {
            case 0: return 3; // JP nn
            case 2: return 2; // OUT (n),A
            case 3: return 2; // IN A,(n)
            default: return 1;
            }
        case 4: return 3; // CALL cc,nn
        case 5:
            if (q == 0) return 1; // PUSH
            if (p == 0) return 3; // CALL nn
            return 1;
        case 6: return 2; // ALU A,n
        case 7: return 1; // RST
        }
        break;
    }

    return 1; // fallback
}

// ---------------------------------------------------------------------------
// is_call_like — CALL nn, CALL cc,nn, RST n, DJNZ
// ---------------------------------------------------------------------------
bool is_call_like(uint16_t addr, DisasmReadFn read_fn) {
    uint8_t op = read_fn(addr);

    // DJNZ
    if (op == 0x10) return true;

    // CALL nn
    if (op == 0xCD) return true;

    // CALL cc,nn: 11 ccc 100
    if ((op & 0xC7) == 0xC4) return true;

    // RST n: 11 ttt 111
    if ((op & 0xC7) == 0xC7) return true;

    return false;
}

// ---------------------------------------------------------------------------
// is_ret_like — RET, RET cc, RETI, RETN
// ---------------------------------------------------------------------------
bool is_ret_like(uint16_t addr, DisasmReadFn read_fn) {
    uint8_t op = read_fn(addr);

    // RET
    if (op == 0xC9) return true;

    // RET cc: 11 ccc 000
    if ((op & 0xC7) == 0xC0) return true;

    // ED prefix: RETI (ED 4D) or RETN (ED 45)
    if (op == 0xED) {
        uint8_t ed_op = read_fn(static_cast<uint16_t>(addr + 1));
        if (ed_op == 0x4D || ed_op == 0x45) return true;
        // Also catch undocumented RETN variants: ED 55, ED 5D, ED 65, ED 6D, ED 75, ED 7D
        if ((ed_op & 0xC7) == 0x45) return true;
    }

    return false;
}
