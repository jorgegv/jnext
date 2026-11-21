#include "debug/nextreg_names.h"

// The NextREG name table the debugger's register panel always had, moved out of
// the Qt panel so the bug report writer (and any non-Qt build) can name a register.

namespace jnext {
namespace dbg {

const char* nextreg_name(uint8_t reg) {
    switch (reg) {
        case 0x00: return "Machine ID";
        case 0x01: return "Core Version";
        case 0x02: return "Reset";
        case 0x03: return "Machine Type";
        case 0x05: return "Peripheral 1";
        case 0x06: return "Peripheral 2";
        case 0x07: return "CPU Speed";
        case 0x08: return "Peripheral 3";
        case 0x09: return "Peripheral 4";
        case 0x0A: return "Peripheral 5";
        case 0x0B: return "Joystick";
        case 0x0E: return "Core Boot";
        case 0x10: return "Mouse Config";
        case 0x11: return "Video Timing";
        case 0x12: return "L2 Active Bank";
        case 0x13: return "L2 Shadow Bank";
        case 0x14: return "Transparency Col";
        case 0x15: return "Spr/Layer Prior";
        case 0x16: return "L2 X Scroll LSB";
        case 0x17: return "L2 Y Scroll";
        case 0x18: return "Clip L2";
        case 0x19: return "Clip Sprite";
        case 0x1A: return "Clip ULA";
        case 0x1B: return "Clip Tilemap";
        case 0x1C: return "Clip Control";
        case 0x1E: return "Line IRQ Ctrl";
        case 0x1F: return "Line IRQ Value";
        case 0x22: return "Line IRQ Enable";
        case 0x23: return "Line IRQ Line";
        case 0x2F: return "TM X Scroll MSB";
        case 0x30: return "TM X Scroll LSB";
        case 0x31: return "TM Y Scroll";
        case 0x34: return "Sprite Slot Sel";
        case 0x35: return "Sprite Attr 0";
        case 0x36: return "Sprite Attr 1";
        case 0x37: return "Sprite Attr 2";
        case 0x38: return "Sprite Attr 3";
        case 0x39: return "Sprite Attr 4";
        case 0x40: return "Palette Index";
        case 0x41: return "Palette Val 8b";
        case 0x43: return "Palette Control";
        case 0x44: return "Palette Val 9b";
        case 0x4A: return "Fallback Colour";
        case 0x4B: return "Spr Transp Idx";
        case 0x4C: return "TM Transp Idx";
        case 0x50: return "MMU Slot 0";
        case 0x51: return "MMU Slot 1";
        case 0x52: return "MMU Slot 2";
        case 0x53: return "MMU Slot 3";
        case 0x54: return "MMU Slot 4";
        case 0x55: return "MMU Slot 5";
        case 0x56: return "MMU Slot 6";
        case 0x57: return "MMU Slot 7";
        case 0x60: return "Copper Data";
        case 0x61: return "Copper Addr Lo";
        case 0x62: return "Copper Control";
        case 0x63: return "Copper Data Hi";
        case 0x68: return "ULA Control";
        case 0x69: return "Display Ctrl 1";
        case 0x6A: return "Display Ctrl 2";
        case 0x6B: return "TM Control";
        case 0x6C: return "TM Def Attr";
        case 0x6E: return "TM Base Addr";
        case 0x6F: return "TM Pat Base";
        case 0x70: return "L2 Control";
        case 0x71: return "L2 X Scroll MSB";
        case 0x75: return "Sprite Attr 0*";
        case 0x76: return "Sprite Attr 1*";
        case 0x77: return "Sprite Attr 2*";
        case 0x78: return "Sprite Attr 3*";
        case 0x79: return "Sprite Attr 4*";
        case 0x7F: return "User Register";
        case 0x80: return "Exp Bus Enable";
        case 0x81: return "Exp Bus Ctrl";
        case 0x8A: return "Exp Bus I/O";
        case 0x8C: return "Alt ROM Ctrl";
        case 0xB8: return "DivMMC Trap 0";
        case 0xB9: return "DivMMC Trap 1";
        case 0xBA: return "DivMMC Trap 2";
        case 0xBB: return "DivMMC Trap 3";
        case 0xC0: return "IM2 Control";
        case 0xC4: return "IM2 Int En 0";
        case 0xC5: return "IM2 Int En 1";
        case 0xC6: return "IM2 Int En 2";
        case 0xC8: return "IM2 Int Stat 0";
        case 0xC9: return "IM2 Int Stat 1";
        case 0xCA: return "IM2 Int Stat 2";
        default:   return "---";
    }
}

}  // namespace dbg
}  // namespace jnext
